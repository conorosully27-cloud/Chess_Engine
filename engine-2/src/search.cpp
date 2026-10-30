#include "search.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "evaluate.h"
#include "movepick.h"
#include "tune.h"
#include "uci.h"

namespace chess {

namespace {

// Pruning and reduction parameters (src/tune.h: constants, or UCI options
// in a tuning build). Centipawns unless noted. Values from an SPSA run on
// 3.0 (Oct 2026, 8,000 games at 8+0.08).

// Reverse futility: at depth <= RFP_DEPTH, return when the static
// evaluation beats beta by RFP_MARGIN per ply.
TUNE_PARAM(RFP_DEPTH, 4, 1, 12);
TUNE_PARAM(RFP_MARGIN, 94, 20, 250);

// Futility: at depth <= FUT_DEPTH, skip quiet moves when the static
// evaluation plus FUT_BASE + FUT_MARGIN per ply can't reach alpha.
TUNE_PARAM(FUT_DEPTH, 3, 1, 8);
TUNE_PARAM(FUT_BASE, -3, -100, 300);
TUNE_PARAM(FUT_MARGIN, 147, 40, 300);

// Late move pruning: at depth <= LMP_DEPTH, after LMP_BASE + depth^2 *
// LMP_SCALE / 100 quiet moves have been tried, the remaining ones are
// skipped (4, 7, 12 at depths 1-3).
TUNE_PARAM(LMP_DEPTH, 3, 1, 10);
TUNE_PARAM(LMP_BASE, 3, 0, 12);
TUNE_PARAM(LMP_SCALE, 102, 25, 300);

// SEE pruning: at depth <= SEE_PRUNE_DEPTH, moves that lose more than this
// much material on their square (static exchange) are skipped. Quiet moves
// get a margin growing with depth squared, captures one growing with depth.
TUNE_PARAM(SEE_PRUNE_DEPTH, 6, 1, 12);
TUNE_PARAM(SEE_QUIET_MARGIN, 25, 5, 120);
TUNE_PARAM(SEE_CAPTURE_MARGIN, 97, 20, 300);

// Late move reductions by depth and move number, in hundredths of a ply:
// LMR_BASE + ln(depth) ln(move) * 100 / LMR_DIV (before tuning: 1 + ... / 2).
TUNE_PARAM(LMR_BASE, 98, -50, 250);
TUNE_PARAM(LMR_DIV, 198, 100, 500);
// A quiet move's history (its own plus both continuation histories) takes
// a ply off the reduction per LMR_HISTORY of score, or adds one per
// LMR_HISTORY below zero, at most two either way.
TUNE_PARAM(LMR_HISTORY, 200, 50, 1000);

// Singular extensions: from SINGULAR_DEPTH, a hash move that the table
// rates well above every alternative is searched a ply deeper. The test
// searches the other moves to half depth against the table's score less
// SINGULAR_MARGIN / 10 per ply (before tuning: 2 x depth).
TUNE_PARAM(SINGULAR_DEPTH, 8, 4, 14);
TUNE_PARAM(SINGULAR_MARGIN, 19, 5, 80);

// Correction history entries are in 1/CORR_GRAIN centipawns, each within
// +/-CORR_LIMIT_CP; each update moves every table's entry by depth + 1
// parts in CORR_WEIGHT_SCALE (at most CORR_MAX_WEIGHT) of what the tables
// together still get wrong. With one table, a running average of the error.
// The non-pawn tables learn at CORR_NON_PAWN_RATE percent of that speed: at
// full speed all three chased the same error and the sum overshot. The
// last-two-moves table learns at CORR_CONT_RATE percent.
// Values from an SPSA run (Oct 2026, 2,000 pairs at 8+0.08, from 64, 16, 25).
constexpr int CORR_GRAIN = 256;
constexpr int CORR_WEIGHT_SCALE = 256;
TUNE_PARAM(CORR_LIMIT_CP, 50, 16, 127);
TUNE_PARAM(CORR_MAX_WEIGHT, 15, 4, 64);
TUNE_PARAM(CORR_NON_PAWN_RATE, 27, 5, 100);
TUNE_PARAM(CORR_CONT_RATE, 27, 5, 100);

using LmrTable = std::array<std::array<std::int8_t, 64>, 64>;
LmrTable make_lmr() {
  LmrTable t{};
  for (int d = 1; d < 64; d++)
    for (int m = 1; m < 64; m++)
      t[d][m] = std::int8_t(LMR_BASE / 100.0 + std::log(d) * std::log(m) / (LMR_DIV / 100.0));
  return t;
}
LmrTable LMR = make_lmr();

// Mate scores are stored relative to the position, not the root, so they
// stay correct when the position is reached at a different depth.
int to_tt(int score, int ply) {
  if (score >= MATE - MAX_PLY) return score + ply;
  if (score <= -(MATE - MAX_PLY)) return score - ply;
  return score;
}

int from_tt(int score, int ply) {
  if (score >= MATE - MAX_PLY) return score - ply;
  if (score <= -(MATE - MAX_PLY)) return score + ply;
  return score;
}

// Does the side to move have anything besides pawns and its king?
bool has_pieces(const Position& pos) {
  const Color us = pos.side_to_move();
  return pos.pieces(us) & ~pos.pieces(us, PAWN) & ~pos.pieces(us, KING);
}

std::string score_to_uci(int score) {
  if (!is_mate_score(score)) return "cp " + std::to_string(score);
  const int plies = MATE - std::abs(score);
  const int moves = (plies + 1) / 2;
  return "mate " + std::to_string(score > 0 ? moves : -moves);
}

}  // namespace

void search_params_changed() { LMR = make_lmr(); }

TimeBudget allocate_time(const SearchLimits& limits, Color us, int move_overhead_ms) {
  TimeBudget b;
  if (limits.infinite) return b;
  if (limits.movetime >= 0) {
    // Fixed time per move: the next depth usually takes longer than all the
    // previous ones together, so don't start one past half the time.
    b.hard_ms = std::max<std::int64_t>(1, limits.movetime - move_overhead_ms);
    b.soft_ms = std::max<std::int64_t>(1, b.hard_ms / 2);
    return b;
  }
  const int time = us == WHITE ? limits.wtime : limits.btime;
  if (time < 0) return b;  // no clock given: depth or node limited, or unlimited
  const int inc = us == WHITE ? limits.winc : limits.binc;
  const std::int64_t remaining = std::max<std::int64_t>(1, time - move_overhead_ms);
  const int moves_to_go = limits.movestogo > 0 ? std::min(limits.movestogo, 40) : 40;
  // Plan a share of the clock plus most of the increment, but never more
  // than a fifth of what is left (half before the last move of a time
  // control), and abort at three times the plan or half the clock. Searches
  // often run past the plan, and with a short clock the increment alone
  // would otherwise be overspent move after move until the clock ran out;
  // these caps make it settle instead.
  const std::int64_t soft_cap = moves_to_go == 1 ? remaining / 2 : remaining / 5;
  const std::int64_t hard_cap = moves_to_go == 1 ? remaining * 3 / 4 : remaining / 2;
  b.soft_ms = std::max<std::int64_t>(1, std::min(soft_cap, remaining / moves_to_go + inc * 3 / 4));
  b.hard_ms = std::max<std::int64_t>(b.soft_ms, std::min(hard_cap, b.soft_ms * 3));
  return b;
}

// ------------------------------------------------------------- the search

void SearchWorker::new_game() {
  std::fill(&history_[0][0], &history_[0][0] + PIECE_NB * 64, 0);
  std::fill(cont_history_.begin(), cont_history_.end(), std::int16_t(0));
  std::fill(&corr_history_[0][0], &corr_history_[0][0] + COLOR_NB * CORR_SIZE, std::int16_t(0));
  std::fill(&corr_non_pawn_[0][0][0], &corr_non_pawn_[0][0][0] + COLOR_NB * COLOR_NB * CORR_SIZE, std::int16_t(0));
  std::fill(corr_cont_.begin(), corr_cont_.end(), std::int16_t(0));
  clear_eval_cache();
}

std::int64_t SearchWorker::elapsed_ms() const {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
}

void SearchWorker::check_limits() {
  if (!can_stop_) return;
  if ((stop_flag_ && stop_flag_->load(std::memory_order_relaxed)) ||
      (hard_ms_ >= 0 && elapsed_ms() >= hard_ms_) || (max_nodes_ && nodes_ >= max_nodes_))
    stopped_ = true;
}

SearchResult SearchWorker::search(Position& pos, const SearchLimits& limits, const TimeBudget& budget,
                                  const std::atomic<bool>* stop,
                                  const std::function<void(const std::string&)>& on_info) {
  pos_ = &pos;
  nodes_ = 0;
  stopped_ = false;
  can_stop_ = false;
  stop_flag_ = stop;
  hard_ms_ = budget.hard_ms;
  max_nodes_ = limits.nodes;
  start_ = budget.start;
  for (auto& k : killers_) k[0] = k[1] = Move();
  for (auto& row : history_)
    for (int& h : row) h >>= 2;  // as Engine 1.6: an arithmetic shift, rounding down
  for (std::int16_t& h : cont_history_) h = std::int16_t(h >> 2);

  SearchResult result;
  MoveList legal;
  generate_legal(pos, legal);
  if (legal.size() == 0) {
    result.score = pos.in_check() ? -MATE : 0;
    return result;
  }
  result.best = legal[0];
  result.pv = {legal[0]};

  const int max_depth = limits.depth > 0 ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
  int previous = 0;
  for (int depth = 1; depth <= max_depth; depth++) {
    root_depth_ = depth;
    int score;
    if (depth >= 5 && !is_mate_score(previous)) {
      // Aspiration window: expect a score near the last one, widen if wrong.
      int delta = 35;
      int alpha = previous - delta, beta = previous + delta;
      for (;;) {
        score = negamax(depth, alpha, beta, 0, false);
        if (stopped_) break;
        if (score <= alpha) alpha = std::max(-INF, alpha - delta);
        else if (score >= beta) beta = std::min(INF, beta + delta);
        else break;
        delta *= 2;
      }
    } else {
      score = negamax(depth, -INF, INF, 0, false);
    }
    if (stopped_) break;  // an unfinished depth can't be trusted
    can_stop_ = true;

    previous = score;
    if (pv_length_[0] > 0) {
      result.pv.assign(pv_table_[0], pv_table_[0] + pv_length_[0]);
      result.best = result.pv[0];
    }
    result.score = score;
    result.depth = depth;

    const std::int64_t elapsed = elapsed_ms();
    if (on_info) {
      std::string line = "info depth " + std::to_string(depth) + " score " + score_to_uci(score) +
                         " nodes " + std::to_string(nodes_) + " nps " +
                         std::to_string(nodes_ * 1000 / std::max<std::int64_t>(elapsed, 1)) + " time " +
                         std::to_string(elapsed) + " pv";
      for (Move m : result.pv) line += " " + m.uci();
      on_info(line);
    }

    if (legal.size() == 1 && depth >= 4) break;  // only one move: no need to think
    if (is_mate_score(score) && MATE - std::abs(score) <= depth) break;  // forced mate found
    if (budget.soft_ms >= 0 && elapsed >= budget.soft_ms) break;
    if (stop && stop->load(std::memory_order_relaxed)) break;
  }
  result.nodes = nodes_;
  return result;
}

int SearchWorker::negamax(int depth, int alpha, int beta, int ply, bool after_null, Move excluded) {
  Position& pos = *pos_;
  pv_length_[ply] = ply;
  if ((++nodes_ & 2047) == 0) check_limits();
  if (stopped_) return 0;
  const bool pv_node = beta - alpha > 1;

  if (ply > 0) {
    // A repeated position or the fifty-move rule is a draw.
    if (pos.is_fifty_move() || pos.is_repetition()) return 0;
    if (ply >= MAX_PLY - 1) return evaluate(pos);
    // Mate distance pruning: no line from here beats a mate already found nearer the root.
    alpha = std::max(alpha, -MATE + ply);
    beta = std::min(beta, MATE - ply - 1);
    if (alpha >= beta) return alpha;
  }

  const bool in_check = pos.in_check();
  if (in_check) depth++;  // look further when in check
  if (depth <= 0) return quiesce(alpha, beta, ply);

  // Transposition table: reuse an earlier result for this position if it
  // was searched deeply enough, or at least try its best move first.
  TTEntry& tte = tt_.entry(pos.key());
  const std::uint32_t check = TranspositionTable::check(pos.key());
  // A test search with a move left out doesn't take the table's result,
  // which counts every move.
  const bool excluding = bool(excluded);
  Move tt_move;
  int tt_eval = EVAL_NONE, tt_score = 0, tt_depth = 0;
  Bound tt_bound = BOUND_NONE;
  if (tte.bound != BOUND_NONE && tte.key == check) {
    tt_move = Move::from_raw(tte.move);
    tt_eval = tte.eval;
    tt_score = from_tt(tte.score, ply);
    tt_depth = tte.depth;
    tt_bound = tte.bound;
    if (!pv_node && !excluding && tte.depth >= depth) {
      const int score = tt_score;
      if (tte.bound == BOUND_EXACT || (tte.bound == BOUND_LOWER && score >= beta) ||
          (tte.bound == BOUND_UPPER && score <= alpha))
        return score;
    }
  }
  // Internal iterative reduction: with no remembered best move the ordering
  // here is poor, so a full-depth search is mostly wasted; go a ply shallower.
  if (ply > 0 && depth >= 4 && !tt_move) depth--;

  int raw_eval = 0, static_eval = 0;
  if (!in_check) {
    // The table keeps the static evaluation: reuse it rather than evaluate again.
    raw_eval = tt_eval != EVAL_NONE ? tt_eval : evaluate(pos);
    // Corrected by what searches of this pawn structure have found.
    static_eval = raw_eval + corr_entries(pos, ply).total() / CORR_GRAIN;
    if (!pv_node && !excluding && !is_mate_score(beta)) {
      // Reverse futility: so far ahead that a shallow search won't change it.
      if (depth <= RFP_DEPTH && static_eval - RFP_MARGIN * depth >= beta) return static_eval;
      // Null move: if passing still leaves us at or above beta, a real move
      // would too. Not without pieces, where passing could be best (zugzwang).
      if (!after_null && depth >= 3 && static_eval >= beta && has_pieces(pos)) {
        const int reduction = depth >= 7 ? 3 : 2;
        ply_move_[ply] = -1;
        pos.make_null_move();
        const int score = -negamax(depth - 1 - reduction, -beta, -beta + 1, ply + 1, true);
        pos.unmake_null_move();
        if (stopped_) return 0;
        if (score >= beta) return is_mate_score(score) ? beta : score;
      }
    }
  }

  // Futility: near the leaves, quiet moves can't lift a hopeless position.
  const bool futile = !pv_node && !in_check && depth <= FUT_DEPTH && static_eval + FUT_BASE + FUT_MARGIN * depth <= alpha;
  const bool prune_late = !pv_node && !in_check && depth <= LMP_DEPTH;
  const int lmp_quiets = LMP_BASE + depth * depth * LMP_SCALE / 100;
  const bool see_prune = !pv_node && !in_check && depth <= SEE_PRUNE_DEPTH;
  int quiets_tried = 0;
  int quiets_searched[MoveList::MAX_MOVES];  // piece * 64 + to of quiet moves tried
  int quiets_count = 0;

  const Move killer1 = killers_[ply][0], killer2 = killers_[ply][1];
  std::int16_t* const cont1 = cont_row(ply, 1);
  std::int16_t* const cont2 = cont_row(ply, 2);
  MovePicker picker(pos, excluding ? Move() : tt_move, killer1, killer2, history_, cont1, cont2);
  const int alpha_at_start = alpha;
  int best_score = -INF;
  Move best_move;
  int legal = 0;

  while (const Move move = picker.next()) {
    if (move == excluded || !pos.legal(move)) continue;
    const bool quiet = !move.is_capture() && !move.is_promotion();
    const Piece piece = pos.piece_on(move.from());
    legal++;

    // Pruned moves are decided before making them, which is cheaper. None
    // that give check, which is worked out only when a rule gets that far.
    if (legal > 1) {
      int checks = -1;  // whether the move gives check; -1: not known yet
      const auto gives_check = [&] {
        if (checks < 0) checks = pos.gives_check(move);
        return checks == 1;
      };
      if (futile && quiet && !gives_check()) continue;
      // Late move pruning: moves ordered this late almost never matter here.
      if (prune_late && quiet && !gives_check() && ++quiets_tried > lmp_quiets) continue;
      // SEE pruning: the move loses too much material to be worth a look this close to the leaves.
      if (see_prune && !pos.see_ge(move, quiet ? -SEE_QUIET_MARGIN * depth * depth : -SEE_CAPTURE_MARGIN * depth) &&
          !gives_check())
        continue;
    }

    // Singular extension: is the hash move the only good one here? Search
    // the others, shallower, against the table's score less a margin. If
    // none reaches it, the hash move gets a ply more. If they do and the
    // margin is still at or above beta, several moves beat beta: cut off.
    int extension = 0;
    if (move == tt_move && !excluding && ply > 0 && ply < 2 * root_depth_ && depth >= SINGULAR_DEPTH &&
        tt_depth >= depth - 3 && (tt_bound == BOUND_LOWER || tt_bound == BOUND_EXACT) && !is_mate_score(tt_score)) {
      const int singular_beta = tt_score - SINGULAR_MARGIN * depth / 10;
      const int score = negamax((depth - 1) / 2, singular_beta - 1, singular_beta, ply, false, move);
      pv_length_[ply] = ply;  // the test search wrote this node's PV
      if (stopped_) return 0;
      if (score < singular_beta) extension = 1;
      else if (singular_beta >= beta) return singular_beta;
    }
    const int new_depth = depth - 1 + extension;

    ply_move_[ply] = piece * 64 + move.to();
    pos.make_move(move);
    if (new_depth > 0) tt_.prefetch(pos.key());  // the child probes the table (quiescence doesn't)

    int score;
    if (legal == 1) {
      score = -negamax(new_depth, -beta, -alpha, ply + 1, false);
    } else {
      // Late move reductions: moves ordered late are rarely best, so search
      // them shallower first and only re-search if they surprise us.
      // Not at the root: reducing there can miss a quicker mate.
      int reduction = 0;
      if (depth >= 3 && legal > 2 && quiet && !in_check && !pos.in_check() && ply > 0) {  // pos: after the move
        reduction = LMR[std::min(depth, 63)][std::min(legal, 63)];
        if (pv_node) reduction--;  // less on the main line
        if (move == killer1 || move == killer2) reduction--;  // less for moves that refuted others
        const int index = piece * 64 + move.to();
        const int history = history_[piece][move.to()] + (cont1 ? cont1[index] : 0) + (cont2 ? cont2[index] : 0);
        reduction -= std::clamp(history / LMR_HISTORY, -2, 2);  // less for moves that did well elsewhere
        reduction = std::clamp(reduction, 0, depth - 2);
      }
      // Principal variation search: prove the move is no better with a null
      // window, and only search it fully if it is.
      score = -negamax(new_depth - reduction, -alpha - 1, -alpha, ply + 1, false);
      if (reduction > 0 && score > alpha) score = -negamax(new_depth, -alpha - 1, -alpha, ply + 1, false);
      if (score > alpha && score < beta) score = -negamax(new_depth, -beta, -alpha, ply + 1, false);
    }
    pos.unmake_move();
    if (stopped_) return 0;

    if (score > best_score) {
      best_score = score;
      best_move = move;
      if (score > alpha) {
        alpha = score;
        update_pv(ply, move);
        if (score >= beta) {
          if (quiet) {
            if (move != killer1) {
              killers_[ply][1] = killer1;
              killers_[ply][0] = move;
            }
            const int bonus = depth * depth;
            const auto cont_add = [](std::int16_t* row, int index, int amount) {
              if (row) row[index] = std::int16_t(std::clamp(row[index] + amount, -CONT_HISTORY_LIMIT, CONT_HISTORY_LIMIT));
            };
            int& h = history_[piece][move.to()];
            h = std::min(h + bonus, HISTORY_LIMIT);
            cont_add(cont1, piece * 64 + move.to(), bonus);
            cont_add(cont2, piece * 64 + move.to(), bonus);
            // The quiet moves tried before it failed here: count against them.
            for (int q = 0; q < quiets_count; q++) {
              int& hq = history_[quiets_searched[q] / 64][quiets_searched[q] % 64];
              hq = std::max(hq - bonus, -HISTORY_LIMIT);
              cont_add(cont1, quiets_searched[q], -bonus);
              cont_add(cont2, quiets_searched[q], -bonus);
            }
          }
          break;
        }
      }
    }
    if (quiet) quiets_searched[quiets_count++] = piece * 64 + move.to();
  }

  // Checkmate or stalemate; with a move left out, the only move was that one.
  if (legal == 0) return excluding ? alpha : in_check ? -MATE + ply : 0;

  const Bound bound = best_score >= beta ? BOUND_LOWER : best_score > alpha_at_start ? BOUND_EXACT : BOUND_UPPER;
  // Correction history learns from quiet results that say something about
  // the evaluation: not a lower bound below it or an upper bound above it.
  if (!in_check && !excluding && !best_move.is_capture() && !best_move.is_promotion() && !is_mate_score(best_score) &&
      !(bound == BOUND_LOWER && best_score <= static_eval) && !(bound == BOUND_UPPER && best_score >= static_eval)) {
    const CorrEntries corr = corr_entries(pos, ply);
    const int weight = std::min(depth + 1, CORR_MAX_WEIGHT);
    const int error = (best_score - raw_eval) * CORR_GRAIN - corr.total();
    for (int i = 0; i < 4; i++) {
      if (!corr.entry[i]) continue;
      std::int16_t& c = *corr.entry[i];
      const std::int64_t rate = i == 0 ? 100 : i < 3 ? CORR_NON_PAWN_RATE : CORR_CONT_RATE;  // percent
      const std::int64_t scale = std::int64_t(CORR_WEIGHT_SCALE) * 100;
      c = std::int16_t(std::clamp<std::int64_t>((c * scale + std::int64_t(error) * weight * rate) / scale,
                                                -CORR_LIMIT_CP * CORR_GRAIN, CORR_LIMIT_CP * CORR_GRAIN));
    }
  }
  if (!excluding && (tte.depth <= depth || tte.key != check || bound == BOUND_EXACT)) {
    // After a fail low every move scored below alpha and the "best" of them
    // is noise: keep the move already stored for this position, or store
    // none (so a revisit gets IIR, as a node with no known best move should).
    if (bound != BOUND_UPPER) tte.move = best_move.raw();
    else if (tte.key != check) tte.move = 0;
    tte.key = check;
    tte.score = std::int16_t(to_tt(best_score, ply));
    tte.depth = std::int8_t(depth);
    tte.bound = bound;
    tte.eval = std::int16_t(in_check ? EVAL_NONE : raw_eval);
  }
  return best_score;
}

// Searches only captures (or every move when in check) until the position
// is quiet, then trusts the static evaluation. "Stand pat": the side to move
// can usually decline to capture, so the evaluation is a lower bound.
int SearchWorker::quiesce(int alpha, int beta, int ply) {
  Position& pos = *pos_;
  pv_length_[ply] = ply;
  if ((++nodes_ & 2047) == 0) check_limits();
  if (stopped_) return 0;
  if (ply >= MAX_PLY - 1) return evaluate(pos);

  const bool in_check = pos.in_check();
  int best_score;
  if (in_check) {
    best_score = -MATE + ply;
  } else {
    best_score = evaluate_lazy(pos, alpha, beta) + corr_entries(pos, ply).total() / CORR_GRAIN;
    if (best_score >= beta) return best_score;
    if (best_score > alpha) alpha = best_score;
  }

  MoveList moves;
  generate_pseudo_legal(pos, moves, !in_check);
  int scores[MoveList::MAX_MOVES];
  score_moves(pos, moves, scores, Move(), killers_[ply][0], killers_[ply][1], history_);

  int legal = 0;
  for (int i = 0; i < moves.count; i++) {
    const Move move = pick_next(moves, scores, i);
    if (!in_check && !move.is_promotion()) {
      // Delta pruning: even winning this piece outright wouldn't reach alpha.
      const PieceType victim = captured_type(pos, move);
      if (best_score + ORDER_VALUE[victim] + 200 < alpha) continue;
      // Bad captures: skip any that lose material once the recaptures on
      // the square are played out (score_moves has already checked).
      if (scores[i] < BAD_CAPTURE_SCORE / 2) continue;
    }
    if (!pos.legal(move)) continue;
    ply_move_[ply] = pos.piece_on(move.from()) * 64 + move.to();  // for the child's correction history
    pos.make_move(move);
    legal++;
    const int score = -quiesce(-beta, -alpha, ply + 1);
    pos.unmake_move();
    if (stopped_) return 0;
    if (score > best_score) {
      best_score = score;
      if (score > alpha) {
        alpha = score;
        update_pv(ply, move);
        if (score >= beta) break;
      }
    }
  }
  if (in_check && legal == 0) return -MATE + ply;
  return best_score;
}

void SearchWorker::update_pv(int ply, Move m) {
  pv_table_[ply][ply] = m;
  const int child_length = pv_length_[ply + 1];
  for (int i = ply + 1; i < child_length; i++) pv_table_[ply][i] = pv_table_[ply + 1][i];
  pv_length_[ply] = std::max(child_length, ply + 1);
}

// ------------------------------------------------------- threaded wrapper

Searcher::~Searcher() {
  stop();
  wait();
}

void Searcher::new_game() {
  tt_.clear();
  worker_.new_game();
}

void Searcher::start(const Position& pos, const SearchLimits& limits, int move_overhead_ms) {
  stop();
  wait();
  budget_ = allocate_time(limits, pos.side_to_move(), move_overhead_ms);  // starts the clock
  root_ = pos;
  limits_ = limits;
  stop_ = false;
  thread_ = std::thread(&Searcher::run, this);
}

void Searcher::wait() {
  if (thread_.joinable()) thread_.join();
}

void Searcher::run() {
  SearchResult r = worker_.search(root_, limits_, budget_, &stop_, uci_send);
  // In infinite mode "bestmove" must wait for "stop".
  while (limits_.infinite && !stop_) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::string line = "bestmove " + r.best.uci();
  if (r.pv.size() >= 2) line += " ponder " + r.pv[1].uci();
  uci_send(line);
}

}  // namespace chess
