#include "datagen.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <random>
#include <vector>

#include "movegen.h"
#include "search.h"

namespace chess {

namespace {

constexpr int MIN_RANDOM_PLIES = 6;      // random moves from the start, for variety: a
constexpr int MAX_RANDOM_PLIES = 11;     // number in this range (odd and even, so either side moves first)
constexpr int MAX_OPENING_SCORE = 200;   // skip openings already this lopsided (2 pawns)
constexpr int WIN_SCORE = 2000;          // adjudicate a win at this score...
constexpr int WIN_PLIES = 4;             // ...held for this many plies in a row
constexpr int DRAW_FROM_PLY = 80;        // adjudicate a draw after this ply...
constexpr int DRAW_SCORE = 10;           // ...once the score stays this close to 0...
constexpr int DRAW_PLIES = 10;           // ...for this many plies in a row
constexpr int MAX_PLIES = 400;           // then call it a draw anyway

PackedPosition pack(const Position& pos, int white_score) {
  PackedPosition p{};
  p.occupancy = pos.pieces();
  int i = 0;
  for (Bitboard b = p.occupancy; b; i++) {
    const Piece pc = pos.piece_on(pop_lsb(b));
    p.pieces[i / 2] |= std::uint8_t(pc << (4 * (i % 2)));
  }
  p.score = std::int16_t(white_score);
  p.stm = pos.side_to_move() == WHITE ? 0 : 1;
  return p;
}

}  // namespace

void run_datagen(const std::string& path, std::uint64_t positions, int nodes, std::uint64_t seed) {
  // Resuming: drop a partial record left by a run stopped mid-write, so the
  // records appended next stay aligned.
  std::error_code ec;
  const std::uintmax_t size = std::filesystem::exists(path) ? std::filesystem::file_size(path) : 0;
  if (size % sizeof(PackedPosition)) std::filesystem::resize_file(path, size - size % sizeof(PackedPosition), ec);
  std::FILE* out = std::fopen(path.c_str(), "ab");
  if (!out || ec) {
    std::fprintf(stderr, "cannot open %s\n", path.c_str());
    return;
  }
  std::uint64_t written = size / sizeof(PackedPosition);

  std::mt19937_64 rng(seed);
  TranspositionTable tt(16);
  SearchWorker worker(tt);
  SearchLimits limits;
  limits.nodes = std::uint64_t(nodes);
  std::uint64_t games = 0;
  std::vector<PackedPosition> game;

  while (written < positions) {
    tt.clear();
    worker.new_game();
    Position pos;
    MoveList legal;

    // A random opening; start again if it ends the game or is already lopsided.
    bool ok = true;
    const int random_plies = MIN_RANDOM_PLIES + int(rng() % (MAX_RANDOM_PLIES - MIN_RANDOM_PLIES + 1));
    for (int i = 0; i < random_plies && ok; i++) {
      legal.count = 0;
      generate_legal(pos, legal);
      if (legal.size() == 0) ok = false;
      else pos.make_move(legal[int(rng() % std::uint64_t(legal.size()))]);
    }
    if (!ok) continue;
    legal.count = 0;
    generate_legal(pos, legal);
    if (legal.size() == 0) continue;
    if (std::abs(worker.search(pos, limits, TimeBudget()).score) > MAX_OPENING_SCORE) continue;

    // Self-play to the end, keeping the quiet positions.
    game.clear();
    int result = 1;  // from White's side: 0 loss, 1 draw, 2 win
    int win_run = 0, draw_run = 0;
    for (int ply = 0;; ply++) {
      legal.count = 0;
      generate_legal(pos, legal);
      if (legal.size() == 0) {
        result = pos.in_check() ? (pos.side_to_move() == WHITE ? 0 : 2) : 1;
        break;
      }
      if (pos.is_threefold() || pos.is_fifty_move() || pos.insufficient_material() || ply >= MAX_PLIES) break;

      const SearchResult r = worker.search(pos, limits, TimeBudget());
      const int white_score = pos.side_to_move() == WHITE ? r.score : -r.score;
      if (is_mate_score(r.score) || std::abs(r.score) >= WIN_SCORE) {
        if (++win_run >= WIN_PLIES) {
          result = white_score > 0 ? 2 : 0;
          break;
        }
      } else {
        win_run = 0;
      }
      draw_run = (ply >= DRAW_FROM_PLY && std::abs(r.score) <= DRAW_SCORE) ? draw_run + 1 : 0;
      if (draw_run >= DRAW_PLIES) break;

      // Quiet positions only: in check, or with a tactic in progress, the
      // static evaluation isn't what the score measures.
      if (!pos.in_check() && !r.best.is_capture() && !r.best.is_promotion() && !is_mate_score(r.score))
        game.push_back(pack(pos, white_score));
      pos.make_move(r.best);
    }

    for (PackedPosition& p : game) p.result = std::uint8_t(result);
    std::fwrite(game.data(), sizeof(PackedPosition), game.size(), out);
    std::fflush(out);
    written += game.size();
    if (++games % 100 == 0) std::fprintf(stderr, "%llu games, %llu positions\n", (unsigned long long)games, (unsigned long long)written);
  }
  std::fclose(out);
}

}  // namespace chess
