#include "movegen.h"

namespace chess {

namespace {

void add_promotions(MoveList& list, Square from, Square to, bool capture, bool tactical) {
  int base = Move::PROMO | (capture ? Move::CAPTURE : 0);
  for (int i = 3; i >= (tactical ? 3 : 0); i--) list.add(Move(from, to, base + i));  // queen first
}

template <Color Us>
void generate_pawn_moves(const Position& pos, MoveList& list, bool tactical) {
  constexpr Color Them = ~Us;
  constexpr int Up = Us == WHITE ? 8 : -8;
  constexpr Bitboard PromoRank = Us == WHITE ? RANK_8 : RANK_1;
  constexpr Bitboard DoubleFrom = Us == WHITE ? RANK_3 : RANK_6;  // rank reached by a single push

  const Bitboard pawns = pos.pieces(Us, PAWN);
  const Bitboard empty = ~pos.pieces();
  const Bitboard enemy = pos.pieces(Them);

  Bitboard single = shift<Up>(pawns) & empty;
  Bitboard dbl = tactical ? 0 : shift<Up>(single & DoubleFrom) & empty;
  if (tactical) single &= PromoRank;

  while (single) {
    Square to = pop_lsb(single);
    if (square_bb(to) & PromoRank) add_promotions(list, to - Up, to, false, tactical);
    else list.add(Move(to - Up, to, Move::QUIET));
  }
  while (dbl) {
    Square to = pop_lsb(dbl);
    list.add(Move(to - 2 * Up, to, Move::DOUBLE_PUSH));
  }

  Bitboard p = pawns;
  while (p) {
    Square from = pop_lsb(p);
    Bitboard caps = PawnAttacks[Us][from] & enemy;
    while (caps) {
      Square to = pop_lsb(caps);
      if (square_bb(to) & PromoRank) add_promotions(list, from, to, true, tactical);
      else list.add(Move(from, to, Move::CAPTURE));
    }
    if (pos.ep_square() != NO_SQUARE && (PawnAttacks[Us][from] & square_bb(pos.ep_square())))
      list.add(Move(from, pos.ep_square(), Move::EP));
  }
}

void generate_piece_moves(const Position& pos, MoveList& list, Color us, bool tactical) {
  const Bitboard occ = pos.pieces();
  const Bitboard enemy = pos.pieces(~us);
  const Bitboard targets_mask = tactical ? enemy : ~pos.pieces(us);
  for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN, KING}) {
    Bitboard from_bb = pos.pieces(us, pt);
    while (from_bb) {
      Square from = pop_lsb(from_bb);
      Bitboard targets = attacks_bb(pt, from, occ) & targets_mask;
      while (targets) {
        Square to = pop_lsb(targets);
        list.add(Move(from, to, (enemy & square_bb(to)) ? Move::CAPTURE : Move::QUIET));
      }
    }
  }
}

void generate_castling(const Position& pos, MoveList& list, Color us) {
  if (pos.in_check()) return;
  const int rights = pos.castling_rights();
  const bool kingside = rights & (us == WHITE ? WHITE_OO : BLACK_OO);
  const bool queenside = rights & (us == WHITE ? WHITE_OOO : BLACK_OOO);
  const Square king = relative_square(us, 4);
  const Bitboard occ = pos.pieces();
  const Color them = ~us;

  if (kingside && !(occ & (square_bb(king + 1) | square_bb(king + 2))) &&
      !pos.is_attacked(king + 1, them) && !pos.is_attacked(king + 2, them))
    list.add(Move(king, king + 2, Move::KING_CASTLE));
  if (queenside && !(occ & (square_bb(king - 1) | square_bb(king - 2) | square_bb(king - 3))) &&
      !pos.is_attacked(king - 1, them) && !pos.is_attacked(king - 2, them))
    list.add(Move(king, king - 2, Move::QUEEN_CASTLE));
}

}  // namespace

void generate_pseudo_legal(const Position& pos, MoveList& list, bool tactical) {
  const Color us = pos.side_to_move();
  if (us == WHITE) generate_pawn_moves<WHITE>(pos, list, tactical);
  else generate_pawn_moves<BLACK>(pos, list, tactical);
  generate_piece_moves(pos, list, us, tactical);
  if (!tactical) generate_castling(pos, list, us);
}

void generate_legal(const Position& pos, MoveList& list) {
  MoveList pseudo;
  generate_pseudo_legal(pos, pseudo);
  for (Move m : pseudo)
    if (pos.legal(m)) list.add(m);
}

Move parse_uci_move(const Position& pos, const std::string& text) {
  MoveList list;
  generate_legal(pos, list);
  for (Move m : list)
    if (m.uci() == text) return m;
  return Move();
}

std::uint64_t perft(Position& pos, int depth) {
  if (depth == 0) return 1;
  MoveList list;
  generate_legal(pos, list);
  if (depth == 1) return list.size();
  std::uint64_t nodes = 0;
  for (Move m : list) {
    pos.make_move(m);
    nodes += perft(pos, depth - 1);
    pos.unmake_move();
  }
  return nodes;
}

}  // namespace chess
