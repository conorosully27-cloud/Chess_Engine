"""Trains an NNUE evaluation for Engine 2 on data from `engine2 datagen`.

    python scripts/nnue/train.py C:/Users/conor/nnue-data/part-*.bin --out nets

The network (the engine's src/nnue.h must match):
  768 inputs per side, one per (piece kind, square) seen from that side
  (its own pieces first, squares flipped for Black)
  -> --hidden units (512 by default), shared weights for both sides, clipped to [0, 1]
  -> both sides' units, side to move first -> 1 output, the evaluation in
     units of SCALE centipawns.
Optional extras, on their own or together (the engine reads which from
the network file, so no rebuild is needed):
  --screlu            the clipped units are squared before the output layer
  --output-buckets N  N output layers instead of one, chosen by the number
                      of pieces on the board (8 is the usual choice)
  --king-buckets N    N sets of input weights (4 or 8), chosen for each side
                      by where its own king stands (KING_MAPS below), with
                      the board mirrored left-right when that king is on
                      files e-h. The first layer is then sparse (only the
                      ~32 active inputs are looked up), so training costs
                      about the same as without.
  --factorize         with king buckets: train a shared set of 768 input
                      weights that every bucket adds to ("factoriser"), so
                      each bucket only learns how it differs from the rest
                      and rare (bucket, piece, square) inputs still learn
                      from all the data; folded into the buckets on export,
                      so the engine and the network file are unchanged
  --init-from CKPT    start from another run's checkpoint (same hidden units,
                      activation and output layers): with king buckets its
                      input weights are copied into every bucket, so the
                      new run starts as the old network and learns from there
Loss: squared error between win probabilities, sigmoid(eval / K), and a
blend of the search score's and the game result's (--wdl of the result);
--loss-power P raises the error to P instead of squaring it.

Data are read from memory-mapped files, a batch at a time, so a dataset
larger than memory works. Every epoch saves a checkpoint (resume with
--resume) and an engine network file (net-<epoch>.nnue, format in
export()).
"""
import argparse
import copy
import glob
import math
import os
import queue
import struct
import threading
import time

import numpy as np
import torch

from data import RECORD

HIDDEN = 512  # default (the engine's since 3.3; 256 from 2.9 to 3.2); --hidden changes it (the engine must be built to match)
INPUTS = 768
QA = 255     # first layer: weights and biases times QA, accumulators in [0, QA]
QB = 64      # output weights times QB
SCALE = 400  # output unit, in centipawns
K = 400      # centipawns per unit of the sigmoid that turns scores into win probabilities
FT_CLIP = 1.98  # first-layer weight limit (--ft-clip): a bias and 32 pieces, 33 x 1.98 x QA = 16,662, fit a 16-bit accumulator
FT_CLIP_MAX = 32767 / (33 * QA)  # 3.89: the largest limit that still fits
OUT_CLIP = 127 / QB  # output weight limit with --screlu: unit x weight must fit 16 bits in the engine
QB_FINE = 1024  # output weights times QB_FINE with --fine-output (engine: output_squared_fine)
# --layers (network file version 4): the first extra layer's inputs are the
# squared units as the engine computes them, (c * c) >> 4 for c in [0, QA],
# so 1.0 is L1_IN = QA * QA / 16; its weights are x L1_Q in 16 bits. The
# limit keeps a sum of 2 x 1024 such products within 32 bits.
L1_IN = QA * QA / 16
L1_Q = 256
L1_CLIP = 253 / L1_Q
ENGINE_LAYERS = (16, 32)  # the only extra-layer sizes the engine runs


# King bucket maps: for each square of a side's own king, seen from that
# side (a1 = 0, rank 1 = its back rank) and after mirroring (so only files
# a-d occur), the bucket. Written into the network file, so the engine needs
# no change for a new map.
def _king_map(rows):
    """rows: 8 lists of 4 buckets, rank 1 first, files a-d; e-h mirror a-d."""
    return [rows[r][f if f < 4 else 7 - f] for r in range(8) for f in range(8)]


KING_MAPS = {
    1: [0] * 64,
    # castled-short corner (mirrored to a/b), centre or long-castled (c/d),
    # the second rank, and anywhere further up.
    4: _king_map([[0, 0, 1, 1], [2, 2, 2, 2]] + [[3, 3, 3, 3]] * 6),
    8: _king_map([[0, 0, 1, 1], [2, 2, 3, 3], [4, 4, 4, 4], [5, 5, 5, 5]] + [[6, 6, 6, 6]] * 2 + [[7, 7, 7, 7]] * 2),
}


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


def decode_torch(records, device):
    """decode() done with torch on `device`, returning tensors there. Only
    the raw 32-byte records cross to the device (512 KB a batch of 16,384),
    so on a GPU the CPU's whole share of a batch is the shuffle. `records`:
    a record array, or its bytes as a (B, 32) uint8 tensor. The same
    values as decode()."""
    raw = records if isinstance(records, torch.Tensor) else \
        torch.from_numpy(np.ascontiguousarray(records).view(np.uint8).reshape(-1, RECORD.itemsize))
    raw = raw.to(device, non_blocking=True)
    bits = torch.arange(8, device=raw.device, dtype=torch.uint8)
    mask = ((raw[:, :8].unsqueeze(2) >> bits) & 1).reshape(-1, 64).bool()  # bit i of byte j = square 8j + i
    nib = raw[:, 8:24]
    nibbles = torch.stack((nib & 15, nib >> 4), dim=2).reshape(-1, 32).to(torch.int64)
    rank = torch.clamp(torch.cumsum(mask, dim=1) - 1, 0, 31)
    pieces = torch.gather(nibbles, 1, rank).masked_fill(~mask, -1)
    score = raw[:, 24].to(torch.int32) | (raw[:, 25].to(torch.int32) << 8)
    score = (score - (score >= 32768).to(torch.int32) * 65536).to(torch.float32)
    return pieces, raw[:, 27].to(torch.int64), score, raw[:, 26].to(torch.float32)


def raw_bytes(records):
    """A batch's bytes as a pinned (B, 32) uint8 tensor, ready for
    decode_torch on a GPU (pinned memory copies to the GPU asynchronously)."""
    return torch.from_numpy(np.ascontiguousarray(records).view(np.uint8).reshape(-1, RECORD.itemsize)).pin_memory()


SQUARES = torch.arange(64)


def feature_indices(pieces, device, king_buckets=1):
    """The active inputs from White's and Black's side, (B, 64) each: the
    input index of the piece on each square, or the number of inputs (a pad)
    where the square is empty. With king buckets each side's index is offset
    by its own king's bucket, and the squares are mirrored left-right when
    that king is on files e-h (the engine's src/nnue.cpp does the same)."""
    pieces = (torch.from_numpy(pieces) if isinstance(pieces, np.ndarray) else pieces).to(device=device, dtype=torch.int64)
    on = pieces >= 0
    sq = SQUARES.to(device)
    total = INPUTS * king_buckets
    white = pieces * 64 + sq
    # Seen from Black: colors swapped (piece 0-5 <-> 6-11) and the board flipped.
    black = ((pieces + 6) % 12) * 64 + (sq ^ 56)
    if king_buckets > 1:
        kmap = torch.tensor(KING_MAPS[king_buckets], device=device)
        for side, king_piece, flip in ((0, 5, 0), (1, 11, 56)):
            king = torch.argmax((pieces == king_piece).to(torch.int8), dim=1) ^ flip  # own king, seen from that side
            mirror = ((king & 7) >= 4).to(torch.int64) * 7
            offset = (kmap[king] * INPUTS).unsqueeze(1)
            if side == 0:
                white = offset + (white ^ mirror.unsqueeze(1))
            else:
                black = offset + (black ^ mirror.unsqueeze(1))
    return torch.where(on, white, total), torch.where(on, black, total)


def features(pieces, device, king_buckets=1, sparse=False):
    """The network's inputs from White's and Black's side: one-hot (B, inputs)
    each, or with `sparse` the (B, 64) index lists of feature_indices. The
    same with or without the factoriser (NNUE.transform folds the shared
    weights into the buckets' before looking rows up)."""
    white, black = feature_indices(pieces, device, king_buckets)
    if sparse:
        return white, black
    total = INPUTS * king_buckets
    xw = torch.zeros(len(pieces), total + 1, device=device).scatter_(1, white, 1.0)[:, :total]
    xb = torch.zeros(len(pieces), total + 1, device=device).scatter_(1, black, 1.0)[:, :total]
    return xw, xb


class FeatureTransformer(torch.nn.Module):
    """The first layer, stored input-major: weight is (inputs, hidden), one
    row per input, as in the engine's file. That layout lets the sparse path
    look rows up directly (through a transposed view the backward pass was
    ~70x slower). Initialised like torch.nn.Linear."""

    def __init__(self, inputs, hidden):
        super().__init__()
        self.in_features, self.out_features = inputs, hidden
        bound = 1 / math.sqrt(inputs)
        self.weight = torch.nn.Parameter(torch.empty(inputs, hidden).uniform_(-bound, bound))
        self.bias = torch.nn.Parameter(torch.empty(hidden).uniform_(-bound, bound))

    def forward(self, x, weight=None):
        """x is one-hot (B, inputs) or, sparse, (B, 64) input indices padded
        with the number of inputs. `weight` replaces the layer's own (the
        factoriser's folded weights)."""
        weight = self.weight if weight is None else weight
        if x.dtype != torch.int64:
            return x @ weight + self.bias
        # Only the real pieces go in, as one flat list with each position's
        # start: padding the empty squares onto row 0 with weight 0 made every
        # position hit that row ~30 times, which serialised the backward pass
        # on a GPU (all those gradient adds land on one row).
        on = x < weight.shape[0]
        flat = x[on]
        offsets = torch.zeros(len(x), dtype=torch.int64, device=x.device)
        offsets[1:] = torch.cumsum(on.sum(dim=1), 0)[:-1]
        return torch.nn.functional.embedding_bag(flat, weight, offsets, mode="sum") + self.bias


def net_state(saved):
    """The weights a checkpoint's network files were exported from: the
    moving average with --ema, else the trained weights (upgraded)."""
    return upgrade_state({**saved, "model": saved["ema"]} if "ema" in saved else saved)


def upgrade_state(saved):
    """A checkpoint's model weights in the current layout: checkpoints from
    before king buckets kept the first layer as torch.nn.Linear's (hidden,
    inputs), now it is (inputs, hidden)."""
    state = dict(saved["model"])
    if not saved.get("ft_input_major", False):
        state["ft.weight"] = state["ft.weight"].t().contiguous()
    return state


class NNUE(torch.nn.Module):
    def __init__(self, hidden=HIDDEN, screlu=False, buckets=1, king_buckets=1, factorize=False, layers=None):
        super().__init__()
        self.screlu, self.buckets, self.king_buckets = screlu, buckets, king_buckets
        self.layers = tuple(layers) if layers else None
        self.factorize = factorize and king_buckets > 1
        rows = INPUTS * king_buckets
        self.ft = FeatureTransformer(rows + (INPUTS if self.factorize else 0), hidden)
        if self.factorize:
            # Start as a plain network: the buckets' own weights at 0, the
            # shared ones initialised as a 768-input layer would be.
            with torch.no_grad():
                bound = 1 / math.sqrt(INPUTS)
                self.ft.weight[:rows].zero_()
                self.ft.weight[rows:].uniform_(-bound, bound)
                self.ft.bias.uniform_(-bound, bound)
        self.out = torch.nn.Linear(2 * hidden, buckets)  # unused with --layers (kept so checkpoints look alike)
        if self.layers:
            # Extra layers, one set per output bucket: both sides' squared
            # units -> l1 clipped units -> l2 clipped units -> the output.
            l1, l2 = self.layers
            self.l1 = torch.nn.Linear(2 * hidden, l1 * buckets)
            self.l2 = torch.nn.Linear(l1, l2 * buckets)
            self.l3 = torch.nn.Linear(l2, buckets)

    def bucket_weights(self):
        """The first layer's weights as the engine uses them, (king_buckets x
        768, hidden): with the factoriser, each bucket's own weights plus the
        shared ones."""
        rows = INPUTS * self.king_buckets
        w = self.ft.weight
        if not self.factorize:
            return w
        return (w[:rows].view(self.king_buckets, INPUTS, -1) + w[rows:]).reshape(rows, -1)

    def transform(self, x, weights=None):
        """The first layer; `weights` is bucket_weights(), computed once per
        batch for both sides."""
        return self.ft(x, self.bucket_weights() if weights is None else weights)

    def forward(self, xw, xb, stm, count=None):
        """count: pieces on the board, (B,); only needed with output buckets."""
        # Side to move first: swap the inputs, not the (B, 2 x hidden) hidden
        # units (moving those around cost about a third of a training step).
        black_to_move = (stm == 1).unsqueeze(1)
        us, them = torch.where(black_to_move, xb, xw), torch.where(black_to_move, xw, xb)
        # With the factoriser: each bucket's own weights plus the shared ones,
        # formed once per batch (3M additions; looking up a second row per
        # piece instead cost 60% more time an epoch).
        weights = self.bucket_weights()
        a = torch.clamp(self.transform(us, weights), 0, 1)
        b = torch.clamp(self.transform(them, weights), 0, 1)
        if self.screlu:
            a, b = a * a, b * b
        hidden = self.ft.out_features
        if self.layers:
            n = len(a)
            k = bucket_of(count, self.buckets) if self.buckets > 1 else torch.zeros(n, dtype=torch.int64, device=a.device)
            row = torch.arange(n, device=a.device)
            h1 = torch.clamp(self.l1(torch.cat([a, b], 1)).view(n, self.buckets, -1)[row, k], 0, 1)
            h2 = torch.clamp(self.l2(h1).view(n, self.buckets, -1)[row, k], 0, 1)
            return self.l3(h2)[row, k]
        w = self.out.weight
        out = a @ w[:, :hidden].T + b @ w[:, hidden:].T + self.out.bias
        if self.buckets == 1:
            return out.squeeze(1)
        return out.gather(1, bucket_of(count, self.buckets).unsqueeze(1)).squeeze(1)

    ft_clip = FT_CLIP

    def clip(self):
        FT_CLIP = self.ft_clip
        with torch.no_grad():
            if self.factorize:
                # Keep what the engine will use, bucket + shared, within the
                # limit (and the shared weights themselves).
                rows = INPUTS * self.king_buckets
                shared = self.ft.weight[rows:]
                shared.clamp_(-FT_CLIP, FT_CLIP)
                own = self.ft.weight[:rows].view(self.king_buckets, INPUTS, -1)
                own.copy_(torch.clamp(own + shared, -FT_CLIP, FT_CLIP) - shared)
            else:
                self.ft.weight.clamp_(-FT_CLIP, FT_CLIP)
            self.ft.bias.clamp_(-FT_CLIP, FT_CLIP)
            if self.screlu:
                self.out.weight.clamp_(-OUT_CLIP, OUT_CLIP)
            if self.layers:
                self.l1.weight.clamp_(-L1_CLIP, L1_CLIP)


def batch_loss(model, records, device, wdl, k=K, power=2.0):
    """records: a batch of records, decode() of them (prefetched), or their
    raw_bytes() (prefetched for a GPU, decoded there).
    power: the exponent of the error (2 = squared error, as always; 2.5
    weights large errors more, as Stockfish's trainer does)."""
    if isinstance(records, torch.Tensor):
        records = decode_torch(records, device)
    elif isinstance(records, np.ndarray):
        records = decode_torch(records, device) if str(device).startswith("cuda") else decode(records)
    pieces, stm, score, result = (torch.from_numpy(x) if isinstance(x, np.ndarray) else x for x in records)
    xw, xb = features(pieces, device, model.king_buckets, sparse=getattr(model, "sparse_ft", True))
    stm_t = stm.to(device)
    score = score.to(device)
    result = result.to(device) / 2  # White's side: 0 loss, 0.5 draw, 1 win
    white = stm_t == 0
    score_stm = torch.where(white, score, -score)
    result_stm = torch.where(white, result, 1 - result)
    target = (1 - wdl) * torch.sigmoid(score_stm / k) + wdl * result_stm
    count = (pieces >= 0).sum(dim=1).to(device)
    pred = torch.sigmoid(model(xw, xb, stm_t, count) * SCALE / k)
    if power == 2.0:
        return torch.mean((pred - target) ** 2)
    return torch.mean(torch.abs(pred - target) ** power)


def prefetch(batches, depth=4, prepare=decode):
    """prepare() the batches in a background thread, a few ahead, so the
    GPU need not wait for the CPU's share of each batch: decode() on the
    CPU, raw_bytes() for decoding on a GPU."""
    q = queue.Queue(maxsize=depth)
    done = object()

    def work():
        try:
            for records in batches:
                q.put(prepare(records))
        finally:
            q.put(done)

    threading.Thread(target=work, daemon=True).start()
    while (item := q.get()) is not done:
        yield item


def export(model, path, fine=False):
    """The engine's network file (little-endian). Without --screlu and
    --output-buckets, version 1, as before:
    b"E2NN", u32 version 1, u32 hidden,
    i16 ft_weight[768][hidden] (input-major), i16 ft_bias[hidden],
    i16 out_weight[2 * hidden] (side to move first), i32 out_bias.
    With either, version 2: after hidden come u32 screlu (0 or 1) and
    u32 buckets, and the output layer is
    i16 out_weight[buckets][2 * hidden], i32 out_bias[buckets].
    With king buckets, version 3: after buckets come u32 king_buckets,
    u32 mirror (1) and u8 king_map[64] (KING_MAPS), and ft_weight has
    king_buckets * 768 rows, bucket-major. The activation field is 0
    (clipped), 1 (SCReLU) or 2 (SCReLU with `fine` output weights: x1024
    instead of x64, out_bias x QA x 1024; engines before Nov 2026 refuse it)."""
    ft_w = torch.round(model.bucket_weights().detach().cpu() * QA).to(torch.int16).numpy()  # (inputs, hidden), input-major
    ft_b = torch.round(model.ft.bias.detach().cpu() * QA).to(torch.int16).numpy()
    if fine and not model.screlu:
        raise ValueError("fine output weights are for SCReLU networks")
    qb = QB_FINE if fine else QB
    out_w = model.out.weight.detach().cpu() * qb
    if out_w.abs().max() > 32767:
        raise ValueError("output weights too large for 16 bits")
    out_w = torch.round(out_w).to(torch.int16).numpy()  # (buckets, 2 * hidden)
    out_b = torch.round(model.out.bias.detach().cpu().double() * QA * qb).to(torch.int32).numpy()
    activation = (2 if fine else 1) if model.screlu else 0
    if model.layers:
        export_layers(model, path, ft_w, ft_b)
        return
    with open(path, "wb") as f:
        if model.king_buckets > 1:
            f.write(b"E2NN" + struct.pack("<IIIIII", 3, model.ft.out_features, activation, model.buckets,
                                          model.king_buckets, 1))
            f.write(bytes(KING_MAPS[model.king_buckets]))
        elif model.screlu or model.buckets > 1:
            f.write(b"E2NN" + struct.pack("<IIII", 2, model.ft.out_features, activation, model.buckets))
        else:
            f.write(b"E2NN" + struct.pack("<II", 1, model.ft.out_features))
        f.write(ft_w.astype("<i2").tobytes())
        f.write(ft_b.astype("<i2").tobytes())
        f.write(out_w.astype("<i2").tobytes())
        f.write(out_b.astype("<i4").tobytes())


def export_layers(model, path, ft_w, ft_b):
    """Version 4, for --layers: the version 3 header (activation 1, king
    buckets 1 and no mirror without them), then u32 l1, u32 l2; the first
    layer as in version 3; then per output bucket
    i16 l1_weight[buckets][l1][2 * hidden] (x L1_Q, side to move first),
    i32 l1_bias[buckets][l1] (x L1_IN x L1_Q),
    f32 l2_weight[buckets][l2][l1], f32 l2_bias[buckets][l2],
    f32 l3_weight[buckets][l2], f32 l3_bias[buckets]
    (l3 in units of SCALE centipawns)."""
    l1, l2 = model.layers
    hidden, buckets = model.ft.out_features, model.buckets
    w1 = model.l1.weight.detach().cpu().double() * L1_Q
    if w1.abs().max() > 253.5:
        raise ValueError("first extra layer's weights past their limit")
    w1 = torch.round(w1).to(torch.int16).numpy().reshape(buckets, l1, 2 * hidden)
    b1 = torch.round(model.l1.bias.detach().cpu().double() * L1_IN * L1_Q).to(torch.int32).numpy().reshape(buckets, l1)
    f32 = lambda t, *shape: t.detach().cpu().to(torch.float32).numpy().reshape(*shape).astype("<f4").tobytes()
    king_map = KING_MAPS[model.king_buckets]
    with open(path, "wb") as f:
        f.write(b"E2NN" + struct.pack("<IIIIII", 4, hidden, 1, buckets, model.king_buckets, int(model.king_buckets > 1)))
        f.write(bytes(king_map))
        f.write(struct.pack("<II", l1, l2))
        f.write(ft_w.astype("<i2").tobytes())
        f.write(ft_b.astype("<i2").tobytes())
        f.write(w1.astype("<i2").tobytes())
        f.write(b1.astype("<i4").tobytes())
        f.write(f32(model.l2.weight, buckets, l2, l1))
        f.write(f32(model.l2.bias, buckets, l2))
        f.write(f32(model.l3.weight, buckets, l2))
        f.write(f32(model.l3.bias, buckets))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("data", nargs="+", help="datagen files (wildcards allowed)")
    ap.add_argument("--out", default="nets", help="folder for checkpoints and networks")
    ap.add_argument("--epochs", type=int, default=20)
    ap.add_argument("--batch", type=int, default=16384)
    ap.add_argument("--lr", type=float, default=1e-3)
    ap.add_argument("--lr-end", type=float, default=0.1, metavar="F",
                    help="the learning rate ends at F x --lr, on a cosine (0.1 as before; 0.01 falls 100x). "
                         "A resumed run keeps the value it started with")
    ap.add_argument("--wdl", type=float, default=0.25, help="weight of the game result in the target")
    ap.add_argument("--limit", type=int, default=0, help="use only this many positions per epoch (for a quick test)")
    ap.add_argument("--resume", help="checkpoint to continue from")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--ft", choices=("sparse", "dense"), default="sparse",
                    help="how the first layer is computed: sparse lookups of the ~32 active inputs, or a dense one-hot "
                         "matrix product (same result; compare the pos/s of both on a GPU, dense with --tf32)")
    ap.add_argument("--hidden", type=int, default=HIDDEN, help="hidden units per side (the engine is built to match)")
    ap.add_argument("--screlu", action="store_true", help="square the clipped hidden units (SCReLU)")
    ap.add_argument("--output-buckets", type=int, default=1, metavar="N",
                    help="N output layers chosen by piece count (1 = a single output, as before; 8 is usual)")
    ap.add_argument("--king-buckets", type=int, default=1, choices=sorted(KING_MAPS),
                    help="input weight sets chosen by each side's own king square, mirrored (1 = none, as before)")
    ap.add_argument("--factorize", action="store_true",
                    help="with king buckets: shared input weights every bucket adds to, folded in on export")
    ap.add_argument("--bucket-decay", type=float, default=0.0, metavar="L",
                    help="with --factorize: each step, shrink the buckets' own weights by learning rate x L (not the "
                         "shared ones), so a bucket with little data stays close to the shared weights (0 = off)")
    ap.add_argument("--init-from", metavar="CKPT",
                    help="start from this checkpoint's weights (its input weights copied into every king bucket, "
                         "or with --factorize into the shared weights)")
    ap.add_argument("--k", type=float, default=K,
                    help="centipawns per unit of the sigmoid that turns scores into win probabilities (400 as before; "
                         "data.py prints the best fit for a data file)")
    ap.add_argument("--wdl-end", type=float, metavar="W",
                    help="move the result weight from --wdl in the first epoch to W in the last, in equal steps")
    ap.add_argument("--fine-output", action="store_true",
                    help="with --screlu: write output weights x1024 (16x less rounding error; needs a current engine)")
    ap.add_argument("--ema", type=float, default=0.0, metavar="F",
                    help="also keep an exponential moving average of the weights, over about F of an epoch, and "
                         "export that (0 = off; e.g. 0.25)")
    ap.add_argument("--ft-clip", type=float, default=FT_CLIP, metavar="C",
                    help=f"first-layer weight and bias limit ({FT_CLIP} as before; up to {FT_CLIP_MAX:.2f} still fits "
                         "the engine's 16-bit sums, and 7.2 has 0.35%% of its weights pinned at 1.98)")
    ap.add_argument("--layers", metavar="L1,L2",
                    help="two extra layers of L1 and L2 clipped units after the hidden units, one set per output "
                         "bucket (16,32 is what the engine runs; network file version 4; needs --screlu)")
    ap.add_argument("--loss-power", type=float, default=2.0, metavar="P",
                    help="train on |error|^P (2 = squared error, as before); validation also prints the squared "
                         "error so runs with different P compare")
    ap.add_argument("--cpu-decode", action="store_true",
                    help="on a GPU, unpack the records on the CPU as before Nov 22 (default: on the GPU)")
    ap.add_argument("--tf32", action="store_true",
                    help="TF32 matrix products on NVIDIA GPUs (Ampere on): faster for --ft dense, less precise")
    args = ap.parse_args()
    if not 1 <= args.output_buckets <= 8:
        raise SystemExit("--output-buckets must be between 1 and 8")
    if not 0 < args.ft_clip <= FT_CLIP_MAX:
        raise SystemExit(f"--ft-clip must be at most {FT_CLIP_MAX:.3f} (a bias and 32 pieces in 16 bits)")
    if args.fine_output and not args.screlu:
        raise SystemExit("--fine-output needs --screlu")
    layers = tuple(int(x) for x in args.layers.split(",")) if args.layers else None
    if layers and (len(layers) != 2 or not args.screlu or args.fine_output):
        raise SystemExit("--layers takes two sizes (16,32), needs --screlu, and has its own output format "
                         "(no --fine-output)")
    if layers and layers != ENGINE_LAYERS:
        print(f"note: the engine only runs --layers {ENGINE_LAYERS[0]},{ENGINE_LAYERS[1]}")
    if args.factorize and args.king_buckets == 1:
        raise SystemExit("--factorize needs --king-buckets")
    if args.bucket_decay and not args.factorize:
        raise SystemExit("--bucket-decay needs --factorize")

    paths = sorted(p for pattern in args.data for p in glob.glob(pattern))
    if not paths:
        raise SystemExit("no data files found")
    device = "cuda" if torch.cuda.is_available() else "cpu"
    # Off by default: with the sparse first layer the only matrix product
    # left is the small output layer, so TF32 saves little, and it rounds
    # inputs to 10 bits of mantissa.
    torch.backends.cuda.matmul.allow_tf32 = args.tf32
    # On a GPU the records are unpacked there: decode() takes one CPU core
    # 20-30 ms a batch, as long as a whole GPU step can take.
    gpu_decode = device == "cuda" and not args.cpu_decode
    data = Dataset(paths)
    val = data.validation()
    os.makedirs(args.out, exist_ok=True)
    print(f"{len(paths)} files, {data.size:,} training positions, {args.hidden} hidden units, "
          f"{'squared clipped (SCReLU)' if args.screlu else 'clipped'} units, "
          f"{args.output_buckets} output layer{'s' if args.output_buckets > 1 else ''}, "
          f"{args.king_buckets} king bucket{'s' if args.king_buckets > 1 else ''}"
          f"{' (factorised)' if args.factorize else ''}, device {device}")

    torch.manual_seed(args.seed)
    model = NNUE(args.hidden, args.screlu, args.output_buckets, args.king_buckets, args.factorize, layers).to(device)
    model.sparse_ft = args.ft == "sparse"
    model.ft_clip = args.ft_clip
    if args.init_from and not args.resume:
        src = torch.load(args.init_from, map_location=device)
        sm = net_state(src)
        if (src.get("screlu", False) != args.screlu or sm["out.weight"].shape[0] != args.output_buckets
                or sm["ft.weight"].shape[1] != args.hidden):
            raise SystemExit("--init-from: the checkpoint has different --hidden / --screlu / --output-buckets")
        src_kb = src.get("king_buckets", sm["ft.weight"].shape[0] // INPUTS)
        src_w = sm["ft.weight"]
        if src.get("factorize", False):  # fold the source's shared weights into its buckets
            rows = INPUTS * src_kb
            src_w = (src_w[:rows].view(src_kb, INPUTS, -1) + src_w[rows:]).reshape(rows, -1)
        rows = INPUTS * args.king_buckets
        with torch.no_grad():
            model.ft.weight.zero_()
            if src_kb == args.king_buckets:
                model.ft.weight[:rows].copy_(src_w)
            elif src_kb == 1 and args.factorize:
                model.ft.weight[rows:].copy_(src_w)  # shared weights = the source's, own weights 0
            elif src_kb == 1:
                model.ft.weight.copy_(src_w.repeat(args.king_buckets, 1))
            else:
                raise SystemExit("--init-from: can only start king buckets from a network without them")
            model.ft.bias.copy_(sm["ft.bias"])
            model.out.weight.copy_(sm["out.weight"])
            model.out.bias.copy_(sm["out.bias"])
            if layers and tuple(src.get("layers") or ()) == layers:
                for name in ("l1", "l2", "l3"):
                    getattr(model, name).load_state_dict({k[3:]: v for k, v in sm.items() if k.startswith(name + ".")})
            elif layers:
                print("--init-from: first layer only (the source has other or no extra layers)")
        print(f"started from {args.init_from} (epoch {src.get('epoch', '?')})")
    opt = torch.optim.Adam(model.parameters(), lr=args.lr)
    # The learning rate falls by 1 / --lr-end (10x by default) over the run, on a cosine.
    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=args.epochs, eta_min=args.lr * args.lr_end)
    start_epoch = 1
    if args.resume:
        state = torch.load(args.resume, map_location=device)
        model_state = upgrade_state(state)
        if (state.get("screlu", False) != args.screlu or model_state["out.weight"].shape[0] != args.output_buckets
                or model_state["ft.weight"].shape[0] != model.ft.in_features
                or state.get("factorize", False) != args.factorize
                or tuple(state.get("layers") or ()) != tuple(layers or ())):
            raise SystemExit("the checkpoint was trained with different --screlu / --output-buckets / --king-buckets / "
                             "--factorize / --layers settings")
        model.load_state_dict(model_state)
        if state.get("ft_input_major", False):
            opt.load_state_dict(state["opt"])
        else:
            # The optimizer's moments are for the old (hidden, inputs) layout:
            # transpose them too.
            opt_state = state["opt"]
            for v in opt_state["state"].values():
                for key in ("exp_avg", "exp_avg_sq"):
                    if key in v and v[key].dim() == 2 and v[key].shape == model_state["ft.weight"].t().shape:
                        v[key] = v[key].t().contiguous()
            opt.load_state_dict(opt_state)
        sched.load_state_dict(state["sched"])
        start_epoch = state["epoch"] + 1
        print(f"resumed from {args.resume} (epoch {state['epoch']})")

    # The moving average: decay per step so that it spans about --ema of an
    # epoch, whatever the data size (a fixed decay would cover several
    # epochs of a small dataset and a sliver of a large one).
    ema, ema_decay = None, 0.0
    if args.ema:
        steps = max(1, (args.limit or data.size) // args.batch)
        ema_decay = 1 - 1 / max(1.0, args.ema * steps)
        ema = copy.deepcopy(model)
        if args.resume and "ema" in state:
            ema.load_state_dict(upgrade_state({**state, "model": state["ema"]}))
        print(f"moving average over about {args.ema * steps:,.0f} steps (decay {ema_decay:.6f})")

    rng = np.random.default_rng(args.seed + start_epoch)
    for epoch in range(start_epoch, args.epochs + 1):
        model.train()
        wdl = args.wdl if args.wdl_end is None or args.epochs == 1 else \
            args.wdl + (args.wdl_end - args.wdl) * (epoch - 1) / (args.epochs - 1)
        # The loss is summed on the device: .item() every batch would make the
        # CPU wait for the GPU each step instead of queueing the next one.
        t0, total, n = time.time(), torch.zeros((), dtype=torch.float64, device=device), 0
        for records in prefetch(data.batches(args.batch, rng), prepare=raw_bytes if gpu_decode else decode):
            loss = batch_loss(model, records, device, wdl, args.k, args.loss_power)
            opt.zero_grad()
            loss.backward()
            opt.step()
            if args.bucket_decay:
                with torch.no_grad():
                    model.ft.weight[:INPUTS * args.king_buckets].mul_(1 - opt.param_groups[0]["lr"] * args.bucket_decay)
            model.clip()
            if ema is not None:
                with torch.no_grad():
                    torch._foreach_lerp_(list(ema.parameters()), list(model.parameters()), 1 - ema_decay)
            total += loss.detach()
            n += 1
            if args.limit and n * args.batch >= args.limit:
                break
            if n % 500 == 0:
                print(f"  epoch {epoch} batch {n}: loss {total.item() / n:.6f} ({n * args.batch / (time.time() - t0):,.0f} pos/s)", flush=True)
        sched.step()
        model.eval()
        with torch.no_grad():
            val_loss = batch_loss(model, val, device, wdl, args.k, args.loss_power).item()
            ema_note = ""
            if ema is not None:
                ema.eval()
                ema_note = f", moving average {batch_loss(ema, val, device, wdl, args.k, args.loss_power).item():.6f}"
            if args.loss_power != 2.0:
                ema_note += f" (squared error {batch_loss(model if ema is None else ema, val, device, wdl, args.k).item():.6f})"
        print(f"epoch {epoch}: train {total.item() / max(n, 1):.6f}, validation {val_loss:.6f}{ema_note}, "
              f"{time.time() - t0:.0f} s", flush=True)
        saved = {"model": model.state_dict(), "opt": opt.state_dict(), "sched": sched.state_dict(), "epoch": epoch,
                 "screlu": args.screlu, "king_buckets": args.king_buckets, "ft_input_major": True,
                 "factorize": args.factorize, "layers": layers}
        if ema is not None:
            saved["ema"] = ema.state_dict()
        torch.save(saved, os.path.join(args.out, "checkpoint.pt"))
        export(model if ema is None else ema, os.path.join(args.out, f"net-{epoch}.nnue"), args.fine_output)


if __name__ == "__main__":
    main()
