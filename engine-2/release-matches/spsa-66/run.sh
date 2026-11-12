#!/bin/sh
# SPSA of 12 values on 6.6; resumes from state.json.
cd /home/user/Chess_Engine/engine-2 && exec python3 scripts/tune/spsa.py --engine ~/spsa-66/engine2 --pairs 6000 --tc 8+0.08 --workers 4 \
  --state ~/spsa-66/state.json --r-end 0.02 --c-scale 2 \
  --params RFP_MARGIN,FUT_MARGIN,LMP_SCALE,LMR_BASE,LMR_DIV,LMR_HISTORY,SEE_QUIET_MARGIN,SEE_CAPTURE_MARGIN,SINGULAR_MARGIN,PROBCUT_MARGIN,HIST_PRUNE_MARGIN,ASP_DELTA >> ~/spsa-66/log.txt 2>&1
