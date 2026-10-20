"""Reads the NNUE training data written by `engine2 datagen` (format in
src/datagen.h: 32 bytes a position). Run as a script to summarise a file:

    python scripts/nnue/data.py data.bin [--fens N]
"""
import sys

import numpy as np

RECORD = np.dtype([
    ("occupancy", "<u8"),
    ("pieces", "u1", (16,)),
    ("score", "<i2"),
    ("result", "u1"),
    ("stm", "u1"),
    ("reserved", "u1", (4,)),
])
assert RECORD.itemsize == 32

PIECE_CHARS = "PNBRQKpnbrqk"


def load(path, count=None, offset=0):
    """Positions from `path` as a numpy record array (a trailing partial
    record, from a generator stopped mid-write, is ignored)."""
    data = np.fromfile(path, dtype=np.uint8)
    usable = len(data) - len(data) % RECORD.itemsize
    records = data[:usable].view(RECORD)
    end = len(records) if count is None else min(len(records), offset + count)
    return records[offset:end]


def board(record):
    """The 64 squares (a1 = 0) as piece indices 0-11, or -1 if empty."""
    squares = [-1] * 64
    occ = int(record["occupancy"])
    i = 0
    while occ:
        sq = (occ & -occ).bit_length() - 1
        occ &= occ - 1
        byte = int(record["pieces"][i // 2])
        squares[sq] = (byte >> (4 * (i % 2))) & 15
        i += 1
    return squares


def fen(record):
    squares = board(record)
    rows = []
    for rank in range(7, -1, -1):
        row, empty = "", 0
        for file in range(8):
            p = squares[rank * 8 + file]
            if p < 0:
                empty += 1
            else:
                row += (str(empty) if empty else "") + PIECE_CHARS[p]
                empty = 0
        rows.append(row + (str(empty) if empty else ""))
    return "/".join(rows) + (" w" if record["stm"] == 0 else " b") + " - - 0 1"


def summary(path, fens=0):
    records = load(path)
    n = len(records)
    print(f"{path}: {n:,} positions ({n * RECORD.itemsize / 1e9:.2f} GB)")
    if n == 0:
        return
    results = np.bincount(records["result"], minlength=3)
    print("results (White's side): loss %.1f%%, draw %.1f%%, win %.1f%%" % tuple(100 * results / n))
    scores = records["score"].astype(np.int32)
    print("score: mean %.0f, |score| median %.0f, 95th percentile %.0f" % (
        scores.mean(), np.median(np.abs(scores)), np.percentile(np.abs(scores), 95)))
    print("side to move: %.1f%% White" % (100 * np.mean(records["stm"] == 0)))
    pieces = np.array([bin(int(o)).count("1") for o in records["occupancy"][:100000]])
    print("pieces on the board: mean %.1f, min %d, max %d" % (pieces.mean(), pieces.min(), pieces.max()))
    for r in records[:fens]:
        print(fen(r), int(r["score"]), int(r["result"]))


if __name__ == "__main__":
    fens = int(sys.argv[sys.argv.index("--fens") + 1]) if "--fens" in sys.argv else 0
    summary(sys.argv[1], fens)
