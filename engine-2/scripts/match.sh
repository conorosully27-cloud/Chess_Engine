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
book="$here/matches/openings.epd"
grep -v '^#' "$here/../engine-1/test/openings.epd" | grep -v '^$' > "$book"
stamp="$(date +%Y%m%d-%H%M%S)"

sprt=()
[ "${SPRT:-0}" = "1" ] && sprt=(-sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 model=logistic)

engine_a="$(engine_args "$a")"
engine_b="$(engine_args "$b")"
# fastchess writes its resume file (config.json) to the working directory.
cd "$here/matches"
# shellcheck disable=SC2086
"$fastchess" \
  -engine $engine_a \
  -engine $engine_b \
  -each tc="$tc" \
  -openings file="$book" format=epd order=random \
  -rounds $((games / 2)) -repeat -concurrency "$conc" -recover \
  "${sprt[@]}" \
  -pgnout file="$here/matches/$stamp.pgn" \
  | tee "$here/matches/$stamp.log"
