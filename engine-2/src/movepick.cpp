#include "movepick.h"

#include <algorithm>
#include <utility>

namespace chess {

void score_moves(const Position& pos, const MoveList& list, int* scores, Move tt_move, Move killer1, Move killer2,
                 const HistoryTable& history, ContHistoryRow cont1, ContHistoryRow cont2) {
  for (int i = 0; i < list.count; i++) {
    const Move m = list.moves[i];
    const int promo = m.is_promotion() ? ORDER_VALUE[m.promotion_type()] : 0;
    int score;
    if (m == tt_move) score = TT_MOVE_SCORE;
    else if (m.is_capture())
      score = (pos.see_ge(m, 0) ? CAPTURE_SCORE : BAD_CAPTURE_SCORE) + ORDER_VALUE[captured_type(pos, m)] * 10 -
              ORDER_VALUE[type_of(pos.piece_on(m.from()))] + promo;
    else if (m.is_promotion()) score = PROMOTION_SCORE + promo;
    else if (m == killer1) score = KILLER1_SCORE;
    else if (m == killer2) score = KILLER2_SCORE;
    else {
      const int index = pos.piece_on(m.from()) * 64 + m.to();
      score = history[index / 64][index % 64];
      if (cont1) score += cont1[index];
      if (cont2) score += cont2[index];
    }
    scores[i] = score;
  }
}

// Two passes, the first of which the compiler vectorizes: it is cheaper
// than one branchy pass.
Move pick_next(MoveList& list, int* scores, int i) {
  int top = scores[i];
  for (int j = i + 1; j < list.count; j++) top = std::max(top, scores[j]);
  int best = i;
  while (scores[best] != top) best++;
  if (best != i) {
    std::swap(list.moves[i], list.moves[best]);
    std::swap(scores[i], scores[best]);
  }
  return list.moves[i];
}

Move MovePicker::next() {
  switch (stage_) {
    case HASH_MOVE:
      stage_ = GEN_CAPTURES;
      if (tt_move_ && pseudo_legal(pos_, tt_move_)) return tt_move_;
      [[fallthrough]];
    case GEN_CAPTURES:
      generate<CAPTURES>(pos_, captures_);
      score_moves(pos_, captures_, capture_scores_, Move(), killer1_, killer2_, history_);
      stage_ = GOOD_CAPTURES;
      [[fallthrough]];
    case GOOD_CAPTURES:
      while (next_capture_ < captures_.count) {
        const Move m = pick_next(captures_, capture_scores_, next_capture_);
        if (capture_scores_[next_capture_] < 0) break;  // the rest lose material: after the quiet moves
        next_capture_++;
        if (m != tt_move_) return m;
      }
      stage_ = GEN_QUIETS;
      [[fallthrough]];
    case GEN_QUIETS:
      generate<QUIETS>(pos_, quiets_);
      score_moves(pos_, quiets_, quiet_scores_, Move(), killer1_, killer2_, history_, cont1_, cont2_);
      stage_ = QUIET_MOVES;
      [[fallthrough]];
    case QUIET_MOVES:
      while (next_quiet_ < quiets_.count) {
        const Move m = pick_next(quiets_, quiet_scores_, next_quiet_++);
        if (m != tt_move_) return m;
      }
      stage_ = BAD_CAPTURES;
      [[fallthrough]];
    case BAD_CAPTURES:
      while (next_capture_ < captures_.count) {
        const Move m = pick_next(captures_, capture_scores_, next_capture_++);
        if (m != tt_move_) return m;
      }
      stage_ = DONE;
      [[fallthrough]];
    case DONE:
      break;
  }
  return Move();
}

}  // namespace chess
