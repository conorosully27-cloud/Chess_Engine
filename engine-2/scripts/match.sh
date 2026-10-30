#!/usr/bin/env bash
# Engine-vs-engine match with fastchess (https://github.com/Disservin/fastchess).
#
#   scripts/match.sh <engine A> <engine B> [games] [time control] [concurrency]
#
# Engines are executables, or "engine1" for Engine 1.6 (node ../engine-1/uci.js).
# Each opening from engine-1/test/openings.epd is played twice with colors
# swapped. Defaults: 200 games, 8+0.08, 3 games at a time. Prints fastchess's
# Elo estimate; the PGN and log go to matches/ (git-ignored).
#
# For an SPRT instead of a fixed number of games, pass SPRT=1:
#   SPRT=1 scripts/match.sh new old 20000 8+0.08
# which stops as soon as the result is clear: H0 "no better" (0 Elo) against
# H1 "at least 10 Elo better", logistic Elo, 5% error each way, the same
# bounds Engine 1's tests used. Accept a change only on "H1 accepted".
#
# Games that are clearly decided are adjudicated, as most testing setups do
# (fastchess's own example values): a loss once both engines agree one side
# is 6+ pawns ahead for 3 moves in a row, a draw once both score within 0.2
# pawns of level for 8 moves in a row after move 34. About 20-30% less time
# per game. ADJUDICATE=0 plays every game out (as before Oct 2026).
#
# BOOK=<file.epd> starts the games from those positions instead, each
# played twice with colors swapped: for example endgames only, from
# scripts/endgame_book.py.
#
# fastchess is looked up as $FASTCHESS, then on the PATH, then ~/tools/fastchess.
set -euo pipefail

here="$(cd "$(dirname "$0")/.." && pwd)"
a="${1:?engine A}"
b="${2:?engine B}"
games="${3:-200}"
tc="${4:-8+0.08}"
conc="${5:-3}"

fastchess="${FASTCHESS:-$(command -v fastchess || echo "$HOME/tools/fastchess/fastchess")}"
[ -x "$fastchess" ] || { echo "fastchess not found; see engine-2/README.md" >&2; exit 1; }

engine_args() {
  if [ "$1" = "engine1" ]; then
    echo "cmd=node args=$here/../engine-1/uci.js name=Engine1.6"
  else
    echo "cmd=$(realpath "$1") name=$(basename "$1")"
  fi
}

mkdir -p "$here/matches"
if [ -n "${BOOK:-}" ]; then
  book="$(realpath "$BOOK")"
else
  book="$here/matches/openings.epd"
  grep -v '^#' "$here/../engine-1/test/openings.epd" | grep -v '^$' > "$book"
fi
stamp="$(date +%Y%m%d-%H%M%S)"

sprt=()
[ "${SPRT:-0}" = "1" ] && sprt=(-sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 model=logistic)

adjudicate=()
[ "${ADJUDICATE:-1}" = "1" ] && adjudicate=(-resign movecount=3 score=600 twosided=true -draw movenumber=34 movecount=8 score=20)

engine_a="$(engine_args "$a")"
engine_b="$(engine_args "$b")"
# fastchess writes its resume file (config.json) to the working directory:
# one folder per run, so matches running side by side don't overwrite each
# other's. To resume a run: cd into its folder, fastchess -config file=config.json
run_dir="$here/matches/run-$stamp"
mkdir -p "$run_dir"
cd "$run_dir"
# shellcheck disable=SC2086
"$fastchess" \
  -engine $engine_a \
  -engine $engine_b \
  -each tc="$tc" \
  -openings file="$book" format=epd order=random \
  -rounds $((games / 2)) -repeat -concurrency "$conc" -recover \
  "${sprt[@]}" \
  "${adjudicate[@]}" \
  -pgnout file="$here/matches/$stamp.pgn" \
  | tee "$here/matches/$stamp.log"
