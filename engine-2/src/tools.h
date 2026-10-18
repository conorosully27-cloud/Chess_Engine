// Tools for building the opening suite; see tools.cpp.
#pragma once

#include <cstdint>

namespace chess {

int gen_openings(int count, int plies, std::uint64_t seed, int depth, int max_cp);
int score_epd(int depth);

}  // namespace chess
