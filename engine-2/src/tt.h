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

struct TTEntry {
  std::uint32_t key;  // high half of the hash, to verify a hit
  std::uint16_t move;
  std::int16_t score;
  std::int8_t depth;
  Bound bound;
  std::uint8_t age;  // the search that last wrote or found it
};

class TranspositionTable {
 public:
  explicit TranspositionTable(std::size_t mb = 16) { resize(mb); }

  // Sizes the table to the largest power-of-two entry count within mb megabytes.
  void resize(std::size_t mb);
  void clear();
  // Call at the start of each search, so older entries count as stale.
  void new_search() { age_++; }
  std::uint8_t age() const { return age_; }

  // The entry for this position (found = true), or else the entry of its
  // bucket to replace: an empty one, or the one worth least.
  TTEntry& probe(Key key, bool& found);
  static std::uint32_t check(Key key) { return std::uint32_t(key >> 32); }
  std::size_t size() const { return table_.size() * BUCKET_SIZE; }

 private:
  static constexpr int BUCKET_SIZE = 5;
  struct alignas(64) Bucket {
    TTEntry entries[BUCKET_SIZE];
  };
  static_assert(sizeof(Bucket) == 64, "a bucket should fill one cache line");
  // An entry's depth, less 8 plies for each search since it was last used.
  int worth(const TTEntry& e) const { return e.depth - 8 * std::uint8_t(age_ - e.age); }

  std::vector<Bucket> table_;
  std::size_t mask_ = 0;
  std::uint8_t age_ = 0;
};

}  // namespace chess
