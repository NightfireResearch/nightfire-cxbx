#include "xboxMatrix.h"

#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// Eurocom's matrix helpers (0x000e81a0-0x000e89d0). The matrices are row-major with the translation in the last
// column (_14, _24, _34), so a rigid transform's rotation is the top-left 3x3 and its position is column 3.
// devtools/MatrixShadow.cpp checks each against the original on random matrices.
// ---------------------------------------------------------------------------------------------------------------

// AUTOINJECT
void maybeD3DMATRIXcopy(undefined4 *dest, undefined4 *src) {
    memmove(dest, src, sizeof(D3DMATRIX));
}

// AUTOINJECT
void d3dMatrixIdentity(D3DMATRIX *mtx) {
    memset(mtx, 0, sizeof(*mtx));
    mtx->_11 = mtx->_22 = mtx->_33 = mtx->_44 = 1.0f;
}

// The rotation transposed (inverted, for a pure rotation); the translation column and the bottom row are left.
// AUTOINJECT
void maybeTransposeRotationPart(D3DMATRIX *mtx) {
    float t;
    t = mtx->_12; mtx->_12 = mtx->_21; mtx->_21 = t;
    t = mtx->_13; mtx->_13 = mtx->_31; mtx->_31 = t;
    t = mtx->_23; mtx->_23 = mtx->_32; mtx->_32 = t;
}

// The inverse of a rigid transform: the rotation transposed, and the translation taken back through it.
// AUTOINJECT
void maybeInvertRigidTransform(D3DMATRIX *mtx) {
    maybeTransposeRotationPart(mtx);
    float x = -mtx->_14, y = -mtx->_24, z = -mtx->_34;
    mtx->_34 = mtx->_32 * y + x * mtx->_31 + z * mtx->_33;
    mtx->_14 = y * mtx->_12 + x * mtx->_11 + z * mtx->_13;
    mtx->_24 = z * mtx->_23 + x * mtx->_21 + y * mtx->_22;
}

// AUTOINJECT
void maybeMtxApplyTransform(D3DMATRIX *mtx, float dx, float dy, float dz) {
    mtx->_14 = dx + mtx->_14;
    mtx->_24 = dy + mtx->_24;
    mtx->_34 = dz + mtx->_34;
}

// Scales the rotation part by axis: each column by its scale when columns is set, otherwise each row - except
// _11, which is scaled by scaleX either way, as in the original.
// AUTOINJECT
void maybeMatrixAxisScale(D3DMATRIX *mtx, float scaleX, float scaleY, float scaleZ, char columns) {
    mtx->_11 = scaleX * mtx->_11;
    if (columns) {
        mtx->_21 = scaleX * mtx->_21;
        mtx->_31 = scaleX * mtx->_31;
        mtx->_12 = scaleY * mtx->_12;
        mtx->_22 = scaleY * mtx->_22;
        mtx->_32 = scaleY * mtx->_32;
        mtx->_13 = scaleZ * mtx->_13;
        mtx->_23 = scaleZ * mtx->_23;
        mtx->_33 = scaleZ * mtx->_33;
        return;
    }
    mtx->_12 = scaleX * mtx->_12;
    mtx->_13 = scaleX * mtx->_13;
    mtx->_21 = scaleY * mtx->_21;
    mtx->_22 = scaleY * mtx->_22;
    mtx->_23 = scaleY * mtx->_23;
    mtx->_31 = scaleZ * mtx->_31;
    mtx->_32 = scaleZ * mtx->_32;
    mtx->_33 = scaleZ * mtx->_33;
}

// A general 4x4 inverse, as D3DXMatrixInverse (0x00111fc0, which the original called): null, and out left alone,
// when the matrix is singular. out may be in.
D3DMATRIX *MatrixInverse(D3DMATRIX *out, float *determinant, const D3DMATRIX *in) {
    const float *m = in->f;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] +
             m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] -
             m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] +
             m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] -
              m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] -
             m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] +
             m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] -
             m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] +
              m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] +
             m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] -
             m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] +
              m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] -
              m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] -
             m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] +
             m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] -
              m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] +
              m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (determinant)
        *determinant = det;
    if (det == 0.0f)
        return 0;
    float r = 1.0f / det;
    for (int i = 0; i < 16; i++)
        out->f[i] = inv[i] * r;
    return out;
}

// AUTOINJECT
void maybeMtxInverse(D3DMATRIX *mtx) {
    MatrixInverse(mtx, 0, mtx);
}

// A camera-style basis looking from one point at another: row 3 the unit direction, row 1 up x direction, row 2
// direction x row 1 (left unnormalised, as the original leaves it), translation the from point. Used by
// maybe_psiDrawShadow for the shadow's projection.
// FUNC_AT(000e8220)
void MatrixLookAt(D3DMATRIX *out, float fromX, float fromY, float fromZ, float toX, float toY, float toZ,
                  float upX, float upY, float upZ) {
    float dx = toX - fromX, dy = toY - fromY, dz = toZ - fromZ;
    float len = sqrtf(dx * dx + dy * dy + dz * dz);
    if (0.0f < len)
        len = 1.0f / len;
    dx *= len; dy *= len; dz *= len;
    float ulen = sqrtf(upZ * upZ + upY * upY + upX * upX);
    if (0.0f < ulen)
        ulen = 1.0f / ulen;
    float sx = upY * ulen * dz - ulen * upZ * dy;
    float sy = ulen * upZ * dx - upX * ulen * dz;
    float sz = upX * ulen * dy - upY * ulen * dx;
    out->_11 = sx;  out->_12 = sy;  out->_13 = sz;  out->_14 = fromX;
    out->_21 = dy * sz - sy * dz;
    out->_22 = sx * dz - sz * dx;
    out->_23 = sy * dx - sx * dy;
    out->_24 = fromY;
    out->_31 = dx;  out->_32 = dy;  out->_33 = dz;  out->_34 = fromZ;
    out->_41 = 0.0f; out->_42 = 0.0f; out->_43 = 0.0f; out->_44 = 1.0f;
}
