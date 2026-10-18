#include "bitboard.h"

#include <cstring>

namespace chess {

Bitboard PawnAttacks[COLOR_NB][64];
Bitboard KnightAttacks[64];
Bitboard KingAttacks[64];
Bitboard BetweenBB[64][64];
Bitboard LineBB[64][64];
Magic RookMagics[64];
Magic BishopMagics[64];

namespace {

Bitboard RookTable[0x19000];
Bitboard BishopTable[0x1480];

// Small xorshift generator; fixed seeds make the magics reproducible.
struct Prng {
  explicit Prng(std::uint64_t seed) : s(seed) {}
  std::uint64_t next() {
    s ^= s >> 12;
    s ^= s << 25;
    s ^= s >> 27;
    return s * 2685821657736338717ULL;
  }
  // Few set bits make better magic candidates.
  std::uint64_t sparse() { return next() & next() & next(); }
  std::uint64_t s;
};

Bitboard sliding_attacks(PieceType pt, Square sq, Bitboard occ) {
  static const int rook_dirs[4][2] = {{0, 1}, {0, -1}, {1, 0}, {-1, 0}};
  static const int bishop_dirs[4][2] = {{1, 1}, {1, -1}, {-1, 1}, {-1, -1}};
  const auto& dirs = pt == ROOK ? rook_dirs : bishop_dirs;
  Bitboard result = 0;
  for (const auto& d : dirs) {
    int f = file_of(sq) + d[0];
    int r = rank_of(sq) + d[1];
    while (f >= 0 && f < 8 && r >= 0 && r < 8) {
      Bitboard b = square_bb(r * 8 + f);
      result |= b;
      if (occ & b) break;
      f += d[0];
      r += d[1];
    }
  }
  return result;
}

void init_magics(PieceType pt, Magic magics[], Bitboard table[]) {
  static const std::uint64_t seeds[8] = {728, 10316, 55013, 32803, 12281, 15100, 16645, 255};
  Bitboard occupancy[4096], reference[4096], used[4096];
  int epoch[4096] = {};
  int attempt = 0;
  Bitboard* next_table = table;

  for (Square s = 0; s < 64; s++) {
    Magic& m = magics[s];
    Bitboard edges = ((RANK_1 | RANK_8) & ~rank_bb(s)) | ((FILE_A | FILE_H) & ~file_bb(s));
    m.mask = sliding_attacks(pt, s, 0) & ~edges;
    m.shift = 64 - popcount(m.mask);
    m.attacks = next_table;
    next_table += 1 << popcount(m.mask);

    // Enumerate every subset of the mask (carry-rippler) with its true attacks.
    int size = 0;
    Bitboard b = 0;
    do {
      occupancy[size] = b;
      reference[size] = sliding_attacks(pt, s, b);
      size++;
      b = (b - m.mask) & m.mask;
    } while (b);

    Prng rng(seeds[rank_of(s)]);
    for (int i = 0; i < size;) {
      m.magic = 0;
      while (popcount((m.mask * m.magic) >> 56) < 6) m.magic = rng.sparse();
      attempt++;
      for (i = 0; i < size; i++) {
        unsigned idx = unsigned(((occupancy[i] & m.mask) * m.magic) >> m.shift);
        if (epoch[idx] < attempt) {
          epoch[idx] = attempt;
          used[idx] = reference[i];
        } else if (used[idx] != reference[i]) {
          break;  // collision between different attack sets: try another magic
        }
      }
    }
    for (int j = 0; j < size; j++) {
      m.attacks[((occupancy[j] & m.mask) * m.magic) >> m.shift] = reference[j];
    }
  }
}

}  // namespace

void init_bitboards() {
  init_magics(ROOK, RookMagics, RookTable);
  init_magics(BISHOP, BishopMagics, BishopTable);

  for (Square s = 0; s < 64; s++) {
    Bitboard b = square_bb(s);
    PawnAttacks[WHITE][s] = shift<9>(b) | shift<7>(b);
    PawnAttacks[BLACK][s] = shift<-7>(b) | shift<-9>(b);

    Bitboard n = 0;
    static const int knight_steps[8][2] = {{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}};
    for (const auto& d : knight_steps) {
      int f = file_of(s) + d[0], r = rank_of(s) + d[1];
      if (f >= 0 && f < 8 && r >= 0 && r < 8) n |= square_bb(r * 8 + f);
    }
    KnightAttacks[s] = n;

    KingAttacks[s] = shift<8>(b) | shift<-8>(b) | shift<9>(b) | shift<7>(b) | shift<-7>(b) |
                     shift<-9>(b) | ((b & ~FILE_H) << 1) | ((b & ~FILE_A) >> 1);
  }

  for (Square s1 = 0; s1 < 64; s1++) {
    for (Square s2 = 0; s2 < 64; s2++) {
      BetweenBB[s1][s2] = 0;
      LineBB[s1][s2] = 0;
      if (s1 == s2) continue;
      for (PieceType pt : {BISHOP, ROOK}) {
        if (attacks_bb(pt, s1, 0) & square_bb(s2)) {
          LineBB[s1][s2] = (attacks_bb(pt, s1, 0) & attacks_bb(pt, s2, 0)) | square_bb(s1) | square_bb(s2);
          BetweenBB[s1][s2] = attacks_bb(pt, s1, square_bb(s2)) & attacks_bb(pt, s2, square_bb(s1));
        }
      }
    }
  }
}

}  // namespace chess
