// Static evaluation, ported term for term from Engine 1.6 (engine-1/src/engine/
// evaluate.js), with its Texel-tuned weights. Scores are in centipawns from
// the side to move's point of view. With an NNUE network loaded (nnue.h),
// evaluate() uses the network instead.
#pragma once

#include "position.h"

namespace chess {

constexpr int EVAL_LIMIT = 20000;  // evaluations are clamped well short of mate scores

// Full evaluation, cached by position hash.
int evaluate(const Position& pos);

// For the quiescence search's stand-pat: when material and piece-square
// tables alone are far outside [alpha, beta], returns that cheap score.
int evaluate_lazy(const Position& pos, int alpha, int beta);

// Material and piece-square tables only, tapered.
int evaluate_material(const Position& pos);

void clear_eval_cache();

}  // namespace chess
