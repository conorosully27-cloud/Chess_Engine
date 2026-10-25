#include "endgame.h"

#include <algorithm>
#include <cstdlib>

#include "bitboard.h"

namespace chess {

namespace {

constexpr Bitboard LIGHT_SQUARES = 0x55AA55AA55AA55AAULL;

// Rough piece values, for comparing material only.
constexpr int KNIGHT_VALUE = 320, BISHOP_VALUE = 330, ROOK_VALUE = 500, QUEEN_VALUE = 900;

int non_pawn_material(const Position& pos, Color c) {
  return KNIGHT_VALUE * popcount(pos.pieces(c, KNIGHT)) + BISHOP_VALUE * popcount(pos.pieces(c, BISHOP)) +
         ROOK_VALUE * popcount(pos.pieces(c, ROOK)) + QUEEN_VALUE * popcount(pos.pieces(c, QUEEN));
}

int distance(Square a, Square b) {
  return std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b)));
}

}  // namespace

int endgame_scale(const Position& pos, int eval) {
  if (eval == 0) return SCALE_NORMAL;
  const Color strong = eval > 0 ? pos.side_to_move() : ~pos.side_to_move();
  const Color weak = ~strong;
  const int strong_material = non_pawn_material(pos, strong), weak_material = non_pawn_material(pos, weak);
  const Bitboard strong_pawns = pos.pieces(strong, PAWN), weak_pawns = pos.pieces(weak, PAWN);
  const Bitboard strong_bishops = pos.pieces(strong, BISHOP);

  if (!strong_pawns) {
    // Without pawns, a minor piece's worth of extra material rarely wins:
    // a lone minor can't mate at all, rook against minor is drawn, rook and
    // minor against rook nearly always (Stockfish's rule).
    if (strong_material - weak_material <= BISHOP_VALUE)
      return strong_material < ROOK_VALUE ? 0 : weak_material <= BISHOP_VALUE ? 4 : 14;
    // Two knights can't force mate on a bare king.
    if (strong_material == 2 * KNIGHT_VALUE && popcount(pos.pieces(strong, KNIGHT)) == 2 && !weak_material &&
        !weak_pawns)
      return 0;
  }

  const Bitboard heavy_and_knights = pos.pieces(KNIGHT) | pos.pieces(ROOK) | pos.pieces(QUEEN);

  // Opposite-coloured bishops with only pawns besides: drawish even a pawn
  // or two up, since the defender's bishop holds the squares the other
  // can't contest.
  const Bitboard weak_bishops = pos.pieces(weak, BISHOP);
  if (!heavy_and_knights && popcount(strong_bishops) == 1 && popcount(weak_bishops) == 1 &&
      bool(strong_bishops & LIGHT_SQUARES) != bool(weak_bishops & LIGHT_SQUARES))
    return popcount(strong_pawns) - popcount(weak_pawns) <= 1 ? 16 : 32;

  // Rook pawns that can't promote: pawns all on the a- or h-file, at most a
  // bishop that doesn't cover the queening square, and the bare defending
  // king next to that square, from where it can't be driven off.
  if (strong_pawns && !weak_pawns && !weak_material && !(heavy_and_knights & pos.pieces(strong)) &&
      popcount(strong_bishops) <= 1) {
    const bool a_file = !(strong_pawns & ~FILE_A), h_file = !(strong_pawns & ~FILE_H);
    if (a_file || h_file) {
      const Square queening = Square((strong == WHITE ? 56 : 0) + (a_file ? 0 : 7));
      const bool bishop_covers =
          strong_bishops && bool(strong_bishops & LIGHT_SQUARES) == bool(square_bb(queening) & LIGHT_SQUARES);
      if (!bishop_covers && distance(pos.king_square(weak), queening) <= 1) return 0;
    }
  }

  return SCALE_NORMAL;
}

}  // namespace chess
