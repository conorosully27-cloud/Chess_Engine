// Move ordering: scores for the moves of a node, and MovePicker, which hands
// the main search its moves best first, generating and scoring them a stage
// at a time so that a cutoff by an early move saves the work on the rest.
#pragma once

#include <cstdint>

#include "movegen.h"

namespace chess {

// Piece values for move ordering and quiescence pruning only.
constexpr int ORDER_VALUE[6] = {100, 320, 330, 500, 900, 2000};

constexpr int TT_MOVE_SCORE = 3000000;
constexpr int CAPTURE_SCORE = 2000000;
constexpr int PROMOTION_SCORE = 1900000;
constexpr int KILLER1_SCORE = 1800000;
constexpr int KILLER2_SCORE = 1700000;
constexpr int HISTORY_LIMIT = 500000;
// Captures that lose material by SEE: after every quiet move (history stays
// within +/-HISTORY_LIMIT).
constexpr int BAD_CAPTURE_SCORE = -2000000;

using HistoryTable = int[PIECE_NB][64];

// Continuation history: how well a quiet move (piece * 64 + to) did right
// after a given earlier move, one row per earlier move. Kept to 16 bits to
// halve the table (1.2 MB) and its cache misses.
constexpr int CONT_INDEX = PIECE_NB * 64;
constexpr int CONT_HISTORY_LIMIT = 30000;
using ContHistoryRow = const std::int16_t*;

// Capture history: how well a capture did, by moving piece, destination
// and captured piece type. Orders captures together with the victim's
// value: a capture scores CAPTURE_SCORE + 10 x the victim's value, less the
// attacker's, plus its history / CAPT_HIST_DIV.
using CaptureHistory = std::int16_t[PIECE_NB][64][6];
constexpr int CAPT_HIST_DIV = 8;

inline PieceType captured_type(const Position& pos, Move m) {
  return m.is_ep() ? PAWN : type_of(pos.piece_on(m.to()));
}

// Orders moves: the remembered best move, then captures by most valuable
// victim / least valuable attacker, promotions, killer moves (quiet moves
// that refuted another line at this ply), then by history, and last the
// captures that lose material by SEE. Quiet moves also get the
// continuation history after the moves one and two plies earlier, from
// cont1 and cont2 where given.
void score_moves(const Position& pos, const MoveList& list, int* scores, Move tt_move, Move killer1, Move killer2,
                 const HistoryTable& history, ContHistoryRow cont1 = nullptr, ContHistoryRow cont2 = nullptr);

// Scores captures and promotions as score_moves does, but without SEE for
// captures that aren't promotions, which is left until one is reached:
// those all get CAPTURE_SCORE, plus most valuable victim / least valuable
// attacker. Most of them are never reached (a cutoff comes first), and SEE
// was most of score_moves' cost.
void score_tactical(const Position& pos, const MoveList& list, int* scores, const CaptureHistory* capt = nullptr);

// Moves the highest-scored remaining move to index i and returns it (the
// first one, if several share the highest score).
Move pick_next(MoveList& list, int* scores, int i);

// The moves of a main-search node in score_moves' order (ties aside), each
// pseudo-legal move once: the hash move before anything is generated, the
// captures that don't lose material (SEE tested as each is reached), the
// killer moves (before the quiet moves are generated, so a killer's cutoff
// saves generating and scoring them; unless a pawn can promote by a push,
// as promotions come before killers), the other quiet moves, then the
// captures that lose material. next() returns an empty move when there
// are no more.
class MovePicker {
 public:
  MovePicker(const Position& pos, Move tt_move, Move killer1, Move killer2, const HistoryTable& history,
             ContHistoryRow cont1 = nullptr, ContHistoryRow cont2 = nullptr, const CaptureHistory* capt = nullptr)
      : pos_(pos), tt_move_(tt_move), killer1_(killer1), killer2_(killer2), history_(history), cont1_(cont1),
        cont2_(cont2), capt_(capt) {}

  Move next();
  // The search will prune every remaining quiet move that doesn't give
  // check: hand them out as generated, without scoring or sorting them.
  void skip_quiets() { skip_quiets_ = true; }

 private:
  enum Stage { HASH_MOVE, GEN_CAPTURES, GOOD_CAPTURES, KILLERS, GEN_QUIETS, QUIET_MOVES, BAD_CAPTURES, DONE };

  const Position& pos_;
  const Move tt_move_, killer1_, killer2_;
  const HistoryTable& history_;
  const ContHistoryRow cont1_, cont2_;
  const CaptureHistory* const capt_;
  Stage stage_ = HASH_MOVE;
  MoveList captures_, quiets_, bad_captures_;  // bad_captures_: those SEE failed, in the order reached
  int capture_scores_[MoveList::MAX_MOVES];
  int quiet_scores_[MoveList::MAX_MOVES];
  int next_capture_ = 0, next_quiet_ = 0, next_bad_ = 0;
  int next_killer_ = 0;
  bool killers_first_ = false;  // the killers were tried before the quiet moves
  bool skip_quiets_ = false;
};

}  // namespace chess
