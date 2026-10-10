#ifndef DRIVING_PLATFORM_REALMATH_H_
#define DRIVING_PLATFORM_REALMATH_H_

// EA's maths library as the driving engine has it: the portable x87 routines (trig in turns, v3*, MATRIX4_*,
// random), the Xbox VU0_* layer (SSE), the builders and extractors, and the D3DX routines linked beside them. See
// RealMath.cpp, VU0Math.cpp and D3DXMath.cpp, and docs/driving/maths.md for the originals.
//
// Vectors and matrices are passed as untyped pointers: callers hand in the game's Vec4, _VEC3, MATRIX4 and their
// own structures alike. The VU0 functions read and write 16 bytes even for "xyz" operations and keep the
// destination's w (docs/driving/maths.md 2.1); MATRIX4 is 16 floats, row-major, row-vector convention.
//
// A value the original leaves unrounded on the x87 stack comes back as a double here (v3length, VEC3_Dot,
// v3distance, rrandom...), so callers that keep computing see what the original's callers saw.

#include <stdint.h>

// ---- trig in turns (x87; the results are FSIN/FCOS/FPATAN's own, so these are assembly)
float sin_fractionalangle(float turns);
float cos_fractionalangle(float turns);
void sincos_fractionalangle(const float *turns, float *sinOut, float *cosOut);
float tan_fractionalangle(float turns);
float atan_turns(float y, float x);
// sin_fractionalangle and cos_fractionalangle leave FSIN's and FCOS's results unrounded in ST0: these read
// them as doubles, for callers that keep computing with them as the original does
inline double SineTurns(float turns) {
    return reinterpret_cast<double (*)(float)>(&sin_fractionalangle)(turns);
}
inline double CosineTurns(float turns) {
    return reinterpret_cast<double (*)(float)>(&cos_fractionalangle)(turns);
}

// ---- EA's portable vector and matrix routines (x87)
void v3add_x87(const float *a, const float *b, float *out);       // the EBP-framed helpers the v3 functions call
const float *v3sub_x87(const float *a, const float *b, float *out);
double v3dot_x87(const float *a, const float *b);
void v3crossprod_x87(const float *a, const float *b, float *out);
double v3length(const void *v);
void v3unit(const void *in, void *out);
double VEC3_Dot(const void *a, const void *b);
void v3crossprod(const void *a, const void *b, void *out);
void v3unitcrossprod(const void *a, const void *b, void *out);
void v3sub(int count, const void *a, const void *b, void *out);
void v3scale(int count, const void *in, float scale, void *out);
double v3distance(const void *a, const void *b);
void MATRIX4_multxlate(const void *m, const void *v, void *out);
void MATRIX4_multscale(const void *m, const void *v, void *out);
void MATRIX4_mult(const void *a, const void *b, void *out);
void MATRIX4_vect3mult(const void *v, const void *m, void *out);
void MATRIX4_vect4mult(const void *v, const void *m, void *out);
void MATRIX4_axisrotate(const void *axis, float turns, void *out);

uint32_t REAL_random();
void seedrandom(uint32_t seed);
double rrandom();

// ---- the Xbox VU0 layer (SSE)
void VU0_v3add(const void *a, const void *b, void *out);
void VU0_v4sub(const void *a, const void *b, void *out);
void VU0_v4add4(const void *a, const void *b, void *out);
void VU0_v4sub4(const void *a, const void *b, void *out);
float v3dotprod(const void *a, const void *b);
float v4dotprod(const void *a, const void *b);
float VU0_sqrt(float x);
float VU0_rsqrt(float x);
float VU0_v3length(const void *v);
float VU0_v3lengthsquare(const void *v);
float VU0_v4lengthsquare(const void *v);
void VU0_v4copy(const void *src, void *dst);
void MatrixCopy(const void *src, void *dst);
void VU0_v4crossprodxyz(const void *a, const void *b, void *out);
void VU0_v4crossprod1(const void *a, const void *b, void *out);
void VU0_v4unitcrossprodxyz(const void *a, const void *b, void *out);
void VU0_v4unitcrossprod1(const void *a, const void *b, void *out);
void VU0_v4unitxyz(const void *in, void *out);
void VU0_v4unit(const void *in, void *out);
void VU0_v4mult(const void *a, const void *b, void *out);
void VU0_v4multxyz(const void *a, const void *b, void *out);
void VU0_v4scale(const void *in, float scale, void *out);
void VU0_v4scale4(const void *in, float scale, void *out);
void VU0_v4addscale(const void *a, const void *b, float scale, void *out);
void VU0_v4scaleadd(const void *a, float scale, const void *b, void *out);
void VU0_v4scaleadd4(const void *a, float scale, const void *b, void *out);
float VU0_v3distancesquare(const void *a, const void *b);
float vec3distance(const void *a, const void *b);
float VU0_v3lengthxz(const void *v);
float VU0_v3distancexz(const void *a, const void *b);
float VU0_v3distancesquarexz(const void *a, const void *b);
void VU0_v3negate(void *v);
void VU0_MATRIX4_vect3mult(const void *in, const void *m, void *out);
void VU0_MATRIX4_vect4mult(const void *in, const void *m, void *out);
void VU0_MATRIX4_vect3rotate(const void *in, const void *m, void *out);
void VU0_MATRIX4_vect4multarray(const void *in, const void *m, void *out, int count);
void VU0_MATRIX4_vect3multsub(const void *in, const void *m, void *out);
void VU0_fastqslerp(const void *q0, const void *q1, void *out, float t);
void VU0_v4quatrotate(const void *q, const void *v, void *out);
void VU0_v4quatrotate_xlate(const void *q, const void *v, const void *t, void *out);

// ---- builders and extractors
void VU0_MATRIX4Init(void *m);
void VU0_v4Init(void *q);
void BuildScaleUniform(void *m, float scale);
void BuildScaleXYZ(void *m, float sx, float sy, float sz);
void BuildScale(void *m, float sx, float sy, float sz);
void BuildTranslate(void *m, float x, float y, float z);
void BuildRotate(void *m, float degrees, float axisX, float axisY, float axisZ);
void ExtractRotTrans(const void *m, float *rot, void *t);
void ExtractQuatTrans(const void *m, void *q, void *t);
void RealQuatFromRot(const float *rotation9, float *quaternion);   // 0x00115440's algorithm (register arguments)
void MATRIX4_TransformPoint(const void *m, const void *in, void *out);
void TransformPoint(const void *m, const void *in, void *out);
void MATRIX4_RotateVector(const void *m, const void *in, void *out);
void OrthoInverse(void *m);
double Determinant4x4(const void *m);
double Inverse(void *m);   // answers the determinant
bool FindOBBIntersect(const void *halfExtents, const void *from, const void *to, void *hit);
void VU0_ExtractXAxis3FromQuat(const void *q, void *out);
void VU0_ExtractZAxis3FromQuat(const void *q, void *out);
void VU0_SQTquattom4(const void *scale, const void *q, const void *t, void *out);
void VU0_v4tocolour(const void *colour, uint32_t *out);
void VU0_MATRIX3x4_mult(const void *a, const void *b, void *out);
void VU0_MATRIX4_3x3transpose(const void *src, void *dst);
void VU0_MATRIX4setxrot(void *m, float turns);
void VU0_MATRIX4setyrot(void *m, float turns);
void VU0_MATRIX4setzrot(void *m, float turns);
void VU0_EulerToQuat(const void *eulerTurns, void *out);
void BytesToCoordXYZ(uint32_t rgb, const void *add, const void *mul, const void *add2, void *out);
void BytesToCoordXYZW(uint32_t rgba, const void *add, const void *mul, const void *add2, void *out);

// ---- D3DX (stdcall, each returning its output)
void *__stdcall VU0_MATRIX4_mult(void *out, const void *a, const void *b);         // D3DXMatrixMultiply
void *__stdcall VU0_MATRIX4_transpose(void *out, const void *m);                    // D3DXMatrixTranspose
void *__stdcall VU0_quattom4(void *out, const void *q);            // D3DXMatrixRotationQuaternion
void *__stdcall VU0_m4toquat(void *out, const void *m);            // D3DXQuaternionRotationMatrix
void *__stdcall D3DXQuaternionMultiply(void *out, const void *q1, const void *q2);
void *__stdcall D3DXMatrixPerspectiveFovRH(void *out, float fovy, float aspect, float zn, float zf);
void *__stdcall D3DXMatrixOrthoRH(void *out, float w, float h, float zn, float zf);
void *__stdcall D3DXMatrixOrthoOffCenterRH(void *out, float l, float r, float b, float t, float zn, float zf);
void *__stdcall D3DXVec3Project(void *out, const void *v, const void *viewport, const void *proj, const void *view,
                                const void *world);

#endif // DRIVING_PLATFORM_REALMATH_H_
