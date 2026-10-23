// Position tests beyond perft: FEN round trips, make/unmake, hashing,
// repetition and draw rules, and random playouts that check everything stays
// consistent.
#include <cstdio>
#include <random>
#include <string>
#include <vector>

#include "movegen.h"
#include "test_util.h"

using namespace chess;

namespace {

const char* FENS[] = {
    START_FEN,
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
    "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
    "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
    "4k3/8/8/3pP3/8/8/8/4K3 w - d6 0 2",
    "8/8/8/8/k2Pp2Q/8/8/3K4 b - d3 0 1",  // en passant that would expose the king
};

void test_fen_round_trip() {
  for (const char* fen : FENS) {
    Position pos;
    CHECK(pos.set(fen));
    CHECK(pos.fen() == fen);
    CHECK_EQ(pos.key(), pos.compute_key());
  }
  Position pos;
  CHECK(!pos.set("not a fen"));
  CHECK(!pos.set("8/8/8/8/8/8/8/8 w - - 0 1"));  // no kings
  CHECK(pos.fen() == START_FEN);                 // a failed set leaves the start position

  // An en passant square no pawn can have just passed is dropped, so en
  // passant can't "capture" a pawn that isn't there.
  const char* bad_ep[] = {
      "4k3/8/8/3P4/8/8/8/4K3 w - e6 0 1",      // no black pawn on e5
      "4k3/8/8/3Pp3/8/8/8/4K3 b - e6 0 1",     // a sixth-rank square with Black to move
      "4k3/8/4n3/3Pp3/8/8/8/4K3 w - e6 0 1",   // e6 occupied
      "4k3/4n3/8/3Pp3/8/8/8/4K3 w - e6 0 1",   // e7 occupied: the pawn didn't come from there
      "4k3/8/8/8/3pP3/8/8/4K3 w - e3 0 1",     // a third-rank square with White to move
  };
  for (const char* fen : bad_ep) {
    CHECK(pos.set(fen));
    CHECK_EQ(pos.ep_square(), NO_SQUARE);
    CHECK_EQ(pos.key(), pos.compute_key());
    MoveList list;
    generate_legal(pos, list);
    for (Move m : list) CHECK(!m.is_ep());
  }
}

void test_move_counts() {
  Position pos;
  MoveList list;
  generate_legal(pos, list);
  CHECK_EQ(list.size(), 20);

  // Checkmate and stalemate positions have no legal moves.
  pos.set("rnb1kbnr/pppp1ppp/8/4p3/6Pq/5P2/PPPPP2P/RNBQKBNR w KQkq - 1 3");
  MoveList mate;
  generate_legal(pos, mate);
  CHECK_EQ(mate.size(), 0);
  CHECK(pos.in_check());
  pos.set("7k/5Q2/6K1/8/8/8/8/8 b - - 0 1");
  MoveList stale;
  generate_legal(pos, stale);
  CHECK_EQ(stale.size(), 0);
  CHECK(!pos.in_check());
}

void test_uci_moves() {
  Position pos;
  Move e4 = parse_uci_move(pos, "e2e4");
  CHECK(e4 && e4.flags() == Move::DOUBLE_PUSH && e4.uci() == "e2e4");
  CHECK(!parse_uci_move(pos, "e2e5"));
  pos.make_move(e4);
  CHECK(pos.ep_square() == 20);
  CHECK(pos.fen() == "rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq e3 0 1");

  pos.set("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  Move oo = parse_uci_move(pos, "e1g1");
  CHECK(oo && oo.flags() == Move::KING_CASTLE);
  pos.make_move(oo);
  CHECK(pos.fen() == "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
  pos.unmake_move();
  CHECK(pos.fen() == "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");

  pos.set("8/P6k/8/8/8/8/8/K7 w - - 0 1");
  Move promo = parse_uci_move(pos, "a7a8q");
  CHECK(promo && promo.is_promotion() && promo.promotion_type() == QUEEN);
  CHECK(parse_uci_move(pos, "a7a8n").promotion_type() == KNIGHT);
}

void test_castling_rules() {
  Position pos;
  // Cannot castle out of, through or into check.
  pos.set("4k3/8/8/8/8/8/4r3/R3K2R w KQ - 0 1");
  CHECK(!parse_uci_move(pos, "e1g1") && !parse_uci_move(pos, "e1c1"));
  pos.set("4k3/8/8/8/8/8/5r2/R3K2R w KQ - 0 1");
  CHECK(!parse_uci_move(pos, "e1g1"));
  CHECK(parse_uci_move(pos, "e1c1"));
  pos.set("4k3/8/8/8/8/8/1r6/R3K2R w KQ - 0 1");  // b1 attacked: queenside still allowed
  CHECK(parse_uci_move(pos, "e1c1"));
  // Moving a rook or capturing it removes the right.
  pos.set("r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  pos.make_move(parse_uci_move(pos, "a1a8"));
  CHECK((pos.castling_rights() & (WHITE_OOO | BLACK_OOO)) == 0);
  CHECK((pos.castling_rights() & (WHITE_OO | BLACK_OO)) == (WHITE_OO | BLACK_OO));
}

void test_repetition_and_draws() {
  Position pos;
  const char* shuffle[] = {"g1f3", "g8f6", "f3g1", "f6g8"};
  CHECK(!pos.is_repetition());
  for (const char* m : shuffle) pos.make_move(parse_uci_move(pos, m));
  CHECK(pos.is_repetition() && !pos.is_threefold());
  for (const char* m : shuffle) pos.make_move(parse_uci_move(pos, m));
  CHECK(pos.is_threefold());

  // The search's rule: one earlier occurrence strictly inside the search,
  // or two in all. `ply` is the distance from the root, here the position
  // the shuffle starts from.
  auto play = [&pos](std::initializer_list<const char*> moves) {
    for (const char* m : moves) pos.make_move(parse_uci_move(pos, m));
  };
  pos.set(START_FEN);
  play({"e2e4", "e7e5", "g1f3", "g8f6", "f3g1", "f6g8"});
  CHECK(pos.is_repetition_draw(6));   // repeats the position at ply 2
  CHECK(!pos.is_repetition_draw(4));  // ...which is the root at ply 4: once at the root isn't enough
  pos.set(START_FEN);
  play({"g1f3", "g8f6", "f3g1", "f6g8"});
  CHECK(!pos.is_repetition_draw(1));  // one occurrence before the root (the game's start)
  play({"g1f3", "g8f6", "f3g1", "f6g8"});
  CHECK(pos.is_repetition_draw(1));   // two before the root: threefold
  CHECK(pos.is_repetition_draw(4));   // one at the root, one before it
  CHECK(pos.is_repetition_draw(5));   // one inside the search (4 plies back)

  // Not across a null move: White passes, then wins the tempo back with a
  // king triangle (a1-a2-b1-a1), reaching the starting position with White
  // to move again. Positions before the pass don't count; after it they do.
  pos.set("8/8/8/8/8/8/8/K6k w - - 0 1");
  pos.make_null_move();
  play({"h1h2", "a1a2", "h2h1", "a2b1", "h1h2", "b1a1", "h2h1"});
  CHECK(pos.fen().rfind("8/8/8/8/8/8/8/K6k w", 0) == 0);
  CHECK(pos.repetitions() == 0 && !pos.is_repetition_draw(8));
  play({"a1a2", "h1h2", "a2a1", "h2h1"});
  CHECK(pos.repetitions() == 1 && pos.is_repetition_draw(12));

  pos.set("8/8/8/4k3/8/8/8/R3K3 w - - 99 80");
  CHECK(!pos.is_fifty_move());
  pos.make_move(parse_uci_move(pos, "a1a2"));
  CHECK(pos.is_fifty_move());

  pos.set("8/8/4k3/8/8/3K4/8/8 w - - 0 1");
  CHECK(pos.insufficient_material());
  pos.set("8/8/4k3/8/8/3K4/8/6B1 w - - 0 1");
  CHECK(pos.insufficient_material());
  pos.set("8/8/4k3/8/8/3K4/8/6N1 w - - 0 1");
  CHECK(pos.insufficient_material());
  pos.set("8/8/4k3/8/8/3K4/8/5NN1 w - - 0 1");
  CHECK(!pos.insufficient_material());
  pos.set("8/8/4k3/8/8/3K4/P7/8 w - - 0 1");
  CHECK(!pos.insufficient_material());
  pos.set("8/8/4kb2/8/8/3KB3/8/8 w - - 0 1");  // f6 and e3 are both dark squares
  CHECK(pos.insufficient_material());
  pos.set("8/8/4kb2/8/8/3K4/8/1B6 w - - 0 1");  // f6 dark, b1 light: mate is possible
  CHECK(!pos.insufficient_material());
}

void test_gives_check_special_moves() {
  struct Case {
    const char* fen;
    const char* move;
    bool check;
  } cases[] = {
      {"5k2/8/8/8/8/8/8/4K2R w K - 0 1", "e1g1", true},                 // castling: the rook checks on f1
      {"4k3/8/8/8/8/8/8/R3K3 w Q - 0 1", "e1c1", false},                // rook to d1, king on e8
      {"3k4/8/8/8/8/8/8/R3K3 w Q - 0 1", "e1c1", true},                 // rook to d1 checks d8
      {"8/8/8/1k1pP2R/8/8/8/4K3 w - d6 0 1", "e5d6", true},             // en passant opens the rank
      {"8/8/8/1k1pP3/8/8/8/4K3 w - d6 0 1", "e5d6", false},
      {"3k4/1P6/8/8/8/8/8/4K3 w - - 0 1", "b7b8n", false},              // knight on b8 doesn't reach d8
      {"8/1P2k3/8/8/8/8/8/4K3 w - - 0 1", "b7b8n", false},
      {"8/1P6/2k5/8/8/8/8/4K3 w - - 0 1", "b7b8n", true},               // knight on b8 checks c6
      {"4k3/8/8/4N3/8/8/8/4R1K1 w - - 0 1", "e5c6", true},              // discovered check by the rook
      {"4k3/8/8/4N3/8/8/8/4R1K1 w - - 0 1", "e5f7", true},              // discovered (and not direct)
      {"4k3/8/8/8/8/8/4P3/4R1K1 w - - 0 1", "e2e4", false},             // the pawn stays on the file
  };
  for (const Case& c : cases) {
    Position pos(c.fen);
    Move m = parse_uci_move(pos, c.move);
    CHECK(m);
    if (!m) continue;
    CHECK(pos.gives_check(m) == c.check);
    pos.make_move(m);
    CHECK(pos.in_check() == c.check);
  }
}

void test_null_move() {
  Position pos("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
  Key before = pos.key();
  std::string fen = pos.fen();
  pos.make_null_move();
  CHECK(pos.side_to_move() == BLACK && pos.ep_square() == NO_SQUARE);
  CHECK_EQ(pos.key(), pos.compute_key());
  pos.unmake_null_move();
  CHECK_EQ(pos.key(), before);
  CHECK(pos.fen() == fen);
}

// Random games: after every move the incremental hash and the bitboards must
// agree with a from-scratch recomputation, and unmaking must restore exactly.
void test_random_playouts() {
  std::mt19937_64 rng(12345);
  long moves_played = 0, checks_tested = 0;
  for (const char* fen : FENS) {
    for (int game = 0; game < 200; game++) {
      Position pos(fen);
      std::vector<std::string> fens{pos.fen()};
      std::vector<Key> keys{pos.key()};
      for (int ply = 0; ply < 150; ply++) {
        MoveList list;
        generate_legal(pos, list);
        if (list.size() == 0) break;
        // gives_check must agree with making the move and looking.
        for (Move cand : list) {
          const bool predicted = pos.gives_check(cand);
          pos.make_move(cand);
          const bool actual = pos.in_check();
          pos.unmake_move();
          if (predicted != actual) {
            std::printf("gives_check(%s) = %d, but the move %s check: %s\n", cand.uci().c_str(), predicted,
                        actual ? "gives" : "does not give", pos.fen().c_str());
            CHECK(false);
            return;
          }
          checks_tested++;
        }
        Move m = list[rng() % list.size()];
        pos.make_move(m);
        moves_played++;
        if (pos.key() != pos.compute_key() || !pos.is_consistent()) {
          std::printf("inconsistent after %s from %s\n", m.uci().c_str(), fens.back().c_str());
          CHECK(false);
          return;
        }
        // The side that just moved must not be left in check.
        CHECK(!pos.is_attacked(pos.king_square(~pos.side_to_move()), pos.side_to_move()));
        fens.push_back(pos.fen());
        keys.push_back(pos.key());
      }
      // The FEN written must read back to the same position and hash.
      Position copy;
      CHECK(copy.set(pos.fen()) && copy.key() == pos.key());
      while (fens.size() > 1) {
        pos.unmake_move();
        fens.pop_back();
        keys.pop_back();
        if (pos.fen() != fens.back() || pos.key() != keys.back()) {
          std::printf("unmake mismatch, expected %s got %s\n", fens.back().c_str(), pos.fen().c_str());
          CHECK(false);
          return;
        }
      }
    }
  }
  std::printf("random playouts: %ld moves checked, gives_check tested on %ld\n", moves_played, checks_tested);
}

// The same position reached by different move orders must hash the same.
void test_transposition_hash() {
  Position a, b;
  for (const char* m : {"e2e4", "e7e5", "g1f3", "b8c6"}) a.make_move(parse_uci_move(a, m));
  for (const char* m : {"g1f3", "b8c6", "e2e4", "e7e5"}) b.make_move(parse_uci_move(b, m));
  CHECK_EQ(a.key(), b.key());
  // An en passant square that no pawn can use is not part of the hash.
  Position c, d;
  for (const char* m : {"e2e4", "a7a6"}) c.make_move(parse_uci_move(c, m));
  d.set("rnbqkbnr/1ppppppp/p7/8/4P3/8/PPPP1PPP/RNBQKBNR w KQkq - 0 2");
  CHECK_EQ(c.key(), d.key());
}

}  // namespace

int main() {
  init_all();
  test_fen_round_trip();
  test_move_counts();
  test_uci_moves();
  test_castling_rules();
  test_repetition_and_draws();
  test_null_move();
  test_gives_check_special_moves();
  test_transposition_hash();
  test_random_playouts();
  std::printf(g_failures ? "%d FAILED\n" : "all position tests pass\n", g_failures);
  return g_failures ? 1 : 0;
}
