// Time allocation tests. The search itself gets its own tests in step 3.
#include <cstdio>

#include "search.h"
#include "test_util.h"

using namespace chess;

int main() {
  SearchLimits l;
  TimeBudget b = allocate_time(l, WHITE, 50);
  CHECK(b.soft_ms == -1 && b.hard_ms == -1);  // no limits: think until told to stop

  l.movetime = 1000;
  b = allocate_time(l, WHITE, 50);
  CHECK_EQ(b.hard_ms, 950);
  CHECK_EQ(b.soft_ms, 950);

  l = SearchLimits();
  l.infinite = true;
  l.wtime = 1000;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.hard_ms == -1);

  // Clock games: a fraction of the remaining time, never most of it.
  l = SearchLimits();
  l.wtime = 60000;
  l.btime = 1000;
  l.winc = 1000;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.soft_ms > 1000 && b.soft_ms < 5000);
  CHECK(b.hard_ms >= b.soft_ms && b.hard_ms <= 60000 * 3 / 4);
  b = allocate_time(l, BLACK, 50);  // black's clock: 1 s, no increment
  CHECK(b.soft_ms >= 1 && b.hard_ms <= 1000);

  // Last move before the time control may use more, but keeps a reserve.
  l = SearchLimits();
  l.wtime = 10000;
  l.movestogo = 1;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.hard_ms <= 7500 && b.soft_ms > 5000);

  // Almost out of time: still a positive budget below the clock.
  l = SearchLimits();
  l.wtime = 30;
  b = allocate_time(l, WHITE, 50);
  CHECK(b.soft_ms >= 1 && b.hard_ms >= 1 && b.hard_ms <= 30);

  std::printf(g_failures ? "%d FAILED\n" : "all search tests pass\n", g_failures);
  return g_failures ? 1 : 0;
}
