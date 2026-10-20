// Move ordering: scores for the moves of a node, and MovePicker, which hands
// the main search its moves best first, generating and scoring them a stage
// at a time so that a cutoff by an early move saves the work on the rest.
#pragma once

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

inline PieceType captured_type(const Position& pos, Move m) {
  return m.is_ep() ? PAWN : type_of(pos.piece_on(m.to()));
}

// Orders moves: the remembered best move, then captures by most valuable
// victim / least valuable attacker, promotions, killer moves (quiet moves
// that refuted another line at this ply), then by history, and last the
// captures that lose material by SEE.
void score_moves(const Position& pos, const MoveList& list, int* scores, Move tt_move, Move killer1, Move killer2,
                 const HistoryTable& history);

// Moves the highest-scored remaining move to index i and returns it (the
// first one, if several share the highest score).
Move pick_next(MoveList& list, int* scores, int i);

// The moves of a main-search node in score_moves' order (ties aside), each
// pseudo-legal move once: the hash move before anything is generated, the
// captures that don't lose material, the quiet moves, then the captures
// that do. next() returns an empty move when there are no more.
class MovePicker {
 public:
  MovePicker(const Position& pos, Move tt_move, Move killer1, Move killer2, const HistoryTable& history)
      : pos_(pos), tt_move_(tt_move), killer1_(killer1), killer2_(killer2), history_(history) {}

  Move next();

 private:
  enum Stage { HASH_MOVE, GEN_CAPTURES, GOOD_CAPTURES, GEN_QUIETS, QUIET_MOVES, BAD_CAPTURES, DONE };

  const Position& pos_;
  const Move tt_move_, killer1_, killer2_;
  const HistoryTable& history_;
  Stage stage_ = HASH_MOVE;
  MoveList captures_, quiets_;
  int capture_scores_[MoveList::MAX_MOVES];
  int quiet_scores_[MoveList::MAX_MOVES];
  int next_capture_ = 0, next_quiet_ = 0;
};

}  // namespace chess
