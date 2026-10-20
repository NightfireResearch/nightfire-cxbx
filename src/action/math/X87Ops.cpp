#include "X87Ops.h"

// The x87 instructions behind Extended (X87Ops.h). Each loads its operands, does one operation and stores the
// 80-bit result, so it is rounded only as the instruction itself rounds it.

static void __declspec(naked) LoadFloat(Extended *out, float x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld dword ptr [esp + 8]
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) LoadDouble(Extended *out, double x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld qword ptr [esp + 8]
        fstp tbyte ptr [eax]
        ret
    }
}

// The result goes through memory, so what comes back in ST0 is exactly the float stored
static float __declspec(naked) StoreFloat(const Extended *x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld tbyte ptr [eax]
        fstp dword ptr [esp + 4]
        fld dword ptr [esp + 4]
        ret
    }
}

static double __declspec(naked) StoreDouble(const Extended *x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld tbyte ptr [eax]
        sub esp, 8
        fstp qword ptr [esp]
        fld qword ptr [esp]
        add esp, 8
        ret
    }
}

static void __declspec(naked) Sine(Extended *out, double x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld qword ptr [esp + 8]
        fsin
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) Cosine(Extended *out, double x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld qword ptr [esp + 8]
        fcos
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) SineOf(Extended *out, const Extended *x) {
    __asm {
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        fsin
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) CosineOf(Extended *out, const Extended *x) {
    __asm {
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        fcos
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) ArcCosine(Extended *out, double x) {
    __asm {
        fld qword ptr [esp + 8]
        mov eax, 0x000ee294
        call eax
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) PartialArctangent(Extended *out, double y, double x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld qword ptr [esp + 8]
        fld qword ptr [esp + 16]
        fpatan
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) Tangent(Extended *out, double x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld qword ptr [esp + 8]
        fptan
        fstp st(0)
        fstp tbyte ptr [eax]
        ret
    }
}

static void __declspec(naked) Exponential(Extended *out, double x) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld qword ptr [esp + 8]
        fldl2e
        fmulp st(1), st(0)
        fld st(0)
        frndint
        fxch st(1)
        fsub st(0), st(1)
        f2xm1
        fld1
        faddp st(1), st(0)
        fscale
        fstp st(1)
        fstp tbyte ptr [eax]
        ret
    }
}

// The operations take a in ST0 and b in ST1 and write ST0, the forms that every assembler reads the same way
static void __declspec(naked) Add(Extended *out, const Extended *a, const Extended *b) {
    __asm {
        mov eax, dword ptr [esp + 12]
        fld tbyte ptr [eax]
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        fadd st(0), st(1)
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        fstp st(0)
        ret
    }
}

static void __declspec(naked) Subtract(Extended *out, const Extended *a, const Extended *b) {
    __asm {
        mov eax, dword ptr [esp + 12]
        fld tbyte ptr [eax]
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        fsub st(0), st(1)
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        fstp st(0)
        ret
    }
}

static void __declspec(naked) Multiply(Extended *out, const Extended *a, const Extended *b) {
    __asm {
        mov eax, dword ptr [esp + 12]
        fld tbyte ptr [eax]
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        fmul st(0), st(1)
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        fstp st(0)
        ret
    }
}

static void __declspec(naked) Divide(Extended *out, const Extended *a, const Extended *b) {
    __asm {
        mov eax, dword ptr [esp + 12]
        fld tbyte ptr [eax]
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        fdiv st(0), st(1)
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        fstp st(0)
        ret
    }
}

// 1 if a < b (FCOMPP's C0 without C2, so not for a NaN), else 0
static int __declspec(naked) Below(const Extended *a, const Extended *b) {
    __asm {
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        mov eax, dword ptr [esp + 4]
        fld tbyte ptr [eax]
        fcompp
        fnstsw ax
        and eax, 0x0500
        cmp eax, 0x0100
        sete al
        movzx eax, al
        ret
    }
}

Extended::Extended(float x) {
    LoadFloat(this, x);
}

Extended::Extended(double x) {
    LoadDouble(this, x);
}

Extended::operator float() const {
    return StoreFloat(this);
}

Extended::operator double() const {
    return StoreDouble(this);
}

Extended FSin(double x) {
    Extended result;
    Sine(&result, x);
    return result;
}

Extended FCos(double x) {
    Extended result;
    Cosine(&result, x);
    return result;
}

Extended FSin(const Extended &x) {
    Extended result;
    SineOf(&result, &x);
    return result;
}

Extended FCos(const Extended &x) {
    Extended result;
    CosineOf(&result, &x);
    return result;
}

Extended CIAcos(double x) {
    Extended result;
    ArcCosine(&result, x);
    return result;
}

Extended FPatan(double y, double x) {
    Extended result;
    PartialArctangent(&result, y, x);
    return result;
}

Extended FPtan(double x) {
    Extended result;
    Tangent(&result, x);
    return result;
}

Extended FExp(double x) {
    Extended result;
    Exponential(&result, x);
    return result;
}

Extended operator+(const Extended &a, const Extended &b) {
    Extended result;
    Add(&result, &a, &b);
    return result;
}

Extended operator-(const Extended &a, const Extended &b) {
    Extended result;
    Subtract(&result, &a, &b);
    return result;
}

Extended operator*(const Extended &a, const Extended &b) {
    Extended result;
    Multiply(&result, &a, &b);
    return result;
}

Extended operator/(const Extended &a, const Extended &b) {
    Extended result;
    Divide(&result, &a, &b);
    return result;
}

bool operator<(const Extended &a, const Extended &b) {
    return Below(&a, &b) != 0;
}

bool operator>(const Extended &a, const Extended &b) {
    return Below(&b, &a) != 0;
}

Extended AbsFchs(const Extended &x) {
    double rounded = (double)x;
    return x < Extended(0.0f) || rounded != rounded ? -x : x;
}

// FCHS
Extended operator-(const Extended &a) {
    Extended result = a;
    result.bytes[9] ^= 0x80;
    return result;
}

static void __declspec(naked) SquareRoot(Extended *out, const Extended *x) {
    __asm {
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        fsqrt
        mov eax, dword ptr [esp + 4]
        fstp tbyte ptr [eax]
        ret
    }
}

// FISTP to 64 bits, rounded as the control word says (to nearest) and truncated
static void __declspec(naked) ToIntegers(int64_t *nearest, int64_t *truncated, const Extended *x) {
    __asm {
        mov eax, dword ptr [esp + 12]
        fld tbyte ptr [eax]
        fld st(0)
        mov eax, dword ptr [esp + 4]
        fistp qword ptr [eax]
        sub esp, 4
        fnstcw word ptr [esp]
        mov cx, word ptr [esp]
        or cx, 0x0c00
        mov word ptr [esp + 2], cx
        fldcw word ptr [esp + 2]
        mov eax, dword ptr [esp + 12]
        fistp qword ptr [eax]
        fldcw word ptr [esp]
        add esp, 4
        ret
    }
}

Extended FSqrt(const Extended &x) {
    Extended result;
    SquareRoot(&result, &x);
    return result;
}

int32_t Ftol(const Extended &x) {
    if (!(AbsFchs(x) < Extended(9223372036854775808.0)))
        return 0;
    int64_t nearest, truncated;
    ToIntegers(&nearest, &truncated, &x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(truncated);
}
