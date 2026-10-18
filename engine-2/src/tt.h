// Transposition table: remembers positions already searched, keyed by the
// Zobrist hash. One entry per slot, as in Engine 1.6.
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
};

class TranspositionTable {
 public:
  explicit TranspositionTable(std::size_t mb = 16) { resize(mb); }

  // Sizes the table to the largest power-of-two entry count within mb megabytes.
  void resize(std::size_t mb);
  void clear();

  TTEntry& entry(Key key) { return table_[key & mask_]; }
  static std::uint32_t check(Key key) { return std::uint32_t(key >> 32); }
  std::size_t size() const { return table_.size(); }

 private:
  std::vector<TTEntry> table_;
  std::size_t mask_ = 0;
};

}  // namespace chess
