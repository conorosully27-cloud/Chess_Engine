# Engine 2 changelog

Every accepted change with its measured effect, newest first. Matches use
`scripts/match.sh` (fastchess, openings from `engine-1/test/openings.epd`,
each played with both colors). Elo figures are fastchess's estimate with its
95% error margin.

## 4.3: smarter time use

4.2 plus the change below (commit `0476b93`; same network and search).
**Against 4.2** (cloud, 8+0.08, SPRT 0/10, 4 games at a time): **H1
accepted after 600 games**, 168 wins, 121 losses, 311 draws (53.9%),
**+27 Elo (+/- 17)**. Bench **3005772**, unchanged (only clock-based
searches are affected). Code only, no APK. State in
`release-matches/timeman/`.

- **Time use** (`src/search.*`): after each depth the planned time
  (remaining / 40 + 3/4 of the increment, as before) is scaled by how
  settled the search is: 130% when the best move just changed, 10% less
  for each depth it has survived (down to 70% after 6), plus 1% per
  centipawn the score fell since the last depth (at most 40%). The hard
  limit (3x the plan, half the clock) is unchanged; fixed `movetime`
  searches are not scaled. The five values are tunable (`TM_*`).

## 4.2: no sorting of quiet moves that will all be pruned

4.1 plus the change below (commit `fcda4f4`; same network). **Against 4.1**
(cloud, 8+0.08, SPRT 0/10, 4 games at a time): **H1 accepted after 976
games**, 259 wins, 208 losses, 509 draws (52.6%), **+18 Elo (+/- 13)**.
New bench signature **3005772** (4.1: 3236061), the same as the tested
build. Code only, no APK. State in `release-matches/speed2/`.

- **Skip sorting pruned quiet moves** (`src/movepick.*`, `src/search.cpp`):
  once futility or late move pruning prunes one quiet move, every later
  one is pruned too unless it gives check, so the picker hands the rest out
  as generated, without scoring or sorting, and the search tests each only
  for check (cheap since 4.1). Only the order of the few quiet checks
  searched changes. 15% more nodes/s, and 15% less time to depth 15.

## 4.1: faster code

4.0 with speed changes found by profiling (commits `66fc41c`, `a4eb9f8`;
same network and search rules). **Against 4.0** (`scripts/release_match.sh`,
cloud, 8+0.08, SPRT 0/10, 4 games at a time): **H1 accepted after 1,810
games**, 502 wins, 437 losses, 871 draws (51.8%), **+12 Elo (+/- 10)**.
New bench signature **3236061** (4.0: 3027103), the same as the tested
build. Code only: no APK (owner). State in `release-matches/speed1/`.

- **Cheap check test** (`src/position.*`): the squares from which each
  piece type would check the enemy king, and the pieces that would uncover
  a check, are worked out once per position on the first `gives_check`;
  each test is then a few bitwise ANDs instead of two slider lookups.
  Same search (bench unchanged); about 10% more nodes/s.
- **Move picker** (`src/movepick.*`, `src/search.cpp`): killer moves are
  tried before the quiet moves are generated and scored (unless a pawn can
  promote by a push), and captures are SEE-tested only when reached, in
  the main search and in quiescence. At depth 15, 3% less time than the
  check test alone.
- Profile of 4.0 before (callgrind, bench depth 9, search only): network
  ~20%, picking and scoring quiet moves ~15%, SEE at generation ~5%,
  check tests ~5.5%. Link-time optimization was tried and was 2-5%
  slower, so it was left out.

## 4.0: non-pawn correction history, tuned; above 3100

3.4 plus the change below (commit `01ca8c6`; same network). **Against
3.4**: H1 after 1,308 games, **+16 Elo (+/- 12)**. **Against Stockfish
18 at `UCI_Elo` 3100** (99% SPRT): H1 after 498 games, +30 +/- 22, the
first version above 3100, hence 4.0 by the owner's numbering rule (one
major version per hundred on Stockfish's scale). Bench **3027103**, the
same as the tested build. Code only: the phone app stays on 3.0. SPRTs
that stop early tend to overstate the gain; 3.4 was -5 +/- 19 against
3100, so about 3110-3120 is a fair estimate.

- **Non-pawn correction history, tuned** (commits `4464ca7`, `0eef8c3`,
  `9490f18`, `01ca8c6`; `src/position.*`, `src/search.*`): two more
  correction tables beside the pawn table, keyed by each side's non-pawn
  pieces (king included; a new incremental key in `Position`), the three
  corrections summed; every update moves each table by its share of the
  error they still leave together, the non-pawn tables at 27% of the
  pawn table's speed. SPSA (2,000 pairs at 8+0.08, `--r-end 0.005
  --c-scale 2`) set the correction limit to 50 cp (64), the maximum
  weight to 15/256 (16) and the non-pawn rate to 27% (25); `LMR_HISTORY`
  stayed at 200. **Against 3.4** (cloud, 8+0.08, SPRT 0/10, 4 at a time):
  **H1 accepted after 1,308 games**, 388 wins, 328 losses, 592 draws
  (52.3%), **+16 Elo (+/- 12)**. Bench **3027103**. At full learning
  rate the non-pawn tables lost (`FAILED_EXPERIMENTS.md`). **Against
  Stockfish 18 at `UCI_Elo` 3100** (99% SPRT, 10+0.1, `8moves_v3`): **H1
  after 498 games**, +173 -130 =195 (54.3%), **+30 +/- 22**: about 3130.

## 3.4: history in late move reductions, and correction history

3.3 plus the two search changes below (commit `b9a2ef3`; same network).
**Against 3.3** (`scripts/release_match.sh`, cloud, 8+0.08, SPRT 0/10, 4
games at a time): **H1 accepted after 812 games**, 231 wins, 178 losses,
403 draws (53.3%), **+23 Elo (+/- 15)**. New bench signature **2944258**
(3.3: 2826031), the same as the tested build. Code only: the phone app
stays on 3.0. An SPRT that stops early tends to overstate the gain. State
in `release-matches/rel34/`. Against Stockfish-3100 (99% SPRT, 10+0.1):
stopped at 660 games, -5 +/- 19: about 3095, not yet 4.0.

- **History in late move reductions** (commit `b4f1e10`, `src/search.cpp`):
  a late quiet move's history score (its own plus both continuation
  histories) takes a ply off its reduction per 200 points, or adds one per
  200 below zero, at most 2 either way (`LMR_HISTORY`). **Against 3.3**
  (`scripts/release_match.sh`, cloud, 8+0.08, SPRT 0/10, 4 games at a
  time): **H1 accepted after 2,048 games**, 544 wins, 477 losses, 1,027
  draws (51.6%), **+11 Elo (+/- 9)**. Bench **3450556** (22% more nodes:
  well-ordered moves are searched deeper).
- **Correction history** (commit `b9a2ef3`, `src/search.h`,
  `src/search.cpp`): the static evaluation is corrected by a running
  average of (search score - evaluation) for the pawn structure and side
  to move (16K entries each, keyed by the pawn hash, within +/-64 cp,
  weight min(depth + 1, 16) / 256 per update, from quiet results that bound
  the evaluation from the right side); the table keeps the raw evaluation.
  Main search and quiescence's stand-pat. **Against the history build
  above** (cloud, 8+0.08, SPRT 0/10, 4 at a time): **H1 accepted after 532
  games**, 159 wins, 112 losses, 261 draws (54.4%), **+31 Elo (+/- 18)**.
  Bench **2944258** (15% fewer nodes).

## 3.3: a 512-unit network, and faster network code

3.2's search with a network twice as wide, 768 -> **512**x2 -> 1
(`nets/engine-3.3.nnue`; trained on the owner's laptop GPU, 15 epochs, on
the same 346.5M positions as 3.2's), plus speed changes that leave the
search unchanged (commit `64a5f7c`). **Against 3.2** (owner's laptop,
8+0.08, SPRT 0/10, 12 games at a time, adjudication on): **H1 accepted
after 472 games**, 181 wins, 119 losses, 172 draws (56.6%), **+46 Elo
(+/- 23)**. New bench signature **2826031** (3.2: 2608402), the same as
the tested build. Code only: the phone app stays on 3.0. An SPRT that
stops early tends to overstate the gain.

- **The network alone** was only **+5 +/- 6** against 3.2 over 6,984
  games (LOS 94.7%, SPRT undecided): about 20% fewer nodes/s on the
  laptop (10% on the cloud CPU) took back most of what it evaluates
  better. Against Stockfish-3100 (cloud, 10+0.1): H0 after 558 games, -19
  +/- 22 (3.2: -25 +/- 25).
- **Speed** (`src/nnue.cpp`, `src/tt.h`, `src/search.cpp`): the network's
  updates written with AVX2, 32 units a step (the compiler's loops stepped
  16), and its output layer clamping with min/max in one pass for both
  sides; plain loops without AVX2 (WebAssembly). A prefetch of the
  transposition-table entry right after each move into a node that will
  probe it. Same evaluations and search (bench unchanged with and without
  AVX2). On the cloud CPU: +4% nodes/s at 512 (+1.5% at 256) and ~2% from
  the prefetch with a 64 MB table; on the laptop clearly more, judging by
  the jump from +5 to +46.
- `scripts/strength_match.sh`: `ALPHA=0.01` runs the SPRT at 99%.

## 3.2: a network trained on 350M positions

3.1's search with a new 256-unit network (`nets/engine-3.2.nnue`, the
training run's `net-15.nnue`), trained on
the owner's laptop GPU (15 epochs) on **346.5M positions**: 2.3's 50M,
2.7's 100M and **200M new ones from 3.0 with book openings** (data run 3,
`datagen`'s new `[book]` argument; random 8moves_v3 lines then 2-5 random
plies). Validation (2.3 data only in this run, see below) 0.00914 after
epoch 1, 0.00846 after epoch 13. **Against 3.1** (owner's laptop,
8+0.08, SPRT 0/10, 12 games at a time, adjudication on): **H1 accepted
after 278 games**, 129 wins, 60 losses, 89 draws (62.4%), **+88 Elo
(+/- 31)**. New bench signature **2608402** (3.1: 2851725), the same as
the tested build. Code only: the phone app stays on 3.0. An SPRT that
stops this early tends to overstate the gain.

**Playing strength: about 3080 (3050-3105) on Stockfish's scale.**
Against Stockfish 18 held to `UCI_Elo` 3050 (`scripts/strength_match.sh`;
cloud, 10+0.1, 4 games at a time, 1 thread and 64 MB each, `8moves_v3`
openings, adjudication on, SPRT -10/+10): **H1 accepted after 372
games** (stronger than Stockfish-3050), 132 wins, 102 losses, 138 draws
(54.0%), **+28 Elo (+/- 27)**, in 58 minutes. No time losses. 3.0
measured about 3010 the same way (at 3000). Against Stockfish-3100 (same
setup): **H0 accepted after 462 games** (weaker), 122 wins, 155 losses,
185 draws (46.4%), -25 Elo (+/- 25), so about 3075; between 3050 and
3100, consistent with the 3050 test.

- The trainer read each position from a random place in the files: fine
  while the data fit in memory, 13.5k positions/s on 11 GB. Batches now
  come from a shuffle buffer (chunks of consecutive positions, in random
  order, shuffled 16M at a time): ~220-280k positions/s on the laptop's
  GPU, ~26 min an epoch.
- Validation took the first 200k held-out positions in file order (here
  all 2.3 data); it now takes the same fraction from every file (after
  this run).

## 3.1: tuned search values + continuation history

3.0 plus SPSA-tuned pruning and reduction values (commit `6f9d162`) and
continuation history (commit `15f2781`). **Against 3.0** (`scripts/match.sh`,
cloud, **20+0.2**, SPRT 0/10, 4 games at a time, adjudication on): **H1
accepted after 1,226 games**, 323 wins, 269 losses, 634 draws (52.2%),
**+15 Elo (+/- 12)**, LOS 99.5%. New bench signature **2851725** (3.0:
3044298), the same as the tested build. Code only: the phone app stays
on 3.0. An SPRT that stops early tends to overstate the gain.

- **Tuning build and SPSA** (`src/tune.h`, `scripts/tune/spsa.py`): with
  `-DENGINE_TUNE=ON` the search parameters become UCI options; a
  fishtest-style SPSA run of 4,000 game pairs at 8+0.08 tuned 15 of them
  on 3.0. Rounded changes: futility pruning up to depth 3 (was 2), base
  -3, margin 147 (150); reverse futility margin 94 (90); SEE capture
  margin 97 (100); late move reductions 0.98 + ln d ln m / 1.98 (1 + ... /
  2); singular margin 1.9 x depth (2). Alone vs 3.0 at 8+0.08: +4 +/- 7
  over 3,560 games (LOS 88%), stopped undecided by the owner to test it
  with continuation history.
- **Continuation history** (`src/movepick.h`, `src/search.cpp`): how well
  a quiet move did after the moves one and two plies earlier, added to its
  history score in move ordering, updated like history. 16-bit entries
  (1.2 MB); speed within noise. First tried before NNUE (-7 +/- 17 at
  8+0.08); this is its first pass.

## 3.0: singular extensions, the first version above 3000

2.10 plus singular extensions (commit `0cf4005`). Named 3.0 by the owner:
the first version rated above 3000 on Stockfish's `UCI_Elo` scale. **Against 2.10**
(`scripts/release_match.sh`, cloud, 8+0.08, SPRT 0/10, 4 games at a time,
no adjudication): **H1 accepted after 368 games**, 121 wins, 73 losses,
174 draws (56.5%), **+46 Elo (+/- 22)**. No time losses. New bench
signature **3044298** (2.10: 1962997), the same as the tested build. An
SPRT that stops early tends to overstate the gain.

**Playing strength: about 3010 (2995-3025) on Stockfish's scale.**
Against Stockfish 18 held to `UCI_Elo` 3000 (`scripts/strength_match.sh`;
cloud, 10+0.1, 4 games at a time, 1 thread and 64 MB each, `8moves_v3`
openings, adjudication on): 471 wins, 434 losses, 395 draws after 1,300
games (51.4%), **+10 Elo (+/- 15)** over Stockfish-3000. The SPRT
(-10/+10) stood near 0 at 424 games; the owner then had it extended to a
fixed 1,424 games (SPRT off), and the session stopped it at 1,300. Its
LLR computed from the game pairs at 1,300 games: **+3.18**, past the
+2.94 for H1 (stronger than Stockfish-3000), so a narrow pass. No time
losses. 2.10 measured about 3000 the same way (even after 620 games).

- **Singular extensions** (`src/search.cpp`): from depth 8 (not at the
  root, and not past twice the iteration's depth), when the hash move's
  entry is a lower or exact bound at most 3 plies shallower and not a
  mate score, the other moves are searched to (depth - 1) / 2 against the
  table's score less 2 x depth, with the hash move left out. If none
  reaches it the hash move gets a ply more; if they do and that margin is
  at or above beta, the node cuts off (multi-cut). At bench 13: 44% of the
  8,145 tests extend, 34% cut off, and the tests cost 5% of the nodes.
- The tested build also has commit `a92cc44` (no change to the search):
  the static evaluation kept in the table entry, and `nnue::update`
  filling in the accumulators it passed over after a refresh.

## 2.10: keep the hash move after a fail low

2.9 plus two fixes (commits `48d9824`, `d8641a9`; tested as merge
`615c3c2`). Named 2.10 rather than 3.0 by the owner: it matched
Stockfish-3000 but didn't beat it.
**Against 2.9** (`scripts/release_match.sh`, cloud, 8+0.08, SPRT 0/10, 3
games at a time, no adjudication): **H1 accepted after 296 games** (112
wins, 54 losses, 130 draws, +69 +/- 27), then extended for a better
estimate and stopped by the owner at 700 games: 257 wins, 145 losses, 298
draws (58.0%), **+56 Elo (+/- 17)**, the final figure. The release build
searches identically to the tested one (same bench). New bench signature
**1962997** (2.9: 3300701). Code only, like 2.9: the phone app stays on
2.8.

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
