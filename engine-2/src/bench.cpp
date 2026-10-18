#include <chrono>
#include <string>

#include "search.h"
#include "uci.h"

namespace chess {

namespace {

const char* BENCH_FENS[] = {
    START_FEN,
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "rnbqkb1r/pp2pppp/3p1n2/8/3NP3/8/PPP2PPP/RNBQKB1R w KQkq - 1 5",
    "r1bq1rk1/ppp1nppp/4p3/3pP3/1b1P4/2NB1N2/PPP2PPP/R1BQK2R w KQ - 3 8",
    "r2q1rk1/pb1nbppp/1p2pn2/2pp4/2PP4/1PN1PN2/PB2BPPP/R2Q1RK1 w - - 0 11",
    "2r2rk1/pp1q1ppp/2n1pn2/3p4/3P4/2PBPN2/P2Q1PPP/R4RK1 w - - 0 15",
    "r1b2rk1/2q1bppp/p2ppn2/1p6/3BPP2/2NB4/PPPQ2PP/2KR3R w - - 2 13",
    "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
    "8/8/4k3/3p4/3P4/4K3/8/8 w - - 0 1",
    "8/5pk1/6p1/7p/7P/6P1/5PK1/8 w - - 0 1",
    "4r1k1/1p3ppp/p1p5/3r4/3P4/P1P2P2/1P3P1P/R3R1K1 b - - 0 22",
    "8/8/1p1k4/1P6/2K5/8/8/8 w - - 0 1",
    "r1bqk2r/pp2bppp/2n1pn2/2pp4/2PP4/2N1PN2/PP3PPP/R1BQKB1R w KQkq - 0 7",
};

}  // namespace

void run_bench(int depth) {
  if (depth <= 0) depth = 11;
  TranspositionTable tt(16);
  SearchWorker worker(tt);
  std::uint64_t nodes = 0;
  auto t0 = std::chrono::steady_clock::now();
  for (const char* fen : BENCH_FENS) {
    Position pos(fen);
    tt.clear();
    worker.new_game();
    SearchLimits limits;
    limits.depth = depth;
    SearchResult r = worker.search(pos, limits, TimeBudget());
    nodes += r.nodes;
  }
  auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
  uci_send("===========================");
  uci_send("Total time (ms) : " + std::to_string(ms));
  uci_send("Nodes searched  : " + std::to_string(nodes));
  uci_send("Nodes/second    : " + std::to_string(nodes * 1000 / std::max<long long>(ms, 1)));
}

}  // namespace chess
