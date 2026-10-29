#!/usr/bin/env bash
# Aggressive SPSA of the correction values (resumes from state.json).
cd /home/user/Chess_Engine/engine-2 && exec python3 scripts/tune/spsa.py --engine ~/tune-build/engine2 --pairs 2000 --workers 4 \
  --params LMR_HISTORY,CORR_LIMIT_CP,CORR_MAX_WEIGHT,CORR_NON_PAWN_RATE --r-end 0.005 --c-scale 2 \
  --state ~/spsa-corr2/state.json >> ~/spsa-corr2/spsa.log 2>&1 < /dev/null
