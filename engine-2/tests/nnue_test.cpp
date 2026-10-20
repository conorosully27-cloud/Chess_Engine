// NNUE tests: the accumulators updated move by move must give exactly the
// evaluation of the same position computed from scratch (a fresh Position
// from its FEN), through captures, castling, en passant, promotions, null
// moves, unmaking, positions left unevaluated, and a change of network.
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>

#include "movegen.h"
#include "nnue.h"
#include "test_util.h"

using namespace chess;

namespace chess::nnue {
extern const unsigned char default_net_data[];
extern const std::size_t default_net_size;
}  // namespace chess::nnue

namespace {

const char* FENS[] = {
    START_FEN,
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
    "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",  // promotions, with and without captures
};

long g_compared = 0;

bool matches_scratch(const Position& pos) {
  const int incremental = nnue::evaluate(pos);
  const int scratch = nnue::evaluate(Position(pos.fen()));
  g_compared++;
  if (incremental == scratch) return true;
  std::printf("incremental %d, from scratch %d: %s\n", incremental, scratch, pos.fen().c_str());
  return false;
}

// Random games, evaluating at a random half of the positions (so updates
// also catch up over several moves) and sometimes passing with a null move;
// then unmaking everything, evaluating on the way back.
void test_random_games() {
  std::mt19937_64 rng(2026);
  for (const char* fen : FENS) {
    for (int game = 0; game < 100; game++) {
      Position pos(fen);
      std::vector<bool> nulls;
      for (int ply = 0; ply < 120; ply++) {
        if (!pos.in_check() && rng() % 10 == 0) {
          pos.make_null_move();
          nulls.push_back(true);
        } else {
          MoveList list;
          generate_legal(pos, list);
          if (list.size() == 0) break;
          pos.make_move(list[rng() % list.size()]);
          nulls.push_back(false);
        }
        if (rng() % 2 == 0 && !matches_scratch(pos)) {
          CHECK(false);
          return;
        }
      }
      while (!nulls.empty()) {
        if (nulls.back()) pos.unmake_null_move();
        else pos.unmake_move();
        nulls.pop_back();
        if (rng() % 4 == 0 && !matches_scratch(pos)) {
          CHECK(false);
          return;
        }
      }
    }
  }
}

// Accumulators computed with one network must not be reused with another.
void test_network_change() {
  Position pos("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1");
  for (const char* m : {"e2a6", "b4c3", "d2c3"}) {
    pos.make_move(parse_uci_move(pos, m));
    nnue::evaluate(pos);
  }
  // The same network with the output bias raised by 100 centipawns' worth.
  std::vector<unsigned char> bytes(nnue::default_net_data, nnue::default_net_data + nnue::default_net_size);
  std::int32_t bias;
  std::memcpy(&bias, bytes.data() + bytes.size() - 4, 4);
  bias += 255 * 64 / 4;
  std::memcpy(bytes.data() + bytes.size() - 4, &bias, 4);
  // And the first-layer row of "own king on e1" (both kings are home, so
  // both sides' sums use it) raised, so stale accumulators would show.
  for (int j = 0; j < 128; j++) {
    std::int16_t w;
    unsigned char* p = bytes.data() + 12 + ((KING * 64 + 4) * 128 + j) * 2;
    std::memcpy(&w, p, 2);
    w += 20;
    std::memcpy(p, &w, 2);
  }
  const int before = nnue::evaluate(pos);
  CHECK(nnue::load(bytes.data(), bytes.size()));
  CHECK(nnue::evaluate(pos) != before);
  CHECK(matches_scratch(pos));
  pos.unmake_move();
  CHECK(matches_scratch(pos));
  nnue::load_default();
  CHECK_EQ(nnue::evaluate(pos), nnue::evaluate(Position(pos.fen())));
}

}  // namespace

int main() {
  init_all();
  nnue::load_default();
  CHECK(nnue::loaded());
  test_random_games();
  test_network_change();
  std::printf("nnue: %ld evaluations compared\n", g_compared);
  std::printf(g_failures ? "%d FAILED\n" : "all nnue tests pass\n", g_failures);
  return g_failures ? 1 : 0;
}
