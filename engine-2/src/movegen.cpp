#include "movegen.h"

namespace chess {

namespace {

void add_promotions(MoveList& list, Square from, Square to, bool capture, bool queen_only) {
  int base = Move::PROMO | (capture ? Move::CAPTURE : 0);
  for (int i = 3; i >= (queen_only ? 3 : 0); i--) list.add(Move(from, to, base + i));  // queen first
}

template <Color Us, GenType Type>
void generate_pawn_moves(const Position& pos, MoveList& list) {
  constexpr Color Them = ~Us;
  constexpr int Up = Us == WHITE ? 8 : -8;
  constexpr Bitboard PromoRank = Us == WHITE ? RANK_8 : RANK_1;
  constexpr Bitboard DoubleFrom = Us == WHITE ? RANK_3 : RANK_6;  // rank reached by a single push
  constexpr bool queen_only = Type == TACTICAL;

  const Bitboard pawns = pos.pieces(Us, PAWN);
  const Bitboard empty = ~pos.pieces();
  const Bitboard enemy = pos.pieces(Them);

  if constexpr (Type != CAPTURES) {
    Bitboard single = shift<Up>(pawns) & empty;
    Bitboard dbl = Type == TACTICAL ? 0 : shift<Up>(single & DoubleFrom) & empty;
    if (Type == TACTICAL) single &= PromoRank;

    while (single) {
      Square to = pop_lsb(single);
      if (square_bb(to) & PromoRank) add_promotions(list, to - Up, to, false, queen_only);
      else list.add(Move(to - Up, to, Move::QUIET));
    }
    while (dbl) {
      Square to = pop_lsb(dbl);
      list.add(Move(to - 2 * Up, to, Move::DOUBLE_PUSH));
    }
  }

  if constexpr (Type != QUIETS) {
    Bitboard p = pawns;
    while (p) {
      Square from = pop_lsb(p);
      Bitboard caps = PawnAttacks[Us][from] & enemy;
      while (caps) {
        Square to = pop_lsb(caps);
        if (square_bb(to) & PromoRank) add_promotions(list, from, to, true, queen_only);
        else list.add(Move(from, to, Move::CAPTURE));
      }
      if (pos.ep_square() != NO_SQUARE && (PawnAttacks[Us][from] & square_bb(pos.ep_square())))
        list.add(Move(from, pos.ep_square(), Move::EP));
    }
  }
}

template <GenType Type>
void generate_piece_moves(const Position& pos, MoveList& list, Color us) {
  const Bitboard occ = pos.pieces();
  const Bitboard enemy = pos.pieces(~us);
  const Bitboard targets_mask = Type == ALL ? ~pos.pieces(us) : Type == QUIETS ? ~occ : enemy;
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

template <GenType Type>
void generate(const Position& pos, MoveList& list) {
  const Color us = pos.side_to_move();
  if (us == WHITE) generate_pawn_moves<WHITE, Type>(pos, list);
  else generate_pawn_moves<BLACK, Type>(pos, list);
  generate_piece_moves<Type>(pos, list, us);
  if (Type == ALL || Type == QUIETS) generate_castling(pos, list, us);
}

template void generate<ALL>(const Position&, MoveList&);
template void generate<CAPTURES>(const Position&, MoveList&);
template void generate<QUIETS>(const Position&, MoveList&);
template void generate<TACTICAL>(const Position&, MoveList&);

void generate_pseudo_legal(const Position& pos, MoveList& list, bool tactical) {
  if (tactical) generate<TACTICAL>(pos, list);
  else generate<ALL>(pos, list);
}

bool pseudo_legal(const Position& pos, Move m) {
  const Square from = m.from(), to = m.to();
  const Piece pc = pos.piece_on(from);
  if (!m || pc == NO_PIECE || color_of(pc) != pos.side_to_move()) return false;
  if (type_of(pc) == PAWN || m.is_castle()) {
    // Fiddly special cases (pushes, en passant, promotions, castling): ask
    // the generator, for that kind of move only.
    MoveList list;
    if (m.is_castle()) generate_castling(pos, list, pos.side_to_move());
    else if (pos.side_to_move() == WHITE) generate_pawn_moves<WHITE, ALL>(pos, list);
    else generate_pawn_moves<BLACK, ALL>(pos, list);
    for (Move x : list)
      if (x == m) return true;
    return false;
  }
  const int flags = pos.piece_on(to) == NO_PIECE ? Move::QUIET : Move::CAPTURE;
  return m.flags() == flags && (attacks_bb(type_of(pc), from, pos.pieces()) & ~pos.pieces(pos.side_to_move()) & square_bb(to));
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
