#!/bin/bash
# Keeps data run 7.4 going: rebuilds the 7.4 datagen binary if it is missing (checks bench 1425857),
# then starts any of the 4 parts that is not running, each with a fresh seed
# (YYYYMMDDHHMM(UTC) * 10 + part). Safe to run at any time; with --loop it checks every minute.
# Usage: TAG=<session tag> datagen_7.4_ensure.sh [--loop]
TAG=${TAG:?set TAG to a short name for this session}
REPO=$(cd "$(dirname "$0")/../.." && pwd)
BIN=~/engines/engine2-7.4-datagen
DIR=~/nnue-data/v74
BOOK=~/nnue-data/8moves_v3.uci
mkdir -p ~/engines "$DIR"
[ -f "$BOOK" ] || cp "$REPO/scripts/nnue/8moves_v3.uci" "$BOOK"
if [ ! -x "$BIN" ]; then
  (B=$(mktemp -d) &&
   git -C "$REPO/.." archive 72942d5 engine-2 | tar -x -C "$B" &&
   cmake -S "$B/engine-2" -B "$B/build" -DCMAKE_BUILD_TYPE=Release >/dev/null &&
   cmake --build "$B/build" -j4 --target engine2 >/dev/null &&
   "$B/build/engine2" bench 2>&1 | grep -q "Nodes searched  : 1425857" &&
   cp "$B/build/engine2" "$BIN"; rm -rf "$B")
  [ -x "$BIN" ] || { echo "$(date -u) build of 7.4 failed" >> "$DIR/run.log"; exit 1; }
  echo "$(date -u) rebuilt $BIN (bench 1425857)" >> "$DIR/run.log"
fi
check() {
  for i in 1 2 3 4; do
    F=v74_${TAG}_part-$i.bin
    pgrep -f "^$BIN datagen $F " >/dev/null && continue
    SEED=$(( $(date -u +%Y%m%d%H%M) * 10 + i ))
    echo "$(date -u) start part $i, seed $SEED" >> "$DIR/run.log"
    (cd "$DIR" && setsid nohup "$BIN" datagen "$F" 1000000000 10000 "$SEED" "$BOOK" 4 >> part-$i.log 2>&1 < /dev/null &)
  done
}
check
if [ "$1" = "--loop" ]; then
  pgrep -f "datagen_7.4_ensure.sh --loop" | grep -vq "^$$\$" && exit 0   # one watchdog only
  while true; do sleep 60; check; done
fi
