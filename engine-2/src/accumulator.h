// The NNUE hidden layer's sums, kept with each position on Position's stack
// so a move updates them from the position before instead of recomputing
// (see nnue.h). Position records what each move changed; nnue.cpp does the
// arithmetic, lazily, when a position is evaluated.
#pragma once

#include <cstdint>

#include "types.h"

namespace chess::nnue {

constexpr int HIDDEN = 128;

// The pieces a move took off and put on squares. At most two of each:
// castling moves king and rook, a capturing promotion removes the pawn and
// the captured piece. A null move changes nothing.
struct DirtyPieces {
  int added = 0, removed = 0;
  Piece add_piece[2], remove_piece[2];
  Square add_sq[2], remove_sq[2];

  void add(Piece pc, Square s) { add_piece[added] = pc, add_sq[added++] = s; }
  void remove(Piece pc, Square s) { remove_piece[removed] = pc, remove_sq[removed++] = s; }
};

struct Accumulator {
  alignas(64) std::int16_t values[COLOR_NB][HIDDEN];  // the hidden units, seen from White's and Black's side
  DirtyPieces dirty;                                  // how this position differs from the one before
  std::uint32_t net = 0;  // id of the network `values` hold sums for; 0 = not computed
};

}  // namespace chess::nnue
