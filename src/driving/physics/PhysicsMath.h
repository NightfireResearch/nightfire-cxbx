#ifndef DRIVING_PHYSICS_PHYSICSMATH_H_
#define DRIVING_PHYSICS_PHYSICSMATH_H_

// ---------------------------------------------------------------------------------------------------------------
// The game's out-of-line float helpers, written inline here: fabs (0x0001b160; -0 stays -0, a NaN stays), and the
// lesser (0x0001c600) and greater (0x0001c620) of two, each answering the second when they are unordered. Each
// answers in ST0 the float it loaded, so a float result is exact.
// ---------------------------------------------------------------------------------------------------------------

#include <stdint.h>
#include <bit>

// The sign flipped by its bit: MSVC compiles `x < 0 ? -x : x` to an ANDPS, which clears -0's sign.
inline float Abs(float x) {
    return x < 0.0f ? std::bit_cast<float>(std::bit_cast<uint32_t>(x) ^ 0x80000000u) : x;
}

inline float Min(float a, float b) {
    return a < b ? a : b;
}

inline float Max(float a, float b) {
    return a > b ? a : b;
}

#endif // DRIVING_PHYSICS_PHYSICSMATH_H_
