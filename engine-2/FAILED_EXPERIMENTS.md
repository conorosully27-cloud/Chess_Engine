# Engine 2 failed experiments

Every change that was tried and not kept: what it was, the commit (so the
code can be recovered), the results, how promising it looked beforehand
(out of 10), and why it failed. Newest at the top. Engine 1's are in
`../engine-1/FAILED_EXPERIMENTS.md`.

## 2.4 bundle: TT (quiescence + buckets) + continuation history (Oct 2026) — parked until after NNUE

**Change:** on top of 2.3, the transposition-table change (quiescence
probes and stores; 5-entry, 64-byte buckets with age-based replacement)
and continuation history (see the entries below). Commit `0218a0a` (taken
out again); bench 3974756 (2.3: 3601124).

**Result:** vs 2.3 at 40+0.4 on the owner's laptop (10 games at a time,
adjudication on): 638 games (the laptop shut down overnight), +189 -182
=267, Elo +4 +/- 19, LOS 65%, LLR -0.13. Separately: the TT change was -3
+/- 15 at 8+0.08 (1,204 games) and +8 +/- 14 at 40+0.4 (1,076 games);
continuation history -7 +/- 17 at 8+0.08 (880 games).

**Prior:** 6/10.

**Commentary:** at best a few Elo, even at the longer time control.
Parked by the owner until after NNUE: refinements to the search often
change in value when the evaluation changes, so retry both on the NNUE
build.

## Continuation history, at 8+0.08 (Oct 2026) — parked for a longer TC

**Change:** a 768 x 768 table (2.4 MB) of how well a quiet move did after
the move one and two plies earlier, added to the history score in move
ordering; updated like history (+depth^2 on a quiet cutoff, -depth^2 for
the quiets tried before it, quartered before each search). Commit
`924f494` (taken out again). On the best build: bench 3107651 (14% fewer
nodes than 3601124), ~10% fewer nodes/s, bench 4% faster.

**Result:** vs 2.2 + IIR + mild LMR (cloud, 4 games at a time,
adjudication on): stopped by the owner at 880 games, +254 -272 =354, Elo
-7 +/- 17, LOS 21%, LLR -1.56.

**Prior:** 7/10.

**Commentary:** not rejected outright: its gain should grow with depth
(better ordering matters more in a deeper tree) while its speed cost
(cache misses on a 2.4 MB table) stays the same, and the same pattern
showed in the TT change (-3 at 8+0.08, +10 so far at 40+0.4). Retest at
20+0.2 or 40+0.4. A smaller table (int16_t, 1.2 MB) would also cut the
speed cost.

## Stronger null move v3: 1.6's reduction + an eval term (Oct 2026)

**Change:** null-move reduction `(depth >= 7 ? 3 : 2) + min((static_eval -
beta) / 200, 3)`: 1.6's reduction plus one ply per 200 cp above beta, at
most 3. Commit `d39d85a`, taken out again after the test.

**Result:**
- On 2.2 + IIR: paused by the owner at 40 games, +16 -13 =11, +26 +/- 71
  (bench 5485422, 3% more nodes).
- On 2.2 + IIR + mild LMR, vs that build (cloud, 4 games at a time,
  adjudication on): stopped at 980 games, +284 -285 =411, Elo -0 +/- 15,
  LOS 48%, LLR -0.88 (bench 3251253, 10% fewer nodes). No time losses.

**Prior:** 6/10.

**Commentary:** no gain in three tries at a stronger null move (this one,
3 + depth/3, 3 + depth/4 with a 1-ply floor). 1.6's plain 2-3 ply
reduction seems to suit this engine; the usual larger reductions assume a
search that makes up for an optimistic cut elsewhere (verification, better
TT, more accurate eval). Worth a retry after the transposition-table work,
not before.

## Stronger null move: 3 + depth/4, null search at least 1 ply (Oct 2026)

**Change:** null-move reduction 3 + depth/4 plus one per 200 cp above beta
(at most 3), with the null search never shallower than 1 ply
(`negamax(std::max(depth - 1 - reduction, 1), ...)`). On 2.2 + IIR, not
committed. Bench 4090339 (23% fewer nodes than 2.2 + IIR).

**Result:** vs 2.2 + IIR (cloud, 1 game at a time, adjudication on),
stopped at 40 games: +6 -14 =20, Elo -70 +/- 64, LOS 1%, 2 pairs won and
8 lost.

**Prior:** 6/10.

**Commentary:** the floor bites in the wrong place: from depth 4 to 7 the
reduction is 4 or more, so the check is clamped to a 1-ply search where
the old engine looked 1 to 4 plies deep. Too shallow to see the
opponent's threats, so it cuts far too often (hence the big node drop)
and misses tactics. The first version erred the other way (into
quiescence, too few cutoffs). Next: keep 1.6's reduction and add only the
"far ahead" term.

## Stronger null move: 3 + depth/3 (Oct 2026)

**Change:** null-move reduction 3 + depth/3 plus one per 200 cp the static
eval is above beta (at most 3), instead of 2 (3 from depth 7). Built on
2.2 + IIR, not committed (one line in `negamax`; see HANDOFF item 5).
Bench 6119429 vs 5320123: *more* nodes.

**Result:** vs 2.2 + IIR (cloud, 1 game at a time, adjudication on),
stopped by the owner at 260 games: +71 -79 =110, Elo -11 +/- 31, LOS 25%,
LLR -0.65.

**Prior:** 7/10.

**Commentary:** the bigger reduction sent most null-move checks straight
into quiescence (depth - 1 - R <= 0 from depth 3 up to about 6), where
the side that passed meets the opponent's captures at once and the check
fails far more often, so fewer cutoffs and more nodes. Next: 3 + depth/4
with the null search at least 1 ply deep; that version has 23% *fewer*
nodes than 2.2 + IIR (bench 4090339).

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
