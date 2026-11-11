#include "search.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "evaluate.h"
#include "movepick.h"
#include "syzygy.h"
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
TUNE_PARAM(RFP_MARGIN, 90, 20, 250);

// Futility: at depth <= FUT_DEPTH, skip quiet moves when the static
// evaluation plus FUT_BASE + FUT_MARGIN per ply can't reach alpha.
TUNE_PARAM(FUT_DEPTH, 6, 1, 8);
TUNE_PARAM(FUT_BASE, -3, -100, 300);
TUNE_PARAM(FUT_MARGIN, 115, 40, 300);

// Late move pruning: at depth <= LMP_DEPTH, after LMP_BASE + depth^2 *
// LMP_SCALE / 100 quiet moves have been tried, the remaining ones are
// skipped (4, 7, 12 at depths 1-3).
TUNE_PARAM(LMP_DEPTH, 5, 1, 10);
TUNE_PARAM(LMP_BASE, 3, 0, 12);
TUNE_PARAM(LMP_SCALE, 80, 25, 300);

// SEE pruning: at depth <= SEE_PRUNE_DEPTH, moves that lose more than this
// much material on their square (static exchange) are skipped. Quiet moves
// get a margin growing with depth squared, captures one growing with depth.
// "Improving" (gentle version): the static evaluation is above the one two
// plies earlier (no information counts as improving). Improving nodes get a
// smaller reverse-futility margin (IMPROVING_RFP cp); late quiet moves
// (from the IMPROVING_LMR_MOVE-th) at non-PV nodes that are not improving
// get IMPROVING_LMR plies more reduction. Earlier versions (2.10) halved
// late move pruning and cut nodes by 35%: too much.
TUNE_PARAM(IMPROVING_RFP, 79, 0, 150);
TUNE_PARAM(IMPROVING_LMR, 1, 0, 2);
TUNE_PARAM(IMPROVING_LMR_MOVE, 10, 3, 20);

// The table's score refines the static evaluation in reverse futility and
// null move (1 on, 0 off); see negamax.
TUNE_PARAM(TT_EVAL_REFINE, 0, 0, 1);
TUNE_PARAM(TT_REFINE_SLACK, 2, 0, 6);

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
TUNE_PARAM(LMR_HISTORY, 2600, 500, 12000);
// Extra late move reduction at cut nodes (null-window nodes expected to
// fail high).
TUNE_PARAM(CUT_NODE_LMR, 1, 0, 3);
// Null move: a ply more reduction per NULL_EVAL_DIV centipawns the static
// evaluation is above beta, at most NULL_EVAL_MAX (0 = off; flat alone on
// 2.2 + IIR as "stronger null move v3").
// Deeper null-move reduction only for deep nodes: one more ply from depth
// NULL_DEEP_START, one more every NULL_DEEP_DIV plies after that. Shallow
// nodes keep 2-3 plies (bigger reductions there failed three times: the
// null search fell into quiescence or was clamped to 1 ply).
TUNE_PARAM(NULL_DEEP_START, 7, 6, 30);
TUNE_PARAM(NULL_DEEP_DIV, 3, 1, 12);
TUNE_PARAM(NULL_EVAL_DIV, 200, 0, 1000);
TUNE_PARAM(NULL_EVAL_MAX, 3, 0, 6);
// Threat-aware reductions: when the null move fails, the reply that refuted
// it is the opponent's threat. If it captures one of our pieces, moves of
// that piece (getting it out of the way) are reduced THREAT_LMR plies less.
// 0 = off (flat on 5.2).
TUNE_PARAM(THREAT_LMR, 1, 0, 3);
// History pruning: at depth <= HIST_PRUNE_DEPTH off the main line, a quiet
// move whose history (its own plus both continuation histories) is below
// -HIST_PRUNE_MARGIN per ply is skipped. 0 = off (flat on 5.2).
TUNE_PARAM(HIST_PRUNE_DEPTH, 3, 0, 8);
TUNE_PARAM(HIST_PRUNE_MARGIN, 4000, 500, 16000);
// Quiet history updates: a bonus of HIST_BONUS_MUL * depth - HIST_BONUS_SUB
// (at most HIST_BONUS_MAX) for the move that cut off, the same against the
// quiet moves tried before it, applied with gravity toward +/-HIST_GRAVITY
// (main table) and +/-CONT_HIST_GRAVITY (continuation tables).
TUNE_PARAM(HIST_BONUS_MUL, 200, 50, 600);
TUNE_PARAM(HIST_BONUS_SUB, 150, 0, 600);
TUNE_PARAM(HIST_BONUS_MAX, 1800, 300, 4000);
TUNE_PARAM(HIST_GRAVITY, 16384, 4000, 30000);
TUNE_PARAM(CONT_HIST_GRAVITY, 16384, 4000, 30000);
// Capture history (src/movepick.h): the same bonus, gravity toward
// +/-CAPT_HIST_GRAVITY.
TUNE_PARAM(CAPT_HIST_GRAVITY, 16384, 4000, 30000);

// Singular extensions: from SINGULAR_DEPTH, a hash move that the table
// rates well above every alternative is searched a ply deeper. The test
// searches the other moves to half depth against the table's score less
// SINGULAR_MARGIN / 10 per ply (before tuning: 2 x depth).
TUNE_PARAM(SINGULAR_DEPTH, 8, 4, 14);
TUNE_PARAM(SINGULAR_MARGIN, 19, 5, 80);
// Double extension: off the main line, a hash move whose alternatives fall
// short by more than DOUBLE_EXT_MARGIN gets two plies, at most
// DOUBLE_EXT_MAX times on one line. Negative extension: when the
// alternatives do reach the test's score and the table's score beats beta,
// the hash move is searched a ply shallower.
TUNE_PARAM(DOUBLE_EXT_MARGIN, 20, 0, 100);
TUNE_PARAM(DOUBLE_EXT_MAX, 6, 0, 20);
// Recapture extension (Berserk 8): 1 on, 0 off. Off: -10 +/- 13 vs 6.1
// (FAILED_EXPERIMENTS.md).
TUNE_PARAM(RECAPTURE_EXT, 0, 0, 1);
// History extension (Berserk 8): from HIST_EXT_DEPTH, a quiet hash move
// whose history (main + both continuation rows, each at most 16384) is at
// least HIST_EXT_MIN gets a ply more, when the singular test gave none.
// Off (HIST_EXT 0): -15 +/- 16 vs 6.1 (FAILED_EXPERIMENTS.md).
TUNE_PARAM(HIST_EXT, 0, 0, 1);
TUNE_PARAM(HIST_EXT_DEPTH, 7, 4, 14);
TUNE_PARAM(HIST_EXT_MIN, 20000, 8000, 45000);
// Check extension: a ply more in check, while the remaining depth is below
// CHECK_EXT_DEPTH (6.1: always, i.e. MAX_PLY).
TUNE_PARAM(CHECK_EXT_DEPTH, 7, 1, 128);
// Late move reductions for captures: the table's value less
// CAPT_LMR_BASE hundredths of a ply (rounded down after), a ply less per CAPT_LMR_HISTORY of capture
// history (or more below zero), at most two either way.
TUNE_PARAM(CAPT_LMR_BASE, 100, 0, 300);
TUNE_PARAM(CAPT_LMR_HISTORY, 4000, 500, 16000);

// ProbCut: from PROBCUT_DEPTH at a non-PV node, a capture that wins enough
// material (SEE) and still beats beta + PROBCUT_MARGIN in a search
// PROBCUT_REDUCTION plies shallower is taken as proof of a cutoff.
TUNE_PARAM(PROBCUT_DEPTH, 4, 3, 10);
TUNE_PARAM(PROBCUT_MARGIN, 222, 50, 500);
TUNE_PARAM(PROBCUT_REDUCTION, 4, 2, 6);

// Time use, in percent of the planned time: after a depth, the search stops
// once past the plan scaled by how settled it is. A best move just changed
// gets TM_STABLE_BASE; each further depth it survives takes TM_STABLE_STEP
// off, for up to TM_STABLE_DEPTHS depths. A score that fell since the last
// depth adds TM_DROP_PER_CP per centipawn, at most TM_DROP_MAX.
TUNE_PARAM(TM_STABLE_BASE, 130, 60, 250);
TUNE_PARAM(TM_STABLE_STEP, 10, 0, 40);
TUNE_PARAM(TM_STABLE_DEPTHS, 6, 1, 12);
TUNE_PARAM(TM_DROP_PER_CP, 1, 0, 5);
TUNE_PARAM(TM_DROP_MAX, 40, 0, 150);
// Search effort: from depth TM_NODE_DEPTH the plan is also scaled by
// TM_NODE_BASE - TM_NODE_MUL / 100 x the percentage of the search's nodes
// spent on the best move. A best move that took nearly all the effort (its
// rivals fell quickly) means a clear position: stop sooner. One that took
// little (its rivals needed real work) means a contested one: think on.
// Off (base 100, mul 0): flat on 5.2.
TUNE_PARAM(TM_NODE_DEPTH, 6, 1, 20);
TUNE_PARAM(TM_NODE_BASE, 150, 50, 300);
TUNE_PARAM(TM_NODE_MUL, 100, 0, 250);
// Surprise clock: when the opponent plays the reply the last search
// expected, that search already looked at this position (the table is full
// of it), so the plan is scaled by TM_EXPECTED_PCT percent; after any other
// reply, by TM_SURPRISE_PCT. Off (100 / 100): -10 on 5.2.
TUNE_PARAM(TM_EXPECTED_PCT, 100, 30, 150);
TUNE_PARAM(TM_SURPRISE_PCT, 100, 50, 200);

// Correction history entries are in 1/CORR_GRAIN centipawns, each within
// +/-CORR_LIMIT_CP; each update moves every table's entry by depth + 1
// parts in CORR_WEIGHT_SCALE (at most CORR_MAX_WEIGHT) of what the tables
// together still get wrong. With one table, a running average of the error.
// The non-pawn tables learn at CORR_NON_PAWN_RATE percent of that speed: at
// full speed all three chased the same error and the sum overshot.
// Values from an SPSA run (Oct 2026, 2,000 pairs at 8+0.08, from 64, 16, 25).
constexpr int CORR_GRAIN = 256;
constexpr int CORR_WEIGHT_SCALE = 256;
TUNE_PARAM(CORR_LIMIT_CP, 50, 16, 127);
TUNE_PARAM(CORR_MAX_WEIGHT, 15, 4, 64);
TUNE_PARAM(CORR_NON_PAWN_RATE, 27, 5, 100);
// The last-two-moves table, far sparser, learns at CORR_CONT_RATE percent
// and is held within +/-CORR_CONT_LIMIT_CP (the cautious version from 4.0,
// even with it then; retried on a newer network as the owner suggested).
TUNE_PARAM(CORR_CONT_RATE, 10, 2, 100);
TUNE_PARAM(CORR_CONT_LIMIT_CP, 16, 4, 127);

// TM_MOVES: the share of the clock planned per move is 1 / TM_MOVES
// (6.3: 25, +27 +/- 17 over 6.2.1's 40; 20 was no better than 25).
TUNE_PARAM(TM_MOVES, 25, 10, 80);
// PARTIAL_ITER: when the clock stops a depth part-way, a root move that
// already beat the previous depth's best move in it is played. On since
// 6.4 (flat alone on 6.2.1; +11 +/- 7 together with QS_TT and ASP_DELTA).
TUNE_PARAM(PARTIAL_ITER, 1, 0, 1);
// HIST_DECAY_SHIFT: every search starts by shifting all history tables
// right by this (2 = keep a quarter, as Engine 1.6 did before history
// gravity; 0 = keep everything). Candidate values: 1 and 0.
TUNE_PARAM(HIST_DECAY_SHIFT, 2, 0, 4);
// TM_FAIL_LOW_PCT: when the previous depth's best move fails low at the
// root, the hard time limit grows to this percentage of itself (still
// within half the clock), so the re-search can find a replacement
// (played even from an unfinished depth with PARTIAL_ITER). 100 = off.
TUNE_PARAM(TM_FAIL_LOW_PCT, 100, 100, 300);
// QS_TT: quiescence probes the table and stores its results at depth 0.
// On since 6.4 (+3 +/- 9 alone on 6.3).
TUNE_PARAM(QS_TT, 1, 0, 1);
// ASP_DELTA: the first aspiration window's half-width (6.4: 15; 35 before,
// 15 alone was 0 +/- 12 on 6.3).
TUNE_PARAM(ASP_DELTA, 15, 5, 100);

using LmrTable = std::array<std::array<std::int8_t, 64>, 64>;
LmrTable make_lmr() {
  LmrTable t{};
  for (int d = 1; d < 64; d++)
    for (int m = 1; m < 64; m++)
      t[d][m] = std::int8_t(LMR_BASE / 100.0 + std::log(d) * std::log(m) / (LMR_DIV / 100.0));
  return t;
}
LmrTable LMR = make_lmr();

// The same reductions in hundredths of a ply (for captures, whose offset
// CAPT_LMR_BASE is in hundredths too, so values between whole plies count).
using Lmr100Table = std::array<std::array<std::int16_t, 64>, 64>;
Lmr100Table make_lmr100() {
  Lmr100Table t{};
  for (int d = 1; d < 64; d++)
    for (int m = 1; m < 64; m++)
      t[d][m] = std::int16_t(LMR_BASE + std::log(d) * std::log(m) * 10000.0 / LMR_DIV);
  return t;
}
Lmr100Table LMR100 = make_lmr100();

// Mate scores are stored relative to the position, not the root, so they
// stay correct when the position is reached at a different depth.
// Tablebase scores too.
int to_tt(int score, int ply) {
  if (score >= TB_WIN - MAX_PLY) return score + ply;
  if (score <= -(TB_WIN - MAX_PLY)) return score - ply;
  return score;
}

int from_tt(int score, int ply) {
  if (score >= TB_WIN - MAX_PLY) return score - ply;
  if (score <= -(TB_WIN - MAX_PLY)) return score + ply;
  return score;
}

// The tables apply with no castling rights, few enough pieces, and (for
// the win/draw/loss tables in the search) right after a capture or pawn move.
bool tb_applies(const Position& pos) {
  return tb::largest() > 0 && pos.castling_rights() == 0 && popcount(pos.pieces()) <= tb::largest();
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

void search_params_changed() {
  LMR = make_lmr();
  LMR100 = make_lmr100();
}

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
  const int moves_to_go = limits.movestogo > 0 ? std::min(limits.movestogo, int(TM_MOVES)) : int(TM_MOVES);
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
  b.hard_cap_ms = std::max(b.hard_ms, hard_cap);
  b.adaptive = true;
  return b;
}

// ------------------------------------------------------------- the search

void SearchWorker::new_game() {
  std::fill(&history_[0][0], &history_[0][0] + PIECE_NB * 64, 0);
  std::fill(&capt_history_[0][0][0], &capt_history_[0][0][0] + PIECE_NB * 64 * 6, std::int16_t(0));
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
  tbhits_ = 0;
  root_move_nodes_.fill(0);
  stopped_ = false;
  can_stop_ = false;
  stop_flag_ = stop;
  hard_ms_ = budget.hard_ms;
  hard_fail_low_ms_ = budget.adaptive ? std::min(budget.hard_cap_ms, budget.hard_ms * TM_FAIL_LOW_PCT / 100) : budget.hard_ms;
  max_nodes_ = limits.nodes;
  start_ = budget.start;
  for (auto& k : killers_) k[0] = k[1] = Move();
  for (auto& row : history_)
    for (int& h : row) h >>= HIST_DECAY_SHIFT;  // as Engine 1.6: an arithmetic shift, rounding down
  for (std::int16_t& h : cont_history_) h = std::int16_t(h >> HIST_DECAY_SHIFT);
  for (auto& by_piece : capt_history_)
    for (auto& by_to : by_piece)
      for (std::int16_t& h : by_to) h = std::int16_t(h >> HIST_DECAY_SHIFT);

  SearchResult result;
  MoveList legal;
  generate_legal(pos, legal);
  if (legal.size() == 0) {
    result.score = pos.in_check() ? -MATE : 0;
    return result;
  }
  result.best = legal[0];
  result.pv = {legal[0]};

  // In the tables: play their move (it keeps the result and, by distance
  // to zero, wins within the fifty-move rule), no search needed.
  Move tb_move;
  tb::Wdl tb_wdl;
  if (tb_applies(pos) && tb::probe_root(pos, legal, tb_move, tb_wdl)) {
    result.best = tb_move;
    result.pv = {tb_move};
    result.score = tb_wdl == tb::WIN ? TB_WIN - 1 : tb_wdl == tb::LOSS ? -(TB_WIN - 1) : 0;
    result.depth = 1;
    if (on_info)
      on_info("info depth 1 score " + score_to_uci(result.score) + " nodes 0 tbhits 1 time " +
              std::to_string(elapsed_ms()) + " pv " + tb_move.uci());
    return result;
  }

  const int max_depth = limits.depth > 0 ? std::min(limits.depth, MAX_PLY - 1) : MAX_PLY - 1;
  int previous = 0;
  int stable_depths = 0;  // depths the best move has stayed the same
  for (int depth = 1; depth <= max_depth; depth++) {
    root_depth_ = depth;
    root_prev_best_ = result.best;
    root_prev_searched_ = false;
    int score;
    if (depth >= 5 && !is_mate_score(previous)) {
      // Aspiration window: expect a score near the last one, widen if wrong.
      int delta = ASP_DELTA;
      int alpha = previous - delta, beta = previous + delta;
      for (;;) {
        score = negamax(depth, alpha, beta, 0, false, false);
        if (stopped_) break;
        if (score <= alpha) {
          alpha = std::max(-INF, alpha - delta);
          root_prev_searched_ = false;  // the re-search must reach the old best move again
        } else if (score >= beta) beta = std::min(INF, beta + delta);
        else break;
        delta *= 2;
      }
    } else {
      score = negamax(depth, -INF, INF, 0, false, false);
    }
    if (stopped_) {
      // An unfinished depth can't be trusted, except for a move that beat
      // the previous depth's best move after that one was searched.
      if (PARTIAL_ITER && depth > 1 && root_prev_searched_ && pv_length_[0] > 0 && pv_table_[0][0] != result.best) {
        result.pv.assign(pv_table_[0], pv_table_[0] + pv_length_[0]);
        result.best = result.pv[0];
      }
      break;
    }
    can_stop_ = true;

    const int drop = depth > 1 ? previous - score : 0;
    previous = score;
    const Move last_best = result.best;
    if (pv_length_[0] > 0) {
      result.pv.assign(pv_table_[0], pv_table_[0] + pv_length_[0]);
      result.best = result.pv[0];
    }
    stable_depths = result.best == last_best ? stable_depths + 1 : 0;
    result.score = score;
    result.depth = depth;

    const std::int64_t elapsed = elapsed_ms();
    if (on_info) {
      std::string line = "info depth " + std::to_string(depth) + " score " + score_to_uci(score) +
                         " nodes " + std::to_string(nodes_) + " nps " +
                         std::to_string(nodes_ * 1000 / std::max<std::int64_t>(elapsed, 1)) + " time " +
                         std::to_string(elapsed) + (tbhits_ ? " tbhits " + std::to_string(tbhits_) : "") + " pv";
      for (Move m : result.pv) line += " " + m.uci();
      on_info(line);
    }

    if (legal.size() == 1 && depth >= 4) break;  // only one move: no need to think
    if (is_mate_score(score) && MATE - std::abs(score) <= depth) break;  // forced mate found
    if (budget.soft_ms >= 0) {
      std::int64_t soft = budget.soft_ms;
      if (budget.adaptive) {
        // Settled searches stop early, unsettled ones think on (the hard
        // limit still applies).
        const int stability = TM_STABLE_BASE - TM_STABLE_STEP * std::min(stable_depths, int(TM_STABLE_DEPTHS));
        const int falling = std::clamp(drop * TM_DROP_PER_CP, 0, int(TM_DROP_MAX));
        soft = soft * stability / 100 * (100 + falling) / 100;
        if (depth >= TM_NODE_DEPTH && nodes_ > 0) {
          const int best_share = int(root_move_nodes_[result.best.from() * 64 + result.best.to()] * 100 / nodes_);
          soft = soft * std::max(TM_NODE_BASE - best_share * TM_NODE_MUL / 100, 10) / 100;
        }
      }
      if (elapsed >= soft) break;
    }
    if (stop && stop->load(std::memory_order_relaxed)) break;
  }
  result.nodes = nodes_;
  return result;
}

int SearchWorker::negamax(int depth, int alpha, int beta, int ply, bool cut_node, bool after_null, Move excluded) {
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

  double_exts_[ply + 1] = double_exts_[ply];  // for children reached outside the move loop (null move, ProbCut)
  const bool in_check = pos.in_check();
  if (in_check && depth < CHECK_EXT_DEPTH) depth++;  // look further when in check, near the leaves (Berserk 8)
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
  // Tablebases: an exact result for this position. Off the root, only right
  // after a capture or pawn move (the tables don't know the fifty-move count).
  if (ply > 0 && !excluding && pos.halfmove_clock() == 0 && tb_applies(pos)) {
    const tb::Wdl wdl = tb::probe_wdl(pos);
    if (wdl != tb::FAILED) {
      tbhits_++;
      const int score = wdl == tb::WIN ? TB_WIN - ply : wdl == tb::LOSS ? -TB_WIN + ply : 0;
      const Bound bound = wdl == tb::WIN ? BOUND_LOWER : wdl == tb::LOSS ? BOUND_UPPER : BOUND_EXACT;
      if (bound == BOUND_EXACT || (bound == BOUND_LOWER && score >= beta) || (bound == BOUND_UPPER && score <= alpha)) {
        tte.key = check;
        tte.move = 0;
        tte.score = std::int16_t(to_tt(score, ply));
        tte.depth = std::int8_t(std::min(depth + 6, MAX_PLY - 1));
        tte.bound = bound;
        tte.eval = EVAL_NONE;
        return score;
      }
    }
  }

  // Internal iterative reduction: with no remembered best move the ordering
  // here is poor, so a full-depth search is mostly wasted; go a ply shallower.
  if (ply > 0 && depth >= 4 && !tt_move) depth--;

  int raw_eval = 0, static_eval = 0;
  Square threat_sq = NO_SQUARE;  // our piece the null move's refutation captures
  if (!in_check) {
    // The table keeps the static evaluation: reuse it rather than evaluate again.
    raw_eval = tt_eval != EVAL_NONE ? tt_eval : evaluate(pos);
    // Corrected by what searches of this pawn structure have found.
    static_eval = raw_eval + corr_entries(pos, ply).total() / CORR_GRAIN;
  }
  eval_stack_[ply] = in_check ? EVAL_NONE : static_eval;
  const bool improving = !in_check && (ply < 2 || eval_stack_[ply - 2] == EVAL_NONE || static_eval > eval_stack_[ply - 2]);
  // The table's score refines the static evaluation for reverse futility
  // and null move, in the direction its bound allows: an upper bound below
  // the static eval lowers it (any depth: it only makes those two prunings
  // more careful), a lower bound above it raises it only from an entry
  // nearly as deep as this node (TT_REFINE_SLACK plies; a deeper one would
  // already have cut off above). Futility, "improving" and correction
  // history keep the static eval.
  int eval = static_eval;
  if (TT_EVAL_REFINE && !in_check && !excluding && tt_bound != BOUND_NONE && !is_mate_score(tt_score)) {
    if (tt_score < static_eval && (tt_bound == BOUND_UPPER || tt_bound == BOUND_EXACT))
      eval = tt_score;
    else if (tt_score > static_eval && (tt_bound == BOUND_LOWER || tt_bound == BOUND_EXACT) &&
             tt_depth >= depth - TT_REFINE_SLACK)
      eval = tt_score;
  }
  if (!in_check) {
    if (!pv_node && !excluding && !is_mate_score(beta)) {
      // Reverse futility: so far ahead that a shallow search won't change it
      // (a little sooner when the position is getting better).
      if (depth <= RFP_DEPTH && eval - RFP_MARGIN * depth + (improving ? int(IMPROVING_RFP) : 0) >= beta) return eval;
      // Null move: if passing still leaves us at or above beta, a real move
      // would too. Not without pieces, where passing could be best (zugzwang).
      if (!after_null && depth >= 3 && eval >= beta && has_pieces(pos)) {
        const int reduction = (depth >= 7 ? 3 : 2) + (depth >= NULL_DEEP_START ? 1 + (depth - NULL_DEEP_START) / NULL_DEEP_DIV : 0) +
                              (NULL_EVAL_DIV > 0 ? std::min((eval - beta) / std::max(int(NULL_EVAL_DIV), 1), int(NULL_EVAL_MAX)) : 0);
        ply_move_[ply] = -1;
        pos.make_null_move();
        const int score = -negamax(depth - 1 - reduction, -beta, -beta + 1, ply + 1, !cut_node, true);
        pos.unmake_null_move();
        if (stopped_) return 0;
        if (score >= beta) return is_mate_score(score) ? beta : score;
        // The child's PV starts with its refutation of the pass: the threat.
        if (pv_length_[ply + 1] > ply + 1) {
          const Move threat = pv_table_[ply + 1][ply + 1];
          if (threat.is_capture() && pos.piece_on(threat.to()) != NO_PIECE &&
              color_of(pos.piece_on(threat.to())) == pos.side_to_move())
            threat_sq = threat.to();
        }
      }
      // ProbCut: a good capture that beats beta by a wide margin in a
      // shallower search almost surely beats beta here. Skipped when the
      // table already says a search this deep stays below that margin.
      const int probcut_beta = beta + PROBCUT_MARGIN;
      if (depth >= PROBCUT_DEPTH && !is_mate_score(probcut_beta) &&
          !(tt_bound != BOUND_NONE && tt_depth >= depth - (PROBCUT_REDUCTION - 1) && tt_score < probcut_beta)) {
        MoveList captures;
        generate_pseudo_legal(pos, captures, true);
        int scores[MoveList::MAX_MOVES];
        score_tactical(pos, captures, scores, &capt_history_);
        const int probcut_depth = depth - PROBCUT_REDUCTION;
        for (int i = 0; i < captures.count; i++) {
          const Move move = pick_next(captures, scores, i);
          if (!pos.see_ge(move, probcut_beta - static_eval) || !pos.legal(move)) continue;
          ply_move_[ply] = pos.piece_on(move.from()) * 64 + move.to();
          pos.make_move(move);
          // A quiescence search first, which is cheap; the real test only if it passes.
          int score = -quiesce(-probcut_beta, -probcut_beta + 1, ply + 1);
          if (score >= probcut_beta && probcut_depth > 0)
            score = -negamax(probcut_depth, -probcut_beta, -probcut_beta + 1, ply + 1, !cut_node, false);
          pos.unmake_move();
          if (stopped_) return 0;
          if (score >= probcut_beta) {
            if (tte.key != check || tte.depth <= probcut_depth + 1) {
              tte.key = check;
              tte.move = move.raw();
              tte.score = std::int16_t(to_tt(score, ply));
              tte.depth = std::int8_t(probcut_depth + 1);
              tte.bound = BOUND_LOWER;
              tte.eval = std::int16_t(raw_eval);
            }
            return score;
          }
        }
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
  std::int16_t* captures_searched[MoveList::MAX_MOVES];  // capture history entries of captures tried
  int captures_count = 0;
  int quiets_count = 0;

  const Move killer1 = killers_[ply][0], killer2 = killers_[ply][1];
  std::int16_t* const cont1 = cont_row(ply, 1);
  std::int16_t* const cont2 = cont_row(ply, 2);
  MovePicker picker(pos, excluding ? Move() : tt_move, killer1, killer2, history_, cont1, cont2, &capt_history_);
  const int alpha_at_start = alpha;
  int best_score = -INF;
  Move best_move;
  int legal = 0;
  // The move that led here, for the recapture extension.
  const Move prev_move = ply > 0 ? pos.last_move() : Move();

  while (const Move move = picker.next()) {
    if (move == excluded || !pos.legal(move)) continue;
    const bool quiet = !move.is_capture() && !move.is_promotion();
    const Piece piece = pos.piece_on(move.from());
    std::int16_t* const capt_entry = move.is_capture() ? &capt_history_[piece][move.to()][captured_type(pos, move)] : nullptr;
    legal++;

    // Pruned moves are decided before making them, which is cheaper. None
    // that give check, which is worked out only when a rule gets that far.
    if (legal > 1) {
      int checks = -1;  // whether the move gives check; -1: not known yet
      const auto gives_check = [&] {
        if (checks < 0) checks = pos.gives_check(move);
        return checks == 1;
      };
      // Once one quiet move is pruned by these two rules, every later one is
      // too unless it gives check: the picker stops sorting them.
      if (futile && quiet && !gives_check()) {
        picker.skip_quiets();
        continue;
      }
      // Late move pruning: moves ordered this late almost never matter here.
      if (prune_late && quiet && !gives_check() && ++quiets_tried > lmp_quiets) {
        picker.skip_quiets();
        continue;
      }
      // History pruning: quiet moves that have kept failing in similar
      // positions, near the leaves, once something has been searched.
      if (!pv_node && !in_check && quiet && depth <= HIST_PRUNE_DEPTH && best_score > -(TB_WIN - MAX_PLY)) {
        const int index = piece * 64 + move.to();
        const int history = history_[piece][move.to()] + (cont1 ? cont1[index] : 0) + (cont2 ? cont2[index] : 0);
        if (history < -HIST_PRUNE_MARGIN * depth && !gives_check()) continue;
      }
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
      const int score = negamax((depth - 1) / 2, singular_beta - 1, singular_beta, ply, cut_node, false, move);
      pv_length_[ply] = ply;  // the test search wrote this node's PV
      if (stopped_) return 0;
      if (score < singular_beta) {
        extension = 1;
        if (!pv_node && score < singular_beta - DOUBLE_EXT_MARGIN && double_exts_[ply] < DOUBLE_EXT_MAX) extension = 2;
      } else if (singular_beta >= beta) {
        return singular_beta;
      } else if (tt_score >= beta) {
        extension = -1;  // other moves come close and this one is expected to cut: less effort here
      }
    }
    // Recapture extension: on the main line, taking back on the square
    // where the opponent just captured gets a ply more, so the exchange is
    // seen to its end.
    if (RECAPTURE_EXT && extension == 0 && pv_node && prev_move && prev_move.is_capture() && move.is_capture() &&
        move.to() == prev_move.to() && ply < 2 * root_depth_)
      extension = 1;
    if (HIST_EXT && extension == 0 && move == tt_move && quiet && depth >= HIST_EXT_DEPTH && ply > 0 && ply < 2 * root_depth_) {
      const int index = piece * 64 + move.to();
      const int history = history_[piece][move.to()] + (cont1 ? cont1[index] : 0) + (cont2 ? cont2[index] : 0);
      if (history >= HIST_EXT_MIN) extension = 1;
    }
    const int new_depth = depth - 1 + extension;
    double_exts_[ply + 1] = double_exts_[ply] + (extension == 2);

    ply_move_[ply] = piece * 64 + move.to();
    const std::uint64_t nodes_before = nodes_;
    pos.make_move(move);
    if (new_depth > 0 || QS_TT) tt_.prefetch(pos.key());  // the child probes the table (quiescence only with QS_TT)

    int score;
    if (legal == 1) {
      score = -negamax(new_depth, -beta, -alpha, ply + 1, !pv_node && !cut_node, false);
    } else {
      // Late move reductions: moves ordered late are rarely best, so search
      // them shallower first and only re-search if they surprise us.
      // Not at the root: reducing there can miss a quicker mate.
      int reduction = 0;
      if (depth >= 3 && legal > 2 && quiet && !in_check && !pos.in_check() && ply > 0) {  // pos: after the move
        reduction = LMR[std::min(depth, 63)][std::min(legal, 63)];
        if (pv_node) reduction--;  // less on the main line
        if (cut_node) reduction += CUT_NODE_LMR;  // a refutation is expected: the late moves matter less
        if (move == killer1 || move == killer2) reduction--;  // less for moves that refuted others
        if (move.from() == threat_sq) reduction -= THREAT_LMR;  // the piece the opponent threatens
        if (!improving && !pv_node && legal >= IMPROVING_LMR_MOVE) reduction += IMPROVING_LMR;  // getting worse: late quiets matter less
        const int index = piece * 64 + move.to();
        const int history = history_[piece][move.to()] + (cont1 ? cont1[index] : 0) + (cont2 ? cont2[index] : 0);
        reduction -= std::clamp(history / LMR_HISTORY, -2, 2);  // less for moves that did well elsewhere
        reduction = std::clamp(reduction, 0, depth - 2);
      } else if (depth >= 3 && legal > 2 && capt_entry && !in_check && !pos.in_check() && ply > 0) {
        // Captures ordered late (losing ones, or ones that did badly before)
        // get a milder reduction, steered by capture history.
        // Rounded down after the offset (adding 10000 keeps the division a floor).
        reduction = (LMR100[std::min(depth, 63)][std::min(legal, 63)] - CAPT_LMR_BASE + 10000) / 100 - 100;
        if (pv_node) reduction--;
        if (cut_node) reduction += CUT_NODE_LMR;
        reduction -= std::clamp(*capt_entry / CAPT_LMR_HISTORY, -2, 2);
        if (move.from() == threat_sq) reduction -= THREAT_LMR;
        reduction = std::clamp(reduction, 0, depth - 2);
      }
      // Principal variation search: prove the move is no better with a null
      // window, and only search it fully if it is.
      score = -negamax(new_depth - reduction, -alpha - 1, -alpha, ply + 1, true, false);
      if (reduction > 0 && score > alpha) score = -negamax(new_depth, -alpha - 1, -alpha, ply + 1, !cut_node, false);
      if (score > alpha && score < beta) score = -negamax(new_depth, -beta, -alpha, ply + 1, false, false);
    }
    pos.unmake_move();
    if (ply == 0) root_move_nodes_[move.from() * 64 + move.to()] += nodes_ - nodes_before;
    if (stopped_) return 0;
    if (ply == 0 && move == root_prev_best_) {
      root_prev_searched_ = true;
      // It fell below the window: allow more time to find something better.
      if (score <= alpha_at_start && hard_ms_ >= 0) hard_ms_ = std::max(hard_ms_, hard_fail_low_ms_);
    }

    if (score > best_score) {
      best_score = score;
      best_move = move;
      if (score > alpha) {
        alpha = score;
        update_pv(ply, move);
        if (score >= beta) {
          // History gravity: each update moves an entry toward +/-the
          // limit by a share of the bonus that shrinks as it nears it, so
          // entries saturate smoothly and recent results still count.
          const int bonus = std::min(HIST_BONUS_MUL * depth - HIST_BONUS_SUB, HIST_BONUS_MAX);
          const auto gravity = [](int value, int amount, int limit) {
            return value + amount - value * std::abs(amount) / limit;
          };
          // Capture history: for the capture that cut off, and against the
          // captures tried before the move that did.
          if (capt_entry) *capt_entry = std::int16_t(gravity(*capt_entry, bonus, CAPT_HIST_GRAVITY));
          for (int c = 0; c < captures_count; c++)
            *captures_searched[c] = std::int16_t(gravity(*captures_searched[c], -bonus, CAPT_HIST_GRAVITY));
          if (quiet) {
            if (move != killer1) {
              killers_[ply][1] = killer1;
              killers_[ply][0] = move;
            }
            const auto cont_add = [&](std::int16_t* row, int index, int amount) {
              if (row) row[index] = std::int16_t(gravity(row[index], amount, CONT_HIST_GRAVITY));
            };
            int& h = history_[piece][move.to()];
            h = gravity(h, bonus, HIST_GRAVITY);
            cont_add(cont1, piece * 64 + move.to(), bonus);
            cont_add(cont2, piece * 64 + move.to(), bonus);
            // The quiet moves tried before it failed here: count against them.
            for (int q = 0; q < quiets_count; q++) {
              int& hq = history_[quiets_searched[q] / 64][quiets_searched[q] % 64];
              hq = gravity(hq, -bonus, HIST_GRAVITY);
              cont_add(cont1, quiets_searched[q], -bonus);
              cont_add(cont2, quiets_searched[q], -bonus);
            }
          }
          break;
        }
      }
    }
    if (quiet) quiets_searched[quiets_count++] = piece * 64 + move.to();
    else if (capt_entry) captures_searched[captures_count++] = capt_entry;
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
      const int limit = (i < 3 ? CORR_LIMIT_CP : CORR_CONT_LIMIT_CP) * CORR_GRAIN;
      c = std::int16_t(std::clamp<std::int64_t>((c * scale + std::int64_t(error) * weight * rate) / scale, -limit, limit));
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
  // The table: any entry for this position is at least as deep as a
  // quiescence search, so its score cuts off where its bound allows.
  TTEntry* tte = nullptr;
  std::uint32_t check = 0;
  const int alpha_at_start = alpha;
  if (QS_TT) {
    tte = &tt_.entry(pos.key());
    check = TranspositionTable::check(pos.key());
    if (beta - alpha == 1 && tte->bound != BOUND_NONE && tte->key == check) {
      const int score = from_tt(tte->score, ply);
      if (tte->bound == BOUND_EXACT || (tte->bound == BOUND_LOWER && score >= beta) ||
          (tte->bound == BOUND_UPPER && score <= alpha))
        return score;
    }
  }
  int best_score;
  int raw_eval = EVAL_NONE;
  if (in_check) {
    best_score = -MATE + ply;
  } else {
    raw_eval = evaluate_lazy(pos, alpha, beta);
    best_score = raw_eval + corr_entries(pos, ply).total() / CORR_GRAIN;
    if (best_score >= beta) return best_score;
    if (best_score > alpha) alpha = best_score;
  }

  MoveList moves;
  generate_pseudo_legal(pos, moves, !in_check);
  int scores[MoveList::MAX_MOVES];
  // Captures: SEE only for those reached (below); evasions: the full ordering.
  if (in_check) score_moves(pos, moves, scores, Move(), killers_[ply][0], killers_[ply][1], history_);
  else score_tactical(pos, moves, scores, &capt_history_);

  int legal = 0;
  for (int i = 0; i < moves.count; i++) {
    const Move move = pick_next(moves, scores, i);
    if (!in_check && !move.is_promotion()) {
      // Delta pruning: even winning this piece outright wouldn't reach alpha.
      const PieceType victim = captured_type(pos, move);
      if (best_score + ORDER_VALUE[victim] + 200 < alpha) continue;
      // Bad captures: skip any that lose material once the recaptures on
      // the square are played out.
      if (!pos.see_ge(move, 0)) continue;
    }
    if (!pos.legal(move)) continue;
    ply_move_[ply] = pos.piece_on(move.from()) * 64 + move.to();  // for the child's correction history
    pos.make_move(move);
    if (QS_TT) tt_.prefetch(pos.key());
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
  // Stored only over an empty slot or another quiescence result: a main
  // search entry is worth more than this one.
  if (tte && (tte->bound == BOUND_NONE || tte->depth <= 0)) {
    if (tte->key != check) tte->move = 0;
    tte->key = check;
    tte->score = std::int16_t(to_tt(best_score, ply));
    tte->depth = 0;
    tte->bound = best_score >= beta ? BOUND_LOWER : best_score > alpha_at_start ? BOUND_EXACT : BOUND_UPPER;
    tte->eval = std::int16_t(raw_eval);
  }
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
  has_prediction_ = false;
  tt_.clear();
  worker_.new_game();
}

void Searcher::start(const Position& pos, const SearchLimits& limits, int move_overhead_ms) {
  stop();
  wait();
  budget_ = allocate_time(limits, pos.side_to_move(), move_overhead_ms);  // starts the clock
  if (budget_.adaptive && has_prediction_) {
    const int pct = pos.key() == predicted_key_ ? TM_EXPECTED_PCT : TM_SURPRISE_PCT;
    budget_.soft_ms = std::clamp<std::int64_t>(budget_.soft_ms * pct / 100, 1, budget_.hard_ms);
  }
  has_prediction_ = false;
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
  if (r.pv.size() >= 2) {
    Position after = root_;
    after.make_move(r.pv[0]);
    after.make_move(r.pv[1]);
    predicted_key_ = after.key();
    has_prediction_ = true;
  }
  // In infinite mode "bestmove" must wait for "stop".
  while (limits_.infinite && !stop_) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  std::string line = "bestmove " + r.best.uci();
  if (r.pv.size() >= 2) line += " ponder " + r.pv[1].uci();
  uci_send(line);
}

}  // namespace chess
