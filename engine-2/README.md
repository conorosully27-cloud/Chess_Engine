# Engine 2

New session? Start with [`HANDOFF.md`](HANDOFF.md).

C++20 engine (bitboards, magic bitboards, later NNUE). Built with CMake; the
goal is maximum playing strength. Engine 1 (JavaScript, up to version 1.6, in
`../engine-1`) is the baseline it is measured against.

## Status

| Step | State |
| --- | --- |
| 1. Position: bitboards, legal move generation, make/unmake, Zobrist, FEN | done, perft matches the standard counts |
| 2. UCI front end | done |
| 3. Search and transposition table, evaluation ported from Engine 1 | done: **Engine 2.0**, same algorithms as 1.6, about 6x faster, +372 Elo over 1.6 |
| 4. Speed and search improvements (2.1: speed; 2.2: SEE) | in progress; see HANDOFF.md |
| 5. NNUE evaluation | after that |

## Build and test

Needs a C++20 compiler (gcc 10+, clang 12+) and CMake 3.16+. Ninja is optional.

```sh
cd engine-2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build          # perft, position, search and UCI tests, a few seconds
./build/perft_test full         # deeper perft counts, about 6 s
./build/engine2 perft 6         # node count from the start position
./build/engine2 divide 5 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
./build/engine2 bench           # fixed-depth search: node signature and speed
```

On Windows (PowerShell) the executables are `build\engine2.exe` and so on.
`-DENGINE_AVX2=OFF` builds without AVX2 for older CPUs.

## Cute Chess

Run `engine2` with no arguments and it speaks UCI. In Cute Chess add an engine
with Command `C:\...\engine-2\build\engine2.exe`, Working Directory
`C:\...\engine-2\build`, Protocol UCI. No `.bat` file or Node.js is needed.
Rebuild (`cmake --build build`) after each `git pull`.

Options: `Hash` (transposition table size in MB, default 64) and `Move
Overhead` (ms kept spare per move for GUI and system lag, default 50).
Besides the standard commands it understands `d` (show the board, FEN and
hash), `eval` (static evaluation), `perft <depth>` (per-move node counts) and
`bench [depth]` for debugging.

## Matches

`scripts/match.sh` plays engine-vs-engine matches with
[fastchess](https://github.com/Disservin/fastchess), the match runner
Stockfish's testing uses, on the openings in `engine-1/test/openings.epd`
(each played with both colors):

```sh
scripts/match.sh build/engine2 engine1            # 200 games vs Engine 1.6 at 8+0.08
SPRT=1 scripts/match.sh new/engine2 old/engine2   # SPRT between two builds
```

Release matches use `scripts/release_match.sh`, which builds both versions
from their git commits and runs a restartable SPRT (`start`, `resume`,
`status`, `checkpoint`; see the script's header). Checkpoints of a match in
progress are kept in `release-matches/` so it can continue even after the
cloud machine is reset.

fastchess is not in the repo; build it once with
`git clone https://github.com/Disservin/fastchess ~/tools/fastchess && make -C ~/tools/fastchess -j`.
Results and PGNs go to `matches/` (git-ignored); accepted changes and their
measured Elo go in `CHANGELOG.md`.

## Layout

- `src/types.h`: colors, pieces, squares, the 16-bit `Move`.
- `src/bitboard.*`: attack tables, magic bitboards (generated at startup with
  fixed seeds), between/line tables.
- `src/position.*`: board state, FEN, make/unmake (incl. null move), Zobrist
  hash, check and pin info, legality test, repetition and draw rules.
- `src/movegen.*`: pseudo-legal and legal move generation, UCI move parsing, perft.
- `src/evaluate.*`: Engine 1.6's hand-tuned evaluation, ported term for term
  (identical scores; checked against 500 recorded Engine 1.6 evaluations).
- `src/tt.*`: transposition table.
- `src/search.*`: Engine 1.6's search (iterative deepening, aspiration windows,
  PVS, quiescence, null move, reverse futility, futility and late move
  pruning, LMR, killers and history), time allocation, and the search thread.
- `src/bench.cpp`: `bench`, fixed-depth searches over a set of positions.
- `src/uci.*`: the UCI loop; `stop` takes effect immediately since the search has its own thread.
- `tests/`: perft against the standard positions; FEN round trips, hash and
  make/unmake consistency over 200k random-playout moves, castling, en passant,
  repetition and draw rules; time allocation; evaluation vs Engine 1.6;
  mates and tactics; `uci_test.py` drives the binary
  over UCI like a GUI (needs Python 3, skipped if missing).

Call `init_all()` once at startup, before creating any `Position`.
