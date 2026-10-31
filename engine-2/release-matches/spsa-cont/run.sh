#!/usr/bin/env bash
# Aggressive SPSA of the continuation correction table's values (resumes from state.json).
cd /home/user/Chess_Engine/engine-2 && exec python3 scripts/tune/spsa.py --engine ~/tune-build/engine2 --pairs 1500 --workers 4 \
  --params CORR_CONT_RATE,CORR_CONT_LIMIT_CP --r-end 0.005 --c-scale 2 \
  --state ~/spsa-cont/state.json >> ~/spsa-cont/spsa.log 2>&1 < /dev/null
