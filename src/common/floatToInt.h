#ifndef COMMON_FLOATTOINT_H
#define COMMON_FLOATTOINT_H

// x86's truncating conversions (CVTTSD2SI, CVTTSS2SI and MSVC's 64-bit helpers) on every host: towards zero, and NaN
// or a value out of range gives the integer indefinite, the type's lowest value. arm64 saturates instead (and gives 0
// for NaN), so a degenerate value - an infinity from a division by zero - would change the game there alone.

#include <stdint.h>

#if defined(_M_IX86) || defined(__i386__) || defined(_M_X64) || defined(__x86_64__)

// The plain cast is the instruction. Macros, so that the builds without inlining (/Ob0) compile exactly as before.
#define TruncateToInt32(x) ((int32_t)(x))
#define TruncateToInt64(x) ((int64_t)(x))

#else

inline int32_t TruncateToInt32(double x) {
    return x > -2147483649.0 && x < 2147483648.0 ? (int32_t)x : INT32_MIN;
}

inline int32_t TruncateToInt32(float x) {
    return x >= -2147483648.0f && x < 2147483648.0f ? (int32_t)x : INT32_MIN;
}

inline int64_t TruncateToInt64(double x) {
    return x >= -9223372036854775808.0 && x < 9223372036854775808.0 ? (int64_t)x : INT64_MIN;
}

#endif

#endif // COMMON_FLOATTOINT_H
