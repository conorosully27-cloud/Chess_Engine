#!/usr/bin/env bash
# Restartable SPRT release match between two commits of Engine 2.
#
#   scripts/release_match.sh start <name> <new commit> <old commit> [tc] [concurrency]
#   scripts/release_match.sh resume <name>       # continue after an interruption
#   scripts/release_match.sh status <name>       # score so far
#   scripts/release_match.sh checkpoint <name>   # copy the state into the repo
#   scripts/release_match.sh extend <name> <total games>   # then resume
#
# Both commits are built from git (so they can always be rebuilt) into
# ~/matches/<name>/, and fastchess plays an SPRT between them: H0 "no better"
# (0 Elo) against H1 "at least 10 Elo better", logistic Elo, 5% errors, from
# engine-1/test/openings.epd with each opening played by both colors. Defaults:
# 8+0.08, 3 games at a time.
#
# fastchess saves the whole match (settings and results so far) to
# ~/matches/<name>/config.json every 10 games; "resume" continues from it.
# Games in progress at an interruption are replayed, and at most the last 10
# finished games are lost.
#
# If the cloud machine itself was reset, ~/matches is gone: "checkpoint"
# copies the saved state to engine-2/release-matches/<name>/ (commit it), and
# "resume" restores from there and rebuilds both engines from their commits.
#
# "extend" turns a stopped match (usually one whose SPRT has decided) into a
# fixed-length one of <total games>, counting the games already played, for
# a more precise Elo estimate: an SPRT that stops early tends to overstate
# the difference. The SPRT verdict stands; resume to play the extra games.
#
# Run start/resume detached so the match survives the session's turns:
#   setsid nohup scripts/release_match.sh resume <name> > /dev/null 2>&1 < /dev/null &
set -euo pipefail

repo="$(cd "$(dirname "$0")/../.." && pwd)"
cmd="${1:?usage: release_match.sh start|resume|status|checkpoint <name> ...}"
name="${2:?match name}"
work="$HOME/matches/$name"
saved="$repo/engine-2/release-matches/$name"
fastchess="${FASTCHESS:-$(command -v fastchess || echo "$HOME/tools/fastchess/fastchess")}"

build() {  # build <commit> <output binary>
  local commit="$1" out="$2"
  [ -x "$out" ] && return
  local src="$work/src-$commit"
  rm -rf "$src" && mkdir -p "$src"
  git -C "$repo" archive "$commit" engine-2 | tar -x -C "$src"
  local gen=()
  command -v ninja > /dev/null && gen=(-G Ninja)
  cmake -S "$src/engine-2" -B "$src/build" "${gen[@]}" -DCMAKE_BUILD_TYPE=Release > /dev/null
  cmake --build "$src/build" --target engine2 > /dev/null
  cp "$src/build/engine2" "$out"
  echo "built $commit -> $out"
}

prepare() {  # engines and openings, from meta
  # shellcheck disable=SC1091
  source "$work/meta"
  [ -x "$fastchess" ] || { echo "fastchess not found; see engine-2/README.md" >&2; exit 1; }
  build "$NEW" "$work/engine-new"
  build "$OLD" "$work/engine-old"
  grep -v '^#' "$repo/engine-1/test/openings.epd" | grep -v '^$' > "$work/openings.epd"
}

case "$cmd" in
  start)
    new="${3:?new commit}"
    old="${4:?old commit}"
    tc="${5:-8+0.08}"
    conc="${6:-3}"
    [ -e "$work/config.json" ] && { echo "$work already has a match; use resume" >&2; exit 1; }
    mkdir -p "$work"
    cat > "$work/meta" <<EOF
NEW=$(git -C "$repo" rev-parse --short "$new")
OLD=$(git -C "$repo" rev-parse --short "$old")
TC=$tc
CONC=$conc
EOF
    prepare
    cd "$work"
    exec "$fastchess" \
      -engine cmd="$work/engine-new" name="new-$NEW" \
      -engine cmd="$work/engine-old" name="old-$OLD" \
      -each tc="$TC" \
      -openings file="$work/openings.epd" format=epd order=random \
      -rounds 50000 -repeat -concurrency "$CONC" -recover \
      -sprt elo0=0 elo1=10 alpha=0.05 beta=0.05 model=logistic \
      -autosaveinterval 10 \
      -pgnout file="$work/games.pgn" \
      >> "$work/match.log" 2>&1
    ;;
  resume)
    if [ ! -e "$work/config.json" ]; then
      [ -e "$saved/config.json" ] || { echo "no saved match $name" >&2; exit 1; }
      mkdir -p "$work"
      cp "$saved/meta" "$saved/config.json" "$work/"
      echo "restored $name from the repo checkpoint"
    fi
    prepare
    cd "$work"
    echo "=== resumed $(date -u +%FT%TZ)" >> "$work/match.log"
    exec "$fastchess" -config file="$work/config.json" >> "$work/match.log" 2>&1
    ;;
  status)
    # Results as of the last save, then fastchess's latest report.
    python3 - "$work/config.json" <<'PY'
import json, sys
for pair, s in json.load(open(sys.argv[1]))["stats"].items():
    n = s["wins"] + s["losses"] + s["draws"]
    print(f"saved: {pair}: {n} games, +{s['wins']} -{s['losses']} ={s['draws']}")
PY
    grep -E "^(Elo|LLR)|accepted|Finished match" "$work/match.log" | tail -3
    ;;
  extend)
    total="${3:?total games}"
    if pgrep -f "^$fastchess -config file=$work/config.json" > /dev/null ||
       pgrep -f "^$fastchess -engine cmd=$work/" > /dev/null; then
      echo "the match is still running; extend it once it stops" >&2
      exit 1
    fi
    python3 - "$work/config.json" "$total" <<'PY'
import json, sys
path, total = sys.argv[1], int(sys.argv[2])
c = json.load(open(path))
c["sprt"]["enabled"] = False
c["rounds"] = total // c["games"]
json.dump(c, open(path, "w"), indent=4)
PY
    echo "EXTENDED_TO=$total" >> "$work/meta"
    echo "=== extended to $total games, SPRT off $(date -u +%FT%TZ)" >> "$work/match.log"
    echo "now: $0 resume $name"
    ;;
  checkpoint)
    mkdir -p "$saved"
    cp "$work/meta" "$work/config.json" "$saved/"
    "$0" status "$name" > "$saved/status.txt"
    echo "saved to $saved; commit it to keep it across machine resets"
    ;;
  *)
    echo "unknown command $cmd" >&2
    exit 1
    ;;
esac
