# Engine 2 changelog

Every accepted change with its measured effect, newest first. Matches use
`scripts/match.sh` (fastchess, openings from `engine-1/test/openings.epd`,
each played with both colors). Elo figures are fastchess's estimate with its
95% error margin.

## Next release (accepted, not yet released)

- **Internal iterative reduction**: at depth 4 or more with no move from
  the transposition table (not at the root), search one ply shallower:
  without a remembered best move the ordering is poor. SPRT against 2.2
  (cloud, 8+0.08, 2 games at a time): **H1 accepted after 776 games**,
  267 wins, 204 losses, 305 draws (54.1%), **+28 Elo (+/- 17)**, LOS
  99.9%. Bench 5320123 (2.2: 6533990). The match was resumed once from
  its saved state after a container restart.
- **Logarithmic late move reductions** (mild version): late quiet moves
  reduced by 1 + ln(depth) ln(move number) / 2 from the third move, one
  ply less on the main line and for killers, never at the root, at most
  depth - 2 (was 1 ply, 2 after the tenth move). Against 2.2 + IIR (cloud,
  8+0.08, 3 games at a time): 2,180 games, 677 wins, 620 losses, 883 draws
  (51.3%), **+9 Elo (+/- 10)**, LOS 96%, LLR 1.54. **Accepted by the
  owner** on that evidence without a formal H1 (the SPRT was still between
  its bounds: a gain of this size takes thousands more games to settle).
  Bench 3601124 (2.2 + IIR: 5320123). The steeper version (divisor 1.75,
  an extra ply for zero-history moves) added nothing on top of IIR; see
  `FAILED_EXPERIMENTS.md`.

## 2.2: static exchange evaluation (SEE)

**Release match against 2.1** (owner's laptop, fastchess, 8+0.08, SPRT
0/10, 12 games at a time): **H1 accepted after 1,366 games**: 461 wins,
385 losses, 520 draws (52.8%), **+19 Elo (+/- 14)**, LOS 99.8%. New bench
signature **6533990** (2.1: 9557353): 32% fewer nodes for the same depths.

- `Position::see_ge(move, threshold)`: does the move, followed by the best
  recaptures on its square (each side taking with its cheapest piece and
  free to stop), win at least `threshold`? Swap algorithm with x-rays, en
  passant, promotion by the moving pawn, kings taking only when nothing can
  take back; pins ignored; values 100/325/325/500/900. Checked by
  `tests/see_test.cpp` against hand-worked exchanges and a slow recursive
  reference (84M comparisons on random playouts).
- Quiescence skips captures that lose material by SEE, replacing 1.6's
  pawn-guard rule. Flat on its own: -1 +/- 17 over 800 games vs 2.1.
- Captures that lose material by SEE are tried after the quiet moves
  instead of before the killers. +11 +/- 11 over 2,118 games vs the
  previous step (LLR 1.79, stopped; laptop).
- SEE pruning in the main search (non-PV nodes, depth <= 6, not in check,
  not checking moves, after the first move): skip quiet moves losing more
  than 25*depth^2 and captures losing more than 100*depth. +18 +/- 21 over
  540 games vs the previous step (LLR 1.18, stopped; cloud).

## 2.1: speed

Search and evaluation unchanged; the same work done faster. **About 28%
more nodes per second than 2.0** (median of 8 alternating `bench` runs:
2.02M -> 2.59M nodes/s on the cloud machine), 30% fewer instructions per
node (callgrind, bench depth 8).

**Release match against 2.0** (`scripts/release_match.sh`, 8+0.08, SPRT
0/10): **H1 accepted after 430 games**, 158 wins, 94 losses, 178 draws,
+52 Elo (+/- 24). Extended to 1,000 games for a firmer estimate: 354 wins,
249 losses, 397 draws (55.3%), **+37 Elo (+/- 16)**, no time losses. The
early-stopped SPRT overstated the gain, as expected.

- Material and piece-square sums kept up to date in `Position` instead of
  recounted every evaluation (as Engine 1.4 did in JS): 12% fewer
  instructions in the search. Bench unchanged.
- Pawn hash table for the pawn-structure terms, and the evaluation's
  arithmetic made exact (integers over a common denominator instead of
  floating point): about 8% fewer instructions per node. Engine 1.6 summed
  king danger's fractions in floating point, which sometimes landed a hair
  below a whole-number score and truncated 1 cp low; 0.13% of the
  positions the bench evaluates were affected (each one checked to be an
  exact integer). Scores still match Engine 1.6 on all 30,000 test
  positions. **New bench signature 9557353** (was 9559579).

- Move picking: the top ordering score is found with a loop the compiler
  vectorizes, then the first move with it (same move as before); move
  lists are no longer zeroed when created. About 6% fewer instructions.
- Futility and late move pruning are decided before making the move, with
  a new `Position::gives_check` instead of making the move to look: about
  8% fewer instructions, and the biggest wall-time gain. Tested against
  making the move on 5.3M random-playout moves.

Tried and dropped (no change in search, so no SPRT needed):
- Larger evaluation cache (2^18, 2^20 entries): slower in wall time, and
  it changes the search, because lazy evaluation uses a cached full score
  when there is one.
- BMI1/BMI2 instructions and link-time optimization: within noise.
- Trying the hash move before generating the rest: most move generation
  happens in quiescence, so at most ~4% to gain; not done.

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
