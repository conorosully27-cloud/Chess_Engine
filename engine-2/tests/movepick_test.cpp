// Staged move generation tests: the capture and quiet generators split the
// full one exactly, pseudo_legal agrees with the generator on every possible
// move, and MovePicker hands out every move once, in score_moves' order.
#include <algorithm>
#include <cstdio>
#include <random>
#include <vector>

#include "movepick.h"
#include "test_util.h"

using namespace chess;

namespace {

const char* FENS[] = {
    START_FEN,
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
    "n1n5/PPPk4/8/8/8/8/4Kppp/5N1N b - - 0 1",
};

// Positions from random games from each FEN.
std::vector<Position> sample_positions() {
  std::mt19937_64 rng(77);
  std::vector<Position> out;
  for (const char* fen : FENS) {
    for (int game = 0; game < 20; game++) {
      Position pos(fen);
      for (int ply = 0; ply < 60; ply++) {
        MoveList list;
        generate_legal(pos, list);
        if (list.size() == 0) break;
        pos.make_move(list[rng() % list.size()]);
        if (ply % 3 == 0) out.emplace_back(pos.fen());
      }
    }
  }
  return out;
}

std::vector<Move> to_vector(const MoveList& list) { return {list.begin(), list.end()}; }

void test_generators(const std::vector<Position>& positions) {
  long moves = 0;
  for (const Position& pos : positions) {
    MoveList all, captures, quiets;
    generate<ALL>(pos, all);
    generate<CAPTURES>(pos, captures);
    generate<QUIETS>(pos, quiets);
    std::vector<Move> want_captures, want_quiets;
    for (Move m : all) (m.is_capture() ? want_captures : want_quiets).push_back(m);
    if (to_vector(captures) != want_captures || to_vector(quiets) != want_quiets) {
      std::printf("capture/quiet split differs from the full list: %s\n", pos.fen().c_str());
      CHECK(false);
      return;
    }
    moves += all.size();
    // Every 16-bit move, against membership of the full list (in a quarter
    // of the positions, to keep the test quick).
    if ((&pos - positions.data()) % 4) continue;
    for (int raw = 0; raw < 65536; raw++) {
      const Move m = Move::from_raw(std::uint16_t(raw));
      const bool in_list = std::find(all.begin(), all.end(), m) != all.end();
      if (pseudo_legal(pos, m) != in_list) {
        std::printf("pseudo_legal(%s) = %d, generated: %d: %s\n", m.uci().c_str(), !in_list, in_list,
                    pos.fen().c_str());
        CHECK(false);
        return;
      }
    }
  }
  std::printf("generators: %zu positions, %ld moves\n", positions.size(), moves);
}

void test_picker(const std::vector<Position>& positions) {
  std::mt19937_64 rng(99);
  static HistoryTable history;
  long picked = 0;
  for (const Position& pos : positions) {
    MoveList all;
    generate<ALL>(pos, all);
    for (int trial = 0; trial < 4; trial++) {
      for (auto& row : history)
        for (int& h : row) h = int(rng() % 2001) - 1000;
      // Hash move and killers: real moves, or junk that may not be legal here.
      const auto any_move = [&] {
        return rng() % 4 == 0 || all.size() == 0 ? Move::from_raw(std::uint16_t(rng())) : all[rng() % all.size()];
      };
      const Move tt_move = trial == 0 ? Move() : any_move();
      const Move killer1 = any_move(), killer2 = any_move();

      int scores[MoveList::MAX_MOVES];
      score_moves(pos, all, scores, tt_move, killer1, killer2, history);
      std::vector<Move> got;
      MovePicker picker(pos, tt_move, killer1, killer2, history);
      while (const Move m = picker.next()) got.push_back(m);

      // The same moves, once each...
      std::vector<Move> a = to_vector(all), b = got;
      const auto by_raw = [](Move x, Move y) { return x.raw() < y.raw(); };
      std::sort(a.begin(), a.end(), by_raw);
      std::sort(b.begin(), b.end(), by_raw);
      if (a != b) {
        std::printf("picker gave %zu moves, generator %d: %s\n", got.size(), all.size(), pos.fen().c_str());
        CHECK(false);
        return;
      }
      // ...best first by score_moves' scores.
      for (std::size_t i = 1; i < got.size(); i++) {
        const auto score_of = [&](Move m) { return scores[std::find(all.begin(), all.end(), m) - all.begin()]; };
        if (score_of(got[i]) > score_of(got[i - 1])) {
          std::printf("picker order: %s (%d) after %s (%d): %s\n", got[i].uci().c_str(), score_of(got[i]),
                      got[i - 1].uci().c_str(), score_of(got[i - 1]), pos.fen().c_str());
          CHECK(false);
          return;
        }
      }
      picked += long(got.size());
    }
  }
  std::printf("picker: %ld moves checked\n", picked);
}

}  // namespace

int main() {
  init_all();
  const std::vector<Position> positions = sample_positions();
  test_generators(positions);
  test_picker(positions);
  std::printf(g_failures ? "%d FAILED\n" : "all move picker tests pass\n", g_failures);
  return g_failures ? 1 : 0;
}
