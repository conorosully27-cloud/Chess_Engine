// Static exchange evaluation tests: hand-checked exchanges, then see_ge
// compared with a slow, plainly written exchange search on every move of
// random playouts.
#include <algorithm>
#include <cstdio>
#include <random>

#include "movegen.h"
#include "test_util.h"

using namespace chess;

namespace {

// Best net gain for `stm` from capturing on `to`, where a piece worth
// `target` stands, given the occupancy: take with the cheapest attacker or
// stop. Attackers are recomputed from scratch each time, so x-rays need no
// special handling. A king may take only if nothing at all can take it back.
int exchange(const Position& pos, Square to, Bitboard occ, Color stm, int target) {
  const Bitboard attackers = pos.attackers_to(to, occ) & occ & pos.pieces(stm);
  if (!attackers) return 0;
  PieceType pt = PAWN;
  while (!(attackers & pos.pieces(pt))) pt = PieceType(pt + 1);
  const Bitboard after = occ ^ square_bb(lsb(attackers & pos.pieces(pt)));
  if (pt == KING) return pos.attackers_to(to, after) & after & pos.pieces(~stm) ? 0 : target;
  return std::max(0, target - exchange(pos, to, after, ~stm, SEE_VALUE[pt]));
}

// The exact exchange value of m, with the same rules as see_ge.
int reference_see(const Position& pos, Move m) {
  if (m.is_castle()) return 0;
  const Square from = m.from(), to = m.to();
  const Color us = pos.side_to_move();
  PieceType on_to = type_of(pos.piece_on(from));
  int gain = m.is_ep() ? SEE_VALUE[PAWN] : m.is_capture() ? SEE_VALUE[type_of(pos.piece_on(to))] : 0;
  if (m.is_promotion()) {
    on_to = m.promotion_type();
    gain += SEE_VALUE[on_to] - SEE_VALUE[PAWN];
  }
  Bitboard occ = (pos.pieces() ^ square_bb(from)) | square_bb(to);
  if (m.is_ep()) occ ^= square_bb(us == WHITE ? to - 8 : to + 8);
  // A legal king move can't be taken back, so its value never matters.
  return gain - exchange(pos, to, occ, ~us, SEE_VALUE[on_to]);
}

struct Case {
  const char* fen;
  const char* move;
  int value;
};

// Each value worked out by hand.
const Case CASES[] = {
    // An undefended pawn.
    {"1k1r4/1pp4p/p7/4p3/8/P5P1/1PP4P/2K1R3 w - - 0 1", "e1e5", 100},
    // Queen takes a rook the enemy queen guards.
    {"3qk3/8/8/3r4/8/8/8/3QK3 w - - 0 1", "d1d5", -400},
    // The same with a rook behind the queen: the x-ray wins the rook back.
    {"3qk3/8/8/3r4/8/8/3Q4/3RK3 w - - 0 1", "d2d5", 500},
    // Bishop for knight, pawn takes back: even.
    {"4k3/8/2p5/3n4/8/8/6B1/4K3 w - - 0 1", "g2d5", 0},
    // Pawn takes a guarded knight.
    {"4k3/2p5/3n4/4P3/8/8/8/4K3 w - - 0 1", "e5d6", 225},
    // A quiet queen move onto a square a pawn guards.
    {"4k3/2p5/8/8/8/8/8/3QK3 w - - 0 1", "d1d6", -900},
    // En passant: the captured pawn leaves the d-file, so the white rook
    // guards d6 and Black's rook won't take back.
    {"3rk3/8/8/3pP3/8/8/8/3RK3 w - d6 0 1", "e5d6", 100},
    // Promotion onto a square the rook guards, and capturing it instead.
    {"r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7b8q", -100},
    {"r3k3/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7a8q", 1300},
    // The king takes an undefended pawn.
    {"4k3/8/8/8/3p4/4K3/8/8 w - - 0 1", "e3d4", 100},
    // Rook takes a pawn and the king takes back...
    {"4k3/3p4/8/8/8/8/3R4/4K3 w - - 0 1", "d2d7", -400},
    // ...unless a second rook behind the first guards the square.
    {"4k3/3p4/8/8/8/8/3R4/3RK3 w - - 0 1", "d2d7", 100},
    // Castling.
    {"4k3/8/8/8/8/8/8/R3K2R w KQ - 0 1", "e1g1", 0},
};

void test_cases() {
  for (const Case& c : CASES) {
    Position pos(c.fen);
    const Move m = parse_uci_move(pos, c.move);
    CHECK(bool(m));
    const int ref = reference_see(pos, m);
    if (ref != c.value) std::printf("reference %s %s: %d, expected %d\n", c.fen, c.move, ref, c.value);
    CHECK(ref == c.value);
    CHECK(pos.see_ge(m, c.value));
    CHECK(!pos.see_ge(m, c.value + 1));
  }
}

const char* FENS[] = {
    START_FEN,
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "1k1r3q/1ppn3p/p4b2/4p3/8/P2N2P1/1PP1R1BP/2K1Q3 w - - 0 1",
};

// see_ge must agree with the reference at and around each move's value.
void test_against_reference() {
  std::mt19937_64 rng(2026);
  long compared = 0;
  for (const char* fen : FENS) {
    for (int game = 0; game < 300; game++) {
      Position pos(fen);
      for (int ply = 0; ply < 120; ply++) {
        MoveList list;
        generate_legal(pos, list);
        if (list.size() == 0) break;
        for (Move m : list) {
          const int ref = reference_see(pos, m);
          for (int t : {ref - 1, ref, ref + 1, -900, -325, -100, -1, 0, 1, 100, 325, 900}) {
            if (pos.see_ge(m, t) != (ref >= t)) {
              std::printf("see_ge(%s, %d) = %d, reference %d: %s\n", m.uci().c_str(), t, pos.see_ge(m, t), ref,
                          pos.fen().c_str());
              CHECK(false);
              return;
            }
            compared++;
          }
        }
        pos.make_move(list[rng() % list.size()]);
      }
    }
  }
  std::printf("%ld see_ge results match the reference\n", compared);
}

}  // namespace

int main() {
  init_all();
  test_cases();
  test_against_reference();
  std::printf(g_failures ? "%d FAILED\n" : "all SEE tests pass\n", g_failures);
  return g_failures ? 1 : 0;
}
