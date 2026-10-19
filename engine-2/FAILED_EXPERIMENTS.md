# Engine 2 failed experiments

Every change that was tried and not kept: what it was, the commit (so the
code can be recovered), the results, how promising it looked beforehand
(out of 10), and why it failed. Newest at the top. Engine 1's are in
`../engine-1/FAILED_EXPERIMENTS.md`.

## Logarithmic LMR, steep (1.6's parked version), on top of IIR (Oct 2026)

**Change:** late quiet moves reduced by 1 + ln(depth) ln(move) / 1.75 from
the third move, one ply less on the PV and for killers, one more for moves
with zero history, never at the root, at most depth - 2. Engine 1.6's
parked version. Commit `220bfc8` (on 2.2 + IIR); bench 2728215 vs
5320123.

**Result:**
- On 2.2 alone, vs 2.2 (cloud, 2 games at a time): 2,760 games, +888
  -818 =1054, Elo +9 +/- 9, LLR 1.66, stopped as slow to decide.
- On 2.2 + IIR, vs 2.2 + IIR (cloud, 3 at a time): 680 games, +194 -202
  =284, Elo -4 +/- 19, LLR -0.99, stopped by the owner as flat.

**Prior:** 6/10.

**Commentary:** a small gain on its own, gone on top of IIR: both make the
search shallower in the same places (moves ordered late when the position
is new or poorly ordered). The "one more ply for zero history" rule bites
far more than intended: history is cleared each game and divided by 4
before every search, so most quiet moves have zero history most of the
time. Next: a milder version (no zero-history rule, divisor 2.0).

## SEE step 2: SEE pruning in quiescence (Oct 2026)

**Change:** quiescence skips captures with `!see_ge(move, 0)` instead of
1.6's rule (skip a capture by a more valuable piece when an enemy pawn
guards the square). Commit `9e12201` on `claude/loving-ramanujan-lewlp6`.
Bench 9557353 -> 9022406 (5.6% fewer nodes), speed per node unchanged.

**Result:** SPRT against 2.1 (8+0.08, 0/10), stopped by the owner at 800
games as clearly flat: +242 -244 =314, Elo -1 +/- 17, LLR -0.79. Kept as
the base of steps 3 and 4 since it costs nothing.

**Prior:** 7/10.

**Commentary:** the pawn-guard rule already skipped the most common losing
capture in quiescence (a piece taking something cheaper that a pawn
defends); SEE adds the rarer cases (guarded by a minor or a rook, x-rays),
too few to show at this time control.
