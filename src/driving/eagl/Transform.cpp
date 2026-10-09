#include "Transform.h"

#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"

#include <bit>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <utility>

// ---------------------------------------------------------------------------------------------------------------
// EAGL::Transform: the matrix class EAGL and EAGLAnim build their transforms with (docs/driving/eagl.md 4.7). Each
// method is the original at the same address, ported from it bit for bit: the x87 arithmetic in double in the
// original's order with its float roundings (Invert, the determinants, the quaternion conversions and BuildSQT are
// generated statement for statement from the listing, then tidied: a double intermediate used once is written
// inline, and one operation on floats rounded straight to a float is written in float, which gives the same bits),
// the products through D3DXMatrixMultiply as the originals' are, BuildRotate in the original instructions because it
// uses FSIN and FCOS unrounded. devtools/MathShadow.cpp compares every method with the original.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

static const float kIdentity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };

// this = a * b through D3DXMatrixMultiply into a temporary, then copied in (the originals' MEM_copy)
static inline void Multiply(Transform *self, const float *a, const float *b) {
    alignas(16) float t[16];
    VU0_MATRIX4_mult(t, a, b);
    MEM_copy(self->m, t, sizeof(t));
}

// AUTOINJECT
void Transform::PostMult(const float *matrix) {
    Multiply(this, m, matrix);
}

// FUNC_AT(0x000f1590)
void Transform::BuildTranslate(float x, float y, float z) {
    memcpy(m, kIdentity, 12 * sizeof(float));   // rows 0..2
    m[12] = x;
    m[13] = y;
    m[14] = z;
    m[15] = 1.0f;
}

// A 3x3 rotation (nine floats) and a translation; the 4-float form copies t.w, the 3-float form writes 1.
// FUNC_AT(0x000f1660)
void Transform::BuildRotTrans4(const float *r, const float *t) {
    m[0] = r[0]; m[1] = r[1]; m[2] = r[2]; m[3] = 0.0f;
    m[4] = r[3]; m[5] = r[4]; m[6] = r[5]; m[7] = 0.0f;
    m[8] = r[6]; m[9] = r[7]; m[10] = r[8]; m[11] = 0.0f;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2]; m[15] = t[3];
}

// FUNC_AT(0x000f16d0)
void Transform::BuildRotTrans3(const float *r, const float *t) {
    m[0] = r[0]; m[1] = r[1]; m[2] = r[2]; m[3] = 0.0f;
    m[4] = r[3]; m[5] = r[4]; m[6] = r[5]; m[7] = 0.0f;
    m[8] = r[6]; m[9] = r[7]; m[10] = r[8]; m[11] = 0.0f;
    m[12] = t[0]; m[13] = t[1]; m[14] = t[2];
    m[15] = 1.0f;
}

// Rows from an aim and an up vector: the aim in row aimRow (element offsets 0, 4 or 8), the unit (up x aim) in the
// remaining row, the unit (aim x that) in row upRow; no translation.
// AUTOINJECT
void Transform::BuildAimedTrans(const float *aim, const float *up, int aimRow, int upRow) {
    int third = 0xc - aimRow - upRow;
    float side[3], top[3];
    v3unitcrossprod(up, aim, side);
    v3unitcrossprod(aim, side, top);
    m[third] = side[0];
    m[third + 1] = side[1];
    m[third + 2] = side[2];
    memcpy(&m[aimRow], aim, 3 * sizeof(float));
    m[upRow] = top[0];
    m[upRow + 1] = top[1];
    m[upRow + 2] = top[2];
    m[3] = m[7] = m[11] = m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

// AUTOINJECT
void Transform::BuildMatrix(const float *matrix) {
    memcpy(m, matrix, sizeof(m));
}

// The rotation as a quaternion and row 3 as the translation (all four words). Ghidra calls it BuildQuatTrans.
// FUNC_AT(0x000f19f0)
void Transform::ExtractQuatTrans(float *quaternion, float *translation) const {
    float r[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] };
    EAGL_RotationToQuat(r, quaternion);
    memcpy(translation, &m[12], 4 * sizeof(float));
}

// In place.
// AUTOINJECT
void Transform::Transpose() {
    std::swap(m[1], m[4]);
    std::swap(m[2], m[8]);
    std::swap(m[3], m[12]);
    std::swap(m[6], m[9]);
    std::swap(m[7], m[13]);
    std::swap(m[11], m[14]);
}

// 0x000f13b0, the determinant Invert and Determinant use (EAX = matrix): the same instructions as the maths
// library's Determinant4x4, which is ours (platform/RealMath.cpp).
static inline double Determinant4(const float *m) {
    return Determinant4x4(m);
}

constexpr float kSingular = 1e-5f;   // Invert leaves the destination alone for a determinant this close to 0
static_assert(std::bit_cast<uint32_t>(kSingular) == 0x3727c5ac, "the original's 1e-5");

// destination = the inverse of source, unless the determinant is within 1e-5 of zero (NaN inverts). Answers the
// determinant, unrounded; the callers pop it. source may be destination: every element is read before the first
// store. Column 0 stays on the x87 stack (double) throughout; the rest is read as floats.
// AUTOINJECT
double Transform::Invert(const float *source, float *destination) {
    const float *m = source;
    float *o = destination;
    double det = Determinant4(source);
    if (det < kSingular && !(det <= -kSingular))
        return det;
    double e0 = m[0];
    float e1 = m[1];
    float e2 = m[2];
    float e3 = m[3];
    double e4 = m[4];
    float e5 = m[5];
    float e6 = m[6];
    float e7 = m[7];
    double e8 = m[8];
    float e9 = m[9];
    float e10 = m[10];
    float e11 = m[11];
    double e12 = m[12];
    float e13 = m[13];
    float e14 = m[14];
    float e15 = m[15];
    double invDet = 1.0 / det;
    float f21 = float(double(e15) * e10 - double(e11) * e14);
    float f23 = e7 * e14;
    float f25 = e15 * e6;
    float f27 = f23 - f25;
    double d30 = double(e11) * e6 - double(e7) * e10;
    float f31 = float(d30);
    o[0] = float((d30 * e13 + double(f27) * e9 + double(f21) * e5) * invDet);
    float f42 = float(double(e3) * e14 - double(e15) * e2);
    float f44 = e11 * e2;
    float f46 = e3 * e10;
    double d47 = double(f44) - f46;
    float f48 = float(d47);
    o[1] = float(-(d47 * e13 + double(f42) * e9 + double(f21) * e1) * invDet);
    float f58 = f25 - f23;
    double d61 = double(e7) * e2 - double(e3) * e6;
    float f62 = float(d61);
    o[2] = float((d61 * e13 + double(f58) * e1 + double(f42) * e5) * invDet);
    double d70 = double(f46) - f44;
    float f71 = float(d70);
    o[3] = float(-(d70 * e5 + double(f62) * e9 + double(f31) * e1) * invDet);
    o[4] = float(-(f31 * e12 + f27 * e8 + f21 * e4) * invDet);
    o[5] = float((f48 * e12 + f42 * e8 + f21 * e0) * invDet);
    o[6] = float(-(f62 * e12 + f58 * e0 + f42 * e4) * invDet);
    o[7] = float((f71 * e4 + f62 * e8 + f31 * e0) * invDet);
    float f113 = float(double(e15) * e9 - double(e11) * e13);
    float f115 = e7 * e13;
    float f117 = e15 * e5;
    float f121 = float(double(e11) * e5 - double(e7) * e9);
    o[8] = float(((double(f115) - f117) * e8 + f121 * e12 + f113 * e4) * invDet);
    float f133 = float(double(e3) * e13 - double(e15) * e1);
    float f135 = e11 * e1;
    float f137 = e3 * e9;
    o[9] = float(-((double(f135) - f137) * e12 + f133 * e8 + f113 * e0) * invDet);
    float f150 = float(double(e7) * e1 - double(e3) * e5);
    o[10] = float(((double(f117) - f115) * e0 + f150 * e12 + f133 * e4) * invDet);
    o[11] = float(-((double(f137) - f135) * e4 + f150 * e8 + f121 * e0) * invDet);
    float f171 = float(double(e14) * e9 - double(e10) * e13);
    float f173 = e6 * e13;
    float f175 = e14 * e5;
    float f179 = float(double(e10) * e5 - double(e6) * e9);
    o[12] = float(-((double(f173) - f175) * e8 + f179 * e12 + f171 * e4) * invDet);
    float f192 = float(double(e2) * e13 - double(e14) * e1);
    float f194 = e10 * e1;
    float f196 = e2 * e9;
    o[13] = float(((double(f194) - f196) * e12 + f192 * e8 + f171 * e0) * invDet);
    float f208 = float(double(e6) * e1 - double(e2) * e5);
    o[14] = float(-((double(f175) - f173) * e0 + f208 * e12 + f192 * e4) * invDet);
    o[15] = float(((double(f196) - f194) * e4 + f208 * e8 + f179 * e0) * invDet);
    return det;
}

// The 3x3 case of Determinant (generated from 0x000f27bb..0x000f2801).
static double Determinant3(const float *m) {
    return (double(m[4]) * m[9] - double(m[5]) * m[8]) * m[2] + (double(m[6]) * m[8] - double(m[4]) * m[10]) * m[1] +
           (double(m[5]) * m[10] - double(m[6]) * m[9]) * m[0];
}

// The minor that leaves out one row and one column, rows four floats apart as in a MATRIX4.
static void Minor(const float *matrix, int row, int column, int size, float *minor) {
    int rowOffset = 0;
    for (int r = 0; r < size; r++) {
        if (r == row)
            continue;
        int c2 = 0;
        for (int c = 0; c < size; c++) {
            if (c == column)
                continue;
            minor[rowOffset + c2++] = matrix[c + r * 4];
        }
        rowOffset += 4;
    }
}

// The determinant of the size x size matrix (rows four floats apart): 4 and 3 directly, 1 trivially, any other by
// expansion along row 0 - each term rounded into a float running sum, the last one returned unrounded.
// AUTOINJECT
double Transform::Determinant(const float *matrix, int size) {
    if (size == 4)
        return Determinant4(matrix);
    if (size == 3)
        return Determinant3(matrix);
    if (size == 1)
        return matrix[0];
    float sum = 0.0f, sign = 1.0f;
    double result = 0.0;
    for (int column = 0; column < size; column++) {
        float minor[64];
        Minor(matrix, 0, column, size, minor);
        result = Determinant(minor, size - 1) * matrix[column] * sign + sum;
        sum = float(result);
        sign = -sign;
    }
    return result;
}

// The signed minor of an element.
// AUTOINJECT
double Transform::ElementMinor(const float *matrix, int row, int column, int size) {
    float sign = ((row + column) & 1) ? -1.0f : 1.0f;
    float minor[64];
    Minor(matrix, row, column, size, minor);
    return Determinant(minor, size - 1) * sign;
}

// ---- Append (this = this * B) and Prepend (this = B * this), B built locally

static inline void Diagonal(float *b, float x, float y, float z, float w) {
    memset(b, 0, 16 * sizeof(float));
    b[0] = x;
    b[5] = y;
    b[10] = z;
    b[15] = w;
}

static inline void Translation(float *b, float x, float y, float z) {
    memcpy(b, kIdentity, sizeof(kIdentity));
    b[12] = x;
    b[13] = y;
    b[14] = z;
}

// AUTOINJECT
void Transform::AppendScale(float x, float y, float z, float w) {
    alignas(16) float b[16];
    Diagonal(b, x, y, z, w);
    Multiply(this, m, b);
}

// AUTOINJECT
void Transform::AppendTranslate(float x, float y, float z) {
    alignas(16) float b[16];
    Translation(b, x, y, z);
    Multiply(this, m, b);
}

// FUNC_AT(0x000f2ae0)
void Transform::AppendRotTrans4(const float *r, const float *t) {
    alignas(16) Transform b;
    b.BuildRotTrans4(r, t);
    Multiply(this, m, b.m);
}

// FUNC_AT(0x000f2b30)
void Transform::AppendRotTrans3(const float *r, const float *t) {
    alignas(16) Transform b;
    b.BuildRotTrans3(r, t);
    Multiply(this, m, b.m);
}

// AUTOINJECT
void Transform::AppendMatrix(const float *matrix) {
    alignas(16) float b[16];
    memcpy(b, matrix, sizeof(b));
    Multiply(this, m, b);
}

// AUTOINJECT
void Transform::AppendQuatTrans(const float *quaternion, const float *translation) {
    float r[9];
    EAGL_QuatToRotation(quaternion, r);
    alignas(16) Transform b;
    b.BuildRotTrans4(r, translation);
    Multiply(this, m, b.m);
}

// AUTOINJECT
void Transform::AppendAimedTrans(const float *aim, const float *up, int aimRow, int upRow) {
    alignas(16) Transform b;
    b.BuildAimedTrans(aim, up, aimRow, upRow);
    Multiply(this, m, b.m);
}

// AUTOINJECT
void Transform::AppendRotate(float degrees, float x, float y, float z) {
    alignas(16) Transform b;
    EAGL_BuildRotate(&b, 0, degrees, x, y, z);
    Multiply(this, m, b.m);
}

// AUTOINJECT
void Transform::PrependScale(float x, float y, float z, float w) {
    alignas(16) float b[16];
    Diagonal(b, x, y, z, w);
    Multiply(this, b, m);
}

// AUTOINJECT
void Transform::PrependTranslate(float x, float y, float z) {
    alignas(16) float b[16];
    Translation(b, x, y, z);
    Multiply(this, b, m);
}

// AUTOINJECT
void Transform::PrependRotTrans(const float *r, const float *t) {
    alignas(16) Transform b;
    b.BuildRotTrans4(r, t);
    Multiply(this, b.m, m);
}

// AUTOINJECT
void Transform::PrependMatrix(const float *matrix) {
    alignas(16) float b[16];
    memcpy(b, matrix, sizeof(b));
    Multiply(this, b, m);
}

// AUTOINJECT
void Transform::PrependQuatTrans(const float *quaternion, const float *translation) {
    float r[9];
    EAGL_QuatToRotation(quaternion, r);
    alignas(16) Transform b;
    b.BuildRotTrans4(r, translation);
    Multiply(this, b.m, m);
}

// AUTOINJECT
void Transform::PrependRotate(float degrees, float x, float y, float z) {
    alignas(16) Transform b;
    EAGL_BuildRotate(&b, 0, degrees, x, y, z);
    Multiply(this, b.m, m);
}

// In place, through a copy; the determinant is dropped.
// AUTOINJECT
void Transform::Inverse() {
    alignas(16) float copy[16];
    memcpy(copy, m, sizeof(copy));
    Invert(copy, m);
}

// ---- quaternions

// (generated from 0x000f3090) Everything is read before the first store.
// FUNC_AT(0x000f3090)
void EAGL_QuatToRotation(const float *q, float *out) {
    double d1 = double(q[0]) + q[0];
    double d2 = double(q[1]) + q[1];
    float f4 = q[2] + q[2];
    float f6 = float(d1 * q[3]);
    float f8 = float(d2 * q[3]);
    float f10 = f4 * q[3];
    float f12 = float(d1 * q[0]);
    float f14 = float(d2 * q[0]);
    float f16 = f4 * q[0];
    double d17 = d2 * q[1];
    double d18 = double(f4) * q[1];
    double d19 = double(f4) * q[2];
    out[0] = float(1.0 - (d19 + d17));
    out[3] = f14 - f10;
    out[6] = f16 + f8;
    out[1] = f14 + f10;
    out[4] = float(1.0 - (d19 + f12));
    out[7] = float(d18 - f6);
    out[2] = f16 - f8;
    out[5] = float(d18 + f6);
    out[8] = float(1.0 - (d17 + f12));
}

// The same code as the maths library's 0x00115440, with stack arguments.
// FUNC_AT(0x000f3160)
void EAGL_RotationToQuat(const float *rotation9, float *quaternion) {
    RealQuatFromRot(rotation9, quaternion);
}

// ---- BuildRotate: the original instructions (FSIN and FCOS are used unrounded)

static const float kPi = 3.14159274f;            // 0x40490fdb
static const float kOneOver180 = 0.00555555569f; // 0x3bb60b61
static const float kZero = 0.0f;
static const float kOne = 1.0f;

// FUNC_AT(0x000f32e0)
__declspec(naked) void __fastcall EAGL_BuildRotate(Transform *self, int unusedEdx, float degrees, float x, float y,
                                                   float z) {
    __asm {
        fld dword ptr [kPi]
        sub esp, 0xc
        fmul dword ptr [kOneOver180]
        fmul dword ptr [esp + 0x10]
        fst dword ptr [esp + 0x10]
        fcomp dword ptr [kZero]
        fnstsw ax
        test ah, 0x44
        jp Rotate
        xor eax, eax
        mov edx, 0x3f800000
        mov dword ptr [ecx], edx
        mov dword ptr [ecx + 4], eax
        mov dword ptr [ecx + 8], eax
        mov dword ptr [ecx + 0x10], eax
        mov dword ptr [ecx + 0x14], edx
        mov dword ptr [ecx + 0x18], eax
        mov dword ptr [ecx + 0x20], eax
        mov dword ptr [ecx + 0x24], eax
        mov dword ptr [ecx + 0x28], edx
        mov dword ptr [ecx + 0x3c], edx
        jmp Finish
    Rotate:
        fld dword ptr [esp + 0x10]
        xor eax, eax
        fsin
        fld dword ptr [esp + 0x10]
        fcos
        fld dword ptr [esp + 0x14]
        fmul dword ptr [esp + 0x14]
        fld dword ptr [esp + 0x18]
        fmul dword ptr [esp + 0x18]
        faddp st(1), st
        fld dword ptr [esp + 0x1c]
        fmul dword ptr [esp + 0x1c]
        faddp st(1), st
        fsqrt
        fdivr dword ptr [kOne]
        fld dword ptr [esp + 0x14]
        fmul st, st(1)
        fld st(1)
        fmul dword ptr [esp + 0x18]
        fstp dword ptr [esp + 0x18]
        fxch st(1)
        fmul dword ptr [esp + 0x1c]
        fstp dword ptr [esp + 0x1c]
        fld dword ptr [kOne]
        fsub st, st(2)
        fld st(0)
        fmul st, st(2)
        fld st(1)
        fmul dword ptr [esp + 0x18]
        fstp dword ptr [esp + 0x10]
        fxch st(1)
        fmul dword ptr [esp + 0x1c]
        fstp dword ptr [esp + 0x14]
        fld st(3)
        fmul st, st(2)
        fstp dword ptr [esp + 8]
        fld st(3)
        fmul dword ptr [esp + 0x18]
        fstp dword ptr [esp + 4]
        fxch st(3)
        fmul dword ptr [esp + 0x1c]
        fstp dword ptr [esp]
        fxch st(2)
        fld st(0)
        fmul st, st(3)
        fadd st, st(2)
        fstp dword ptr [ecx]
        fld dword ptr [esp + 0x18]
        fmul st, st(1)
        fadd dword ptr [esp]
        fstp dword ptr [ecx + 4]
        fmul dword ptr [esp + 0x1c]
        fsub dword ptr [esp + 4]
        fstp dword ptr [ecx + 8]
        fld dword ptr [esp + 0x10]
        fmul st, st(2)
        fsub dword ptr [esp]
        fstp dword ptr [ecx + 0x10]
        fld dword ptr [esp + 0x10]
        fmul dword ptr [esp + 0x18]
        fadd st, st(1)
        fstp dword ptr [ecx + 0x14]
        fld dword ptr [esp + 0x10]
        fmul dword ptr [esp + 0x1c]
        fadd dword ptr [esp + 8]
        fstp dword ptr [ecx + 0x18]
        fld dword ptr [esp + 0x14]
        fmul st, st(2)
        fadd dword ptr [esp + 4]
        fstp dword ptr [ecx + 0x20]
        fstp st(1)
        fld dword ptr [esp + 0x14]
        fmul dword ptr [esp + 0x18]
        fsub dword ptr [esp + 8]
        fstp dword ptr [ecx + 0x24]
        fld dword ptr [esp + 0x14]
        fmul dword ptr [esp + 0x1c]
        fadd st, st(1)
        fstp dword ptr [ecx + 0x28]
        mov dword ptr [ecx + 0x3c], 0x3f800000
        fstp st(0)
    Finish:
        mov dword ptr [ecx + 0xc], eax
        mov dword ptr [ecx + 0x1c], eax
        mov dword ptr [ecx + 0x2c], eax
        mov dword ptr [ecx + 0x30], eax
        mov dword ptr [ecx + 0x34], eax
        mov dword ptr [ecx + 0x38], eax
        add esp, 0xc
        ret 0x10
    }
}

// ---- BuildSQT and TransformPoint

// Scale, quaternion and translation by value (generated from 0x000f8780..0x000f887b; column c scaled by s[c]).
// AUTOINJECT
void Transform::BuildSQT(float sx, float sy, float sz, float qx, float qy, float qz, float qw, float tx, float ty,
                         float tz) {
    float *o = m;
    double d1 = double(qx) + qx;
    double d2 = double(qy) + qy;
    float f4 = qz + qz;
    float f6 = float(d1 * qw);
    float f8 = float(qw * d2);
    float f10 = f4 * qw;
    float f12 = float(d1 * qx);
    float f14 = float(qx * d2);
    float f16 = f4 * qx;
    double d17 = d2 * qy;
    double d18 = double(f4) * qy;
    double d19 = double(f4) * qz;
    o[3] = 0.0f;
    o[7] = 0.0f;
    o[0] = float((1.0 - (d19 + d17)) * sx);
    o[4] = float((double(f14) - f10) * sy);
    o[8] = float((double(f16) + f8) * sz);
    o[1] = float((double(f14) + f10) * sx);
    o[5] = float((1.0 - (d19 + f12)) * sy);
    o[9] = float((d18 - f6) * sz);
    o[2] = float((double(f16) - f8) * sx);
    o[6] = float((d18 + f6) * sy);
    o[10] = float((1.0 - (d17 + f12)) * sz);
    o[11] = 0.0f;
    o[12] = tx;
    o[13] = ty;
    o[14] = tz;
    o[15] = 1.0f;
}

// out = in (x, y, z, 1) through the matrix, three floats. out may be in; when it is not, y and z are computed again
// after out[0] is stored, as the original does (it matters only if out overlaps in partly).
// AUTOINJECT
void Transform::TransformPoint(const float *in, float *out) const {
    float x = float(double(m[8]) * in[2] + double(m[4]) * in[1] + double(in[0]) * m[0] + m[12]);
    float y = float(double(m[1]) * in[0] + double(m[9]) * in[2] + double(m[5]) * in[1] + m[13]);
    float z = float(double(m[2]) * in[0] + double(m[10]) * in[2] + double(m[6]) * in[1] + m[14]);
    if (in != out) {
        out[0] = x;
        out[1] = float(double(m[1]) * in[0] + double(m[9]) * in[2] + double(m[5]) * in[1] + m[13]);
        out[2] = float(double(m[2]) * in[0] + double(m[10]) * in[2] + double(m[6]) * in[1] + m[14]);
        return;
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// ---- the methods and helpers compiled beside the actors' IK (0x000161a0-0x000163e0, 0x00016530, 0x00016820)

namespace {

// FSIN, FCOS and FPATAN (atan2(y, x)) as the originals use them, their results unrounded
__declspec(naked) double X87Sin(double) {
    __asm {
        fld qword ptr [esp + 4]
        fsin
        ret
    }
}

__declspec(naked) double X87Cos(double) {
    __asm {
        fld qword ptr [esp + 4]
        fcos
        ret
    }
}

__declspec(naked) double X87Atan2(double, double) {
    __asm {
        fld qword ptr [esp + 4]
        fld qword ptr [esp + 12]
        fpatan
        ret
    }
}

} // namespace

// out may be in; when it is not, y and z are computed again after out[0] is stored, as the original does (it
// matters only if out overlaps in partly).
// FUNC_AT(0x000161a0)
void Transform::TransformVector(const float *in, float *out) const {
    float x = float(double(m[8]) * in[2] + double(m[4]) * in[1] + double(in[0]) * m[0]);
    float y = float(double(m[1]) * in[0] + double(m[9]) * in[2] + double(m[5]) * in[1]);
    float z = float(double(m[2]) * in[0] + double(m[10]) * in[2] + double(m[6]) * in[1]);
    if (in != out) {
        out[0] = x;
        out[1] = float(double(m[1]) * in[0] + double(m[9]) * in[2] + double(m[5]) * in[1]);
        out[2] = float(double(m[2]) * in[0] + double(m[10]) * in[2] + double(m[6]) * in[1]);
        return;
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}

// FUNC_AT(0x00016250)
void Transform::GetOrthoInverse(Transform *out) const {
    out->m[0] = m[0];
    out->m[1] = m[4];
    out->m[2] = m[8];
    out->m[4] = m[1];
    out->m[5] = m[5];
    out->m[6] = m[9];
    out->m[8] = m[2];
    out->m[9] = m[6];
    out->m[10] = m[10];
    out->m[12] = float(-(double(m[12]) * m[0]) - double(m[1]) * m[13] - double(m[14]) * m[2]);
    out->m[13] = float(-(double(m[12]) * m[4]) - double(m[5]) * m[13] - double(m[14]) * m[6]);
    out->m[14] = float(-(double(m[12]) * m[8]) - double(m[9]) * m[13] - double(m[10]) * m[14]);
}

// FUNC_AT(0x000162e0)
void Transform::BuildQT(float qx, float qy, float qz, float qw, float tx, float ty, float tz) {
    double x2 = double(qx) + qx;
    double y2 = double(qy) + qy;
    float z2 = float(double(qz) + qz);
    float wx = float(x2 * qw);
    float wy = float(double(qw) * y2);
    float wz = float(double(z2) * qw);
    float xx = float(x2 * qx);
    float xy = float(double(qx) * y2);
    float xz = float(double(z2) * qx);
    double yy = y2 * qy;
    double yz = double(z2) * qy;
    double zz = double(z2) * qz;
    m[0] = float(1.0 - (zz + yy));
    m[1] = xy + wz;
    m[2] = xz - wy;
    m[3] = 0.0f;
    m[4] = xy - wz;
    m[5] = float(1.0 - (zz + xx));
    m[6] = float(yz + wx);
    m[7] = 0.0f;
    m[8] = xz + wy;
    m[9] = float(yz - wx);
    m[10] = float(1.0 - (yy + xx));
    m[11] = 0.0f;
    m[12] = tx;
    m[13] = ty;
    m[14] = tz;
    m[15] = 1.0f;
}

// The angle is compared with 0 before it is rounded; FSIN and FCOS take it rounded.
// FUNC_AT(0x000163e0)
void Transform::BuildRotation(float degrees, float x, float y, float z) {
    double radians = double(kPi) * kOneOver180 * degrees;
    if (radians == 0.0) {
        m[0] = 1.0f;
        m[1] = 0.0f;
        m[2] = 0.0f;
        m[4] = 0.0f;
        m[5] = 1.0f;
        m[6] = 0.0f;
        m[8] = 0.0f;
        m[9] = 0.0f;
        m[10] = 1.0f;
        return;
    }
    float angle = float(radians);
    double s = X87Sin(angle);
    double c = X87Cos(angle);
    double scale = 1.0 / sqrt(double(x) * x + double(y) * y + double(z) * z);
    double ux = x * scale;
    float uy = float(scale * y);
    float uz = float(scale * z);
    double t = 1.0 - c;
    double tx = t * ux;
    float ty = float(t * uy);
    float tz = float(t * uz);
    float sx = float(s * ux);
    float sy = float(s * uy);
    float sz = float(s * uz);
    m[0] = float(tx * ux + c);
    m[1] = float(uy * tx + sz);
    m[2] = float(tx * uz - sy);
    m[4] = float(ty * ux - sz);
    m[5] = float(double(ty) * uy + c);
    m[6] = float(double(ty) * uz + sx);
    m[8] = float(tz * ux + sy);
    m[9] = float(double(tz) * uy - sx);
    m[10] = float(double(tz) * uz + c);
}

// FUNC_AT(0x00016530)
double AngleBetweenVectors(const float *a, const float *b) {
    double x = double(b[2]) * a[1] - double(a[2]) * b[1];
    double y = double(a[2]) * b[0] - double(b[2]) * a[0];
    double z = double(a[0]) * b[1] - double(b[0]) * a[1];
    double sine = sqrt(z * z + y * y + x * x);
    double cosine = double(a[0]) * b[0] + double(a[2]) * b[2] + double(a[1]) * b[1];
    return X87Atan2(sine, cosine);
}

// FUNC_AT(0x00016820)
void QuatProduct(const float *a, const float *b, float *out) {
    out[0] = float(double(a[0]) * b[3] - double(b[2]) * a[1] + double(b[1]) * a[2] + double(a[3]) * b[0]);
    out[1] = float(double(a[1]) * b[3] + double(b[2]) * a[0] - double(a[2]) * b[0] + double(b[1]) * a[3]);
    out[2] = float(double(b[2]) * a[3] + double(a[2]) * b[3] - double(b[1]) * a[0] + double(a[1]) * b[0]);
    out[3] = float(double(a[3]) * b[3] - (double(a[0]) * b[0] + double(b[1]) * a[1] + double(b[2]) * a[2]));
}
