"""Trains an NNUE evaluation for Engine 2 on data from `engine2 datagen`.

    python scripts/nnue/train.py C:/Users/conor/nnue-data/part-*.bin --out nets

The network (the engine's src/nnue.h must match):
  768 inputs per side, one per (piece kind, square) seen from that side
  (its own pieces first, squares flipped for Black)
  -> --hidden units (512 by default), shared weights for both sides, clipped to [0, 1]
  -> both sides' units, side to move first -> 1 output, the evaluation in
     units of SCALE centipawns.
Two optional extras, each on its own or together (the engine reads which
from the network file, so no rebuild is needed):
  --screlu            the clipped units are squared before the output layer
  --output-buckets N  N output layers instead of one, chosen by the number
                      of pieces on the board (8 is the usual choice)
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
import queue
import struct
import threading
import time

import numpy as np
import torch
import torch.nn.functional as F

from data import RECORD

HIDDEN = 512  # default (the engine's since 3.3; 256 from 2.9 to 3.2); --hidden changes it (the engine must be built to match)
INPUTS = 768
QA = 255     # first layer: weights and biases times QA, accumulators in [0, QA]
QB = 64      # output weights times QB
SCALE = 400  # output unit, in centipawns
K = 400      # centipawns per unit of the sigmoid that turns scores into win probabilities
FT_CLIP = 1.98  # first-layer weight limit: 32 pieces * 1.98 * QA fits a 16-bit accumulator
OUT_CLIP = 127 / QB  # output weight limit with --screlu: unit x weight must fit 16 bits in the engine


def bucket_of(count, buckets):
    """The output layer for a position with `count` pieces (2 to 32): equal
    steps of piece count, the fullest boards in the last. The engine's
    src/nnue.cpp has the same formula."""
    return torch.clamp((count - 2) * buckets // 32, 0, buckets - 1)


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
    """The inputs that are on, from White's and Black's side: (B, 32) feature
    numbers each, padded with INPUTS (no feature) after the board's pieces.
    A position has at most 32 of the 768 inputs on, so the first layer sums
    32 weight rows (embedding_bag) instead of multiplying a mostly-zero
    (B, 768) one-hot matrix: the same result, much less work."""
    pieces = torch.from_numpy(pieces).to(device=device, dtype=torch.int64)
    on = pieces >= 0
    sq = SQUARES.to(device)
    white = torch.where(on, pieces * 64 + sq, INPUTS)
    # Seen from Black: colors swapped (piece 0-5 <-> 6-11) and the board flipped.
    black = torch.where(on, ((pieces + 6) % 12) * 64 + (sq ^ 56), INPUTS)
    # Sorting moves the padding (INPUTS, the largest number) to the end.
    return torch.sort(white, dim=1).values[:, :32], torch.sort(black, dim=1).values[:, :32]


class NNUE(torch.nn.Module):
    def __init__(self, hidden=HIDDEN, screlu=False, buckets=1):
        super().__init__()
        self.screlu, self.buckets = screlu, buckets
        self.ft = torch.nn.Linear(INPUTS, hidden)
        self.out = torch.nn.Linear(2 * hidden, buckets)

    def forward(self, xw, xb, stm, count=None):
        """xw, xb: features() from White's and Black's side; count: pieces on
        the board, (B,), only needed with output buckets."""
        # Side to move first: swap the (B, 32) inputs, not the (B, 2 x hidden)
        # hidden units (moving those around cost a third of a training step).
        black_to_move = (stm == 1).unsqueeze(1)
        us, them = torch.where(black_to_move, xb, xw), torch.where(black_to_move, xw, xb)
        hidden = self.ft.out_features
        # The first layer's weights as rows, one per input, plus a zero row
        # for the padding.
        rows = torch.cat([self.ft.weight.T, self.ft.weight.new_zeros(1, hidden)])
        a = torch.clamp(F.embedding_bag(us, rows, mode="sum", padding_idx=INPUTS) + self.ft.bias, 0, 1)
        b = torch.clamp(F.embedding_bag(them, rows, mode="sum", padding_idx=INPUTS) + self.ft.bias, 0, 1)
        if self.screlu:
            a, b = a * a, b * b
        w = self.out.weight
        out = a @ w[:, :hidden].T + b @ w[:, hidden:].T + self.out.bias
        if self.buckets == 1:
            return out.squeeze(1)
        return out.gather(1, bucket_of(count, self.buckets).unsqueeze(1)).squeeze(1)

    def clip(self):
        with torch.no_grad():
            self.ft.weight.clamp_(-FT_CLIP, FT_CLIP)
            self.ft.bias.clamp_(-FT_CLIP, FT_CLIP)
            if self.screlu:
                self.out.weight.clamp_(-OUT_CLIP, OUT_CLIP)


def batch_loss(model, records, device, wdl):
    """records: a batch of records, or decode() of them (prefetched)."""
    pieces, stm, score, result = decode(records) if isinstance(records, np.ndarray) else records
    xw, xb = features(pieces, device)
    stm_t = torch.from_numpy(stm).to(device)
    score = torch.from_numpy(score).to(device)
    result = torch.from_numpy(result).to(device) / 2  # White's side: 0 loss, 0.5 draw, 1 win
    white = stm_t == 0
    score_stm = torch.where(white, score, -score)
    result_stm = torch.where(white, result, 1 - result)
    target = (1 - wdl) * torch.sigmoid(score_stm / K) + wdl * result_stm
    count = torch.from_numpy((pieces >= 0).sum(axis=1)).to(device)
    pred = torch.sigmoid(model(xw, xb, stm_t, count) * SCALE / K)
    return torch.mean((pred - target) ** 2)


def prefetch(batches, depth=4):
    """decode() the batches in a background thread, a few ahead, so the
    GPU need not wait for the CPU's share of each batch."""
    q = queue.Queue(maxsize=depth)
    done = object()

    def work():
        try:
            for records in batches:
                q.put(decode(records))
        finally:
            q.put(done)

    threading.Thread(target=work, daemon=True).start()
    while (item := q.get()) is not done:
        yield item


def export(model, path):
    """The engine's network file (little-endian). Without --screlu and
    --output-buckets, version 1, as before:
    b"E2NN", u32 version 1, u32 hidden,
    i16 ft_weight[768][hidden] (input-major), i16 ft_bias[hidden],
    i16 out_weight[2 * hidden] (side to move first), i32 out_bias.
    With either, version 2: after hidden come u32 screlu (0 or 1) and
    u32 buckets, and the output layer is
    i16 out_weight[buckets][2 * hidden], i32 out_bias[buckets]."""
    ft_w = torch.round(model.ft.weight.detach().cpu().T * QA).to(torch.int16).numpy()
    ft_b = torch.round(model.ft.bias.detach().cpu() * QA).to(torch.int16).numpy()
    out_w = torch.round(model.out.weight.detach().cpu() * QB).to(torch.int16).numpy()  # (buckets, 2 * hidden)
    out_b = torch.round(model.out.bias.detach().cpu().double() * QA * QB).to(torch.int32).numpy()
    with open(path, "wb") as f:
        if model.screlu or model.buckets > 1:
            f.write(b"E2NN" + struct.pack("<IIII", 2, model.ft.out_features, int(model.screlu), model.buckets))
        else:
            f.write(b"E2NN" + struct.pack("<II", 1, model.ft.out_features))
        f.write(ft_w.astype("<i2").tobytes())
        f.write(ft_b.astype("<i2").tobytes())
        f.write(out_w.astype("<i2").tobytes())
        f.write(out_b.astype("<i4").tobytes())


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data", nargs="+", help="datagen files (wildcards allowed)")
    ap.add_argument("--out", default="nets", help="folder for checkpoints and networks")
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--lr-end", type=float, default=0.1, metavar="F",
                    help="the learning rate ends at F x --lr, on a cosine (0.1 as before; 0.01 falls 100x)")
    ap.add_argument("--wdl", type=float, default=0.25, help="weight of the game result in the target")
    ap.add_argument("--limit", type=int, default=0, help="use only this many positions per epoch (for a quick test)")
    ap.add_argument("--resume", help="checkpoint to continue from")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--hidden", type=int, default=HIDDEN, help="hidden units per side (the engine is built to match)")
    ap.add_argument("--screlu", action="store_true", help="square the clipped hidden units (SCReLU)")
    ap.add_argument("--output-buckets", type=int, default=1, metavar="N",
                    help="N output layers chosen by piece count (1 = a single output, as before; 8 is usual)")
    args = ap.parse_args()
    if not 1 <= args.output_buckets <= 8:
        raise SystemExit("--output-buckets must be between 1 and 8")

    paths = sorted(p for pattern in args.data for p in glob.glob(pattern))
    if not paths:
        raise SystemExit("no data files found")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    # TF32 for the output layer's matrix products on recent NVIDIA GPUs
    # (Ampere on): several times faster than full float32.
    torch.backends.cuda.matmul.allow_tf32 = True
    data = Dataset(paths)
    val = data.validation()
    os.makedirs(args.out, exist_ok=True)
    print(f"{len(paths)} files, {data.size:,} training positions, {args.hidden} hidden units, "
          f"{'squared clipped (SCReLU)' if args.screlu else 'clipped'} units, "
          f"{args.output_buckets} output layer{'s' if args.output_buckets > 1 else ''}, device {device}")

    torch.manual_seed(args.seed)
    model = NNUE(args.hidden, args.screlu, args.output_buckets).to(device)
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    # The learning rate falls by 1 / --lr-end (10x by default) over the run, on a cosine.
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs, eta_min=args.lr * args.lr_end)
    start_epoch = 1
    if args.resume:
        state = torch.load(args.resume, map_location=device)
        if state.get("screlu", False) != args.screlu or state["model"]["out.weight"].shape[0] != args.output_buckets:
            raise SystemExit("the checkpoint was trained with different --screlu / --output-buckets settings")
        model.load_state_dict(state["model"])
        opt.load_state_dict(state["opt"])
        sched.load_state_dict(state["sched"])
        start_epoch = state["epoch"] + 1
        print(f"resumed from {args.resume} (epoch {state['epoch']})")

    rng = np.random.default_rng(args.seed + start_epoch)
    for epoch in range(start_epoch, args.epochs + 1):
        model.train()
        t0, total, n = time.time(), 0.0, 0
        for records in prefetch(data.batches(args.batch, rng)):
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
        torch.save({"model": model.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(), "epoch": epoch,
                    "screlu": args.screlu},
                   os.path.join(args.out, "checkpoint.pt"))
        export(model, os.path.join(args.out, f"net-{epoch}.nnue"))


if __name__ == "__main__":
    main()
