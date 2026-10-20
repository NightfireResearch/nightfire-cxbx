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
// ... of a value still on the x87 stack, unrounded
Extended FSin(const Extended &x);
Extended FCos(const Extended &x);
// FPATAN: the angle of (x, y), atan(y / x) in the right quadrant
Extended FPatan(double y, double x);
// FPTAN: the tangent (the 1 the instruction pushes after it is popped)
Extended FPtan(double x);
// The C runtime's acos (__CIacos, Crt::CIacos): the argument and the result in ST0. Outside -1 to 1 it is a domain
// error, which sets errno in the per-thread block the runtime allocates on the process heap.
Extended CIAcos(double x);
// e^x as MSVC's inline exp computes it: FLDL2E, FMUL, then FRNDINT, F2XM1, FLD1, FADD and FSCALE on the whole and
// fractional parts
Extended FExp(double x);

Extended operator+(const Extended &a, const Extended &b);
Extended operator-(const Extended &a, const Extended &b);
Extended operator*(const Extended &a, const Extended &b);
Extended operator/(const Extended &a, const Extended &b);
Extended operator-(const Extended &a);
// FCOMP: false where either is a NaN
bool operator<(const Extended &a, const Extended &b);
bool operator>(const Extended &a, const Extended &b);
// |x| as the original takes it, comparing with 0 and FCHS when below or a NaN (AbsFchs, below, for a double)
Extended AbsFchs(const Extended &x);
// FSQRT
Extended FSqrt(const Extended &x);
// MSVC's __ftol2 of a value still on the x87 stack: the low word of its truncation to 64 bits, 0 where it is out of
// that range or rounds to a multiple of 2^32 (as Ftol in src/driving/platform/X87.h has it for a double)
int32_t Ftol(const Extended &x);

// FCHS: the sign bit flipped, a NaN's too (the compiler may turn a plain -x + c into c - x, which keeps it)
inline double Negate(double x) {
    return std::bit_cast<double>(std::bit_cast<uint64_t>(x) ^ 0x8000000000000000ull);
}

// |x| as the original takes it, comparing with 0 and FCHS when below: -0 keeps its sign, and a NaN is negated
inline double AbsFchs(double x) {
    return x >= 0.0 ? x : Negate(x);
}

// A float the original copies through the x87 (FLD, FSTP): a signalling NaN comes back quiet, where a plain copy
// keeps it
inline float X87Copy(float x) {
    uint32_t bits = std::bit_cast<uint32_t>(x);
    if ((bits & 0x7f800000u) == 0x7f800000u && (bits & 0x007fffffu) != 0)
        bits |= 0x00400000u;
    return std::bit_cast<float>(bits);
}

// FLD, FCHS and FSTP of a float
inline float X87Negate(float x) {
    return std::bit_cast<float>(std::bit_cast<uint32_t>(X87Copy(x)) ^ 0x80000000u);
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
