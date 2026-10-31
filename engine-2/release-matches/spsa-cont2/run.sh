#!/usr/bin/env bash
# Tighter SPSA of the continuation correction values (half the perturbation of
# ~/spsa-cont: about +/-3-4 on each), from 11.4 / 16 (resumes from state.json).
cd /home/user/Chess_Engine/engine-2 && exec python3 scripts/tune/spsa.py --engine ~/tune-build/engine2 --pairs 1000 --workers 4 \
  --params CORR_CONT_RATE,CORR_CONT_LIMIT_CP --r-end 0.005 --c-scale 1 \
  --state ~/spsa-cont2/state.json >> ~/spsa-cont2/spsa.log 2>&1 < /dev/null
