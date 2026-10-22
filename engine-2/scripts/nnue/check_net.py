"""Checks that the engine's NNUE evaluation (integer) matches the trainer's
(floating point) on positions from a datagen file:

    python scripts/nnue/check_net.py nets/checkpoint.pt nets/net-20.nnue data.bin build/engine2

The two should agree to within a few centipawns on average: the engine's
weights are rounded to whole numbers (first layer x255, output layer x64),
which costs a few centipawns, up to about 15 on the odd position. A bug
shows as differences of hundreds.
"""
import subprocess
import sys

import numpy as np
import torch

import data
from train import NNUE, SCALE, decode, features


def main():
    checkpoint, net, datafile, engine = sys.argv[1:5]
    count = int(sys.argv[5]) if len(sys.argv) > 5 else 2000
    records = data.load(datafile)
    idx = np.random.default_rng(7).choice(len(records), count, replace=False)
    records = records[np.sort(idx)]

    state = torch.load(checkpoint, map_location="cpu")["model"]
    model = NNUE(state["ft.weight"].shape[0])  # hidden units, from the checkpoint
    model.load_state_dict(state)
    model.eval()
    pieces, stm, _, _ = decode(records)
    xw, xb = features(pieces, "cpu")
    with torch.no_grad():
        float_eval = (model(xw, xb, torch.from_numpy(stm)) * SCALE).numpy()

    fens = "\n".join(data.fen(r) for r in records) + "\n"
    out = subprocess.run([engine, "eval", net], input=fens, capture_output=True, text=True, check=True).stdout.split()
    engine_eval = np.array([int(x) for x in out], dtype=np.float64)
    diff = np.abs(engine_eval - float_eval)
    print(f"{count} positions: engine vs trainer, mean |diff| {diff.mean():.2f} cp, max {diff.max():.1f} cp, "
          f"99th percentile {np.percentile(diff, 99):.1f} cp; eval spread (std) {float_eval.std():.0f} cp")
    worst = int(np.argmax(diff))
    print(f"largest gap: {data.fen(records[worst])}: engine {engine_eval[worst]:.0f}, trainer {float_eval[worst]:.1f}")
    return 0 if diff.mean() <= 5 and diff.max() <= 25 else 1


if __name__ == "__main__":
    sys.exit(main())
