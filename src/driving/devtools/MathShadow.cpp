#include "MathShadow.h"

#include "../platform/RealMath.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <float.h>
#include <xmmintrin.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_MATHSHADOW=1, at injection time on the loader's thread before the game runs: every function of
// platform/RealMath.cpp and VU0Math.cpp against the original at its address (swapped back in for the call,
// common/xbeOriginal.h), on the same inputs, compared bit for bit - every output float (the output buffers are
// filled with the same pattern first, so "keeps w" and "untouched" are checked), the inputs (for the in-place
// functions) and the return value. Inputs are random floats of every size, raw bit patterns (NaN, infinities,
// denormals), the special values, unit quaternions and rotation matrices with translations, as
// docs/driving/maths.md 9.3 asks.
//
// A difference where both sides are NaN is counted apart: the x87 quiets a signalling NaN it loads and picks
// NaN payloads differently from SSE, which only shows on NaN inputs. Original composites call the ported leaves
// (the original BuildRotate calls our sin_fractionalangle), so leaves are tested first.
// ---------------------------------------------------------------------------------------------------------------

static uint32_t g_seed = 0x12345678;
static uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
static float Bits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static uint32_t U(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

static float RandomFloat() {
    static const uint32_t specials[] = { 0x00000000, 0x80000000, 0x00000001, 0x80000001, 0x00800000, 0x3f800000,
                                         0xbf800000, 0x7f7fffff, 0xff7fffff, 0x7f800000, 0xff800000, 0x7fc00000,
                                         0x7fa00000, 0x3f000000, 0x40000000, 0x3727c5ac, 0xb727c5ac, 0x38d1b717 };
    uint32_t r = Next() % 100;
    if (r < 40) {                                   // any size
        float m = (float)(Next() & 0xffffff) / 16777216.0f;
        int e = (int)(Next() % 13) - 6;
        float v = m * powf(10.0f, (float)e);
        return (Next() & 1) ? -v : v;
    }
    if (r < 65)                                     // -1..1
        return (float)((int32_t)Next()) / 2147483648.0f;
    if (r < 75)
        return Bits(specials[Next() % (sizeof(specials) / 4)]);
    if (r < 85)
        return Bits(Next());                        // raw bits
    return (float)((int)(Next() % 21) - 10);        // small integers
}

static void UnitQuat(float *q) {
    float x = RandomFloat(), y = RandomFloat(), z = RandomFloat(), w = RandomFloat();
    if (!(fabsf(x) < 1e6f && fabsf(y) < 1e6f && fabsf(z) < 1e6f && fabsf(w) < 1e6f))
        x = 0.3f, y = -0.2f, z = 0.5f, w = 0.7f;
    double n = sqrt((double)x * x + (double)y * y + (double)z * z + (double)w * w);
    if (n == 0)
        n = 1, w = 1;
    q[0] = (float)(x / n); q[1] = (float)(y / n); q[2] = (float)(z / n); q[3] = (float)(w / n);
}

static void RotationMatrix(float *m) {
    float q[4];
    UnitQuat(q);
    double x = q[0], y = q[1], z = q[2], w = q[3];
    float r[16] = { (float)(1 - 2 * (y * y + z * z)), (float)(2 * (x * y + w * z)), (float)(2 * (x * z - w * y)), 0,
                    (float)(2 * (x * y - w * z)), (float)(1 - 2 * (x * x + z * z)), (float)(2 * (y * z + w * x)), 0,
                    (float)(2 * (x * z + w * y)), (float)(2 * (y * z - w * x)), (float)(1 - 2 * (x * x + y * y)), 0,
                    RandomFloat(), RandomFloat(), RandomFloat(), 1 };
    memcpy(m, r, sizeof(r));
}

enum Kind { ANY, QUATS, MATRICES };

struct Inputs {
    alignas(16) float a[4][64];   // four vectors or matrices
    float s[8];                   // scalars
    uint32_t u[4];
};

static void Generate(Inputs *in, Kind kind) {
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 64; j++)
            in->a[i][j] = RandomFloat();
    for (int i = 0; i < 8; i++)
        in->s[i] = RandomFloat();
    for (int i = 0; i < 4; i++)
        in->u[i] = Next();
    if (kind != ANY && (Next() % 4) != 0) {         // mostly well-formed, some raw
        for (int i = 0; i < 4; i++) {
            if (kind == QUATS) {
                for (int j = 0; j < 64; j += 4)
                    UnitQuat(in->a[i] + j);
            } else {
                for (int j = 0; j < 64; j += 16)
                    RotationMatrix(in->a[i] + j);
            }
        }
    }
}

typedef uint64_t (*CallFn)(Inputs *in, float *out, bool original);

struct Result {
    int runs, mismatches, nanOnly;
};

static int g_failedFunctions;

static bool BothNaN(float a, float b) { return a != a && b != b; }

static void Test(const char *name, const unsigned *originals, int count, Kind kind, int runs, CallFn call) {
    static Inputs inO, inP;
    alignas(16) static float outO[64], outP[64];
    Result r = { 0, 0, 0 };
    for (int i = 0; i < runs; i++) {
        Generate(&inO, kind);
        inP = inO;
        for (int j = 0; j < 64; j++)
            outO[j] = outP[j] = Bits(0x5a5a0000u | (uint32_t)j);
        for (int j = 0; j < count; j++)
            XbeOriginal_Restore(originals[j], true);
        uint64_t retO = call(&inO, outO, true);
        for (int j = 0; j < count; j++)
            XbeOriginal_Restore(originals[j], false);
        uint64_t retP = call(&inP, outP, false);
        r.runs++;
        bool same = retO == retP && memcmp(outO, outP, sizeof(outO)) == 0 && memcmp(&inO, &inP, sizeof(inO)) == 0;
        if (same)
            continue;
        bool nanOnly = true;
        for (int j = 0; j < 64 && nanOnly; j++)
            if (U(outO[j]) != U(outP[j]) && !BothNaN(outO[j], outP[j]))
                nanOnly = false;
        for (int k = 0; k < 4 && nanOnly; k++)
            for (int j = 0; j < 64 && nanOnly; j++)
                if (U(inO.a[k][j]) != U(inP.a[k][j]) && !BothNaN(inO.a[k][j], inP.a[k][j]))
                    nanOnly = false;
        if (retO != retP) {
            double dO, dP;
            memcpy(&dO, &retO, 8);
            memcpy(&dP, &retP, 8);
            if (!(dO != dO && dP != dP))
                nanOnly = false;
        }
        if (nanOnly) {
            r.nanOnly++;
            continue;
        }
        if (r.mismatches++ < 3) {
            printf("[mathshadow] %s run %d: ret %016llx / %016llx\n", name, i, (unsigned long long)retO,
                   (unsigned long long)retP);
            for (int j = 0; j < 64; j++)
                if (U(outO[j]) != U(outP[j]))
                    printf("[mathshadow]   out[%d] %08x / %08x\n", j, U(outO[j]), U(outP[j]));
            for (int k = 0; k < 4; k++)
                for (int j = 0; j < 64; j++)
                    if (U(inO.a[k][j]) != U(inP.a[k][j]))
                        printf("[mathshadow]   in[%d][%d] %08x / %08x\n", k, j, U(inO.a[k][j]), U(inP.a[k][j]));
            printf("[mathshadow]   inputs: a0 %08x %08x %08x %08x  a1 %08x %08x %08x %08x  s %08x %08x\n",
                   U(inP.a[0][0]), U(inP.a[0][1]), U(inP.a[0][2]), U(inP.a[0][3]), U(inP.a[1][0]), U(inP.a[1][1]),
                   U(inP.a[1][2]), U(inP.a[1][3]), U(inP.s[0]), U(inP.s[1]));
        }
    }
    if (r.mismatches != 0)
        g_failedFunctions++;
    printf("[mathshadow] %-28s %6d runs: %s", name, r.runs, r.mismatches ? "MISMATCH" : "same");
    if (r.mismatches)
        printf(" (%d)", r.mismatches);
    if (r.nanOnly)
        printf(", %d NaN-only differences", r.nanOnly);
    printf("\n");
}

static uint64_t D(double d) {
    uint64_t u;
    memcpy(&u, &d, 8);
    return u;
}

// Each case calls the original at its address (typed) or the port, on the inputs; the macro gives the address.
#define ORIG(type, address) ((type)(address))
#define CASE(name, kind, runs, address, ...)                                                                       \
    do {                                                                                                           \
        static const unsigned addrs[] = { address };                                                               \
        struct C {                                                                                                 \
            static uint64_t call(Inputs *in, float *out, bool original) {                                          \
                (void)in; (void)out; (void)original;                                                               \
                __VA_ARGS__                                                                                        \
            }                                                                                                      \
        };                                                                                                         \
        Test(name, addrs, 1, kind, runs, C::call);                                                                 \
    } while (0)

typedef float (*FloatF)(float);
typedef float (*FloatFF)(float, float);
typedef double (*DoubleV)(const void *);
typedef double (*DoubleVV)(const void *, const void *);
typedef float (*FloatV)(const void *);
typedef float (*FloatVV)(const void *, const void *);
typedef void (*VoidV)(void *);
typedef void (*VoidVV)(const void *, void *);
typedef void (*VoidVVV)(const void *, const void *, void *);
typedef void (*VoidVFV)(const void *, float, void *);
typedef void (*VoidVVFV)(const void *, const void *, float, void *);
typedef void (*VoidVFVV)(const void *, float, const void *, void *);
typedef void (*VoidVVVV)(const void *, const void *, const void *, void *);
typedef void (*VoidVF)(void *, float);
typedef void (*VoidVFFF)(void *, float, float, float);
typedef void (*VoidVFFFF)(void *, float, float, float, float);
typedef void (*VoidIVVV)(int, const void *, const void *, void *);
typedef void (*VoidIVFV)(int, const void *, float, void *);
typedef void (*VoidVVVI)(const void *, const void *, void *, int);
typedef void (*VoidVVVF)(const void *, const void *, void *, float);
typedef void (*VoidUVVVV)(uint32_t, const void *, const void *, const void *, void *);
typedef bool (*BoolVVVV)(const void *, const void *, const void *, void *);
typedef uint32_t (*U32V)(void);
typedef void (*VoidU)(uint32_t);
typedef double (*DoubleVoid)(void);

// The random generator's state, at the original's address: both sides must start from the same.
#define RandomState ((uint32_t *)0x001d187cu)

static unsigned short ControlWord() {
    unsigned short controlWord = 0;
    __asm {
        fnstcw controlWord
    }
    return controlWord;
}

void MathShadow_Run(void) {
    if (getenv("NIGHTFIRE_MATHSHADOW") == NULL)
        return;
    unsigned short cw = ControlWord();
    unsigned mxcsr = _mm_getcsr();
    printf("[mathshadow] x87 control word %04x (expect 027f), MXCSR controls %08x (expect 00001f80)\n", cw, mxcsr & ~0x3fu);
    const int N = 20000;

    // ---- trig in turns: the same instructions
    CASE("sin_fractionalangle", ANY, N, 0x00108790, {
        return D(original ? ORIG(FloatF, 0x00108790)(in->s[0]) : sin_fractionalangle(in->s[0]));
    });
    CASE("cos_fractionalangle", ANY, N, 0x001087a0, {
        return D(original ? ORIG(FloatF, 0x001087a0)(in->s[0]) : cos_fractionalangle(in->s[0]));
    });
    CASE("sincos_fractionalangle", ANY, N, 0x001087b0, {
        typedef void (*Fn)(const float *, float *, float *);
        (original ? ORIG(Fn, 0x001087b0) : sincos_fractionalangle)(in->s, out, out + 1);
        return 0;
    });
    CASE("tan_fractionalangle", ANY, N, 0x001087e0, {
        return D(original ? ORIG(FloatF, 0x001087e0)(in->s[0]) : tan_fractionalangle(in->s[0]));
    });
    CASE("atan_turns", ANY, N, 0x001087f0, {
        return D(original ? ORIG(FloatFF, 0x001087f0)(in->s[0], in->s[1]) : atan_turns(in->s[0], in->s[1]));
    });

    // ---- EA's portable x87 routines
    CASE("v3add_x87", ANY, N, 0x0010a460, {
        typedef void (*Fn)(const float *, const float *, float *);
        (original ? ORIG(Fn, 0x0010a460) : v3add_x87)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("v3sub_x87", ANY, N, 0x0010a48d, {
        typedef const float *(*Fn)(const float *, const float *, float *);
        const float *next = (original ? ORIG(Fn, 0x0010a48d) : v3sub_x87)(in->a[0], in->a[1], out);
        return (uint64_t)(next - in->a[0]);
    });
    CASE("v3dot_x87", ANY, N, 0x0010a4ba, {
        typedef double (*Fn)(const float *, const float *);
        return D((original ? ORIG(Fn, 0x0010a4ba) : v3dot_x87)(in->a[0], in->a[1]));
    });
    CASE("v3crossprod_x87", ANY, N, 0x0010a4de, {
        typedef void (*Fn)(const float *, const float *, float *);
        (original ? ORIG(Fn, 0x0010a4de) : v3crossprod_x87)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("v3length", ANY, N, 0x00108810, { return D((original ? ORIG(DoubleV, 0x00108810) : v3length)(in->a[0])); });
    CASE("v3unit", ANY, N, 0x001089b0, {
        (original ? ORIG(VoidVV, 0x001089b0) : v3unit)(in->a[0], out);
        return 0;
    });
    CASE("v3unit in place", ANY, N, 0x001089b0, {
        (original ? ORIG(VoidVV, 0x001089b0) : v3unit)(in->a[0], in->a[0]);
        return 0;
    });
    CASE("VEC3_Dot", ANY, N, 0x001089e0, {
        return D((original ? ORIG(DoubleVV, 0x001089e0) : VEC3_Dot)(in->a[0], in->a[1]));
    });
    CASE("v3crossprod", ANY, N, 0x001089a0, {
        (original ? ORIG(VoidVVV, 0x001089a0) : v3crossprod)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("v3unitcrossprod", ANY, N, 0x00108a00, {
        (original ? ORIG(VoidVVV, 0x00108a00) : v3unitcrossprod)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("v3sub", ANY, N, 0x00108b50, {
        int n = 1 + (int)(in->u[0] % 3);
        (original ? ORIG(VoidIVVV, 0x00108b50) : v3sub)(n, in->a[0], in->a[1], out);
        return 0;
    });
    CASE("v3scale", ANY, N, 0x00108b80, {
        int n = 1 + (int)(in->u[0] % 3);
        (original ? ORIG(VoidIVFV, 0x00108b80) : v3scale)(n, in->a[0], in->s[0], out);
        return 0;
    });
    CASE("v3distance", ANY, N, 0x00108bc0, {
        return D((original ? ORIG(DoubleVV, 0x00108bc0) : v3distance)(in->a[0], in->a[1]));
    });
    CASE("MATRIX4_multxlate", MATRICES, N, 0x00108840, {
        (original ? ORIG(VoidVVV, 0x00108840) : MATRIX4_multxlate)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("MATRIX4_multscale", MATRICES, N, 0x00108900, {
        (original ? ORIG(VoidVVV, 0x00108900) : MATRIX4_multscale)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("MATRIX4_mult", MATRICES, N, 0x00108c00, {
        (original ? ORIG(VoidVVV, 0x00108c00) : MATRIX4_mult)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("MATRIX4_mult out=a", MATRICES, N, 0x00108c00, {
        (original ? ORIG(VoidVVV, 0x00108c00) : MATRIX4_mult)(in->a[0], in->a[1], in->a[0]);
        return 0;
    });
    CASE("MATRIX4_mult out=b", MATRICES, N, 0x00108c00, {
        (original ? ORIG(VoidVVV, 0x00108c00) : MATRIX4_mult)(in->a[0], in->a[1], in->a[1]);
        return 0;
    });
    CASE("MATRIX4_vect3mult", MATRICES, N, 0x00108ce0, {
        (original ? ORIG(VoidVVV, 0x00108ce0) : MATRIX4_vect3mult)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("MATRIX4_vect3mult in place", MATRICES, N, 0x00108ce0, {
        (original ? ORIG(VoidVVV, 0x00108ce0) : MATRIX4_vect3mult)(in->a[0], in->a[1], in->a[0]);
        return 0;
    });
    CASE("MATRIX4_vect4mult", MATRICES, N, 0x00108d50, {
        (original ? ORIG(VoidVVV, 0x00108d50) : MATRIX4_vect4mult)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("MATRIX4_axisrotate", ANY, N, 0x00108e70, {
        typedef void (*Fn)(const void *, float, void *);
        (original ? ORIG(Fn, 0x00108e70) : MATRIX4_axisrotate)(in->a[0], in->s[0], out);
        return 0;
    });
    CASE("random", ANY, N, 0x00108a50, {
        memcpy(RandomState, in->u, 16);
        RandomState[4] = in->u[0] ^ 0x9e3779b9;
        RandomState[5] = (in->u[1] & 1) ? 0xffffffffu : in->u[2];
        uint32_t r = (original ? ORIG(U32V, 0x00108a50) : REAL_random)();
        memcpy(out, RandomState, 24);
        return r;
    });
    CASE("seedrandom", ANY, N, 0x00108b00, {
        (original ? ORIG(VoidU, 0x00108b00) : seedrandom)(in->u[0]);
        memcpy(out, RandomState, 24);
        return 0;
    });
    CASE("rrandom", ANY, N, 0x00114a60, {
        memcpy(RandomState, in->u, 16);
        RandomState[4] = in->u[0] ^ 0x9e3779b9;
        RandomState[5] = in->u[2];
        double r = (original ? ORIG(DoubleVoid, 0x00114a60) : rrandom)();
        memcpy(out, RandomState, 24);
        return D(r);
    });

    // ---- the VU0 layer
    CASE("VU0_v3add", ANY, N, 0x00115ab0, {
        (original ? ORIG(VoidVVV, 0x00115ab0) : VU0_v3add)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4sub", ANY, N, 0x00115ad0, {
        (original ? ORIG(VoidVVV, 0x00115ad0) : VU0_v4sub)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4add4", ANY, N, 0x00115af0, {
        (original ? ORIG(VoidVVV, 0x00115af0) : VU0_v4add4)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4sub4", ANY, N, 0x00115b10, {
        (original ? ORIG(VoidVVV, 0x00115b10) : VU0_v4sub4)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("v3dotprod", ANY, N, 0x00115b30, {
        return D((original ? ORIG(FloatVV, 0x00115b30) : v3dotprod)(in->a[0], in->a[1]));
    });
    CASE("v4dotprod", ANY, N, 0x00115b60, {
        return D((original ? ORIG(FloatVV, 0x00115b60) : v4dotprod)(in->a[0], in->a[1]));
    });
    CASE("VU0_sqrt", ANY, N, 0x00115ba0, { return D((original ? ORIG(FloatF, 0x00115ba0) : VU0_sqrt)(in->s[0])); });
    CASE("VU0_rsqrt", ANY, N, 0x00115bc0, { return D((original ? ORIG(FloatF, 0x00115bc0) : VU0_rsqrt)(in->s[0])); });
    CASE("VU0_v3length", ANY, N, 0x00115bf0, { return D((original ? ORIG(FloatV, 0x00115bf0) : VU0_v3length)(in->a[0])); });
    CASE("VU0_v3lengthsquare", ANY, N, 0x00115c20, {
        return D((original ? ORIG(FloatV, 0x00115c20) : VU0_v3lengthsquare)(in->a[0]));
    });
    CASE("VU0_v4lengthsquare", ANY, N, 0x00115c50, {
        return D((original ? ORIG(FloatV, 0x00115c50) : VU0_v4lengthsquare)(in->a[0]));
    });
    CASE("VU0_v4copy", ANY, N, 0x00115c80, {
        (original ? ORIG(VoidVV, 0x00115c80) : VU0_v4copy)(in->a[0], out);
        return 0;
    });
    CASE("MatrixCopy", ANY, N, 0x00115c90, {
        (original ? ORIG(VoidVV, 0x00115c90) : MatrixCopy)(in->a[0], out);
        return 0;
    });
    CASE("VU0_v4crossprodxyz", ANY, N, 0x00115cc0, {
        (original ? ORIG(VoidVVV, 0x00115cc0) : VU0_v4crossprodxyz)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4crossprod1", ANY, N, 0x00115d00, {
        (original ? ORIG(VoidVVV, 0x00115d00) : VU0_v4crossprod1)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4unitcrossprodxyz", ANY, N, 0x00115d40, {
        (original ? ORIG(VoidVVV, 0x00115d40) : VU0_v4unitcrossprodxyz)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4unitcrossprod1", ANY, N, 0x00115db0, {
        (original ? ORIG(VoidVVV, 0x00115db0) : VU0_v4unitcrossprod1)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4unitxyz", ANY, N, 0x00115e20, {
        (original ? ORIG(VoidVV, 0x00115e20) : VU0_v4unitxyz)(in->a[0], out);
        return 0;
    });
    CASE("VU0_v4unitxyz in place", ANY, N, 0x00115e20, {
        (original ? ORIG(VoidVV, 0x00115e20) : VU0_v4unitxyz)(in->a[0], in->a[0]);
        return 0;
    });
    CASE("VU0_v4unit", ANY, N, 0x00115e70, {
        (original ? ORIG(VoidVV, 0x00115e70) : VU0_v4unit)(in->a[0], out);
        return 0;
    });
    CASE("VU0_v4mult", ANY, N, 0x00115ec0, {
        (original ? ORIG(VoidVVV, 0x00115ec0) : VU0_v4mult)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4multxyz", ANY, N, 0x00115ee0, {
        (original ? ORIG(VoidVVV, 0x00115ee0) : VU0_v4multxyz)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4scale", ANY, N, 0x00115f00, {
        (original ? ORIG(VoidVFV, 0x00115f00) : VU0_v4scale)(in->a[0], in->s[0], out);
        return 0;
    });
    CASE("VU0_v4scale4", ANY, N, 0x00115f30, {
        (original ? ORIG(VoidVFV, 0x00115f30) : VU0_v4scale4)(in->a[0], in->s[0], out);
        return 0;
    });
    CASE("VU0_v4addscale", ANY, N, 0x00115f50, {
        (original ? ORIG(VoidVVFV, 0x00115f50) : VU0_v4addscale)(in->a[0], in->a[1], in->s[0], out);
        return 0;
    });
    CASE("VU0_v4scaleadd", ANY, N, 0x00115f80, {
        (original ? ORIG(VoidVFVV, 0x00115f80) : VU0_v4scaleadd)(in->a[0], in->s[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4scaleadd4", ANY, N, 0x00115fb0, {
        (original ? ORIG(VoidVFVV, 0x00115fb0) : VU0_v4scaleadd4)(in->a[0], in->s[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v3distancesquare", ANY, N, 0x00115fe0, {
        return D((original ? ORIG(FloatVV, 0x00115fe0) : VU0_v3distancesquare)(in->a[0], in->a[1]));
    });
    CASE("vec3distance", ANY, N, 0x00116020, {
        return D((original ? ORIG(FloatVV, 0x00116020) : vec3distance)(in->a[0], in->a[1]));
    });
    CASE("VU0_v3lengthxz", ANY, N, 0x00116060, {
        return D((original ? ORIG(FloatV, 0x00116060) : VU0_v3lengthxz)(in->a[0]));
    });
    CASE("VU0_v3distancexz", ANY, N, 0x00116090, {
        return D((original ? ORIG(FloatVV, 0x00116090) : VU0_v3distancexz)(in->a[0], in->a[1]));
    });
    CASE("VU0_v3distancesquarexz", ANY, N, 0x001160c0, {
        return D((original ? ORIG(FloatVV, 0x001160c0) : VU0_v3distancesquarexz)(in->a[0], in->a[1]));
    });
    CASE("VU0_v3negate", ANY, N, 0x001160f0, {
        (original ? ORIG(VoidV, 0x001160f0) : VU0_v3negate)(in->a[0]);
        return 0;
    });
    CASE("VU0_MATRIX4_vect3mult", MATRICES, N, 0x00116240, {
        (original ? ORIG(VoidVVV, 0x00116240) : VU0_MATRIX4_vect3mult)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_MATRIX4_vect4mult", MATRICES, N, 0x00116280, {
        (original ? ORIG(VoidVVV, 0x00116280) : VU0_MATRIX4_vect4mult)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_MATRIX4_vect3rotate", MATRICES, N, 0x001162d0, {
        (original ? ORIG(VoidVVV, 0x001162d0) : VU0_MATRIX4_vect3rotate)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_MATRIX4_vect4multarray", MATRICES, N, 0x00116310, {
        int n = 1 + (int)(in->u[0] % 3);
        (original ? ORIG(VoidVVVI, 0x00116310) : VU0_MATRIX4_vect4multarray)(in->a[0], in->a[1], out, n);
        return 0;
    });
    CASE("VU0_MATRIX4_vect3multsub", MATRICES, N, 0x001163f0, {
        (original ? ORIG(VoidVVV, 0x001163f0) : VU0_MATRIX4_vect3multsub)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_fastqslerp", QUATS, N, 0x00116630, {
        (original ? ORIG(VoidVVVF, 0x00116630) : VU0_fastqslerp)(in->a[0], in->a[1], out, in->s[0]);
        return 0;
    });
    CASE("VU0_v4quatrotate", QUATS, N, 0x001167f0, {
        (original ? ORIG(VoidVVV, 0x001167f0) : VU0_v4quatrotate)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_v4quatrotate_xlate", QUATS, N, 0x001169a0, {
        (original ? ORIG(VoidVVVV, 0x001169a0) : VU0_v4quatrotate_xlate)(in->a[0], in->a[1], in->a[2], out);
        return 0;
    });

    // ---- builders and extractors
    CASE("VU0_MATRIX4Init", ANY, 100, 0x00116120, {
        (original ? ORIG(VoidV, 0x00116120) : VU0_MATRIX4Init)(out);
        return 0;
    });
    CASE("VU0_v4Init", ANY, 100, 0x00116210, {
        (original ? ORIG(VoidV, 0x00116210) : VU0_v4Init)(out);
        return 0;
    });
    CASE("BuildScale", ANY, N, 0x00116190, {
        (original ? ORIG(VoidVFFF, 0x00116190) : BuildScale)(out, in->s[0], in->s[1], in->s[2]);
        return 0;
    });
    CASE("BuildScaleUniform", ANY, N, 0x00114c00, {
        (original ? ORIG(VoidVF, 0x00114c00) : BuildScaleUniform)(out, in->s[0]);
        return 0;
    });
    CASE("BuildScaleXYZ", ANY, N, 0x00114c20, {
        (original ? ORIG(VoidVFFF, 0x00114c20) : BuildScaleXYZ)(out, in->s[0], in->s[1], in->s[2]);
        return 0;
    });
    CASE("BuildTranslate", ANY, N, 0x00114c30, {
        (original ? ORIG(VoidVFFF, 0x00114c30) : BuildTranslate)(out, in->s[0], in->s[1], in->s[2]);
        return 0;
    });
    CASE("BuildRotate", ANY, N, 0x00114c60, {
        (original ? ORIG(VoidVFFFF, 0x00114c60) : BuildRotate)(out, in->s[0], in->s[1], in->s[2], in->s[3]);
        return 0;
    });
    CASE("ExtractRotTrans", MATRICES, N, 0x00114dd0, {
        typedef void (*Fn)(const void *, float *, void *);
        (original ? ORIG(Fn, 0x00114dd0) : ExtractRotTrans)(in->a[0], out, out + 16);
        return 0;
    });
    CASE("ExtractQuatTrans", MATRICES, N, 0x001155c0, {
        typedef void (*Fn)(const void *, void *, void *);
        (original ? ORIG(Fn, 0x001155c0) : ExtractQuatTrans)(in->a[0], out, out + 16);
        return 0;
    });
    CASE("MATRIX4_TransformPoint", MATRICES, N, 0x00114e20, {
        (original ? ORIG(VoidVVV, 0x00114e20) : MATRIX4_TransformPoint)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("TransformPoint", MATRICES, N, 0x00114e40, {
        (original ? ORIG(VoidVVV, 0x00114e40) : TransformPoint)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("MATRIX4_RotateVector", MATRICES, N, 0x00114e60, {
        (original ? ORIG(VoidVVV, 0x00114e60) : MATRIX4_RotateVector)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("OrthoInverse", MATRICES, N, 0x00114e80, {
        (original ? ORIG(VoidV, 0x00114e80) : OrthoInverse)(in->a[0]);
        return 0;
    });
    CASE("VU0_ExtractXAxis3FromQuat", QUATS, N, 0x001158f0, {
        (original ? ORIG(VoidVV, 0x001158f0) : VU0_ExtractXAxis3FromQuat)(in->a[0], out);
        return 0;
    });
    CASE("VU0_ExtractZAxis3FromQuat", QUATS, N, 0x00115950, {
        (original ? ORIG(VoidVV, 0x00115950) : VU0_ExtractZAxis3FromQuat)(in->a[0], out);
        return 0;
    });
    CASE("VU0_SQTquattom4", QUATS, N, 0x001159a0, {
        (original ? ORIG(VoidVVVV, 0x001159a0) : VU0_SQTquattom4)(in->a[0], in->a[1], in->a[2], out);
        return 0;
    });
    CASE("VU0_v4tocolour", ANY, N, 0x00116370, {
        typedef void (*Fn)(const void *, uint32_t *);
        (original ? ORIG(Fn, 0x00116370) : VU0_v4tocolour)(in->a[0], (uint32_t *)out);
        return 0;
    });
    CASE("VU0_MATRIX3x4_mult", MATRICES, N, 0x00116440, {
        (original ? ORIG(VoidVVV, 0x00116440) : VU0_MATRIX3x4_mult)(in->a[0], in->a[1], out);
        return 0;
    });
    CASE("VU0_MATRIX3x4_mult out=a", MATRICES, N, 0x00116440, {
        (original ? ORIG(VoidVVV, 0x00116440) : VU0_MATRIX3x4_mult)(in->a[0], in->a[1], in->a[0]);
        return 0;
    });
    CASE("VU0_MATRIX4_3x3transpose", MATRICES, N, 0x00116b70, {
        (original ? ORIG(VoidVV, 0x00116b70) : VU0_MATRIX4_3x3transpose)(in->a[0], out);
        return 0;
    });
    CASE("VU0_MATRIX4_3x3transpose in place", MATRICES, N, 0x00116b70, {
        (original ? ORIG(VoidVV, 0x00116b70) : VU0_MATRIX4_3x3transpose)(in->a[0], in->a[0]);
        return 0;
    });
    CASE("VU0_MATRIX4setxrot", ANY, N, 0x00116c20, {
        (original ? ORIG(VoidVF, 0x00116c20) : VU0_MATRIX4setxrot)(out, in->s[0]);
        return 0;
    });
    CASE("VU0_MATRIX4setyrot", ANY, N, 0x00116c70, {
        (original ? ORIG(VoidVF, 0x00116c70) : VU0_MATRIX4setyrot)(out, in->s[0]);
        return 0;
    });
    CASE("VU0_MATRIX4setzrot", ANY, N, 0x00116cc0, {
        (original ? ORIG(VoidVF, 0x00116cc0) : VU0_MATRIX4setzrot)(out, in->s[0]);
        return 0;
    });
    CASE("BytesToCoordXYZ", ANY, N, 0x00114a80, {
        const void *add = (in->u[1] & 1) ? in->a[0] : NULL, *mul = (in->u[1] & 2) ? in->a[1] : NULL;
        const void *add2 = (in->u[1] & 4) ? in->a[2] : NULL;
        (original ? ORIG(VoidUVVVV, 0x00114a80) : BytesToCoordXYZ)(in->u[0], add, mul, add2, out);
        return 0;
    });
    CASE("BytesToCoordXYZW", ANY, N, 0x00114b30, {
        const void *add = (in->u[1] & 1) ? in->a[0] : NULL, *mul = (in->u[1] & 2) ? in->a[1] : NULL;
        const void *add2 = (in->u[1] & 4) ? in->a[2] : NULL;
        (original ? ORIG(VoidUVVVV, 0x00114b30) : BytesToCoordXYZW)(in->u[0], add, mul, add2, out);
        return 0;
    });
    CASE("FindOBBIntersect", ANY, N, 0x00115600, {
        return (original ? ORIG(BoolVVVV, 0x00115600) : FindOBBIntersect)(in->a[0], in->a[1], in->a[2], out) ? 1 : 0;
    });

    // ---- determinant, inverse, Euler angles
    CASE("Determinant4x4", MATRICES, N, 0x00114f00, {
        return D((original ? ORIG(DoubleV, 0x00114f00) : Determinant4x4)(in->a[0]));
    });
    CASE("Inverse", MATRICES, N, 0x00114fe0, {
        typedef double (*Fn)(void *);
        return D((original ? ORIG(Fn, 0x00114fe0) : Inverse)(in->a[0]));
    });
    CASE("VU0_EulerToQuat", ANY, N, 0x00116d10, {
        (original ? ORIG(VoidVV, 0x00116d10) : VU0_EulerToQuat)(in->a[0], out);
        return 0;
    });

    // ---- D3DX
    typedef void *(__stdcall *SV3)(void *, const void *, const void *);
    typedef void *(__stdcall *SV2)(void *, const void *);
    typedef void *(__stdcall *SVFFFF)(void *, float, float, float, float);
    typedef void *(__stdcall *SVFFFFFF)(void *, float, float, float, float, float, float);
    typedef void *(__stdcall *SProject)(void *, const void *, const void *, const void *, const void *, const void *);
    CASE("D3DXMatrixMultiply", MATRICES, N, 0x001134ba, {
        void *r = (original ? ORIG(SV3, 0x001134ba) : VU0_MATRIX4_mult)(out, in->a[0], in->a[1]);
        return (uint64_t)((char *)r - (char *)out);
    });
    CASE("D3DXMatrixMultiply out=a", MATRICES, N, 0x001134ba, {
        (original ? ORIG(SV3, 0x001134ba) : VU0_MATRIX4_mult)(in->a[0], in->a[0], in->a[1]);
        return 0;
    });
    CASE("D3DXMatrixTranspose", MATRICES, N, 0x001135b8, {
        void *r = (original ? ORIG(SV2, 0x001135b8) : VU0_MATRIX4_transpose)(out, in->a[0]);
        return (uint64_t)((char *)r - (char *)out);
    });
    CASE("D3DXMatrixRotationQuaternion", QUATS, N, 0x00113603, {
        void *r = (original ? ORIG(SV2, 0x00113603) : VU0_quattom4)(out, in->a[0]);
        return (uint64_t)((char *)r - (char *)out);
    });
    CASE("D3DXQuaternionRotationMatrix", MATRICES, N, 0x0011388d, {
        void *r = (original ? ORIG(SV2, 0x0011388d) : VU0_m4toquat)(out, in->a[0]);
        return (uint64_t)((char *)r - (char *)out);
    });
    CASE("D3DXQuaternionMultiply", QUATS, N, 0x001139c1, {
        void *r = (original ? ORIG(SV3, 0x001139c1) : D3DXQuaternionMultiply)(out, in->a[0], in->a[1]);
        return (uint64_t)((char *)r - (char *)out);
    });
    CASE("D3DXMatrixPerspectiveFovRH", ANY, N, 0x001136e5, {
        (original ? ORIG(SVFFFF, 0x001136e5) : D3DXMatrixPerspectiveFovRH)(out, in->s[0], in->s[1], in->s[2], in->s[3]);
        return 0;
    });
    CASE("D3DXMatrixOrthoRH", ANY, N, 0x00113779, {
        (original ? ORIG(SVFFFF, 0x00113779) : D3DXMatrixOrthoRH)(out, in->s[0], in->s[1], in->s[2], in->s[3]);
        return 0;
    });
    CASE("D3DXMatrixOrthoOffCenterRH", ANY, N, 0x001137eb, {
        (original ? ORIG(SVFFFFFF, 0x001137eb) : D3DXMatrixOrthoOffCenterRH)(out, in->s[0], in->s[1], in->s[2],
                                                                               in->s[3], in->s[4], in->s[5]);
        return 0;
    });
    CASE("D3DXVec3Project", MATRICES, N, 0x00113387, {
        uint32_t *vp = in->u;   // X, Y, Width, Height from the random words; MinZ, MaxZ from the scalars
        static uint32_t viewport[6];
        viewport[0] = vp[0] & 0xffff; viewport[1] = vp[1] & 0xffff;
        viewport[2] = (vp[2] & 1) ? vp[2] : (vp[2] & 0x7ff); viewport[3] = vp[3] & 0x7ff;
        memcpy(viewport + 4, in->s, 8);
        unsigned pick = in->u[0] >> 28;
        const void *proj = (pick & 1) ? in->a[1] : NULL, *view = (pick & 2) ? in->a[2] : NULL;
        const void *world = (pick & 4) ? in->a[3] : NULL;
        void *r = (original ? ORIG(SProject, 0x00113387) : D3DXVec3Project)(out, in->a[0], (pick & 8) ? viewport : NULL,
                                                                           proj, view, world);
        return (uint64_t)((char *)r - (char *)out);
    });

    printf("[mathshadow] %s\n", g_failedFunctions == 0 ? "every function the same as the original"
                                                       : "FAILED: some functions differ");
    fflush(stdout);
}
