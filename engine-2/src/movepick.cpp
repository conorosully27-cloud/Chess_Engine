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

void score_tactical(const Position& pos, const MoveList& list, int* scores) {
  for (int i = 0; i < list.count; i++) {
    const Move m = list.moves[i];
    const int mvv_lva = m.is_capture() ? ORDER_VALUE[captured_type(pos, m)] * 10 - ORDER_VALUE[type_of(pos.piece_on(m.from()))] : 0;
    if (!m.is_promotion()) scores[i] = CAPTURE_SCORE + mvv_lva;
    else if (m.is_capture())  // as score_moves
      scores[i] = (pos.see_ge(m, 0) ? CAPTURE_SCORE : BAD_CAPTURE_SCORE) + mvv_lva + ORDER_VALUE[m.promotion_type()];
    else scores[i] = PROMOTION_SCORE + ORDER_VALUE[m.promotion_type()];
  }
}

namespace {

// Whether a pawn of the side to move can promote by a push.
bool push_promotion(const Position& pos) {
  const Color us = pos.side_to_move();
  const Bitboard pawns = pos.pieces(us, PAWN), empty = ~pos.pieces();
  return us == WHITE ? (shift<8>(pawns & (RANK_8 >> 8)) & empty) : (shift<-8>(pawns & (RANK_1 << 8)) & empty);
}

}  // namespace

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
      score_tactical(pos_, captures_, capture_scores_);
      stage_ = GOOD_CAPTURES;
      [[fallthrough]];
    case GOOD_CAPTURES:
      while (next_capture_ < captures_.count) {
        const Move m = pick_next(captures_, capture_scores_, next_capture_);
        if (capture_scores_[next_capture_] < 0) break;  // the rest are losing promotions: after the quiet moves
        next_capture_++;
        if (m == tt_move_) continue;
        if (!m.is_promotion() && !pos_.see_ge(m, 0)) {
          bad_captures_.add(m);  // loses material: after the quiet moves
          continue;
        }
        return m;
      }
      stage_ = KILLERS;
      killers_first_ = !push_promotion(pos_);
      [[fallthrough]];
    case KILLERS:
      while (killers_first_ && next_killer_ < 2) {
        const Move k = next_killer_++ == 0 ? killer1_ : killer2_;
        if (k && k != tt_move_ && !k.is_capture() && !k.is_promotion() && (next_killer_ == 1 || k != killer1_) &&
            pseudo_legal(pos_, k))
          return k;
      }
      stage_ = GEN_QUIETS;
      [[fallthrough]];
    case GEN_QUIETS:
      generate<QUIETS>(pos_, quiets_);
      score_moves(pos_, quiets_, quiet_scores_, Move(), killer1_, killer2_, history_, cont1_, cont2_);
      stage_ = QUIET_MOVES;
      [[fallthrough]];
    case QUIET_MOVES:
      // The killers keep their scores, so the other moves come out in the
      // same order as when the killers came from this list.
      while (next_quiet_ < quiets_.count) {
        const Move m = pick_next(quiets_, quiet_scores_, next_quiet_++);
        if (m != tt_move_ && !(killers_first_ && (m == killer1_ || m == killer2_))) return m;
      }
      stage_ = BAD_CAPTURES;
      [[fallthrough]];
    case BAD_CAPTURES:
      if (next_bad_ < bad_captures_.count) return bad_captures_.moves[next_bad_++];
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
