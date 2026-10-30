// The search, ported from Engine 1.6 (engine-1/src/engine/search.js):
// iterative deepening negamax alpha-beta with aspiration windows, principal
// variation search, quiescence, a transposition table, null-move pruning,
// reverse futility, futility and late move pruning, late move reductions,
// and killer/history move ordering (movepick.h).
//
// It runs on its own thread so the UCI loop keeps answering while it thinks.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "movegen.h"
#include "movepick.h"
#include "position.h"
#include "tt.h"

namespace chess {

constexpr int MATE = 30000;  // mate now; mate in n plies scores MATE - n
constexpr int INF = 32000;
constexpr int MAX_PLY = 100;
inline bool is_mate_score(int s) { return std::abs(s) >= MATE - MAX_PLY; }

// What "go" asked for. -1 means "not given".
struct SearchLimits {
  int wtime = -1, btime = -1;
  int winc = 0, binc = 0;
  int movestogo = 0;
  int movetime = -1;
  int depth = 0;
  std::uint64_t nodes = 0;
  bool infinite = false;
};

// How long to think, in milliseconds. soft: don't start another iteration
// after this; hard: abort the search. -1 means no limit.
struct TimeBudget {
  std::int64_t soft_ms = -1;
  std::int64_t hard_ms = -1;
  // Measured from when "go" arrived, not when the search thread got going.
  std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
};

TimeBudget allocate_time(const SearchLimits& limits, Color us, int move_overhead_ms);

// Rebuilds the tables made from tunable parameters (src/tune.h); call
// after changing one.
void search_params_changed();

struct SearchResult {
  Move best;
  int score = 0;
  int depth = 0;
  std::uint64_t nodes = 0;
  std::vector<Move> pv;
};

// One search: the tree walk and its tables. Usable directly (tests, bench)
// or through Searcher (UCI, threaded).
class SearchWorker {
 public:
  explicit SearchWorker(TranspositionTable& tt) : tt_(tt) {}

  // Searches pos (left unchanged) within the limits. stop is polled and may
  // be set from another thread. on_info gets a UCI "info" line per depth.
  SearchResult search(Position& pos, const SearchLimits& limits, const TimeBudget& budget,
                      const std::atomic<bool>* stop = nullptr,
                      const std::function<void(const std::string&)>& on_info = nullptr);

  void new_game();  // clears history; the caller clears the TT

 private:
  // `excluded`: a move to leave out (the singular extension's test search).
  int negamax(int depth, int alpha, int beta, int ply, bool after_null, Move excluded = Move());
  int quiesce(int alpha, int beta, int ply);
  void update_pv(int ply, Move m);
  void check_limits();
  std::int64_t elapsed_ms() const;

  TranspositionTable& tt_;
  Position* pos_ = nullptr;
  Move killers_[MAX_PLY][2] = {};
  int history_[PIECE_NB][64] = {};
  // Continuation history (src/movepick.h), indexed [earlier move][this move]
  // by piece * 64 + to; used for the move one ply earlier (the opponent's)
  // and two plies earlier (our own).
  std::vector<std::int16_t> cont_history_ = std::vector<std::int16_t>(CONT_INDEX * CONT_INDEX);
  int ply_move_[MAX_PLY + 1] = {};  // piece * 64 + to of the move made at each ply; -1 for a null move
  // The continuation history row after the move made `back` plies before
  // ply, or nullptr if there was none (before the root, a null move).
  std::int16_t* cont_row(int ply, int back) {
    return ply < back || ply_move_[ply - back] < 0 ? nullptr : &cont_history_[ply_move_[ply - back] * CONT_INDEX];
  }
  // Correction history: how far the static evaluation tends to be from the
  // search's result, learned per side to move in 1/CORR_GRAIN centipawns
  // (search.cpp) in tables whose entries add up: one keyed by the pawn
  // structure, one by each side's other pieces, and one by the last two
  // moves (piece * 64 + to of each; not before the root or after a null
  // move, when entry[3] is null).
  static constexpr int CORR_SIZE = 16384;
  std::int16_t corr_history_[COLOR_NB][CORR_SIZE] = {};
  std::int16_t corr_non_pawn_[COLOR_NB][COLOR_NB][CORR_SIZE] = {};  // [side to move][pieces' color]
  std::vector<std::int16_t> corr_cont_ = std::vector<std::int16_t>(CONT_INDEX * CONT_INDEX);  // [move 2 back][last move]
  struct CorrEntries {
    std::int16_t* entry[4];
    int total() const { return *entry[0] + *entry[1] + *entry[2] + (entry[3] ? *entry[3] : 0); }
  };
  CorrEntries corr_entries(const Position& pos, int ply) {
    const Color us = pos.side_to_move();
    std::int16_t* cont = nullptr;
    if (ply >= 2 && ply_move_[ply - 1] >= 0 && ply_move_[ply - 2] >= 0)
      cont = &corr_cont_[ply_move_[ply - 2] * CONT_INDEX + ply_move_[ply - 1]];
    return {{&corr_history_[us][pos.pawn_key() & (CORR_SIZE - 1)],
             &corr_non_pawn_[us][WHITE][pos.non_pawn_key(WHITE) & (CORR_SIZE - 1)],
             &corr_non_pawn_[us][BLACK][pos.non_pawn_key(BLACK) & (CORR_SIZE - 1)], cont}};
  }
  Move pv_table_[MAX_PLY][MAX_PLY] = {};
  int pv_length_[MAX_PLY] = {};

  std::uint64_t nodes_ = 0;
  std::uint64_t max_nodes_ = 0;
  std::int64_t hard_ms_ = -1;
  int root_depth_ = 0;  // the iteration in progress
  bool stopped_ = false;
  bool can_stop_ = false;  // depth 1 always finishes, so there is always a move
  const std::atomic<bool>* stop_flag_ = nullptr;
  std::chrono::steady_clock::time_point start_;
};

class Searcher {
 public:
  Searcher() : worker_(tt_) {}
  ~Searcher();

  // Starts thinking on a copy of pos. Prints "info" lines and "bestmove";
  // with "go infinite" it waits for stop() before printing bestmove.
  void start(const Position& pos, const SearchLimits& limits, int move_overhead_ms);
  void stop() { stop_ = true; }
  void wait();

  // Only while not searching.
  void set_hash_mb(std::size_t mb) { tt_.resize(mb); }
  void new_game();

 private:
  void run();

  TranspositionTable tt_;
  SearchWorker worker_;
  std::thread thread_;
  std::atomic<bool> stop_{false};
  Position root_;
  SearchLimits limits_;
  TimeBudget budget_;
};

}  // namespace chess
