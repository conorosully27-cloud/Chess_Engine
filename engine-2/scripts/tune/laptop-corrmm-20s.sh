#!/usr/bin/env bash
# 20+0.2 SPSA of the correction values (minor/major tables + the old ones),
# started from the 8+0.08 tune's values. Run from engine-2/ on the laptop:
#   scripts/tune/laptop-corrmm-20s.sh [workers]
# workers = games at once (default 14; about one per physical core, minus two).
# Interrupted? Run it again: it resumes from ~/spsa-corrmm-20s/state.json.
# Needs fastchess on the PATH, in $FASTCHESS or at ~/tools/fastchess.
set -e
cd "$(dirname "$0")/../.."
WORKERS=${1:-14}
DIR=~/spsa-corrmm-20s
mkdir -p "$DIR"
cmake -S . -B build-tune -DENGINE_TUNE=ON -DENGINE_NET="$PWD/nets/engine-7.3.nnue" -DCMAKE_BUILD_TYPE=Release >/dev/null
cmake --build build-tune -j >/dev/null
cp build-tune/engine2 "$DIR/engine2"
"$DIR/engine2" bench | grep "Nodes searched"   # expect 1180660
if [ ! -f "$DIR/state.json" ]; then
  cat > "$DIR/state.json" <<'JSON'
{
 "theta": {
  "CORR_LIMIT_CP": 31,
  "CORR_MAX_WEIGHT": 14,
  "CORR_NON_PAWN_RATE": 22,
  "CORR_MINMAJ_RATE": 20,
  "CORR_MINMAJ_LIMIT_CP": 32
 },
 "pairs": 0,
 "wins": 0,
 "losses": 0,
 "draws": 0
}
JSON
fi
echo "SPSA running; progress in $DIR/log.txt"
exec python3 scripts/tune/spsa.py --engine "$DIR/engine2" --pairs 3000 --tc 20+0.2 --workers "$WORKERS" \
  --params CORR_MINMAJ_RATE,CORR_MINMAJ_LIMIT_CP,CORR_NON_PAWN_RATE,CORR_LIMIT_CP,CORR_MAX_WEIGHT --r-end 0.005 --c-scale 2 \
  --state "$DIR/state.json" >> "$DIR/log.txt" 2>&1 < /dev/null
