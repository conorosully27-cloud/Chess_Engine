# Engine 2 failed experiments

Every change that was tried and not kept: what it was, the commit (so the
code can be recovered), the results, how promising it looked beforehand
(out of 10), and why it failed. Newest at the top. Engine 1's are in
`../engine-1/FAILED_EXPERIMENTS.md`.

## Continuation correction history at the non-pawn settings, on 4.0 (Oct 2026)

**Change:** a fourth correction table keyed by the last two moves (piece *
64 + to of each, 768 x 768 int16), summed with the pawn and non-pawn
tables and updated with them at the non-pawn rate (27%) and the shared
50 cp limit; quiescence records its moves for the key. Commit `d8657e6`.
Bench 3027103 -> 3008416 (exactly 4.0's with the table off).

**Result:** vs 4.0 (cloud, 8+0.08, 4 at a time), stopped by the owner at
180 games: +38 -54 =88 (45.6%), Elo -31 +/- 32, LOS 2.7%, LLR -1.37.
State in `release-matches/contcorr/`.

**Prior:** 6/10.

**Commentary:** the table has ~590,000 entries against 16K per pawn or
non-pawn table, so each is updated a few times a game and stays noisy,
and through the shared error its noise also disturbs the other tables'
learning. Retried cautiously: 10% learning rate and its own 16 cp limit.

## Non-pawn correction history at full learning rate, on 3.4 (Oct 2026)

**Change:** two more correction tables beside 3.4's pawn table, keyed by
each side's non-pawn pieces (king included; a new incremental key in
`Position`), the three summed; each update moved all three by
weight/256 of the error they left together. Commit `4464ca7`. Bench
2944258 -> 2710415 (8% fewer nodes; with the new tables off, exactly
3.4's 2944258).

**Result:** vs 3.4 (cloud, 8+0.08, 4 at a time), stopped by the owner at
380 games: +91 -103 =186 (48.4%), Elo -11 +/- 21, LLR -1.40. State in
`release-matches/npcorr/`.

**Prior:** 7/10 (a standard gain in strong engines).

**Commentary:** most likely the learning rate: three tables each moving
the full step toward the same error make the total correction react about
three times as fast as 3.4's single table, so it is noisier. Retried with
the new tables learning 4x more slowly.

## Endgame knowledge: scaling known draws + fifty-move scaling, on 3.1 (Oct 2026)

**Change:** `src/endgame.cpp` scaled the network's score (out of 64) for
the side it favours: no pawns and at most a minor piece up (0 below a
rook's worth, else 4 or 14, Stockfish's rule); two knights v bare king 0;
opposite-coloured bishops with only pawns 16 (one pawn up) or 32; rook
pawns with no bishop or the wrong one, bare defending king next to the
queening square, 0. The score also shrank with the fifty-move counter
(half at 100 plies). Commit `0973551` on `claude/loving-ramanujan-lewlp6`
(with `tests/endgame_test.cpp`). Bench 2851725 -> 3022709.

**Effect on test positions (3 s each):** wrong bishop + h-pawn +6.33 ->
0.00; two knights v king +6.75 -> 0.00; rook v bishop +2.10 -> +0.11;
opposite bishops a pawn up +0.73 -> +0.16; wins (Lucena, breakthrough,
queen v rook) unchanged. The Philidor draw stayed -1.94.

**Result:** three SPRTs against 3.1 (8+0.08, 0/10, cloud):
- Normal openings: **H0 after 3,852 games**, +1049 -1038 =1765, Elo +1.0
  +/- 7.2, LLR -2.97.
- Level endgames (`scripts/endgame_book.py`, at most 3 pieces besides
  kings and pawns, |score| <= 200): H0 after 206 games, +12 -13 =181:
  97% draws, too drawn to tell the two apart.
- Endgames with a 1-4 pawn edge (`--min-score 100 --max-score 400`): H0
  after 426 games, +90 -92 =244, about -2 Elo.

**Prior:** 5/10 for Elo (fixes visible misjudgements, but in rare
endgames).

**Commentary:** neutral, not harmful: the rules fire in material
balances that rarely arise, and when they do the search mostly plays
them out correctly anyway (it held the draws; only the displayed score
was wrong). Worth it only for honest evaluations in the app's analysis
board, which the owner may choose to keep despite no Elo. The fix for
impossible FENs (side not to move in check, a pawn on the back rank),
found while testing, was kept: it doesn't change play.

## "Improving" flag in late move reductions, on 2.10 (Oct 2026)

**Change:** the same flag as the entry below, used only in late move
reductions: one ply more when not improving. Commit `f13b6a1` on
`claude/loving-ramanujan-lewlp6`. Bench 1962997 -> 1717843 (12% fewer
nodes).

**Result:** SPRT against 2.10 (8+0.08, 0/10, 4 at a time, cloud), stopped
by the owner at 240 games: +53 -65 =122, Elo -17 +/- 28, LLR -1.11.

**Prior:** 6/10.

**Commentary:** the second "improving" failure, and like the first it
searches less. Both suggest the pruning and reduction margins, set for
the hand-written evaluation, are already too tight with NNUE; next is
tuning them all (SPSA) rather than adding more pruning.

## "Improving" flag in the pruning rules, on 2.10 (Oct 2026)

**Change:** improving = the static evaluation is better than two plies
ago (false if that node was in check). Reverse futility margin
`90 * (depth - improving)`; futility margin 50 smaller when not
improving; late move pruning keeps half as many quiet moves (2/3/6
instead of 4/7/12) when not improving. Commit `d3dc1f5` on
`claude/loving-ramanujan-lewlp6`. Bench 1962997 -> 1276855 (35% fewer
nodes).

**Result:** SPRT against 2.10 (8+0.08, 0/10, 4 games at a time, cloud):
H0 accepted after 1,038 games, +271 -306 =461, Elo -11.7 +/- 14.6,
LLR -2.99.

**Prior:** 7/10 (a standard gain in most engines).

**Commentary:** too aggressive rather than a bug: a 35% drop in nodes
throws good moves away with the bad. The likeliest culprits are the zero
reverse-futility margin at depth 1 when improving (any eval at or above
beta returns) and halving the late move pruning counts in about half of
all nodes. Next: the flag in late move reductions instead (one ply more
when not improving), then a gentler pruning version.
## Stricter repetition draws, on 2.10 (Oct 2026)

**Change:** Stockfish's repetition rule in the search: a position is a
draw if it occurred once before strictly inside the search, or twice
before in all (2.10: once before anywhere, including the game before the
root); and the look-back stops at the last null move
(`StateInfo::plies_from_null`). Commit `c942d0a`, with tests; bench
1963115 (2.10: 1962997).

**Result:** vs 2.10 at 8+0.08 (cloud, 4 games at a time, no
adjudication): stopped at 1,120 games, +296 -307 =517, Elo -3 +/- 13,
LLR -1.99. Taken out by the owner as neutral. State in
`release-matches/rep-2.10/`.

**Prior:** 5/10.

**Commentary:** about a third of the games (376 of 1,133) ended by
threefold repetition, so the rule was in play constantly, and it made no
difference: the old rule's misjudgments rarely decided a real game. The
null-move part is a correctness fix with no measurable effect.

## TT buckets alone, on 2.10 (Oct 2026) — parked for a long time control

**Change:** 5-entry, 64-byte buckets with age-based replacement (the
bundle below without its quiescence half); the table now fills the Hash
setting (64 MB: 5.2M entries, 2.10's: 4M in 48 MB). Commit `0588eff`;
bench 2006389 (2.10: 1962997), same speed.

**Result:** vs 2.10 at 20+0.2 (cloud, 4 games at a time, no
adjudication): stopped by the owner at 440 games, +110 -111 =219, Elo -1
+/- 20, LLR -0.54. It started at -17 +/- 32 (200 games), noise. State in
`release-matches/buckets-2.10/`.

**Prior:** 6/10 ("a free gain").

**Commentary:** neutral, not harmful. Replaying a 120-ply game at depth
14 with the table kept between moves: at 64 MB buckets and 2.10 had the
same hits, cutoffs, IIR reductions and nodes within 0.3%, since at
these speeds even the one-entry table rarely runs out of room. Under
pressure (8 MB) buckets kept 11% more hits but needed 7% more nodes, and
2.10 itself needs fewer nodes with 8 MB than with 64 MB: with this search
a table that remembers more is not automatically cheaper. Worth a long
overnight test on the laptop (40+0.4 or longer), where the table fills.

## TT buckets + quiescence probes, retried on 2.10 (Oct 2026)

**Change:** the parked transposition-table change (below) ported onto
2.10: 5-entry, 64-byte buckets with age-based replacement, and
quiescence probing the table and storing at depth 0. Commit `6d64ef6`;
bench 1843187 (2.10: 1962997). At bench 13: 4% more nodes than 2.10 and
about 8% fewer nodes/s, all from the quiescence half (buckets alone: 3%
fewer nodes at 2.10's speed).

**Result:** vs 2.10 at 20+0.2 (cloud, 4 games at a time, no
adjudication): stopped by the owner at 440 games, +107 -109 =224, Elo -2
+/- 19, LLR -0.72. It stood at +9 at 280 games before evening out. State
in `release-matches/ttb-2.10/`.

**Prior:** 5/10 (flat at 8+0.08 and +8 at 40+0.4 on 2.3).

**Commentary:** flat a third time. The quiescence half's probes cost
about as much speed as the table gains; next, buckets alone.

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
