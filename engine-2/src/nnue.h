// NNUE evaluation: a small neural network trained on Engine 2's self-play
// (scripts/nnue/train.py, which must match the sizes and scales here).
//   768 inputs per side (piece kind x square, seen from that side)
//   -> HIDDEN hidden units (accumulator.h; 512 by default), clipped to [0, QA]
//   -> both sides' units, side to move first -> 1 output.
// Integer arithmetic throughout. The hidden layer's sums (accumulator.h) are
// updated from the position before rather than recomputed: each move only
// adds and removes a few pieces.
//
// The engine carries a default network (nets/engine-3.3.nnue, built in by
// cmake/embed_net.cmake). The engine program loads it at startup; library
// users (tests, tools) start with none, i.e. the hand-written evaluation.
#pragma once

#include <cstddef>
#include <string>

#include "position.h"

namespace chess::nnue {

// Loads a network file written by train.py's export(). Returns false (and
// leaves no network loaded) if the file is missing or the wrong shape.
bool load(const std::string& path);
// The same from bytes in memory, and the built-in network.
bool load(const unsigned char* data, std::size_t size);
void load_default();
void unload();
bool loaded();

// Centipawns from the side to move's point of view.
int evaluate(const Position& pos);

}  // namespace chess::nnue
