#include "tt.h"

#include <algorithm>

namespace chess {

void TranspositionTable::resize(std::size_t mb) {
  const std::size_t bytes = std::max<std::size_t>(mb, 1) * 1024 * 1024;
  std::size_t buckets = 1;
  while (buckets * 2 * sizeof(Bucket) <= bytes) buckets *= 2;
  table_.assign(buckets, Bucket{});
  mask_ = buckets - 1;
}

void TranspositionTable::clear() {
  std::fill(table_.begin(), table_.end(), Bucket{});
  age_ = 0;
}

TTEntry& TranspositionTable::probe(Key key, bool& found) {
  Bucket& bucket = table_[key & mask_];
  const std::uint32_t k = check(key);
  for (TTEntry& e : bucket.entries) {
    if (e.bound != BOUND_NONE && e.key == k) {
      e.age = age_;
      found = true;
      return e;
    }
  }
  found = false;
  TTEntry* replace = &bucket.entries[0];
  for (TTEntry& e : bucket.entries) {
    if (e.bound == BOUND_NONE) return e;
    if (worth(e) < worth(*replace)) replace = &e;
  }
  return *replace;
}

}  // namespace chess
