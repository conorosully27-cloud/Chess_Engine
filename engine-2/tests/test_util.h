#pragma once

#include <cstdio>

inline int g_failures = 0;

#define CHECK(cond)                                                      \
  do {                                                                   \
    if (!(cond)) {                                                       \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
      g_failures++;                                                      \
    }                                                                    \
  } while (0)

#define CHECK_EQ(a, b)                                                                   \
  do {                                                                                   \
    auto va = (a);                                                                       \
    auto vb = (b);                                                                       \
    if (!(va == vb)) {                                                                   \
      std::printf("FAIL %s:%d: %s == %s (%llu vs %llu)\n", __FILE__, __LINE__, #a, #b,   \
                  (unsigned long long)va, (unsigned long long)vb);                       \
      g_failures++;                                                                      \
    }                                                                                    \
  } while (0)
