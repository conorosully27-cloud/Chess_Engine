#include "tt.h"

#include <algorithm>

namespace chess {

void TranspositionTable::resize(std::size_t mb) {
  const std::size_t bytes = std::max<std::size_t>(mb, 1) * 1024 * 1024;
  std::size_t entries = 1;
  while (entries * 2 * sizeof(TTEntry) <= bytes) entries *= 2;
  table_.assign(entries, TTEntry{});
  mask_ = entries - 1;
}

void TranspositionTable::clear() { std::fill(table_.begin(), table_.end(), TTEntry{}); }

}  // namespace chess
