"""Health report for training data and for engine network files:

    python scripts/nnue/diagnose.py data/*.bin [--net nets/engine-7.2.nnue ...] [--positions N]

Data (from up to --positions positions, 200k by default):
  - the sigmoid scale K that best turns search scores into game results
    (train.py --k; 400 has been used so far)
  - how many pieces the positions have, and how the king buckets of
    train.py's KING_MAPS would split them: positions per bucket, and how
    many examples each first-layer weight row gets (scaled to a dataset of
    --scale-to positions, 660M by default)

Each network (any version of the engine's file; the weights are read back
as the engine uses them):
  - its evaluation against the positions' search scores, by output bucket:
    mean difference (bias), median absolute difference
  - its predicted win probability, sigmoid(eval / K), against the game
    results, by output bucket
  - the output layer's rounding error: how far the evaluation moves when
    the output weights are put back between their rounding steps (a
    stand-in for the unrounded weights the trainer had); about 6 cp for a
    x64 network, under 1 cp for a fine (x1024) one
  - output weights at the 16-bit limit that x64 SCReLU networks have
"""
import argparse
import struct

import numpy as np
import torch

import train
from data import RECORD


def read_net(path):
    """A network file -> an NNUE model with the engine's (dequantized)
    weights, plus a description. Versions 1-3, activations 0-2."""
    b = open(path, "rb").read()
    magic, version, hidden = struct.unpack_from("<4sII", b, 0)
    if magic != b"E2NN":
        raise SystemExit(f"{path}: not an engine network file")
    pos = 12
    activation, buckets, king_buckets, mirror = 0, 1, 1, 0
    king_map = [0] * 64
    if version >= 2:
        activation, buckets = struct.unpack_from("<II", b, pos)
        pos += 8
    if version >= 3:
        king_buckets, mirror = struct.unpack_from("<II", b, pos)
        pos += 8
        king_map = list(b[pos:pos + 64])
        pos += 64
    rows = train.INPUTS * king_buckets
    ft = np.frombuffer(b, "<i2", rows * hidden, pos).reshape(rows, hidden)
    pos += rows * hidden * 2
    fb = np.frombuffer(b, "<i2", hidden, pos)
    pos += hidden * 2
    ow = np.frombuffer(b, "<i2", buckets * 2 * hidden, pos).reshape(buckets, 2 * hidden)
    pos += buckets * 2 * hidden * 2
    ob = np.frombuffer(b, "<i4", buckets, pos)
    qb = train.QB_FINE if activation == 2 else train.QB
    if king_buckets > 1 and (king_map != train.KING_MAPS.get(king_buckets) or not mirror):
        raise SystemExit(f"{path}: king map differs from train.py's KING_MAPS[{king_buckets}]")
    model = train.NNUE(hidden, activation >= 1, buckets, king_buckets)
    with torch.no_grad():
        model.ft.weight.copy_(torch.from_numpy(ft.astype(np.float32) / train.QA))
        model.ft.bias.copy_(torch.from_numpy(fb.astype(np.float32) / train.QA))
        model.out.weight.copy_(torch.from_numpy(ow.astype(np.float32) / qb))
        model.out.bias.copy_(torch.from_numpy(ob.astype(np.float64) / (train.QA * qb)).float())
    model.eval()
    desc = (f"version {version}, {hidden} hidden units, {['clipped', 'SCReLU', 'SCReLU fine'][activation]}, "
            f"{buckets} output bucket(s), {king_buckets} king bucket(s)")
    return model, desc, qb, ow


def evaluate(model, pieces, stm, count):
    xw, xb = train.features(pieces, "cpu", model.king_buckets, sparse=True)
    out = []
    with torch.no_grad():
        for i in range(0, len(pieces), 50_000):
            s = slice(i, i + 50_000)
            out.append(model(xw[s], xb[s], torch.from_numpy(stm[s]), torch.from_numpy(count[s])).numpy())
    return np.concatenate(out) * train.SCALE


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data", nargs="+")
    ap.add_argument("--net", action="append", default=[], help="a network file to check (repeatable)")
    ap.add_argument("--positions", type=int, default=200_000)
    ap.add_argument("--scale-to", type=float, default=660e6, help="dataset size for the per-row example counts")
    args = ap.parse_args()

    records = np.concatenate([np.fromfile(p, dtype=RECORD)[: (len(np.fromfile(p, dtype=np.uint8)) // 32)]
                              for p in args.data])
    if len(records) > args.positions:
        records = records[np.sort(np.random.default_rng(0).choice(len(records), args.positions, replace=False))]
    pieces, stm, score, result = train.decode(records)
    count = (pieces >= 0).sum(axis=1)
    score_stm = np.where(stm == 0, score, -score)
    result_stm = np.where(stm == 0, result / 2, 1 - result / 2)
    bucket8 = np.minimum((count - 2) * 8 // 32, 7)

    print(f"== data: {len(records):,} positions from {len(args.data)} file(s)")
    keep = np.abs(score) < 2000
    errors = {k: np.mean((1 / (1 + np.exp(-score[keep] / k)) - result[keep] / 2) ** 2) for k in range(150, 801, 10)}
    best = min(errors, key=errors.get)
    print(f"best K {best} (score -> result squared error {errors[best]:.5f}; at K 400 {errors[400]:.5f})")
    print("pieces: " + ", ".join(f"{lo}-{hi}: {100 * np.mean((count >= lo) & (count <= hi)):.0f}%"
                                 for lo, hi in ((2, 9), (10, 17), (18, 25), (26, 32))) + f"; mean {count.mean():.1f}")
    scale = args.scale_to / len(records)
    for kb in (1, 4, 8):
        w, b = train.feature_indices(pieces, "cpu", kb)
        rows = train.INPUTS * kb
        hits = torch.bincount(torch.cat([w.flatten(), b.flatten()]), minlength=rows + 1)[:rows].numpy()
        used = hits[hits > 0] * scale
        line = f"{kb} king bucket(s): examples per row in {args.scale_to / 1e6:.0f}M, median {np.median(used):,.0f}, " \
               f"10th percentile {np.percentile(used, 10):,.0f}, rows under 1M {int((used < 1e6).sum())}"
        if kb > 1:
            king = torch.argmax((torch.from_numpy(pieces) == 5).to(torch.int8), dim=1).numpy()
            share = np.bincount(np.array(train.KING_MAPS[kb])[king], minlength=kb) / len(king)
            line += "; White's positions per bucket " + " ".join(f"{100 * x:.0f}%" for x in share)
        print(line)

    for path in args.net:
        model, desc, qb, ow = read_net(path)
        ev = evaluate(model, pieces, stm, count)
        print(f"\n== {path}: {desc}")
        print("pieces  positions  eval-search mean  |eval-search| median  results - predicted (K 400)")
        for k in range(8):
            m = (bucket8 == k) & (np.abs(score_stm) < 1500)
            if m.sum() < 200:
                continue
            d = ev[m] - score_stm[m]
            pred = 1 / (1 + np.exp(-ev[m] / 400))
            print(f"{2 + 4 * k:2d}-{min(5 + 4 * k, 32):2d}   {m.sum():9,d}  {d.mean():+15.1f}  {np.median(np.abs(d)):20.1f}"
                  f"  {100 * (result_stm[m].mean() - pred.mean()):+20.1f}%")
        rng = np.random.default_rng(1)
        with torch.no_grad():
            saved = model.out.weight.clone()
            model.out.weight.add_(torch.from_numpy(rng.uniform(-0.5, 0.5, saved.shape).astype(np.float32) / qb))
        moved = np.abs(evaluate(model, pieces[:20000], stm[:20000], count[:20000]) - ev[:20000])
        with torch.no_grad():
            model.out.weight.copy_(saved)
        print(f"output rounding error (x{qb}): mean {moved.mean():.2f} cp, 99th percentile {np.percentile(moved, 99):.1f} cp")
        if qb == train.QB and model.screlu:
            print(f"output weights at the +/-127 limit: {int((np.abs(ow) >= 126).sum())} of {ow.size}")


if __name__ == "__main__":
    main()
