// Bitboards and precomputed attack tables, including magic bitboards for
// sliding pieces. Call init_bitboards() once before using anything here.
#pragma once

#include <bit>

#include "types.h"

namespace chess {

constexpr Bitboard FILE_A = 0x0101010101010101ULL;
constexpr Bitboard FILE_H = FILE_A << 7;
constexpr Bitboard RANK_1 = 0xFFULL;
constexpr Bitboard RANK_3 = RANK_1 << 16;
constexpr Bitboard RANK_6 = RANK_1 << 40;
constexpr Bitboard RANK_8 = RANK_1 << 56;

constexpr Bitboard square_bb(Square s) { return 1ULL << s; }
constexpr Bitboard file_bb(Square s) { return FILE_A << file_of(s); }
constexpr Bitboard rank_bb(Square s) { return RANK_1 << (8 * rank_of(s)); }

inline int popcount(Bitboard b) { return std::popcount(b); }
inline Square lsb(Bitboard b) { return std::countr_zero(b); }
inline Square pop_lsb(Bitboard& b) {
  Square s = lsb(b);
  b &= b - 1;
  return s;
}
constexpr bool more_than_one(Bitboard b) { return b & (b - 1); }

// Shifts by a board direction, masking off squares that would wrap around.
template <int D>
constexpr Bitboard shift(Bitboard b) {
  if constexpr (D == 8) return b << 8;
  else if constexpr (D == -8) return b >> 8;
  else if constexpr (D == 9) return (b & ~FILE_H) << 9;
  else if constexpr (D == 7) return (b & ~FILE_A) << 7;
  else if constexpr (D == -7) return (b & ~FILE_H) >> 7;
  else if constexpr (D == -9) return (b & ~FILE_A) >> 9;
  else return 0;
}

struct Magic {
  Bitboard mask;
  Bitboard magic;
  Bitboard* attacks;
  unsigned shift;
};

extern Bitboard PawnAttacks[COLOR_NB][64];
extern Bitboard KnightAttacks[64];
extern Bitboard KingAttacks[64];
extern Bitboard BetweenBB[64][64];  // squares strictly between two aligned squares
extern Bitboard LineBB[64][64];     // the whole line through two aligned squares
extern Magic RookMagics[64];
extern Magic BishopMagics[64];

inline Bitboard rook_attacks(Square s, Bitboard occ) {
  const Magic& m = RookMagics[s];
  return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}
inline Bitboard bishop_attacks(Square s, Bitboard occ) {
  const Magic& m = BishopMagics[s];
  return m.attacks[((occ & m.mask) * m.magic) >> m.shift];
}

inline Bitboard attacks_bb(PieceType pt, Square s, Bitboard occ) {
  switch (pt) {
    case KNIGHT: return KnightAttacks[s];
    case BISHOP: return bishop_attacks(s, occ);
    case ROOK: return rook_attacks(s, occ);
    case QUEEN: return bishop_attacks(s, occ) | rook_attacks(s, occ);
    case KING: return KingAttacks[s];
    default: return 0;
  }
}

// True if s1, s2 and s3 lie on one line (rank, file or diagonal).
inline bool aligned(Square s1, Square s2, Square s3) { return LineBB[s1][s2] & square_bb(s3); }

void init_bitboards();

}  // namespace chess
