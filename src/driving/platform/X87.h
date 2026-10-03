#ifndef DRIVING_PLATFORM_X87_H_
#define DRIVING_PLATFORM_X87_H_

// The conversions the driving engine's x87 code makes, for the ports that reproduce it bit for bit (docs/driving/
// maths.md 3.2). One copy for every port: before this header each file carried its own.

#include <math.h>
#include <stdint.h>
#include <bit>
#include <xmmintrin.h>
#include <emmintrin.h>

// CVTTSS2SI: truncation towards zero; out of range and NaN give 0x80000000.
inline int Truncate(float f) {
    return _mm_cvtt_ss2si(_mm_set_ss(f));
}

// CVTSS2SI: rounding by MXCSR (to nearest even); out of range and NaN give 0x80000000.
inline int RoundToInt(float f) {
    return _mm_cvtss_si32(_mm_set_ss(f));
}

// MSVC's __ftol2 as the compiled code uses it, the low dword of a truncating conversion of ST0 to a 64-bit integer.
// The routine rounds with FISTP and corrects towards zero, skipping the correction when the rounded value's low
// dword is 0 - so NaN, anything outside the 64-bit range, and values that round to a multiple of 2^32 give 0. Inside
// the int32 range that is exactly truncation.
inline int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// A float moved through the x87 (FLD then FSTP) rather than by integer moves: a signalling NaN comes out quiet.
inline float QuietNaN(float f) {
    uint32_t bits = std::bit_cast<uint32_t>(f);
    if ((bits & 0x7f800000) == 0x7f800000 && (bits & 0x007fffff) != 0)
        bits |= 0x00400000;
    return std::bit_cast<float>(bits);
}

#endif // DRIVING_PLATFORM_X87_H_
