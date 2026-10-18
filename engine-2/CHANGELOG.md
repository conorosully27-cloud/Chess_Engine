# Engine 2 changelog

Every accepted change with its measured effect, newest first. Matches use
`scripts/match.sh` (fastchess, openings from `engine-1/test/openings.epd`,
each played with both colors). Elo figures are fastchess's estimate with its
95% error margin.

## 2.0: Engine 1.6 ported to C++

Same search and evaluation as Engine 1.6, term for term: the evaluation gives
identical scores on 30,000 test positions. The only difference is speed:
about 1.2M nodes/s on the cloud machine (1.8M on the owner's laptop) against
Engine 1.6's ~200k, so it searches 2-3 plies deeper in the same time.

**Against Engine 1.6**, 200 games at 8+0.08, concurrency 3:
163 wins, 5 losses, 32 draws (89.5%), **+372 Elo (+/- 59)**.
One of the losses was on time; see below.

Bench signature: 9559579 nodes (`engine2 bench`, depth 11), the same on
Linux/gcc and Windows/MinGW.

### Time allocation fix

The time loss above: at 8+0.08 the clock drained below 100 ms over the last
15 moves because each move could take up to four times its planned time, and
the planned time leaned on the increment. Now the plan is capped at a fifth of
the clock and the hard limit at three times the plan or half the clock. A
test plays out a 300-move game at 8+0.08 to check the clock can't run out.

Checked with 200 games at 8+0.08 against the build above, on an otherwise
idle machine: no time losses, 56 wins, 61 losses, 83 draws, -9 Elo (+/- 33),
so no measurable change in strength.
