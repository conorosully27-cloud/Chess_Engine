# Handoff

Start here when picking up this project in a new session. It covers where
things stand, how to set up, how the owner likes to work, and what to do
next. Last updated 2026-10-17, at Engine 1.6 (UCI front end added).

Companion files: `ENGINE_CHANGELOG.md` (every accepted change and its Elo
gain, plus results against chess.com bots), `FAILED_EXPERIMENTS.md` (every
rejected change, with how promising it looked and why it failed), `README.md`
(the app and tools), `games/` (PGNs of notable games), `docs/cutechess-results.md` (Cute Chess
results against Stockfish).

## Where things stand

- **Engine 1.6** is current, on `main` (commit `1576297` is the 1.6
  release; `ENGINE_VERSION` in `src/engine/index.js`). About +217 Elo over
  1.0:
  - 1.1 (`19aa147`): all evaluation weights Texel-tuned, +18 Elo over 1.0.
  - 1.2 (`5a42226`): tuned endgame terms (king distance to passed pawns,
    blocked passers, rooks behind passers, rook on the seventh, rule of the
    square), +44 Elo over 1.1.
  - 1.3 (`b902214`): king safety (king danger, pawn storms, open files at
    the king) and an evaluation cache, +28 Elo over 1.2.
  - 1.4 (`9d16dc3`): speed (incremental piece-square sums, cheaper legality
    test, lazy evaluation, bad-capture skip in quiescence), +46 Elo over 1.3.
  - 1.5 (`648648e`): late move pruning, +73 Elo over 1.4.
  - 1.6 (`1576297`): history penalty for failed quiet moves (move
    ordering), +8 Elo over 1.5, not significant (released on the owner's
    call to close 1.x before NNUE).
- **Engine 1.0** is frozen on the `engine-1.0` branch (on GitHub). 1.1 to
  1.6 have no branches of their own; use their commits.
- **The phone app** is a private claude.ai artifact the owner plays on:
  https://claude.ai/artifact/PT7svYxyEeGvaDLofYkmBL (runs Engine 1.6).
- **Tests:** `npm test`, 105 tests, about 7 seconds, all passing.
- **Outside results:** beat the chess.com 2200 and 2600 bots; lost twice to
  the 2900 bot (details in `ENGINE_CHANGELOG.md`, games in `games/`).
- **Cute Chess / UCI:** `uci.js` (with `engine.bat` for Windows) runs the
  engine over UCI, so any UCI GUI can use it. Against Stockfish with
  `UCI_Elo` 2550 at about 1 s per move, 200 games scored 92 - 86 - 22
  (+10 +/- 46 Elo); with Stockfish's time losses removed that is about
  -5 Elo, so **Engine 1.6 is about 2550 on Stockfish's `UCI_Elo` scale**
  (not a rating-list Elo). Full output and a 2700 run in
  `docs/cutechess-results.md`. Work is on branch
  `claude/zen-hamilton-x7adgd`, PR
  [conorosully27-cloud/Chess_Engine#1](https://github.com/conorosully27-cloud/Chess_Engine/pull/1).

## Setting up a new session

```sh
npm test                                       # should pass 105 tests
git worktree add ../engine-1.6 1576297         # baseline to measure against
git worktree add ../engine-1.0 engine-1.0      # optional, the original

# Tuning data (25 MB, not in the repo; BSD 3-Clause, The Zurichess Authors)
mkdir -p ~/data && curl -sSL -o ~/data/quiet-labeled.v7.epd.gz \
  https://bitbucket.org/zurichess/tuner/downloads/quiet-labeled.v7.epd.gz
```

The cloud machine has 4 cores. A match with `--threads 3` uses them; two
matches side by side should use `--threads 2` each, or timed searches get
starved and results skew.

## Testing in Cute Chess (UCI)

- **Wrapper:** `uci.js` handles `uci`, `isready`, `ucinewgame`,
  `setoption` (`OwnBook`, default off), `position` and `go`
  (`movetime`, `wtime`/`btime`/`winc`/`binc`/`movestogo`, `depth`, `nodes`).
  The search is synchronous, so `stop` is only noticed after the current
  search; `go infinite` is capped at 5 s. Time per move leaves at least
  100 ms (or 10%) spare for `movetime`, and 100 ms spare on clock games.
- **Setup on the owner's Windows PC:** clone the repo outside
  `C:\Users\conor\ClaudeNShit` (that folder has a `.git` of its own that
  also holds unrelated projects and `API KEY.txt`; never run `git add .`
  there). In Cute Chess add the engine with Command `engine.bat`, working
  directory the repo, protocol UCI. Node.js must be on the PATH.
- **Tournament settings that worked:** Round Robin, 100 rounds, 2 games per
  encounter, play each opening once, swap sides on, random opening order,
  `test/openings.epd`, 1 s per move, Margin 250-300 ms, concurrency at most
  half the cores, Stockfish `Move Overhead` about 100. PGN output to a file
  outside the repo.
- **Time losses:** the first run lost 33 of 200 games on time on both sides
  (1 s per move with Margin 0). Raise Margin and cut concurrency rather than
  trusting such a run; the 2550 run had 9, all Stockfish's.
- **Not yet done:** nobody has looked through the PGNs. Run 1 (vs 2700)
  had 6 fifty-move draws and 2 stalemates, worth checking for endgame
  conversion problems.

## How the owner works (follow these)

- **The owner is a strong chess player** who tests by playing, including
  relaying moves against chess.com bots. They appreciate chess explanations
  of what the engine does, short status updates, and honest reads of noisy
  results ("early leads fade" has happened every time).
- **The only goal is beating the current best engine** (by SPRT). Games
  against chess.com bots are background, not a target: don't tune or judge
  changes by how they play in those games.
- **Testing a change:** SPRT against the current version, from the varied
  openings:
  `npm run match -- --b ../engine-1.6 --fens test/openings.epd --sprt 0,10 --games 2000`.
  Accept only on "H1 accepted". Use `--fens test/endgames.epd` (400 games,
  fixed length) as an extra check for endgame changes.
- **The owner starts the final full test** of a release candidate against
  the previous version. Get the candidate ready (tested, committed), then
  stop and tell them; don't start that match yourself.
- **Releases:** set `ENGINE_VERSION` and its comment (what changed, Elo
  gain) in `src/engine/index.js`, update the README Engine section and this
  file, add a row to `ENGINE_CHANGELOG.md`. Keep older versions reachable.
- **Every rejected change goes in `FAILED_EXPERIMENTS.md`:** what changed,
  the commit, the results, a prior rating out of 10, and commentary on why
  it failed. Newest at the top.
- **Git:** work on the session's assigned branch and push there; merge to
  `main` only when the owner asks (it has always been a fast-forward).
- **Keep the session awake during matches:** the cloud session sleeps a
  while after Claude's turn ends unless a tracked background task is
  running, and sleeping kills detached matches (this killed five tests).
  Run a watcher beside every match: a Bash `run_in_background` loop that
  waits until the log says "accepted" or the match process is gone. It
  keeps the session awake for up to 2 hours and can't be re-armed after,
  so plan tests to decide within about 2 hours (more threads, fewer time
  controls at once), or expect the owner's messages to keep it awake.
- **Long matches:** start them detached (`setsid nohup node scripts/match.js
  ... > log 2>&1 < /dev/null &`), since tracked background tasks stop after
  2 hours, and watch the process ID. Label which experiment each log
  belongs to when reporting; the owner was confused once when two
  experiments' logs were mixed up. The owner likes to follow logs in the
  side panel (send them as files when asked).
- **Updating the app:** republish to the same artifact URL. The page is
  `index.html` with the document skeleton stripped (`<!doctype>`, `<html>`,
  `<head>`, the charset and viewport metas, `</head>`, `<body>`, `</body>`,
  `</html>`); compare the published files with the repo and send only the
  changed ones in `files` (unchanged ones are kept). The 1.6 update
  republished `src/engine/search.js` and `index.js`.

## What to do next, in order

### 0. How 1.6 went, and what is left open

1.6 was "the rest of 1.5's search work", each change tested against 1.5:
- History penalty (move ordering): +8 at 2000 games, not significant;
  released as 1.6.
- Countermove heuristic: made ordering worse on statistics; never matched.
- Lazy margin 2 pawns + bad-capture skip for any guard: +4 together with
  the history penalty, left out (`FAILED_EXPERIMENTS.md`).
- Steeper late move reductions: PARKED (100 ms: -10, 300 ms: +13); the
  code is written out in `FAILED_EXPERIMENTS.md` for a recheck at longer
  time controls, best after the NNUE work changes the search's speed.
- Texel retune: nothing to gain (held-back error 0.059835 -> 0.059832).
Small search tweaks have stopped paying at this level; the next step is
Engine 2.0 below.

### 1. Next: Engine 2.0 is NNUE (owner's plan)

Once 1.6 ships, the owner wants to switch the evaluation to an NNUE
network as Engine 2.0. What carries over: rules, move generation, the
whole search, the app, the match/SPRT tooling and opening sets. The
incremental piece-square sums in `Position` (1.4) are a template for the
network's accumulator; 1.6's evaluation can label self-play positions to
train the first network and stays as a fallback behind a switch. What
retires: the handcrafted terms and the Texel tuner. To revisit: lazy
evaluation (assumes material approximates the evaluation) and search
margins tuned to this evaluation's scale. New work: training data,
a trainer (likely Python), fast quantised inference in plain JavaScript
(the phone's speed is the main risk), then SPRT against the best 1.x.

### 2. Where 1.3 left off

How the king safety works, for whoever builds on it: `attackScan()` in
`evaluate.js` makes one pass over every piece's attacks for the attack
maps, mobility and the king-danger features (`DANGER`). Units are summed
with the `kingDanger` weights and cost units² / 720 (middlegame) and
units / 20 (endgame). The tuner fits the square directly (`kind:
'danger'` in `eval-trace.js`, the chain rule in `tune.js`), and
`test/tune.test.js` checks its gradient against finite differences. The
evaluation cache in `evaluate()` is keyed by the full hash.

The attack scan made the evaluation about 26% slower; the cache won back
about 12%, and 1.4's speed work more (see 3).

The repetition fix the previous handoff put first cost Elo in self-play
and was reverted (see `FAILED_EXPERIMENTS.md`).

### 3. More king safety

Ideas not yet in: x-ray attacks (a queen behind a bishop, doubled rooks),
counting our own defenders in the zone, a king-danger term for the
attacker's piece count beyond the zone, tuning `DANGER_MG_DIVISOR`.
Always tune; always SPRT against the current best.

### 4. After 1.4's speed work

1.4 (see `ENGINE_CHANGELOG.md`) made the search about 20-25% faster to the
same depth. Where the time goes now, at depth 9 on the benchmark: the
evaluation about 45% (`attackScan` alone about 25%), move generation about
11%, the legality test about 10%. Ideas not yet tried:
- tune lazy evaluation's margin (3 pawns, a first guess) and widen the
  bad-capture rule (any cheaper defender, not only pawns), each by SPRT;
- a pawn hash table for the pawn-structure terms;
- trying the stored best move before generating the other moves (measured:
  saves only about 12% of main-search move generations, so low value).
Changes that keep the search identical can be checked by node count at a
fixed depth: it must not change.

### 5. Smaller items

- **Over-optimism:** the engine rates balanced openings around +1 (e.g. a
  normal Leningrad Dutch). The tuned `tempo` of 30 is a suspect; it may be
  a data artifact.
- **Rook on the seventh** tuned to -70 in the middlegame (+43 endgame),
  which looks like noise; worth a look when retuning.
- **Not encoded:** Lucena and Philidor rook endings, the principle of two
  weaknesses. Drawish-ending scaling and an exact KPK table were tried and
  were neutral (`FAILED_EXPERIMENTS.md`, code in commits `6701735` and
  `c3ca513`).
- **Weaker play levels** are planned, but ask the owner before starting:
  they want full strength right first.
- **App behaviours to know:** Resign resigns for the human's side (the owner
  used it to end a game the engine had lost, so that PGN said 1-0); the
  engine replies instantly to book moves early in a game; the thinking time
  is read when a new game starts.

## Tools quick reference

| Command | What it does |
|---|---|
| `npm test` | All unit tests, perft and the tuner consistency check |
| `npm run match -- --b <dir> [--fens <file>] [--sprt 0,10] [--games n] [--threads n] [--time ms]` | Engine-vs-engine match between this checkout and `<dir>` |
| `npm run tune -- --data <epd.gz> [--epochs n] [--out file]` | Texel tuning of every weight in `weights.js` |
| `node scripts/make-openings.js` | Regenerates `test/openings.epd` |
| `npm start` | Serves the app locally on port 8000 |

## History in brief

1. Handover step 1 (king shelter only for a castled king, plus a central-king
   penalty): three versions, all neutral or worse.
2. Handover step 2 (development penalties, hand-set): neutral.
3. Texel tuner built; tuning all weights gave Engine 1.1 (+18 Elo).
4. Endgame work: tuned endgame terms gave Engine 1.2 (+44 Elo); draw scaling
   and the KPK table were neutral and left out.
5. Infrastructure: `--fens` match starts, an endgame suite, 1000 varied
   openings, the changelog and the failure log.
6. Owner's chess.com tests found the repetition bug and the king-safety gap.
7. King safety gave Engine 1.3 (+28 Elo); the repetition fix lost Elo in
   self-play and was reverted.
8. Speed work gave Engine 1.4 (+46 Elo); ablation tests showed both lazy
   evaluation and the bad-capture skip pull their weight.
9. Search work for 1.5: late move pruning gave +73 Elo; logarithmic late
   move reductions were neutral at 100 ms (see `FAILED_EXPERIMENTS.md`).
10. 1.6: the history penalty (+8, not significant) closed the 1.x series;
   the owner's next step is NNUE as Engine 2.0.
11. UCI front end (`uci.js`, `engine.bat`) added so the engine runs in Cute
   Chess; measured at about 2550 against Stockfish's `UCI_Elo` scale.
