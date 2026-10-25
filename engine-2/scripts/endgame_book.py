#!/usr/bin/env python3
"""Endgame starting positions from the NNUE training data, for endgame-only
matches (scripts/match.sh with BOOK=<file>):

    python3 scripts/endgame_book.py ~/nnue-data/v2/part-1.bin --out matches/endgames.epd
    python3 scripts/endgame_book.py ~/nnue-data/v2/part-1.bin --rules --out matches/endgame-rules.epd

Default: endgames with at most --pieces pieces besides kings and pawns
(default 3) and a search score within --max-score centipawns of level
(default 200), so that the game is still to be decided. --rules instead
picks positions where one of src/endgame.cpp's rules applies to the side
the score favours (a material edge without pawns, opposite-coloured
bishops, a rook pawn the defending king holds), whatever the score. Each
distinct position once, --count of them (default 4000), sampled evenly.
"""
import argparse
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "nnue"))
import data  # noqa: E402

VALUES = {1: 320, 2: 330, 3: 500, 4: 900}  # knight, bishop, rook, queen
LIGHT = 0x55AA55AA55AA55AA


def piece_counts(records, chunk=1 << 20):
    """(N, 12) counts of each piece (PNBRQKpnbrqk) per position."""
    if len(records) > chunk:
        return np.concatenate([piece_counts(records[i:i + chunk]) for i in range(0, len(records), chunk)])
    occ = np.ascontiguousarray(records["occupancy"])
    nbits = np.unpackbits(occ.view(np.uint8).reshape(-1, 8), axis=1).sum(axis=1)
    nib = np.empty((len(records), 32), dtype=np.uint8)
    nib[:, 0::2] = records["pieces"] & 15
    nib[:, 1::2] = records["pieces"] >> 4
    valid = np.arange(32)[None, :] < nbits[:, None]
    return np.stack([((nib == p) & valid).sum(axis=1) for p in range(12)], axis=1)


def rule_applies(squares, white_ahead):
    """Whether one of src/endgame.cpp's rules scales this position down."""
    strong, weak = (0, 6) if white_ahead else (6, 0)  # piece index offsets
    def count(off, t):
        return sum(1 for p in squares if p == off + t)
    def material(off):
        return sum(VALUES[t] * count(off, t) for t in VALUES)
    s_pawns, w_pawns = count(strong, 0), count(weak, 0)
    s_mat, w_mat = material(strong), material(weak)
    if not s_pawns and s_mat - w_mat <= 330:
        return True
    others = sum(count(o, t) for o in (0, 6) for t in (1, 3, 4))
    sb = [sq for sq, p in enumerate(squares) if p == strong + 2]
    wb = [sq for sq, p in enumerate(squares) if p == weak + 2]
    if not others and len(sb) == 1 and len(wb) == 1 and bool(LIGHT >> sb[0] & 1) != bool(LIGHT >> wb[0] & 1):
        return True
    if s_pawns and not w_pawns and not w_mat and not sum(count(strong, t) for t in (1, 3, 4)) and len(sb) <= 1:
        files = {sq % 8 for sq, p in enumerate(squares) if p == strong}
        if files in ({0}, {7}):
            queening = (56 if white_ahead else 0) + (0 if files == {0} else 7)
            covers = sb and bool(LIGHT >> sb[0] & 1) == bool(LIGHT >> queening & 1)
            king = next(sq for sq, p in enumerate(squares) if p == weak + 5)
            if not covers and max(abs(king % 8 - queening % 8), abs(king // 8 - queening // 8)) <= 1:
                return True
    return False


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data", nargs="+", help="training data files (src/datagen.h format)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--count", type=int, default=4000)
    ap.add_argument("--pieces", type=int, default=3)
    ap.add_argument("--max-score", type=int, default=200)
    ap.add_argument("--rules", action="store_true", help="positions where an endgame rule applies")
    args = ap.parse_args()

    fens = {}
    for path in args.data:
        records = data.load(path)
        counts = piece_counts(records)
        pieces = counts[:, [1, 2, 3, 4, 7, 8, 9, 10]].sum(axis=1)
        if args.rules:
            # Cheap first cut: a side without pawns, bishops only, or a bare king.
            no_pawns = (counts[:, 0] == 0) | (counts[:, 6] == 0)
            bishops_only = (counts[:, 2] == 1) & (counts[:, 8] == 1) & (counts[:, [1, 3, 4, 7, 9, 10]].sum(axis=1) == 0)
            bare = (counts[:, 0:5].sum(axis=1) == 0) | (counts[:, 6:11].sum(axis=1) == 0)
            keep = np.nonzero((pieces <= 4) & (no_pawns | bishops_only | bare))[0]
        else:
            keep = np.nonzero((pieces <= args.pieces) & (np.abs(records["score"].astype(int)) <= args.max_score))[0]
        for i in keep:
            r = records[i]
            if args.rules and not (r["score"] != 0 and rule_applies(data.board(r), r["score"] > 0)):
                continue
            fens.setdefault(data.fen(r), None)
        print(f"{path}: {len(keep):,} candidates, {len(fens):,} distinct so far", file=sys.stderr)

    chosen = list(fens)
    if len(chosen) > args.count:
        step = len(chosen) / args.count
        chosen = [chosen[int(i * step)] for i in range(args.count)]
    with open(args.out, "w") as f:
        for fen in chosen:
            f.write(fen.rsplit(" ", 2)[0] + "\n")  # EPD: no move counters
    print(f"wrote {len(chosen):,} positions to {args.out}", file=sys.stderr)


if __name__ == "__main__":
    main()
