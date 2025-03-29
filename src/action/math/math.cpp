#include "math.h"
#include <cmath>

inline float ABS(float n) {
	if(n < 0)
		return -n;
	return n;
}

inline bool ISNAN(float n) {
	return n != n;
}

inline float SQRT(float n) {
	return sqrtf(n);
}

// AUTOINJECT
void Quat_Copy(quaternion_tag *target, const quaternion_tag *from) {
	target->q[0] = from->q[0];
	target->q[1] = from->q[1];
	target->q[2] = from->q[2];
	target->q[3] = from->q[3];
}

// AUTOINJECT
bool Quat_IsEqual(const quaternion_tag *a, const quaternion_tag *b, float threshold) {
  if (	(ABS(a->q[0] - b->q[0]) > threshold) ||
		(ABS(a->q[1] - b->q[1]) > threshold) ||
		(ABS(a->q[2] - b->q[2]) > threshold) ||
		(ABS(a->q[3] - b->q[3]) > threshold))
		return false;
	return true;
}

// AUTOINJECT
void Quat_Mul(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *qOut) {
  qOut->q[0] = (a->q[3] * b->q[0] + b->q[2] * a->q[1] + b->q[3] * a->q[0]) - b->q[1] * a->q[2];
  qOut->q[1] = (a->q[3] * b->q[1] + b->q[3] * a->q[1] + a->q[2] * b->q[0]) - a->q[0] * b->q[2];
  qOut->q[2] = (a->q[3] * b->q[2] + b->q[1] * a->q[0] + b->q[3] * a->q[2]) - b->q[0] * a->q[1];
  qOut->q[3] = ((b->q[3] * a->q[3] - a->q[0] * b->q[0]) - b->q[1] * a->q[1]) - a->q[2] * b->q[2];
}

// AUTOINJECT
void Quat_QuatTransToMat(quaternion_tag *quatIn, float *vecIn, _MATRIX *mOut) {
  float q0_2 = quatIn->q[0] + quatIn->q[0];
  float q1_2 = quatIn->q[1] + quatIn->q[1];
  float q2_2 = quatIn->q[2] + quatIn->q[2];
  float q0_q0 = q0_2 * quatIn->q[0];
  float q0_q1 = q1_2 * quatIn->q[0];
  float q0_q2 = q2_2 * quatIn->q[0];
  float q1_q1 = q1_2 * quatIn->q[1];
  float q1_q2 = q2_2 * quatIn->q[1];
  float q2_q2 = q2_2 * quatIn->q[2];
  float q0_q3 = q0_2 * quatIn->q[3];
  float q1_q3 = q1_2 * quatIn->q[3];
  float q2_q3 = q2_2 * quatIn->q[3];

  mOut->m[0] = 1.0f - (q2_q2 + q1_q1);
  mOut->m[1] = q0_q1 + q2_q3;
  mOut->m[2] = q0_q2 - q1_q3;
  mOut->m[4] = q0_q1 - q2_q3;
  mOut->m[5] = 1.0f - (q2_q2 + q0_q0);
  mOut->m[6] = q0_q3 + q1_q2;
  mOut->m[8] = q1_q3 + q0_q2;
  mOut->m[9] = q1_q2 - q0_q3;
  mOut->m[10] = 1.0f - (q1_q1 + q0_q0);
  mOut->m[12] = vecIn[0];
  mOut->m[13] = vecIn[1];
  mOut->m[14] = vecIn[2];
}

// AUTOINJECT
void Mat_Copy(const _MATRIX *source, _MATRIX *target) {
	for (int i = 0; i < 15; i++) {
		target->m[i] = source->m[i];
	}
}

// AUTOINJECT
void Mat_CopyRot(const _MATRIX *source, _MATRIX *target) {
	for (int i = 0; i < 12; i++) {
		target->m[i] = source->m[i];
	}
}

// AUTOINJECT
void Mat_IdentityT(_MATRIX *mtx) {
	for(int i = 0; i < 15; i++) {
		mtx->m[i] = 0.0f;
	}
	mtx->m[0] = 1.0;
	mtx->m[5] = 1.0;
	mtx->m[10] = 1.0;
}

// AUTOINJECT
void Mat_Identity(_MATRIX *mtx) {
	mtx->m[0] = 1.0;
	mtx->m[1] = 0.0;
	mtx->m[2] = 0.0;
	mtx->m[4] = 0.0;
	mtx->m[5] = 1.0;
	mtx->m[6] = 0.0;
	mtx->m[8] = 0.0;
	mtx->m[9] = 0.0;
	mtx->m[10] = 1.0;
}

// Combine the rotation and translation of two matrices
// AUTOINJECT
void RotTransMat(_MATRIX *mat1, _MATRIX *mat2) {
  float m2_00 = mat2->m[0];
  float m2_01 = mat2->m[1];
  float m2_02 = mat2->m[2];
  float m2_10 = mat2->m[4];
  float m2_11 = mat2->m[5];
  float m2_12 = mat2->m[6];
  float m2_20 = mat2->m[8];
  float m2_21 = mat2->m[9];
  float m2_22 = mat2->m[10];

  mat2->m[0] = m2_00 * mat1->m[0] + m2_02 * mat1->m[8] + m2_01 * mat1->m[4];
  mat2->m[1] = m2_00 * mat1->m[1] + m2_02 * mat1->m[9] + m2_01 * mat1->m[5];
  mat2->m[2] = m2_01 * mat1->m[6] + m2_00 * mat1->m[2] + m2_02 * mat1->m[10];
  mat2->m[4] = m2_10 * mat1->m[0] + m2_12 * mat1->m[8] + m2_11 * mat1->m[4];
  mat2->m[5] = m2_10 * mat1->m[1] + m2_12 * mat1->m[9] + m2_11 * mat1->m[5];
  mat2->m[6] = m2_11 * mat1->m[6] + m2_10 * mat1->m[2] + m2_12 * mat1->m[10];
  mat2->m[8] = m2_20 * mat1->m[0] + m2_22 * mat1->m[8] + m2_21 * mat1->m[4];
  mat2->m[9] = m2_20 * mat1->m[1] + m2_22 * mat1->m[9] + m2_21 * mat1->m[5];
  mat2->m[10] = m2_21 * mat1->m[6] + m2_20 * mat1->m[2] + m2_22 * mat1->m[10];
  mat2->m[12] = mat1->m[12] + mat2->m[12];
  mat2->m[13] = mat1->m[13] + mat2->m[13];
  mat2->m[14] = mat1->m[14] + mat2->m[14];
}

// AUTOINJECT
void Vec_Normalise(_VECTOR *vOut, _VECTOR *vIn) {
  
  float magnitude = SQRT(vIn->z * vIn->z + vIn->y * vIn->y + vIn->x * vIn->x);
  
  if (!ISNAN(magnitude) && (magnitude != 0.0)) {
    float rcpMag = 1.0f / magnitude;
    vOut->x = rcpMag * vIn->x;
    vOut->y = rcpMag * vIn->y;
    vOut->z = rcpMag * vIn->z;
    return;
  }

  vOut->z = 0.0;
  vOut->y = 0.0;
  vOut->x = 0.0;
}

// AUTOINJECT
void Vec_Subtract(const _VECTOR *a, const _VECTOR *b, _VECTOR *vOut) {
  vOut->x = a->x - b->x;
  vOut->y = a->y - b->y;
  vOut->z = a->z - b->z;
}

// AUTOINJECT
void Vec_Zero(_VECTOR *v) {
  v->x = 0.0;
  v->y = 0.0;
  v->z = 0.0;
}

// AUTOINJECT
void Vec_Negate(const _VECTOR *vIn,_VECTOR *vOut) {
  vOut->x = -vIn->x;
  vOut->y = -vIn->y;
  vOut->z = -vIn->z;
}

// AUTOINJECT
void Vec_Copy(_VECTOR *src, _VECTOR *dst) {
  dst->x = src->x;
  dst->y = src->y;
  dst->z = src->z;
}

// AUTOINJECT
void Vec_MulR32(_VECTOR *outVec, _VECTOR *inVec, float scale) {
  outVec->x = scale * inVec->x;
  outVec->y = scale * inVec->y;
  outVec->z = scale * inVec->z;
}

// AUTOINJECT
void Vec_Cross(_VECTOR *a,_VECTOR *b,_VECTOR *vecOut) {
  vecOut->x = b->z * a->y - b->y * a->z;
  vecOut->y = b->x * a->z - a->x * b->z;
  vecOut->z = a->x * b->y - b->x * a->y;
}

// Set up a matrix with the given Euler rotations
// AUTOINJECT
void RotMatrixZYX(_VECTOR *angles, _MATRIX *mtx) {
  
  float cosX = cosf(angles->x);
  float sinX = sinf(angles->x);
  float cosY = cosf(angles->y);
  float sinY = sinf(angles->y);
  float cosZ = cosf(angles->z);
  float sinZ = sinf(angles->z);

  mtx->m[0] = cosZ * cosY;
  mtx->m[4] = cosZ * sinY * sinX - sinZ * cosX;
  mtx->m[8] = cosZ * sinY * cosX + sinZ * sinX;
  mtx->m[1] = sinZ * cosY;
  mtx->m[5] = cosZ * cosX + sinZ * sinY * sinX;
  mtx->m[9] = sinZ * sinY * cosX - cosZ * sinX;
  mtx->m[2] = -sinY;
  mtx->m[6] = cosY * sinX;
  mtx->m[10] = cosY * cosX;

}

// Set up a matrix with the given Euler rotations and translation
// AUTOINJECT
void RotTransMatrix(_VECTOR *rot, _VECTOR *trans, _MATRIX *mtx) {

  float cosX = cosf(rot->x);
  float sinX = sinf(rot->x);
  float cosY = cosf(rot->y);
  float sinY = sinf(rot->y);
  float cosZ = cosf(rot->z);
  float sinZ = sinf(rot->z);

  mtx->m[0] = cosZ * cosY;
  mtx->m[4] = -sinZ * cosY;
  mtx->m[8] = sinY;
  mtx->m[1] = sinZ * cosX + cosZ * sinY * sinX;
  mtx->m[5] = cosZ * cosX - sinZ * sinY * sinX;
  mtx->m[9] = -cosY * sinX;
  mtx->m[2] = sinZ * sinX - cosZ * sinY * cosX;
  mtx->m[6] = cosZ * sinX + sinZ * sinY * cosX;
  mtx->m[10] = cosY * cosX;
  mtx->m[12] = trans->x;
  mtx->m[13] = trans->y;
  mtx->m[14] = trans->z;

}

// Helper function which either didn't exist or was inlined on original code
float Vec_Dot(_VECTOR *a, _VECTOR *b) {
  return a->x * b->x + a->y * b->y + a->z * b->z;
}

float Vec_Magnitude(_VECTOR *a) {
  return SQRT(a->x * a->x + a->y * a->y + a->z * a->z);
}

// Find the equation of the plane defined by the three given points
// AUTOINJECT
bool Plane_PlaneEq(plane_equ_tag *planeEq, _VECTOR *v1, _VECTOR *v2, _VECTOR *v3) {
  
  _VECTOR v1v2, v2v3;
  // Find two edges
  Vec_Subtract(v2, v1, &v1v2);
  Vec_Subtract(v3, v2, &v2v3);

  // Find the normal from the two edges
  Vec_Cross(&v1v2, &v2v3, (_VECTOR*)planeEq); // Only works because the plane normal is the first 3 components of a plane_equ_tag

  // Handle the degenerate case where the points are collinear or coincident
  float mag = Vec_Magnitude((_VECTOR*)planeEq);
  if(mag == 0.0f) {
    planeEq->a = 0;
    planeEq->b = 0;
    planeEq->c = 0;
    planeEq->d = 0;
    return false;
  }

  // Otherwise, normalise and calculate the distance component
  float rcpMag = 1.0f / mag;
  planeEq->a = planeEq->a * rcpMag;
  planeEq->b = planeEq->b * rcpMag;
  planeEq->c = planeEq->c * rcpMag;
  planeEq->d = -Vec_Dot((_VECTOR*)planeEq, v1);
  return true;
}