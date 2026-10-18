#include "search.h"

#include <algorithm>
#include <chrono>
#include <random>

#include "movegen.h"
#include "uci.h"

namespace chess {

TimeBudget allocate_time(const SearchLimits& limits, Color us, int move_overhead_ms) {
  TimeBudget b;
  if (limits.infinite) return b;
  if (limits.movetime >= 0) {
    b.hard_ms = b.soft_ms = std::max<std::int64_t>(1, limits.movetime - move_overhead_ms);
    return b;
  }
  const int time = us == WHITE ? limits.wtime : limits.btime;
  if (time < 0) return b;  // no clock given: depth or node limited, or unlimited
  const int inc = us == WHITE ? limits.winc : limits.binc;
  const std::int64_t remaining = std::max<std::int64_t>(1, time - move_overhead_ms);
  const int moves_to_go = limits.movestogo > 0 ? std::min(limits.movestogo, 40) : 40;
  // Never plan to use more than three quarters of what is left on the clock.
  const std::int64_t cap = std::max<std::int64_t>(1, remaining * 3 / 4);
  b.soft_ms = std::min(cap, remaining / moves_to_go + inc * 3 / 4);
  b.hard_ms = std::min(cap, b.soft_ms * 4);
  b.soft_ms = std::max<std::int64_t>(1, b.soft_ms);
  b.hard_ms = std::max(b.hard_ms, b.soft_ms);
  return b;
}

Searcher::~Searcher() {
  stop();
  wait();
}

void Searcher::start(const Position& pos, const SearchLimits& limits, int move_overhead_ms) {
  stop();
  wait();
  root_ = pos;
  limits_ = limits;
  budget_ = allocate_time(limits, pos.side_to_move(), move_overhead_ms);
  stop_ = false;
  thread_ = std::thread(&Searcher::run, this);
}

void Searcher::wait() {
  if (thread_.joinable()) thread_.join();
}

void Searcher::run() {
  MoveList moves;
  generate_legal(root_, moves);

  // Placeholder until the real search: a random legal move, seeded by the
  // position so a given position always gets the same reply.
  Move best;
  if (moves.size() > 0) {
    std::mt19937_64 rng(root_.key());
    best = moves[int(rng() % moves.size())];
  }
  uci_send("info depth 1 score cp 0 nodes " + std::to_string(moves.size()) + " pv " + best.uci());

  // In infinite mode "bestmove" must wait for "stop".
  while (limits_.infinite && !stop_) std::this_thread::sleep_for(std::chrono::milliseconds(1));

  uci_send("bestmove " + best.uci());
}

}  // namespace chess
