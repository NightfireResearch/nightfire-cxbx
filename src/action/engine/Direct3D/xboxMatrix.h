#ifndef _XBOXMATRIX_H_
#define _XBOXMATRIX_H_

#include "d3dhelpers.h"

// Eurocom's matrix helpers from its Xbox layer (0x000e81a0-0x000e89d0), and the D3DX inverse one of them wraps.
// Matrices are D3DMATRIX, row-major, with the translation in the last column (_14, _24, _34): the layout the
// game's own _MATRIX uses, before d3dSetMatrix transposes it for Direct3D.

void maybeD3DMATRIXcopy(undefined4 *dest, undefined4 *src);
void d3dMatrixIdentity(D3DMATRIX *mtx);
void maybeTransposeRotationPart(D3DMATRIX *mtx);
void maybeInvertRigidTransform(D3DMATRIX *mtx);
void maybeMtxApplyTransform(D3DMATRIX *mtx, float dx, float dy, float dz);
void maybeMatrixAxisScale(D3DMATRIX *mtx, float scaleX, float scaleY, float scaleZ, char columns);
void maybeMtxInverse(D3DMATRIX *mtx);
void MatrixLookAt(D3DMATRIX *out, float fromX, float fromY, float fromZ, float toX, float toY, float toZ,
                  float upX, float upY, float upZ);
D3DMATRIX *MatrixInverse(D3DMATRIX *out, float *determinant, const D3DMATRIX *in);

#endif // _XBOXMATRIX_H_
