#include "syzygy.h"

#ifdef ENGINE_SYZYGY
#include "fathom/tbprobe.h"
#endif

namespace chess::tb {

#ifdef ENGINE_SYZYGY

namespace {

struct Boards {
  std::uint64_t white, black, kings, queens, rooks, bishops, knights, pawns;
  unsigned ep;
  bool turn;
};

Boards boards(const Position& pos) {
  Boards b;
  b.white = pos.pieces(WHITE);
  b.black = pos.pieces(BLACK);
  b.kings = pos.pieces(KING);
  b.queens = pos.pieces(QUEEN);
  b.rooks = pos.pieces(ROOK);
  b.bishops = pos.pieces(BISHOP);
  b.knights = pos.pieces(KNIGHT);
  b.pawns = pos.pieces(PAWN);
  b.ep = pos.ep_square() == NO_SQUARE ? 0 : unsigned(pos.ep_square());
  b.turn = pos.side_to_move() == WHITE;
  return b;
}

Wdl to_wdl(unsigned r) {
  switch (r) {
    case TB_WIN: return WIN;
    case TB_LOSS: return LOSS;
    default: return DRAW;  // draw, cursed win, blessed loss
  }
}

}  // namespace

int init(const std::string& path) {
  tb_init(path.c_str());  // an empty path unloads the tables
  return int(TB_LARGEST);
}

int largest() { return int(TB_LARGEST); }

Wdl probe_wdl(const Position& pos) {
  const Boards b = boards(pos);
  const unsigned r = tb_probe_wdl(b.white, b.black, b.kings, b.queens, b.rooks, b.bishops, b.knights, b.pawns,
                                  unsigned(pos.halfmove_clock()), unsigned(pos.castling_rights()), b.ep, b.turn);
  return r == TB_RESULT_FAILED ? FAILED : to_wdl(r);
}

bool probe_root(const Position& pos, const MoveList& legal, Move& move, Wdl& wdl) {
  const Boards b = boards(pos);
  const unsigned r = tb_probe_root(b.white, b.black, b.kings, b.queens, b.rooks, b.bishops, b.knights, b.pawns,
                                   unsigned(pos.halfmove_clock()), unsigned(pos.castling_rights()), b.ep, b.turn,
                                   nullptr);
  if (r == TB_RESULT_FAILED || r == TB_RESULT_CHECKMATE || r == TB_RESULT_STALEMATE) return false;
  const Square from = Square(TB_GET_FROM(r)), to = Square(TB_GET_TO(r));
  const unsigned promotes = TB_GET_PROMOTES(r);
  // Fathom: 1 queen, 2 rook, 3 bishop, 4 knight.
  static constexpr PieceType PROMO[5] = {PAWN, QUEEN, ROOK, BISHOP, KNIGHT};
  for (int i = 0; i < legal.size(); i++) {
    const Move m = legal[i];
    if (m.from() != from || m.to() != to) continue;
    if (promotes ? !m.is_promotion() || m.promotion_type() != PROMO[promotes] : m.is_promotion()) continue;
    move = m;
    wdl = to_wdl(TB_GET_WDL(r));
    return true;
  }
  return false;
}

#else  // no tablebase support (WebAssembly)

int init(const std::string&) { return 0; }
int largest() { return 0; }
Wdl probe_wdl(const Position&) { return FAILED; }
bool probe_root(const Position&, const MoveList&, Move&, Wdl&) { return false; }

#endif

}  // namespace chess::tb
