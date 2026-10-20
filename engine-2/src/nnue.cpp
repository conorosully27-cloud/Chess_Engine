#include "nnue.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

namespace chess::nnue {

namespace {

constexpr int INPUTS = 768;
constexpr int HIDDEN = 128;
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

// The feature for a piece on a square, seen from White's or Black's side:
// from Black's side the colors swap and the board flips.
inline int feature(Piece pc, Square s, Color side) {
  return side == WHITE ? pc * 64 + s : ((pc + 6) % 12) * 64 + (s ^ 56);
}

}  // namespace

bool load(const std::string& path) {
  have_net = false;
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  char magic[4];
  std::uint32_t version = 0, hidden = 0;
  in.read(magic, 4);
  in.read(reinterpret_cast<char*>(&version), 4);
  in.read(reinterpret_cast<char*>(&hidden), 4);
  if (!in || std::memcmp(magic, "E2NN", 4) != 0 || version != 1 || hidden != HIDDEN) return false;
  in.read(reinterpret_cast<char*>(net.ft_weight), sizeof(net.ft_weight));
  in.read(reinterpret_cast<char*>(net.ft_bias), sizeof(net.ft_bias));
  in.read(reinterpret_cast<char*>(net.out_weight), sizeof(net.out_weight));
  in.read(reinterpret_cast<char*>(&net.out_bias), sizeof(net.out_bias));
  if (!in || in.peek() != std::ifstream::traits_type::eof()) return false;
  have_net = true;
  return true;
}

void unload() { have_net = false; }
bool loaded() { return have_net; }

int evaluate(const Position& pos) {
  // Accumulators fit in 16 bits: the trainer keeps first-layer weights and
  // biases within 1.98, so 33 terms of at most 1.98 * QA stay under 32767.
  alignas(64) std::int16_t acc[COLOR_NB][HIDDEN];
  std::memcpy(acc[WHITE], net.ft_bias, sizeof(net.ft_bias));
  std::memcpy(acc[BLACK], net.ft_bias, sizeof(net.ft_bias));
  for (Bitboard b = pos.pieces(); b;) {
    const Square s = pop_lsb(b);
    const Piece pc = pos.piece_on(s);
    const std::int16_t* w = net.ft_weight[feature(pc, s, WHITE)];
    const std::int16_t* k = net.ft_weight[feature(pc, s, BLACK)];
    for (int j = 0; j < HIDDEN; j++) {
      acc[WHITE][j] += w[j];
      acc[BLACK][j] += k[j];
    }
  }
  const Color us = pos.side_to_move();
  std::int32_t sum = 0;
  for (int j = 0; j < HIDDEN; j++) {
    sum += std::clamp<int>(acc[us][j], 0, QA) * net.out_weight[j];
    sum += std::clamp<int>(acc[~us][j], 0, QA) * net.out_weight[HIDDEN + j];
  }
  return int((std::int64_t(sum) + net.out_bias) * SCALE / (QA * QB));
}

}  // namespace chess::nnue
