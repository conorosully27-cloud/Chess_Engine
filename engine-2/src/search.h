// The search, run on its own thread so the UCI loop can keep answering
// ("isready", "stop") while it thinks. For now it only picks a legal move;
// the real alpha-beta search arrives in step 3 behind the same interface.
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>

#include "position.h"

namespace chess {

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
};

TimeBudget allocate_time(const SearchLimits& limits, Color us, int move_overhead_ms);

class Searcher {
 public:
  ~Searcher();

  // Starts thinking on a copy of pos. Prints "bestmove" when done; with
  // "go infinite" it waits for stop() before doing so, as UCI requires.
  void start(const Position& pos, const SearchLimits& limits, int move_overhead_ms);
  void stop() { stop_ = true; }
  void wait();
  bool searching() const { return thread_.joinable(); }

 private:
  void run();

  std::thread thread_;
  std::atomic<bool> stop_{false};
  Position root_;
  SearchLimits limits_;
  TimeBudget budget_;
};

}  // namespace chess
