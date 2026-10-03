#include "RealMath.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EA's portable maths routines as the driving engine has them - trig in turns, the v3* and MATRIX4_* functions,
// the random number generator - and the Xbox builders and extractors beside them. docs/driving/maths.md describes
// each original; every function here is the original at the same address, ported from it, bit for bit.
//
// How that is done (docs/driving/maths.md section 3): the originals compute on the x87 at 53-bit precision, which
// is exactly C++ double, so the arithmetic is done in double in the original's order and rounded to float exactly
// where the original stores a float. A value the original leaves on the x87 stack comes back as a double. FSIN,
// FCOS, FPTAN and FPATAN keep 64 bits whatever the precision, which nothing in C++ can, so the trig functions are
// the original instructions; callers that only store their results call them as float functions.
// devtools/MathShadow.cpp compares every function here with the original.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

typedef float F;
#define V(p) ((const float *)(p))
#define W(p) ((float *)(p))

static const float kTwoPi = 6.28318548f;       // 0x40c90fdb
static const float kOneOverTwoPi = 0.159154937f; // 0x3e22f983

// ---- trig in turns: the original instructions

// FUNC_AT(0x00108790)
__declspec(naked) float sin_fractionalangle(float turns) {
    __asm {
        fld dword ptr [kTwoPi]
        fmul dword ptr [esp + 4]
        fsin
        ret
    }
}

// FUNC_AT(0x001087a0)
__declspec(naked) float cos_fractionalangle(float turns) {
    __asm {
        fld dword ptr [kTwoPi]
        fmul dword ptr [esp + 4]
        fcos
        ret
    }
}

// FUNC_AT(0x001087b0)
__declspec(naked) void sincos_fractionalangle(const float *turns, float *sinOut, float *cosOut) {
    __asm {
        mov eax, dword ptr [esp + 4]
        fld dword ptr [eax]
        mov ecx, dword ptr [esp + 8]
        fmul dword ptr [kTwoPi]
        mov edx, dword ptr [esp + 0xc]
        fsin
        fstp dword ptr [ecx]
        fld dword ptr [eax]
        fmul dword ptr [kTwoPi]
        fcos
        fstp dword ptr [edx]
        ret
    }
}

// FUNC_AT(0x001087e0)
__declspec(naked) float tan_fractionalangle(float turns) {
    __asm {
        fld dword ptr [kTwoPi]
        fmul dword ptr [esp + 4]
        fptan
        fstp st(0)
        ret
    }
}

// atan2(y, x) in turns.
// AUTOINJECT
__declspec(naked) float atan_turns(float y, float x) {
    __asm {
        fld dword ptr [esp + 4]
        fld dword ptr [esp + 8]
        fpatan
        fmul dword ptr [kOneOverTwoPi]
        ret
    }
}

// ---- the x87 helpers the v3 functions call (EBP-framed in the original)

// Nothing calls it.
// FUNC_AT(0x0010a460)
void v3add_x87(const float *a, const float *b, float *out) {   // 0x0010a460
    out[0] = (F)((double)a[0] + b[0]);
    out[1] = (F)((double)a[1] + b[1]);
    out[2] = (F)((double)a[2] + b[2]);
}

// Answers a + 3 (the original leaves it in EAX, and v3sub walks a with it).
// FUNC_AT(0x0010a48d)
const float *v3sub_x87(const float *a, const float *b, float *out) {
    float x = (F)((double)a[0] - b[0]), y = (F)((double)a[1] - b[1]), z = (F)((double)a[2] - b[2]);
    out[0] = x;
    out[1] = y;
    out[2] = z;
    return a + 3;
}

// FUNC_AT(0x0010a4ba)
double v3dot_x87(const float *a, const float *b) {
    return ((double)a[0] * b[0] + (double)a[1] * b[1]) + (double)a[2] * b[2];
}

// FUNC_AT(0x0010a4de)
void v3crossprod_x87(const float *a, const float *b, float *out) {
    double x = (double)a[1] * b[2] - (double)a[2] * b[1];
    double y = (double)a[2] * b[0] - (double)a[0] * b[2];
    double z = (double)a[0] * b[1] - (double)a[1] * b[0];
    out[0] = (F)x;
    out[1] = (F)y;
    out[2] = (F)z;
}

// ---- EA's portable vector and matrix routines

// The length, unrounded.
// AUTOINJECT
double v3length(const void *v) {
    const float *p = V(v);
    return sqrt(((double)p[0] * p[0] + (double)p[1] * p[1]) + (double)p[2] * p[2]);
}

// No check for zero length: a zero vector comes out NaN.
// AUTOINJECT
void v3unit(const void *in, void *out) {
    const float *p = V(in);
    double r = 1.0 / v3length(in);
    float x = (F)(r * p[0]), y = (F)(r * p[1]), z = (F)(r * p[2]);
    W(out)[0] = x;
    W(out)[1] = y;
    W(out)[2] = z;
}

// The dot product, unrounded.
// AUTOINJECT
double VEC3_Dot(const void *a, const void *b) {
    return v3dot_x87(V(a), V(b));
}

// FUNC_AT(0x001089a0)
void v3crossprod(const void *a, const void *b, void *out) {
    v3crossprod_x87(V(a), V(b), W(out));
}

// AUTOINJECT
void v3unitcrossprod(const void *a, const void *b, void *out) {
    float t[3];
    v3crossprod_x87(V(a), V(b), t);
    v3unit(t, out);
}

// count vectors; a and out advance, b does not (a moves on through what v3sub_x87 leaves in EAX). Every caller
// passes 1.
// AUTOINJECT
void v3sub(int count, const void *a, const void *b, void *out) {
    const float *p = V(a);
    float *o = W(out);
    for (int i = 0; i < count; i++, o += 3)
        p = v3sub_x87(p, V(b), o);
}

// AUTOINJECT
void v3scale(int count, const void *in, float scale, void *out) {
    const float *p = V(in);
    float *o = W(out);
    for (int i = 0; i < count; i++, p += 3, o += 3) {
        o[0] = (F)((double)p[0] * scale);
        o[1] = (F)((double)p[1] * scale);
        o[2] = (F)((double)p[2] * scale);
    }
}

// The distance from a to b, unrounded.
// AUTOINJECT
double v3distance(const void *a, const void *b) {
    float d[3];
    d[0] = (F)((double)V(b)[0] - V(a)[0]);
    d[1] = (F)((double)V(b)[1] - V(a)[1]);
    d[2] = (F)((double)V(b)[2] - V(a)[2]);
    return v3length(d);
}

// out[r][c] = m[r][c] + m[r][3] * v[c] for c < 3, out[r][3] = m[r][3] * v[3].
// AUTOINJECT
void MATRIX4_multxlate(const void *m, const void *v, void *out) {
    const float *a = V(m), *p = V(v);
    float *o = W(out);
    for (int r = 0; r < 4; r++) {
        float row[4];
        for (int c = 0; c < 3; c++)
            row[c] = (F)(a[4 * r + c] + (double)a[4 * r + 3] * p[c]);
        row[3] = (F)((double)a[4 * r + 3] * p[3]);
        memcpy(o + 4 * r, row, sizeof(row));
    }
}

// out[r][c] = m[r][c] * v[c].
// AUTOINJECT
void MATRIX4_multscale(const void *m, const void *v, void *out) {
    const float *a = V(m), *p = V(v);
    float *o = W(out);
    for (int r = 0; r < 4; r++) {
        float row[4];
        for (int c = 0; c < 4; c++)
            row[c] = (F)((double)a[4 * r + c] * p[c]);
        memcpy(o + 4 * r, row, sizeof(row));
    }
}

// A row through a matrix; the original takes EAX = row, ECX = matrix, EDX = out (0x00108d5c).
static void MATRIX4_rowmult(const float *row, const float *m, float *out) {
    float o[4];
    for (int c = 0; c < 4; c++)
        o[c] = (F)((((double)row[0] * m[c] + (double)row[1] * m[4 + c]) + (double)row[2] * m[8 + c]) +
                   (double)row[3] * m[12 + c]);
    memcpy(out, o, sizeof(o));
}

static float MatrixScratch[16];   // the original's 0x002420f0, for b == out

// AUTOINJECT
void MATRIX4_mult(const void *a, const void *b, void *out) {
    const float *m = V(b);
    if (b == out) {
        memcpy(MatrixScratch, b, sizeof(MatrixScratch));
        m = MatrixScratch;
    }
    for (int r = 0; r < 4; r++)
        MATRIX4_rowmult(V(a) + 4 * r, m, W(out) + 4 * r);
}

// out = v (x, y, z, 1) through m, three floats out.
// FUNC_AT(0x00108ce0)
void MATRIX4_vect3mult(const void *v, const void *m, void *out) {
    const float *p = V(v), *a = V(m);
    float o[3];
    for (int c = 0; c < 3; c++)
        o[c] = (F)((((double)p[0] * a[c] + (double)p[1] * a[4 + c]) + (double)p[2] * a[8 + c]) + a[12 + c]);
    memcpy(out, o, sizeof(o));
}

// FUNC_AT(0x00108d50)
void MATRIX4_vect4mult(const void *v, const void *m, void *out) {
    MATRIX4_rowmult(V(v), V(m), W(out));
}

// A rotation of turns about axis (normalised here).
// FUNC_AT(0x00108e70)
void MATRIX4_axisrotate(const void *axis, float turns, void *out) {
    float n[3], s, c;
    v3unit(axis, n);
    sincos_fractionalangle(&turns, &s, &c);
    float *m = W(out);
    double t = 1.0 - c;
    double A = n[0] * t, B = n[1] * t;
    float SX = (F)((double)s * n[0]), SY = (F)((double)s * n[1]);
    double SZ = (double)s * n[2];
    m[0] = (F)(n[0] * A + c);
    double AY = n[1] * A;
    m[1] = (F)(AY + SZ);
    double AZ = n[2] * A;
    float AZf = (F)AZ;
    m[2] = (F)(AZ - SY);
    m[4] = (F)(AY - SZ);
    m[5] = (F)(n[1] * B + c);
    double BZ = n[2] * B;
    m[6] = (F)(SX + BZ);
    m[8] = (F)(AZf + (double)SY);
    m[9] = (F)(BZ - SX);
    m[10] = (F)((n[2] * t) * n[2] + c);
    m[3] = m[7] = m[11] = m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

// ---- the random number generator: six words of state, an add-with-carry chain and a counter

// The state stays where the original kept it (0x001d187c, six words, initialised to seedrandom(0)).
#define S ((uint32_t *)0x001d187cu)

// FUNC_AT(0x00108a50)
uint32_t REAL_random() {
    uint32_t a = S[4] + S[5];
    uint32_t c = (a < S[5] || a < S[4]);
    S[4] = a;
    a = a + S[3] + c; c = a < S[3]; S[3] = a;   // the original's carry test, not a 64-bit add
    a = a + S[2] + c; c = a < S[2]; S[2] = a;
    a = a + S[1] + c; c = a < S[1]; S[1] = a;
    S[0] = S[0] + a + c;
    if (++S[5] == 0 && ++S[4] == 0 && ++S[3] == 0 && ++S[2] == 0 && ++S[1] == 0)
        ++S[0];
    return S[0];
}

// AUTOINJECT
void seedrandom(uint32_t seed) {
    S[0] = seed + 0xf22d0e56;
    S[1] = S[0] - 0x69fbe76d;
    S[2] = S[1] + 0x3df3b646;
    S[3] = S[2] + 0x40dde76d;
    S[4] = S[3] - 0x68cd851f;
    S[5] = S[4] + 0xd1a9fbe7;
}
#undef S

// In [0, 1), exact (unrounded).
// AUTOINJECT
double rrandom() {
    return (double)REAL_random() * (1.0 / 4294967296.0);
}

// ---- the Xbox builders and extractors

static inline float FloatBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// AUTOINJECT
void VU0_MATRIX4Init(void *m) {
    static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    memcpy(m, identity, sizeof(identity));
}

// (0, 0, 0, 1)
// AUTOINJECT
void VU0_v4Init(void *q) {
    static const float unit[4] = { 0, 0, 0, 1 };
    memcpy(q, unit, sizeof(unit));
}

// A scale matrix; the arguments are copied as they are.
// FUNC_AT(0x00116190)
void BuildScale(void *m, float sx, float sy, float sz) {
    float *o = W(m);
    memset(o, 0, 64);
    memcpy(o + 0, &sx, 4);
    memcpy(o + 5, &sy, 4);
    memcpy(o + 10, &sz, 4);
    o[15] = 1.0f;
}

// FUNC_AT(0x00114c00)
void BuildScaleUniform(void *m, float scale) {
    BuildScale(m, scale, scale, scale);
}

// FUNC_AT(0x00114c20)
void BuildScaleXYZ(void *m, float sx, float sy, float sz) {
    BuildScale(m, sx, sy, sz);
}

// AUTOINJECT
void BuildTranslate(void *m, float x, float y, float z) {
    VU0_MATRIX4Init(m);
    memcpy(W(m) + 12, &x, 4);
    memcpy(W(m) + 13, &y, 4);
    memcpy(W(m) + 14, &z, 4);
}

// A rotation of degrees about (x, y, z), normalised with the SSE reciprocal square root estimate. An angle of
// exactly zero is the identity; NaN goes on.
// AUTOINJECT
void BuildRotate(void *m, float degrees, float axisX, float axisY, float axisZ) {
    double turns = (double)FloatBits(0x3b360b61) * degrees;   // 1/360
    float a = (F)turns;
    if (turns == 0.0) {   // the unrounded product: a denormal angle rounds to 0 but still builds a matrix
        VU0_MATRIX4Init(m);
        return;
    }
    float s = sin_fractionalangle(a), c = cos_fractionalangle(a);
    float r = VU0_rsqrt((F)(((double)axisX * axisX + (double)axisY * axisY) + (double)axisZ * axisZ));
    double nx = (double)axisX * r;
    float ny = (F)((double)r * axisY), nz = (F)((double)r * axisZ);
    double t = 1.0 - c;
    double TX = t * nx;
    float TY = (F)(t * ny), TZ = (F)(t * nz);
    float SX = (F)((double)s * nx), SY = (F)((double)s * ny), SZ = (F)((double)s * nz);
    float *o = W(m);
    o[3] = o[7] = 0.0f;
    o[0] = (F)(TX * nx + c);
    o[1] = (F)(ny * TX + SZ);
    o[2] = (F)(TX * nz - SY);
    o[4] = (F)(TY * nx - SZ);
    o[5] = (F)((double)TY * ny + c);
    o[6] = (F)((double)TY * nz + SX);
    o[8] = (F)(TZ * nx + SY);
    o[9] = (F)((double)TZ * ny - SX);
    o[11] = o[12] = o[13] = o[14] = 0.0f;
    o[10] = (F)((double)TZ * nz + c);
    o[15] = 1.0f;
}

// The 3x3 (nine floats, stride three) and the translation, as integers.
// AUTOINJECT
void ExtractRotTrans(const void *m, float *rot, void *t) {
    const uint32_t *s = (const uint32_t *)m;
    uint32_t *r = (uint32_t *)rot, *o = (uint32_t *)t;
    for (int i = 0; i < 3; i++) {
        r[3 * i] = s[4 * i];
        r[3 * i + 1] = s[4 * i + 1];
        r[3 * i + 2] = s[4 * i + 2];
    }
    o[0] = s[12];
    o[1] = s[13];
    o[2] = s[14];
}

// A rotation (3x3, stride three) to a quaternion; the original takes ECX = rotation, EDX = quaternion (0x00115440).
static void QuatFromRot(const float *R, float *q) {
    double s45 = (double)R[4] + R[8];
    float tmp = (F)s45;
    double tr = s45 + R[0];
    if (tr > 0) {
        double s = sqrt(tr + 1.0);
        q[3] = (F)(0.5 * s);
        double k = 0.5 / s;
        q[0] = (F)(((double)R[5] - R[7]) * k);
        q[1] = (F)(((double)R[6] - R[2]) * k);
        q[2] = (F)(((double)R[1] - R[3]) * k);
        return;
    }
    int i = R[4] > R[0] ? 1 : 0;
    double s, k;
    if (R[8] > R[i == 1 ? 4 : 0]) {
        s = sqrt(((double)R[8] - ((double)R[4] + R[0])) + 1.0);
        q[2] = (F)(0.5 * s);
        k = (s == 0) ? s : 0.5 / s;
        q[3] = (F)(((double)R[1] - R[3]) * k);
        q[0] = (F)(((double)R[6] + R[2]) * k);
        q[1] = (F)(((double)R[7] + R[5]) * k);
    } else if (i == 1) {
        s = sqrt(((double)R[4] - ((double)R[0] + R[8])) + 1.0);
        q[1] = (F)(0.5 * s);
        k = (s == 0) ? s : 0.5 / s;
        q[3] = (F)(((double)R[6] - R[2]) * k);
        q[2] = (F)(((double)R[7] + R[5]) * k);
        q[0] = (F)(((double)R[3] + R[1]) * k);
    } else {
        s = sqrt(((double)R[0] - tmp) + 1.0);   // the float-rounded R[4] + R[8]
        q[0] = (F)(0.5 * s);
        k = (s == 0) ? s : 0.5 / s;
        q[3] = (F)(((double)R[5] - R[7]) * k);
        q[1] = (F)(((double)R[3] + R[1]) * k);
        q[2] = (F)(((double)R[6] + R[2]) * k);
    }
}

// The rotation as a quaternion and the translation, with t.w = 1.
// FUNC_AT(0x001155c0)
void ExtractQuatTrans(const void *m, void *q, void *t) {
    float rot[9];
    ExtractRotTrans(m, rot, t);
    QuatFromRot(rot, W(q));
    W(t)[3] = 1.0f;
}

// AUTOINJECT
void MATRIX4_TransformPoint(const void *m, const void *in, void *out) {
    VU0_MATRIX4_vect3mult(in, m, out);
}

// AUTOINJECT
void TransformPoint(const void *m, const void *in, void *out) {
    VU0_MATRIX4_vect4mult(in, m, out);
}

// AUTOINJECT
void MATRIX4_RotateVector(const void *m, const void *in, void *out) {
    VU0_MATRIX4_vect3rotate(in, m, out);
}

// The inverse of a rotation-and-translation, in place: the 3x3 transposed, the translation taken back through it.
// m[3], m[7], m[11] and m[15] are left alone.
// AUTOINJECT
void OrthoInverse(void *m) {
    float *o = W(m);
    float r0[3] = { o[0], o[1], o[2] }, r1[3] = { o[4], o[5], o[6] }, r2[3] = { o[8], o[9], o[10] };
    double tx = o[12], ty = o[13], tz = o[14];
    o[1] = r1[0]; o[4] = r0[1];
    o[2] = r2[0]; o[8] = r0[2];
    o[6] = r2[1]; o[9] = r1[2];
    o[12] = (F)((-(tx * r0[0]) - ty * r0[1]) - tz * r0[2]);
    o[13] = (F)((-(tx * r1[0]) - ty * r1[1]) - tz * r1[2]);
    o[14] = (F)((-(tx * r2[0]) - ty * r2[1]) - tz * r2[2]);
}

// The axes of a rotation quaternion (the doublings are exact).
// AUTOINJECT
void VU0_ExtractXAxis3FromQuat(const void *q, void *out) {
    const float *p = V(q);
    double A = 2.0 * ((double)p[0] * p[1]), B = 2.0 * ((double)p[0] * p[2]);
    double C = 2.0 * ((double)p[3] * p[1]), D = 2.0 * ((double)p[3] * p[2]);
    double ZZ = 2.0 * ((double)p[2] * p[2]), YY = 2.0 * ((double)p[1] * p[1]);
    float *o = W(out);
    o[0] = (F)(1.0 - (ZZ + YY));
    o[1] = (F)(D + A);
    o[2] = (F)(B - C);
}

// AUTOINJECT
void VU0_ExtractZAxis3FromQuat(const void *q, void *out) {
    const float *p = V(q);
    double XX = 2.0 * ((double)p[0] * p[0]), YY = 2.0 * ((double)p[1] * p[1]);
    double WX = 2.0 * ((double)p[3] * p[0]), ZY = 2.0 * ((double)p[2] * p[1]);
    double ZX = 2.0 * ((double)p[2] * p[0]), WY = 2.0 * ((double)p[3] * p[1]);
    float *o = W(out);
    o[0] = (F)(ZX + WY);
    o[1] = (F)(ZY - WX);
    o[2] = (F)(1.0 - (YY + XX));
}

// Scale, rotation (quaternion) and translation to a matrix; column c is scaled by scale[c].
// AUTOINJECT
void VU0_SQTquattom4(const void *scale, const void *q, const void *t, void *out) {
    const float *s = V(scale), *p = V(q);
    double XX = 2.0 * ((double)p[0] * p[0]), YY = 2.0 * ((double)p[1] * p[1]), ZZ = 2.0 * ((double)p[2] * p[2]);
    double XY = 2.0 * ((double)p[0] * p[1]);
    float ZXf = (F)(2.0 * ((double)p[2] * p[0])), WXf = (F)(2.0 * ((double)p[3] * p[0]));
    float ZYf = (F)(2.0 * ((double)p[2] * p[1])), WYf = (F)(2.0 * ((double)p[3] * p[1]));
    double WZ = 2.0 * ((double)p[3] * p[2]);
    float o[16];
    o[0] = (F)((1.0 - (ZZ + YY)) * s[0]);
    o[1] = (F)((WZ + XY) * s[1]);
    o[2] = (F)(((double)ZXf - WYf) * s[2]);
    o[3] = 0.0f;
    o[4] = (F)((XY - WZ) * s[0]);
    o[5] = (F)((1.0 - (ZZ + XX)) * s[1]);
    o[6] = (F)(((double)ZYf + WXf) * s[2]);
    o[7] = 0.0f;
    o[8] = (F)(((double)WYf + ZXf) * s[0]);
    o[9] = (F)(((double)ZYf - WXf) * s[1]);
    o[10] = (F)((1.0 - (YY + XX)) * s[2]);
    o[11] = 0.0f;
    memcpy(o + 12, t, 12);
    o[15] = 1.0f;
    memcpy(out, o, sizeof(o));
}

// __ftol2's truncation, of which only the low byte is kept: anything it cannot convert gives 0x80000000_00000000.
static inline uint32_t LowByteOfTruncation(double v) {
    if (!(v > -9223372036854775808.0 && v < 9223372036854775808.0))
        return 0;
    return (uint32_t)(int64_t)v & 0xff;
}

// A colour (0..1 floats) packed as r, g, b, a bytes, unclamped; red is truncated from the unrounded product.
// FUNC_AT(0x00116370)
void VU0_v4tocolour(const void *colour, uint32_t *out) {
    const float *c = V(colour);
    double r = (double)c[0] * 255.0;
    float g = (F)((double)c[1] * 255.0), b = (F)((double)c[2] * 255.0), a = (F)((double)c[3] * 255.0);
    *out = LowByteOfTruncation(r) << 24 | LowByteOfTruncation(g) << 16 | LowByteOfTruncation(b) << 8 |
           LowByteOfTruncation(a);
}

// The 3x3 of a times the 3x3 of b; out[3], out[7], out[11] = 0; row 3 of out untouched. Inputs copied first.
// AUTOINJECT
void VU0_MATRIX3x4_mult(const void *a, const void *b, void *out) {
    float A[12], B[12];
    memcpy(A, a, sizeof(A));
    memcpy(B, b, sizeof(B));
    float *o = W(out);
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            o[4 * r + c] = (F)(((double)A[4 * r + 2] * B[8 + c] + (double)A[4 * r + 1] * B[4 + c]) +
                               (double)A[4 * r] * B[c]);
    o[3] = o[7] = o[11] = 0.0f;
}

// The 3x3 transposed; with src != dst the rest is copied too. Integer moves.
// AUTOINJECT
void VU0_MATRIX4_3x3transpose(const void *src, void *dst) {
    const uint32_t *s = (const uint32_t *)src;
    uint32_t *d = (uint32_t *)dst;
    uint32_t s1 = s[1], s2 = s[2], s4 = s[4], s6 = s[6], s8 = s[8], s9 = s[9];
    d[4] = s1; d[1] = s4; d[2] = s8; d[6] = s9; d[8] = s2; d[9] = s6;
    if (src != dst) {
        static const int rest[] = { 0, 3, 5, 7, 10, 11, 12, 13, 14, 15 };
        for (int i : rest)
            d[i] = s[i];
    }
}

// Rotations about one axis: zero, then the sine and cosine (cosine computed first) and 1s.
// AUTOINJECT
void VU0_MATRIX4setxrot(void *m, float turns) {
    float c = cos_fractionalangle(turns), s = sin_fractionalangle(turns);
    float *o = W(m);
    memset(o, 0, 64);
    o[6] = s; o[9] = -s;
    o[10] = c; o[5] = c;
    o[0] = 1.0f; o[15] = 1.0f;
}

// AUTOINJECT
void VU0_MATRIX4setyrot(void *m, float turns) {
    float c = cos_fractionalangle(turns), s = sin_fractionalangle(turns);
    float *o = W(m);
    memset(o, 0, 64);
    o[2] = -s; o[8] = s;
    o[0] = c; o[10] = c;
    o[5] = 1.0f; o[15] = 1.0f;
}

// AUTOINJECT
void VU0_MATRIX4setzrot(void *m, float turns) {
    float c = cos_fractionalangle(turns), s = sin_fractionalangle(turns);
    float *o = W(m);
    memset(o, 0, 64);
    o[1] = s; o[4] = -s;
    o[5] = c; o[0] = c;
    o[10] = 1.0f; o[15] = 1.0f;
}

// A colour from its bytes, x, y and z (r, g, b) or all four (r, g, b, a from the top byte down), scaled to 0..1,
// then optionally added to, multiplied by and added to again, as the originals do with the VU0 functions.
// FUNC_AT(0x00114a80)
void BytesToCoordXYZ(uint32_t rgb, const void *add, const void *mul, const void *add2, void *out) {
    float *o = W(out);
    o[0] = (F)(rgb >> 16 & 0xff);
    o[1] = (F)(rgb >> 8 & 0xff);
    o[2] = (F)(rgb & 0xff);
    VU0_v4scale(out, FloatBits(0x3b808081), out);   // 1/255
    if (add != NULL)
        VU0_v3add(out, add, out);
    if (mul != NULL)
        VU0_v4multxyz(out, mul, out);
    if (add2 != NULL)
        VU0_v3add(out, add2, out);
}

// FUNC_AT(0x00114b30)
void BytesToCoordXYZW(uint32_t rgba, const void *add, const void *mul, const void *add2, void *out) {
    float *o = W(out);
    o[0] = (F)(rgba >> 24);
    o[1] = (F)(rgba >> 16 & 0xff);
    o[2] = (F)(rgba >> 8 & 0xff);
    o[3] = (F)(rgba & 0xff);
    VU0_v4scale4(out, FloatBits(0x3b808081), out);
    if (add != NULL)
        VU0_v4add4(out, add, out);
    if (mul != NULL)
        VU0_v4mult(out, mul, out);
    if (add2 != NULL)
        VU0_v4add4(out, add2, out);
}

// A segment (from, to) against a box of half-extents about the origin: the nearest face plane along the
// segment's direction, tried in turn, the candidate hit point left in hit. NaN hits pass.
// AUTOINJECT
bool FindOBBIntersect(const void *halfExtents, const void *from, const void *to, void *hit) {
    const float *half = V(halfExtents), *f = V(from);
    alignas(16) float dir[4] = {}, face[4] = {}, diff[4] = {}, big[4] = {}, a[4] = {}, scaled[4] = {};
    VU0_v4sub(to, from, diff);
    VU0_v4unitxyz(diff, dir);
    for (int i = 0; i < 3; i++)
        face[i] = (f[i] < 0) ? -half[i] : half[i];
    VU0_v4sub(face, from, diff);
    float t[3] = { 0.0f, 0.0f, 0.0f };
    for (int i = 0; i < 3; i++) {
        if ((double)dir[i] * dir[i] > (double)FloatBits(0x38d1b717))   // 1e-4
            t[i] = (F)((double)diff[i] / dir[i]);
    }
    for (int i = 0; i < 3; i++)
        a[i] = (t[i] < 0) ? -t[i] : t[i];
    const float *zero = (const float *)0x00243030u;   // the game's zero vector, never written
    for (int n = 3; n > 0; n--) {
        memcpy(big, zero, 12);
        int k = (a[0] < a[1] && a[0] < a[2]) ? 0 : (a[1] < a[2]) ? 1 : 2;
        VU0_v4scale(dir, t[k], scaled);
        big[k] = FloatBits(0x4e6e6b28);   // 1e9
        VU0_v3add(from, scaled, hit);
        const float *h = V(hit);
        bool inside = true;
        for (int i = 0; i < 3 && inside; i++) {
            double v = h[i];
            if (v < 0)
                v = -v;
            inside = !(v > (double)half[i] + (double)0.1f);
        }
        if (inside)
            return true;
        VU0_v3add(a, big, a);
    }
    return false;
}

// The determinant, unrounded. The statements are the original's x87 instructions one for one (each a double
// operation, each float store a rounding), generated from the listing and checked by devtools/MathShadow.cpp.
// AUTOINJECT
double Determinant4x4(const void *matrix) {
    const float *m = V(matrix);
    double d1 = (double)m[10] * (double)m[15];
    double d2 = (double)m[14] * (double)m[11];
    double d3 = d1 - d2;
    double d4 = (double)m[13] * (double)m[11];
    double d5 = (double)m[15] * (double)m[9];
    double d6 = (double)m[14] * (double)m[9];
    double d7 = (double)m[10] * (double)m[13];
    double d8 = d6 - d7;
    double d9 = (double)m[15] * (double)m[8];
    double d10 = (double)m[14] * (double)m[8];
    float f11 = (float)d10;
    double d12 = (double)m[10] * (double)m[12];
    float f13 = (float)d12;
    double d14 = (double)m[13] * (double)m[8];
    double d15 = (double)m[12] * (double)m[9];
    double d16 = d14 - d15;
    float f17 = (float)d16;
    double d18 = d4 - d5;
    double d19 = d18 * (double)m[6];
    double d20 = d8 * (double)m[7];
    double d21 = d19 + d20;
    double d22 = d3 * (double)m[5];
    double d23 = d21 + d22;
    double d24 = d23 * (double)m[0];
    double d25 = (double)m[12] * (double)m[11];
    double d26 = d25 - d9;
    double d27 = d26 * (double)m[6];
    double d28 = (double)f11 - (double)f13;
    double d29 = d28 * (double)m[7];
    double d30 = d27 + d29;
    double d31 = d3 * (double)m[4];
    double d32 = d30 + d31;
    double d33 = d32 * (double)m[1];
    double d34 = d24 - d33;
    double d35 = d4 - d9;
    double d36 = d35 * (double)m[5];
    double d37 = d5 - d4;
    double d38 = d37 * (double)m[4];
    double d39 = d36 + d38;
    double d40 = (double)f17 * (double)m[7];
    double d41 = d39 + d40;
    double d42 = d41 * (double)m[2];
    double d43 = d34 + d42;
    double d44 = (double)f13 - (double)f11;
    double d45 = d44 * (double)m[5];
    double d46 = d8 * (double)m[4];
    double d47 = d45 + d46;
    double d48 = (double)f17 * (double)m[6];
    double d49 = d47 + d48;
    double d50 = d49 * (double)m[3];
    double d51 = d43 - d50;
    return d51;
}

// The inverse, in place, unless the determinant is within 1e-5 of zero (NaN inverts). Answers the determinant,
// unrounded, which the original leaves on the x87 stack for its callers to pop. Generated from the listing as
// Determinant4x4 is; the original keeps 1/det, column 0 and some minors in registers and stores the rest of the
// minors as float temporaries, which is where the float roundings below come from.
// AUTOINJECT
double Inverse(void *matrix) {
    const float *m = V(matrix);
    float *o = W(matrix);
    double det = Determinant4x4(matrix);
    if (det < (double)FloatBits(0x3727c5ac) && !(det <= -(double)FloatBits(0x3727c5ac)))   // +-1e-5
        return det;
    double d1 = 1.0 / det;
    double d2 = (double)m[15] * (double)m[10];
    double d3 = (double)m[11] * (double)m[14];
    double d4 = d2 - d3;
    float f5 = (float)d4;
    double d6 = (double)m[7] * (double)m[14];
    float f7 = (float)d6;
    double d8 = (double)m[15] * (double)m[6];
    float f9 = (float)d8;
    double d10 = (double)f7 - (double)f9;
    float f11 = (float)d10;
    double d12 = (double)m[11] * (double)m[6];
    double d13 = (double)m[7] * (double)m[10];
    double d14 = d12 - d13;
    float f15 = (float)d14;
    double d16 = d14 * (double)m[13];
    double d17 = (double)f11 * (double)m[9];
    double d18 = d16 + d17;
    double d19 = (double)f5 * (double)m[5];
    double d20 = d18 + d19;
    double d21 = d20 * d1;
    float f22 = (float)d21;
    double d23 = (double)m[3] * (double)m[14];
    double d24 = (double)m[15] * (double)m[2];
    double d25 = d23 - d24;
    float f26 = (float)d25;
    double d27 = (double)m[11] * (double)m[2];
    float f28 = (float)d27;
    double d29 = (double)m[3] * (double)m[10];
    float f30 = (float)d29;
    double d31 = (double)f28 - (double)f30;
    float f32 = (float)d31;
    double d33 = d31 * (double)m[13];
    double d34 = (double)f26 * (double)m[9];
    double d35 = d33 + d34;
    double d36 = (double)f5 * (double)m[1];
    double d37 = d35 + d36;
    double d38 = -d37;
    double d39 = d38 * d1;
    float f40 = (float)d39;
    double d41 = (double)f9 - (double)f7;
    float f42 = (float)d41;
    double d43 = (double)m[7] * (double)m[2];
    double d44 = (double)m[3] * (double)m[6];
    double d45 = d43 - d44;
    float f46 = (float)d45;
    double d47 = d45 * (double)m[13];
    double d48 = (double)f42 * (double)m[1];
    double d49 = d47 + d48;
    double d50 = (double)f26 * (double)m[5];
    double d51 = d49 + d50;
    double d52 = d51 * d1;
    float f53 = (float)d52;
    double d54 = (double)f30 - (double)f28;
    float f55 = (float)d54;
    double d56 = d54 * (double)m[5];
    double d57 = (double)f46 * (double)m[9];
    double d58 = d56 + d57;
    double d59 = (double)f15 * (double)m[1];
    double d60 = d58 + d59;
    double d61 = -d60;
    double d62 = d61 * d1;
    float f63 = (float)d62;
    double d64 = (double)f15 * (double)m[12];
    double d65 = (double)f11 * (double)m[8];
    double d66 = d64 + d65;
    double d67 = (double)f5 * (double)m[4];
    double d68 = d66 + d67;
    double d69 = -d68;
    double d70 = d69 * d1;
    float f71 = (float)d70;
    double d72 = (double)f32 * (double)m[12];
    double d73 = (double)f26 * (double)m[8];
    double d74 = d72 + d73;
    double d75 = (double)f5 * (double)m[0];
    double d76 = d74 + d75;
    double d77 = d76 * d1;
    float f78 = (float)d77;
    double d79 = (double)f46 * (double)m[12];
    double d80 = (double)f42 * (double)m[0];
    double d81 = d79 + d80;
    double d82 = (double)f26 * (double)m[4];
    double d83 = d81 + d82;
    double d84 = -d83;
    double d85 = d84 * d1;
    float f86 = (float)d85;
    double d87 = (double)f55 * (double)m[4];
    double d88 = (double)f46 * (double)m[8];
    double d89 = d87 + d88;
    double d90 = (double)f15 * (double)m[0];
    double d91 = d89 + d90;
    double d92 = d91 * d1;
    float f93 = (float)d92;
    double d94 = (double)m[15] * (double)m[9];
    double d95 = (double)m[11] * (double)m[13];
    double d96 = d94 - d95;
    float f97 = (float)d96;
    double d98 = (double)m[7] * (double)m[13];
    float f99 = (float)d98;
    double d100 = (double)m[15] * (double)m[5];
    float f101 = (float)d100;
    double d102 = (double)m[11] * (double)m[5];
    double d103 = (double)m[7] * (double)m[9];
    double d104 = d102 - d103;
    float f105 = (float)d104;
    double d106 = (double)f99 - (double)f101;
    double d107 = d106 * (double)m[8];
    double d108 = (double)f105 * (double)m[12];
    double d109 = d107 + d108;
    double d110 = (double)f97 * (double)m[4];
    double d111 = d109 + d110;
    double d112 = d111 * d1;
    float f113 = (float)d112;
    double d114 = (double)m[3] * (double)m[13];
    double d115 = (double)m[15] * (double)m[1];
    double d116 = d114 - d115;
    float f117 = (float)d116;
    double d118 = (double)m[11] * (double)m[1];
    float f119 = (float)d118;
    double d120 = (double)m[3] * (double)m[9];
    float f121 = (float)d120;
    double d122 = (double)f119 - (double)f121;
    double d123 = d122 * (double)m[12];
    double d124 = (double)f117 * (double)m[8];
    double d125 = d123 + d124;
    double d126 = (double)f97 * (double)m[0];
    double d127 = d125 + d126;
    double d128 = -d127;
    double d129 = d128 * d1;
    float f130 = (float)d129;
    double d131 = (double)m[7] * (double)m[1];
    double d132 = (double)m[3] * (double)m[5];
    double d133 = d131 - d132;
    float f134 = (float)d133;
    double d135 = (double)f101 - (double)f99;
    double d136 = d135 * (double)m[0];
    double d137 = (double)f134 * (double)m[12];
    double d138 = d136 + d137;
    double d139 = (double)f117 * (double)m[4];
    double d140 = d138 + d139;
    double d141 = d140 * d1;
    float f142 = (float)d141;
    double d143 = (double)f121 - (double)f119;
    double d144 = d143 * (double)m[4];
    double d145 = (double)f134 * (double)m[8];
    double d146 = d144 + d145;
    double d147 = (double)f105 * (double)m[0];
    double d148 = d146 + d147;
    double d149 = -d148;
    double d150 = d149 * d1;
    float f151 = (float)d150;
    double d152 = (double)m[14] * (double)m[9];
    double d153 = (double)m[10] * (double)m[13];
    double d154 = d152 - d153;
    float f155 = (float)d154;
    double d156 = (double)m[6] * (double)m[13];
    float f157 = (float)d156;
    double d158 = (double)m[14] * (double)m[5];
    float f159 = (float)d158;
    double d160 = (double)m[10] * (double)m[5];
    double d161 = (double)m[6] * (double)m[9];
    double d162 = d160 - d161;
    float f163 = (float)d162;
    double d164 = (double)f157 - (double)f159;
    double d165 = d164 * (double)m[8];
    double d166 = (double)f163 * (double)m[12];
    double d167 = d165 + d166;
    double d168 = (double)f155 * (double)m[4];
    double d169 = d167 + d168;
    double d170 = -d169;
    double d171 = d170 * d1;
    float f172 = (float)d171;
    double d173 = (double)m[2] * (double)m[13];
    double d174 = (double)m[14] * (double)m[1];
    double d175 = d173 - d174;
    float f176 = (float)d175;
    double d177 = (double)m[10] * (double)m[1];
    float f178 = (float)d177;
    double d179 = (double)m[2] * (double)m[9];
    float f180 = (float)d179;
    double d181 = (double)f178 - (double)f180;
    double d182 = d181 * (double)m[12];
    double d183 = (double)f176 * (double)m[8];
    double d184 = d182 + d183;
    double d185 = (double)f155 * (double)m[0];
    double d186 = d184 + d185;
    double d187 = d186 * d1;
    float f188 = (float)d187;
    double d189 = (double)m[6] * (double)m[1];
    double d190 = (double)m[2] * (double)m[5];
    double d191 = d189 - d190;
    float f192 = (float)d191;
    double d193 = (double)f159 - (double)f157;
    double d194 = d193 * (double)m[0];
    double d195 = (double)f192 * (double)m[12];
    double d196 = d194 + d195;
    double d197 = (double)f176 * (double)m[4];
    double d198 = d196 + d197;
    double d199 = -d198;
    double d200 = d199 * d1;
    float f201 = (float)d200;
    double d202 = (double)f180 - (double)f178;
    double d203 = d202 * (double)m[4];
    double d204 = (double)f192 * (double)m[8];
    double d205 = d203 + d204;
    double d206 = (double)f163 * (double)m[0];
    double d207 = d205 + d206;
    double d208 = d207 * d1;
    float f209 = (float)d208;
    o[0] = f22;
    o[1] = f40;
    o[2] = f53;
    o[3] = f63;
    o[4] = f71;
    o[5] = f78;
    o[6] = f86;
    o[7] = f93;
    o[8] = f113;
    o[9] = f130;
    o[10] = f142;
    o[11] = f151;
    o[12] = f172;
    o[13] = f188;
    o[14] = f201;
    o[15] = f209;
    return det;
}

// sin(turns) - FSIN's 64-bit result, which the original never stores - times four floats, each product rounded
// to 53 bits as the x87 does at this precision, so each is exactly a double: f = { c0, s0, c1, s1 }.
static __declspec(naked) void SinTimes(float turns, const float *f, double *products) {
    __asm {
        fld dword ptr [kTwoPi]
        fmul dword ptr [esp + 4]
        fsin
        mov eax, dword ptr [esp + 8]
        mov edx, dword ptr [esp + 0xc]
        fld st(0)
        fmul dword ptr [eax]
        fstp qword ptr [edx]
        fld st(0)
        fmul dword ptr [eax + 4]
        fstp qword ptr [edx + 8]
        fld st(0)
        fmul dword ptr [eax + 8]
        fstp qword ptr [edx + 16]
        fmul dword ptr [eax + 12]
        fstp qword ptr [edx + 24]
        ret
    }
}

// Euler angles (turns about x, y, z) to a quaternion, through the rotation matrix D3DX turns into one. Only the
// nine elements D3DXQuaternionRotationMatrix reads are filled. sin(z) is used unrounded in four products.
// FUNC_AT(0x00116d10)
void VU0_EulerToQuat(const void *eulerTurns, void *out) {
    const float *e = V(eulerTurns);
    float f[4];
    f[0] = cos_fractionalangle(e[0]);   // c0
    f[1] = sin_fractionalangle(e[0]);   // s0
    f[2] = cos_fractionalangle(e[1]);   // c1
    f[3] = sin_fractionalangle(e[1]);   // s1
    float c2 = cos_fractionalangle(e[2]);
    double p[4];                        // s2*c0, s2*s0, s2*c1, s2*s1
    SinTimes(e[2], f, p);
    float c0 = f[0], s0 = f[1], c1 = f[2], s1 = f[3];
    alignas(16) float m[16] = {};
    double A = (double)c2 * s1;
    m[0] = (F)((double)c2 * c1);
    m[4] = (F)(s0 * A - p[0]);
    m[8] = (F)(A * c0 - p[1]);
    m[1] = (F)p[2];
    m[5] = (F)(s0 * p[3] + (double)c2 * c0);
    m[9] = (F)(p[3] * c0 - (double)c2 * s0);
    m[2] = -s1;
    m[6] = (F)((double)c1 * s0);
    m[10] = (F)((double)c1 * c0);
    VU0_m4toquat(out, m);
}
