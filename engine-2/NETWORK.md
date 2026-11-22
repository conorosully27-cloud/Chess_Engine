# The network: what we have learned

Everything about Engine 2's NNUE evaluation in one place: every network
we released, what each change to data, size and architecture was worth,
what failed, and what was done to the trainer. Elo figures are from the
SPRTs in [`CHANGELOG.md`](CHANGELOG.md) (fastchess, both colors per
opening, 95% error margins); failed ideas are in full in
[`FAILED_EXPERIMENTS.md`](FAILED_EXPERIMENTS.md) and day-to-day notes in
[`HANDOFF.md`](HANDOFF.md). STC = 8+0.08, LTC = 40+0.4 unless noted.

Contents:
1. [The network today](#1-the-network-today)
2. [Every network released](#2-every-network-released)
3. [What each lever was worth](#3-what-each-lever-was-worth)
4. [Network ideas that failed](#4-network-ideas-that-failed)
5. [The trainer and training algorithm](#5-the-trainer-and-training-algorithm)
6. [Engine-side network speed](#6-engine-side-network-speed)
7. [Next steps](#7-next-steps)
8. [How to train, pause and resume](#8-how-to-train-pause-and-resume)

## 1. The network today

**ChessMcEngineFace 7.2**, `nets/engine-7.2.nnue` (1.6 MB):

```
768 inputs per side (piece kind x square, from that side's view)
  -> 1024 hidden units per side, weights shared by both sides
  -> clipped to [0, 1] and squared (SCReLU)
  -> both sides' units, side to move first (2048)
  -> 1 of 8 output layers, chosen by the number of pieces on the board
```

- **Engine:** integer arithmetic (first layer x255, output x64). The
  hidden sums ("accumulator") are updated move by move and only when a
  position is evaluated. AVX2, or WebAssembly SIMD in the app.
- **Training:** `train.py --hidden 1024 --wdl 0.4 --epochs 20 --screlu --output-buckets 8`.
- **Strength:** about 3550-3565 on CCRL Blitz by 40+0.4 matches against
  Berserk 8, Renegade 1.1.0 and Thrawn 3.2 (measured on 7.0). 7.2 is
  level with Renegade at 8+0.08, where 7.0 was 58 Elo behind.
- **Cost:** the network is about 37% of the engine's instructions
  (callgrind, Nov 20): 24% updating the hidden layer, 12% the output layer.

## 2. Every network released

| Engine | Net file | Hidden units | Extras | Data | Epochs | Gain (vs the version before) |
|---|---|---|---|---|---|---|
| 2.4 | (replaced) | 128 | | 50M from 2.3, 5,000 nodes | epoch 3 of 20 | **+181** over the hand-written evaluation |
| 2.5 | (replaced) | 128 | | same 50M | 20 | **+80** (same run, epoch 20 vs 3) |
| 2.6 | same as 2.5 | 128 | incremental updates | | | **+102** from speed alone |
| 2.8 | (replaced) | 128 | | 150M: 100M from 2.7 + the old 50M | 20 (CPU) | **+105** |
| 2.9 | (replaced) | 256 | | same 150M | 20 | **+25** |
| 3.2 | (replaced) | 256 | | 346.5M (adds 200M from 3.0 with book openings) | 15 | **+88** |
| 3.3 | (replaced) | 512 | faster network code | same 346.5M | 15 | **+46** (the net alone +5 +/- 6) |
| 5.0 | `engine-5.0.nnue` | 512 | | 216M fresh: 3.3, 4.0, 4.5 with book, keep 1/4 | 15 | +21.5 vs Stockfish at 3190 |
| 5.5 | `engine-5.5.nnue` | 512 | | 395M (run 7) | 20 | **+33** (800 games) |
| 5.6 | `engine-5.6.nnue` | 1024 | | run 8 | 20 | **+20** |
| 6.0 | `engine-6.0.nnue` | 1024 | result weight 0.4 | run 9 (= run 8 data) | 20 | +14 together with two pruning changes |
| 7.0 | `engine-7.0.nnue` | 1024 | | new run | 20 | **+28 STC, +19 LTC** |
| 7.1 | `engine-7.1.nnue` | 1024 | **SCReLU** | 7.0's data (run v12) | 20 | **+14 STC, +6 LTC** |
| 7.2 | `engine-7.2.nnue` | 1024 | SCReLU + **8 output buckets** | 7.0's data (run v13) | 20 | **+27 STC, +8 LTC** |
| v14 | not kept | 1024 | + **4 king buckets, mirrored** | 7.0's data (660M) | 20 | **failed against 7.2** (see section 4) |

The network files from 5.0 on are kept in `nets/`. Any of them loads with
`setoption name EvalFile value nets/engine-X.nnue` (the hidden size must
match the build's `-DENGINE_NNUE_HIDDEN`, 1024 by default).

## 3. What each lever was worth

### More (and fresher) data: the most reliable gain
- 50M -> 150M (2.8): **+105**. Keeping the old 50M next to the new 100M
  beat the new 100M alone by **+16 +/- 12**: old data still helps.
- 150M -> 346.5M (3.2): **+88**.
- 216M of fresh data from newer engines (5.0) beat 346.5M of older data
  (3.3's net): the labels of a stronger engine are better labels.
- 216M -> 395M (5.5): **+33**.
- After 2.5, the gap between training and validation loss widened
  (0.0002 -> 0.0003): more data mattered more than more epochs.
- The owner's note on Berserk 8: a 512-unit net yet about 50 Elo
  stronger, mostly from data.
- **Data-generation settings that stuck:**
  - 5,000 nodes a move.
  - 6-11 random opening plies, so either side can move first.
  - Book openings (8moves_v3) plus 2-5 random plies (from 3.2).
  - Keep each quiet position with probability 1/4 (`keep 4`, from 5.0),
    for more games per position.
  - Skip positions in check, with a capture or promotion as best move,
    or with a mate score.

### Hidden units: each doubling paid, less each time
- 128 -> 256 (2.9): **+25**, same data.
- 256 -> 512 (3.3): the net alone **+5 +/- 6**: it evaluated better but
  cost about 20% of nodes/s on the laptop. With faster network code in
  the same release it was **+46**. A bigger net only pays when the code
  keeps up.
- 512 -> 1024 (5.6): **+20**, at about 0.8x the nodes/s.
- Lesson: grow the net together with the data and the speed work.

### Epochs and learning rate
- Epoch 3 -> epoch 20 of the same run (2.5): **+80**.
- Run 8's net-9 was **-26** against the previous release while its
  net-20 was +20: undertrained nets lose. Always test the last epoch.
- The schedule has been a cosine falling 10x since 2.4.
  `--lr-end 0.01` (a 100x fall) is planned for v15.
- CPU tests (10.4M positions, 256 units, no king buckets, 8 epochs):

| Schedule | Validation |
|---|---|
| `--lr-end 0.1` (as now) | 0.021212 |
| `--lr-end 0.01` | 0.021193 |
| `--lr-end 0.01 --ema 0.25` (exported moving average) | **0.021176** |

  Small gains, but free: no change to the engine or to training time.
  The moving average is far ahead mid-run (epoch 2: 0.02195 against
  0.02224), so every `net-N` file is a better net.

### Result weight (`--wdl`, the game result's share of the target)
- 0.25 -> 0.4 (run 9): **-2 +/- 10** at 8+0.08 (H0, 1,670 games), but
  **+15 +/- 21** at 40+0.4 against run 8's net. It has stayed at 0.4
  since 6.0. The weight barely matters at this data size.

### The networks' scale, and why it matters for every SPRT (Nov 22)

Slope of each net's evaluation against 7.2's own search scores
(`diagnose.py`; 1.000 = the search's scale):

| Net | Result weight | Scale |
|---|---|---|
| 5.5 | 0.25 | 1.004 |
| 5.6 | 0.25 | 1.017 |
| 6.0 | 0.4 | 1.069 |
| 7.0 | 0.4 | 1.056 |
| 7.1 | 0.4 | 1.058 |
| 7.2 | 0.4 | 1.071 |

- **Since the result weight went to 0.4, every net evaluates about 6%
  more extreme than its search scores.** The game results are more
  decisive than `sigmoid(score / 400)` says (best fit K is about 300), so
  the result part of the target pulls evaluations outward.
- The search's margins were SPSA-tuned (6.2, 6.7, 6.8) on these
  6%-"hot" nets. **A new net on another scale moves every margin**, so it
  can lose an SPRT while evaluating better. Changes that move the scale
  include `--k`, `--wdl`, king buckets and more data.
- **Fix:** `diagnose.py --net nets/engine-7.2.nnue --net <candidate>`
  prints the factor F that puts the candidate on 7.2's scale, and
  `export.py <checkpoint> <out> --scale F` applies it exactly. That
  multiplies the output layer, so the net prefers exactly the same
  positions.

### Activation and output layers (same data, so a clean comparison)
- SCReLU (squared clipped units), 7.1: **+14 STC, +6 LTC**, about 5%
  slower per node.
- 8 output buckets by piece count, 7.2: **+27 STC, +8 LTC**, about 6%
  slower per node.
- Gains are smaller at the longer time control in both cases: speed
  matters less there, and the evaluation matters less once the search is
  deeper.

### King input buckets (v14): failed on 660M positions
- 4 zones for each side's own king, mirrored left-right when that king is
  on files e-h.
- The engine keeps a refresh cache ("Finny table") per side and zone.
- Costs about 5% nodes/s (a 4x larger first layer, 6.3 MB, misses the
  cache more); the Nov 21 refresh rewrite takes back about 3%.
- Other engines report +20 to +50 for king buckets, but they train on far
  more data. **v14 lost to 7.2** on the same 660M positions; section 4
  has the post-mortem.

### Speed is strength
- Incremental updates alone (2.6): **+102**. Faster network code was
  most of 3.3's +46.
- Rule of thumb from the profile: x% faster network code gives about
  0.4x% more nodes/s.

## 4. Network ideas that failed

Details and commits in `FAILED_EXPERIMENTS.md`.

| Idea | Result | Lesson |
|---|---|---|
| Result weight 0.4 alone (run 9 vs run 8) | -2 +/- 10 STC (H0) | Kept later in a bundle (6.0) for its LTC reading |
| Endgame scaling of the network's score (on 5.2) | flat | The net's endgame errors are not a matter of scale |
| Network correction history (hidden-unit signature as a key, on 6.6) | flat, removed in 7.1.1 | |
| TT evaluation refinement (on 6.x) | -13 +/- 15 | Margins and correction history already cover it |
| Run 8's epoch 9 instead of epoch 20 | -26 +/- 19 | Use the end of the run |
| **v14: 4 king buckets, mirrored** (660M, 20 epochs) | lost to 7.2 | Too little data per bucket; see below |

### The v14 post-mortem (Nov 21)

**No bugs found.** Checked:
- The trainer's king-bucket inputs and mirroring against the engine's.
- Engine vs trainer evaluations (`check_net.py`).
- Incremental vs from-scratch updates through king moves (`nnue_test`,
  with king-bucket nets built in, AVX2 and AVX-512).
- WebAssembly vs native bench node counts.
- Pausing v14 at epoch 5 and resuming with the faster trainer: same losses
  as the old trainer resuming it.
- TF32, which was on for epochs 6-20: only the small output-layer product
  used it, which is harmless, but it gained nothing either and is now off.

**The weakness: too little data per weight.**
- With 4 buckets each first-layer row learns only from positions with
  its own king in that zone.
- In 660M positions the median row sees **1.9M positions, against 8.3M**
  without buckets. 877 rows see fewer than 1M; with 8 buckets the median
  would be 0.77M.
- The buckets are also strongly tied to game phase: bucket 3 (king off
  the back two ranks) is 60% of positions with 2-14 pieces and 0.2% of
  openings.

**Smaller contributors:**
- About 5% fewer nodes/s.
- The first layer started at half v13's weight scale (`1/sqrt(3072)`).
- `--init-from` does not reproduce the source net exactly: with mirroring,
  a side whose king is on e-h sees the board flipped, which v13's weights
  never saw.

**CPU experiments** (2.5M positions from 7.2 at 800 nodes, 256 hidden
units, 8 epochs; final validation loss):

| Net | Validation | vs no buckets |
|---|---|---|
| No king buckets | **0.02391** | |
| 4 king buckets (v14) | 0.02491 | 4.2% worse |
| 4 king buckets + factoriser | 0.02471 | 3.4% worse |
| + factoriser, `--lr-end 0.01` | 0.02465 | |
| + factoriser, started from the plain net | 0.02450 | |
| + factoriser, `--bucket-decay 1` | 0.02415 | 1.0% worse |
| **+ factoriser, `--bucket-decay 10`** | **0.02373** | **0.7% better** |
| + factoriser, `--bucket-decay 100` | 0.02379 | 0.5% better |

- Without the decay, the king-bucket nets overfit: lower training loss,
  higher validation loss, and at 16 epochs they got worse faster than
  the plain net (0.02620 vs 0.02417).
- **The factoriser with bucket decay is the first king-bucket setup to beat
  no buckets.** Each bucket's own weights are pulled toward zero, so where
  a bucket has little data the net falls back on the shared weights, and
  where it has plenty the data wins.
- This is 260x less data than v14 had, so it shows the mechanism, not the
  size of the effect at 660M. More data should need a weaker decay.

**At 4x the data** (10.4M positions, same settings; a different
validation set, so compare within this table):

| Net | Validation after 8 epochs | vs no buckets | At 2.5M |
|---|---|---|---|
| No king buckets | 0.021212 | | |
| 4 king buckets (v14) | 0.021740 | 2.5% worse | 4.2% worse |
| 4 king buckets + factoriser, `--bucket-decay 10` | 0.021015 | 0.93% better | 0.7% better |
| **4 king buckets + factoriser, `--bucket-decay 1`** | **0.021006** | **0.97% better** | 1.0% worse |

- **v14's design is data-starved:** its deficit shrinks with data (4.2% ->
  2.5% for 4x the data) but it is still behind at 10.4M. On 660M it
  probably ended about level in accuracy and lost on its 5% speed cost,
  which fits v14's result.
- **With the factoriser and decay, king buckets beat no buckets at both
  sizes, by more with more data.** They were still improving at epoch 8;
  the plain net had stopped (0.021212 at epochs 7 and 8).
- **The best decay falls as data grows:** 10 was best at 2.5M, 1 at
  10.4M (and 1 learned faster early). For 660M+, use `--bucket-decay 1`.

**The v15 recipes side by side** (10.4M positions, 256 units, 8 epochs;
the exported net's validation loss):

| Recipe | Validation | vs the old recipe |
|---|---|---|
| Old: no buckets, `--lr-end 0.1` | 0.021212 | |
| v15a: + `--lr-end 0.01 --ema 0.25` | 0.021176 | 0.17% better |
| **v15b: v15a + `--king-buckets 4 --factorize --bucket-decay 1`** | **0.020880** | **1.6% better** |

v15b's moving average was best at epoch 5 (0.020820) and drifted up
slightly after: mild overfitting at this size, which should matter far
less with 660M positions.

**In games** (the two exported nets above, v15b against v15a, both 256
units, each built into the engine; 8+0.08, 8moves_v3, 4 at a time,
`scripts/match.sh`): **v15b +33.6 +/- 11.9 Elo over 1,640 games**, +519
-361 =760, LOS 100%, pentanomial [37, 155, 316, 237, 75], no time losses. The validation gain
shows up in play, after the king buckets' speed cost. These are small
nets on 10M positions, so the size of the gain at 660M and 1024 units
is for the real SPRT to show.

## 5. The trainer and training algorithm

`scripts/nnue/train.py` (PyTorch), `scripts/nnue/data.py` (data format,
32 bytes a position) and `scripts/nnue/check_net.py` (the engine's
integer evaluation against the trainer's, to catch export bugs).

### The loss
- Squared error between win probabilities: `sigmoid(eval / 400)` against
  a blend of `sigmoid(search score / 400)` and the game result
  (`--wdl`, 0.4 now).
- Adam, learning rate 1e-3 falling on a cosine, batch 16,384.
- Weights are clipped so the engine's 16-bit arithmetic cannot overflow:
  first layer to +/-1.98; with SCReLU, output layer to +/-127/64.

### Changes to the trainer, in order

| When | Change | Effect |
|---|---|---|
| 2.4 | First trainer: dense one-hot inputs, Adam, cosine 10x, GPU if present | 128-unit nets in ~13 min on the RTX A2000 |
| 2.5 | Data memory-mapped, checkpoint each epoch, `--resume` | Datasets bigger than memory; restartable |
| 3.2 | **Shuffle buffer**: chunks of consecutive positions in random order, mixed 16M at a time, instead of random single positions | **13.5k -> 220-280k positions/s** on the laptop GPU (~26 min an epoch on 346M) |
| 3.2 | Validation from every file in proportion | The validation set matches the training mix |
| 2.9 / 3.3 / 5.6 | `--hidden N` | 256, 512, 1024 units |
| 6.0 | Runs use `--wdl 0.4` (the flag's default is still 0.25) | See section 3 |
| 7.1 | `--screlu`; network file version 2 | +14 STC |
| 7.2 | `--output-buckets N` | +27 STC |
| v14 | `--king-buckets 4/8`, `--init-from` (start from another run, its input weights copied into every bucket); input-major first layer; sparse first layer (embedding_bag) for king-bucket nets; old checkpoints upgraded on load | A 4-bucket epoch costs about what a plain one did; network file version 3 |
| Nov 20 | **Faster trainer, same results** (below) | **84k -> 215k positions/s** on the laptop GPU (king buckets); 1.7x on CPU |
| Nov 20 | `--lr-end F` | The learning rate ends at F x `--lr` (0.1 as before, 0.01 for v15) |
| Nov 21 | `--factorize` (king buckets): 768 shared input weights every bucket adds to, folded into the buckets on export | Rare (bucket, piece, square) inputs learn from all the data; engine and file unchanged |
| Nov 21 | `--bucket-decay L` (with `--factorize`): shrinks the buckets' own weights toward the shared ones | A bucket with little data stays close to a plain net |
| Nov 21 | `--fine-output` and `scripts/nnue/export.py` (re-export any checkpoint) | Output weights x1024: 9x less rounding error |
| Nov 21 | `--k K` and a K fit in `data.py` | Score -> win probability scale; 300 fits 7.2's games better than 400 |
| Nov 21 | `--wdl-end W` | Result weight moving from `--wdl` to W over the run |
| Nov 21 | TF32 opt-in (`--tf32`) | No speed left to gain with the sparse first layer |
| Nov 22 | Factoriser folds the shared weights into the buckets once per batch | Same losses to the last digit; no second lookup per piece |
| Nov 22 | `scripts/nnue/diagnose.py`: data and network health report | See below |
| Nov 22 | `--ema F`: a moving average of the weights over about F of an epoch, exported | 0.08% better validation at the end (10.4M positions, with `--lr-end 0.01`); the average's epoch 6 matched the live weights' epoch 8 |
| Nov 22 | `export.py --scale F` | Puts a net on another net's scale (section 3) |
| Nov 22 | `--ft-clip C` (first-layer limit, 1.98 as before, up to 3.89) | 7.2 has 2,730 first-layer weights (0.35%, in 584 of 1,024 units) pinned at 1.98, yet its largest possible sum is 16,009 of the 32,767 a 16-bit accumulator holds. The 1.98 was the output layer's x64 limit. Engine checked with weights up to 3.85. Not testable on CPU: the test nets' largest first-layer weights were 0.8-1.1, so the limit never bound (identical losses) |
| Nov 22 | `--ft sparse` (the default) for every net | The king-bucket merge had left plain nets on the dense path: a plain 1024-unit epoch 22 s -> 10 s on CPU, same losses |

### Diagnosing a network before playing it

```bash
python scripts/nnue/diagnose.py <fresh data>.bin --net nets/engine-7.2.nnue --net <candidate>.nnue
```

- **For the data:** the best K, the piece-count mix, and how many
  examples each first-layer row gets with 1, 4 or 8 king buckets, scaled
  to the dataset's size.
- **For each net (any file version):** eval against search score and
  against game results for each output bucket, the output-layer rounding
  error, and the net's scale (with the factor that matches the first net
  given).
- Use data the nets were not trained on, ideally fresh games from the
  current engine.
- 7.2 against 7.1 on 7.2's games: the median eval-to-search error is lower
  in every bucket from 18 pieces up (e.g. 40.7 vs 46.7 cp at 26-29
  pieces), where the output buckets were expected to help.

### Training speed on the GPU
- With king buckets, each step reads about 20 rows of 1024 floats per
  position per side: about 2.7 GB forward and as much again backward at
  batch 16,384. That is most of an ~80 ms step on the A2000.
- **Worth one test: `--ft dense --tf32`.** The dense product runs on
  tensor cores, and sparse and dense agree to 3e-7. Compare the `pos/s`
  lines over a few hundred batches.

### The Nov 20 speedups in detail

1. **Swap the inputs, not the hidden units.** To put the side to move
   first, the trainer used to build two (B, 2048) tensors and pick
   between them (`torch.where` + `torch.cat`). That took about a third of
   each step, more than the first layer's matrix product itself. Now it
   swaps the inputs (32-64 numbers per position) and multiplies each half
   by its half of the output weights.
2. **Sparse first layer:** at most 32 of the 768 inputs are on, so the
   layer adds up 32 weight rows (`embedding_bag`) instead of multiplying
   a mostly-zero one-hot matrix.
3. **Prefetch:** batches are decoded a few ahead in a background thread,
   so the GPU does not wait for the CPU's share.
4. **TF32** matrix products on Ampere or newer GPUs (the laptop's A2000).
   Off again since Nov 21 (`--tf32` turns it on): with the sparse first
   layer only the small output layer uses it.

**Checks:**
- Plain net, 2 epochs on 600k positions: identical losses to 6 digits and
  byte-identical `.nnue` files, 31 s vs 52 s on the cloud CPU.
- King-bucket net, an old-trainer checkpoint resumed by the new trainer:
  the same losses as the old trainer resuming it, 11 s vs 19 s an epoch.
  3 of 3.16M weights differ by one rounding step (floating-point sums in
  a different order).

**On the owner's laptop GPU (RTX A2000, Nov 20):** the v14 king-bucket
run went from **about 84k to about 215k positions/s (2.6x)** after it was
paused and resumed with the new trainer. That is about 30 minutes an epoch
on ~400M positions instead of about 80. The GPU gains more than the CPU
because the removed tensor shuffling was limited by memory traffic.

## 6. Engine-side network speed

| When | Change | Effect |
|---|---|---|
| 2.6 | Incremental, lazy accumulator updates; vectorized output layer | 1.14M -> 1.46M nodes/s, **+102 Elo**; network 42% -> 8% of instructions |
| 3.3 | AVX2 updates 32 units a step; one-pass clamping | +4% nodes/s on the cloud, more on the laptop |
| 5.3 | Link-time optimization | about +2% |
| 2.x | WebAssembly SIMD for the phone app | 1.5x plain WebAssembly |
| v14 | Per-side accumulators and a refresh cache for king-bucket zone changes | King-bucket nets at about 5% fewer nodes/s |
| Nov 20 | `-DENGINE_AVX512=ON` (off by default) | Same moves; roughly +5-15% nodes/s on an AVX-512 Xeon (noisy cloud VM) |
| Nov 21 | Refresh cache: one register-blocked pass for all changed pieces instead of one pass per piece plus a copy | -2.9% instructions for the bench with a king-bucket net, same moves |
| Nov 21 | **Fine output weights** (activation 2): a rounding multiply-high (`pmulhrsw`) keeps unit x weight in 16 bits with weights x1024 | Output rounding error on 7.2's net 6.5 -> 0.7 cp (simulated); +1.2% instructions per node |
| Nov 22 | WebAssembly SIMD path for the fine output (`q15mulr_sat`, `dot_i16x8`) | The plain loop made the app 10% slower with a fine net; written out, a fine net runs at least as fast as an x64 one (same node counts as native) |

**Output-layer precision (Nov 21).** The output weights are rounded to
1/64. On 7.2's real weights that adds about **6.5 cp of evaluation error
on average (22 cp at the 99th percentile)**, the same in balanced
positions as in lopsided ones, and 3x the first layer's rounding error
(2.2 cp). The fine format fixes it:

| Output weights | Mean error | 99th percentile |
|---|---|---|
| x64 (all nets up to 7.2) | 6.46 cp | 21.9 cp |
| x128, unit halved first | 3.31 cp | 12.9 cp |
| **x1024, rounding multiply-high (fine)** | **0.74 cp** | **2.6 cp** |

Measured on a trained test net, engine vs trainer: 10.2 cp -> 3.5 cp
(plain), 11.5 cp -> 3.3 cp (4 king buckets). The fine format also:
- removes the +/-127 output-weight limit (up to +/-32 fits);
- gives the int32 output sum 16x more headroom. **2048 hidden units are
  only safe with fine output weights:** with x64 weights, 7.2's worst case
  is already 0.90 of the int32 range at 1024 units.

## 7. Next steps

The owner's roadmap (one unproven network change per run):

| Run | Change | Notes |
|---|---|---|
| v14 | 4 king buckets, mirrored | **Failed** (section 4) |
| **7.2.1** | **v13's checkpoint re-exported with `--fine-output`** | No training: `export.py`, then an SPRT against 7.2. Needs this engine |
| v15a | v13's design + `--lr-end 0.01 --ema 0.25`, exported with `--fine-output` | Training-only tweaks: 0.17% better validation together at 10.4M positions; add `--ft-clip 3.85` if testing it too |
| v15b | 4 king buckets + `--factorize --bucket-decay 1`, same schedule | King buckets again, with the fix for what sank v14 (section 4): 1.4% better validation than v15a and **+34 +/- 12 Elo over v15a in 1,640 games** (small nets, 10.4M positions) |
| data | **Fresh data from 7.2**, as much as possible, added to the 660M | The most reliable gain in this project's history; king buckets need it most. `keep 4` searches 4 quiet positions for each 1 it keeps; `keep 2` doubles the positions per hour (neighbouring positions are more alike, so worth a comparison) |
| later | `--k` from `data.py`'s fit; 8 king buckets or 1536-2048 units (fine output first); extra hidden layers, threat inputs | |

To test a bundle against 7.2, one change per run is still the rule; the
re-export (7.2.1) is free and tests the fine format on its own.

## 8. How to train, pause and resume

```bash
python scripts/nnue/train.py "C:/Users/conor/nnue-data/*.bin" --out C:/Users/conor/nnue-nets/v14 --hidden 1024 --wdl 0.4 --epochs 20 --screlu --output-buckets 8 --king-buckets 4
```

**To pause and resume:**
1. Stop with Ctrl+C right after an `epoch N:` line. The checkpoint is
   written at the end of each epoch, so a partly done epoch is lost.
2. Run the same command again with `--resume <out>/checkpoint.pt`.
   Keep `--epochs` the same: it sets the learning-rate schedule's length.
   A resumed run keeps its original `--lr-end`.

Checkpoints from every trainer version load in the current one.

**v15 commands.** First the free test: re-export v13's last checkpoint
(the one `nets/engine-7.2.nnue` came from). Without `--fine-output` the
file should be byte-identical to `nets/engine-7.2.nnue`, which confirms
it is the right checkpoint:
```bash
python scripts/nnue/export.py <v13 run>/checkpoint.pt nets/candidate-7.2.1-fine.nnue --fine-output
```
```bash
python scripts/nnue/train.py "C:/Users/conor/nnue-data/*.bin" --out C:/Users/conor/nnue-nets/v15a --hidden 1024 --wdl 0.4 --epochs 20 --screlu --output-buckets 8 --lr-end 0.01 --ema 0.25 --fine-output
```
```bash
python scripts/nnue/train.py "C:/Users/conor/nnue-data/*.bin" --out C:/Users/conor/nnue-nets/v15b --hidden 1024 --wdl 0.4 --epochs 20 --screlu --output-buckets 8 --lr-end 0.01 --ema 0.25 --fine-output --king-buckets 4 --factorize --bucket-decay 1
```
Before an SPRT, check each candidate's scale against 7.2 on fresh data:
```bash
python scripts/nnue/diagnose.py <fresh 7.2 data>.bin --net nets/engine-7.2.nnue --net <run>/net-20.nnue
```
If the printed F is outside about 0.97-1.03, re-export with
`export.py <run>/checkpoint.pt <out> --fine-output --scale F` and test that
file, so the search's tuned margins still fit.
Fine nets need an engine from Nov 21 on (older ones refuse them).

**To check an exported net against the engine:**
```bash
python scripts/nnue/check_net.py <out>/checkpoint.pt <out>/net-20.nnue <data>.bin build/engine2
```
