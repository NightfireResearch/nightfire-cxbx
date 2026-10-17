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

Extended FPatan(double y, double x) {
    Extended result;
    PartialArctangent(&result, y, x);
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

// FCHS
Extended operator-(const Extended &a) {
    Extended result = a;
    result.bytes[9] ^= 0x80;
    return result;
}
