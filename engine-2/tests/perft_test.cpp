// Perft node counts from the standard test positions (chessprogramming.org).
// Pass "full" to also run the deeper, slower counts.
#include <cstdio>
#include <cstring>
#include <vector>

#include "movegen.h"
#include "test_util.h"

using namespace chess;

struct Case {
  const char* name;
  const char* fen;
  std::vector<std::uint64_t> counts;  // counts[i] is the node count at depth i + 1
  int quick_depth;                    // deepest depth run by default
};

int main(int argc, char** argv) {
  init_all();
  const bool full = argc > 1 && std::strcmp(argv[1], "full") == 0;

  const std::vector<Case> cases = {
      {"startpos", START_FEN, {20, 400, 8902, 197281, 4865609, 119060324}, 5},
      {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
       {48, 2039, 97862, 4085603, 193690690}, 4},
      {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
       {14, 191, 2812, 43238, 674624, 11030083}, 5},
      {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
       {6, 264, 9467, 422333, 15833292}, 4},
      {"position 4 mirrored", "r2q1rk1/pP1p2pp/Q4n2/bbp1p3/Np6/1B3NBn/pPPP1PPP/R3K2R b KQ - 0 1",
       {6, 264, 9467, 422333, 15833292}, 4},
      {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
       {44, 1486, 62379, 2103487, 89941194}, 4},
      {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
       {46, 2079, 89890, 3894594, 164075551}, 4},
  };

  for (const Case& c : cases) {
    Position pos;
    CHECK(pos.set(c.fen));
    int deepest = full ? int(c.counts.size()) : c.quick_depth;
    for (int d = 1; d <= deepest; d++) {
      std::uint64_t n = perft(pos, d);
      std::printf("%-20s depth %d: %llu\n", c.name, d, (unsigned long long)n);
      CHECK_EQ(n, c.counts[d - 1]);
    }
    CHECK(pos.is_consistent());
  }

  std::printf(g_failures ? "%d FAILED\n" : "all perft counts match\n", g_failures);
  return g_failures ? 1 : 0;
}
