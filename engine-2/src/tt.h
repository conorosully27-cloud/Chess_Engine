// Transposition table: remembers positions already searched, keyed by the
// Zobrist hash. Buckets of five entries, one 64-byte cache line each (one
// memory read per probe); a new position replaces the bucket's least useful
// entry (see probe).
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "types.h"

namespace chess {

enum Bound : std::uint8_t { BOUND_NONE, BOUND_EXACT, BOUND_LOWER, BOUND_UPPER };

// No static evaluation stored (the position was in check).
constexpr std::int16_t EVAL_NONE = INT16_MIN;

struct TTEntry {
  std::uint32_t key;  // high half of the hash, to verify a hit
  std::uint16_t move;
  std::int16_t score;
  std::int8_t depth;
  Bound bound : 2;
  std::uint8_t age : 6;  // the search (mod 64) that last wrote or found it
  std::int16_t eval;     // the position's static evaluation, or EVAL_NONE
};
static_assert(sizeof(TTEntry) == 12, "five entries fit a cache line");

class TranspositionTable {
 public:
  explicit TranspositionTable(std::size_t mb = 16) { resize(mb); }

  // Sizes the table to the largest power-of-two bucket count within mb megabytes.
  void resize(std::size_t mb);
  void clear();
  // Called once per search ("go"), so older entries count as stale.
  void new_search() { age_ = (age_ + 1) & AGE_MASK; }
  std::uint8_t age() const { return age_; }

  // The entry for this position (found = true; its age is refreshed), or
  // else the entry of its bucket to replace: an empty one, or the one worth
  // least.
  TTEntry& probe(Key key, bool& found);
  // Starts loading a position's bucket into the cache: called right after a
  // move, so the memory read overlaps with the work before the probe.
  void prefetch(Key key) const { __builtin_prefetch(&table_[key & mask_]); }
  static std::uint32_t check(Key key) { return std::uint32_t(key >> 32); }
  std::size_t size() const { return table_.size() * BUCKET_SIZE; }

 private:
  static constexpr int BUCKET_SIZE = 5;
  static constexpr std::uint8_t AGE_MASK = 63;
  struct alignas(64) Bucket {
    TTEntry entries[BUCKET_SIZE];
  };
  static_assert(sizeof(Bucket) == 64, "a bucket should fill one cache line");
  // An entry's depth, less 8 plies for each search since it was last used.
  int worth(const TTEntry& e) const { return e.depth - 8 * ((age_ - e.age) & AGE_MASK); }

  std::vector<Bucket> table_;
  std::size_t mask_ = 0;
  std::uint8_t age_ = 0;
};

}  // namespace chess
