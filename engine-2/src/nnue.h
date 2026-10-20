// NNUE evaluation: a small neural network trained on Engine 2's self-play
// (scripts/nnue/train.py, which must match the sizes and scales here).
//   768 inputs per side (piece kind x square, seen from that side)
//   -> 128 hidden units, clipped to [0, QA]
//   -> both sides' units, side to move first -> 1 output.
// Integer arithmetic throughout. The hidden layer is recomputed from scratch
// for each evaluation for now (no incremental updates yet).
#pragma once

#include <string>

#include "position.h"

namespace chess::nnue {

// Loads a network file written by train.py's export(). Returns false (and
// leaves no network loaded) if the file is missing or the wrong shape.
bool load(const std::string& path);
void unload();
bool loaded();

// Centipawns from the side to move's point of view.
int evaluate(const Position& pos);

}  // namespace chess::nnue
