"""Writes an engine network file from a train.py checkpoint, without training:

    python scripts/nnue/export.py <run>/checkpoint.pt out.nnue [--fine-output]

Use it to re-export an existing run in another format, e.g. a SCReLU run
(7.2's v13) with fine output weights (x1024 instead of x64: about 0.7 cp
of rounding error in the output layer instead of 6). The hidden units,
activation, output and king buckets and the factoriser come from the
checkpoint, and so do --layers nets' extra layers (file version 4).

--scale F multiplies the output layer by F, so every evaluation is F times
larger: the net prefers exactly the same positions, on another scale. Use
it to put a candidate on the reference net's scale (diagnose.py --net REF
--net CANDIDATE prints the F to use), so an SPRT tests the evaluation and
not a shift in every pruning margin the search was tuned with.
"""
import argparse

import torch

from train import NNUE, export, net_state


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("checkpoint")
    ap.add_argument("out")
    ap.add_argument("--fine-output", action="store_true", help="output weights x1024 (SCReLU networks only)")
    ap.add_argument("--scale", type=float, default=1.0, metavar="F", help="multiply every evaluation by F")
    args = ap.parse_args()
    saved = torch.load(args.checkpoint, map_location="cpu")
    state = net_state(saved)
    factorize = saved.get("factorize", False)
    king_buckets = saved.get("king_buckets", state["ft.weight"].shape[0] // 768)
    model = NNUE(state["ft.weight"].shape[1], saved.get("screlu", False), state["out.weight"].shape[0], king_buckets,
                 factorize, saved.get("layers"))
    model.load_state_dict(state)
    if args.scale != 1.0:
        last = model.l3 if model.layers else model.out
        with torch.no_grad():
            last.weight.mul_(args.scale)
            last.bias.mul_(args.scale)
        if not model.layers and not args.fine_output and model.screlu and model.out.weight.abs().max() > 127 / 64:
            raise SystemExit("--scale takes the output weights past the x64 format's limit: add --fine-output")
    export(model, args.out, args.fine_output)
    print(f"{args.out}: epoch {saved.get('epoch', '?')}, {model.ft.out_features} hidden units, "
          f"{'SCReLU' if model.screlu else 'clipped'}{' (fine output)' if args.fine_output else ''}, "
          f"{model.buckets} output layer(s), {king_buckets} king bucket(s){' (factorised)' if factorize else ''}"
          f"{f', extra layers {model.layers}' if model.layers else ''}"
          f"{f', evaluations x{args.scale}' if args.scale != 1.0 else ''}")


if __name__ == "__main__":
    main()
