// Syzygy endgame tablebases (src/fathom, native build only). With no path
// set, or in the WebAssembly build, nothing is probed and the search is
// unchanged.
#pragma once

#include <string>

#include "movegen.h"
#include "position.h"

namespace chess::tb {

// Win/draw/loss for the side to move; blessed losses and cursed wins
// (decided past the fifty-move rule) count as draws.
enum Wdl { LOSS = -1, DRAW = 0, WIN = 1, FAILED = 2 };

// Loads the tables in path (directories separated by ':' or ';' on Windows);
// returns the most pieces any loaded table covers (0: none).
int init(const std::string& path);
int largest();  // 0 when no tables are loaded

// Only for positions with no castling rights, at most largest() pieces, and
// a fifty-move counter of 0 (just after a capture or pawn move).
Wdl probe_wdl(const Position& pos);

// At the root: the move the tables give (keeps the result and, using the
// distance-to-zero tables, respects the fifty-move rule). False if not found.
bool probe_root(const Position& pos, const MoveList& legal, Move& move, Wdl& wdl);

}  // namespace chess::tb
