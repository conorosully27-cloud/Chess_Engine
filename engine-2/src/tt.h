// Transposition table: remembers positions already searched, keyed by the
// Zobrist hash. Entries come in buckets of five, one 64-byte cache line
// each: a position may sit in any slot of its bucket, and a new one replaces
// the least useful (empty, then shallow and from older searches).
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
  std::uint8_t gen : 6;  // the search that last wrote it (TranspositionTable::generation)
  std::int16_t eval;     // the position's static evaluation, or EVAL_NONE
};
static_assert(sizeof(TTEntry) == 12, "bound and generation share a byte");

struct alignas(64) TTBucket {
  static constexpr int SIZE = 5;
  TTEntry entries[SIZE];
};
static_assert(sizeof(TTBucket) == 64, "one bucket per cache line");

class TranspositionTable {
 public:
  explicit TranspositionTable(std::size_t mb = 16) { resize(mb); }

  // Sizes the table to as many buckets as fit in mb megabytes.
  void resize(std::size_t mb);
  void clear();
  // Called at the start of every search, so entries from earlier ones can
  // be told apart and replaced first.
  void new_search() { gen_ = (gen_ + 1) & 63; }
  std::uint8_t generation() const { return gen_; }

  // The position's entry if it is stored (its key matches; it is then
  // marked as this search's), otherwise the slot a new entry for it should
  // replace. Callers write gen when they store into it.
  TTEntry& entry(Key key) {
    TTBucket& b = bucket(key);
    const std::uint32_t k = check(key);
    TTEntry* worst = &b.entries[0];
    for (TTEntry& e : b.entries) {
      if (e.key == k && e.bound != BOUND_NONE) {
        e.gen = gen_;  // still in use: not from an old search
        return e;
      }
      if (value(e) < value(*worst)) worst = &e;
    }
    return *worst;
  }
  // Starts loading a position's bucket into the cache: called right after a
  // move, so the memory read overlaps with the work before the probe.
  void prefetch(Key key) const { __builtin_prefetch(&bucket(key)); }
  static std::uint32_t check(Key key) { return std::uint32_t(key >> 32); }
  std::size_t size() const { return table_.size() * TTBucket::SIZE; }

 private:
  // The bucket from the hash's low half (the high half verifies hits), over
  // any number of buckets: a multiply instead of a power-of-two mask.
  TTBucket& bucket(Key key) { return table_[(std::uint64_t(std::uint32_t(key)) * table_.size()) >> 32]; }
  const TTBucket& bucket(Key key) const {
    return table_[(std::uint64_t(std::uint32_t(key)) * table_.size()) >> 32];
  }
  // How much an entry is worth keeping: empty slots go first, then the
  // shallowest, with each search of age costing as much as 8 plies.
  int value(const TTEntry& e) const {
    if (e.bound == BOUND_NONE) return -1000;
    return e.depth - 8 * ((gen_ - e.gen) & 63);
  }

  std::vector<TTBucket> table_;
  std::uint8_t gen_ = 0;
};

}  // namespace chess
