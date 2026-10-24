# Engine 2 handoff

Start here when picking up Engine 2 in a new session. Companion files:
`README.md` (build, layout, tools), `CHANGELOG.md` (every accepted change and
its measured Elo), `FAILED_EXPERIMENTS.md` (every rejected one), and Engine 1's
`../engine-1/HANDOFF.md` and `../engine-1/FAILED_EXPERIMENTS.md` for the
history behind the search and evaluation.

## Where things stand

- **Engine 2.0 is Engine 1.6 ported to C++**: same search, same
  evaluation (identical scores on 30,000 positions), about 6x the speed.
  It beat Engine 1.6 by **+372 Elo (+/- 59)** over 200 games at 8+0.08.
- **Plan agreed with the owner:** search and speed work first (list below),
  NNUE after. **2.1 is 2.0 plus the speed work** (item 1); later items get
  later version numbers.
- 2.0 is merged to `main`
  ([conorosully27-cloud/Chess_Engine#2](https://github.com/conorosully27-cloud/Chess_Engine/pull/2)).
- **Engine 2.1** (speed, +28% nodes/s over 2.0, same search) passed its
  release SPRT against 2.0 and measured **+37 Elo (+/- 16)** over 1,000
  games at 8+0.08 (commit `3a3f237`).
- **Engine 2.2** (SEE: quiescence pruning, losing captures ordered last,
  SEE pruning in the main search) passed its release SPRT against 2.1 on
  the owner's laptop: **+19 Elo (+/- 14)** over 1,366 games at 8+0.08.
  It is the next baseline; details in `CHANGELOG.md`.
- **Engine 2.3** (2.2 + IIR + mild LMR) passed its release SPRT against
  2.2: H1 after 586 games at 8+0.08, then extended and stopped at 760
  games: **+37 Elo (+/- 18)**. Merged to `main`
  ([conorosully27-cloud/Chess_Engine#5](https://github.com/conorosully27-cloud/Chess_Engine/pull/5)).
  Commit `836fe46`; **the next baseline**, bench 3601124.
- **Engine 2.4** = 2.3 + NNUE evaluation (network built in,
  `nets/engine-2.4.nnue`): **+181 Elo (+/- 42)** over 2.3 (H1 after 234
  games). **The new baseline**, bench 3116434. Details in `CHANGELOG.md`
  and the NNUE section below.
- **Engine 2.5** = 2.4 with the final network (net-20) built in,
  `nets/engine-2.5.nnue`: **+80 Elo (+/- 30)** over 2.4 (H1 after 332
  games). **The new baseline**, bench 3183003.
- **Engine 2.6** = 2.5 with incremental NNUE updates and a vectorized
  output layer (same evaluations, about 1.3x the nodes/s natively, 4.5x in
  WebAssembly): **+102 Elo (+/- 32)** over 2.5 (H1 after 242 games).
  **The new baseline**, bench 3183003 (unchanged from 2.5).
- **Engine 2.7** = 2.6 with staged move generation and lazy gives_check:
  **+20 Elo (+/- 14)** over 2.6 (H1 after 1,186 games). **The new
  baseline**, bench 2870078. (The phone app went 2.5 -> 2.8, skipping 2.6
  and 2.7.)
- **2.7's playing strength: about 2920 (2900-2940) on Stockfish's
  `UCI_Elo` scale (CCRL Blitz).** vs Stockfish 18 at `UCI_Elo` 2900, 10+0.1,
  `8moves_v3` openings: H1 (stronger) after 724 games, +285 -246 =193,
  +19 Elo (+/- 22). Details in `CHANGELOG.md`; rerun for later versions
  with `scripts/strength_match.sh` (at 3000 once they pass 2950 or so).
  The owner prefers this standard book over our random openings for
  ratings.
- **Engine 2.8** = 2.7 with a network trained on 2.7's games (100M new +
  50M old positions, `nets/engine-2.8.nnue`): **+105 Elo (+/- 33)** over
  2.7 (H1 after 242 games). Bench 2933981. The phone app has 2.8.
- **Engine 2.9** = 2.8 with a 256-unit network (`nets/engine-2.9.nnue`,
  same 150M positions): **+25 Elo (+/- 16)** over 2.8 (H1 after 760
  games). Bench 3300701. Released as code only (the owner's call): the
  app is still 2.8 until asked (`build_wasm.sh` would now build 2.10).
- **Engine 2.10** = 2.9 keeping the hash move after a fail low (and storing
  none for a new position), plus a FEN en passant fix: on 2.8, **+32 Elo
  (+/- 19)** over 2.8 (H1 after 462 games, cloud, 8+0.08). On 2.9:
  **+56 Elo (+/- 17)** over 2.9 (H1 after 296 games, final figure at 700
  games, cloud, 8+0.08), bench 1962997. **Playing strength about
  3000 (2980-3020)** on Stockfish's `UCI_Elo` scale: vs Stockfish 18 at
  3000, 10+0.1, `8moves_v3`, dead even after 620 games (+209 -208 =203,
  +1 +/- 22), SPRT undecided, stopped at the cloud's 2-hour task limit.
  The owner kept 3.0 for a version that beats 3000. Bench 1962997.
  Code only, like 2.9. Details in `CHANGELOG.md`.
- **Engine 3.0** = 2.10 with singular extensions (`0cf4005`, plus
  `a92cc44`, which leaves the search unchanged): **+46 Elo (+/- 22)** over
  2.10 (H1 after 368 games, cloud, 8+0.08, 4 at a time). **The new
  baseline**, bench 3044298. **Playing strength about 3010
  (2995-3025)**: vs Stockfish 18 at `UCI_Elo` 3000, 10+0.1, +471 -434 =395
  after 1,300 games, +10 +/- 15, LLR +3.18 from the game pairs (H1, a
  narrow pass). The first version above 3000. Another session was
  retuning the pruning margins (reverse futility etc.) at the same time;
  the two don't touch the same code, but whichever merges second should
  get one SPRT of the combined build.
- **Phone app: Engine 3.0** (WebAssembly rebuilt from 3.0, skipping 2.9
  and 2.10 as the owner planned; Android `versionCode` 6 / `versionName`
  "3.0"). `node scripts/wasm_bench.mjs`: 3044298, the native bench.
  Checked in Chromium at phone size: labels "Engine 3.0", a game, the
  analysis board at depth 19 in 5 s, no console errors. APK not built
  here (Android Studio on the owner's laptop).
- For fun, 2.5 vs Engine 1.6 (cloud, 8+0.08, 200 games, adjudication):
  +192 -3 =5 (97.3%), about **+620 Elo (+/- 157)** (2.0 had +372 over 1.6).
- **Tests:** `ctest --test-dir build`, 7 suites (perft, position, SEE,
  move picker, NNUE, search, UCI), a few seconds. **Bench signature: 3044298** (`build/engine2 bench`, with the
  built-in network; 2.10 was 1962997; 2.9 was 3300701; 2.8 was 2933981; 2.7 was 2870078; 2.6 and 2.5 were 3183003; 2.4 was 3116434, 2.3 3601124, 2.2 6533990, 2.1 9557353, 2.0 9559579).
  The tests use the hand-written evaluation (no network loaded), except the NNUE test, which loads the built-in one; the same on Linux/gcc and Windows/MinGW.
- **Owner's laptop:** Windows, gcc (WinLibs), CMake, Ninja, AVX2 CPU; builds
  and passes the tests; about 3.1M nodes/s on bench. Has fastchess
  (prebuilt Windows release, `C:\Users\conor\fastchess-bin\...`) and runs
  SPRTs at 12 games at a time, about 4x the cloud's rate: good for long
  tests. Cute Chess is set up for Engine 1.6 and not yet for Engine 2.
- **Repository:** `engine-1/` (JavaScript, frozen at 1.6, the phone app) and
  `engine-2/` (C++), built and run separately.

## Setting up a new session

```sh
cd engine-2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build
ctest --test-dir build
./build/engine2 bench            # must print the signature above until a search change

# fastchess, for matches (not in the repo)
git clone https://github.com/Disservin/fastchess ~/tools/fastchess && make -C ~/tools/fastchess -j4
```

The cloud machine has 4 cores. Matches use 3 (`concurrency 3`); keep the
fourth free and **don't compile or run anything heavy during a match**: a
build stole CPU from a game once and caused a time loss.

## Testing a change

- **Behavior-preserving changes** (speed work, refactors): the bench node
  count must not change. Then check speed with `bench` (best of a few runs,
  nothing else running).
- **Everything else:** SPRT against the previous build:
  `SPRT=1 scripts/match.sh build/engine2 ~/matches/engine2-before 20000 8+0.08`
  (copy the old binary first). Bounds 0 / 10 Elo, as Engine 1 used. Accept
  only on "H1 accepted"; record rejections in `FAILED_EXPERIMENTS.md`.
  Since Oct 2026 `match.sh` adjudicates decided games (resign at 6+ pawns
  for 3 moves with both engines agreeing, draw within 0.2 pawns for 8
  moves after move 34), about 20-30% less time per game; `ADJUDICATE=0`
  turns it off. Tests before that played every game out. Release matches
  (`release_match.sh`) still play every game out.
- Keep a copy of each accepted build in `~/matches/` (outside the repo) as
  the next baseline.
- **Release matches:** `scripts/release_match.sh start <name> <new commit>
  <old commit>` (run detached with `setsid nohup ... &`), then `status`,
  `checkpoint` (copies the state to `release-matches/<name>/`; commit and
  push it now and then) and `resume` after any interruption, even on a fresh
  machine.

## How the owner works (follow these)

Carried over from Engine 1:
- The owner is a strong chess player. They like short status updates, chess
  explanations of what the engine does, and honest reads of noisy results
  (early leads fade).
- **The only goal is beating the current best version, by SPRT.**
- **The owner starts the final full test of a release candidate** against
  the previous version: get the candidate ready (tested, committed, pushed),
  then stop and tell them.
- Every rejected change goes in `FAILED_EXPERIMENTS.md`: what changed, the
  commit, the results, a prior rating out of 10 and why it failed.
- Work on the session's assigned branch and push there; the owner asks for
  pull requests.
- The owner follows from a phone; the laptop is for Cute Chess and Windows
  builds. Give PowerShell commands one per line, from the right folder.
- **Keep the session awake during matches:** the session sleeps after the
  turn ends unless a tracked background task runs, and that kills detached
  matches. Start the match detached (`nohup ... &`) and run a watcher beside
  it: a Bash `run_in_background` loop that waits for "Finished match" in the
  log (fastchess writes "H1 was accepted" / "H0 was accepted", not
  "H1 accepted": a watcher matching the latter missed a verdict once). Tracked tasks stop after 2 hours, so plan tests to
  decide within that.

## Plan after 2.0, in order

Each item is one or more SPRT-tested changes.

1. **Speed: done as 2.1** (+37 Elo) (+28% nodes/s; see `CHANGELOG.md` for what was
   done and what didn't help). Where the time goes now (callgrind,
   `valgrind --tool=callgrind ./engine2 bench 8` on a `-g` Release build):
   evaluation ~25%, move picking ~9%, ordering scores ~8%, move generation
   ~8% (mostly in quiescence), make/unmake ~8%. Wall-time comparisons are
   noisy on the shared cloud machine (+/-10%): alternate the builds and
   compare medians.
2. **Static exchange evaluation (SEE): done as 2.2** (+19 Elo). In steps,
   each tested on its own:
   1. `Position::see_ge` (swap algorithm; pins ignored; knight = bishop =
      325), checked by `tests/see_test.cpp` against hand-worked exchanges
      and a slow recursive reference on every move of random playouts.
      **Done**; no search change.
   2. Quiescence skips captures with `!see_ge(move, 0)` instead of the
      pawn-guard rule (`9e12201`, bench 9022406). **Flat**: -1 +/- 17 at
      800 games vs 2.1, stopped (see `FAILED_EXPERIMENTS.md`). Kept as the
      base of the next steps.
   3. Ordering: captures losing by SEE after the quiet moves (`727f270`,
      bench 8856674). vs step 2 on the laptop: +7 +/- 13 at 1,578 games,
      LLR 0.42, still running.
   4. SEE pruning in the main search: non-PV, depth <= 6, skip quiets losing
      more than 25*depth^2 and captures losing more than 100*depth
      (`9feb847`, bench 6533990). vs step 3 (cloud): +177 -149 =214 at
      540 games, Elo +18 +/- 21, LLR 1.18, stopped to free the cores.
   The package vs 2.1 is the 2.2 release match (laptop, H1 at 1,366
   games). Version plan: 2.3 = further SEE enhancements (e.g. item 11);
   LMR, IIR and the rest get later numbers.

Outstanding, in priority order (owner's ranking, Oct 2026). Each its own
SPRT against the best build at the time:

3. **Logarithmic LMR**: 1.6's parked steeper version (Engine 1's
   `FAILED_EXPERIMENTS.md`): 1 + ln(depth) ln(move) / 1.75 from the third
   move, one ply less on the PV and for killers, one more for zero
   history, never at the root.
   - On 2.2 alone (vs 2.2, cloud, 2 games at a time): +888 -818 =1054 at
     2,760 games, Elo +9 +/- 9, LLR 1.66, **stopped** by the owner as
     slow to decide (a small gain, not a rejection).
   - On 2.2 + IIR, vs 2.2 + IIR: 680 games, -4 +/- 19, LLR -0.99,
     stopped as flat (see `FAILED_EXPERIMENTS.md`).
   - **Mild version accepted by the owner** (divisor 2.0, no zero-history
     rule): vs 2.2 + IIR, 2,180 games, +9 +/- 10, LOS 96%, LLR 1.54 (no
     formal H1; see `CHANGELOG.md`). On this branch on top of IIR. **2.2 +
     IIR + mild LMR (bench 3601124) is now the best build** and the
     baseline for the next tests. Not yet released.
4. **Internal iterative reduction: accepted** (+28 +/- 17 vs 2.2 over 776
   games, see `CHANGELOG.md`). Depth >= 4 with no TT move (not at the
   root) searches a ply shallower. On this branch on top of 2.2; **bench
   5320123** is now the signature of the best build, and the next tests'
   baseline. Not yet released. (Superseded: see item 3.)
5. **Stronger null move**, built on 2.2 + IIR (not committed yet):
   - 3 + depth/3 (+1 per 200 cp above beta, at most 3): more nodes (bench
     6119429), -11 +/- 31 at 260 games, stopped (`FAILED_EXPERIMENTS.md`).
   - 3 + depth/4 with the null search at least 1 ply: bench 4090339, -70
     +/- 64 at 40 games, stopped (too shallow a check from depth 4 to 7).
   - v3, 1.6's reduction (2, 3 from depth 7) plus one per 200 cp the
     static eval is above beta, at most 3:
     `(depth >= 7 ? 3 : 2) + std::min((static_eval - beta) / 200, 3)`.
     On 2.2 + IIR, bench 5485422. **Paused** by the owner at 40 games
     (+16 -13 =11, +26 +/- 71), not rejected.
   - v3 on the best build (2.2 + IIR + mild LMR): 980 games, -0 +/- 15,
     stopped and taken out. **No gain in three tries**; retry after the
     transposition-table work (`FAILED_EXPERIMENTS.md`).
6. **Transposition table**, in separate steps:
   1. **Quiescence uses the table** (it didn't): probe it, cut off on any
      matching entry off the main line, try its move first; store results
      at depth 0. Alone, on the best build (bench 3329263): 700 games,
      +222 -216 =262, +3 +/- 18, LOS 63%, stopped as flat: with one entry
      per slot, quiescence entries rarely survive to be found again.
   2. **Buckets**, tested together with step 1: 5 entries per bucket, one
      64-byte cache line (alignas(64)); each entry records the search that
      last wrote or found it (`age`); a new position replaces an empty
      entry, else the one with the lowest depth minus 8 per search of age.
      Quiescence stores only over empty, quiescence or stale entries. Bench
      **3139756** (13% fewer nodes than 3601124), about 5% fewer nodes/s
      (memory), bench 8% faster overall (median of 6 alternating runs).
      (A 4-entry, 48-byte bucket straddled cache lines: 13% fewer
      nodes/s, no faster.) Steps 1 + 2 vs the best build:
      - 8+0.08 (owner's laptop, 12 at a time): 1,204 games, +366 -377
        =461, Elo -3 +/- 15, LOS 34%, LLR -1.37; stopped as flat (the 64 MB
        table hardly fills at this speed, and the probes cost ~5% speed).
      - 40+0.4 (laptop): 1,076 games, +317 -293 =466, Elo +8 +/- 14, LOS
        86%, LLR 0.53; stopped. Probably a small gain at longer time
        controls (the table fills), unproven.
      - In a "2.4 bundle" with continuation history, vs 2.3 at 40+0.4:
        +4 +/- 19 over 638 games. **Parked until after NNUE**
        (`FAILED_EXPERIMENTS.md`); code in `31fddc5`, `d2d1303`.
   3. Store the static evaluation, and reuse it instead of re-evaluating.
7. **"Improving" flag** (static eval better than two plies ago) to tighten
   reverse futility, futility and late move pruning when not improving.
8. **Continuation history**: tested at 8+0.08 on the best build, 880 games,
   -7 +/- 17, stopped and taken out; **parked for a longer time control**
   (see `FAILED_EXPERIMENTS.md`; code in `924f494`). Then **history
   pruning** of quiet moves with very bad history at low depth.
9. **Singular extensions** (needs the TT work first).
10. **Razoring, ProbCut**: small gains, later.
11. **SEE with tuned piece values** (owner's parked idea): phase-blended
    material from the evaluation instead of 100/325/325/500/900; the tuned
    middlegame values rate a queen above two rooks.
12. **Lazy SMP (threads)**: big gain on the owner's laptop in Cute Chess;
    hard to test here with 3 of 4 cores in use. Do last, confirm on the
    laptop.

**NNUE (started Oct 2026).** Plan: self-play data from Engine 2 (cloud,
and the owner's laptop when free), a PyTorch trainer on the owner's laptop
GPU (NVIDIA RTX A2000 8 GB, CUDA; Python 3.12 and PyTorch 2.11+cu128
installed and checked; 16 GB RAM, so the trainer streams data from disk),
then NNUE inference in the engine, checked against the trainer, and an
SPRT against the best build. Network to start with: 768 inputs (piece x
square, per side) -> 128 hidden per side -> 1.
1. **Data:** `engine2 datagen` (`src/datagen.h` for the 32-byte format;
   reader `scripts/nnue/data.py`). First run: 2.3's search plus datagen
   (`~/nnue-data/engine2-2.3-datagen`, bench 3601124), 4 processes, 5,000
   nodes a move, writing `~/nnue-data/part-1..4.bin`, 12.5M positions each
   (50M, 1.6 GB; the owner cut it from 100M), about 6M positions an hour
   in all. Survived a container restart at ~03:10 UTC (26.75M saved, no
   partial records); restarted with new seeds. **Resume with a new seed**
   (a reused seed replays the same games); used: 1001-4, 2001-4, 3001-4.
   **Done 20 Oct ~09:00 UTC: 50,000,150 positions (1.6 GB)** in
   `~/nnue-data/part-1..4.bin` (cloud; gone if the machine is reset, so
   copy it off before relying on it). Checked: White wins 37.0%, draws
   31.0%, Black wins 32.0%, mean score +31, all four parts alike; 93.8%
   of positions distinct (only 41k seen 10+ times). Moving data to the
   laptop: compressed chunks sent to the owner, or generate there.
2. **Trainer: done**, `scripts/nnue/train.py` (PyTorch; memory-mapped
   data, so the 16 GB laptop copes; GPU if present; checkpoint and
   `net-<epoch>.nnue` every epoch, `--resume` to continue). Target: 75%
   sigmoid(score / 400) + 25% game result (`--wdl`). Adam 1e-3, cosine
   to 1e-4, batch 16384. On the cloud CPU ~65k positions/s.
3. **Inference in the engine: done**, `src/nnue.h/.cpp`: integer (first
   layer x255 in 16-bit accumulators, output x64), hidden layer
   recomputed per evaluation for now (incremental updates later: a speed
   gain once a net is worth keeping). Loaded with the UCI option
   `EvalFile` or `engine2 eval <net>`; no network = the hand-written
   evaluation, bench unchanged (3601124). With a net about 8% fewer
   nodes/s. `scripts/nnue/check_net.py` compares engine and trainer:
   mean 2.9 cp apart, max 14 (rounding).
   End-to-end test: a 30-second CPU net (2M positions) played 20 clean
   games vs 2.3 (lost 1.5-18.5, as expected from so little training).
4. **Training run on the laptop GPU** (RTX A2000): ~197k positions/s,
   ~252 s an epoch on the 49.5M training positions, 20 epochs planned.
   Epoch 3: train loss 0.00804, validation 0.00824. `check_net.py`: engine
   within 6.2 cp of the trainer on average (max 29, ~1% of the eval
   spread: rounding; the check's fixed thresholds are too tight for
   stronger nets and should become relative).
5. **First SPRT: 2.3 + net-3 vs 2.3** (cloud, 8+0.08, 4 at a time,
   adjudication): **H1 accepted after 234 games, +147 -35 =52 (73.9%),
   +181 Elo (+/- 42)**, LOS 100%, no time losses. With the network the
   search reaches a given depth on ~2.6x the nodes (pruning margins tuned
   for the old evaluation's scale): retuning them is a likely further gain.
   **Released as 2.4** with net-3 built in (owner's call, without waiting
   for the 20-epoch net).
6. **net-20** (the run's last epoch: train 0.00760, validation 0.00793;
   `check_net.py` 5.8 cp mean): **2.4 + net-20 vs 2.4: H1 after 332
   games, +156 -81 =95 (61.3%), +80 Elo (+/- 30)**, LOS 100%, no time
   losses (cloud, 8+0.08, 4 at a time, adjudication). **Released as
   2.5** (net-20 built in). Train/validation gap widened (0.0002 ->
   0.0003): the next net needs more data more than more epochs. Next: the later epochs of the laptop run vs
   2.4 (`EvalFile` = the candidate net), incremental accumulator updates
   (speed), retuning the pruning margins for the network's scale, then
   bigger nets and more data (generated with 2.4, so better labels).
7. **Incremental accumulator updates + vectorized output layer** (pure
   speed, same evaluations, bench unchanged at 3183003): bench 1.14M ->
   1.46M nodes/s; the profile showed the scalar output layer, not the
   from-scratch accumulators, was most of the NNUE's cost. **vs 2.5: H1
   after 242 games, +106 -37 =99 (64.3%), +102 Elo (+/- 32)** (cloud,
   8+0.08, 4 at a time, adjudication). **Released as 2.6.** The profile
   now puts move ordering on top (`pick_next` + `score_moves` ~22%):
   staged move generation is the next speed job.
8. **Staged move generation + lazy gives_check** (`src/movepick.h`;
   commit `2d5f3b4`, bench 2870078): the hash move before any generation,
   then good captures, quiets, bad captures; gives_check only when a
   pruning rule needs it. ~7% fewer instructions per node; ties between
   equal scores now come out in generation order, so the search differs.
   **vs 2.6: H1 after 1,186 games, +381 -313 =492 (52.9%), +20 Elo
   (+/- 14)** (cloud, 8+0.08, 4 at a time, adjudication). **Released as
   2.7.** Picking the next quiet (selection over long lists) is still ~12%. Next for strength: more
9. **Data run 2 (Oct 20, 22:24 UTC):** 100M positions from **2.7** (commit
   `5ba42d6`: random opening of 6-11 plies instead of 8), 5,000 nodes a
   move, 4 cores, ~4M positions an hour (~25 h). Writing
   `~/nnue-data/v2/part-1..4.bin` (25M each, logs beside them), seeds
   **5001-5004** (resume with new seeds, e.g. 6001-6004). Binary
   `~/engines/engine2-2.7-datagen`. **Training queued on the cloud CPU**
   (the laptop is away; data stays put, nets are small enough for git):
   `~/nnue-nets/run2/queue.sh` (detached, log `queue.log`) waits for the
   datagen, checks all four parts reached 25M, then trains **A-new** (new
   100M only) and **B-mix** (new 100M + old 50M), 20 epochs each, ~65k
   positions/s: ~9 h + ~13 h. Restartable (resumes from checkpoints). Then
   SPRT A vs B, the winner vs 2.7. 256 hidden units later (engine change).
   **Datagen done 21 Oct 23:38 UTC: 100,000,067 positions** (3.2 GB),
   ~3.9M an hour. Sample of 1M per part: White wins 32%, draws 35%, Black
   wins 32%, White to move 50.0%, mean score about 0 (the old set: 37/31/32,
   +31). Training A started 23:38, ~88k positions/s (19 min an epoch). A
   container restart at ~03:40 on 22 Oct stopped it in epoch 14; the queue
   was restarted and resumed from the epoch-13 checkpoint. **A done 05:23:
   validation 0.00909 (epoch 1) -> 0.00837 (epoch 20)**; net-20 loads and
   evaluates sensibly. B-mix started 05:23 (~8 h); another container
   restart (~09:30) resumed it from epoch 10. **B done 13:18: validation
   0.00813** (its validation set includes old-data positions, so not
   comparable with A's). Built with `-DENGINE_NET=<net>`:
   `~/engines/engine2-2.7-{A-new,B-mix}` (bench 2801732 / 2933981).
   **B-mix vs A-new: H1 after 1,734 games, +579 -498 =657 (52.3%), +16 Elo
   (+/- 12)**: keeping the old 2.3 data helps (owner had asked whether to
   drop it). **B-mix vs 2.7: H1 after 242 games, +116 -45 =81 (64.7%),
   +105 Elo (+/- 33)** (cloud, 8+0.08, 4 at a time, adjudication). Release
   candidate, **released as 2.8** (`nets/engine-2.8.nnue`). Data stays in
   `~/nnue-data/` (old 50M) and `~/nnue-data/v2/` (new 100M) on this cloud
   machine only. Next ideas: a 256-unit net on the same 150M (engine
   change: HIDDEN), more data from 2.8, retuning pruning margins for NNUE.
10. **256 hidden units (owner's go, 22 Oct ~19:00 UTC).** Size is now an
   option: `train.py --hidden 256`, engine `-DENGINE_NNUE_HIDDEN=256` (the
   default stays 128 until a 256 net is released; then change the default
   in `src/accumulator.h`, which the WebAssembly build also uses). Training
   on the cloud CPU: `~/nnue-nets/run3/queue.sh` (detached, restartable,
   log `queue.log`), out `~/nnue-nets/run3/C-256`, same 150M positions as
   2.8, 20 epochs. Then SPRT vs 2.8 (build with both -D options). If it
   passes clearly, try 512 (likely with more data), then king buckets.
   Trainer note: the first minutes of a run read the data from disk (looked
   like 27k positions/s); cached it is ~80-90k/s at 256. A sparse first
   layer (embedding_bag) gave identical results but was slower on this CPU
   (59k vs 89k/s), so the trainer stays dense. Two more container restarts
   (resumed from epochs 10 and 19). **Done 23 Oct ~04:05: validation
   0.00803** (2.8's run: 0.00813). Binary `~/engines/engine2-2.8-256`
   (bench 3300701), all tests pass at 256. **vs 2.8: H1 after 760 games,
   +263 -208 =289 (53.6%), +25 Elo (+/- 16)**. **Released as 2.9**
   (default size now 256). Next: 512 units (probably with more data from
   2.9), or king buckets.
   data from 2.6 (datagen at a fixed node count is ~1.3x faster now), a
   256-unit net, retuning the pruning margins, the parked TT and
   continuation-history changes.

11. **Data run 3 (Oct 24, 17:18 UTC, this session's cloud machine):**
   **200M** positions (owner raised it from 100M) from **Engine 3.0** (bench 3044298) with **book
   openings** (owner's choice, for more varied games): `datagen`'s new
   `[book]` argument plays a random line of Stockfish's `8moves_v3`
   (34,700 8-move openings, converted by `scripts/nnue/book_to_uci.mjs`
   into `~/nnue-data/8moves_v3.uci`), then 2-5 random plies. 5,000 nodes a
   move, 4 x 50M into `~/nnue-data/v3/part-1..4.bin` (the first start
   aimed at 25M each; `run.sh` now asks for 50M), binary
   `~/engines/engine2-3.0-datagen`, ~8.5M positions an hour (~24 h).
   **Resume with `~/nnue-data/v3/run.sh`** (detached): it takes the next
   seed block itself (7101-7104, 7201-7204, ...). A 200k sample: 99.1%
   distinct (random plies: 99.2%; too small to show duplicates), results
   B/D/W 33/36/32%. The old 150M (2.3 and 2.7 data) is on the other
   session's machine, not here: plan to train on the new data alone here
   (2.7's A-new vs B-mix suggests mixing old data is worth ~+16).

**Side project, owner's request (Oct 2026): Engine 2 in the phone app.**
**Done (WebAssembly):** the app (`engine-1/index.html`, `engine-1/app/`)
plays and analyses with Engine 2 by default; Engine 1.6 stays selectable
under "Engine" in the New game sheet. Pieces:
- `src/wasm.cpp`: entry points (init, new game, go, bench); the search runs
  on the calling thread (the app's Web Worker). Built by
  `scripts/build_wasm.sh` (Emscripten in `~/tools/emsdk`) into
  `engine-1/app/engine2/`, committed. **Rebuild from the best accepted
  build only** (now 3.0, bench 3044298; built with `-msimd128` since 2.6): check with
  `node scripts/wasm_bench.mjs`, whose node count must match the native
  bench. About 85% of native speed.
- `engine-1/app/engine2-worker.js` (same messages as Engine 1.6's worker,
  Engine 1.6's opening book in games), `engine-1/app/uci-info.js` (UCI
  info lines to the app's analysis format, tested in
  `engine-1/test/uci-info.test.js`), `EngineClient(script)`.
- Checked in Chromium with Playwright at phone size: Engine 2 replies within
  its time, analysis reaches depth 17-18 in 4 s, Engine 1.6 still works, no
  console errors.

**App update for 2.7 (owner's requests, Oct 2026; WebAssembly rebuilt from
2.7, APK not built yet — the owner asked to wait):**
- **Home page** on launch (`#home` in `engine-1/index.html`): the New game
  options (the sheet is gone), Continue game, and an Analysis board card.
  The game screen's "Home" button returns to it.
- **Engine version in the labels**, from the build: `build_wasm.sh` writes
  `engine-1/app/engine2/version.js` (`ENGINE2_NAME`, read from
  `src/uci.cpp`'s `ENGINE_NAME`).
- **Analysis board** (`mode` 'analysis' in `app/ui.js`): both sides move,
  analysis always on, panel under the board with **Best move** large, the
  score and the line. "Analyse" in a game opens it on the game's position.
  Reset = standard start. Its moves (and starting FEN) are saved apart from
  the game's.
- **Import FEN or PGN** on the analysis board (`Game.fromText` in
  `app/game.js`, tested in `test/game.test.js`): PGN comments, variations
  and NAGs are skipped, a `[FEN]` tag is honoured, a bad move or FEN is
  reported in the sheet. Games can start from a FEN (`new Game(fen)`,
  scoresheet "3. … Nf6", PGN export with SetUp/FEN). Both workers take a
  `fen`; `engine_go` in `src/wasm.cpp` accepts "fen <FEN> moves ...".
- Checked in Chromium at phone and desktop size (Playwright): play, continue,
  analyse, import FEN/PGN (mate in one found by both engines), errors,
  reload; no console errors.
- **Move navigation on the analysis board:** ⏮ ◀ ▶ ⏭ under the board,
  every scoresheet move tappable (later moves dimmed), arrow keys. Stepping
  back keeps the line (`Game.back/forward/goTo`, `future`); playing the
  line's next move keeps the rest, another move starts a new line. The line
  and current move are saved. Take back is game-only now.
- Android: `versionCode` 4 / `versionName` "2.7".

Next for the app, in order (owner's phone is Android):
1. **Installable web app (PWA):** a manifest and a service worker so it
   installs to the home screen and works offline.
2. **Android APK** with Capacitor wrapping the same web app: the project
   is in `engine-1/mobile/` (Capacitor 8.5.3, app id
   `com.chessengine.app`). `npm run web` copies the page, `app/` and
   `src/` into `www/` and swaps Google Fonts for the bundled copies in
   `mobile/fonts/` (Archivo, Martian Mono; SIL OFL), so it works offline;
   `npm run apk` builds a debug APK into
   `android/app/build/outputs/apk/debug/`. Needs Java (21 here), and the
   Android Gradle plugin and SDK, both from `dl.google.com`, which the
   cloud environment's network policy blocked until the owner allowed it
   (Oct 2026). SDK: `~/tools/android-sdk` (command-line tools, platform
   36; set `sdk.dir` in `android/local.properties`, not committed). The
   first build hit Maven Central rate limits (429): `./gradlew
   --max-workers=1 assembleDebug` got through. **First APK built** (debug,
   4.5 MB, with the best build's engine, bench 3601124) and sent to the
   owner to sideload. **Installed and working on the owner's Android
   phone** (Oct 2026): Engine 2 plays offline on the phone. Rebuild the
   wasm and the APK after each accepted engine release.

For each release: set `ENGINE_NAME` in `src/uci.cpp`, update `CHANGELOG.md`
and this file, and let the owner start the release match. NNUE after these
(training data from Engine 2 self-play; the cloud machine has no GPU).
