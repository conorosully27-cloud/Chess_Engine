#include "tt.h"

#include <algorithm>

namespace chess {

void TranspositionTable::resize(std::size_t mb) {
  const std::size_t bytes = std::max<std::size_t>(mb, 1) * 1024 * 1024;
  table_.assign(std::max<std::size_t>(bytes / sizeof(TTBucket), 1), TTBucket{});
}

void TranspositionTable::clear() {
  std::fill(table_.begin(), table_.end(), TTBucket{});
  gen_ = 0;
}

}  // namespace chess
