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
- **Tests:** `ctest --test-dir build`, 4 suites (perft, position, search,
  UCI), a few seconds. **Bench signature: 9557353** (`build/engine2 bench`;
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
  it: a Bash `run_in_background` loop that waits for "Finished match" (or
  "accepted") in the log. Tracked tasks stop after 2 hours, so plan tests to
  decide within that.

## Plan after 2.0, in order

Each item is one or more SPRT-tested changes.

1. **Speed, released as 2.1** (in progress; bench must not change except
   where noted in `CHANGELOG.md`). Ideas: incremental material and
   piece-square sums in `Position` (the evaluation recounts them every call,
   and `evaluate_material` for lazy eval too); a cheaper evaluation pass
   (pawn hash for the pawn terms, fewer per-square loops in `rook_behind`,
   shelter and storms); staged move generation (TT move first, captures,
   then quiets); avoid generating quiets when the TT move cuts off. Profile
   first (`perf` is not installed; `-pg` or manual timing works).
2. **Static exchange evaluation (SEE)** for capture ordering and quiescence
   pruning, replacing 1.6's "bad capture" pawn-guard check.
3. **Transposition table:** buckets, age-based replacement, store the
   static evaluation, use it in quiescence.
4. **Reductions and ordering:** logarithmic LMR table (1.6's steeper LMR
   was parked; see Engine 1's `FAILED_EXPERIMENTS.md`), "improving" flag,
   continuation history (countermoves failed in 1.6 as a plain table).
5. **More pruning and extensions:** singular extensions, internal iterative
   reductions, razoring.
6. **Lazy SMP (threads):** big gain on the owner's laptop in Cute Chess;
   hard to test here with 3 of 4 cores in use. Do last, confirm on the laptop.

For each release: set `ENGINE_NAME` in `src/uci.cpp`, update `CHANGELOG.md`
and this file, and let the owner start the release match. NNUE after these
(training data from Engine 2 self-play; the cloud machine has no GPU).
