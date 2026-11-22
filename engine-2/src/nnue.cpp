#include "nnue.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#if defined(__AVX2__) || defined(__AVX512BW__)
#include <immintrin.h>
#elif defined(__wasm_simd128__)
#include <wasm_simd128.h>
#endif

namespace chess::nnue {

namespace {

constexpr int INPUTS = 768;
constexpr int QA = 255;
constexpr int QB = 64;
// Output weights of "fine" SCReLU networks (activation 2 in the file):
// x1024 instead of x64, so the output layer's rounding error is 16x smaller
// (about 0.7 cp on average instead of 6 on 7.2's network).
constexpr int QB_FINE = 1024;
constexpr int SCALE = 400;

constexpr int MAX_BUCKETS = 8;
constexpr int MAX_KING_BUCKETS = 16;

// Extra layers (network file version 4, train.py --layers 16,32): both
// sides' squared units -> L1 clipped units -> L2 clipped units -> output,
// one set per output bucket. L1's inputs are (c * c) >> 4 for each unit c
// in [0, QA] (0-4064), its weights x L1_Q, so its sums are in units of
// L1_UNIT; the trainer's limit on those weights (253 / 256) keeps 2 x 1024
// products in 32 bits. L2 and the output are in float, added up in a fixed
// order (and built with -ffp-contract=off), so every build gives the same
// evaluations.
constexpr int L1 = 16;
constexpr int L2 = 32;
constexpr int L1_SHIFT = 4;
constexpr int L1_Q = 256;
constexpr float L1_UNIT = float(QA * QA) / (1 << L1_SHIFT) * L1_Q;  // 1,040,400

struct Network {
  // First layer: one block of INPUTS rows per king bucket (one block without
  // king buckets), each row HIDDEN weights.
  std::vector<std::int16_t> ft_weight;
  alignas(64) std::int16_t ft_bias[HIDDEN];
  // One output layer per bucket (only the first `buckets` are used); in
  // each, the side to move's units first.
  alignas(64) std::int16_t out_weight[MAX_BUCKETS][2 * HIDDEN];
  std::int32_t out_bias[MAX_BUCKETS];
  int buckets = 1;      // output layers, chosen by piece count
  bool screlu = false;  // the clipped units are squared before the output layer
  bool fine = false;    // ...and the output weights are x QB_FINE (output_squared_fine)
  // King buckets (network file version 3): each side's inputs depend on its
  // own king's square, seen from that side: king_map gives the bucket, and
  // with `mirror` a king on files e-h flips every square left-right (so the
  // map only needs the a-d half). One bucket and no mirror = plain inputs.
  int king_buckets = 1;
  bool mirror = false;
  std::uint8_t king_map[64] = {};
  // Extra layers (version 4): `layers` replaces out_weight / out_bias.
  // l1_weight is [bucket][pair of inputs][L1][2], so one pair's weights for
  // all 16 outputs are 64 consecutive bytes (the file has [bucket][L1][input]).
  bool layers = false;
  std::vector<std::int16_t> l1_weight;
  std::int32_t l1_bias[MAX_BUCKETS][L1] = {};
  float l2_weight[MAX_BUCKETS][L1][L2] = {};  // [input][output] (the file has [output][input])
  float l2_bias[MAX_BUCKETS][L2] = {};
  float l3_weight[MAX_BUCKETS][L2] = {};
  float l3_bias[MAX_BUCKETS] = {};
};

Network net;
bool have_net = false;
// Changes with every network loaded, so accumulators computed with an
// earlier one read as not computed. Never 0 (Accumulator's "none").
std::uint32_t net_id = 0;

// Reads `size` bytes into dst from data + *pos.
bool take(const unsigned char* data, std::size_t size, std::size_t& pos, void* dst, std::size_t n) {
  if (pos + n > size) return false;
  std::memcpy(dst, data + pos, n);
  pos += n;
  return true;
}

// The feature for a piece on a square, seen from White's or Black's side:
// from Black's side the colors swap and the board flips.
inline int feature(Piece pc, Square s, Color side) {
  return side == WHITE ? pc * 64 + s : ((pc + 6) % 12) * 64 + (s ^ 56);
}

// The side's king state: its bucket times 2, plus 1 if the board is mirrored.
// The inputs (and so the update) depend on it; while it stays the same the
// side's accumulator is updated move by move.
inline int king_state(Color side, Square king) {
  const int rel = side == WHITE ? king : king ^ 56;
  return net.king_map[rel] * 2 + (net.mirror && file_of(Square(rel)) >= 4);
}

inline const std::int16_t* weights(Piece pc, Square s, Color side, int state) {
  const int f = feature(pc, s, side) ^ (state & 1 ? 7 : 0);  // mirrored: the file flips (bits 0-2 of the square)
  return &net.ft_weight[std::size_t((state >> 1) * INPUTS + f) * HIDDEN];
}

// The update loops: out = in + the added rows - the removed rows. Sums wrap
// like the 16-bit lanes they vectorize to; the trainer's weight limit keeps
// every finished accumulator in range, so the result is exactly a
// from-scratch computation's.
using Row = const std::int16_t* __restrict;

#if defined(__AVX512BW__)
// AVX-512 (ENGINE_AVX512 builds): 64 units a step. Same results.
template <int Adds, int Subs>
inline void add_sub(std::int16_t* __restrict out, Row in, const std::int16_t* const (&rows)[Adds + Subs]) {
  for (int j = 0; j < HIDDEN; j += 64) {
    __m512i v0 = _mm512_loadu_si512(in + j);
    __m512i v1 = _mm512_loadu_si512(in + j + 32);
    for (int k = 0; k < Adds + Subs; k++) {
      const __m512i w0 = _mm512_loadu_si512(rows[k] + j);
      const __m512i w1 = _mm512_loadu_si512(rows[k] + j + 32);
      v0 = k < Adds ? _mm512_add_epi16(v0, w0) : _mm512_sub_epi16(v0, w0);
      v1 = k < Adds ? _mm512_add_epi16(v1, w1) : _mm512_sub_epi16(v1, w1);
    }
    _mm512_storeu_si512(out + j, v0);
    _mm512_storeu_si512(out + j + 32, v1);
  }
}
#elif defined(__AVX2__)
// Written out with AVX2, 32 units a step: the compiler's own vectorization
// of the plain loops below steps 16 at a time, and at 512 units the loop
// overhead was a sizeable share of the network's cost. Same results.
template <int Adds, int Subs>
inline void add_sub(std::int16_t* __restrict out, Row in, const std::int16_t* const (&rows)[Adds + Subs]) {
  for (int j = 0; j < HIDDEN; j += 32) {
    __m256i v0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + j));
    __m256i v1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(in + j + 16));
    for (int k = 0; k < Adds + Subs; k++) {
      const __m256i w0 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[k] + j));
      const __m256i w1 = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(rows[k] + j + 16));
      v0 = k < Adds ? _mm256_add_epi16(v0, w0) : _mm256_sub_epi16(v0, w0);
      v1 = k < Adds ? _mm256_add_epi16(v1, w1) : _mm256_sub_epi16(v1, w1);
    }
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + j), v0);
    _mm256_storeu_si256(reinterpret_cast<__m256i*>(out + j + 16), v1);
  }
}
#else
template <int Adds, int Subs>
inline void add_sub(std::int16_t* __restrict out, Row in, const std::int16_t* const (&rows)[Adds + Subs]) {
  for (int j = 0; j < HIDDEN; j++) {
    int v = in[j];
    for (int k = 0; k < Adds + Subs; k++) v += k < Adds ? rows[k][j] : -rows[k][j];
    out[j] = std::int16_t(v);
  }
}
#endif

inline void add1_sub1(std::int16_t* __restrict out, Row in, Row a, Row r) { add_sub<1, 1>(out, in, {a, r}); }
inline void add1_sub2(std::int16_t* __restrict out, Row in, Row a, Row r1, Row r2) { add_sub<1, 2>(out, in, {a, r1, r2}); }
inline void add2_sub1(std::int16_t* __restrict out, Row in, Row a1, Row a2, Row r) { add_sub<2, 1>(out, in, {a1, a2, r}); }
inline void add2_sub2(std::int16_t* __restrict out, Row in, Row a1, Row a2, Row r1, Row r2) {
  add_sub<2, 2>(out, in, {a1, a2, r1, r2});
}

// The output layer: both sides' hidden units, clipped to [0, QA], times
// their output weights (side to move first), summed.
#if defined(__AVX2__)
inline std::int32_t output(Row us, Row them, Row w) {
  const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
  __m256i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 16) {
    const __m256i u = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(us + j)), zero), qa);
    const __m256i t = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(them + j)), zero), qa);
    s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(u, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + j))));
    s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(t, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + HIDDEN + j))));
  }
  __m256i s = _mm256_add_epi32(s0, s1);
  __m128i h = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0x4E));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0xB1));
  return _mm_cvtsi128_si32(h);
}
#else
// A plain dot product of 16-bit values, which compilers vectorize
// (pmaddwd on x86, i32x4.dot_i16x8_s in WebAssembly).
inline std::int32_t output(Row us, Row them, Row w) {
  std::int32_t sum = 0;
  for (int j = 0; j < HIDDEN; j++) {
    sum += std::min<std::int16_t>(std::max<std::int16_t>(us[j], 0), QA) * w[j];
    sum += std::min<std::int16_t>(std::max<std::int16_t>(them[j], 0), QA) * w[HIDDEN + j];
  }
  return sum;
}
#endif

// The same with the clipped units squared (SCReLU): unit x unit x weight.
// unit x weight is formed first and fits 16 bits (the trainer keeps these
// output weights within +/-127), so the 16-bit multiply-add still applies.
// The sum is in units of QA x QA x QB.
#if defined(__AVX512BW__)
inline std::int32_t output_squared(Row us, Row them, Row w) {
  const __m512i zero = _mm512_setzero_si512(), qa = _mm512_set1_epi16(QA);
  __m512i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 32) {
    const __m512i u = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512(us + j), zero), qa);
    const __m512i t = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512(them + j), zero), qa);
    s0 = _mm512_add_epi32(s0, _mm512_madd_epi16(_mm512_mullo_epi16(u, _mm512_loadu_si512(w + j)), u));
    s1 = _mm512_add_epi32(s1, _mm512_madd_epi16(_mm512_mullo_epi16(t, _mm512_loadu_si512(w + HIDDEN + j)), t));
  }
  return _mm512_reduce_add_epi32(_mm512_add_epi32(s0, s1));
}
#elif defined(__AVX2__)
inline std::int32_t output_squared(Row us, Row them, Row w) {
  const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
  __m256i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 16) {
    const __m256i u = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(us + j)), zero), qa);
    const __m256i t = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(them + j)), zero), qa);
    const __m256i uw = _mm256_mullo_epi16(u, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + j)));
    const __m256i tw = _mm256_mullo_epi16(t, _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + HIDDEN + j)));
    s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(uw, u));
    s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(tw, t));
  }
  __m256i s = _mm256_add_epi32(s0, s1);
  __m128i h = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0x4E));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0xB1));
  return _mm_cvtsi128_si32(h);
}
#else
inline std::int32_t output_squared(Row us, Row them, Row w) {
  std::int32_t sum = 0;
  for (int j = 0; j < HIDDEN; j++) {
    const std::int32_t u = std::min<std::int16_t>(std::max<std::int16_t>(us[j], 0), QA);
    const std::int32_t t = std::min<std::int16_t>(std::max<std::int16_t>(them[j], 0), QA);
    sum += std::int16_t(u * w[j]) * u;
    sum += std::int16_t(t * w[HIDDEN + j]) * t;
  }
  return sum;
}
#endif

// SCReLU with fine output weights (x QB_FINE): unit x weight no longer fits
// 16 bits, so it is formed with a rounding multiply-high instead:
//   v = round((unit << 7) x weight / 2^15) = round(unit x weight / 256),
// at most about |weight| (unit <= 255), and then v x unit is summed by the
// 16-bit multiply-add as before. The sum is in units of QA x QA x QB_FINE /
// 256. Same instruction count as output_squared but one shift.
constexpr int FINE_SHIFT = 7;
#if defined(__AVX512BW__)
inline std::int32_t output_squared_fine(Row us, Row them, Row w) {
  const __m512i zero = _mm512_setzero_si512(), qa = _mm512_set1_epi16(QA);
  __m512i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 32) {
    const __m512i u = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512(us + j), zero), qa);
    const __m512i t = _mm512_min_epi16(_mm512_max_epi16(_mm512_loadu_si512(them + j), zero), qa);
    const __m512i uw = _mm512_mulhrs_epi16(_mm512_slli_epi16(u, FINE_SHIFT), _mm512_loadu_si512(w + j));
    const __m512i tw = _mm512_mulhrs_epi16(_mm512_slli_epi16(t, FINE_SHIFT), _mm512_loadu_si512(w + HIDDEN + j));
    s0 = _mm512_add_epi32(s0, _mm512_madd_epi16(uw, u));
    s1 = _mm512_add_epi32(s1, _mm512_madd_epi16(tw, t));
  }
  return _mm512_reduce_add_epi32(_mm512_add_epi32(s0, s1));
}
#elif defined(__AVX2__)
inline std::int32_t output_squared_fine(Row us, Row them, Row w) {
  const __m256i zero = _mm256_setzero_si256(), qa = _mm256_set1_epi16(QA);
  __m256i s0 = zero, s1 = zero;
  for (int j = 0; j < HIDDEN; j += 16) {
    const __m256i u = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(us + j)), zero), qa);
    const __m256i t = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(them + j)), zero), qa);
    const __m256i uw = _mm256_mulhrs_epi16(_mm256_slli_epi16(u, FINE_SHIFT), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + j)));
    const __m256i tw =
        _mm256_mulhrs_epi16(_mm256_slli_epi16(t, FINE_SHIFT), _mm256_loadu_si256(reinterpret_cast<const __m256i*>(w + HIDDEN + j)));
    s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(uw, u));
    s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(tw, t));
  }
  __m256i s = _mm256_add_epi32(s0, s1);
  __m128i h = _mm_add_epi32(_mm256_castsi256_si128(s), _mm256_extracti128_si256(s, 1));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0x4E));
  h = _mm_add_epi32(h, _mm_shuffle_epi32(h, 0xB1));
  return _mm_cvtsi128_si32(h);
}
#elif defined(__wasm_simd128__)
// WebAssembly SIMD (the app): q15mulr_sat is the rounding multiply-high
// (saturating only for -32768 x -32768, which a shifted unit, at least 0,
// never is), dot_i16x8 the 16-bit multiply-add. The compiler does not find
// these from the plain loop below, which ran 10% slower overall.
inline std::int32_t output_squared_fine(Row us, Row them, Row w) {
  const v128_t zero = wasm_i16x8_splat(0), qa = wasm_i16x8_splat(QA);
  v128_t s0 = wasm_i32x4_splat(0), s1 = s0;
  for (int j = 0; j < HIDDEN; j += 8) {
    const v128_t u = wasm_i16x8_min(wasm_i16x8_max(wasm_v128_load(us + j), zero), qa);
    const v128_t t = wasm_i16x8_min(wasm_i16x8_max(wasm_v128_load(them + j), zero), qa);
    const v128_t uw = wasm_i16x8_q15mulr_sat(wasm_i16x8_shl(u, FINE_SHIFT), wasm_v128_load(w + j));
    const v128_t tw = wasm_i16x8_q15mulr_sat(wasm_i16x8_shl(t, FINE_SHIFT), wasm_v128_load(w + HIDDEN + j));
    s0 = wasm_i32x4_add(s0, wasm_i32x4_dot_i16x8(uw, u));
    s1 = wasm_i32x4_add(s1, wasm_i32x4_dot_i16x8(tw, t));
  }
  const v128_t s = wasm_i32x4_add(s0, s1);
  return wasm_i32x4_extract_lane(s, 0) + wasm_i32x4_extract_lane(s, 1) + wasm_i32x4_extract_lane(s, 2) +
         wasm_i32x4_extract_lane(s, 3);
}
#else
// The rounding multiply-high (x86 pmulhrsw, WebAssembly i16x8.q15mulr_sat_s)
// written out: (a x b + 2^14) >> 15.
inline std::int32_t mulhrs(std::int32_t a, std::int32_t b) { return (a * b + (1 << 14)) >> 15; }
inline std::int32_t output_squared_fine(Row us, Row them, Row w) {
  std::int32_t sum = 0;
  for (int j = 0; j < HIDDEN; j++) {
    const std::int32_t u = std::min<std::int16_t>(std::max<std::int16_t>(us[j], 0), QA);
    const std::int32_t t = std::min<std::int16_t>(std::max<std::int16_t>(them[j], 0), QA);
    sum += mulhrs(u << FINE_SHIFT, w[j]) * u;
    sum += mulhrs(t << FINE_SHIFT, w[HIDDEN + j]) * t;
  }
  return sum;
}
#endif

// Extra layers. activate(): out = (clamp(v, 0, QA)^2) >> L1_SHIFT for one
// side's HIDDEN units. l1_sums(): sums[o] = sum over inputs i of act[i] x
// weight[o][i], skipping the pairs of inputs that are both 0 (most are:
// 7.2's units are 0 89% of the time). Integer sums, so every version gives
// the same result.
#if defined(__AVX2__)
inline void activate(const std::int16_t* __restrict v, std::int16_t* __restrict out) {
  const __m256i zero = _mm256_setzero_si256(), top = _mm256_set1_epi16(QA);
  for (int j = 0; j < HIDDEN; j += 16) {
    const __m256i c = _mm256_min_epi16(_mm256_max_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(v + j)), zero), top);
    // c * c < 65536 fits the 16 bits as an unsigned number; shift it as one.
    _mm256_store_si256(reinterpret_cast<__m256i*>(out + j), _mm256_srli_epi16(_mm256_mullo_epi16(c, c), L1_SHIFT));
  }
}

// For each 8-bit mask, the positions of its set bits (the rest 0): turns a
// comparison's mask into a list of non-zero pairs without a branch per bit.
struct NnzTable {
  alignas(16) std::uint16_t idx[256][8];
  constexpr NnzTable() : idx() {
    for (int m = 0; m < 256; m++) {
      int n = 0;
      for (int b = 0; b < 8; b++)
        if (m & (1 << b)) idx[m][n++] = std::uint16_t(b);
    }
  }
};
constexpr NnzTable nnz_table;

inline void l1_sums(const std::int16_t* __restrict act, const std::int16_t* __restrict w, std::int32_t* sums) {
  // First the list of pairs of inputs that are not both 0 (as 32-bit
  // numbers, pairs are never negative), 8 pairs a step...
  alignas(32) std::uint16_t list[HIDDEN + 8];
  int n = 0;
  const __m256i zero = _mm256_setzero_si256();
  for (int p0 = 0; p0 < HIDDEN; p0 += 8) {
    const __m256i v = _mm256_load_si256(reinterpret_cast<const __m256i*>(act + 2 * p0));
    const unsigned mask = unsigned(_mm256_movemask_ps(_mm256_castsi256_ps(_mm256_cmpgt_epi32(v, zero))));
    const __m128i idx = _mm_load_si128(reinterpret_cast<const __m128i*>(nnz_table.idx[mask]));
    _mm_storeu_si128(reinterpret_cast<__m128i*>(list + n), _mm_add_epi16(idx, _mm_set1_epi16(std::int16_t(p0))));
    n += __builtin_popcount(mask);
  }
  // ...then each listed pair's weights for all 16 outputs, 2 registers.
  __m256i s0 = _mm256_setzero_si256(), s1 = _mm256_setzero_si256();
  for (int k = 0; k < n; k++) {
    const int p = list[k];
    std::int32_t pair;
    std::memcpy(&pair, act + 2 * p, 4);
    const __m256i b = _mm256_set1_epi32(pair);
    const __m256i* row = reinterpret_cast<const __m256i*>(w + std::size_t(p) * L1 * 2);
    s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(b, _mm256_loadu_si256(row)));
    s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(b, _mm256_loadu_si256(row + 1)));
  }
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(sums), s0);
  _mm256_storeu_si256(reinterpret_cast<__m256i*>(sums + 8), s1);
}
#elif defined(__wasm_simd128__)
inline void activate(const std::int16_t* __restrict v, std::int16_t* __restrict out) {
  const v128_t zero = wasm_i16x8_splat(0), top = wasm_i16x8_splat(QA);
  for (int j = 0; j < HIDDEN; j += 8) {
    const v128_t c = wasm_i16x8_min(wasm_i16x8_max(wasm_v128_load(v + j), zero), top);
    wasm_v128_store(out + j, wasm_u16x8_shr(wasm_i16x8_mul(c, c), L1_SHIFT));
  }
}

inline void l1_sums(const std::int16_t* __restrict act, const std::int16_t* __restrict w, std::int32_t* sums) {
  v128_t s[4] = {wasm_i32x4_splat(0), wasm_i32x4_splat(0), wasm_i32x4_splat(0), wasm_i32x4_splat(0)};
  for (int p = 0; p < HIDDEN; p++) {
    std::int32_t pair;
    std::memcpy(&pair, act + 2 * p, 4);
    if (!pair) continue;
    const v128_t b = wasm_i32x4_splat(pair);
    const std::int16_t* row = w + std::size_t(p) * L1 * 2;
    for (int k = 0; k < 4; k++) s[k] = wasm_i32x4_add(s[k], wasm_i32x4_dot_i16x8(b, wasm_v128_load(row + 8 * k)));
  }
  for (int k = 0; k < 4; k++) wasm_v128_store(sums + 4 * k, s[k]);
}
#else
inline void activate(const std::int16_t* __restrict v, std::int16_t* __restrict out) {
  for (int j = 0; j < HIDDEN; j++) {
    const int c = std::clamp(int(v[j]), 0, QA);
    out[j] = std::int16_t((c * c) >> L1_SHIFT);
  }
}

inline void l1_sums(const std::int16_t* __restrict act, const std::int16_t* __restrict w, std::int32_t* sums) {
  for (int o = 0; o < L1; o++) sums[o] = 0;
  for (int i = 0; i < 2 * HIDDEN; i++) {
    if (!act[i]) continue;
    const std::int16_t* row = w + std::size_t(i / 2) * L1 * 2 + (i & 1);
    for (int o = 0; o < L1; o++) sums[o] += act[i] * row[2 * o];
  }
}
#endif

// The extra layers' evaluation, in units of SCALE centipawns.
float evaluate_layers(const std::int16_t* us, const std::int16_t* them, int bucket) {
  alignas(64) std::int16_t act[2 * HIDDEN];
  activate(us, act);
  activate(them, act + HIDDEN);
  std::int32_t sums[L1];
  l1_sums(act, &net.l1_weight[std::size_t(bucket) * HIDDEN * L1 * 2], sums);
  float h1[L1];
  for (int o = 0; o < L1; o++)
    h1[o] = std::clamp(float(sums[o] + net.l1_bias[bucket][o]) * (1.0f / L1_UNIT), 0.0f, 1.0f);
  // Input by input over all outputs: each output's terms are added in the
  // same order whether or not the compiler vectorizes the inner loop.
  float h2[L2];
  for (int o = 0; o < L2; o++) h2[o] = net.l2_bias[bucket][o];
  for (int i = 0; i < L1; i++)
    for (int o = 0; o < L2; o++) h2[o] += h1[i] * net.l2_weight[bucket][i][o];
  float out = net.l3_bias[bucket];
  for (int o = 0; o < L2; o++) out += std::clamp(h2[o], 0.0f, 1.0f) * net.l3_weight[bucket][o];
  return out;
}

// next = prev changed by next.dirty, from one side, whose king state
// (king_state) is the same in both.
void apply(const Accumulator& prev, Accumulator& next, Color side, int st) {
  const DirtyPieces& d = next.dirty;
  const std::int16_t* in = prev.values[side];
  std::int16_t* out = next.values[side];
  if (d.removed == 1) {  // a quiet move or a promotion
    add1_sub1(out, in, weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.remove_piece[0], d.remove_sq[0], side, st));
  } else if (d.added == 1) {  // a capture
    add1_sub2(out, in, weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.remove_piece[0], d.remove_sq[0], side, st),
              weights(d.remove_piece[1], d.remove_sq[1], side, st));
  } else if (d.added == 2) {  // castling
    add2_sub2(out, in, weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.add_piece[1], d.add_sq[1], side, st),
              weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.remove_piece[1], d.remove_sq[1], side, st));
  } else {  // a null move
    std::memcpy(out, in, sizeof(prev.values[side]));
  }
  next.net[side] = net_id;
}

// prev = next with next.dirty undone, from one side: the pieces it added
// come off and the ones it removed go back.
void unapply(const Accumulator& next, Accumulator& prev, Color side, int st) {
  const DirtyPieces& d = next.dirty;
  const std::int16_t* in = next.values[side];
  std::int16_t* out = prev.values[side];
  if (d.removed == 1) {
    add1_sub1(out, in, weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.add_piece[0], d.add_sq[0], side, st));
  } else if (d.added == 1) {
    add2_sub1(out, in, weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.remove_piece[1], d.remove_sq[1], side, st),
              weights(d.add_piece[0], d.add_sq[0], side, st));
  } else if (d.added == 2) {
    add2_sub2(out, in, weights(d.remove_piece[0], d.remove_sq[0], side, st), weights(d.remove_piece[1], d.remove_sq[1], side, st),
              weights(d.add_piece[0], d.add_sq[0], side, st), weights(d.add_piece[1], d.add_sq[1], side, st));
  } else {
    std::memcpy(out, in, sizeof(next.values[side]));
  }
  prev.net[side] = net_id;
}

// sums += the `na` rows in adds - the `ns` rows in subs, written to both
// `sums` and `out`; a block of units at a time, kept in registers while
// every row is applied. Wraps like add_sub (same results in any order).
#if defined(__AVX2__) && !defined(__AVX512BW__)
inline void add_sub_many(std::int16_t* sums, std::int16_t* out, const std::int16_t* const* adds, int na,
                         const std::int16_t* const* subs, int ns) {
  constexpr int REGS = 8, BLOCK = REGS * 16;
  static_assert(HIDDEN % BLOCK == 0, "refresh blocks of 128 units");
  for (int j = 0; j < HIDDEN; j += BLOCK) {
    __m256i r[REGS];
    for (int k = 0; k < REGS; k++) r[k] = _mm256_load_si256(reinterpret_cast<const __m256i*>(sums + j + 16 * k));
    for (int a = 0; a < na; a++)
      for (int k = 0; k < REGS; k++)
        r[k] = _mm256_add_epi16(r[k], _mm256_loadu_si256(reinterpret_cast<const __m256i*>(adds[a] + j + 16 * k)));
    for (int a = 0; a < ns; a++)
      for (int k = 0; k < REGS; k++)
        r[k] = _mm256_sub_epi16(r[k], _mm256_loadu_si256(reinterpret_cast<const __m256i*>(subs[a] + j + 16 * k)));
    for (int k = 0; k < REGS; k++) {
      _mm256_store_si256(reinterpret_cast<__m256i*>(sums + j + 16 * k), r[k]);
      _mm256_store_si256(reinterpret_cast<__m256i*>(out + j + 16 * k), r[k]);
    }
  }
}
#else
// Plain loops in blocks (the compiler vectorizes them; AVX-512 and
// WebAssembly builds).
inline void add_sub_many(std::int16_t* sums, std::int16_t* out, const std::int16_t* const* adds, int na,
                         const std::int16_t* const* subs, int ns) {
  constexpr int BLOCK = 128;
  static_assert(HIDDEN % BLOCK == 0, "refresh blocks of 128 units");
  for (int j = 0; j < HIDDEN; j += BLOCK) {
    std::int16_t r[BLOCK];
    std::memcpy(r, sums + j, sizeof(r));
    for (int a = 0; a < na; a++)
      for (int k = 0; k < BLOCK; k++) r[k] = std::int16_t(r[k] + adds[a][j + k]);
    for (int a = 0; a < ns; a++)
      for (int k = 0; k < BLOCK; k++) r[k] = std::int16_t(r[k] - subs[a][j + k]);
    std::memcpy(sums + j, r, sizeof(r));
    std::memcpy(out + j, r, sizeof(r));
  }
}
#endif

// Accumulator cache ("Finny table"), for king buckets: per side and king
// state, the sums last computed there and the pieces they were for. A side
// whose king changes zone (or crosses between files d and e) needs its
// sums for other inputs; starting from the cached ones only the pieces that
// changed since are added or removed, instead of all of them. One per
// thread (each search thread has its own).
struct CacheEntry {
  alignas(64) std::int16_t values[HIDDEN];
  Bitboard pieces[PIECE_NB];
  std::uint32_t net = 0;  // the network the entry belongs to; 0 = empty
};
thread_local std::vector<CacheEntry> cache;

void refresh(const Position& pos, Accumulator& acc, Color side, int st) {
  std::int16_t* v = acc.values[side];
  if (net.king_buckets == 1 && !net.mirror) {  // plain inputs: from scratch, as before king buckets
    std::memcpy(v, net.ft_bias, sizeof(net.ft_bias));
    for (Bitboard b = pos.pieces(); b;) {
      const Square s = pop_lsb(b);
      const std::int16_t* w = weights(pos.piece_on(s), s, side, st);
      for (int j = 0; j < HIDDEN; j++) v[j] += w[j];
    }
    acc.net[side] = net_id;
    return;
  }
  const std::size_t states = std::size_t(net.king_buckets) * 2;
  if (cache.size() < COLOR_NB * states) cache.resize(COLOR_NB * MAX_KING_BUCKETS * 2);
  CacheEntry& e = cache[side * MAX_KING_BUCKETS * 2 + st];
  if (e.net != net_id) {
    std::memcpy(e.values, net.ft_bias, sizeof(net.ft_bias));
    std::fill(std::begin(e.pieces), std::end(e.pieces), Bitboard(0));
    e.net = net_id;
  }
  // The rows to add and remove, then one pass over the sums for all of them,
  // writing the entry and the accumulator together (a pass per piece, and a
  // copy after, cost about 4% of the bench with a king-bucket network).
  const std::int16_t* adds[32];
  const std::int16_t* subs[32];
  int na = 0, ns = 0;
  for (int p = 0; p < PIECE_NB; p++) {
    const Piece pc = Piece(p);
    const Bitboard now = pos.pieces(color_of(pc), type_of(pc));
    for (Bitboard add = now & ~e.pieces[p]; add;) adds[na++] = weights(pc, pop_lsb(add), side, st);
    for (Bitboard sub = e.pieces[p] & ~now; sub;) subs[ns++] = weights(pc, pop_lsb(sub), side, st);
    e.pieces[p] = now;
  }
  add_sub_many(e.values, v, adds, na, subs, ns);
  acc.net[side] = net_id;
}

// Where `side`'s king stood before the move recorded in d, given where it
// stands after it.
inline Square king_before(const DirtyPieces& d, Color side, Square after) {
  const Piece king = make_piece(side, KING);
  for (int a = 0; a < d.added; a++)
    if (d.add_piece[a] == king && d.add_sq[a] == after)
      for (int r = 0; r < d.removed; r++)
        if (d.remove_piece[r] == king) return d.remove_sq[r];
  return after;
}

// Brings one side of the current position's accumulator up to date: forward
// from the nearest position before it whose side is computed, or from
// scratch when the updates on the way would add and remove more pieces than
// the board holds, or when that side's king state changes on the way (its
// inputs are then different ones). After a refresh the positions before it
// that were passed over are filled in backward, one update each, while the
// king state allows: otherwise every sibling of this position (and their
// children) would find no computed parent and refresh too.
void update(const Position& pos, Color side) {
  Accumulator* stack = pos.accumulators();
  const int top = pos.game_ply();
  const int limit = popcount(pos.pieces());
  const Square top_king = pos.king_square(side);
  const int st = king_state(side, top_king);
  Square king = top_king;
  int i = top, cost = 0;
  while (stack[i].net[side] != net_id) {
    cost += stack[i].dirty.added + stack[i].dirty.removed;
    const Square before = i > 0 ? king_before(stack[i].dirty, side, king) : king;
    if (i == 0 || cost > limit || king_state(side, before) != st) {
      refresh(pos, stack[top], side, st);
      Square k = top_king;
      for (int j = top - 1; j >= i; j--) {
        k = king_before(stack[j + 1].dirty, side, k);
        if (king_state(side, k) != st) break;
        unapply(stack[j + 1], stack[j], side, st);
      }
      return;
    }
    king = before;
    i--;
  }
  for (int j = i + 1; j <= top; j++) apply(stack[j - 1], stack[j], side, st);
}

void update(const Position& pos) {
  const Accumulator& acc = pos.accumulators()[pos.game_ply()];
  for (Color side : {WHITE, BLACK})
    if (acc.net[side] != net_id) update(pos, side);
}

}  // namespace

extern const unsigned char default_net_data[];
extern const std::size_t default_net_size;

bool load(const unsigned char* data, std::size_t size) {
  have_net = false;
  char magic[4];
  std::uint32_t version = 0, hidden = 0;
  std::size_t pos = 0;
  if (!take(data, size, pos, magic, 4) || !take(data, size, pos, &version, 4) || !take(data, size, pos, &hidden, 4))
    return false;
  if (std::memcmp(magic, "E2NN", 4) != 0 || version < 1 || version > 4 || hidden != HIDDEN) return false;
  // Version 2 adds the activation and the number of output layers; version
  // 3 also king buckets (their count, the mirror flag and the 64-square
  // map); version 1 is plain clipped units and one output. The activation
  // field: 0 clipped, 1 squared (SCReLU), 2 squared with fine output weights.
  std::uint32_t screlu = 0, buckets = 1, king_buckets = 1, mirror = 0;
  std::uint8_t king_map[64] = {};
  if (version >= 2 && (!take(data, size, pos, &screlu, 4) || !take(data, size, pos, &buckets, 4))) return false;
  if (version >= 3 && (!take(data, size, pos, &king_buckets, 4) || !take(data, size, pos, &mirror, 4) ||
                       !take(data, size, pos, king_map, 64)))
    return false;
  // Version 4: the extra layers' sizes, which must be the engine's.
  std::uint32_t l1 = 0, l2 = 0;
  if (version >= 4 && (!take(data, size, pos, &l1, 4) || !take(data, size, pos, &l2, 4) || l1 != L1 || l2 != L2 ||
                       screlu != 1))
    return false;
  if (screlu > 2 || buckets < 1 || buckets > MAX_BUCKETS || king_buckets < 1 || king_buckets > MAX_KING_BUCKETS || mirror > 1)
    return false;
  for (std::uint8_t b : king_map)
    if (b >= king_buckets) return false;
  net.ft_weight.resize(std::size_t(king_buckets) * INPUTS * HIDDEN);
  if (!take(data, size, pos, net.ft_weight.data(), net.ft_weight.size() * sizeof(std::int16_t)) ||
      !take(data, size, pos, net.ft_bias, sizeof(net.ft_bias)))
    return false;
  net.layers = version >= 4;
  if (net.layers) {
    std::vector<std::int16_t> w1(std::size_t(buckets) * L1 * 2 * HIDDEN);
    std::vector<float> w2(std::size_t(buckets) * L2 * L1);
    if (!take(data, size, pos, w1.data(), w1.size() * sizeof(std::int16_t)) ||
        !take(data, size, pos, net.l1_bias, buckets * sizeof(net.l1_bias[0])) ||
        !take(data, size, pos, w2.data(), w2.size() * sizeof(float)) ||
        !take(data, size, pos, net.l2_bias, buckets * sizeof(net.l2_bias[0])) ||
        !take(data, size, pos, net.l3_weight, buckets * sizeof(net.l3_weight[0])) ||
        !take(data, size, pos, net.l3_bias, buckets * sizeof(float)) || pos != size)
      return false;
    net.l1_weight.resize(w1.size());
    for (std::size_t b = 0; b < buckets; b++)
      for (int o = 0; o < L1; o++)
        for (int i = 0; i < 2 * HIDDEN; i++)
          net.l1_weight[((b * HIDDEN + i / 2) * L1 + o) * 2 + (i & 1)] = w1[(b * L1 + o) * 2 * HIDDEN + i];
    for (std::size_t b = 0; b < buckets; b++)
      for (int o = 0; o < L2; o++)
        for (int i = 0; i < L1; i++) net.l2_weight[b][i][o] = w2[(b * L2 + o) * L1 + i];
  } else if (!take(data, size, pos, net.out_weight, buckets * sizeof(net.out_weight[0])) ||
             !take(data, size, pos, net.out_bias, buckets * sizeof(net.out_bias[0])) || pos != size) {
    return false;
  }
  net.screlu = screlu >= 1;
  net.fine = screlu == 2;
  net.buckets = int(buckets);
  net.king_buckets = int(king_buckets);
  net.mirror = mirror == 1;
  std::memcpy(net.king_map, king_map, 64);
  if (++net_id == 0) net_id = 1;
  have_net = true;
  return true;
}

bool load(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    have_net = false;
    return false;
  }
  const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return load(bytes.data(), bytes.size());
}

void load_default() { load(default_net_data, default_net_size); }

void unload() { have_net = false; }
bool loaded() { return have_net; }

int evaluate(const Position& pos) {
  const Accumulator& acc = pos.accumulators()[pos.game_ply()];
  if (acc.net[WHITE] != net_id || acc.net[BLACK] != net_id) update(pos);
  // Accumulators fit in 16 bits: the trainer keeps first-layer weights and
  // biases within its --ft-clip (1.98 by default, at most 3.89), so a bias
  // and 32 pieces, 33 terms of at most 3.89 * QA, stay under 32767.
  const Color us = pos.side_to_move();
  // The output layer for this many pieces (2 to 32): equal steps of piece
  // count, the fullest boards in the last. train.py's bucket_of is the same.
  const int bucket = net.buckets == 1 ? 0 : std::min((popcount(pos.pieces()) - 2) * net.buckets / 32, net.buckets - 1);
  if (net.layers) return int(evaluate_layers(acc.values[us], acc.values[~us], bucket) * SCALE);
  const std::int16_t* w = net.out_weight[bucket];
  if (net.fine) {
    // The sum is in units of QA x QA x QB_FINE / 2^(15 - FINE_SHIFT): times
    // 256 and over QA, it is in the bias's units, QA x QB_FINE.
    const std::int32_t sum = output_squared_fine(acc.values[us], acc.values[~us], w);
    return int(((std::int64_t(sum) << (15 - FINE_SHIFT)) / QA + net.out_bias[bucket]) * SCALE / (QA * QB_FINE));
  }
  if (net.screlu) {
    const std::int32_t sum = output_squared(acc.values[us], acc.values[~us], w);
    return int((std::int64_t(sum) / QA + net.out_bias[bucket]) * SCALE / (QA * QB));
  }
  const std::int32_t sum = output(acc.values[us], acc.values[~us], w);
  return int((std::int64_t(sum) + net.out_bias[bucket]) * SCALE / (QA * QB));
}

}  // namespace chess::nnue
