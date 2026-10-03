#include "Transform.h"

#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"

#include <stdint.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGL::Transform: the matrix class EAGL and EAGLAnim build their transforms with (docs/driving/eagl.md 4.7). Each
// method is the original at the same address, ported from it bit for bit: the x87 arithmetic in double in the
// original's order with its float roundings (Invert, the determinants, the quaternion conversions and BuildSQT are
// generated statement for statement from the listing), the products through D3DXMatrixMultiply as the originals'
// are, BuildRotate in the original instructions because it uses FSIN and FCOS unrounded.
// devtools/TransformShadow.cpp compares every method with the original.
// ---------------------------------------------------------------------------------------------------------------

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

typedef float F;

static inline float FloatBits(uint32_t u) {
    float f;
    memcpy(&f, &u, 4);
    return f;
}

// this = a * b through D3DXMatrixMultiply into a temporary, then copied in (the originals' MEM_copy)
static inline void Multiply(Transform *self, const float *a, const float *b) {
    alignas(16) float t[16];
    VU0_MATRIX4_mult(t, a, b);
    MEM_copy(self->m, t, 0x40);
}

// AUTOINJECT
void Transform::PostMult(const float *matrix) {
    Multiply(this, m, matrix);
}

// FUNC_AT(0x000f1590)
void Transform::BuildTranslate(float x, float y, float z) {
    static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    memcpy(m, identity, 48);
    memcpy(&m[12], &x, 4);
    memcpy(&m[13], &y, 4);
    memcpy(&m[14], &z, 4);
    m[15] = 1.0f;
}

// A 3x3 rotation (nine floats) and a translation, copied as integers; the 4-float form copies t.w, the 3-float form
// writes 1.
// FUNC_AT(0x000f1660)
void Transform::BuildRotTrans4(const float *r, const float *t) {
    uint32_t *o = (uint32_t *)m;
    const uint32_t *a = (const uint32_t *)r, *b = (const uint32_t *)t;
    o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; o[3] = 0;
    o[4] = a[3]; o[5] = a[4]; o[6] = a[5]; o[7] = 0;
    o[8] = a[6]; o[9] = a[7]; o[10] = a[8]; o[11] = 0;
    o[12] = b[0]; o[13] = b[1]; o[14] = b[2]; o[15] = b[3];
}

// FUNC_AT(0x000f16d0)
void Transform::BuildRotTrans3(const float *r, const float *t) {
    uint32_t *o = (uint32_t *)m;
    const uint32_t *a = (const uint32_t *)r, *b = (const uint32_t *)t;
    o[0] = a[0]; o[1] = a[1]; o[2] = a[2]; o[3] = 0;
    o[4] = a[3]; o[5] = a[4]; o[6] = a[5]; o[7] = 0;
    o[8] = a[6]; o[9] = a[7]; o[10] = a[8]; o[11] = 0;
    o[12] = b[0]; o[13] = b[1]; o[14] = b[2];
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
    memcpy(&m[aimRow], aim, 12);
    m[upRow] = top[0];
    m[upRow + 1] = top[1];
    m[upRow + 2] = top[2];
    m[3] = m[7] = m[11] = m[12] = m[13] = m[14] = 0.0f;
    m[15] = 1.0f;
}

// AUTOINJECT
void Transform::BuildMatrix(const float *matrix) {
    memcpy(m, matrix, 64);
}

// The rotation as a quaternion and row 3 as the translation (all four words). Ghidra calls it BuildQuatTrans.
// FUNC_AT(0x000f19f0)
void Transform::ExtractQuatTrans(float *quaternion, float *translation) const {
    float r[9] = { m[0], m[1], m[2], m[4], m[5], m[6], m[8], m[9], m[10] };
    EAGL_RotationToQuat(r, quaternion);
    memcpy(translation, &m[12], 16);
}

// In place, as the original swaps (some words through the x87, some as integers).
// AUTOINJECT
void Transform::Transpose() {
    uint32_t *o = (uint32_t *)m;
    uint32_t t;
    t = o[1]; o[1] = o[4]; o[4] = t;
    t = o[2]; o[2] = o[8]; o[8] = t;
    t = o[3]; o[3] = o[12]; o[12] = t;
    t = o[6]; o[6] = o[9]; o[9] = t;
    t = o[7]; o[7] = o[13]; o[13] = t;
    t = o[11]; o[11] = o[14]; o[14] = t;
}

// 0x000f13b0, the determinant Invert and Determinant use (EAX = matrix): the same instructions as the maths
// library's Determinant4x4, which is ours (platform/RealMath.cpp).
static inline double Determinant4(const float *m) {
    return Determinant4x4(m);
}

// destination = the inverse of source, unless the determinant is within 1e-5 of zero (NaN inverts). Answers the
// determinant, unrounded; the callers pop it.
// AUTOINJECT
double Transform::Invert(const float *source, float *destination) {
    const float *m = source;
    float *o = destination;
    double det = Determinant4(source);
    if (det < (double)FloatBits(0x3727c5ac) && !(det <= -(double)FloatBits(0x3727c5ac)))   // +-1e-5
        return det;
    float f1 = m[1];
    double d2 = 1.0 / det;
    float f3 = m[5];
    float f4 = m[9];
    float f5 = m[2];
    float f6 = m[13];
    float f7 = m[10];
    float f8 = m[6];
    float f9 = m[3];
    float f10 = m[14];
    float f11 = m[7];
    float f12 = m[11];
    double d13 = (double)m[0];
    double d14 = (double)m[4];
    double d15 = (double)m[8];
    double d16 = (double)m[12];
    float f17 = m[15];
    double d18 = (double)f17 * (double)f7;
    double d19 = (double)f12 * (double)f10;
    double d20 = d18 - d19;
    float f21 = (float)d20;
    double d22 = (double)f11 * (double)f10;
    float f23 = (float)d22;
    double d24 = (double)f17 * (double)f8;
    float f25 = (float)d24;
    double d26 = (double)f23 - (double)f25;
    float f27 = (float)d26;
    double d28 = (double)f12 * (double)f8;
    double d29 = (double)f11 * (double)f7;
    double d30 = d28 - d29;
    float f31 = (float)d30;
    double d32 = d30 * (double)f6;
    double d33 = (double)f27 * (double)f4;
    double d34 = d32 + d33;
    double d35 = (double)f21 * (double)f3;
    double d36 = d34 + d35;
    double d37 = d36 * d2;
    float f38 = (float)d37;
    o[0] = f38;
    double d39 = (double)f9 * (double)f10;
    double d40 = (double)f17 * (double)f5;
    double d41 = d39 - d40;
    float f42 = (float)d41;
    double d43 = (double)f12 * (double)f5;
    float f44 = (float)d43;
    double d45 = (double)f9 * (double)f7;
    float f46 = (float)d45;
    double d47 = (double)f44 - (double)f46;
    float f48 = (float)d47;
    double d49 = d47 * (double)f6;
    double d50 = (double)f42 * (double)f4;
    double d51 = d49 + d50;
    double d52 = (double)f21 * (double)f1;
    double d53 = d51 + d52;
    double d54 = -d53;
    double d55 = d54 * d2;
    float f56 = (float)d55;
    o[1] = f56;
    double d57 = (double)f25 - (double)f23;
    float f58 = (float)d57;
    double d59 = (double)f11 * (double)f5;
    double d60 = (double)f9 * (double)f8;
    double d61 = d59 - d60;
    float f62 = (float)d61;
    double d63 = d61 * (double)f6;
    double d64 = (double)f58 * (double)f1;
    double d65 = d63 + d64;
    double d66 = (double)f42 * (double)f3;
    double d67 = d65 + d66;
    double d68 = d67 * d2;
    float f69 = (float)d68;
    o[2] = f69;
    double d70 = (double)f46 - (double)f44;
    float f71 = (float)d70;
    double d72 = d70 * (double)f3;
    double d73 = (double)f62 * (double)f4;
    double d74 = d72 + d73;
    double d75 = (double)f31 * (double)f1;
    double d76 = d74 + d75;
    double d77 = -d76;
    double d78 = d77 * d2;
    float f79 = (float)d78;
    o[3] = f79;
    double d80 = (double)f31 * d16;
    double d81 = (double)f27 * d15;
    double d82 = d80 + d81;
    double d83 = (double)f21 * d14;
    double d84 = d82 + d83;
    double d85 = -d84;
    double d86 = d85 * d2;
    float f87 = (float)d86;
    o[4] = f87;
    double d88 = (double)f48 * d16;
    double d89 = (double)f42 * d15;
    double d90 = d88 + d89;
    double d91 = (double)f21 * d13;
    double d92 = d90 + d91;
    double d93 = d92 * d2;
    float f94 = (float)d93;
    o[5] = f94;
    double d95 = (double)f62 * d16;
    double d96 = (double)f58 * d13;
    double d97 = d95 + d96;
    double d98 = (double)f42 * d14;
    double d99 = d97 + d98;
    double d100 = -d99;
    double d101 = d100 * d2;
    float f102 = (float)d101;
    o[6] = f102;
    double d103 = (double)f71 * d14;
    double d104 = (double)f62 * d15;
    double d105 = d103 + d104;
    double d106 = (double)f31 * d13;
    double d107 = d105 + d106;
    double d108 = d107 * d2;
    float f109 = (float)d108;
    o[7] = f109;
    double d110 = (double)f17 * (double)f4;
    double d111 = (double)f12 * (double)f6;
    double d112 = d110 - d111;
    float f113 = (float)d112;
    double d114 = (double)f11 * (double)f6;
    float f115 = (float)d114;
    double d116 = (double)f17 * (double)f3;
    float f117 = (float)d116;
    double d118 = (double)f12 * (double)f3;
    double d119 = (double)f11 * (double)f4;
    double d120 = d118 - d119;
    float f121 = (float)d120;
    double d122 = (double)f115 - (double)f117;
    double d123 = d122 * d15;
    double d124 = (double)f121 * d16;
    double d125 = d123 + d124;
    double d126 = (double)f113 * d14;
    double d127 = d125 + d126;
    double d128 = d127 * d2;
    float f129 = (float)d128;
    o[8] = f129;
    double d130 = (double)f9 * (double)f6;
    double d131 = (double)f17 * (double)f1;
    double d132 = d130 - d131;
    float f133 = (float)d132;
    double d134 = (double)f12 * (double)f1;
    float f135 = (float)d134;
    double d136 = (double)f9 * (double)f4;
    float f137 = (float)d136;
    double d138 = (double)f135 - (double)f137;
    double d139 = d138 * d16;
    double d140 = (double)f133 * d15;
    double d141 = d139 + d140;
    double d142 = (double)f113 * d13;
    double d143 = d141 + d142;
    double d144 = -d143;
    double d145 = d144 * d2;
    float f146 = (float)d145;
    o[9] = f146;
    double d147 = (double)f11 * (double)f1;
    double d148 = (double)f9 * (double)f3;
    double d149 = d147 - d148;
    float f150 = (float)d149;
    double d151 = (double)f117 - (double)f115;
    double d152 = d151 * d13;
    double d153 = (double)f150 * d16;
    double d154 = d152 + d153;
    double d155 = (double)f133 * d14;
    double d156 = d154 + d155;
    double d157 = d156 * d2;
    float f158 = (float)d157;
    o[10] = f158;
    double d159 = (double)f137 - (double)f135;
    double d160 = d159 * d14;
    double d161 = (double)f150 * d15;
    double d162 = d160 + d161;
    double d163 = (double)f121 * d13;
    double d164 = d162 + d163;
    double d165 = -d164;
    double d166 = d165 * d2;
    float f167 = (float)d166;
    o[11] = f167;
    double d168 = (double)f10 * (double)f4;
    double d169 = (double)f7 * (double)f6;
    double d170 = d168 - d169;
    float f171 = (float)d170;
    double d172 = (double)f8 * (double)f6;
    float f173 = (float)d172;
    double d174 = (double)f10 * (double)f3;
    float f175 = (float)d174;
    double d176 = (double)f7 * (double)f3;
    double d177 = (double)f8 * (double)f4;
    double d178 = d176 - d177;
    float f179 = (float)d178;
    double d180 = (double)f173 - (double)f175;
    double d181 = d180 * d15;
    double d182 = (double)f179 * d16;
    double d183 = d181 + d182;
    double d184 = (double)f171 * d14;
    double d185 = d183 + d184;
    double d186 = -d185;
    double d187 = d186 * d2;
    float f188 = (float)d187;
    o[12] = f188;
    double d189 = (double)f5 * (double)f6;
    double d190 = (double)f10 * (double)f1;
    double d191 = d189 - d190;
    float f192 = (float)d191;
    double d193 = (double)f7 * (double)f1;
    float f194 = (float)d193;
    double d195 = (double)f5 * (double)f4;
    float f196 = (float)d195;
    double d197 = (double)f194 - (double)f196;
    double d198 = d197 * d16;
    double d199 = (double)f192 * d15;
    double d200 = d198 + d199;
    double d201 = (double)f171 * d13;
    double d202 = d200 + d201;
    double d203 = d202 * d2;
    float f204 = (float)d203;
    o[13] = f204;
    double d205 = (double)f8 * (double)f1;
    double d206 = (double)f5 * (double)f3;
    double d207 = d205 - d206;
    float f208 = (float)d207;
    double d209 = (double)f175 - (double)f173;
    double d210 = d209 * d13;
    double d211 = (double)f208 * d16;
    double d212 = d210 + d211;
    double d213 = (double)f192 * d14;
    double d214 = d212 + d213;
    double d215 = -d214;
    double d216 = d215 * d2;
    float f217 = (float)d216;
    o[14] = f217;
    double d218 = (double)f196 - (double)f194;
    double d219 = d218 * d14;
    double d220 = (double)f208 * d15;
    double d221 = d219 + d220;
    double d222 = (double)f179 * d13;
    double d223 = d221 + d222;
    double d224 = d223 * d2;
    float f225 = (float)d224;
    o[15] = f225;
    return det;
}

// The 3x3 case of Determinant (generated from 0x000f27bb..0x000f2801).
static double Determinant3(const float *m) {
    double d1 = (double)m[4] * (double)m[9];
    double d2 = (double)m[5] * (double)m[8];
    double d3 = d1 - d2;
    double d4 = d3 * (double)m[2];
    double d5 = (double)m[6] * (double)m[8];
    double d6 = (double)m[4] * (double)m[10];
    double d7 = d5 - d6;
    double d8 = d7 * (double)m[1];
    double d9 = d4 + d8;
    double d10 = (double)m[5] * (double)m[10];
    double d11 = (double)m[6] * (double)m[9];
    double d12 = d10 - d11;
    double d13 = d12 * (double)m[0];
    return d9 + d13;
}

// The minor that leaves out one row and one column, rows four floats apart as in a MATRIX4.
static void Minor(const float *matrix, int row, int column, int size, float *minor) {
    const uint32_t *s = (const uint32_t *)matrix;
    uint32_t *d = (uint32_t *)minor;
    int rowOffset = 0;
    for (int r = 0; r < size; r++) {
        if (r == row)
            continue;
        int c2 = 0;
        for (int c = 0; c < size; c++) {
            if (c == column)
                continue;
            d[rowOffset + c2++] = s[c + r * 4];
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
        result = (Determinant(minor, size - 1) * matrix[column]) * sign + sum;
        sum = (F)result;
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
    memset(b, 0, 64);
    memcpy(&b[0], &x, 4);
    memcpy(&b[5], &y, 4);
    memcpy(&b[10], &z, 4);
    memcpy(&b[15], &w, 4);
}

static inline void Translation(float *b, float x, float y, float z) {
    static const float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    memcpy(b, identity, 64);
    memcpy(&b[12], &x, 4);
    memcpy(&b[13], &y, 4);
    memcpy(&b[14], &z, 4);
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
    memcpy(b, matrix, 64);
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
    memcpy(b, matrix, 64);
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
    memcpy(copy, m, 64);
    Invert(copy, m);
}

// ---- quaternions

// (generated from 0x000f3090)
// FUNC_AT(0x000f3090)
void EAGL_QuatToRotation(const float *q, float *out) {
    double d1 = (double)q[0] + (double)q[0];
    double d2 = (double)q[1] + (double)q[1];
    double d3 = (double)q[2] + (double)q[2];
    float f4 = (float)d3;
    double d5 = d1 * (double)q[3];
    float f6 = (float)d5;
    double d7 = d2 * (double)q[3];
    float f8 = (float)d7;
    double d9 = (double)f4 * (double)q[3];
    float f10 = (float)d9;
    double d11 = d1 * (double)q[0];
    float f12 = (float)d11;
    double d13 = d2 * (double)q[0];
    float f14 = (float)d13;
    double d15 = (double)f4 * (double)q[0];
    float f16 = (float)d15;
    double d17 = d2 * (double)q[1];
    double d18 = (double)f4 * (double)q[1];
    double d19 = (double)f4 * (double)q[2];
    double d20 = d19 + d17;
    double d21 = 1.0 - d20;
    float f22 = (float)d21;
    double d23 = (double)f14 - (double)f10;
    float f24 = (float)d23;
    double d25 = (double)f16 + (double)f8;
    float f26 = (float)d25;
    double d27 = (double)f14 + (double)f10;
    float f28 = (float)d27;
    double d29 = d19 + (double)f12;
    double d30 = 1.0 - d29;
    float f31 = (float)d30;
    double d32 = d18 - (double)f6;
    float f33 = (float)d32;
    double d34 = (double)f16 - (double)f8;
    float f35 = (float)d34;
    double d36 = d18 + (double)f6;
    float f37 = (float)d36;
    double d38 = d17 + (double)f12;
    double d39 = 1.0 - d38;
    float f40 = (float)d39;
    out[0] = f22;
    out[3] = f24;
    out[6] = f26;
    out[1] = f28;
    out[4] = f31;
    out[7] = f33;
    out[2] = f35;
    out[5] = f37;
    out[8] = f40;
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
    double d1 = (double)qx + (double)qx;
    double d2 = (double)qy + (double)qy;
    double d3 = (double)qz + (double)qz;
    float f4 = (float)d3;
    double d5 = d1 * (double)qw;
    float f6 = (float)d5;
    double d7 = (double)qw * d2;
    float f8 = (float)d7;
    double d9 = (double)f4 * (double)qw;
    float f10 = (float)d9;
    double d11 = d1 * (double)qx;
    float f12 = (float)d11;
    double d13 = (double)qx * d2;
    float f14 = (float)d13;
    double d15 = (double)f4 * (double)qx;
    float f16 = (float)d15;
    double d17 = d2 * (double)qy;
    double d18 = (double)f4 * (double)qy;
    double d19 = (double)f4 * (double)qz;
    o[3] = 0.0f;
    o[7] = 0.0f;
    double d20 = d19 + d17;
    double d21 = 1.0 - d20;
    o[0] = (float)(d21 * (double)sx);
    o[4] = (float)(((double)f14 - (double)f10) * (double)sy);
    o[8] = (float)(((double)f16 + (double)f8) * (double)sz);
    o[1] = (float)(((double)f14 + (double)f10) * (double)sx);
    o[5] = (float)((1.0 - (d19 + (double)f12)) * (double)sy);
    o[9] = (float)((d18 - (double)f6) * (double)sz);
    o[2] = (float)(((double)f16 - (double)f8) * (double)sx);
    o[6] = (float)((d18 + (double)f6) * (double)sy);
    o[10] = (float)((1.0 - (d17 + (double)f12)) * (double)sz);
    o[11] = 0.0f;
    memcpy(&o[12], &tx, 4);
    memcpy(&o[13], &ty, 4);
    memcpy(&o[14], &tz, 4);
    o[15] = 1.0f;
}

// out = in (x, y, z, 1) through the matrix, three floats; out may be in.
// AUTOINJECT
void Transform::TransformPoint(const float *in, float *out) const {
    float x = (F)((((double)m[8] * in[2] + (double)m[4] * in[1]) + (double)in[0] * m[0]) + m[12]);
    float y = (F)((((double)m[1] * in[0] + (double)m[9] * in[2]) + (double)m[5] * in[1]) + m[13]);
    float z = (F)((((double)m[2] * in[0] + (double)m[10] * in[2]) + (double)m[6] * in[1]) + m[14]);
    if (in != out) {
        out[0] = x;
        out[1] = (F)((((double)m[1] * in[0] + (double)m[9] * in[2]) + (double)m[5] * in[1]) + m[13]);
        out[2] = (F)((((double)m[2] * in[0] + (double)m[10] * in[2]) + (double)m[6] * in[1]) + m[14]);
        return;
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
}
