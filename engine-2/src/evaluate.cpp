#include "evaluate.h"

#include <algorithm>
#include <cstdlib>

namespace chess {

namespace {

// ---------------------------------------------------------------- weights
// Copied from engine-1/src/engine/weights.js; material and piece-square
// tables are in psqt.h.

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
constexpr std::int64_t DANGER_MG_DIVISOR = 720;
constexpr std::int64_t DANGER_EG_DIVISOR = 20;

// Fixed piece values for mop-up; not tuned.
constexpr int MOPUP_VALUE[6] = {90, 320, 330, 480, 950, 0};
using psqt::MAX_PHASE;

// ------------------------------------------------------------- tables

struct Tables {
  // King zone: the king's square, its neighbors and the three squares two
  // ranks in front, for a king of each color on each square.
  Bitboard king_zone[COLOR_NB][64];

  Tables() {
    for (int c = 0; c < 2; c++) {
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

// Pawn structure, which depends only on where the pawns are, is cached by
// the position's pawn hash: pawns move far less often than pieces.
struct PawnEntry {
  Key key = 0;
  Bitboard passed[2] = {};
  int mg = 0, eg = 0;  // doubled, isolated and passed-pawn rank terms, White minus Black
  std::int8_t count[2][8] = {};
  std::int8_t most_advanced[2][8] = {};   // relative rank of the most advanced pawn, -1 if none
  std::int8_t least_advanced[2][8] = {};  // ... of the least advanced, 8 if none
};
constexpr int PAWN_TABLE_SIZE = 1 << 14;
thread_local PawnEntry pawn_table[PAWN_TABLE_SIZE];

const PawnEntry& probe_pawns(const Position& pos) {
  PawnEntry& e = pawn_table[pos.pawn_key() & (PAWN_TABLE_SIZE - 1)];
  if (e.key == pos.pawn_key()) return e;
  e = PawnEntry();
  e.key = pos.pawn_key();
  for (int c = 0; c < 2; c++) {
    for (int f = 0; f < 8; f++) {
      e.most_advanced[c][f] = -1;
      e.least_advanced[c][f] = 8;
    }
    Bitboard pawns = pos.pieces(Color(c), PAWN);
    while (pawns) {
      const Square s = pop_lsb(pawns);
      const int f = file_of(s), rr = rel_rank(Color(c), s);
      e.count[c][f]++;
      e.most_advanced[c][f] = std::int8_t(std::max<int>(e.most_advanced[c][f], rr));
      e.least_advanced[c][f] = std::int8_t(std::min<int>(e.least_advanced[c][f], rr));
    }
  }
  for (int ci = 0; ci < 2; ci++) {
    const Color c = Color(ci), them = ~c;
    const int sign = c == WHITE ? 1 : -1;
    for (int f = 0; f < 8; f++) {
      if (e.count[c][f] > 1) {
        e.mg += sign * DOUBLED_PAWN[0] * (e.count[c][f] - 1);
        e.eg += sign * DOUBLED_PAWN[1] * (e.count[c][f] - 1);
      }
    }
    Bitboard pawns = pos.pieces(c, PAWN);
    while (pawns) {
      const Square sq = pop_lsb(pawns);
      const int file = file_of(sq), rr = rel_rank(c, sq);
      const int left = file > 0 ? e.count[c][file - 1] : 0;
      const int right = file < 7 ? e.count[c][file + 1] : 0;
      if (left == 0 && right == 0) {
        e.mg += sign * ISOLATED_PAWN[0];
        e.eg += sign * ISOLATED_PAWN[1];
      }
      // Passed: no enemy pawn ahead of it on its own or a neighboring file.
      bool passed = true;
      for (int f = std::max(0, file - 1); f <= std::min(7, file + 1) && passed; f++)
        if (e.count[them][f] && 7 - e.least_advanced[them][f] > rr) passed = false;
      if (passed) {
        e.passed[c] |= square_bb(sq);
        e.mg += sign * PASSED_PAWN_MG[rr];
        e.eg += sign * PASSED_PAWN_EG[rr];
      }
    }
  }
  return e;
}

int evaluate_uncached(const Position& pos) {
  // Material and tables, kept up to date by the position as moves are made.
  int phase = pos.phase();
  // Integer terms; king danger's fractional costs (units^2 / 720 middlegame,
  // units / 20 endgame) are summed separately so the final score is computed
  // exactly, with one division.
  int mg = pos.psq_mg(), eg = pos.psq_eg();
  std::int64_t danger_mg = 0, danger_eg = 0;  // sum of sign * units^2 and sign * units
  const Bitboard occ = pos.pieces();

  int non_pawn_material[2] = {};
  for (int c = 0; c < 2; c++)
    for (PieceType pt : {KNIGHT, BISHOP, ROOK, QUEEN})
      non_pawn_material[c] += MOPUP_VALUE[pt] * popcount(pos.pieces(Color(c), pt));

  // Bare kings and lone minor pieces can't win.
  if (!pos.pieces(PAWN) && phase <= 2 && pos.insufficient_material()) return 0;

  const PawnEntry& pawn_info = probe_pawns(pos);
  mg += pawn_info.mg;
  eg += pawn_info.eg;

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

    // Passed pawns: kings matter around them, ours escorting, theirs
    // stopping; rooks belong behind them (Tarrasch).
    Bitboard passers = pawn_info.passed[c];
    while (passers) {
      const Square sq = pop_lsb(passers);
      const int rr = rel_rank(c, sq);
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
      if (pawn_info.count[c][file] == 0) {
        const bool open = pawn_info.count[them][file] == 0;
        mg += sign * (open ? ROOK_OPEN_FILE[0] : ROOK_SEMI_OPEN_FILE[0]);
        eg += sign * (open ? ROOK_OPEN_FILE[1] : ROOK_SEMI_OPEN_FILE[1]);
      }
      // On the seventh, confining the enemy king or attacking unmoved pawns.
      if (rel_rank(c, sq) == 6) {
        bool seventh = rel_rank(c, pos.king_square(them)) == 7;
        for (int f = 0; f < 8 && !seventh; f++) seventh = pawn_info.least_advanced[them][f] == 1;
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
      if (pawn_info.count[c][f] == 0) mg += sign * KING_OPEN_FILE[pawn_info.count[them][f] == 0 ? 1 : 0];
      const int storm_rank = pawn_info.most_advanced[them][f];  // from their side, -1 if none
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
        danger_mg += std::int64_t(sign) * units * units;
        danger_eg += sign * units;
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

  // Tapered: (mg * phase + eg * (24 - phase)) / 24 with mg and eg less their
  // danger costs, over the common denominator 720 * 20 * 24, truncated
  // toward zero as Engine 1.6 does.
  phase = std::min(phase, MAX_PHASE);
  const std::int64_t num = (std::int64_t(mg) * DANGER_MG_DIVISOR - danger_mg) * DANGER_EG_DIVISOR * phase +
                           (std::int64_t(eg) * DANGER_EG_DIVISOR - danger_eg) * DANGER_MG_DIVISOR * (MAX_PHASE - phase);
  const int score = int(num / (DANGER_MG_DIVISOR * DANGER_EG_DIVISOR * MAX_PHASE));
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
  const int mg = pos.psq_mg(), eg = pos.psq_eg();
  const int phase = std::min(pos.phase(), MAX_PHASE);
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
  for (auto& e : pawn_table) e = PawnEntry();
}

}  // namespace chess
