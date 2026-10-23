# Engine 2 changelog

Every accepted change with its measured effect, newest first. Matches use
`scripts/match.sh` (fastchess, openings from `engine-1/test/openings.epd`,
each played with both colors). Elo figures are fastchess's estimate with its
95% error margin.

## Candidate: keep the hash move after a fail low (not yet released)

Two fixes (commits `48d9824`, `d8641a9`) on top of 2.9 (merge `615c3c2`).
**Against 2.9** (`scripts/release_match.sh`, cloud, 8+0.08, SPRT 0/10, 3
games at a time, no adjudication): **H1 accepted after 296 games** (112
wins, 54 losses, 130 draws, +69 +/- 27), then extended for a better
estimate and stopped by the owner at 700 games: 257 wins, 145 losses, 298
draws (58.0%), **+56 Elo (+/- 17)**, the final figure. New bench
signature **1962997** (2.9: 3300701).

First tested on top of 2.8, **against 2.8**
(`scripts/release_match.sh`, cloud, 8+0.08, SPRT 0/10, 3 games at a time,
no adjudication): **H1 accepted after 462 games**, 145 wins, 103 losses,
214 draws (54.5%), **+32 Elo (+/- 19)**. Bench signature on 2.8
**1820176** (2.8: 2933981).

**Playing strength: about 3000 (2980-3020) on Stockfish's scale.**
Against Stockfish 18 held to `UCI_Elo` 3000 (`scripts/strength_match.sh`;
cloud, 10+0.1, 3 games at a time, 1 thread and 64 MB each, `8moves_v3`
openings, adjudication on, SPRT -10/+10): undecided after two hours,
stopped at 620 games, 209 wins, 208 losses, 203 draws (50.1%), **+1 Elo
(+/- 22)** over Stockfish-3000 (LLR 0.09). It had led toward H0 (-13
+/- 28 at 360 games) before evening out. No time losses. 2.7 measured
about 2920 the same way (at 2900).

- **Hash move after a fail low** (`src/search.cpp`): at a node where every
  move scored below alpha, the highest of those scores picks a move more
  or less at random, and storing it overwrote the table's move for the
  position. Now a fail low keeps the move already stored for the same
  position and stores none for a new one (Stockfish's rule), so a revisit
  with no known best move also gets IIR. 38% fewer nodes on bench, 44% at
  bench 13; most of that is the extra IIR. Keeping the old move while
  still storing the noisy one for new positions saved only 3% (bench
  2856600, not match-tested).
- **FEN en passant square** (`src/position.cpp`): a square no pawn can
  have just passed (e.g. `4k3/8/8/3P4/8/8/8/4K3 w - e6`) is now dropped.
  Before, en passant "captured" from an empty square and read past the
  piece tables. No change to the search (bench unchanged).

## 2.9: a 256-unit network

2.8's search with a network twice as wide: 768 -> **256**x2 -> 1 (was
128), trained on the same 150M positions as 2.8 (20 epochs, cloud CPU).
**Against 2.8** (cloud, 8+0.08, SPRT 0/10, 4 games at a time,
adjudication on): **H1 accepted after 760 games**, 263 wins, 208 losses,
289 draws (53.6%), **+25 Elo (+/- 16)**. The release build searches
identically to the tested one (same bench). New bench signature
**3300701** (2.8: 2933981). Validation loss 0.00803 (2.8's net: 0.00813).

- The size is an option: `train.py --hidden N`, engine
  `-DENGINE_NNUE_HIDDEN=N` (the default, in `src/accumulator.h`, is now
  256; the built-in network must match it).
- Code only: the phone app stays on Engine 2.8 for now.

## 2.8: a network trained on 2.7's games

2.7's search with a new network (`nets/engine-2.8.nnue`). **Against 2.7**
(cloud, 8+0.08, SPRT 0/10, 4 games at a time, adjudication on): **H1
accepted after 242 games**, 116 wins, 45 losses, 81 draws (64.7%), **+105
Elo (+/- 33)**. The release build searches identically to the tested one
(same bench). New bench signature **2933981** (2.7: 2870078).

- **Data:** 100M new positions from 2.7's self-play at 5,000 nodes a move,
  with a random opening of 6-11 moves (was always 8, so either side can
  move first): 32% White wins, 35% draws, 32% Black wins (the 2.3 data
  leaned White, 37/31/32). About 26 hours on the cloud's 4 cores.
- **Training:** on the cloud CPU this time (the laptop was away), 20
  epochs, same network shape (768 -> 128x2 -> 1) and settings as 2.5.
- **Old data kept:** a net on the new 100M plus the old 50M (2.3's)
  beat one on the new 100M alone by **+16 Elo (+/- 12)** (H1 after 1,734
  games), so 2.8 uses all 150M.
- `-DENGINE_NET=<file>` now builds the engine with another network.

## 2.7: staged move generation (speed)

2.6 with moves generated and scored a stage at a time. **Against 2.6**
(cloud, 8+0.08, SPRT 0/10, 4 games at a time, adjudication on): **H1
accepted after 1,186 games**, 381 wins, 313 losses, 492 draws (52.9%),
**+20 Elo (+/- 14)**. New bench signature **2870078** (2.6: 3183003).

**Playing strength: about 2920 (2900-2940) on Stockfish's scale.** Against
Stockfish 18 held to `UCI_Elo` 2900 (`scripts/strength_match.sh`; cloud,
10+0.1, 3 games at a time, 1 thread and 64 MB each, Stockfish's
`8moves_v3` openings, adjudication on, SPRT -10/+10): **H1 accepted after
724 games**, 285 wins, 246 losses, 193 draws (52.7%), **+19 Elo (+/- 22)**
over Stockfish-2900. `UCI_Elo` is mapped to CCRL Blitz and calibrated at
longer time controls, so this is an estimate, not a list rating. One time
loss, Stockfish's (10 ms over). An earlier start on the random openings
(`engine-1/test/openings.epd`), stopped at 96 games to switch books, stood
at +45 -40 =11.

- **Staged move generation** (`src/movepick.h`, `MovePicker`): the hash
  move first, before anything is generated (checked with the new
  `pseudo_legal`), then the captures that don't lose material by SEE, the
  quiet moves (promotions, killers, then by history), and last the losing
  captures. A cutoff by an early move skips generating and scoring the
  rest. The order is score_moves' as before; only ties between equal
  scores come out differently (generation order), hence the new bench.
- **Lazy gives_check:** worked out only when a pruning rule needs it; LMR
  reads it from the position after the move. Same search on its own.
- **Speed:** about 7% fewer instructions per node than 2.6.
- **Test:** `tests/movepick_test.cpp`: the capture and quiet generators
  split the full one exactly, `pseudo_legal` agrees with the generator on
  all 65,536 move codes, and the picker returns every move once in score
  order (it catches four deliberately broken variants).

## 2.6: incremental NNUE updates (speed)

2.5's search and network, evaluated faster. **Against 2.5** (cloud,
8+0.08, SPRT 0/10, 4 games at a time, adjudication on): **H1 accepted
after 242 games**, 106 wins, 37 losses, 99 draws (64.3%), **+102 Elo
(+/- 32)**, no time losses. Evaluations are unchanged: bench signature
**3183003**, as 2.5.

- **Incremental accumulators:** `make_move` records the pieces each move
  adds and removes; the hidden layer's sums are kept per position
  (`src/accumulator.h`) and updated lazily at evaluation from the nearest
  computed position before, or from scratch when that is cheaper. Each
  accumulator is tagged with its network, so a new `EvalFile` never
  reuses stale sums.
- **Vectorized output layer:** the clipped-units-times-weights loop is now
  a plain 16-bit dot product the compiler vectorizes (`pmaddwd`); before,
  it ran one unit at a time and was a quarter of all instructions.
- **Speed:** bench about 1.14M -> 1.46M nodes/s on the cloud; NNUE went
  from 42% of the instructions to 8%. WebAssembly (with `-msimd128`)
  275k -> 1.25M nodes/s; the phone app is not rebuilt yet (still 2.5).
- **Test:** `tests/nnue_test.cpp` compares ~60,000 incremental evaluations
  with from-scratch ones (castling, en passant, promotions, null moves,
  unmaking, a change of network); it catches a broken castling update and
  a stale network.

## 2.5: the trained network

2.4 with the final network of the same training run (epoch 20 of 20; 2.4
had epoch 3). **Against 2.4** (cloud, 8+0.08, SPRT 0/10, 4 games at a
time, adjudication on): **H1 accepted after 332 games**, 156 wins, 81
losses, 95 draws (61.3%), **+80 Elo (+/- 30)**, LOS 100%, no time losses.
The release build (network built in, `nets/engine-2.5.nnue`) searches
identically to the tested setup (same scores and node counts at fixed depth
on four positions). New bench signature **3183003** (2.4: 3116434). Also
in the phone app (Android app version 2.5).

## 2.4: NNUE evaluation

2.3's search with a neural-network evaluation in place of the hand-written
one. **Against 2.3** (cloud, 8+0.08, SPRT 0/10, 4 games at a time,
adjudication on): **H1 accepted after 234 games**, 147 wins, 35 losses, 52
draws (73.9%), **+181 Elo (+/- 42)**, LOS 100%, no time losses. That test
ran 2.3's search with the network loaded from a file; the release build
(network built in) searches identically: same scores and node counts at
fixed depth on four test positions. Released on the owner's call without
a separate release match. New bench signature **3116434** (2.3: 3601124).

- **Network:** 768 inputs per side (piece kind x square, seen from that
  side) -> 128 hidden units (shared, clipped) -> both sides' units, side
  to move first -> 1 output. Integer inference (`src/nnue.h`), the hidden
  layer recomputed per evaluation (incremental updates are a later speed
  gain). About 20% fewer nodes/s than the hand-written evaluation, and
  more nodes per depth (pruning margins were tuned for the old scale).
- **Training data:** 50M quiet positions from 2.3's self-play at 5,000
  nodes a move (`engine2 datagen`, cloud, ~10 hours on 4 cores).
- **Training:** `scripts/nnue/train.py` (PyTorch) on the owner's laptop GPU
  (RTX A2000): target 75% search score, 25% game result; this network is
  epoch 3 of a 20-epoch run (about 13 minutes of training).
- **Built in:** `nets/engine-2.4.nnue` (now `nets/engine-2.5.nnue`, 2.5's network), embedded at build time
  (`cmake/embed_net.cmake`). UCI `EvalFile`: a network file, `<empty>` for
  the built-in one, `classical` for the hand-written evaluation.
- **Phone app:** the WebAssembly build carries the network too (same bench
  node count); analysis reaches about depth 13 in 4 s.

## 2.3: internal iterative reduction and logarithmic LMR

**Release match against 2.2** (cloud, `scripts/release_match.sh`, 8+0.08,
SPRT 0/10, 4 games at a time, every game played out): **H1 accepted after
586 games**: 203 wins, 138 losses, 245 draws (55.5%), **+39 Elo (+/- 21)**,
LOS 99.99%, no time losses. Extended for a firmer estimate and stopped
by the owner at **760 games**: 261 wins, 180 losses, 319 draws (55.3%),
**+37 Elo (+/- 18)**, no time losses. New bench signature **3601124** (2.2: 6533990): 45% fewer nodes
for the same depths. Also in the phone app (`engine-1/app/engine2/`).

Not in 2.3: the transposition-table change (quiescence probes + buckets;
flat at 8+0.08, under test at 40+0.4) and continuation history (parked for
a longer time control); see `HANDOFF.md`.

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
