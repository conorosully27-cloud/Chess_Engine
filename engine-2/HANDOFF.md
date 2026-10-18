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
  games at 8+0.08. Its commit is `3a3f237`; keep it as the next baseline.
- **Tests:** `ctest --test-dir build`, 5 suites (perft, position, SEE,
  search, UCI), a few seconds. **Bench signature: 9557353** (`build/engine2 bench`;
  2.0 was 9559579, see the 2.1 notes in `CHANGELOG.md`), the same on
  Linux/gcc and Windows/MinGW.
- **Owner's laptop:** Windows, gcc (WinLibs), CMake, Ninja, AVX2 CPU; builds
  and passes the tests; about 1.8M nodes/s on bench. Cute Chess is set up
  for Engine 1.6 and not yet for Engine 2 (the owner will do that later).
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
2. **Static exchange evaluation (SEE)**, in steps, each tested on its own
   (all on `claude/loving-ramanujan-lewlp6`):
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
      (`9feb847`, bench 6533990). Under SPRT vs step 3 in the cloud.

Outstanding, in priority order (owner's ranking, Oct 2026). Each its own
SPRT against the best build at the time:

3. **Logarithmic LMR**: 1.6's parked steeper version (Engine 1's
   `FAILED_EXPERIMENTS.md`): 1 + ln(depth) ln(move) / 1.75 from the third
   move, one ply less on the PV and for killers, one more for zero
   history, never at the root. **Built** on step 4 (bench 3617582, 45%
   fewer nodes); test binary from step 4 + this patch alone, vs step 4.
4. **Internal iterative reduction**: depth >= 4 with no TT move (not at
   the root) searches a ply shallower (`07cba28`, bench 5320123 on step
   4). **Built**; test vs step 4.
5. **Stronger null move**: reduction 3 + depth/3, more when the static
   eval is well above beta (now a fixed 2-3).
6. **Transposition table**: probe it in quiescence (it isn't now), buckets,
   age-based replacement, store the static evaluation.
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

For each release: set `ENGINE_NAME` in `src/uci.cpp`, update `CHANGELOG.md`
and this file, and let the owner start the release match. NNUE after these
(training data from Engine 2 self-play; the cloud machine has no GPU).
