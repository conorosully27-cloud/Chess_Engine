// Move generation. Pseudo-legal moves obey piece movement but may leave the
// king in check; generate_legal filters them with Position::legal.
#pragma once

#include <cstdint>
#include <string>

#include "position.h"

namespace chess {

struct MoveList {
  static constexpr int MAX_MOVES = 256;

  // The storage is left uninitialized: only the first count entries are used,
  // and zeroing 256 moves for every node showed up in profiles.
  MoveList() {}

  void add(Move m) { moves[count++] = m; }
  int size() const { return count; }
  Move operator[](int i) const { return moves[i]; }
  const Move* begin() const { return moves; }
  const Move* end() const { return moves + count; }

  union {
    Move moves[MAX_MOVES];
  };
  int count = 0;
};

// What to generate: every move; captures only (en passant and capturing
// promotions included); everything else (pushes, quiet promotions,
// castling); or captures and promotions to a queen only (for quiescence).
// CAPTURES and QUIETS together are exactly ALL, each in ALL's relative order.
enum GenType { ALL, CAPTURES, QUIETS, TACTICAL };

template <GenType Type>
void generate(const Position& pos, MoveList& list);

// With tactical, generate<TACTICAL>; else generate<ALL>.
void generate_pseudo_legal(const Position& pos, MoveList& list, bool tactical = false);
// Whether generate<ALL> would produce m (for a move from the hash table,
// which may come from another position).
bool pseudo_legal(const Position& pos, Move m);
void generate_legal(const Position& pos, MoveList& list);

// Finds the legal move written in UCI notation (e.g. "e2e4", "e7e8q"), or an
// empty move if there is none.
Move parse_uci_move(const Position& pos, const std::string& text);

std::uint64_t perft(Position& pos, int depth);

}  // namespace chess
