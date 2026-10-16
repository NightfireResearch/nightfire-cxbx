#ifndef ACTION_MATH_X87OPS_H
#define ACTION_MATH_X87OPS_H

// What the action engine's x87 code does where MSVC's SSE2 doubles would differ: FCHS, FSIN and FCOS, and the NaN
// an operation on two NaNs gives. Finite FSIN and FCOS results through the host's sin and cos may come out an ulp
// apart (accepted: docs/nightfire-port-lifts.md, parcel 7).

#include <math.h>
#include <stdint.h>
#include <bit>

// FCHS: the sign bit flipped, a NaN's too (the compiler may turn a plain -x + c into c - x, which keeps it)
inline double Negate(double x) {
    return std::bit_cast<double>(std::bit_cast<uint64_t>(x) ^ 0x8000000000000000ull);
}

// FSIN: a NaN comes back as itself
inline double FSin(double x) {
    if (x != x)
        return x;
    return sin(x);
}

// FCOS: a NaN comes back as itself
inline double FCos(double x) {
    if (x != x)
        return x;
    return cos(x);
}

// What the x87 gives for an operation on two NaNs: the one with the larger significand, the positive one when only
// their signs differ (SSE2 gives the first operand). Otherwise the result as computed.
inline double X87NaNs(double result, double a, double b) {
    if (a == a || b == b)
        return result;
    uint64_t magnitudeA = std::bit_cast<uint64_t>(a) & 0x7fffffffffffffffull;
    uint64_t magnitudeB = std::bit_cast<uint64_t>(b) & 0x7fffffffffffffffull;
    if (magnitudeA != magnitudeB)
        return magnitudeA > magnitudeB ? a : b;
    return std::bit_cast<double>(std::bit_cast<uint64_t>(a) & std::bit_cast<uint64_t>(b));
}

#endif // ACTION_MATH_X87OPS_H
