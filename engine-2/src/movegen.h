// Move generation. Pseudo-legal moves obey piece movement but may leave the
// king in check; generate_legal filters them with Position::legal.
#pragma once

#include <cstdint>
#include <string>

#include "position.h"

namespace chess {

struct MoveList {
  static constexpr int MAX_MOVES = 256;

  void add(Move m) { moves[count++] = m; }
  int size() const { return count; }
  Move operator[](int i) const { return moves[i]; }
  const Move* begin() const { return moves; }
  const Move* end() const { return moves + count; }

  Move moves[MAX_MOVES];
  int count = 0;
};

void generate_pseudo_legal(const Position& pos, MoveList& list);
void generate_legal(const Position& pos, MoveList& list);

// Finds the legal move written in UCI notation (e.g. "e2e4", "e7e8q"), or an
// empty move if there is none.
Move parse_uci_move(const Position& pos, const std::string& text);

std::uint64_t perft(Position& pos, int depth);

}  // namespace chess
