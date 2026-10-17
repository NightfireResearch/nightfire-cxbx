#ifndef DRIVING_PLATFORM_SSEC_H_
#define DRIVING_PLATFORM_SSEC_H_

// The x86 instructions the driving engine's ports use, written in C with the instructions' results, for hosts
// without them (Sse.h chooses; docs/driving/macos.md 3). Each lane is IEEE single precision as SSE computes it with
// the game's MXCSR, the default: rounding to nearest even, denormals kept. Where the result is NaN, SSE's choice is
// made here rather than the host's: an operand's NaN, quieted (the first operand's when both are NaN), or the
// negative default NaN 0xffc00000 from an invalid operation; ARM's default NaN is positive and it prefers a
// signalling operand. RSQRTSS's estimate is the runner's Intel's, from a table of its results (SseRsqrtTable.h).

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <chrono>

#include "SseRsqrtTable.h"

namespace sse {

struct alignas(16) m128 {
    float lane[4];
};

struct alignas(16) m128i {
    int32_t lane[4];
};

inline uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

inline float FromBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

inline bool IsNaN(float f) {
    return (Bits(f) & 0x7fffffff) > 0x7f800000;
}

inline float Quiet(float f) {
    return FromBits(Bits(f) | 0x00400000);
}

const uint32_t kDefaultNaN = 0xffc00000;

// The NaN an operation on a and b gives when its result is NaN
inline float NaNResult(float a, float b) {
    if (IsNaN(a))
        return Quiet(a);
    if (IsNaN(b))
        return Quiet(b);
    return FromBits(kDefaultNaN);
}

inline float Add(float a, float b) {
    float r = a + b;
    return IsNaN(r) ? NaNResult(a, b) : r;
}

inline float Sub(float a, float b) {
    float r = a - b;
    return IsNaN(r) ? NaNResult(a, b) : r;
}

inline float Mul(float a, float b) {
    float r = a * b;
    return IsNaN(r) ? NaNResult(a, b) : r;
}

inline float Sqrt(float a) {
    if (IsNaN(a))
        return Quiet(a);
    if (Bits(a) > 0x80000000)   // below -0
        return FromBits(kDefaultNaN);
    return sqrtf(a);
}

// RSQRTSS: zero and denormals give infinity of their sign, anything else negative the default NaN, +infinity +0.
// The estimate depends on the exponent's parity and the mantissa's top bits; the table holds the results for
// exponents 126 and 127, and every two steps of the exponent halve the result exactly.
inline float RsqrtEstimate(float a) {
    uint32_t u = Bits(a);
    if (IsNaN(a))
        return Quiet(a);
    uint32_t exponent = (u >> 23) & 0xff;
    if (exponent == 0)
        return FromBits((u & 0x80000000) | 0x7f800000);
    if (u & 0x80000000)
        return FromBits(kDefaultNaN);
    if (exponent == 0xff)
        return 0.0f;
    uint32_t odd = exponent & 1;
    uint32_t estimate = kRsqrtEstimate[odd][(u & 0x007fffff) >> (23 - kRsqrtIndexBits)];
    int32_t halvings = (int32_t(exponent) - int32_t(126 + odd)) / 2;
    return FromBits(estimate - uint32_t(halvings * (1 << 23)));
}

// CVTTSS2SI and CVTSS2SI: out of range and NaN give 0x80000000
inline int32_t TruncateToInt(float a) {
    if (!(a >= -2147483648.0f && a < 2147483648.0f))
        return INT32_MIN;
    return int32_t(a);
}

inline int32_t RoundToNearestInt(float a) {
    if (!(a >= -2147483648.0f && a < 2147483648.0f))
        return INT32_MIN;
    int32_t whole = int32_t(a);
    float fraction = a - float(whole);   // exact
    if (fraction > 0.5f || (fraction == 0.5f && (whole & 1) != 0))
        whole++;
    else if (fraction < -0.5f || (fraction == -0.5f && (whole & 1) != 0))
        whole--;
    return whole;
}

// ---- the intrinsics, by their names without _mm_

inline m128 setzero_ps() {
    return m128{{0.0f, 0.0f, 0.0f, 0.0f}};
}

inline m128 set_ss(float a) {
    return m128{{a, 0.0f, 0.0f, 0.0f}};
}

inline m128 set1_ps(float a) {
    return m128{{a, a, a, a}};
}

inline m128 set_ps(float e3, float e2, float e1, float e0) {
    return m128{{e0, e1, e2, e3}};
}

inline m128 setr_ps(float e0, float e1, float e2, float e3) {
    return m128{{e0, e1, e2, e3}};
}

inline m128 load_ss(const float *p) {
    m128 r = setzero_ps();
    memcpy(&r.lane[0], p, 4);
    return r;
}

inline m128 loadu_ps(const float *p) {
    m128 r;
    memcpy(r.lane, p, 16);
    return r;
}

inline m128 load_ps(const float *p) {
    return loadu_ps(p);
}

inline m128i load_si128(const m128i *p) {
    m128i r;
    memcpy(r.lane, p, 16);
    return r;
}

inline void store_ss(float *p, m128 a) {
    memcpy(p, &a.lane[0], 4);
}

inline void storeu_ps(float *p, m128 a) {
    memcpy(p, a.lane, 16);
}

inline void store_ps(float *p, m128 a) {
    storeu_ps(p, a);
}

inline float cvtss_f32(m128 a) {
    return a.lane[0];
}

inline m128 add_ps(m128 a, m128 b) {
    return m128{{Add(a.lane[0], b.lane[0]), Add(a.lane[1], b.lane[1]), Add(a.lane[2], b.lane[2]),
                 Add(a.lane[3], b.lane[3])}};
}

inline m128 sub_ps(m128 a, m128 b) {
    return m128{{Sub(a.lane[0], b.lane[0]), Sub(a.lane[1], b.lane[1]), Sub(a.lane[2], b.lane[2]),
                 Sub(a.lane[3], b.lane[3])}};
}

inline m128 mul_ps(m128 a, m128 b) {
    return m128{{Mul(a.lane[0], b.lane[0]), Mul(a.lane[1], b.lane[1]), Mul(a.lane[2], b.lane[2]),
                 Mul(a.lane[3], b.lane[3])}};
}

// The _ss forms compute lane 0 and keep a's other lanes.
inline m128 add_ss(m128 a, m128 b) {
    a.lane[0] = Add(a.lane[0], b.lane[0]);
    return a;
}

inline m128 sub_ss(m128 a, m128 b) {
    a.lane[0] = Sub(a.lane[0], b.lane[0]);
    return a;
}

inline m128 mul_ss(m128 a, m128 b) {
    a.lane[0] = Mul(a.lane[0], b.lane[0]);
    return a;
}

inline m128 sqrt_ss(m128 a) {
    a.lane[0] = Sqrt(a.lane[0]);
    return a;
}

inline m128 rsqrt_ss(m128 a) {
    a.lane[0] = RsqrtEstimate(a.lane[0]);
    return a;
}

inline m128 xor_ps(m128 a, m128 b) {
    m128 r;
    for (int i = 0; i < 4; i++)
        r.lane[i] = FromBits(Bits(a.lane[i]) ^ Bits(b.lane[i]));
    return r;
}

// Lanes 0 and 1 from a, 2 and 3 from b, each chosen by two bits of the selector.
inline m128 shuffle_ps(m128 a, m128 b, int selector) {
    return m128{{a.lane[selector & 3], a.lane[(selector >> 2) & 3], b.lane[(selector >> 4) & 3],
                 b.lane[(selector >> 6) & 3]}};
}

inline m128 cvtepi32_ps(m128i a) {
    return m128{{float(a.lane[0]), float(a.lane[1]), float(a.lane[2]), float(a.lane[3])}};
}

inline m128 cvtsi32_ss(m128 a, int b) {
    a.lane[0] = float(b);
    return a;
}

inline int cvtt_ss2si(m128 a) {
    return TruncateToInt(a.lane[0]);
}

inline int cvtss_si32(m128 a) {
    return RoundToNearestInt(a.lane[0]);
}

inline void empty() {
}

// RDTSC as a count of the console's 733,333,333 Hz cycles, from the host's steady clock (as XboxTimer.cpp's
// Xbox_ReadCycleCounter counts them on Windows).
inline uint64_t rdtsc() {
    const uint64_t kXboxHz = 733333333;
    uint64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count();
    return ns / 1000000000 * kXboxHz + ns % 1000000000 * kXboxHz / 1000000000;
}

// REP MOVSD and REP MOVSB: element by element, upwards, as the instructions copy overlapping ranges.
inline void movsd(void *destination, const void *source, size_t count) {
    for (size_t i = 0; i < count; i++) {
        uint32_t word;
        memcpy(&word, (const char *)source + 4 * i, 4);
        memcpy((char *)destination + 4 * i, &word, 4);
    }
}

inline void movsb(unsigned char *destination, const unsigned char *source, size_t count) {
    for (size_t i = 0; i < count; i++)
        destination[i] = source[i];
}

} // namespace sse

#endif // DRIVING_PLATFORM_SSEC_H_
