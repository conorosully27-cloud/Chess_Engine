#include "evaluate.h"

#include <algorithm>
#include <cstdlib>

namespace chess {

namespace {

// ---------------------------------------------------------------- weights
// Copied from engine-1/src/engine/weights.js. Tables are written from
// White's side with rank 8 first; Black uses them mirrored.

constexpr int MATERIAL_MG[6] = {115, 472, 503, 685, 1438, 0};
constexpr int MATERIAL_EG[6] = {85, 298, 307, 538, 990, 0};

constexpr int PST_MG[6][64] = {
    {   0,    0,    0,    0,    0,    0,    0,    0,
       14,    8,   -9,   53,  124,  105,   32,  -32,
       -3,   -2,   40,   16,   56,   93,   24,   -7,
        4,   10,   11,   26,   39,   16,    3,  -18,
      -19,  -14,    9,   29,   24,   19,   -3,  -23,
      -14,  -17,   12,    2,   21,   13,   28,    0,
      -21,   -7,   -8,  -12,   -1,   31,   37,  -13,
        0,    0,    0,    0,    0,    0,    0,    0},
    {-270,  -46,  -42,  -72,   23, -157, -157, -154,
      -55,    4,  122,   41,  125,   97,   31,   -6,
       29,   59,   62,  100,  123,  160,   82,    9,
       51,   56,   51,   79,   39,   47,   24,   52,
       35,   40,   64,   53,   57,   58,   25,    9,
       17,   41,   58,   66,   73,   58,   68,   17,
       21,    7,   43,   58,   58,   59,   44,   34,
       -5,   18,    6,   21,   23,   38,   13,  -43},
    { -27,  -41,  -40, -104, -127,  -32,  -12,  -66,
      -17,   25,   -2,    8,   20,   67,  -13,   29,
       12,   19,   81,   24,   87,   67,   67,   44,
       -2,   18,   24,   41,   37,   -7,   22,    7,
       24,   23,   21,   49,   49,   18,   29,    6,
       46,   49,   49,   25,   36,   53,   38,   39,
       10,   61,   38,   33,   41,   61,   78,   36,
       43,   17,   29,   26,   18,   11,   12,   36},
    {  47,   86,   69,   84,  103,  171,  104,   20,
       93,   66,  105,  136,   97,  149,  171,  134,
       22,   11,   -4,   25,   41,   61,   94,   32,
      -10,    3,   24,   41,    7,   26,   33,   37,
       -3,  -17,  -21,   -1,    6,    4,   12,    2,
       -5,   -2,    6,   27,   18,   18,   26,   13,
       -4,    0,    0,   15,   21,   23,   37,  -39,
       18,   21,   32,   37,   43,   43,   -1,   27},
    { -15,  -11,   21,   78,  111,   64,   11,   -8,
       -4,   -8,  -10,  -54,  -35,   79,   10,  141,
        7,    9,   37,   43,   54,   54,  117,   60,
        7,   26,   21,    5,   10,    5,    4,   31,
       23,   23,   26,   14,   37,   32,   42,   28,
       32,   48,   39,   42,   43,   43,   50,   36,
       39,   52,   57,   62,   68,   79,   87,   44,
       56,   45,   57,   66,   51,   44,   20,   54},
    {   7,  -21,   14, -100,   91,   -2,  -85, -104,
      -66,   26,   10,   19,  -53,   32, -138, -148,
      -57,  117,   42,   73,  -45,   49,   42, -110,
     -101,  -16,   -4,   -9,  -18,  -35,   -2, -119,
      -63,   56,   22,  -28,    3,  -50,  -20, -149,
      -23,   42,  -12,  -25,  -10,   -2,   20,  -61,
       42,   26,   36,  -15,   -1,   18,   57,   26,
      -52,   48,   35,  -32,   25,  -14,   47,   26},
};

constexpr int PST_EG[6][64] = {
    {   0,    0,    0,    0,    0,    0,    0,    0,
       68,   88,  106,   75,   52,   66,  107,   78,
       32,   31,   27,   20,   10,    4,   26,   28,
       27,   21,   12,   -2,   -3,    5,   17,   16,
       22,   20,    2,   -5,   -2,    0,    7,    7,
       12,   11,    4,    9,    9,    8,   -2,   -2,
       18,   12,   19,   18,   15,   10,   -5,   -3,
        0,    0,    0,    0,    0,    0,    0,    0},
    { -15,  -52,  -18,   -5,  -19,  -10,  -11,  -90,
      -20,   -9,  -25,    2,  -27,  -24,  -24,  -39,
      -30,  -10,   22,   12,   -8,   -9,  -23,  -18,
       -7,   15,   23,   39,   40,   30,   18,  -21,
      -16,    6,   19,   25,   31,    8,   -1,  -11,
      -26,   -3,   -8,   12,   15,   -8,  -23,  -30,
      -34,  -18,  -13,  -12,  -15,  -12,  -19,  -31,
      -68,  -46,  -10,   -9,  -17,  -14,  -43,  -12},
    {   7,   -1,    9,   21,   25,    4,    8,   21,
        2,    1,    7,   -5,    2,   -7,   13,  -23,
       11,    7,   -8,   10,   -7,    6,    0,    2,
       18,   14,    8,    8,    5,    8,   -1,    6,
        0,   -1,   12,   -3,   -4,    9,   -6,   -7,
      -17,    3,   -2,    9,    7,   -4,   -9,  -11,
        4,  -21,  -12,   -5,   -1,   -9,  -13,  -16,
      -22,    3,  -17,    2,    5,    2,   -7,  -11},
    {  24,    7,   12,    6,    1,  -14,   12,   24,
       -6,   13,    0,  -12,   -4,   -5,  -14,  -12,
       28,   28,   31,   23,   18,   12,    7,   17,
       31,   25,   26,   17,   21,   20,   16,   15,
       23,   28,   32,   28,   25,   20,   13,   10,
       15,   18,   15,   13,   16,   10,    2,    2,
       14,   17,   26,   17,   13,   12,    8,   21,
       14,   15,   13,   12,    3,    5,   15,  -15},
    {  56,   62,   49,   30,    8,   62,   39,   93,
       69,   52,   62,  131,   91,   99,   81,  -45,
       27,   40,   42,   28,   77,   28,   -3,    9,
       51,   60,   41,   70,   64,   70,   57,   28,
       41,   52,   42,   65,   42,   27,   37,   26,
       -7,   -8,   33,   20,   31,   31,   46,   22,
       22,   14,   -1,    8,    6,  -33,  -51,   12,
       -9,   -3,   -5,  -25,   26,  -15,    0,  -22},
    { -53,  -16,  -17,   13,  -28,   -4,    8,  -19,
      -21,    0,   -2,   -3,   11,   17,   50,   21,
      -14,  -13,   -8,   -9,   15,   19,   23,   23,
      -20,   -2,    5,    6,    8,   21,   13,   13,
      -28,  -23,   -3,    8,    6,   15,    3,   13,
      -30,  -19,    2,    8,    7,    6,   -6,   -3,
      -39,  -16,   -6,    9,    9,    5,  -10,  -26,
      -34,  -37,  -15,   -8,  -22,   -9,  -39,  -67},
};

constexpr int BISHOP_PAIR[2] = {36, 54};
constexpr int DOUBLED_PAWN[2] = {-5, -11};
constexpr int ISOLATED_PAWN[2] = {-22, -10};
constexpr int PASSED_PAWN_MG[8] = {0, 0, -7, -11, 10, 17, 53, 0};
constexpr int PASSED_PAWN_EG[8] = {0, 24, 17, 38, 64, 100, 80, 0};
constexpr int ROOK_OPEN_FILE[2] = {48, -9};
constexpr int ROOK_SEMI_OPEN_FILE[2] = {20, -1};
constexpr int MOBILITY[6][2] = {{0, 0}, {2, -1}, {5, 4}, {3, 3}, {2, 3}, {0, 0}};
constexpr int MOBILITY_TYPICAL[6] = {0, 4, 6, 7, 13, 0};
constexpr int SHELTER_NEAR = 1, SHELTER_FAR = -1, SHELTER_NONE = -12;
constexpr int PASSED_OWN_KING[8] = {0, -3, -4, -11, -19, -23, -20, 0};
constexpr int PASSED_ENEMY_KING[8] = {0, -1, 3, 11, 24, 43, 54, 0};
constexpr int PASSED_BLOCKED[8] = {0, -9, -5, -13, -16, -47, -81, 0};
constexpr int ROOK_BEHIND_PASSER[2] = {19, 20};
constexpr int ROOK_BEHIND_ENEMY_PASSER[2] = {3, 50};
constexpr int ROOK_ON_SEVENTH[2] = {-89, 48};
constexpr int UNSTOPPABLE_PASSER = 235;
constexpr int PAWN_STORM[8] = {0, 3, 8, 0, -12, -15, 22, 0};
constexpr int PAWN_STORM_BLOCKED[8] = {0, 8, 7, 8, 1, -13, 0, 0};
constexpr int KING_OPEN_FILE[2] = {-12, -33};  // half-open, fully open
constexpr int TEMPO = 32;

// King danger attack units, indexed by the Danger terms below.
enum Danger {
  KNIGHT_ATTACKER, BISHOP_ATTACKER, ROOK_ATTACKER, QUEEN_ATTACKER,
  ZONE_ATTACKS, WEAK_SQUARES,
  KNIGHT_CHECK, BISHOP_CHECK, ROOK_CHECK, QUEEN_CHECK,
  NO_QUEEN, BASE, DANGER_TERMS,
};
constexpr int KING_DANGER[DANGER_TERMS] = {45, 21, 25, 12, 23, 33, 124, 90, 110, 38, -275, -8};
constexpr double DANGER_MG_DIVISOR = 720;
constexpr double DANGER_EG_DIVISOR = 20;

// Fixed piece values for mop-up; not tuned.
constexpr int MOPUP_VALUE[6] = {90, 320, 330, 480, 950, 0};
constexpr int PHASE_WEIGHT[6] = {0, 1, 1, 2, 4, 0};
constexpr int MAX_PHASE = 24;

// ------------------------------------------------------------- tables

struct Tables {
  int psq_mg[PIECE_NB][64];  // material + table, positive for both colors
  int psq_eg[PIECE_NB][64];
  // King zone: the king's square, its neighbors and the three squares two
  // ranks in front, for a king of each color on each square.
  Bitboard king_zone[COLOR_NB][64];

  Tables() {
    for (int c = 0; c < 2; c++) {
      for (int t = 0; t < 6; t++) {
        for (Square s = 0; s < 64; s++) {
          int idx = c == WHITE ? (7 - rank_of(s)) * 8 + file_of(s) : s;
          Piece pc = make_piece(Color(c), PieceType(t));
          psq_mg[pc][s] = MATERIAL_MG[t] + PST_MG[t][idx];
          psq_eg[pc][s] = MATERIAL_EG[t] + PST_EG[t][idx];
        }
      }
      for (Square k = 0; k < 64; k++) {
        Bitboard z = 0;
        for (int dr = -1; dr <= 1; dr++)
          for (int df = -1; df <= 1; df++) {
            int r = rank_of(k) + dr, f = file_of(k) + df;
            if (r >= 0 && r < 8 && f >= 0 && f < 8) z |= 1ULL << (r * 8 + f);
          }
        int fr = rank_of(k) + (c == WHITE ? 2 : -2);
        if (fr >= 0 && fr < 8)
          for (int df = -1; df <= 1; df++) {
            int f = file_of(k) + df;
            if (f >= 0 && f < 8) z |= 1ULL << (fr * 8 + f);
          }
        king_zone[c][k] = z;
      }
    }
  }
};
const Tables T;

constexpr int rel_rank(Color c, Square s) { return c == WHITE ? rank_of(s) : 7 - rank_of(s); }

int distance(Square a, Square b) {
  return std::max(std::abs(file_of(a) - file_of(b)), std::abs(rank_of(a) - rank_of(b)));
}

// Whose rook, if any, stands behind the pawn of color c on sq, with nothing
// in between: 0 white, 1 black, -1 none.
int rook_behind(const Position& pos, Square sq, Color c) {
  const int back = c == WHITE ? -8 : 8;
  for (Square s = sq + back; s >= 0 && s < 64; s += back) {
    Piece pc = pos.piece_on(s);
    if (pc == NO_PIECE) continue;
    return type_of(pc) == ROOK ? color_of(pc) : -1;
  }
  return -1;
}

// Rule of the square: can the enemy king no longer catch this passer?
bool outside_square(const Position& pos, Square sq, Color c) {
  const int forward = c == WHITE ? 8 : -8;
  const Square promotion = file_of(sq) + (c == WHITE ? 56 : 0);
  for (Square s = sq + forward;; s += forward) {
    if (pos.piece_on(s) != NO_PIECE) return false;
    if (s == promotion) break;
  }
  const int rr = rel_rank(c, sq);
  const int steps = rr == 1 ? 5 : 7 - rr;
  const bool their_move = pos.side_to_move() != c;
  return distance(pos.king_square(~c), promotion) - (their_move ? 1 : 0) > steps;
}

struct EvalCacheEntry {
  Key key = 0;
  int score = 0;
};
constexpr int CACHE_SIZE = 1 << 16;
thread_local EvalCacheEntry eval_cache[CACHE_SIZE];

int psq_score(const Position& pos, int& mg, int& eg) {
  mg = eg = 0;
  int phase = 0;
  Bitboard occ = pos.pieces();
  while (occ) {
    Square s = pop_lsb(occ);
    Piece pc = pos.piece_on(s);
    int sign = color_of(pc) == WHITE ? 1 : -1;
    mg += sign * T.psq_mg[pc][s];
    eg += sign * T.psq_eg[pc][s];
    phase += PHASE_WEIGHT[type_of(pc)];
  }
  return phase;
}

int evaluate_uncached(const Position& pos) {
  int psq_mg, psq_eg;
  int phase = psq_score(pos, psq_mg, psq_eg);
  // Doubles, because king danger adds fractions; the arithmetic follows the
  // JavaScript order so both engines give identical scores.
  double mg = psq_mg, eg = psq_eg;
  const Bitboard occ = pos.pieces();

  // Pawn structure per [color][file]: count and most/least advanced relative rank.
  int pawn_count[2][8] = {};
  int most_advanced[2][8], least_advanced[2][8];
  int non_pawn_material[2] = {};
  for (int c = 0; c < 2; c++) {
    for (int f = 0; f < 8; f++) {
      most_advanced[c][f] = -1;
      least_advanced[c][f] = 8;
    }
    Bitboard pawns = pos.pieces(Color(c), PAWN);
    while (pawns) {
      Square s = pop_lsb(pawns);
      int f = file_of(s), rr = rel_rank(Color(c), s);
      pawn_count[c][f]++;
      most_advanced[c][f] = std::max(most_advanced[c][f], rr);
      least_advanced[c][f] = std::min(least_advanced[c][f], rr);
    }
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN})
      non_pawn_material[c] += MOPUP_VALUE[pt] * popcount(pos.pieces(Color(c), pt));
  }

  // Bare kings and lone minor pieces can't win.
  if (!pos.pieces(PAWN) && phase <= 2 && pos.insufficient_material()) return 0;

  // Attack maps, mobility and king-danger features in one pass.
  Bitboard attacks[2][6] = {};
  Bitboard all_attacks[2];
  int danger[2][DANGER_TERMS] = {};
  int mobility_total[2][6] = {};
  int zone_attackers[2] = {};  // enemy pieces bearing on this color's king zone
  bool has_queen[2];
  for (int ci = 0; ci < 2; ci++) {
    const Color c = Color(ci), them = ~c;
    const Bitboard pawns = pos.pieces(c, PAWN);
    attacks[c][PAWN] = c == WHITE ? shift<9>(pawns) | shift<7>(pawns) : shift<-7>(pawns) | shift<-9>(pawns);
    attacks[c][KING] = KingAttacks[pos.king_square(c)];
    const Bitboard zone = T.king_zone[them][pos.king_square(them)];
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
      Bitboard b = pos.pieces(c, pt);
      while (b) {
        Bitboard att = attacks_bb(pt, pop_lsb(b), occ);
        attacks[c][pt] |= att;
        mobility_total[c][pt] += popcount(att & ~pos.pieces(c)) - MOBILITY_TYPICAL[pt];
        int hits = popcount(att & zone);
        if (hits) {
          danger[them][pt - KNIGHT]++;
          danger[them][ZONE_ATTACKS] += hits;
          zone_attackers[them]++;
        }
      }
    }
    has_queen[c] = pos.pieces(c, QUEEN) != 0;
    all_attacks[c] = 0;
    for (int t = 0; t < 6; t++) all_attacks[c] |= attacks[c][t];
  }
  for (int ci = 0; ci < 2; ci++) {
    const Color c = Color(ci), them = ~c;
    // Danger counts only with two attackers, or one backed by a queen.
    if (zone_attackers[c] <= 1 - int(has_queen[them])) {
      for (int i = 0; i < DANGER_TERMS; i++) danger[c][i] = 0;
      continue;
    }
    const Square king = pos.king_square(c);
    const Bitboard minor_guard = attacks[c][PAWN] | attacks[c][KNIGHT] | attacks[c][BISHOP] | attacks[c][ROOK];
    danger[c][WEAK_SQUARES] += popcount(T.king_zone[c][king] & all_attacks[them] & ~minor_guard);
    // Safe checks: squares checking our king that a piece of theirs can
    // reach, not occupied by their own pieces, and not guarded by us.
    const Bitboard safe = ~pos.pieces(them) & ~all_attacks[c];
    danger[c][KNIGHT_CHECK] += popcount(KnightAttacks[king] & safe & attacks[them][KNIGHT]);
    const Bitboard diag = bishop_attacks(king, occ) & safe;
    const Bitboard orth = rook_attacks(king, occ) & safe;
    danger[c][BISHOP_CHECK] += popcount(diag & attacks[them][BISHOP]);
    danger[c][ROOK_CHECK] += popcount(orth & attacks[them][ROOK]);
    danger[c][QUEEN_CHECK] += popcount((diag | orth) & attacks[them][QUEEN]);
    if (!has_queen[them]) danger[c][NO_QUEEN] = 1;
    danger[c][BASE] = 1;
  }

  bool unstoppable[2] = {};
  for (int ci = 0; ci < 2; ci++) {
    const Color c = Color(ci), them = ~c;
    const int sign = c == WHITE ? 1 : -1;

    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN}) {
      mg += sign * MOBILITY[pt][0] * mobility_total[c][pt];
      eg += sign * MOBILITY[pt][1] * mobility_total[c][pt];
    }

    if (popcount(pos.pieces(c, BISHOP)) >= 2) {
      mg += sign * BISHOP_PAIR[0];
      eg += sign * BISHOP_PAIR[1];
    }

    for (int f = 0; f < 8; f++) {
      int count = pawn_count[c][f];
      if (count > 1) {
        mg += sign * DOUBLED_PAWN[0] * (count - 1);
        eg += sign * DOUBLED_PAWN[1] * (count - 1);
      }
    }

    Bitboard pawns = pos.pieces(c, PAWN);
    while (pawns) {
      const Square sq = pop_lsb(pawns);
      const int file = file_of(sq), rr = rel_rank(c, sq);
      const int left = file > 0 ? pawn_count[c][file - 1] : 0;
      const int right = file < 7 ? pawn_count[c][file + 1] : 0;
      if (left == 0 && right == 0) {
        mg += sign * ISOLATED_PAWN[0];
        eg += sign * ISOLATED_PAWN[1];
      }
      // Passed: no enemy pawn ahead of it on its own or a neighboring file.
      bool passed = true;
      for (int f = std::max(0, file - 1); f <= std::min(7, file + 1) && passed; f++)
        if (pawn_count[them][f] && 7 - least_advanced[them][f] > rr) passed = false;
      if (!passed) continue;
      mg += sign * PASSED_PAWN_MG[rr];
      eg += sign * PASSED_PAWN_EG[rr];
      const Square front = sq + (c == WHITE ? 8 : -8);
      eg += sign * PASSED_OWN_KING[rr] * distance(pos.king_square(c), front);
      eg += sign * PASSED_ENEMY_KING[rr] * distance(pos.king_square(them), front);
      if (pos.piece_on(front) != NO_PIECE) eg += sign * PASSED_BLOCKED[rr];
      const int behind = rook_behind(pos, sq, c);
      if (behind == c) {
        mg += sign * ROOK_BEHIND_PASSER[0];
        eg += sign * ROOK_BEHIND_PASSER[1];
      } else if (behind == them) {
        mg -= sign * ROOK_BEHIND_ENEMY_PASSER[0];
        eg -= sign * ROOK_BEHIND_ENEMY_PASSER[1];
      }
      if (!unstoppable[c] && non_pawn_material[them] == 0 && outside_square(pos, sq, c)) unstoppable[c] = true;
    }
    if (unstoppable[c]) eg += sign * UNSTOPPABLE_PASSER;

    Bitboard rooks = pos.pieces(c, ROOK);
    while (rooks) {
      const Square sq = pop_lsb(rooks);
      const int file = file_of(sq);
      if (pawn_count[c][file] == 0) {
        const bool open = pawn_count[them][file] == 0;
        mg += sign * (open ? ROOK_OPEN_FILE[0] : ROOK_SEMI_OPEN_FILE[0]);
        eg += sign * (open ? ROOK_OPEN_FILE[1] : ROOK_SEMI_OPEN_FILE[1]);
      }
      // On the seventh, confining the enemy king or attacking unmoved pawns.
      if (rel_rank(c, sq) == 6) {
        bool seventh = rel_rank(c, pos.king_square(them)) == 7;
        for (int f = 0; f < 8 && !seventh; f++) seventh = least_advanced[them][f] == 1;
        if (seventh) {
          mg += sign * ROOK_ON_SEVENTH[0];
          eg += sign * ROOK_ON_SEVENTH[1];
        }
      }
    }

    // Pawn shelter for a king on its back two ranks.
    const Square king = pos.king_square(c);
    const int kfile = file_of(king);
    if (rel_rank(c, king) <= 1) {
      const Piece our_pawn = make_piece(c, PAWN);
      const int forward = c == WHITE ? 8 : -8;
      int bonus = 0;
      for (int f = std::max(0, kfile - 1); f <= std::min(7, kfile + 1); f++) {
        const Square front = rank_of(king) * 8 + f + forward;
        if (pos.piece_on(front) == our_pawn) bonus += SHELTER_NEAR;
        else if (pos.piece_on(front + forward) == our_pawn) bonus += SHELTER_FAR;
        else bonus += SHELTER_NONE;
      }
      mg += sign * bonus;
    }

    // Files around the king without our pawns, and enemy pawns storming it.
    for (int f = std::max(0, kfile - 1); f <= std::min(7, kfile + 1); f++) {
      if (pawn_count[c][f] == 0) mg += sign * KING_OPEN_FILE[pawn_count[them][f] == 0 ? 1 : 0];
      const int storm_rank = most_advanced[them][f];  // from their side, -1 if none
      if (storm_rank >= 0 && 7 - storm_rank > rel_rank(c, king)) {
        const Square s = (c == WHITE ? 7 - storm_rank : storm_rank) * 8 + f;
        const bool blocked = pos.piece_on(s + (c == WHITE ? -8 : 8)) == make_piece(c, PAWN);
        mg += sign * (blocked ? PAWN_STORM_BLOCKED[storm_rank] : PAWN_STORM[storm_rank]);
      }
    }

    // King danger: attack units, worth more the more of them there are.
    if (danger[c][BASE]) {
      int units = 0;
      for (int i = 0; i < DANGER_TERMS; i++) units += KING_DANGER[i] * danger[c][i];
      if (units > 0) {
        mg -= (sign * units * units) / DANGER_MG_DIVISOR;
        eg -= (sign * units) / DANGER_EG_DIVISOR;
      }
    }
  }

  // Mop-up: against a bare king, drive it to the edge and bring ours close.
  for (int ci = 0; ci < 2; ci++) {
    const Color c = Color(ci), them = ~c;
    if (!pos.pieces(them, PAWN) && non_pawn_material[them] == 0 && non_pawn_material[c] >= MOPUP_VALUE[ROOK]) {
      const Square loser = pos.king_square(them), winner = pos.king_square(c);
      const int lf = file_of(loser), lr = rank_of(loser);
      const int center_distance = std::max(3 - lf, lf - 4) + std::max(3 - lr, lr - 4);
      const int king_distance = std::abs(lf - file_of(winner)) + std::abs(lr - rank_of(winner));
      eg += (c == WHITE ? 1 : -1) * (10 * center_distance + 4 * (14 - king_distance));
    }
  }

  phase = std::min(phase, MAX_PHASE);
  const int score = int((mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE);
  return (pos.side_to_move() == WHITE ? score : -score) + TEMPO;
}

}  // namespace

int evaluate(const Position& pos) {
  EvalCacheEntry& e = eval_cache[pos.key() & (CACHE_SIZE - 1)];
  if (e.key == pos.key()) return e.score;
  const int score = std::clamp(evaluate_uncached(pos), -EVAL_LIMIT, EVAL_LIMIT);
  e.key = pos.key();
  e.score = score;
  return score;
}

int evaluate_material(const Position& pos) {
  int mg, eg;
  const int phase = std::min(psq_score(pos, mg, eg), MAX_PHASE);
  const int score = (mg * phase + eg * (MAX_PHASE - phase)) / MAX_PHASE;
  return (pos.side_to_move() == WHITE ? score : -score) + TEMPO;
}

// Material and tables alone, when further than this outside the window: the
// rest of the evaluation is worth under 2 pawns 99% of the time.
constexpr int LAZY_MARGIN = 300;

int evaluate_lazy(const Position& pos, int alpha, int beta) {
  const EvalCacheEntry& e = eval_cache[pos.key() & (CACHE_SIZE - 1)];
  if (e.key == pos.key()) return e.score;
  const int quick = evaluate_material(pos);
  if (quick - LAZY_MARGIN >= beta || quick + LAZY_MARGIN <= alpha) return quick;
  return evaluate(pos);
}

void clear_eval_cache() {
  for (auto& e : eval_cache) e = EvalCacheEntry();
}

}  // namespace chess
