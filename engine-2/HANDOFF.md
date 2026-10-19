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
- **Tests:** `ctest --test-dir build`, 5 suites (perft, position, SEE,
  search, UCI), a few seconds. **Bench signature: 6533990** (`build/engine2 bench`;
  2.1 was 9557353, 2.0 9559579), the same on Linux/gcc and Windows/MinGW.
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
      **Under SPRT** vs the best build, cloud, 4 games at a time,
      adjudication on, log `~/matches/best-tt2.out`. (A 4-entry, 48-byte
      bucket straddled cache lines: 13% fewer nodes/s, no faster.)
   3. Store the static evaluation, and reuse it instead of re-evaluating.
7. **"Improving" flag** (static eval better than two plies ago) to tighten
   reverse futility, futility and late move pruning when not improving.
8. **Continuation history** (countermoves failed in 1.6 as a plain table),
   then **history pruning** of quiet moves with very bad history at low
   depth.
9. **Singular extensions** (needs the TT work first).
10. **Razoring, ProbCut**: small gains, later.
11. **SEE with tuned piece values** (owner's parked idea): phase-blended
    material from the evaluation instead of 100/325/325/500/900; the tuned
    middlegame values rate a queen above two rooks.
12. **Lazy SMP (threads)**: big gain on the owner's laptop in Cute Chess;
    hard to test here with 3 of 4 cores in use. Do last, confirm on the
    laptop.

**Side project, owner's request (Oct 2026): Engine 2 in the phone app.**
The app (`engine-1/index.html`, `engine-1/app/`) still plays Engine 1.6,
the JS engine, run in a Web Worker (`app/engine-worker.js`). Plan: compile
Engine 2 to WebAssembly and let the app use it. Start when a test slot
frees up: installing Emscripten and compiling are heavy, so not while all 4
cores run matches.
1. Install the Emscripten SDK (outside the repo, like fastchess).
2. A WebAssembly build target in `CMakeLists.txt` (no AVX2; no threads:
   `Searcher` runs the search on a `std::thread`, so the wasm entry point
   calls `SearchWorker::search` directly, synchronously, inside the worker).
   A small C entry point takes "position ... / go movetime N" and reports
   info lines and the best move back to JavaScript.
3. `app/engine2-worker.js` speaking the same messages as `engine-worker.js`
   (`analyse` / `play` with the move history and time), translating to UCI
   and parsing `info` lines into the app's format. Stopping works as now:
   the page ends the worker. Keep Engine 1.6's opening book (`bookMove`)
   for variety in play mode.
4. A setting to pick the engine (1.6 or 2.x), defaulting to 2.x once it
   is checked; Engine 1's code itself stays frozen.
5. Tests: a Node test running the wasm engine on a few positions (bench
   node count must match the native build at the same depth), the app's
   existing tests, and a look in a real browser (Playwright, Chromium is
   installed here) before showing the owner.

For each release: set `ENGINE_NAME` in `src/uci.cpp`, update `CHANGELOG.md`
and this file, and let the owner start the release match. NNUE after these
(training data from Engine 2 self-play; the cloud machine has no GPU).
