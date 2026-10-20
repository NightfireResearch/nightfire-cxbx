#include "CRuntime.h"
#include "CRuntimeInternal.h"

#include "Win32Types.h"
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The C runtime's x87 helpers: the _CI arc cosine, arc sine and pow, and the floating-point set-up and status.
// Both engines' runtimes are this code (the same XDK library); the tags are the driving engine's, and the action
// engine calls these from its own runtime (src/action/engine/CRuntime.cpp).
// ---------------------------------------------------------------------------------------------------------------

namespace Crt {
namespace {

// What the _CI functions answer outside the instructions' own range: a domain error (errno EDOM), the argument
// itself if it is a NaN (quietened), the x87's indefinite NaN otherwise.
double DomainError(double x) {
    *_errno() = kDomainError;
    uint64_t bits;
    memcpy(&bits, &x, 8);
    if ((bits & 0x7ff0000000000000ull) == 0x7ff0000000000000ull && (bits & 0x000fffffffffffffull) != 0)
        bits |= 0x0008000000000000ull;
    else
        bits = 0xfff8000000000000ull;
    double result;
    memcpy(&result, &bits, 8);
    return result;
}

#if NIGHTFIRE_X87
// A NaN operand's NaN, as the x87 picks it from two (the larger significand, quietened).
__declspec(naked) double X87NaN(double, double) {
    __asm {
        fld qword ptr [esp + 4]
        fadd qword ptr [esp + 12]
        ret
    }
}
#else
// A NaN operand's NaN, as the x87 picks it from two: FLD quietens the first; of two quiet NaNs the larger
// significand, and a signalling second loses to the quiet first.
double X87NaN(double x, double y) {
    const uint64_t kQuiet = 0x0008000000000000ull, kSignificand = 0x000fffffffffffffull;
    uint64_t a, b;
    memcpy(&a, &x, 8);
    memcpy(&b, &y, 8);
    uint64_t pick;
    if (x != x && y != y)
        pick = (b & kQuiet) == 0 || ((a | kQuiet) & kSignificand) >= (b & kSignificand) ? a : b;
    else
        pick = x != x ? a : b;
    pick |= kQuiet;
    double result;
    memcpy(&result, &pick, 8);
    return result;
}
#endif

// pow where the x87 path does not apply: a zero, infinite or NaN operand, or a negative base with a fractional
// exponent. As the runtime: a NaN operand gives a NaN and +-1 to an infinite power the indefinite NaN, both domain
// errors (errno EDOM); a negative base with a fractional exponent the indefinite NaN likewise; a zero base a signed
// zero or an infinity (a pole: errno ERANGE). Infinite operands otherwise follow C's rules (pow), as the runtime's do.
double PowEdge(double x, double y) {
    if (x != x || y != y) {
        *_errno() = kDomainError;
        return X87NaN(x, y);
    }
    if (x == 0.0 && fabs(y) != INFINITY) {
        if (y == 0.0)
            return 1.0;
        bool odd = floor(y) == y && fmod(y, 2.0) != 0.0;
        bool negative = signbit(x) && odd;
        if (y < 0.0) {
            *_errno() = kRangeError;
            return negative ? -INFINITY : INFINITY;
        }
        return negative ? -0.0 : 0.0;
    }
    if (fabs(x) == 1.0 && fabs(y) == INFINITY)
        return DomainError(x);
    if (x < 0.0 && fabs(x) != INFINITY && fabs(y) != INFINITY && floor(y) != y)
        return DomainError(x);
    return pow(x, y);
}

// pow's answer past the largest double: the infinity of its sign, a range error (errno ERANGE).
double PowOverflow(double answer) {
    *_errno() = kRangeError;
    return answer;
}

#if NIGHTFIRE_X87
// pow's answer below the normal doubles, as the runtime's masked-underflow handling (_handle_exc) makes it from the
// answer scaled up by 2^1536 and rounded to a double: the mantissa shifted down to a denormal, the bits shifted out
// dropped rather than rounded.
double PowUnderflow(double scaled) {
    if (scaled == 0.0)
        return scaled;
    uint64_t bits;
    memcpy(&bits, &scaled, 8);
    int exponent = int((bits >> 52) & 0x7ff) - 0x3fe - 1536;
    if (exponent < -1074)
        return scaled * 0.0;
    uint64_t mantissa = (bits & 0x000fffffffffffffull) | 0x0010000000000000ull;
    if (exponent < -1021)
        mantissa >>= -1021 - exponent;
    double result;
    memcpy(&result, &mantissa, 8);
    return scaled < 0.0 ? -result : result;
}
#endif

// The x87 status word's exception flags as the runtime's _SW_ bits (__abstract_sw).
unsigned AbstractStatus(unsigned status) {
    unsigned abstract = 0;
    if (status & 0x01)
        abstract = 0x10;                 // invalid
    if (status & 0x04)
        abstract |= 0x08;                // divide by zero
    if (status & 0x08)
        abstract |= 0x04;                // overflow
    if (status & 0x10)
        abstract |= 0x02;                // underflow
    if (status & 0x20)
        abstract |= 0x01;                // inexact
    if (status & 0x02)
        abstract |= 0x80000;             // denormal
    return abstract;
}

} // namespace
} // namespace Crt

#if NIGHTFIRE_X87
// The arc cosine of ST0, answered in ST0 unrounded: atan2(sqrt((1 + x)(1 - x)), x) on the x87, as the runtime
// computes it (with the FPU at the game's 53-bit precision); exactly pi or 0 at -1 and 1.
// FUNC_AT(0x001328d0)
__declspec(naked) void Crt::CIacos() {
    __asm {
        sub esp, 8
        fst qword ptr [esp]
        mov eax, dword ptr [esp + 4]
        and eax, 0x7ff00000
        cmp eax, 0x3ff00000
        jae aboveInside
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        fxch st(1)
        fpatan
        add esp, 8
        ret
    aboveInside:
        ja domainError
        mov eax, dword ptr [esp + 4]
        and eax, 0xfffff
        or eax, dword ptr [esp]
        jnz domainError
        fstp st(0)
        test byte ptr [esp + 7], 0x80
        jz acosOfOne
        fldpi
        add esp, 8
        ret
    acosOfOne:
        fldz
        add esp, 8
        ret
    domainError:
        fstp st(0)
        call DomainError
        add esp, 8
        ret
    }
}

// The arc sine likewise: atan2(x, sqrt((1 + x)(1 - x))); exactly +-pi/2 at +-1.
// FUNC_AT(0x00133f94)
__declspec(naked) void Crt::CIasin() {
    __asm {
        sub esp, 8
        fst qword ptr [esp]
        mov eax, dword ptr [esp + 4]
        and eax, 0x7ff00000
        cmp eax, 0x3ff00000
        jae aboveInside
        fld1
        fadd st, st(1)
        fld1
        fsub st, st(2)
        fmulp st(1), st
        fsqrt
        fpatan
        add esp, 8
        ret
    aboveInside:
        ja domainError
        mov eax, dword ptr [esp + 4]
        and eax, 0xfffff
        or eax, dword ptr [esp]
        jnz domainError
        fstp st(0)
        fld1
        fchs
        fldpi
        fscale
        fstp st(1)
        test byte ptr [esp + 7], 0x80
        jz asinPositive
        fchs
    asinPositive:
        add esp, 8
        ret
    domainError:
        fstp st(0)
        call DomainError
        add esp, 8
        ret
    }
}

// pow(ST1, ST0), answered in ST0 unrounded: 2 to the power y * log2|x| on the x87, as the runtime computes it,
// negated for a negative base with an odd exponent; an answer below the normal doubles is PowUnderflow's. A negative
// base needs a whole exponent; zero, infinite and NaN operands are PowEdge's.
// FUNC_AT(0x00132a94)
__declspec(naked) void Crt::CIpow() {
    __asm {
        sub esp, 24
        fstp qword ptr [esp + 8]         // the exponent
        fstp qword ptr [esp]             // the base
        mov eax, dword ptr [esp + 12]
        and eax, 0x7ff00000
        cmp eax, 0x7ff00000
        je edgeCase
        mov eax, dword ptr [esp + 4]
        and eax, 0x7ff00000
        cmp eax, 0x7ff00000
        je edgeCase
        mov eax, dword ptr [esp + 4]
        and eax, 0x7fffffff
        or eax, dword ptr [esp]
        jz edgeCase
        fld qword ptr [esp + 8]
        fld qword ptr [esp]
        xor ecx, ecx                     // 1: an odd exponent of a negative base
        test byte ptr [esp + 7], 0x80
        jz positiveBase
        fld st(1)                        // is the exponent whole, and odd?
        fld st(0)
        frndint
        fcomp st(1)
        fnstsw ax
        sahf
        jne notWhole
        fld1                             // halved
        fchs
        fxch st(1)
        fscale
        fstp st(1)
        fld st(0)
        frndint
        fcompp
        fnstsw ax
        sahf
        je evenExponent
        inc ecx
    evenExponent:
        fchs                             // the base made positive
    positiveBase:
        fyl2x
        fld st(0)                        // 2 to that: 2^(v - round v) scaled by 2^round v
        frndint
        fsubr st(1), st
        fxch st(1)
        fchs
        f2xm1
        fld1
        faddp st(1), st
        fscale
        fstp st(1)
        test ecx, ecx
        jz rangeCheck
        fchs
    rangeCheck:
        fst qword ptr [esp + 16]         // does it fit a double?
        mov eax, dword ptr [esp + 20]
        and eax, 0x7ff00000
        jz underflow
        cmp eax, 0x7ff00000
        je outOfRange
        add esp, 24
        ret
    underflow:
        sub esp, 8                       // scaled up by 2^1536, or a zero of its sign if still too small
        mov dword ptr [esp], 0
        mov dword ptr [esp + 4], 0x40980000
        fld qword ptr [esp]
        fxch st(1)
        fscale
        fstp st(1)
        mov dword ptr [esp + 4], 0x100000
        fld st(0)
        fabs
        fcomp qword ptr [esp]
        fnstsw ax
        sahf
        jae scaled
        fldz
        fmulp st(1), st
    scaled:
        fstp qword ptr [esp]
        call PowUnderflow
        add esp, 32
        ret
    outOfRange:
        fstp st(0)
        fld qword ptr [esp + 16]
        fstp qword ptr [esp]
        call PowOverflow
        add esp, 24
        ret
    notWhole:
        fstp st(0)
        fstp st(0)
        fstp st(0)
    edgeCase:
        call PowEdge
        add esp, 24
        ret
    }
}
#else
// The runtime's arc cosine, arc sine and pow in C: the same domain errors, overflows and edge cases, the answers
// <math.h>'s. Untagged: the injection's tags are on the x87 forms above.
double Crt::CIacos(double x) {
    if (!(fabs(x) <= 1.0))
        return DomainError(x);
    return acos(x);
}

double Crt::CIasin(double x) {
    if (!(fabs(x) <= 1.0))
        return DomainError(x);
    return asin(x);
}

double Crt::CIpow(double x, double y) {
    if (x == 0.0 || !isfinite(x) || !isfinite(y) || (x < 0.0 && floor(y) != y))
        return PowEdge(x, y);
    double answer = pow(x, y);
    if (isinf(answer))
        return PowOverflow(answer);
    return answer;
}
#endif

#if NIGHTFIRE_X87
// _fpmath, which _cinit calls first: the conversion pointers printf and scanf would go through (_cfltcvt_init: left
// out, our printf and scanf call the conversions directly), then the x87 at 53-bit precision (_setdefaultprecision:
// _controlfp(_PC_53, _MCW_PC), which leaves the other control bits as they are) and its exceptions cleared.
// FUNC_AT(0x00131d28)
__declspec(naked) void Crt::_fpmath() {
    __asm {
        push eax
        fnstcw word ptr [esp]
        and word ptr [esp], 0xfcff
        or word ptr [esp], 0x0200
        fldcw word ptr [esp]
        fnclex
        pop eax
        ret
    }
}

// The x87 status as the runtime abstracts it, the exceptions cleared.
// FUNC_AT(0x00133c9e)
__declspec(naked) unsigned Crt::_clearfp() {
    __asm {
        push ecx
        fnstsw word ptr [esp]
        fnclex
        call AbstractStatus
        pop ecx
        ret
    }
}
#else
// _fpmath without an x87: no precision to set (double is double), the exceptions cleared.
void Crt::_fpmath() {
    feclearexcept(FE_ALL_EXCEPT);
}

// The floating-point exceptions raised, as the runtime abstracts the x87 status, then cleared.
unsigned Crt::_clearfp() {
    int raised = fetestexcept(FE_ALL_EXCEPT);
    feclearexcept(FE_ALL_EXCEPT);
    unsigned status = 0;          // as the x87 status word's flags
    if (raised & FE_INVALID)
        status |= 0x01;
    if (raised & FE_DIVBYZERO)
        status |= 0x04;
    if (raised & FE_OVERFLOW)
        status |= 0x08;
    if (raised & FE_UNDERFLOW)
        status |= 0x10;
    if (raised & FE_INEXACT)
        status |= 0x20;
    return AbstractStatus(status);
}
#endif
