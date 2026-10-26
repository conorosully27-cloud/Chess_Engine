"""Trains an NNUE evaluation for Engine 2 on data from `engine2 datagen`.

    python scripts/nnue/train.py C:/Users/conor/nnue-data/part-*.bin --out nets

The network (the engine's src/nnue.h must match):
  768 inputs per side, one per (piece kind, square) seen from that side
  (its own pieces first, squares flipped for Black)
  -> --hidden units (256 by default), shared weights for both sides, clipped to [0, 1]
  -> both sides' units, side to move first -> 1 output, the evaluation in
     units of SCALE centipawns.
Loss: squared error between win probabilities, sigmoid(eval / K), and a
blend of the search score's and the game result's (--wdl of the result).

Data are read from memory-mapped files, a batch at a time, so a dataset
larger than memory works. Every epoch saves a checkpoint (resume with
--resume) and an engine network file (net-<epoch>.nnue, format in
export()).
"""
import argparse
import glob
import math
import os
import struct
import time

import numpy as np
import torch

from data import RECORD

HIDDEN = 256  # default (the engine's since 2.9); --hidden changes it (the engine must be built to match)
INPUTS = 768
QA = 255     # first layer: weights and biases times QA, accumulators in [0, QA]
QB = 64      # output weights times QB
SCALE = 400  # output unit, in centipawns
K = 400      # centipawns per unit of the sigmoid that turns scores into win probabilities
FT_CLIP = 1.98  # first-layer weight limit: 32 pieces * 1.98 * QA fits a 16-bit accumulator


class Dataset:
    """All records of the given files, memory-mapped; the last `val_fraction`
    of each file is held out for validation."""

    def __init__(self, paths, val_fraction=0.01):
        self.train, self.val = [], []
        for path in paths:
            size = os.path.getsize(path) // RECORD.itemsize
            records = np.memmap(path, dtype=RECORD, mode="r", shape=(size,))
            split = int(size * (1 - val_fraction))
            self.train.append(records[:split])
            self.val.append(records[split:])
        self.train_sizes = np.array([len(r) for r in self.train])
        self.size = int(self.train_sizes.sum())

    def batches(self, batch_size, rng, chunk=1 << 19, buffer_chunks=32):
        """One epoch of random training batches. Picking each position at
        random from all over the files is far too slow once they no longer fit
        in memory (thousands of scattered disk reads a batch: 13k positions/s
        on 350M), so the data are read in chunks of consecutive positions,
        taken in random order from all files, and shuffled together
        `buffer_chunks` at a time (16M positions, 512 MB). Every position is
        used once an epoch."""
        chunks = [(f, s, min(s + chunk, n)) for f, n in enumerate(self.train_sizes) for s in range(0, n, chunk)]
        order = rng.permutation(len(chunks))
        leftover = np.empty(0, dtype=RECORD)
        for i in range(0, len(order), buffer_chunks):
            parts = [leftover] + [np.asarray(self.train[f][s:e]) for f, s, e in (chunks[j] for j in order[i:i + buffer_chunks])]
            buf = np.concatenate(parts)
            buf = buf[rng.permutation(len(buf))]
            full = len(buf) - len(buf) % batch_size
            for start in range(0, full, batch_size):
                yield buf[start:start + batch_size]
            leftover = buf[full:]

    def validation(self, limit=200_000):
        """About `limit` held-out positions, the same fraction from every
        file, so the validation set has the training data's mix (the first
        `limit` held-out positions came only from the first files by name)."""
        total = sum(len(v) for v in self.val)
        fraction = min(1.0, limit / max(total, 1))
        return np.concatenate([np.asarray(v[:math.ceil(len(v) * fraction)]) for v in self.val])


def decode(records):
    """Records -> (pieces, stm, score, result): pieces is (B, 64) with the
    Piece (0-11) on each square or -1; score and result from White's side."""
    occ_bytes = records["occupancy"].astype("<u8").view(np.uint8).reshape(-1, 8)
    mask = np.unpackbits(occ_bytes, axis=1, bitorder="little").astype(bool)  # (B, 64), square order
    nib = records["pieces"]
    nibbles = np.empty((len(records), 32), dtype=np.int8)
    nibbles[:, 0::2] = nib & 15
    nibbles[:, 1::2] = nib >> 4
    rank = np.clip(np.cumsum(mask, axis=1) - 1, 0, 31)
    pieces = np.take_along_axis(nibbles, rank, axis=1)
    pieces[~mask] = -1
    return pieces, records["stm"].astype(np.int64), records["score"].astype(np.float32), records["result"].astype(np.float32)


SQUARES = torch.arange(64)


def features(pieces, device):
    """One-hot inputs from White's and Black's side, (B, 768) each."""
    pieces = torch.from_numpy(pieces).to(device=device, dtype=torch.int64)
    on = pieces >= 0
    sq = SQUARES.to(device)
    white = torch.where(on, pieces * 64 + sq, INPUTS)
    # Seen from Black: colors swapped (piece 0-5 <-> 6-11) and the board flipped.
    black = torch.where(on, ((pieces + 6) % 12) * 64 + (sq ^ 56), INPUTS)
    xw = torch.zeros(len(pieces), INPUTS + 1, device=device).scatter_(1, white, 1.0)[:, :INPUTS]
    xb = torch.zeros(len(pieces), INPUTS + 1, device=device).scatter_(1, black, 1.0)[:, :INPUTS]
    return xw, xb


class NNUE(torch.nn.Module):
    def __init__(self, hidden=HIDDEN):
        super().__init__()
        self.ft = torch.nn.Linear(INPUTS, hidden)
        self.out = torch.nn.Linear(2 * hidden, 1)

    def forward(self, xw, xb, stm):
        w, b = self.ft(xw), self.ft(xb)
        black_to_move = (stm == 1).unsqueeze(1)
        hidden = torch.where(black_to_move, torch.cat([b, w], 1), torch.cat([w, b], 1))
        return self.out(torch.clamp(hidden, 0, 1)).squeeze(1)

    def clip(self):
        with torch.no_grad():
            self.ft.weight.clamp_(-FT_CLIP, FT_CLIP)
            self.ft.bias.clamp_(-FT_CLIP, FT_CLIP)


def batch_loss(model, records, device, wdl):
    pieces, stm, score, result = decode(records)
    xw, xb = features(pieces, device)
    stm_t = torch.from_numpy(stm).to(device)
    score = torch.from_numpy(score).to(device)
    result = torch.from_numpy(result).to(device) / 2  # White's side: 0 loss, 0.5 draw, 1 win
    white = stm_t == 0
    score_stm = torch.where(white, score, -score)
    result_stm = torch.where(white, result, 1 - result)
    target = (1 - wdl) * torch.sigmoid(score_stm / K) + wdl * result_stm
    pred = torch.sigmoid(model(xw, xb, stm_t) * SCALE / K)
    return torch.mean((pred - target) ** 2)


def export(model, path):
    """The engine's network file (little-endian):
    b"E2NN", u32 version 1, u32 hidden,
    i16 ft_weight[768][hidden] (input-major), i16 ft_bias[hidden],
    i16 out_weight[2 * hidden] (side to move first), i32 out_bias."""
    ft_w = torch.round(model.ft.weight.detach().cpu().T * QA).to(torch.int16).numpy()
    ft_b = torch.round(model.ft.bias.detach().cpu() * QA).to(torch.int16).numpy()
    out_w = torch.round(model.out.weight.detach().cpu().squeeze(0) * QB).to(torch.int16).numpy()
    out_b = int(round(model.out.bias.item() * QA * QB))
    with open(path, "wb") as f:
        f.write(b"E2NN" + struct.pack("<II", 1, model.ft.out_features))
        f.write(ft_w.astype("<i2").tobytes())
        f.write(ft_b.astype("<i2").tobytes())
        f.write(out_w.astype("<i2").tobytes())
        f.write(struct.pack("<i", out_b))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data", nargs="+", help="datagen files (wildcards allowed)")
    ap.add_argument("--out", default="nets", help="folder for checkpoints and networks")
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--wdl", type=float, default=0.25, help="weight of the game result in the target")
    ap.add_argument("--limit", type=int, default=0, help="use only this many positions per epoch (for a quick test)")
    ap.add_argument("--resume", help="checkpoint to continue from")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--hidden", type=int, default=HIDDEN, help="hidden units per side (the engine is built to match)")
    args = ap.parse_args()

    paths = sorted(p for pattern in args.data for p in glob.glob(pattern))
    if not paths:
        raise SystemExit("no data files found")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    data = Dataset(paths)
    val = data.validation()
    os.makedirs(args.out, exist_ok=True)
    print(f"{len(paths)} files, {data.size:,} training positions, {args.hidden} hidden units, device {device}")

    torch.manual_seed(args.seed)
    model = NNUE(args.hidden).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    # The learning rate falls by 10x over the run, on a cosine.
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs, eta_min=args.lr / 10)
    start_epoch = 1
    if args.resume:
        state = torch.load(args.resume, map_location=device)
        model.load_state_dict(state["model"])
        opt.load_state_dict(state["opt"])
        sched.load_state_dict(state["sched"])
        start_epoch = state["epoch"] + 1
        print(f"resumed from {args.resume} (epoch {state['epoch']})")

    rng = np.random.default_rng(args.seed + start_epoch)
    for epoch in range(start_epoch, args.epochs + 1):
        model.train()
        t0, total, n = time.time(), 0.0, 0
        for records in data.batches(args.batch, rng):
            loss = batch_loss(model, records, device, args.wdl)
            opt.zero_grad()
            loss.backward()
            opt.step()
            model.clip()
            total += loss.item()
            n += 1
            if args.limit and n * args.batch >= args.limit:
                break
            if n % 500 == 0:
                print(f"  epoch {epoch} batch {n}: loss {total / n:.6f} ({n * args.batch / (time.time() - t0):,.0f} pos/s)", flush=True)
        sched.step()
        model.eval()
        with torch.no_grad():
            val_loss = batch_loss(model, val, device, args.wdl).item()
        print(f"epoch {epoch}: train {total / max(n, 1):.6f}, validation {val_loss:.6f}, {time.time() - t0:.0f} s", flush=True)
        torch.save({"model": model.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(), "epoch": epoch},
                   os.path.join(args.out, "checkpoint.pt"))
        export(model, os.path.join(args.out, f"net-{epoch}.nnue"))


if __name__ == "__main__":
    main()
