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

// FUNC_AT(000d55f0)
void Quat_Copy(quaternion_tag *target, const quaternion_tag *from) {
	target->q[0] = from->q[0];
	target->q[1] = from->q[1];
	target->q[2] = from->q[2];
	target->q[3] = from->q[3];
}

// FUNC_AT(000d5560)
bool Quat_IsEqual(const quaternion_tag *a, const quaternion_tag *b, float threshold) {
  if (	(ABS(a->q[0] - b->q[0]) > threshold) ||
		(ABS(a->q[1] - b->q[1]) > threshold) ||
		(ABS(a->q[2] - b->q[2]) > threshold) ||
		(ABS(a->q[3] - b->q[3]) > threshold))
		return false;
	return true;
}

// FUNC_AT(000d7470)
void Quat_Mul(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *qOut) {
  qOut->q[0] = (a->q[3] * b->q[0] + b->q[2] * a->q[1] + b->q[3] * a->q[0]) - b->q[1] * a->q[2];
  qOut->q[1] = (a->q[3] * b->q[1] + b->q[3] * a->q[1] + a->q[2] * b->q[0]) - a->q[0] * b->q[2];
  qOut->q[2] = (a->q[3] * b->q[2] + b->q[1] * a->q[0] + b->q[3] * a->q[2]) - b->q[0] * a->q[1];
  qOut->q[3] = ((b->q[3] * a->q[3] - a->q[0] * b->q[0]) - b->q[1] * a->q[1]) - a->q[2] * b->q[2];
  return;
}

// FUNC_AT(000d4ff0)
void Mat_Copy(const _MATRIX *source, _MATRIX *target) {
	for (int i = 0; i < 15; i++) {
		target->m[i] = source->m[i];
	}
}

// FUNC_AT(000d5010)
void Mat_CopyRot(const _MATRIX *source, _MATRIX *target) {
	for (int i = 0; i < 12; i++) {
		target->m[i] = source->m[i];
	}
}

// FUNC_AT(000d5160)
void Mat_IdentityT(_MATRIX *mtx) {
	for(int i = 0; i < 15; i++) {
		mtx->m[i] = 0.0f;
	}
	mtx->m[0] = 1.0;
	mtx->m[5] = 1.0;
	mtx->m[10] = 1.0;
}

// FUNC_AT(000d5130)
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

// FUNC_AT(000d6b80)
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

// FUNC_AT(000d76d0)
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

// FUNC_AT(000d7370)
void Vec_Subtract(const _VECTOR *a, const _VECTOR *b, _VECTOR *vOut) {
  vOut->x = a->x - b->x;
  vOut->y = a->y - b->y;
  vOut->z = a->z - b->z;
}

// FUNC_AT(000d4dc0)
void Vec_Zero(_VECTOR *v) {
  v->x = 0.0;
  v->y = 0.0;
  v->z = 0.0;
}

// FUNC_AT(000d7280)
void Vec_Negate(const _VECTOR *vIn,_VECTOR *vOut) {
  vOut->x = -vIn->x;
  vOut->y = -vIn->y;
  vOut->z = -vIn->z;
  return;
}