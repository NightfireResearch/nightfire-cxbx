#ifndef ACTION_MATH_X87OPS_H
#define ACTION_MATH_X87OPS_H

// What the action engine's x87 code does where MSVC's SSE2 doubles would differ: FCHS, FSIN and FCOS, and the NaN
// an operation on two NaNs gives.

#include <math.h>
#include <stdint.h>
#include <bit>

// A value on the x87 stack, in the FPU's own 80-bit format. FSIN and FCOS give a full 64-bit significand whatever
// the precision control says, which no C++ type holds on MSVC, so code that uses their results before storing them
// works in this type. Its arithmetic is the x87's (X87Ops.cpp): rounded as the precision control says, and an
// operation on two NaNs gives the NaN the x87 chooses. A conversion to float or double is one FSTP, one rounding.
struct Extended {
    uint8_t bytes[10];

    Extended() = default;
    Extended(float x);
    Extended(double x);
    explicit operator float() const;
    explicit operator double() const;
};

// FSIN and FCOS: the instructions, so a huge angle (2^63 and beyond) comes back unchanged and a NaN as itself
Extended FSin(double x);
Extended FCos(double x);

Extended operator+(const Extended &a, const Extended &b);
Extended operator-(const Extended &a, const Extended &b);
Extended operator*(const Extended &a, const Extended &b);
Extended operator/(const Extended &a, const Extended &b);
Extended operator-(const Extended &a);

// FCHS: the sign bit flipped, a NaN's too (the compiler may turn a plain -x + c into c - x, which keeps it)
inline double Negate(double x) {
    return std::bit_cast<double>(std::bit_cast<uint64_t>(x) ^ 0x8000000000000000ull);
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
