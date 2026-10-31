#!/usr/bin/env python3
"""SPSA tuning of the search parameters (src/tune.h), with fastchess.

    python3 scripts/tune/spsa.py --engine build-tune/engine2 --pairs 4000

The engine must be a tuning build (cmake -DENGINE_TUNE=ON), which exposes
the parameters as UCI options; their names, defaults and ranges are read
from it. Each step plays one game pair (same opening, colors swapped)
between theta + c*delta and theta - c*delta, delta a random +-1 per
parameter, and moves theta along delta by the result, the way fishtest
does: a_k = a / (A + k)^0.602, c_k = c / k^0.101, with each parameter's
c_end (perturbation at the end) below and r_end = a_end / c_end^2.
Several pairs run at once (--workers), each against the latest theta.

State (theta and the pair count) is saved after every pair to --state, so
an interrupted run continues where it left off when started again with the
same arguments. Progress goes to stdout. --params tunes only the listed
parameters (the rest keep their defaults), e.g.
    --params LMR_HISTORY,CORR_LIMIT_CP,CORR_MAX_WEIGHT,CORR_NON_PAWN_RATE
Each step moves theta by about r_end * c_end per unit of game result, so
fishtest's r_end suits tens of thousands of pairs; for a run of a few
thousand, --r-end 0.005 --c-scale 2 moves about 5x as far. A new run can
start from an old run's values: write its theta into a fresh state file
with pairs 0.
"""
import argparse
import json
import math
import os
import random
import re
import subprocess
import tempfile
import threading
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ENGINE2 = os.path.dirname(os.path.dirname(HERE))

# Perturbation at the end of the run, in each parameter's own units: about
# the smallest change expected to show in a few thousand games.
C_END = {
    "RFP_DEPTH": 1, "RFP_MARGIN": 10,
    "FUT_DEPTH": 1, "FUT_BASE": 15, "FUT_MARGIN": 15,
    "LMP_DEPTH": 1, "LMP_BASE": 1, "LMP_SCALE": 15,
    "SEE_PRUNE_DEPTH": 1, "SEE_QUIET_MARGIN": 4, "SEE_CAPTURE_MARGIN": 12,
    "LMR_BASE": 12, "LMR_DIV": 20,
    "SINGULAR_DEPTH": 1, "SINGULAR_MARGIN": 4,
    "LMR_HISTORY": 400,
    "HIST_BONUS_MUL": 25, "HIST_BONUS_SUB": 40, "HIST_BONUS_MAX": 250, "HIST_GRAVITY": 2000, "CONT_HIST_GRAVITY": 2000, "CAPT_HIST_GRAVITY": 2000,
    "CORR_LIMIT_CP": 8, "CORR_MAX_WEIGHT": 3, "CORR_NON_PAWN_RATE": 6,
    "TM_STABLE_BASE": 15, "TM_STABLE_STEP": 3, "TM_STABLE_DEPTHS": 1, "TM_DROP_PER_CP": 1, "TM_DROP_MAX": 10,
}
R_END = 0.002
ALPHA, GAMMA = 0.602, 0.101

# The same adjudication as scripts/match.sh.
ADJUDICATE = ["-resign", "movecount=3", "score=600", "twosided=true",
              "-draw", "movenumber=34", "movecount=8", "score=20"]
RESULT = re.compile(r"Games: (\d+), Wins: (\d+), Losses: (\d+), Draws: (\d+)")
OPTION = re.compile(r"option name (\S+) type spin default (-?\d+) min (-?\d+) max (-?\d+)")


def engine_params(engine, only=None):
    wanted = set(only) if only else set(C_END)
    unknown = wanted - set(C_END)
    if unknown:
        raise SystemExit(f"no perturbation size (C_END) for: {sorted(unknown)}")
    out = subprocess.run([engine], input="uci\nquit\n", capture_output=True, text=True, timeout=30).stdout
    params = {}
    for name, default, lo, hi in OPTION.findall(out):
        if name in wanted:
            params[name] = (int(default), int(lo), int(hi))
    missing = wanted - set(params)
    if missing:
        raise SystemExit(f"not a tuning build, or missing options: {sorted(missing)}")
    return params


def find_fastchess():
    for path in (os.environ.get("FASTCHESS"), os.path.expanduser("~/tools/fastchess/fastchess")):
        if path and os.access(path, os.X_OK):
            return path
    return "fastchess"


def opening_book():
    os.makedirs(os.path.join(ENGINE2, "matches"), exist_ok=True)
    book = os.path.join(ENGINE2, "matches", "openings.epd")
    with open(os.path.join(ENGINE2, "..", "engine-1", "test", "openings.epd")) as f:
        lines = [l for l in f if l.strip() and not l.startswith("#")]
    with open(book, "w") as f:
        f.writelines(lines)
    return book


def stochastic_round(x):
    # Unbiased: 4.3 plays as 4 seven times in ten and as 5 three times.
    return math.floor(x + random.random())


class Tuner:
    def __init__(self, args):
        self.args = args
        self.engine = os.path.realpath(args.engine)
        self.params = engine_params(self.engine, args.params.split(",") if args.params else None)
        self.fastchess = find_fastchess()
        self.book = opening_book()
        self.lock = threading.Lock()
        n = args.pairs
        self.A = 0.1 * n
        c_end = {p: C_END[p] * args.c_scale for p in self.params}
        self.c = {p: c_end[p] * n ** GAMMA for p in self.params}
        self.a = {p: args.r_end * c_end[p] ** 2 * (self.A + n) ** ALPHA for p in self.params}
        self.state = {"theta": {p: float(v[0]) for p, v in self.params.items()},
                      "pairs": 0, "wins": 0, "losses": 0, "draws": 0}
        if os.path.exists(args.state):
            with open(args.state) as f:
                self.state.update(json.load(f))
            print(f"resuming at pair {self.state['pairs']}", flush=True)
        self.issued = self.state["pairs"]
        self.started = time.time()
        self.pairs_at_start = self.state["pairs"]

    def save(self):
        tmp = self.args.state + ".tmp"
        with open(tmp, "w") as f:
            json.dump(self.state, f, indent=1)
        os.replace(tmp, self.args.state)

    def play_pair(self, plus, minus, workdir):
        def engine(name, values):
            return ["-engine", f"cmd={self.engine}", f"name={name}"] + [f"option.{p}={v}" for p, v in values.items()]
        cmd = ([self.fastchess] + engine("plus", plus) + engine("minus", minus) +
               ["-each", f"tc={self.args.tc}", "-openings", f"file={self.book}", "format=epd", "order=random",
                "-srand", str(random.getrandbits(31)), "-rounds", "1", "-games", "2", "-repeat",
                "-concurrency", "1", "-recover"] + ADJUDICATE)
        out = subprocess.run(cmd, cwd=workdir, capture_output=True, text=True, timeout=600).stdout
        found = RESULT.findall(out)
        if not found:
            return None
        games, wins, losses, draws = map(int, found[-1])
        return (wins, losses, draws) if games == 2 else None

    def worker(self, index):
        workdir = tempfile.mkdtemp(prefix=f"spsa-{index}-")
        while True:
            with self.lock:
                if self.issued >= self.args.pairs:
                    return
                self.issued += 1
                k = self.issued
                theta = dict(self.state["theta"])
            delta = {p: random.choice((-1, 1)) for p in self.params}
            ck = {p: self.c[p] / k ** GAMMA for p in self.params}
            ak = {p: self.a[p] / (self.A + k) ** ALPHA for p in self.params}
            clamp = lambda p, x: min(max(x, self.params[p][1]), self.params[p][2])
            plus = {p: clamp(p, stochastic_round(theta[p] + ck[p] * delta[p])) for p in self.params}
            minus = {p: clamp(p, stochastic_round(theta[p] - ck[p] * delta[p])) for p in self.params}
            result = None
            for _ in range(3):
                try:
                    result = self.play_pair(plus, minus, workdir)
                except subprocess.TimeoutExpired:
                    result = None
                if result:
                    break
            if not result:
                print(f"pair {k}: no result after 3 tries, skipped", flush=True)
                continue
            wins, losses, draws = result
            with self.lock:
                t = self.state["theta"]
                for p in self.params:
                    t[p] = clamp(p, t[p] + ak[p] * (wins - losses) * delta[p] / ck[p])
                s = self.state
                s["pairs"] += 1
                s["wins"] += wins
                s["losses"] += losses
                s["draws"] += draws
                self.save()
                if s["pairs"] % self.args.report == 0:
                    self.report()

    def report(self):
        s = self.state
        done = s["pairs"] - self.pairs_at_start
        rate = done / max(time.time() - self.started, 1) * 3600
        left = (self.args.pairs - s["pairs"]) / max(rate, 1e-9)
        values = " ".join(f"{p}={v:.1f}" for p, v in s["theta"].items())
        print(f"pair {s['pairs']}/{self.args.pairs} ({rate:.0f} pairs/h, {left:.1f} h left) "
              f"+{s['wins']} -{s['losses']} ={s['draws']}: {values}", flush=True)

    def run(self):
        threads = [threading.Thread(target=self.worker, args=(i,)) for i in range(self.args.workers)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.report()
        print("final (rounded): " + " ".join(f"{p}={round(v)}" for p, v in self.state["theta"].items()), flush=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--engine", required=True, help="tuning build of engine2 (cmake -DENGINE_TUNE=ON)")
    ap.add_argument("--pairs", type=int, default=4000, help="game pairs in the whole run (default 4000)")
    ap.add_argument("--tc", default="8+0.08", help="time control (default 8+0.08)")
    ap.add_argument("--workers", type=int, default=4, help="pairs played at once (default 4)")
    ap.add_argument("--state", default="spsa-state.json", help="state file, for resuming")
    ap.add_argument("--report", type=int, default=20, help="print progress every N pairs")
    ap.add_argument("--params", help="comma-separated parameters to tune (default: all in C_END)")
    ap.add_argument("--r-end", type=float, default=R_END,
                    help=f"step size at the end, relative to c_end^2 (default {R_END}, fishtest's; "
                         "larger moves further in a short run, with noisier final values)")
    ap.add_argument("--c-scale", type=float, default=1.0,
                    help="multiplies every C_END (the perturbation): larger compares more different settings")
    Tuner(ap.parse_args()).run()


if __name__ == "__main__":
    main()
