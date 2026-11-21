"""Writes an engine network file from a train.py checkpoint, without training:

    python scripts/nnue/export.py <run>/checkpoint.pt out.nnue [--fine-output]

Use it to re-export an existing run in another format, e.g. a SCReLU run
(7.2's v13) with fine output weights (x1024 instead of x64: about 0.7 cp
of rounding error in the output layer instead of 6). The hidden units,
activation, output and king buckets and the factoriser come from the
checkpoint.
"""
import argparse

import torch

from train import NNUE, export, upgrade_state


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("checkpoint")
    ap.add_argument("out")
    ap.add_argument("--fine-output", action="store_true", help="output weights x1024 (SCReLU networks only)")
    args = ap.parse_args()
    saved = torch.load(args.checkpoint, map_location="cpu")
    state = upgrade_state(saved)
    factorize = saved.get("factorize", False)
    king_buckets = saved.get("king_buckets", state["ft.weight"].shape[0] // 768)
    model = NNUE(state["ft.weight"].shape[1], saved.get("screlu", False), state["out.weight"].shape[0], king_buckets,
                 factorize)
    model.load_state_dict(state)
    export(model, args.out, args.fine_output)
    print(f"{args.out}: epoch {saved.get('epoch', '?')}, {model.ft.out_features} hidden units, "
          f"{'SCReLU' if model.screlu else 'clipped'}{' (fine output)' if args.fine_output else ''}, "
          f"{model.buckets} output layer(s), {king_buckets} king bucket(s){' (factorised)' if factorize else ''}")


if __name__ == "__main__":
    main()
