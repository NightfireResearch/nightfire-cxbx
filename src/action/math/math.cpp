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
  return;
}

// AUTOINJECT
void Quat_QuatTransToMat(quaternion_tag *quatIn,float *vecIn,_MATRIX *mOut) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  
  fVar1 = quatIn->q[0] + quatIn->q[0];
  fVar3 = quatIn->q[1] + quatIn->q[1];
  fVar6 = quatIn->q[2] + quatIn->q[2];
  fVar2 = fVar1 * quatIn->q[0];
  fVar5 = fVar3 * quatIn->q[0];
  fVar7 = fVar6 * quatIn->q[0];
  fVar4 = fVar3 * quatIn->q[1];
  fVar8 = fVar6 * quatIn->q[1];
  fVar9 = fVar6 * quatIn->q[2];
  fVar1 = fVar1 * quatIn->q[3];
  fVar3 = fVar3 * quatIn->q[3];
  fVar6 = fVar6 * quatIn->q[3];
  mOut->m[0] = 1.0f - (fVar9 + fVar4);
  mOut->m[1] = fVar5 + fVar6;
  mOut->m[2] = fVar7 - fVar3;
  mOut->m[4] = fVar5 - fVar6;
  mOut->m[5] = 1.0f - (fVar9 + fVar2);
  mOut->m[6] = fVar1 + fVar8;
  mOut->m[8] = fVar3 + fVar7;
  mOut->m[9] = fVar8 - fVar1;
  mOut->m[10] = 1.0f - (fVar4 + fVar2);
  mOut->m[0xc] = *vecIn;
  mOut->m[0xd] = vecIn[1];
  mOut->m[0xe] = vecIn[2];
  return;
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

// AUTOINJECT
void RotTransMat(_MATRIX *param_1,_MATRIX *param_2) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  
  fVar5 = param_2->m[8];
  fVar1 = param_2->m[0];
  fVar2 = param_2->m[4];
  fVar6 = param_2->m[9];
  fVar3 = param_2->m[1];
  fVar4 = param_2->m[2];
  fVar7 = param_2->m[10];
  fVar8 = param_2->m[5];
  fVar9 = param_2->m[6];
  param_2->m[0] = fVar1 * param_1->m[0] + fVar4 * param_1->m[8] + fVar3 * param_1->m[4];
  param_2->m[1] = fVar1 * param_1->m[1] + fVar4 * param_1->m[9] + fVar3 * param_1->m[5];
  param_2->m[2] = fVar3 * param_1->m[6] + fVar1 * param_1->m[2] + fVar4 * param_1->m[10];
  param_2->m[4] = fVar2 * param_1->m[0] + fVar9 * param_1->m[8] + fVar8 * param_1->m[4];
  param_2->m[5] = fVar2 * param_1->m[1] + fVar9 * param_1->m[9] + fVar8 * param_1->m[5];
  param_2->m[6] = fVar8 * param_1->m[6] + fVar2 * param_1->m[2] + fVar9 * param_1->m[10];
  param_2->m[8] = fVar5 * param_1->m[0] + fVar7 * param_1->m[8] + fVar6 * param_1->m[4];
  param_2->m[9] = fVar5 * param_1->m[1] + fVar7 * param_1->m[9] + fVar6 * param_1->m[5];
  param_2->m[10] = fVar6 * param_1->m[6] + fVar5 * param_1->m[2] + fVar7 * param_1->m[10];
  param_2->m[0xc] = param_1->m[0xc] + param_2->m[0xc];
  param_2->m[0xd] = param_1->m[0xd] + param_2->m[0xd];
  param_2->m[0xe] = param_1->m[0xe] + param_2->m[0xe];
  return;
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
  return;
}

// AUTOINJECT
void Vec_Copy(_VECTOR *src, _VECTOR *dst) {
  dst->x = src->x;
  dst->y = src->y;
  dst->z = src->z;
}

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

  return;
}
