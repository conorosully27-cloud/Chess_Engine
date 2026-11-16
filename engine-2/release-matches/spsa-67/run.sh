#!/bin/sh
# SPSA of 12 more values on 6.7; resumes from state.json.
cd /home/user/Chess_Engine/engine-2 && exec python3 scripts/tune/spsa.py --engine ~/spsa-67/engine2 --pairs 6000 --tc 8+0.08 --workers 4 \
  --state ~/spsa-67/state.json --r-end 0.02 --c-scale 2 \
  --params HIST_BONUS_MUL,HIST_BONUS_SUB,HIST_BONUS_MAX,HIST_GRAVITY,CONT_HIST_GRAVITY,CAPT_HIST_GRAVITY,CORR_LIMIT_CP,CORR_NON_PAWN_RATE,NULL_EVAL_DIV,CAPT_LMR_BASE,CAPT_LMR_HISTORY,IMPROVING_RFP >> ~/spsa-67/log.txt 2>&1
