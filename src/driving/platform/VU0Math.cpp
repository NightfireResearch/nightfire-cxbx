#include "RealMath.h"

#include <xmmintrin.h>
#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The Xbox build of EA's VU0 layer: the PS2's vector-unit routines rewritten in SSE, which game, physics and AI
// code call everywhere (docs/driving/maths.md, "The SSE family"). Each function here is the original at the same
// address, ported from it lane for lane with the same SSE instructions: single-precision arithmetic is exact IEEE,
// so the same operations in the same order give the same bits, and RSQRTSS - an estimate, never refined here - is
// the same instruction on the same CPU.
//
// The shapes callers rely on are kept: the "xyz" functions compute all four lanes, then put the destination's own
// w back (read before the store, so out == in works); the "4" variants store all four; horizontal sums add lane
// by lane as the original's rotate-and-add does, ((p0 + p1) + p2) + p3. The unit-vector functions skip the
// reciprocal square root when the length squared is +0 (its bits are 0), which makes the result the zero vector.
// Loads and stores are unaligned-tolerant here, where the original faults on a misaligned vector.
// ---------------------------------------------------------------------------------------------------------------

typedef __m128 Q;

static inline Q L(const void *p) { return _mm_loadu_ps((const float *)p); }
static inline void S4(void *p, Q v) { _mm_storeu_ps((float *)p, v); }

// Store x, y and z, keeping the destination's w (the original reads it first and puts it back).
static inline void S3(void *p, Q v) {
    uint32_t w;
    memcpy(&w, (const char *)p + 12, 4);
    _mm_storeu_ps((float *)p, v);
    memcpy((char *)p + 12, &w, 4);
}

static inline void S3W1(void *p, Q v) {
    _mm_storeu_ps((float *)p, v);
    const uint32_t one = 0x3f800000;
    memcpy((char *)p + 12, &one, 4);
}

static inline Q Rot(Q v) { return _mm_shuffle_ps(v, v, 0x39); }   // lanes 1, 2, 3, 0
static inline Q Splat(float s) { return _mm_set1_ps(s); }
static inline Q Lane(Q v, int i) {
    switch (i) {
    case 0: return _mm_shuffle_ps(v, v, 0x00);
    case 1: return _mm_shuffle_ps(v, v, 0x55);
    case 2: return _mm_shuffle_ps(v, v, 0xaa);
    default: return _mm_shuffle_ps(v, v, 0xff);
    }
}
static inline float F0(Q v) { return _mm_cvtss_f32(v); }

static inline Q Sum3(Q p) {   // lane 0: (p0 + p1) + p2
    Q a = Rot(p), s = _mm_add_ps(p, a);
    return _mm_add_ps(s, Rot(a));
}
static inline Q Sum4(Q p) {   // lane 0: ((p0 + p1) + p2) + p3
    Q a = Rot(p), s = _mm_add_ps(p, a);
    a = Rot(a);
    s = _mm_add_ps(s, a);
    return _mm_add_ps(s, Rot(a));
}

// a.yzx * b.zxy - a.zxy * b.yzx, in the original's shuffles
static inline Q Cross(Q a, Q b) {
    Q l = _mm_mul_ps(_mm_shuffle_ps(a, a, 0xc9), _mm_shuffle_ps(b, b, 0xd2));
    Q r = _mm_mul_ps(_mm_shuffle_ps(a, a, 0xd2), _mm_shuffle_ps(b, b, 0xc9));
    return _mm_sub_ps(l, r);
}

static inline uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}

// The multiplier of the unit functions, broadcast: the estimate of 1/sqrt(len2), or len2 itself when it is +0
static inline Q UnitScale(Q len2) {
    if (Bits(F0(len2)) != 0)
        len2 = _mm_rsqrt_ss(len2);
    return _mm_shuffle_ps(len2, len2, 0);
}

// ---- sums and differences

// AUTOINJECT
void VU0_v3add(const void *a, const void *b, void *out) {
    S3(out, _mm_add_ps(L(a), L(b)));
}

// AUTOINJECT
void VU0_v4sub(const void *a, const void *b, void *out) {
    S3(out, _mm_sub_ps(L(a), L(b)));
}

// FUNC_AT(0x00115af0)
void VU0_v4add4(const void *a, const void *b, void *out) {
    S4(out, _mm_add_ps(L(a), L(b)));
}

// FUNC_AT(0x00115b10)
void VU0_v4sub4(const void *a, const void *b, void *out) {
    S4(out, _mm_sub_ps(L(a), L(b)));
}

// ---- dot products, lengths, distances (returned through the x87 already rounded to float)

// FUNC_AT(0x00115b30)
float v3dotprod(const void *a, const void *b) {
    return F0(Sum3(_mm_mul_ps(L(a), L(b))));
}

// FUNC_AT(0x00115b60)
float v4dotprod(const void *a, const void *b) {
    return F0(Sum4(_mm_mul_ps(L(a), L(b))));
}

// FUNC_AT(0x00115ba0)
float VU0_sqrt(float x) {
    return F0(_mm_sqrt_ss(_mm_set_ss(x)));
}

// +0 comes back as itself rather than as RSQRTSS's infinity.
// FUNC_AT(0x00115bc0)
float VU0_rsqrt(float x) {
    if (Bits(x) == 0)
        return x;
    return F0(_mm_rsqrt_ss(_mm_set_ss(x)));
}

// FUNC_AT(0x00115bf0)
float VU0_v3length(const void *v) {
    Q p = L(v);
    return F0(_mm_sqrt_ss(Sum3(_mm_mul_ps(p, p))));
}

// FUNC_AT(0x00115c20)
float VU0_v3lengthsquare(const void *v) {
    Q p = L(v);
    return F0(Sum3(_mm_mul_ps(p, p)));
}

// FUNC_AT(0x00115c50)
float VU0_v4lengthsquare(const void *v) {
    Q p = L(v);
    return F0(Sum4(_mm_mul_ps(p, p)));
}

// AUTOINJECT
float VU0_v3distancesquare(const void *a, const void *b) {
    Q d = _mm_sub_ps(L(a), L(b));
    return F0(Sum3(_mm_mul_ps(d, d)));
}

// AUTOINJECT
float vec3distance(const void *a, const void *b) {
    Q d = _mm_sub_ps(L(a), L(b));
    return F0(_mm_sqrt_ss(Sum3(_mm_mul_ps(d, d))));
}

static inline Q SumXZ(Q p) { return _mm_add_ps(p, _mm_shuffle_ps(p, p, 0x02)); }   // lane 0: x + z

// FUNC_AT(0x00116060)
float VU0_v3lengthxz(const void *v) {
    Q p = L(v);
    return F0(_mm_sqrt_ss(SumXZ(_mm_mul_ps(p, p))));
}

// FUNC_AT(0x00116090)
float VU0_v3distancexz(const void *a, const void *b) {
    Q d = _mm_sub_ps(L(a), L(b));
    return F0(_mm_sqrt_ss(SumXZ(_mm_mul_ps(d, d))));
}

// FUNC_AT(0x001160c0)
float VU0_v3distancesquarexz(const void *a, const void *b) {
    Q d = _mm_sub_ps(L(a), L(b));
    return F0(SumXZ(_mm_mul_ps(d, d)));
}

// ---- copies

// FUNC_AT(0x00115c80)
void VU0_v4copy(const void *src, void *dst) {
    S4(dst, L(src));
}

// AUTOINJECT
void MatrixCopy(const void *src, void *dst) {
    Q r0 = L(src), r1 = L((const float *)src + 4), r2 = L((const float *)src + 8), r3 = L((const float *)src + 12);
    S4(dst, r0);
    S4((float *)dst + 4, r1);
    S4((float *)dst + 8, r2);
    S4((float *)dst + 12, r3);
}

// The sign bits of x, y and z flipped, in place, as integers.
// FUNC_AT(0x001160f0)
void VU0_v3negate(void *v) {
    uint32_t *p = (uint32_t *)v;
    uint32_t x = p[0], y = p[1], z = p[2];
    p[0] = x ^ 0x80000000u;
    p[1] = y ^ 0x80000000u;
    p[2] = z ^ 0x80000000u;
}

// ---- cross products and unit vectors

// FUNC_AT(0x00115cc0)
void VU0_v4crossprodxyz(const void *a, const void *b, void *out) {
    S3(out, Cross(L(a), L(b)));
}

// FUNC_AT(0x00115d00)
void VU0_v4crossprod1(const void *a, const void *b, void *out) {
    S3W1(out, Cross(L(a), L(b)));
}

// AUTOINJECT
void VU0_v4unitcrossprodxyz(const void *a, const void *b, void *out) {
    Q c = Cross(L(a), L(b));
    S3(out, _mm_mul_ps(c, UnitScale(Sum3(_mm_mul_ps(c, c)))));
}

// FUNC_AT(0x00115db0)
void VU0_v4unitcrossprod1(const void *a, const void *b, void *out) {
    Q c = Cross(L(a), L(b));
    S3W1(out, _mm_mul_ps(c, UnitScale(Sum3(_mm_mul_ps(c, c)))));
}

// AUTOINJECT
void VU0_v4unitxyz(const void *in, void *out) {
    Q p = L(in);
    S3(out, _mm_mul_ps(p, UnitScale(Sum3(_mm_mul_ps(p, p)))));
}

// FUNC_AT(0x00115e70)
void VU0_v4unit(const void *in, void *out) {
    Q p = L(in);
    S4(out, _mm_mul_ps(p, UnitScale(Sum4(_mm_mul_ps(p, p)))));
}

// ---- products and scales

// out = b * a, all four lanes.
// FUNC_AT(0x00115ec0)
void VU0_v4mult(const void *a, const void *b, void *out) {
    S4(out, _mm_mul_ps(L(b), L(a)));
}

// FUNC_AT(0x00115ee0)
void VU0_v4multxyz(const void *a, const void *b, void *out) {
    S3(out, _mm_mul_ps(L(a), L(b)));
}

// Reads and writes only x, y and z, and needs no alignment.
// AUTOINJECT
void VU0_v4scale(const void *in, float scale, void *out) {
    const float *p = (const float *)in;
    Q v = _mm_set_ps(p[2], p[1], 0.0f, p[0]);   // MOVSS, then MOVHPS [in + 4] into lanes 2 and 3
    float r[4];
    _mm_storeu_ps(r, _mm_mul_ps(Splat(scale), v));
    float *o = (float *)out;
    o[0] = r[0];
    o[1] = r[2];
    o[2] = r[3];
}

// FUNC_AT(0x00115f30)
void VU0_v4scale4(const void *in, float scale, void *out) {
    S4(out, _mm_mul_ps(Splat(scale), L(in)));
}

// out = scale * (b + a), keeping w.
// FUNC_AT(0x00115f50)
void VU0_v4addscale(const void *a, const void *b, float scale, void *out) {
    S3(out, _mm_mul_ps(Splat(scale), _mm_add_ps(L(b), L(a))));
}

// out = scale * a + b, keeping w.
// AUTOINJECT
void VU0_v4scaleadd(const void *a, float scale, const void *b, void *out) {
    S3(out, _mm_add_ps(_mm_mul_ps(Splat(scale), L(a)), L(b)));
}

// FUNC_AT(0x00115fb0)
void VU0_v4scaleadd4(const void *a, float scale, const void *b, void *out) {
    S4(out, _mm_add_ps(_mm_mul_ps(Splat(scale), L(a)), L(b)));
}

// ---- matrix times vector (rows r0..r3, the vector's lanes broadcast)

static inline Q Row(const void *m, int r) { return L((const float *)m + 4 * r); }

// out.xyz = (x*r0 + y*r1) + (z*r2 + r3) - note the pairing - keeping w.
// AUTOINJECT
void VU0_MATRIX4_vect3mult(const void *in, const void *m, void *out) {
    Q v = L(in);
    Q a = _mm_mul_ps(Lane(v, 0), Row(m, 0)), b = _mm_mul_ps(Lane(v, 1), Row(m, 1));
    Q c = _mm_mul_ps(Lane(v, 2), Row(m, 2));
    a = _mm_add_ps(a, b);
    c = _mm_add_ps(c, Row(m, 3));
    S3(out, _mm_add_ps(a, c));
}

static inline Q Vect4(Q v, const void *m) {
    Q a = _mm_mul_ps(Lane(v, 0), Row(m, 0)), b = _mm_mul_ps(Lane(v, 1), Row(m, 1));
    Q c = _mm_mul_ps(Lane(v, 2), Row(m, 2));
    a = _mm_add_ps(a, b);
    Q d = _mm_mul_ps(Lane(v, 3), Row(m, 3));
    a = _mm_add_ps(a, c);
    return _mm_add_ps(a, d);
}

// FUNC_AT(0x00116280)
void VU0_MATRIX4_vect4mult(const void *in, const void *m, void *out) {
    S4(out, Vect4(L(in), m));
}

// AUTOINJECT
void VU0_MATRIX4_vect3rotate(const void *in, const void *m, void *out) {
    Q v = L(in);
    Q a = _mm_mul_ps(Lane(v, 0), Row(m, 0)), b = _mm_mul_ps(Lane(v, 1), Row(m, 1));
    Q c = _mm_mul_ps(Lane(v, 2), Row(m, 2));
    a = _mm_add_ps(a, b);
    S3(out, _mm_add_ps(a, c));
}

// count vectors; as the original's DEC/JNZ loop, a count of 0 runs 2^32 times.
// FUNC_AT(0x00116310)
void VU0_MATRIX4_vect4multarray(const void *in, const void *m, void *out, int count) {
    const float *p = (const float *)in;
    float *o = (float *)out;
    uint32_t n = (uint32_t)count;
    do {
        S4(o, Vect4(L(p), m));
        p += 4;
        o += 4;
    } while (--n != 0);
}

// out = ((x*r0 + y*r1) + z*r2) - r3, all four lanes.
// FUNC_AT(0x001163f0)
void VU0_MATRIX4_vect3multsub(const void *in, const void *m, void *out) {
    Q v = L(in);
    Q a = _mm_mul_ps(Row(m, 0), Lane(v, 0)), b = _mm_mul_ps(Row(m, 1), Lane(v, 1));
    Q c = _mm_mul_ps(Row(m, 2), Lane(v, 2));
    a = _mm_add_ps(a, b);
    a = _mm_add_ps(a, c);
    S4(out, _mm_sub_ps(a, Row(m, 3)));
}

// ---- quaternions

// Spherical interpolation the fast way: a normalised lerp along the shorter arc. 1 - t and 1 / length are x87
// operations on floats, stored as floats, which is the float operation's result.
// AUTOINJECT
void VU0_fastqslerp(const void *q0, const void *q1, void *out, float t) {
    Q a0 = L(q0), a1 = L(q1);
    Q d = _mm_sub_ps(a1, a0), s = _mm_add_ps(a1, a0);
    Q A = _mm_mul_ps(Splat(t), a1);
    float omt = (float)(1.0 - (double)t);
    Q B = _mm_mul_ps(Splat(omt), a0);
    float dd = F0(Sum4(_mm_mul_ps(d, d))), ss = F0(Sum4(_mm_mul_ps(s, s)));
    Q r = (dd <= ss) ? _mm_add_ps(A, B) : _mm_sub_ps(A, B);   // unordered takes the subtraction
    S4(out, r);
    r = L(out);
    float len = F0(_mm_sqrt_ss(Sum4(_mm_mul_ps(r, r))));
    float k = (float)(1.0 / (double)len);
    S4(out, _mm_mul_ps(Splat(k), r));
}

// v rotated by q: t = q.w*v + q x v; out = (q.w*t + q*(q.v)) - t x q, keeping out.w.
static inline Q QuatRotate(const void *q, const void *v) {
    Q a = L(q), b = L(v);
    float w = ((const float *)q)[3];
    Q t = _mm_add_ps(_mm_mul_ps(Splat(w), b), Cross(a, b));
    Q u = Cross(t, a);
    float d = F0(Sum3(_mm_mul_ps(a, b)));
    Q p = _mm_mul_ps(Splat(d), a);   // VU0_v4scale: x, y, z only, the rest never stored
    p = _mm_add_ps(_mm_mul_ps(Splat(w), t), p);
    return _mm_sub_ps(p, u);
}

// AUTOINJECT
void VU0_v4quatrotate(const void *q, const void *v, void *out) {
    S3(out, QuatRotate(q, v));
}

// The rotation, then out.xyz += t.
// AUTOINJECT
void VU0_v4quatrotate_xlate(const void *q, const void *v, const void *t, void *out) {
    S3(out, QuatRotate(q, v));
    S3(out, _mm_add_ps(L(out), L(t)));
}
