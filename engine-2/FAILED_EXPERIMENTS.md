# Engine 2 failed experiments

Every change that was tried and not kept: what it was, the commit (so the
code can be recovered), the results, how promising it looked beforehand
(out of 10), and why it failed. Newest at the top. Engine 1's are in
`../engine-1/FAILED_EXPERIMENTS.md`.

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
