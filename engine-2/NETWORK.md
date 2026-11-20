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
| v14 | in training | 1024 | + **4 king buckets, mirrored** | 7.0's data | 20 | not yet measured |

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

### Result weight (`--wdl`, the game result's share of the target)
- 0.25 -> 0.4 (run 9): **-2 +/- 10** at 8+0.08 (H0, 1,670 games), but
  **+15 +/- 21** at 40+0.4 against run 8's net. It has stayed at 0.4
  since 6.0. The weight barely matters at this data size.

### Activation and output layers (same data, so a clean comparison)
- SCReLU (squared clipped units), 7.1: **+14 STC, +6 LTC**, about 5%
  slower per node.
- 8 output buckets by piece count, 7.2: **+27 STC, +8 LTC**, about 6%
  slower per node.
- Gains are smaller at the longer time control in both cases: speed
  matters less there, and the evaluation matters less once the search is
  deeper.

### King input buckets (v14, in training)
- 4 zones for each side's own king, mirrored left-right when that king is
  on files e-h.
- The engine keeps a refresh cache ("Finny table") per side and zone.
- Costs about 5% nodes/s (a 4x larger first layer, 6.3 MB, misses the
  cache more).
- Other engines report +20 to +50 for king buckets.

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

**Watch out:**
- **2048 hidden units can overflow.** The SCReLU output sum is a 32-bit
  integer. With 7.2's weights the worst case is 0.90 of its range, so
  1024 units are safe but 2048 units are not. Sum each half separately
  and divide by QA before adding, before training a 2048-unit net.
- **Endgame output weights are capped.** 23 of the 2-5 piece output
  layer's 2048 weights sit at the +/-127 limit that SCReLU's 16-bit
  multiply needs. Mildly limiting in bare endgames only.

## 7. Next steps

The owner's roadmap (one unproven network change per run):

| Run | Change | Notes |
|---|---|---|
| v14 | 4 king buckets, mirrored | Training now; biggest expected gain |
| v15 | Training schedule: more epochs, `--lr-end 0.01`, maybe a `--wdl` schedule | Cheap; can ride on v14 |
| v16 | 8-16 king zones and/or 1536-2048 hidden units, with more data | Fix the int32 output sum first (section 6) |
| later | Extra small hidden layers, pairwise multiplication, threat inputs | The biggest engine projects |

Also worth doing: fresh data from 7.2 or later. Every data run so far
came from older engines, and better labels have paid every time.

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

**To check an exported net against the engine:**
```bash
python scripts/nnue/check_net.py <out>/checkpoint.pt <out>/net-20.nnue <data>.bin build/engine2
```
