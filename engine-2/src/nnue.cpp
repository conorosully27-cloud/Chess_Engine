#include "nnue.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace chess::nnue {

namespace {

constexpr int INPUTS = 768;
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 400;

struct Network {
  alignas(64) std::int16_t ft_weight[INPUTS][HIDDEN];
  alignas(64) std::int16_t ft_bias[HIDDEN];
  alignas(64) std::int16_t out_weight[2 * HIDDEN];  // side to move's units first
  std::int32_t out_bias;
};

Network net;
bool have_net = false;
// Changes with every network loaded, so accumulators computed with an
// earlier one read as not computed. Never 0 (Accumulator's "none").
std::uint32_t net_id = 0;

// Reads `size` bytes into dst from data + *pos.
bool take(const unsigned char* data, std::size_t size, std::size_t& pos, void* dst, std::size_t n) {
  if (pos + n > size) return false;
  std::memcpy(dst, data + pos, n);
  pos += n;
  return true;
}

// The feature for a piece on a square, seen from White's or Black's side:
// from Black's side the colors swap and the board flips.
inline int feature(Piece pc, Square s, Color side) {
  return side == WHITE ? pc * 64 + s : ((pc + 6) % 12) * 64 + (s ^ 56);
}

inline const std::int16_t* weights(Piece pc, Square s, Color side) { return net.ft_weight[feature(pc, s, side)]; }

// The update loops. Sums wrap like the 16-bit lanes they vectorize to; the
// trainer's weight limit keeps every finished accumulator in range, so the
// result is exactly a from-scratch computation's.
using Row = const std::int16_t* __restrict;

inline void add1_sub1(std::int16_t* __restrict out, Row in, Row a, Row r) {
  for (int j = 0; j < HIDDEN; j++) out[j] = std::int16_t(in[j] + a[j] - r[j]);
}
inline void add1_sub2(std::int16_t* __restrict out, Row in, Row a, Row r1, Row r2) {
  for (int j = 0; j < HIDDEN; j++) out[j] = std::int16_t(in[j] + a[j] - r1[j] - r2[j]);
}
inline void add2_sub1(std::int16_t* __restrict out, Row in, Row a1, Row a2, Row r) {
  for (int j = 0; j < HIDDEN; j++) out[j] = std::int16_t(in[j] + a1[j] + a2[j] - r[j]);
}
inline void add2_sub2(std::int16_t* __restrict out, Row in, Row a1, Row a2, Row r1, Row r2) {
  for (int j = 0; j < HIDDEN; j++) out[j] = std::int16_t(in[j] + a1[j] + a2[j] - r1[j] - r2[j]);
}

// One side's hidden units, clipped to [0, QA], times their output weights.
// A plain dot product of 16-bit values, which compilers vectorize
// (pmaddwd on x86, i32x4.dot_i16x8_s in WebAssembly).
inline std::int32_t output(Row units, Row w) {
  std::int32_t sum = 0;
  for (int j = 0; j < HIDDEN; j++) {
    const std::int16_t u = std::min<std::int16_t>(std::max<std::int16_t>(units[j], 0), QA);
    sum += u * w[j];
  }
  return sum;
}

// next = prev changed by next.dirty, from both sides.
void apply(const Accumulator& prev, Accumulator& next) {
  const DirtyPieces& d = next.dirty;
  for (Color side : {WHITE, BLACK}) {
    const std::int16_t* in = prev.values[side];
    std::int16_t* out = next.values[side];
    if (d.removed == 1) {  // a quiet move or a promotion
      add1_sub1(out, in, weights(d.add_piece[0], d.add_sq[0], side), weights(d.remove_piece[0], d.remove_sq[0], side));
    } else if (d.added == 1) {  // a capture
      add1_sub2(out, in, weights(d.add_piece[0], d.add_sq[0], side), weights(d.remove_piece[0], d.remove_sq[0], side),
                weights(d.remove_piece[1], d.remove_sq[1], side));
    } else if (d.added == 2) {  // castling
      add2_sub2(out, in, weights(d.add_piece[0], d.add_sq[0], side), weights(d.add_piece[1], d.add_sq[1], side),
                weights(d.remove_piece[0], d.remove_sq[0], side), weights(d.remove_piece[1], d.remove_sq[1], side));
    } else {  // a null move
      std::memcpy(out, in, sizeof(prev.values[side]));
    }
  }
  next.net = net_id;
}

// prev = next with next.dirty undone, from both sides: the pieces it added
// come off and the ones it removed go back.
void unapply(const Accumulator& next, Accumulator& prev) {
  const DirtyPieces& d = next.dirty;
  for (Color side : {WHITE, BLACK}) {
    const std::int16_t* in = next.values[side];
    std::int16_t* out = prev.values[side];
    if (d.removed == 1) {
      add1_sub1(out, in, weights(d.remove_piece[0], d.remove_sq[0], side), weights(d.add_piece[0], d.add_sq[0], side));
    } else if (d.added == 1) {
      add2_sub1(out, in, weights(d.remove_piece[0], d.remove_sq[0], side), weights(d.remove_piece[1], d.remove_sq[1], side),
                weights(d.add_piece[0], d.add_sq[0], side));
    } else if (d.added == 2) {
      add2_sub2(out, in, weights(d.remove_piece[0], d.remove_sq[0], side), weights(d.remove_piece[1], d.remove_sq[1], side),
                weights(d.add_piece[0], d.add_sq[0], side), weights(d.add_piece[1], d.add_sq[1], side));
    } else {
      std::memcpy(out, in, sizeof(next.values[side]));
    }
  }
  prev.net = net_id;
}

void refresh(const Position& pos, Accumulator& acc) {
  std::memcpy(acc.values[WHITE], net.ft_bias, sizeof(net.ft_bias));
  std::memcpy(acc.values[BLACK], net.ft_bias, sizeof(net.ft_bias));
  for (Bitboard b = pos.pieces(); b;) {
    const Square s = pop_lsb(b);
    const Piece pc = pos.piece_on(s);
    const std::int16_t* w = weights(pc, s, WHITE);
    const std::int16_t* k = weights(pc, s, BLACK);
    for (int j = 0; j < HIDDEN; j++) {
      acc.values[WHITE][j] += w[j];
      acc.values[BLACK][j] += k[j];
    }
  }
  acc.net = net_id;
}

// Brings the current position's accumulator up to date: forward from the
// nearest computed one before it, or from scratch when the updates on the
// way would add and remove more pieces than the board holds. After a
// refresh the positions before it that were passed over are filled in
// backward, one update each: otherwise every sibling of this position
// (and their children) would find no computed parent and refresh too.
void update(const Position& pos) {
  Accumulator* stack = pos.accumulators();
  const int top = pos.game_ply();
  const int limit = popcount(pos.pieces());
  int i = top, cost = 0;
  while (stack[i].net != net_id) {
    cost += stack[i].dirty.added + stack[i].dirty.removed;
    if (i == 0 || cost > limit) {
      refresh(pos, stack[top]);
      for (int j = top - 1; j >= i; j--) unapply(stack[j + 1], stack[j]);
      return;
    }
    i--;
  }
  for (int j = i + 1; j <= top; j++) apply(stack[j - 1], stack[j]);
}

}  // namespace

extern const unsigned char default_net_data[];
extern const std::size_t default_net_size;

bool load(const unsigned char* data, std::size_t size) {
  have_net = false;
  char magic[4];
  std::uint32_t version = 0, hidden = 0;
  std::size_t pos = 0;
  if (!take(data, size, pos, magic, 4) || !take(data, size, pos, &version, 4) || !take(data, size, pos, &hidden, 4))
    return false;
  if (std::memcmp(magic, "E2NN", 4) != 0 || version != 1 || hidden != HIDDEN) return false;
  if (!take(data, size, pos, net.ft_weight, sizeof(net.ft_weight)) ||
      !take(data, size, pos, net.ft_bias, sizeof(net.ft_bias)) ||
      !take(data, size, pos, net.out_weight, sizeof(net.out_weight)) ||
      !take(data, size, pos, &net.out_bias, sizeof(net.out_bias)) || pos != size)
    return false;
  if (++net_id == 0) net_id = 1;
  have_net = true;
  return true;
}

bool load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    have_net = false;
    return false;
  }
  const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return load(bytes.data(), bytes.size());
}

void load_default() { load(default_net_data, default_net_size); }

void unload() { have_net = false; }
bool loaded() { return have_net; }

int evaluate(const Position& pos) {
  const Accumulator& acc = pos.accumulators()[pos.game_ply()];
  if (acc.net != net_id) update(pos);
  // Accumulators fit in 16 bits: the trainer keeps first-layer weights and
  // biases within 1.98, so 33 terms of at most 1.98 * QA stay under 32767.
  const Color us = pos.side_to_move();
  const std::int32_t sum = output(acc.values[us], net.out_weight) + output(acc.values[~us], net.out_weight + HIDDEN);
  return int((std::int64_t(sum) + net.out_bias) * SCALE / (QA * QB));
}

}  // namespace chess::nnue
