#pragma once

// Search parameters for tuning. In a normal build each is a constant; built
// with -DENGINE_TUNE=ON each becomes a variable the UCI exposes as a spin
// option of the same name, so scripts/tune/spsa.py can set them per game.

#include <vector>

namespace chess::tune {

struct Param {
  const char* name;
  int* value;
  int min, max;
};

#ifdef ENGINE_TUNE
inline std::vector<Param>& params() {
  static std::vector<Param> all;
  return all;
}
inline bool add(const char* name, int* value, int min, int max) {
  params().push_back({name, value, min, max});
  return true;
}
#define TUNE_PARAM(name, value, min, max) \
  int name = value;                       \
  [[maybe_unused]] const bool name##_added = ::chess::tune::add(#name, &name, min, max)
#else
#define TUNE_PARAM(name, value, min, max) constexpr int name = value
#endif

}  // namespace chess::tune
