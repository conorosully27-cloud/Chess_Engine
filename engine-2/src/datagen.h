// Training data for an NNUE evaluation: self-play games, with the quiet
// positions they pass through saved alongside the search score and the game's
// result.
#pragma once

#include <cstdint>
#include <string>

namespace chess {

// One position, 32 bytes, as written to the data file (little-endian):
//   occupancy  which squares hold a piece (bit n = square n, a1 = 0)
//   pieces     the Piece (0-11) on each occupied square, 4 bits each, in
//              square order (low nibble first); up to 32 pieces
//   score      the search score in centipawns, from White's side
//   result     the game's result: 0 Black won, 1 draw, 2 White won
//   stm        side to move: 0 White, 1 Black
struct PackedPosition {
  std::uint64_t occupancy;
  std::uint8_t pieces[16];
  std::int16_t score;
  std::uint8_t result;
  std::uint8_t stm;
  std::uint8_t reserved[4];
};
static_assert(sizeof(PackedPosition) == 32, "the data format is 32 bytes a position");

// Plays games against itself (`nodes` per move) and appends positions to
// `path` until it holds at least `positions` of them. Each game opens with
// random moves; with a `book` (one opening per line, in UCI moves, e.g. from
// scripts/nnue/book_to_uci.mjs), it plays a random book opening first and
// fewer random moves after it, for more varied games. With `keep` above 1,
// each quiet position is saved with probability 1/keep: more games for the
// same number of positions, so more varied data (neighbouring positions of
// one game are much alike). Safe to stop at any
// time: each game's positions are written together once it ends, a reader
// ignores a partial record at the end of the file, and a resumed run trims
// one before appending. Resume with a different seed: the same seed replays
// the same games.
void run_datagen(const std::string& path, std::uint64_t positions, int nodes, std::uint64_t seed,
                 const std::string& book = "", int keep = 1);

}  // namespace chess
