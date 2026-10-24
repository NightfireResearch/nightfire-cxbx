#ifndef COMMON_MATH_DETMATH_H
#define COMMON_MATH_DETMATH_H

// The C library's transcendental functions in plain C++, the same bits on every host: fdlibm's algorithms (FreeBSD
// msun), with nothing but IEEE double additions, multiplications, divisions and square roots, each rounded once.
// The hosts' own libraries (MSVC's UCRT, glibc, Apple's libm) each round about one result in thirty differently in
// the last place, and the 32-bit UCRT's sin and cos lose their range reduction above 2^20 (docs/deterministic-maths.md).
//
// What every host must do for these to agree: double arithmetic in SSE2 or NEON registers (not the x87), no fused
// multiply-adds (-ffp-contract=off; MSVC contracts only with /fp:contract or /fp:fast), no fast-math, and the default
// rounding and denormals (no flush to zero).
//
// A NaN argument comes back quiet; an invalid operation (the sine of an infinity, the arc sine of 2) gives the x87's
// default NaN (negative), where SSE2 and NEON would differ in its sign.

namespace DetMath {

double Sin(double x);
double Cos(double x);
double Tan(double x);
double Atan(double x);
double Atan2(double y, double x);
double Asin(double x);
double Acos(double x);
double Exp(double x);

} // namespace DetMath

#endif // COMMON_MATH_DETMATH_H
