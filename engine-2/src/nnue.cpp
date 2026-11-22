#include "nnue.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#if defined(__AVX2__) || defined(__AVX512BW__)
#include <immintrin.h>
#endif

namespace chess::nnue {

namespace {

constexpr int INPUTS = 768;
constexpr int QA = 255;
constexpr int QB = 64;
constexpr int SCALE = 400;

constexpr int MAX_BUCKETS = 8;
constexpr int MAX_KING_BUCKETS = 16;

struct Network {
  // First layer: one block of INPUTS rows per king bucket (one block without
  // king buckets), each row HIDDEN weights.
  std::vector<std::int16_t> ft_weight;
  alignas(64) std::int16_t ft_bias[HIDDEN];
  // One output layer per bucket (only the first `buckets` are used); in
  // each, the side to move's units first.
  alignas(64) std::int16_t out_weight[MAX_BUCKETS][2 * HIDDEN];
  std::int32_t out_bias[MAX_BUCKETS];
  int buckets = 1;      // output layers, chosen by piece count
  bool screlu = false;  // the clipped units are squared before the output layer
  // King buckets (network file version 3): each side's inputs depend on its
  // own king's square, seen from that side: king_map gives the bucket, and
  // with `mirror` a king on files e-h flips every square left-right (so the
  // map only needs the a-d half). One bucket and no mirror = plain inputs.
  int king_buckets = 1;
  bool mirror = false;
  std::uint8_t king_map[64] = {};
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

// The side's king state: its bucket times 2, plus 1 if the board is mirrored.
// The inputs (and so the update) depend on it; while it stays the same the
// side's accumulator is updated move by move.
inline int king_state(Color side, Square king) {
  const int rel = side == WHITE ? king : king ^ 56;
  return net.king_map[rel] * 2 + (net.mirror && file_of(Square(rel)) >= 4);
}

inline const std::int16_t* weights(Piece pc, Square s, Color side, int state) {
  const int f = feature(pc, s, side) ^ (state & 1 ? 7 : 0);  // mirrored: the file flips (bits 0-2 of the square)
  return &net.ft_weight[std::size_t((state >> 1) * INPUTS + f) * HIDDEN];
}

// The update loops: out = in + the added rows - the removed rows. Sums wrap
// like the 16-bit lanes they vectorize to; the trainer's weight limit keeps
// every finished accumulator in range, so the result is exactly a
// from-scratch computation's.
using Row = const std::int16_t* __restrict;

#if defined(__AVX512BW__)
// AVX-512 (ENGINE_AVX512 builds): 64 units a step. Same results.
template <int Adds, int Subs>
inline void add_sub(std::int16_t* __restrict out, Row in, const std::int16_t* const (&rows)[Adds + Subs]) {
  for (int j = 0; j < HIDDEN; j += 64) {
    __m512i v0 = _mm512_loadu_si512(in + j);
    __m512i v1 = _mm512_loadu_si512(in + j + 32);
    for (int k = 0; k < Adds + Subs; k++) {
      const __m512i w0 = _mm512_loadu_si512(rows[k] + j);
      const __m512i w1 = _mm512_loadu_si512(rows[k] + j + 32);
      v0 = k < Adds ? _mm512_add_epi16(v0, w0) : _mm512_sub_epi16(v0, w0);
      v1 = k < Adds ? _mm512_add_epi16(v1, w1) : _mm512_sub_epi16(v1, w1);
    }
    _mm512_storeu_si512(out + j, v0);
    _mm512_storeu_si512(out + j + 32, v1);
  }
}
#elif defined(__AVX2__)
// Written out with AVX2, 32 units a step: the compiler's own vectorization
// of the plain loops below steps 16 at a time, and at 512 units the loop
// overhead was a sizeable share of the network's cost. Same results.
template <int Adds, int Subs>
inline void add_sub(std::int16_t* __restrict out, Row in, const std::int16_t* const (&rows)[Adds + Subs]) {
  for (int j = 0; j < HIDDEN; j += 32) {
    __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + j));
    __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + j + 16));
    for (int k = 0; k < Adds + Subs; k++) {
      const __m256i w0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[k] + j));
      const __m256i w1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[k] + j + 16));
      v0 = k < Adds ? _mm256_add_epi16(v0, w0) : _mm256_sub_epi16(v0, w0);
      v1 = k < Adds ? _mm256_add_epi16(v1, w1) : _mm256_sub_epi16(v1, w1);
    }
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + j), v0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + j + 16), v1);
  }
}
#else
template <int Adds, int Subs>
inline void add_sub(std::int16_t* __restrict out, Row in, const std::int16_t* const (&rows)[Adds + Subs]) {
  for (int j = 0; j < HIDDEN; j++) {
    int v = in[j];
    for (int k = 0; k < Adds + Subs; k++) v += k < Adds ? rows[k][j] : -rows[k][j];
    out[j] = std::int16_t(v);
  }
}
#endif

inline void add1_sub1(std::int16_t* __restrict out, Row in, Row a, Row r) { add_sub<1, 1>(out, in, {a, r}); }
inline void add1_sub2(std::int16_t* __restrict out, Row in, Row a, Row r1, Row r2) { add_sub<1, 2>(out, in, {a, r1, r2}); }
inline void add2_sub1(std::int16_t* __restrict out, Row in, Row a1, Row a2, Row r) { add_sub<2, 1>(out, in, {a1, a2, r}); }
inline void add2_sub2(std::int16_t* __restrict out, Row in, Row a1, Row a2, Row r1, Row r2) {
  add_sub<2, 2>(out, in, {a1, a2, r1, r2});
}

// The output layer: both sides' hidden units, clipped to [0, QA], times
// their output weights (side to move first), summed.
#if defined(__AVX2__)
inline std::int32_t output(Row us, Row them, Row w) {
  const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
  __m256i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 16) {
    const __m256i u = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(us + j)), zero), qa);
    const __m256i t = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(them + j)), zero), qa);
    s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(u, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + j))));
    s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(t, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + HIDDEN + j))));
  }
  __m256i s = _mm256_add_epi32(s0, s1);
  __m128i h = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0x4E));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0xB1));
  return _mm_cvtsi128_si32(h);
}
#else
// A plain dot product of 16-bit values, which compilers vectorize
// (pmaddwd on x86, i32x4.dot_i16x8_s in WebAssembly).
inline std::int32_t output(Row us, Row them, Row w) {
  std::int32_t sum = 0;
  for (int j = 0; j < HIDDEN; j++) {
    sum += std::min<std::int16_t>(std::max<std::int16_t>(us[j], 0), QA) * w[j];
    sum += std::min<std::int16_t>(std::max<std::int16_t>(them[j], 0), QA) * w[HIDDEN + j];
  }
  return sum;
}
#endif

// The same with the clipped units squared (SCReLU): unit x unit x weight.
// unit x weight is formed first and fits 16 bits (the trainer keeps these
// output weights within +/-127), so the 16-bit multiply-add still applies.
// The sum is in units of QA x QA x QB.
#if defined(__AVX512BW__)
inline std::int32_t output_squared(Row us, Row them, Row w) {
  const __m512i zero = _mm512_setzero_si512(), qa = _mm512_set1_epi16(QA);
  __m512i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 32) {
    const __m512i u = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512(us + j), zero), qa);
    const __m512i t = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512(them + j), zero), qa);
    s0 = _mm512_add_epi32(s0, _mm512_madd_epi16(_mm512_mullo_epi16(u, _mm512_loadu_si512(w + j)), u));
    s1 = _mm512_add_epi32(s1, _mm512_madd_epi16(_mm512_mullo_epi16(t, _mm512_loadu_si512(w + HIDDEN + j)), t));
  }
  return _mm512_reduce_add_epi32(_mm512_add_epi32(s0, s1));
}
#elif defined(__AVX2__)
inline std::int32_t output_squared(Row us, Row them, Row w) {
  const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
  __m256i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 16) {
    const __m256i u = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(us + j)), zero), qa);
    const __m256i t = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(them + j)), zero), qa);
    const __m256i uw = _mm256_mullo_epi16(u, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + j)));
    const __m256i tw = _mm256_mullo_epi16(t, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + HIDDEN + j)));
    s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(uw, u));
    s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(tw, t));
  }
  __m256i s = _mm256_add_epi32(s0, s1);
  __m128i h = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0x4E));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0xB1));
  return _mm_cvtsi128_si32(h);
}
#else
inline std::int32_t output_squared(Row us, Row them, Row w) {
  std::int32_t sum = 0;
  for (int j = 0; j < HIDDEN; j++) {
    const std::int32_t u = std::min<std::int16_t>(std::max<std::int16_t>(us[j], 0), QA);
    const std::int32_t t = std::min<std::int16_t>(std::max<std::int16_t>(them[j], 0), QA);
    sum += std::int16_t(u * w[j]) * u;
    sum += std::int16_t(t * w[HIDDEN + j]) * t;
  }
  return sum;
}
#endif

// next = prev changed by next.dirty, from one side, whose king state
// (king_state) is the same in both.
void apply(const Accumulator& prev, Accumulator& next, Color side, int st) {
  const DirtyPieces& d = next.dirty;
  const std::int16_t* in = prev.values[side];
  std::int16_t* out = next.values[side];
  if (d.removed == 1) {  // a quiet move or a promotion
    add1_sub1(out, in, weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.remove_piece[0], d.remove_sq[0], side, st));
  } else if (d.added == 1) {  // a capture
    add1_sub2(out, in, weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.remove_piece[0], d.remove_sq[0], side, st),
              weights(d.remove_piece[1], d.remove_sq[1], side, st));
  } else if (d.added == 2) {  // castling
    add2_sub2(out, in, weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.add_piece[1], d.add_sq[1], side, st),
              weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.remove_piece[1], d.remove_sq[1], side, st));
  } else {  // a null move
    std::memcpy(out, in, sizeof(prev.values[side]));
  }
  next.net[side] = net_id;
}

// prev = next with next.dirty undone, from one side: the pieces it added
// come off and the ones it removed go back.
void unapply(const Accumulator& next, Accumulator& prev, Color side, int st) {
  const DirtyPieces& d = next.dirty;
  const std::int16_t* in = next.values[side];
  std::int16_t* out = prev.values[side];
  if (d.removed == 1) {
    add1_sub1(out, in, weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.add_piece[0], d.add_sq[0], side, st));
  } else if (d.added == 1) {
    add2_sub1(out, in, weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.remove_piece[1], d.remove_sq[1], side, st),
              weights(d.add_piece[0], d.add_sq[0], side, st));
  } else if (d.added == 2) {
    add2_sub2(out, in, weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.remove_piece[1], d.remove_sq[1], side, st),
              weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.add_piece[1], d.add_sq[1], side, st));
  } else {
    std::memcpy(out, in, sizeof(next.values[side]));
  }
  prev.net[side] = net_id;
}

// Accumulator cache ("Finny table"), for king buckets: per side and king
// state, the sums last computed there and the pieces they were for. A side
// whose king changes zone (or crosses between files d and e) needs its
// sums for other inputs; starting from the cached ones only the pieces that
// changed since are added or removed, instead of all of them. One per
// thread (each search thread has its own).
struct CacheEntry {
  alignas(64) std::int16_t values[HIDDEN];
  Bitboard pieces[PIECE_NB];
  std::uint32_t net = 0;  // the network the entry belongs to; 0 = empty
};
thread_local std::vector<CacheEntry> cache;

void refresh(const Position& pos, Accumulator& acc, Color side, int st) {
  std::int16_t* v = acc.values[side];
  if (net.king_buckets == 1 && !net.mirror) {  // plain inputs: from scratch, as before king buckets
    std::memcpy(v, net.ft_bias, sizeof(net.ft_bias));
    for (Bitboard b = pos.pieces(); b;) {
      const Square s = pop_lsb(b);
      const std::int16_t* w = weights(pos.piece_on(s), s, side, st);
      for (int j = 0; j < HIDDEN; j++) v[j] += w[j];
    }
    acc.net[side] = net_id;
    return;
  }
  const std::size_t states = std::size_t(net.king_buckets) * 2;
  if (cache.size() < COLOR_NB * states) cache.resize(COLOR_NB * MAX_KING_BUCKETS * 2);
  CacheEntry& e = cache[side * MAX_KING_BUCKETS * 2 + st];
  if (e.net != net_id) {
    std::memcpy(e.values, net.ft_bias, sizeof(net.ft_bias));
    std::fill(std::begin(e.pieces), std::end(e.pieces), Bitboard(0));
    e.net = net_id;
  }
  for (int p = 0; p < PIECE_NB; p++) {
    const Piece pc = Piece(p);
    const Bitboard now = pos.pieces(color_of(pc), type_of(pc));
    for (Bitboard add = now & ~e.pieces[p]; add;) {
      const std::int16_t* w = weights(pc, pop_lsb(add), side, st);
      for (int j = 0; j < HIDDEN; j++) e.values[j] += w[j];
    }
    for (Bitboard sub = e.pieces[p] & ~now; sub;) {
      const std::int16_t* w = weights(pc, pop_lsb(sub), side, st);
      for (int j = 0; j < HIDDEN; j++) e.values[j] -= w[j];
    }
    e.pieces[p] = now;
  }
  std::memcpy(v, e.values, sizeof(e.values));
  acc.net[side] = net_id;
}

// Where `side`'s king stood before the move recorded in d, given where it
// stands after it.
inline Square king_before(const DirtyPieces& d, Color side, Square after) {
  const Piece king = make_piece(side, KING);
  for (int a = 0; a < d.added; a++)
    if (d.add_piece[a] == king && d.add_sq[a] == after)
      for (int r = 0; r < d.removed; r++)
        if (d.remove_piece[r] == king) return d.remove_sq[r];
  return after;
}

// Brings one side of the current position's accumulator up to date: forward
// from the nearest position before it whose side is computed, or from
// scratch when the updates on the way would add and remove more pieces than
// the board holds, or when that side's king state changes on the way (its
// inputs are then different ones). After a refresh the positions before it
// that were passed over are filled in backward, one update each, while the
// king state allows: otherwise every sibling of this position (and their
// children) would find no computed parent and refresh too.
void update(const Position& pos, Color side) {
  Accumulator* stack = pos.accumulators();
  const int top = pos.game_ply();
  const int limit = popcount(pos.pieces());
  const Square top_king = pos.king_square(side);
  const int st = king_state(side, top_king);
  Square king = top_king;
  int i = top, cost = 0;
  while (stack[i].net[side] != net_id) {
    cost += stack[i].dirty.added + stack[i].dirty.removed;
    const Square before = i > 0 ? king_before(stack[i].dirty, side, king) : king;
    if (i == 0 || cost > limit || king_state(side, before) != st) {
      refresh(pos, stack[top], side, st);
      Square k = top_king;
      for (int j = top - 1; j >= i; j--) {
        k = king_before(stack[j + 1].dirty, side, k);
        if (king_state(side, k) != st) break;
        unapply(stack[j + 1], stack[j], side, st);
      }
      return;
    }
    king = before;
    i--;
  }
  for (int j = i + 1; j <= top; j++) apply(stack[j - 1], stack[j], side, st);
}

void update(const Position& pos) {
  const Accumulator& acc = pos.accumulators()[pos.game_ply()];
  for (Color side : {WHITE, BLACK})
    if (acc.net[side] != net_id) update(pos, side);
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
  if (std::memcmp(magic, "E2NN", 4) != 0 || version < 1 || version > 3 || hidden != HIDDEN) return false;
  // Version 2 adds the activation and the number of output layers; version
  // 3 also king buckets (their count, the mirror flag and the 64-square
  // map); version 1 is plain clipped units and one output.
  std::uint32_t screlu = 0, buckets = 1, king_buckets = 1, mirror = 0;
  std::uint8_t king_map[64] = {};
  if (version >= 2 && (!take(data, size, pos, &screlu, 4) || !take(data, size, pos, &buckets, 4))) return false;
  if (version >= 3 && (!take(data, size, pos, &king_buckets, 4) || !take(data, size, pos, &mirror, 4) ||
                       !take(data, size, pos, king_map, 64)))
    return false;
  if (screlu > 1 || buckets < 1 || buckets > MAX_BUCKETS || king_buckets < 1 || king_buckets > MAX_KING_BUCKETS || mirror > 1)
    return false;
  for (std::uint8_t b : king_map)
    if (b >= king_buckets) return false;
  net.ft_weight.resize(std::size_t(king_buckets) * INPUTS * HIDDEN);
  if (!take(data, size, pos, net.ft_weight.data(), net.ft_weight.size() * sizeof(std::int16_t)) ||
      !take(data, size, pos, net.ft_bias, sizeof(net.ft_bias)) ||
      !take(data, size, pos, net.out_weight, buckets * sizeof(net.out_weight[0])) ||
      !take(data, size, pos, net.out_bias, buckets * sizeof(net.out_bias[0])) || pos != size)
    return false;
  net.screlu = screlu == 1;
  net.buckets = int(buckets);
  net.king_buckets = int(king_buckets);
  net.mirror = mirror == 1;
  std::memcpy(net.king_map, king_map, 64);
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
  if (acc.net[WHITE] != net_id || acc.net[BLACK] != net_id) update(pos);
  // Accumulators fit in 16 bits: the trainer keeps first-layer weights and
  // biases within 1.98, so 33 terms of at most 1.98 * QA stay under 32767.
  const Color us = pos.side_to_move();
  // The output layer for this many pieces (2 to 32): equal steps of piece
  // count, the fullest boards in the last. train.py's bucket_of is the same.
  const int bucket = net.buckets == 1 ? 0 : std::min((popcount(pos.pieces()) - 2) * net.buckets / 32, net.buckets - 1);
  const std::int16_t* w = net.out_weight[bucket];
  if (net.screlu) {
    const std::int32_t sum = output_squared(acc.values[us], acc.values[~us], w);
    return int((std::int64_t(sum) / QA + net.out_bias[bucket]) * SCALE / (QA * QB));
  }
  const std::int32_t sum = output(acc.values[us], acc.values[~us], w);
  return int((std::int64_t(sum) + net.out_bias[bucket]) * SCALE / (QA * QB));
}

}  // namespace chess::nnue
