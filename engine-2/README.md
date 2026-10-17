# Engine 2

C++20 engine (bitboards, magic bitboards, later NNUE). Built with CMake; the
goal is maximum playing strength. Engine 1 (JavaScript, up to version 1.6) is
the baseline it is measured against.

Status: not started. Build order:

1. Position: bitboards, legal move generation, make/unmake, Zobrist, FEN. Gate: perft matches known counts.
2. UCI front end and CMake build.
3. Search and transposition table, with the evaluation ported from Engine 1.
4. NNUE evaluation.
5. Search refinements and tuning.
