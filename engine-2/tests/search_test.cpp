// Search tests: time allocation, the evaluation against Engine 1.6's, and
// that the search finds mates and simple tactics.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include "evaluate.h"
#include "search.h"
#include "test_util.h"

using namespace chess;

namespace {

void test_time_allocation() {
  SearchLimits l;
  TimeBudget b = allocate_time(l, WHITE, 50);
  CHECK(b.soft_ms == -1 && b.hard_ms == -1);  // no limits: think until told to stop

  l.movetime = 1000;
  b = allocate_time(l, WHITE, 50);
  CHECK_EQ(b.hard_ms, 950);
  CHECK_EQ(b.soft_ms, 475);  // no new depth past half the time

  l = SearchLimits();
  l.infinite = true;
  l.wtime = 1000;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.hard_ms == -1);

  // Clock games: a fraction of the remaining time, never most of it.
  l = SearchLimits();
  l.wtime = 60000;
  l.btime = 1000;
  l.winc = 1000;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.soft_ms > 1000 && b.soft_ms < 5000);
  CHECK(b.hard_ms >= b.soft_ms && b.hard_ms <= 60000 * 3 / 4);
  b = allocate_time(l, BLACK, 50);  // black's clock: 1 s, no increment
  CHECK(b.soft_ms >= 1 && b.hard_ms <= 1000);

  // Last move before the time control may use more, but keeps a reserve.
  l = SearchLimits();
  l.wtime = 10000;
  l.movestogo = 1;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.hard_ms <= 7500 && b.soft_ms > 5000);

  // Almost out of time: still a positive budget below the clock.
  l = SearchLimits();
  l.wtime = 30;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.soft_ms >= 1 && b.hard_ms >= 1 && b.hard_ms <= 30);
}

// Scores recorded from Engine 1.6's evaluate() on positions from random
// games: the port must match them exactly.
void test_eval_matches_engine1(const char* path) {
  std::ifstream in(path);
  CHECK(in.good());
  std::string line;
  int n = 0, wrong = 0;
  while (std::getline(in, line)) {
    const auto tab = line.find('\t');
    if (tab == std::string::npos) continue;
    Position pos;
    CHECK(pos.set(line.substr(0, tab)));
    const int expected = std::stoi(line.substr(tab + 1));
    n++;
    if (evaluate(pos) != expected) {
      if (wrong++ < 5) std::printf("eval %d, expected %d: %s\n", evaluate(pos), expected, line.c_str());
    }
  }
  CHECK(n >= 400);
  CHECK_EQ(wrong, 0);
  std::printf("evaluation matches Engine 1.6 on %d positions\n", n);
}

void test_eval_symmetry() {
  // Mirroring the board and swapping colors must give the same score.
  const char* pairs[][2] = {
      {"r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
       "rnbqk2r/pppp1ppp/5n2/2b1p3/4P3/2N2N2/PPPP1PPP/R1BQKB1R b KQkq - 4 4"},
      {"8/5pk1/6p1/7p/7P/6P1/5PK1/8 w - - 0 1", "8/5pk1/6p1/7p/7P/6P1/5PK1/8 b - - 0 1"},
  };
  for (auto& p : pairs) {
    Position a(p[0]), b(p[1]);
    CHECK_EQ(evaluate(a), evaluate(b));
  }
}

Move best_at_depth(const char* fen, int depth, int* score = nullptr) {
  TranspositionTable tt(16);
  SearchWorker worker(tt);
  Position pos(fen);
  SearchLimits limits;
  limits.depth = depth;
  SearchResult r = worker.search(pos, limits, TimeBudget());
  CHECK(pos.fen() == Position(fen).fen());  // the search leaves the position as it was
  if (score) *score = r.score;
  return r.best;
}

void test_search_finds_mates_and_tactics() {
  int score = 0;
  // Back-rank mate in one.
  CHECK(best_at_depth("6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1", 4, &score).uci() == "d1d8");
  CHECK_EQ(score, MATE - 1);
  // Mate in two with king and rook (Kb6 or similar, then Rh8#): 3 plies.
  best_at_depth("k7/8/2K5/8/8/8/8/7R w - - 0 1", 6, &score);
  CHECK_EQ(score, MATE - 3);
  // Checkmated: no move, mate score.
  CHECK(!best_at_depth("3R2k1/5ppp/8/8/8/8/5PPP/6K1 b - - 0 1", 3, &score));
  CHECK_EQ(score, -MATE);
  // Stalemated: no move, draw score.
  CHECK(!best_at_depth("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1", 3, &score));
  CHECK_EQ(score, 0);
  // Win a hanging queen.
  CHECK(best_at_depth("4k3/8/8/3q4/8/8/3R4/4K3 w - - 0 1", 5).uci() == "d2d5");
  // Knight fork of king and queen.
  CHECK(best_at_depth("q3k3/8/8/3N4/8/8/8/4K3 w - - 0 1", 5).uci() == "d5c7");
}

}  // namespace

int main(int argc, char** argv) {
  init_all();
  test_time_allocation();
  test_eval_matches_engine1(argc > 1 ? argv[1] : "eval_reference.tsv");
  test_eval_symmetry();
  test_search_finds_mates_and_tactics();
  std::printf(g_failures ? "%d FAILED\n" : "all search tests pass\n", g_failures);
  return g_failures ? 1 : 0;
}
