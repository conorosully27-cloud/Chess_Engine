#!/usr/bin/env bash
# Playing-strength estimate: an engine against Stockfish held to a rating with
# UCI_LimitStrength / UCI_Elo (1320-3190, Stockfish's CCRL Blitz scale).
#
#   scripts/strength_match.sh <engine> [UCI_Elo] [time control] [concurrency]
#
# Defaults: 2900, 10+0.1, 3 games at a time. SPRT with bounds -10 / +10 Elo
# (5% error each way; ALPHA=0.01 for 1%, i.e. 99% confidence): "H1 accepted" means stronger than Stockfish at that
# rating, "H0 accepted" weaker. fastchess's Elo estimate then places the
# engine on the scale: rating ~ UCI_Elo + Elo difference. The calibration
# was made at longer time controls, so treat the result as approximate.
#
# Openings: Stockfish's 8moves_v3 book (34,700 balanced 8-move human lines),
# each played with both colors, not engine-1/test/openings.epd (random
# 6-ply openings, fine between our own builds but not for a rating).
# Same adjudication as match.sh.
#
# Not in the repo (outside it, once per machine):
#   Stockfish: a release binary from
#     https://github.com/official-stockfish/Stockfish/releases (e.g. sf_18,
#     stockfish-ubuntu-x86-64-avx2.tar), as $STOCKFISH or ~/tools/stockfish
#   Book: git clone --depth 1 https://github.com/official-stockfish/books
#     and unzip 8moves_v3.pgn.zip, as $BOOK or ~/tools/books/8moves_v3.pgn
#   fastchess: as for match.sh
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
engine="$(realpath "${1:?engine}")"
elo="${2:-2900}"
tc="${3:-10+0.1}"
conc="${4:-3}"
alpha="${ALPHA:-0.05}"

fastchess="${FASTCHESS:-$(command -v fastchess || echo "$HOME/tools/fastchess/fastchess")}"
stockfish="${STOCKFISH:-$HOME/tools/stockfish}"
book="${BOOK:-$HOME/tools/books/8moves_v3.pgn}"
[ -x "$fastchess" ] || { echo "fastchess not found; see engine-2/README.md" >&2; exit 1; }
[ -x "$stockfish" ] || { echo "Stockfish not found at $stockfish; see the header" >&2; exit 1; }
[ -f "$book" ] || { echo "opening book not found at $book; see the header" >&2; exit 1; }

stamp="$(date +%Y%m%d-%H%M%S)"
run_dir="$here/matches/strength-$stamp"
mkdir -p "$run_dir"
cd "$run_dir"
"$fastchess" \
  -engine cmd="$engine" name="$(basename "$engine")" option.Hash=64 \
  -engine cmd="$(realpath "$stockfish")" name="Stockfish-$elo" option.Hash=64 option.Threads=1 \
    option.UCI_LimitStrength=true option.UCI_Elo="$elo" \
  -each tc="$tc" \
  -openings file="$book" format=pgn order=random \
  -rounds 5000 -repeat -concurrency "$conc" -recover \
  -sprt elo0=-10 elo1=10 alpha="$alpha" beta="$alpha" model=logistic \
  -resign movecount=3 score=600 twosided=true -draw movenumber=34 movecount=8 score=20 \
  -pgnout file="$run_dir/games.pgn" \
  | tee "$run_dir/match.log"
