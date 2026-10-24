#ifndef COMMON_MATH_LIBM_H
#define COMMON_MATH_LIBM_H

// The transcendentals the C forms of the x87 code call (both engines, NIGHTFIRE_X87 or NIGHTFIRE_ACTION_X87 off):
// DetMath's, the same bits on every host, with NIGHTFIRE_DET_MATH (the default); the host's <math.h> without it.

#include <math.h>
#include "DetMath.h"

namespace Libm {

#if NIGHTFIRE_DET_MATH
using DetMath::Sin;
using DetMath::Cos;
using DetMath::Tan;
using DetMath::Atan2;
using DetMath::Asin;
using DetMath::Acos;
using DetMath::Exp;
using DetMath::Log;
using DetMath::Pow;
#else
inline double Sin(double x) { return sin(x); }
inline double Cos(double x) { return cos(x); }
inline double Tan(double x) { return tan(x); }
inline double Atan2(double y, double x) { return atan2(y, x); }
inline double Asin(double x) { return asin(x); }
inline double Acos(double x) { return acos(x); }
inline double Exp(double x) { return exp(x); }
inline double Log(double x) { return log(x); }
inline double Pow(double x, double y) { return pow(x, y); }
#endif

} // namespace Libm

#endif // COMMON_MATH_LIBM_H
