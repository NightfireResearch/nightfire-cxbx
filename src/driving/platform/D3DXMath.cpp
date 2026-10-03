#include "RealMath.h"

#include <xmmintrin.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// Microsoft's D3DX maths routines that the driving engine links into .text beside EA's maths (0x00113264..
// 0x00113a48), which game, camera and physics code call under EA's VU0_* names (docs/driving/maths.md 4.4). Each
// is the original at the same address, ported from it bit for bit: the SSE ones lane for lane, the x87 ones in
// double in the original's order with the original's float roundings. All are stdcall and answer their output.
// devtools/MathShadow.cpp compares every one with the original.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

typedef float F;
typedef __m128 Q;
#define V(p) ((const float *)(p))
#define W(p) ((float *)(p))

static inline float FloatBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}
static inline Q L(const void *p) { return _mm_loadu_ps((const float *)p); }
static inline Q Lane(Q v, int i) {
    switch (i) {
    case 0: return _mm_shuffle_ps(v, v, 0x00);
    case 1: return _mm_shuffle_ps(v, v, 0x55);
    case 2: return _mm_shuffle_ps(v, v, 0xaa);
    default: return _mm_shuffle_ps(v, v, 0xff);
    }
}

// out = a * b, every row computed before any is stored (so out may be a or b).
// AUTOINJECT
void *__stdcall VU0_MATRIX4_mult(void *out, const void *a, const void *b) {
    Q b0 = L(b), b1 = L(V(b) + 4), b2 = L(V(b) + 8), b3 = L(V(b) + 12);
    Q r[4];
    for (int i = 0; i < 4; i++) {
        Q row = L(V(a) + 4 * i);
        Q s = _mm_mul_ps(Lane(row, 0), b0), t = _mm_mul_ps(Lane(row, 1), b1);
        Q u = _mm_mul_ps(Lane(row, 2), b2);
        s = _mm_add_ps(s, t);
        t = _mm_mul_ps(Lane(row, 3), b3);
        s = _mm_add_ps(s, u);
        r[i] = _mm_add_ps(s, t);
    }
    for (int i = 0; i < 4; i++)
        _mm_storeu_ps(W(out) + 4 * i, r[i]);
    return out;
}

// Integer moves.
// AUTOINJECT
void *__stdcall VU0_MATRIX4_transpose(void *out, const void *m) {
    uint32_t t[16];
    memcpy(t, m, sizeof(t));
    uint32_t *o = (uint32_t *)out;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            o[4 * r + c] = t[4 * c + r];
    return out;
}

// D3DXMatrixRotationQuaternion. X2 = 2x and Y2 = 2y are stored as floats, Z2 = 2z kept; 1 - XX is used unrounded
// in m11 and rounded in m22, as the original does.
// AUTOINJECT
void *__stdcall VU0_quattom4(void *out, const void *q) {
    const float *p = V(q);
    float X2 = (F)(2.0 * p[0]), Y2 = (F)(2.0 * p[1]);
    double Z2 = 2.0 * p[2];
    double WX = (double)X2 * p[3], WY = (double)Y2 * p[3], WZ = Z2 * p[3];
    float XX = (F)((double)X2 * p[0]), XY = (F)((double)Y2 * p[0]), XZ = (F)(Z2 * p[0]);
    float YY = (F)((double)Y2 * p[1]), YZ = (F)(Z2 * p[1]);
    double ZZ = Z2 * p[2];
    float m[16];
    m[0] = (F)((1.0 - YY) - ZZ);
    m[1] = (F)(XY + WZ);
    m[2] = (F)(XZ - WY);
    m[3] = 0.0f;
    m[4] = (F)(XY - WZ);
    double oneMinusXX = 1.0 - XX;
    float oneMinusXXf = (F)oneMinusXX;
    m[5] = (F)(oneMinusXX - ZZ);
    m[6] = (F)(YZ + WX);
    m[7] = 0.0f;
    m[8] = (F)(XZ + WY);
    m[9] = (F)(YZ - WX);
    m[10] = (F)((double)oneMinusXXf - YY);
    m[11] = m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
    memcpy(out, m, sizeof(m));
    return out;
}

// D3DXQuaternionRotationMatrix.
// AUTOINJECT
void *__stdcall VU0_m4toquat(void *out, const void *matrix) {
    const float *m = V(matrix);
    float *q = W(out);
    double tr = ((double)m[5] + m[0]) + m[10];
    if (tr > 0) {
        double s = sqrt(tr + 1.0) * 0.5;
        q[3] = (F)s;
        double k = 0.25 / s;
        q[0] = (F)(((double)m[6] - m[9]) * k);
        q[1] = (F)(((double)m[8] - m[2]) * k);
        q[2] = (F)(((double)m[1] - m[4]) * k);
        return out;
    }
    static const int next[3] = { 1, 2, 0 };
    double d0 = ((double)m[0] - m[5]) - m[10];
    float d[3];
    d[0] = (F)d0;
    d[1] = (F)(((double)m[5] - m[0]) - m[10]);
    d[2] = (F)((double)m[10] - ((double)m[5] + m[0]));
    int i = (d0 < d[1] || d0 != d0 || d[1] != d[1]) ? 1 : 0;   // the unrounded d0; unordered takes 1
    if (d[i] < d[2] || d[i] != d[i] || d[2] != d[2])
        i = 2;
    int j = next[i], k = next[j];
    double s = sqrt((double)d[i] + 1.0) * 0.5;
    q[i] = (F)s;
    double r = 0.25 / s;
    q[j] = (F)(((double)m[4 * j + i] + m[4 * i + j]) * r);
    q[k] = (F)(((double)m[4 * k + i] + m[4 * i + k]) * r);
    q[3] = (F)(((double)m[4 * j + k] - m[4 * k + j]) * r);
    return out;
}

// D3DXQuaternionMultiply (unaligned), the original's shuffles and sign masks.
// FUNC_AT(0x001139c1)
void *__stdcall D3DXQuaternionMultiply(void *out, const void *q1, const void *q2) {
    const float *b = V(q2);
    Q a = L(q1);
    const float s = FloatBits(0x80000000u);
    Q m1 = _mm_set_ps(s, 0.0f, s, 0.0f);       // (0, -0, 0, -0)
    Q m2 = _mm_set_ps(s, s, 0.0f, 0.0f);       // (0, 0, -0, -0)
    Q m3 = _mm_set_ps(s, 0.0f, 0.0f, s);       // (-0, 0, 0, -0)
    Q r = _mm_mul_ps(a, _mm_set1_ps(b[3]));
    r = _mm_add_ps(r, _mm_xor_ps(_mm_mul_ps(_mm_shuffle_ps(a, a, 0x1b), _mm_set1_ps(b[0])), m1));
    r = _mm_add_ps(r, _mm_xor_ps(_mm_mul_ps(_mm_shuffle_ps(a, a, 0x4e), _mm_set1_ps(b[1])), m2));
    r = _mm_add_ps(r, _mm_xor_ps(_mm_mul_ps(_mm_shuffle_ps(a, a, 0xb1), _mm_set1_ps(b[2])), m3));
    _mm_storeu_ps(W(out), r);
    return out;
}

// FSINCOS of a float, each result stored as a float (one rounding from the 64-bit result).
static __declspec(naked) void SinCosFloat(float angle, float *cosOut, float *sinOut) {
    __asm {
        fld dword ptr [esp + 4]
        fsincos
        mov eax, dword ptr [esp + 8]
        fstp dword ptr [eax]
        mov eax, dword ptr [esp + 0xc]
        fstp dword ptr [eax]
        ret
    }
}

// D3DXMatrixPerspectiveFovRH.
// FUNC_AT(0x001136e5)
void *__stdcall D3DXMatrixPerspectiveFovRH(void *out, float fovy, float aspect, float zn, float zf) {
    float h = (F)((double)fovy * 0.5), c, s;
    SinCosFloat(h, &c, &s);
    double ys = (double)c / s;
    float *m = W(out);
    memset(m, 0, 64);
    m[11] = -1.0f;
    m[0] = (F)(ys / aspect);
    m[5] = (F)ys;
    double q = (double)zf / ((double)zn - zf);
    m[10] = (F)q;
    m[14] = (F)(q * zn);
    return out;
}

// D3DXMatrixOrthoRH.
// FUNC_AT(0x00113779)
void *__stdcall D3DXMatrixOrthoRH(void *out, float w, float h, float zn, float zf) {
    float *m = W(out);
    memset(m, 0, 64);
    m[0] = (F)(2.0 / w);
    m[5] = (F)(2.0 / h);
    double c = 1.0 / ((double)zn - zf);
    m[10] = (F)c;
    m[14] = (F)(c * zn);
    m[15] = 1.0f;
    return out;
}

// D3DXMatrixOrthoOffCenterRH: 1/(t-b) and 1/(zn-zf) are rounded to float where the original stores them.
// FUNC_AT(0x001137eb)
void *__stdcall D3DXMatrixOrthoOffCenterRH(void *out, float l, float r, float b, float t, float zn, float zf) {
    double A = 1.0 / ((double)r - l);
    float Bf = (F)(1.0 / ((double)t - b));
    double C = 1.0 / ((double)zn - zf);
    float Cf = (F)C;
    float *m = W(out);
    memset(m, 0, 64);
    m[0] = (F)(A * 2.0);
    m[5] = (F)((double)Bf * 2.0);
    m[10] = (F)C;
    m[12] = (F)-(((double)l + r) * A);
    m[13] = (F)-(((double)b + t) * Bf);
    m[14] = (F)((double)Cf * zn);
    m[15] = 1.0f;
    return out;
}

// ---- D3DXVec3Project and the helpers only it calls

// 0x00113283
static void MatrixIdentity(float *m) {
    memset(m, 0, 64);
    m[0] = m[5] = m[10] = m[15] = 1.0f;
}

// a - b within +-1e-5 (NaN is not). 0x001132d9
static bool NearlyEqual(float a, float b) {
    double d = (double)a - b;
    return !(-(double)FloatBits(0x3727c5ac) > d || d != d) && !(d > (double)FloatBits(0x3727c5ac));
}

// D3DXVec3TransformCoord (0x00113309): p = ((x*r0 + r3) + y*r1) + z*r2, then divided by p.w unless that is 1.
static void TransformCoord(float *out, const float *v, const float *m) {
    Q p = _mm_mul_ps(_mm_set1_ps(v[0]), L(m));
    Q y = _mm_mul_ps(_mm_set1_ps(v[1]), L(m + 4));
    Q z = _mm_mul_ps(_mm_set1_ps(v[2]), L(m + 8));
    p = _mm_add_ps(p, L(m + 12));
    p = _mm_add_ps(p, y);
    p = _mm_add_ps(p, z);
    float r[4];
    _mm_storeu_ps(r, p);
    out[0] = r[0];
    out[1] = r[1];
    out[2] = r[2];
    if (!NearlyEqual(r[3], 1.0f)) {   // 0x00113264, D3DXVECTOR3::operator/=
        double k = 1.0 / r[3];
        out[0] = (F)(k * out[0]);
        out[1] = (F)(k * out[1]);
        out[2] = (F)(k * out[2]);
    }
}

static inline double UnsignedToDouble(uint32_t u) { return (double)u; }   // FILD, + 2^32 when "negative"

// D3DXVec3Project: through world * view * proj (whichever are given), then to the viewport if there is one.
// FUNC_AT(0x00113387)
void *__stdcall D3DXVec3Project(void *out, const void *v, const void *viewport, const void *proj, const void *view,
                                const void *world) {
    alignas(16) float local[16];
    const float *m = local;
    int which = (world != NULL) << 2 | (view != NULL) << 1 | (proj != NULL);
    switch (which) {
    case 0: MatrixIdentity(local); break;
    case 1: m = V(proj); break;
    case 2: m = V(view); break;
    case 3: VU0_MATRIX4_mult(local, view, proj); break;
    case 4: m = V(world); break;
    case 5: VU0_MATRIX4_mult(local, world, proj); break;
    case 6: VU0_MATRIX4_mult(local, world, view); break;
    case 7:
        VU0_MATRIX4_mult(local, world, view);
        VU0_MATRIX4_mult(local, local, proj);
        break;
    }
    float *o = W(out);
    TransformCoord(o, V(v), m);
    if (viewport != NULL) {
        const uint32_t *vp = (const uint32_t *)viewport;   // X, Y, Width, Height, then MinZ, MaxZ as floats
        float minZ, maxZ;
        memcpy(&minZ, vp + 4, 4);
        memcpy(&maxZ, vp + 5, 4);
        o[0] = (F)((((double)o[0] + 1.0) * UnsignedToDouble(vp[2])) * 0.5 + UnsignedToDouble(vp[0]));
        o[1] = (F)(((1.0 - o[1]) * UnsignedToDouble(vp[3])) * 0.5 + UnsignedToDouble(vp[1]));
        o[2] = (F)(((double)maxZ - minZ) * o[2] + minZ);
    }
    return out;
}
