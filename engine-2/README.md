# Engine 2

C++20 engine (bitboards, magic bitboards, later NNUE). Built with CMake; the
goal is maximum playing strength. Engine 1 (JavaScript, up to version 1.6, in
`../engine-1`) is the baseline it is measured against.

## Status

| Step | State |
| --- | --- |
| 1. Position: bitboards, legal move generation, make/unmake, Zobrist, FEN | done, perft matches the standard counts |
| 2. UCI front end | done; plays a random legal move until step 3 |
| 3. Search and transposition table, evaluation ported from Engine 1 | next |
| 4. NNUE evaluation | |
| 5. Search refinements and tuning | |

## Build and test

Needs a C++20 compiler (gcc 10+, clang 12+) and CMake 3.16+. Ninja is optional.

```sh
cd engine-2
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build          # perft, position, search and UCI tests, about 1 s
./build/perft_test full         # deeper perft counts, about 6 s
./build/engine2 perft 6         # node count from the start position
./build/engine2 divide 5 "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"
./build/engine2 bench           # perft speed
```

On Windows (PowerShell) the executables are `build\engine2.exe` and so on.
`-DENGINE_AVX2=OFF` builds without AVX2 for older CPUs.

## Cute Chess

Run `engine2` with no arguments and it speaks UCI. In Cute Chess add an engine
with Command `C:\...\engine-2\build\engine2.exe`, Working Directory
`C:\...\engine-2\build`, Protocol UCI. No `.bat` file or Node.js is needed.
Rebuild (`cmake --build build`) after each `git pull`.

Options: `Move Overhead` (ms kept spare per move for GUI and system lag,
default 50). Besides the standard commands it understands `d` (show the board,
FEN and hash) and `perft <depth>` (per-move node counts) for debugging.

## Layout

- `src/types.h`: colors, pieces, squares, the 16-bit `Move`.
- `src/bitboard.*`: attack tables, magic bitboards (generated at startup with
  fixed seeds), between/line tables.
- `src/position.*`: board state, FEN, make/unmake (incl. null move), Zobrist
  hash, check and pin info, legality test, repetition and draw rules.
- `src/movegen.*`: pseudo-legal and legal move generation, UCI move parsing, perft.
- `src/search.*`: search thread and time allocation (the search itself is a placeholder).
- `src/uci.*`: the UCI loop; `stop` takes effect immediately since the search has its own thread.
- `tests/`: perft against the standard positions; FEN round trips, hash and
  make/unmake consistency over 200k random-playout moves, castling, en passant,
  repetition and draw rules; time allocation; `uci_test.py` drives the binary
  over UCI like a GUI (needs Python 3, skipped if missing).

Call `init_all()` once at startup, before creating any `Position`.
