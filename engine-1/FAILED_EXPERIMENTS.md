# Failed experiments

Engine changes that were tried and did not make the engine stronger, so the
same ground isn't covered twice. Each was tested against Engine 1.0 with
`npm run match -- --b ../engine-1.0 --sprt 0,10` (100 ms per move) unless
noted. "Prior" is how promising the idea looked before testing, out of 10.

Add new entries at the top.

## Lazy margin of 2 pawns and the bad-capture skip for any guard (Oct 2026)

**Change:** two of 1.4's settings, tried together on top of the history
penalty (which became Engine 1.6): lazy evaluation's margin 3 -> 2 pawns,
and the quiescence search skipping a capture by a piece worth more than
its victim whenever the enemy guards the square (`pos.isAttacked`), not
only when an enemy pawn does. Local branch `exp-16`, commit `587de88`.

**Result:** vs Engine 1.5, 2000 games, +715 -692 =593, Elo +4 (95% -9 to
+17), LLR -0.24, inconclusive; the history penalty alone had +8 (-5 to
+21). 12% fewer nodes than 1.5 to depth 9.

**Prior:** 5/10.

**Commentary:** fewer nodes but no gain, so the pruning cost accuracy.
The wider bad-capture rule is the likelier culprit: "the square is
guarded" ignores our own second attacker and the defender's value (QxR
guarded by a queen is fine), which a real exchange evaluation would see.
The 2-pawn margin is close to the 99th percentile of the non-material
evaluation (1.92 pawns) and may also bite. Each would need its own test
to tell them apart.

## Countermove heuristic (Oct 2026)

**Change:** for the opponent's last move (piece and square it landed on),
remember the quiet reply that last caused a cut-off, and try it right
after the killer moves. Built on Engine 1.5; never committed (needs a
`Position.lastMove()` and a 16 x 128 table in the engine).

**Result:** dropped before any match, on ordering statistics over 600
searches to depth 8 from 20 openings (tables kept between moves): quiet
cut-offs on the first move fell from 45.2% to 43.8%, and the search took
5% more nodes (33.1M -> 34.8M).

**Prior:** 5/10. A standard ordering gain in many engines, usually small.

**Commentary:** it ranks above history, so when its stored reply is stale
it pushes aside a better history move. The history penalty (failed quiet
moves lose history) did better on the same statistics and was tested
instead; a countermove could be retried below the history scores, or
only once history is better maintained.

## PARKED: steeper late move reductions, retry on top of 1.5 (Oct 2026)

**Status:** parked by the owner, not rejected: undecided at two time
controls, worth rechecking later (see the commentary).

**Change:** a retry of the logarithmic reductions below, made to bite at
the depths a fast search reaches. Built on Engine 1.5 (local branch
`exp-lmr2`, commit `e6bda98`, not pushed), in `src/engine/search.js`:

```js
// next to LMP_QUIETS:
const LMR = new Int8Array(64 * 64);
for (let d = 1; d < 64; d++) {
  for (let m = 1; m < 64; m++) LMR[d * 64 + m] = Math.floor(1 + (Math.log(d) * Math.log(m)) / 1.75);
}

// replacing the late move reduction block in negamax():
let reduction = 0;
if (depth >= 3 && legal > 2 && quiet && !inCheck && !givesCheck && ply > 0) {
  reduction = LMR[Math.min(depth, 63) * 64 + Math.min(legal, 63)];
  if (pvNode) reduction--; // less on the main line
  if (move === killer1 || move === killer2) reduction--; // less for moves that refuted others
  else if (this.history[piece * 128 + moveTo(move)] === 0) reduction++; // more for moves that never have
  reduction = Math.max(0, Math.min(reduction, depth - 2));
}
```

Not at the root: reducing there made the engine settle for a mate in 3
in the mate-in-2 test (it reduced the mating king move Kc7). Checked at
match speed before testing: at 100 ms a move about a quarter of
reductions are 3+ plies (1.5 never goes past 2), and the average depth
reached rises from 7.2 to 7.7.

**Results** (both vs Engine 1.5, stopped when the cloud session slept):
- 100 ms a move, 1 thread: 738 games, +239 -261 =238, Elo -10 (95% -31
  to +10), LLR -1.39.
- 300 ms a move, 3 threads: 765 games, +275 -247 =243, Elo +13 (-8 to
  +33), LLR 0.72.
(Earlier runs at 100/500/1000 ms died within 60 games in machine
restarts.)

**Prior:** 6/10.

**Commentary:** leaning the way you would expect if bigger reductions
only pay off with more thinking time: slightly worse at 100 ms, slightly
better at 300 ms, but neither is significant. The app thinks 2-20 s a
move, so a gain at longer time controls would matter there even if it
doesn't show at 100 ms. To recheck: run the 300 ms (or 1 s) test alone on
all threads, inside a session that stays awake (see HANDOFF, long matches).

## Logarithmic late move reductions (Oct 2026)

**Change:** late quiet moves were reduced by 1 ply (2 after the tenth
move), killers exempt. Replaced by the usual table, 0.75 + ln(depth) *
ln(move number) / 2.25, one ply less on the main line and for killers,
at most depth - 2. Commit `769d0b8`, reverted in `6dc0579`. Tested
against Engine 1.4.

**Result:** stopped by the owner at 772 games, +267 -263 =242, Elo +2
(95% -19 to +22), LLR -0.30.

**Prior:** 8/10. Logarithmic reductions are standard and halved the nodes
to depth 9 on the benchmark (3.16M -> 1.68M).

**Commentary:** at match speed it barely changed anything. The table only
departs from the old rule at higher depths and later moves; at 100 ms a
move the search reaches about depth 7, where it mostly gives the same
1 or 2 plies. Counted over 800 searches of 100 ms: the old rule made 2.65M
1-ply and 0.82M 2-ply reductions, the new one 2.43M, 0.99M and 0.15M
3-ply, so under a tenth of reductions differed. The benchmark's "half the
nodes" was measured at depth 9-11, which matches don't reach. Two
pointers for a retry: only about 1% of reduced moves fail high (turn out
better than expected), so move ordering is good enough for bigger
reductions, e.g. a smaller divisor or reducing from the third move; and
reductions matter more at longer thinking times, so 100 ms matches
under-measure them (the app thinks 2-20 s).

## Repetition fix: a twofold from before the root needs a threefold (Oct 2026)

**Change:** the search scored any position seen once before in the game as
a draw (`repetitionCount() >= 2` in `negamax`), so a repeat of a position
from before the search was scored 0 without searching the alternatives.
The fix (`Position.isSearchDraw(rootPly)`) counts a twofold as a draw only
when the earlier occurrence is inside the search; a repeat of an earlier
game position needs a true threefold. Commit `8aa90bc`, reverted in
`21ae938`.

**Results:**
- Alone vs Engine 1.2, from the varied openings: stopped by the owner at
  438 games, +133 -161 =144, Elo -22 (95% -49 to +4), LLR -1.48.
- On top of king safety vs 1.2: stopped at 1424 games, +570 -502 =352,
  Elo +17 (+1 to +32), LLR 1.82, while the same king safety without it
  reached H1 at +28 (+11 to +46) after 1151 games.

**Prior:** 7/10. It is how engines handle repetitions, and it decided a
game against the chess.com 2900 bot (24.Rf4 scored 0.00).

**Commentary:** correct but costs Elo in self-play, and the two runs agree
on roughly -10 to -20. Likely causes: treating every twofold as a draw is
a cheap cut-off that prunes whole subtrees, and it also acts like
contempt, steering the side that thinks it is better away from any
repetition, which in engine-vs-engine play keeps games going and wins
more than it loses. The fixed engine is happier to shuffle into
repeated positions. The bug is real in human games (the Rf4 game), so a
variant may be worth a retry: e.g. a draw at twofold only for the side
that isn't better, or a small contempt score instead of 0.

## Drawish-ending scaling and an exact KPK table, on top of 1.2's endgame terms (Oct 2026)

**Change ("candidate B"):** two pieces of endgame knowledge added to the
endgame terms that became Engine 1.2:
- `endgameScale()`: keep only part of the score in usually-drawn endings:
  opposite-colored bishops with only pawns (1/2), no pawns for the stronger
  side and at most a minor piece more (1/8), two knights against a bare king
  and the wrong rook pawn with the defending king in the corner (0).
- `kpk.js`: every king-and-pawn-v-king position solved by retrograde
  analysis at engine start (about 0.3 s); drawn ones score 0, won ones get
  a fixed bonus. Checked against textbook positions (opposition, key
  squares, the square, rook-pawn draws).
Weights retuned with both in the model (held-back error 0.0605 -> 0.0592).
Commits `6701735` and `c3ca513` (reverted on the main line), tuned version
`4cb5f71` on local branch `exp-endgame-draws`.

**Results:**
- Head to head vs the endgame-terms engine (A), normal openings: stopped at
  1479 games, +489 -484 =506, Elo +1 (95% -13 to +16), LLR -0.71.
- Same, 400 games from the endgame suite: +99 -107 =194, Elo -7 (-31 to
  +17).
- KPK table alone ("candidate C", A's weights, no scaling), endgame suite:
  +98 -97 =205, Elo +1 (-23 to +25).
- Full test vs Engine 1.1, run side by side with A's: stopped at 712 games
  when A was accepted, +280 -217 =215, Elo +31 (+10 to +52), LLR 2.20. A
  reached H1 first at +44, so B was dropped.

**Prior:** 6/10. Both are textbook endgame knowledge that strong engines
carry.

**Commentary:** correct but too rare to measure. The positions they change
come up in a small share of games, and when they do, the search often finds
the same draw or win by itself; the tuner's error dropped because these
endings are frequent among the labelled positions, but that didn't turn
into won games. The 1/8 rule for "no pawns, at most a minor up" is the
bluntest part and the likeliest to cost points (it also scales rook v rook
and similar). Worth reviving once matches can isolate endgames better, or
if the owner notices the engine misplaying these endings: the KPK table at
least is exact and can't misjudge a position.

## Development: undeveloped minors and early queen moves (Oct 2026)

**Change:** a middlegame penalty of 12 centipawns per knight or bishop
still on its starting square, plus 6 per undeveloped minor once the queen
has left d1/d8. Built on Engine 1.0. Commit `5ac00ca` on local branch
`exp-development` (not pushed).

**Result:** stopped at 320 games: +104 -111 =105, Elo -8 (95% -39 to +24),
LLR -0.50. Led at +29 Elo after 120 games, then went 59-76-65.

**Prior:** 6/10. Slow development is a standard weakness and the handover
listed it as step 2.

**Commentary:** the same double counting as the king terms. The
piece-square tables already score a knight on b1 at -40 against +10 on c3,
and a bishop on c1 at -10, so development was already being rewarded; the
new term mostly made the engine rush pieces out at the expense of better
moves. The early-queen part is also crude: it penalises useful queen moves
(Qe2 to support e4, recapturing with the queen) the same as Qh5 sorties.
Hand-picked weights for new terms keep landing on top of what the tables
already do; the next step is Texel tuning, which fits all the weights
together, and these terms could be retried there with tuned weights.

## Castled-only shelter plus a smaller central-king penalty of 5/20 (Oct 2026)

**Change:** the 15/40 version below with the penalty cut to 5 centipawns
(can still castle) and 20 (can't). Commit `60f8d12` on local branch
`exp-central-king-small` (not pushed).

**Result:** stopped at 622 games: +210 -218 =194, Elo -4 (95% -27 to +18),
LLR -0.71 and falling. It led by about +20 Elo for the first 250 games,
then faded to even and below.

**Prior:** 5/10. Smaller weights were the obvious fix if the 15/40 version
was double counting with the king table, but a smaller version of a
harmful term is at best neutral.

**Commentary:** shrinking the penalty took it from -10 Elo to roughly 0,
which fits the double-counting theory: the king table already does this
job, and the extra term only adds noise. Together with the shelter-only
result, this suggests "which file is the king on" is not where Engine 1.0
loses strength. Real king danger (attackers near the king, open files
toward it, a pawn storm) would be a different idea, not a retune of this
one. The early lead is a reminder not to read anything into the first few
hundred games.

## Shelter only for a castled king (Oct 2026)

**Change:** king shelter (`kingShelter` in `src/engine/evaluate.js`) scored
only for a king on the a-c or g-h files, so pushing the d-, e- or f-pawn in
front of a king on e1/e8 is no longer penalised. Commit `c18debf` on branch
`ccr-d45a8a60-8e88me`.

**Result:** stopped at 531 games, exactly even: +189 -189 =153, Elo 0 (95%
-25 to +25), LLR -0.31. No decision, but no sign of a gain; stopped to free
cores for the development test.

**Prior:** 6/10. Penalising 1.e4 for "weakening the king" is plainly wrong,
and the handover listed it first.

**Commentary:** the change is real but tiny in practice. It is a
middlegame-only term of at most 24 centipawns, and the piece-square tables
already reward central pawn advances by more than that, so the engine was
playing e4/d4 anyway; the term rarely changed a move. It also probably isn't
what made Engine 1.0 misjudge the gambit game by 1.5 pawns. One side effect
to keep in mind: a king on f1 (after Kf1) now gets no shelter score at all.
Worth keeping only as a correctness fix if a later change builds on it; on
its own it is Elo-neutral.

## Castled-only shelter plus a central-king penalty of 15/40 (Oct 2026)

**Change:** as above, plus a middlegame penalty for a king on the d, e or f
file while the enemy queen is on: 15 centipawns if it can still castle, 40
once it can't. Commit `36b437a`.

**Result:** H0 accepted after 1621 games: +535 -583 =503, Elo -10 (95% -24
to +4).

**Prior:** 6/10. A king stuck in the centre is a classic weakness, and the
handover asked for exactly this.

**Commentary:** most likely double counting. The middlegame king table
already scores e1 at 0 and g1 at 30, so an uncastled king was already
penalised; adding 15-40 on top made the engine overvalue castling and fear
king moves. That hurts in exactly the sharp, gambit-style positions it was
meant to help: lines where Kf1 or keeping the king in the centre is
correct, or where it castles into an attack rather than finishing a
tactic. The 40 for lost castling rights is also blunt: it ignores whether
the centre is closed or whether the enemy queen can actually get at the
king. A smaller 5/20 version is being tested; if that also fails, king
danger probably needs to be measured directly (attackers near the king,
open files toward it) instead of by which file the king is on.

## Capture-exchange (SEE) pruning (before Oct 2026)

**Change:** pruning captures that lose material by static exchange
evaluation. Tried before this log existed and reverted; recorded in the
README handover.

**Result:** no gain (details not recorded).

**Prior:** 7/10. SEE pruning of losing captures in quiescence search is a
standard gain in most engines.

**Commentary:** no notes survive. Likely causes: a static exchange
evaluation is relatively expensive in plain JavaScript, so at 100 ms a move
the time it costs may cancel the nodes it saves, and MVV-LVA capture
ordering was already putting the bad captures last. Could be worth
retrying later with a cheap SEE used only in quiescence, and with longer
time controls, where pruning pays off more.
