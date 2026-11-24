#!/bin/bash
# Datagen from ChessMcEngineFace 7.4 (bench 1425857): 10,000 nodes, 8moves_v3 book + 2-5 random plies, keep 4.
# 4 processes, appending to part-1..4.bin. Each start takes a fresh seed block from the UTC time
# (YYYYMMDDHHMM * 10 + part), so restarts and other sessions never replay the same games.
cd "$(dirname "$0")"
TAG=${TAG:?set TAG to a short name for this session}
BASE=$(( $(date -u +%Y%m%d%H%M) * 10 ))
echo "$(date -u) start, seeds $((BASE+1))-$((BASE+4))" >> run.log
for i in 1 2 3 4; do
  nohup ~/engines/engine2-7.4-datagen datagen v74_${TAG}_part-$i.bin 1000000000 10000 $((BASE+i)) ~/nnue-data/8moves_v3.uci 4 \
    >> part-$i.log 2>&1 &
done
