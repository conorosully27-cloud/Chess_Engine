#pragma once

// Endgame knowledge the network lacks: material balances that are known
// draws, or nearly so, however far ahead the network thinks one side is.
// Each gives a scale for the evaluation, out of SCALE_NORMAL.

#include "position.h"

namespace chess {

constexpr int SCALE_NORMAL = 64;

// The scale (0 to SCALE_NORMAL) for evaluation `eval` of pos, from the side
// to move's point of view. The rules look at the side the score favours.
int endgame_scale(const Position& pos, int eval);

}  // namespace chess
