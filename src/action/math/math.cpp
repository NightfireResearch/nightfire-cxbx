#include "math.h"
#include "../actionhelpers.h"
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

inline float MAX(float a, float b) {
	if(a > b)
		return a;
	return b;
}

inline float MIN(float a, float b) {
	if(a < b)
		return a;
	return b;
}

inline float MAX3(float a, float b, float c) {
	return MAX(MAX(a, b), c);
}

inline float MIN3(float a, float b, float c) {
	return MIN(MIN(a, b), c);
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

// Alternate order (xyzw) - used by DirectX?
// AUTOINJECT
void MultiplyQuaternionAltOrder(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *qOut) {
  qOut->q[0] = (a->q[3] * b->q[0] + b->q[2] * a->q[1] + b->q[3] * a->q[0]) - b->q[1] * a->q[2];
  qOut->q[1] = (a->q[3] * b->q[1] + b->q[3] * a->q[1] + a->q[2] * b->q[0]) - a->q[0] * b->q[2];
  qOut->q[2] = (a->q[3] * b->q[2] + b->q[1] * a->q[0] + b->q[3] * a->q[2]) - b->q[0] * a->q[1];
  qOut->q[3] = ((b->q[3] * a->q[3] - a->q[0] * b->q[0]) - b->q[1] * a->q[1]) - a->q[2] * b->q[2];
}

// AUTOINJECT
void Quat_QuaternionMultiply(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *qOut) {
  qOut->q[0] = ((a->q[0] * b->q[0] - b->q[1] * a->q[1]) - b->q[2] * a->q[2]) - b->q[3] * a->q[3];
  qOut->q[1] = b->q[1] * a->q[0] + a->q[1] * b->q[0] + (b->q[3] * a->q[2] - b->q[2] * a->q[3]);
  qOut->q[2] = b->q[2] * a->q[0] + a->q[2] * b->q[0] + (b->q[1] * a->q[3] - b->q[3] * a->q[1]);
  qOut->q[3] = b->q[3] * a->q[0] + a->q[3] * b->q[0] + (b->q[2] * a->q[1] - b->q[1] * a->q[2]);
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
void Quat_QuatToMat(quaternion_tag *param_1, _MATRIX *param_2) {
  float fVar1;
  float fVar2;
  float fVar3;
  float fVar4;
  float fVar5;
  float fVar6;
  float fVar7;
  float fVar8;
  float fVar9;
  
  fVar1 = param_1->q[0] + param_1->q[0];
  fVar3 = param_1->q[1] + param_1->q[1];
  fVar6 = param_1->q[2] + param_1->q[2];
  fVar2 = fVar1 * param_1->q[0];
  fVar5 = fVar3 * param_1->q[0];
  fVar7 = fVar6 * param_1->q[0];
  fVar4 = fVar3 * param_1->q[1];
  fVar8 = fVar6 * param_1->q[1];
  fVar9 = fVar6 * param_1->q[2];
  fVar1 = fVar1 * param_1->q[3];
  fVar3 = fVar3 * param_1->q[3];
  fVar6 = fVar6 * param_1->q[3];
  param_2->m[0] = 1.0f - (fVar9 + fVar4);
  param_2->m[1] = fVar5 + fVar6;
  param_2->m[2] = fVar7 - fVar3;
  param_2->m[4] = fVar5 - fVar6;
  param_2->m[5] = 1.0f - (fVar9 + fVar2);
  param_2->m[6] = fVar1 + fVar8;
  param_2->m[8] = fVar3 + fVar7;
  param_2->m[9] = fVar8 - fVar1;
  param_2->m[10] = 1.0f - (fVar4 + fVar2);
}

// Convert a rotation matrix to a quaternion (Shepperd's method)
// AUTOINJECT
void Quat_MatToQuat(quaternion_tag *quatOut, _MATRIX *mtxIn) {
  float trace = mtxIn->m[0] + mtxIn->m[5] + mtxIn->m[10];

  if (trace >= 0.0f) {
    float s = SQRT(trace + 1.0f);
    quatOut->q[3] = s * 0.5f;
    s = 0.5f / s;
    quatOut->q[0] = (mtxIn->m[6] - mtxIn->m[9]) * s;
    quatOut->q[1] = (mtxIn->m[8] - mtxIn->m[2]) * s;
    quatOut->q[2] = (mtxIn->m[1] - mtxIn->m[4]) * s;
    return;
  }

  // Diagonal indices in the 4-wide-stride matrix are i*5 (0, 5, 10)
  static const int next[3] = {1, 2, 0};
  int i = (mtxIn->m[0] < mtxIn->m[5]) ? 1 : 0;
  if (mtxIn->m[i * 5] < mtxIn->m[10]) {
    i = 2;
  }
  int j = next[i];
  int k = next[j];

  float s = SQRT((mtxIn->m[i * 5] - (mtxIn->m[j * 5] + mtxIn->m[k * 5])) + 1.0f);

  float q[4];
  q[i] = s * 0.5f;
  if (s != 0.0f) {
    s = 0.5f / s;
  }
  q[3] = (mtxIn->m[j * 4 + k] - mtxIn->m[k * 4 + j]) * s;
  q[j] = (mtxIn->m[i * 4 + j] + mtxIn->m[j * 4 + i]) * s;
  q[k] = (mtxIn->m[i * 4 + k] + mtxIn->m[k * 4 + i]) * s;

  quatOut->q[0] = q[0];
  quatOut->q[1] = q[1];
  quatOut->q[2] = q[2];
  quatOut->q[3] = q[3];
}

// Accurate (as opposed to Quat_Slerp's fast approximation) spherical interpolation between two quaternions
// AUTOINJECT
void Quat_Slerp_Acc(float blendFactor, float *quatStart, float *quatEnd, float *quatOut) {
  float cosOmega = quatStart[0] * quatEnd[0] + quatStart[1] * quatEnd[1] +
                    quatStart[2] * quatEnd[2] + quatStart[3] * quatEnd[3];

  bool negate = cosOmega < 0.0f;
  if (negate) {
    cosOmega = -cosOmega;
  }

  float scaleEnd, scaleStart;
  if (1.0f - cosOmega <= 0.01f) {
    // Quaternions are nearly coincident - fall back to linear interpolation
    scaleEnd = blendFactor;
    scaleStart = 1.0f - blendFactor;
  } else {
    double omega = acos((double)cosOmega);
    double sinOmega = sin(omega);
    scaleEnd = (float)(sin(omega * blendFactor) / sinOmega);
    scaleStart = (float)(sin(omega - omega * blendFactor) / sinOmega);
  }

  if (negate) {
    scaleStart = -scaleStart;
  }

  quatOut[0] = scaleEnd * quatEnd[0] + scaleStart * quatStart[0];
  quatOut[1] = scaleEnd * quatEnd[1] + scaleStart * quatStart[1];
  quatOut[2] = scaleEnd * quatEnd[2] + scaleStart * quatStart[2];
  quatOut[3] = scaleEnd * quatEnd[3] + scaleStart * quatStart[3];
}

// Smoothly rotate a matrix so it tracks/faces from currentPos towards targetPos
// AUTOINJECT
void Vec_Track(float blendFactor, float *targetPos, float *currentPos, _MATRIX *matrix, quaternion_tag *quatOut) {
  _VECTOR dir;
  dir.x = targetPos[0] - currentPos[0];
  dir.y = targetPos[1] - currentPos[1];
  dir.z = targetPos[2] - currentPos[2];
  Vec_Normalise(&dir, &dir);

  _MATRIX targetMtx;
  Mat_Align2Dir(&targetMtx, &dir, &CONST_UP_VECTOR, &MAYBE_CONST_FORWARD_VECTOR);

  quaternion_tag currentQuat, targetQuat;
  Quat_MatToQuat(&currentQuat, matrix);
  Quat_MatToQuat(&targetQuat, &targetMtx);

  Quat_Slerp_Acc(blendFactor, currentQuat.q, targetQuat.q, quatOut->q);

  Quat_QuatToMat(quatOut, matrix);
  Mat_Normalize(matrix);
}

// A fast approximation of slerp
// AUTOINJECT
void Quat_Slerp(float progress, quaternion_tag *qStart, quaternion_tag *qEnd, quaternion_tag *qOut) {
  if (progress < 0.5f) {
    qOut->q[0] = qStart->q[0];
    qOut->q[1] = qStart->q[1];
    qOut->q[2] = qStart->q[2];
    qOut->q[3] = qStart->q[3];
  } else {
    qOut->q[0] = qEnd->q[0];
    qOut->q[1] = qEnd->q[1];
    qOut->q[2] = qEnd->q[2];
    qOut->q[3] = qEnd->q[3];
  }
}

// Set up a matrix with the given Euler rotations (same rotation part as RotTransMatrix, without the translation)
// AUTOINJECT
void RotMatrix(_VECTOR *vIn,_MATRIX *mtxOut) {

  float cosX = cosf(vIn->x);
  float sinX = sinf(vIn->x);
  float cosY = cosf(vIn->y);
  float sinY = sinf(vIn->y);
  float cosZ = cosf(vIn->z);
  float sinZ = sinf(vIn->z);

  mtxOut->m[0] = cosZ * cosY;
  mtxOut->m[4] = -sinZ * cosY;
  mtxOut->m[8] = sinY;
  mtxOut->m[1] = sinZ * cosX + cosZ * sinY * sinX;
  mtxOut->m[5] = cosZ * cosX - sinZ * sinY * sinX;
  mtxOut->m[9] = -cosY * sinX;
  mtxOut->m[2] = sinZ * sinX - cosZ * sinY * cosX;
  mtxOut->m[6] = cosZ * sinX + sinZ * sinY * cosX;
  mtxOut->m[10] = cosY * cosX;

}

// Despite the parameter names, this reads from viewMtx and writes the inverted (transposed rotation,
// re-derived translation) result into worldMtx - see the callers for confirmation of this direction
// AUTOINJECT
void Mat_World2ViewMat(_MATRIX *viewMtx, _MATRIX *worldMtx) {
  worldMtx->m[0] = viewMtx->m[0];
  worldMtx->m[4] = viewMtx->m[1];
  worldMtx->m[8] = viewMtx->m[2];
  worldMtx->m[1] = viewMtx->m[4];
  worldMtx->m[5] = viewMtx->m[5];
  worldMtx->m[9] = viewMtx->m[6];
  worldMtx->m[2] = viewMtx->m[8];
  worldMtx->m[6] = viewMtx->m[9];
  worldMtx->m[10] = viewMtx->m[10];

  worldMtx->m[0xc] = -(worldMtx->m[1] * viewMtx->m[0xd] +
                       worldMtx->m[0] * viewMtx->m[0xc] + viewMtx->m[0xe] * worldMtx->m[2]);
  worldMtx->m[0xd] = -(viewMtx->m[0xc] * worldMtx->m[4] +
                       worldMtx->m[5] * viewMtx->m[0xd] + viewMtx->m[0xe] * worldMtx->m[6]);
  worldMtx->m[0xe] = -(worldMtx->m[8] * viewMtx->m[0xc] +
                       viewMtx->m[0xd] * worldMtx->m[9] + viewMtx->m[0xe] * worldMtx->m[10]);
}

// AUTOINJECT
void Mat_Scale3f(_MATRIX *m_out,_MATRIX *m_in,float scale_x,float scale_y,float scale_z) {
  m_out->m[0] = scale_x * m_in->m[0];
  m_out->m[4] = scale_y * m_in->m[4];
  m_out->m[8] = scale_z * m_in->m[8];
  m_out->m[1] = scale_x * m_in->m[1];
  m_out->m[5] = scale_y * m_in->m[5];
  m_out->m[9] = scale_z * m_in->m[9];
  m_out->m[2] = scale_x * m_in->m[2];
  m_out->m[6] = scale_y * m_in->m[6];
  m_out->m[10] = scale_z * m_in->m[10];
  m_out->m[0xc] = m_in->m[0xc];
  m_out->m[0xd] = m_in->m[0xd];
  m_out->m[0xe] = m_in->m[0xe];
}

// AUTOINJECT
void Mat_GetDir(_VECTOR *dirOut, _MATRIX *mtxIn) {
  dirOut->x = mtxIn->m[0x8];
  dirOut->y = mtxIn->m[0x9];
  dirOut->z = mtxIn->m[0xA];
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

// Build a matrix that keeps "up" as the exact up vector, deriving a level "forward" from direction and a "right"
// orthogonal to both (eg. used to keep a turret/sensor upright while aiming roughly towards a target)
// AUTOINJECT
void Mat_Align2Up(float *matrix, float *up, float *direction) {
  _VECTOR right;
  right.x = direction[2] * up[1] - direction[1] * up[2];
  right.y = direction[0] * up[2] - direction[2] * up[0];
  right.z = direction[1] * up[0] - direction[0] * up[1];
  Vec_Normalise(&right, &right);

  _VECTOR forward;
  forward.x = right.y * up[2] - right.z * up[1];
  forward.y = right.z * up[0] - right.x * up[2];
  forward.z = right.x * up[1] - right.y * up[0];
  Vec_Normalise(&forward, &forward);

  matrix[8] = forward.x;
  matrix[9] = forward.y;
  matrix[10] = forward.z;

  matrix[4] = up[0];
  matrix[5] = up[1];
  matrix[6] = up[2];

  matrix[0] = right.x;
  matrix[1] = right.y;
  matrix[2] = right.z;
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
void Matrix_SetTrans(_VECTOR *vec, _MATRIX *mtx) {
  mtx->m[0xc] = vec->x;
  mtx->m[0xd] = vec->y;
  mtx->m[0xe] = vec->z;
}

// AUTOINJECT
void Vec_Normalise(_VECTOR *vOut, _VECTOR *vIn) {
  
  float magnitude = Vec_Magnitude(vIn);
  
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
float Vec_NormaliseLen(_VECTOR *output, _VECTOR *input) {
  float len = Vec_Magnitude(input);
  Vec_Normalise(output,input);
  return len;
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

// AUTOINJECT
void Vec_Copy2(_VECTOR* src, _VECTOR *dst1, _VECTOR *dst2) {
  dst1->x = src->x;
  dst1->y = src->y;
  dst1->z = src->z;
  dst2->x = src->x;
  dst2->y = src->y;
  dst2->z = src->z;
}

// AUTOINJECT
bool Vec_IsEqual(_VECTOR *param_1,_VECTOR *param_2,float epsilon) {
  
  float dx = ABS(param_2->x - param_1->x);
  float dy = ABS(param_2->y - param_1->y);
  float dz = ABS(param_2->z - param_1->z);

  return (dx <= epsilon && dy <= epsilon && dz <= epsilon);
}

// AUTOINJECT
void Vec_CrossNormalise(_VECTOR *a,_VECTOR *b,_VECTOR *vecOut) {
  Vec_Cross(a,b,vecOut);
  Vec_Normalise(vecOut,vecOut);
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

float Vec_Dot(_VECTOR *a, _VECTOR *b) {
  return a->x * b->x + a->y * b->y + a->z * b->z;
}

// AUTOINJECT
float Vec_Magnitude(_VECTOR *a) {
  return SQRT(a->x * a->x + a->y * a->y + a->z * a->z);
}

// AUTOINJECT
void Vec_Max(_VECTOR *a, _VECTOR *b, _VECTOR *vecOut) {
  vecOut->x = MAX(a->x, b->x);
  vecOut->y = MAX(a->y, b->y);
  vecOut->z = MAX(a->z, b->z);
}

// AUTOINJECT
void Vec_Min(_VECTOR *a, _VECTOR *b, _VECTOR *vecOut) {
  vecOut->x = MIN(a->x, b->x);
  vecOut->y = MIN(a->y, b->y);
  vecOut->z = MIN(a->z, b->z);
}

// AUTOINJECT
void Vec_MinMax(_VECTOR *a, _VECTOR *b, _VECTOR *minimums, _VECTOR *maximums) {
  maximums->x = MAX(a->x, b->x);
  maximums->y = MAX(a->y, b->y);
  maximums->z = MAX(a->z, b->z);
  minimums->x = MIN(a->x, b->x);
  minimums->y = MIN(a->y, b->y);
  minimums->z = MIN(a->z, b->z);
}

// AUTOINJECT
void Vec_MinMax3(_VECTOR *a, _VECTOR *b, _VECTOR *c, _VECTOR *minimums, _VECTOR *maximums) {
  maximums->x = MAX3(a->x, b->x, c->x);
  maximums->y = MAX3(a->y, b->y, c->y);
  maximums->z = MAX3(a->z, b->z, c->z);
  minimums->x = MIN3(a->x, b->x, c->x);
  minimums->y = MIN3(a->y, b->y, c->y);
  minimums->z = MIN3(a->z, b->z, c->z);
}

// Find the equation of the plane defined by the three given points
// AUTOINJECT
bool Plane_PlaneEq(plane_equ_tag *planeEq, _VECTOR *v1, _VECTOR *v2, _VECTOR *v3) {
  
  _VECTOR v1v2, v2v3;
  // Find two edges
  Vec_Subtract(v2, v1, &v1v2);
  Vec_Subtract(v3, v2, &v2v3);

  // Find the normal from the two edges
  Vec_Cross(&v1v2, &v2v3, &planeEq->normal); // Only works because the plane normal is the first 3 components of a plane_equ_tag

  // Handle the degenerate case where the points are collinear or coincident
  float mag = Vec_Magnitude(&planeEq->normal);
  if(mag == 0.0f) {
    planeEq->a = 0;
    planeEq->b = 0;
    planeEq->c = 0;
    planeEq->d = 0;
    return false;
  }

  // Otherwise, normalise and calculate the distance component
  float rcpMag = 1.0f / mag;
  Vec_MulR32(&planeEq->normal, &planeEq->normal, rcpMag);
  planeEq->d = -Vec_Dot(&planeEq->normal, v1);
  return true;
}

// AUTOINJECT
float DistancePointToPlane(_VECTOR *point, plane_equ_tag *plane) {
  return point->x * plane->a + point->y * plane->b + point->z * plane->c + plane->d;
}

// AUTOINJECT
void auxVec_AddMulR32(_VECTOR *add, _VECTOR *vIn, float multiply, _VECTOR *vOut) {
  vOut->x = multiply * vIn->x + add->x;
  vOut->y = multiply * vIn->y + add->y;
  vOut->z = multiply * vIn->z + add->z;
}

// AUTOINJECT
bool vecutil_point_on_poly(_VECTOR *point, _VECTOR *vtx1, _VECTOR *vtx2, _VECTOR *vtx3, plane_equ_tag *plane) {

  // Vectors between the vertices
  _VECTOR v1v3 = {vtx1->x - vtx3->x, vtx1->y - vtx3->y, vtx1->z - vtx3->z};
  _VECTOR v2v1 = {vtx2->x - vtx1->x, vtx2->y - vtx1->y, vtx2->z - vtx1->z};
  _VECTOR v3v2 = {vtx3->x - vtx2->x, vtx3->y - vtx2->y, vtx3->z - vtx2->z};

  // Vectors between the point and the vertices
  _VECTOR pv1 = {point->x - vtx1->x, point->y - vtx1->y, point->z - vtx1->z};
  _VECTOR pv2 = {point->x - vtx2->x, point->y - vtx2->y, point->z - vtx2->z};
  _VECTOR pv3 = {point->x - vtx3->x, point->y - vtx3->y, point->z - vtx3->z};

  // Check against line segment v3v1 using the cross product on the plane
  _VECTOR tmp;
  Vec_Cross(&v1v3, &plane->normal, &tmp);
  if(Vec_Dot(&tmp, &pv1) > 0.0f)
      return false;

  // Check against line segment v2v1 using the cross product on the plane
  Vec_Cross(&v2v1, &plane->normal, &tmp);
  if(Vec_Dot(&tmp, &pv2) > 0.0f)
      return false;

  // Check against line segment v3v2 using the cross product on the plane
  Vec_Cross(&v3v2, &plane->normal, &tmp);
  if(Vec_Dot(&tmp, &pv3) > 0.0f)
      return false;

  // If on the correct side of all three line segments, we're inside the polygon
  return true;
}

// AUTOINJECT
float Vec_Dist3D(_VECTOR *a, _VECTOR *b) {

    float dx = (a->x-b->x);
    float dy = (a->y-b->y);
    float dz = (a->z-b->z);

    return SQRT(dx*dx + dy*dy + dz*dz);
}

// AUTOINJECT
float Vec_SqDist3D(_VECTOR *a, _VECTOR *b) {

  float dx = (a->x-b->x);
  float dy = (a->y-b->y);
  float dz = (a->z-b->z);

  return dx*dx + dy*dy + dz*dz;
}

// AUTOINJECT
void Vec_Swap(_VECTOR *a, _VECTOR *b) {
  float tmp_x = a->x;
  float tmp_y = a->y;
  float tmp_z = a->z;
  a->x = b->x;
  a->y = b->y;
  a->z = b->z;
  b->x = tmp_x;
  b->y = tmp_y;
  b->z = tmp_z;
}

// AUTOINJECT
void MatrixMultiplyVector(_MATRIX *mtx, _VECTOR *vecIn, _VECTOR *vecOut) {
  vecOut->x = mtx->m[0] * vecIn->x + mtx->m[4] * vecIn->y + mtx->m[8] * vecIn->z + mtx->m[0xc];
  vecOut->y = mtx->m[5] * vecIn->y + mtx->m[1] * vecIn->x + mtx->m[9] * vecIn->z + mtx->m[0xd];
  vecOut->z = mtx->m[6] * vecIn->y + mtx->m[2] * vecIn->x + mtx->m[10] * vecIn->z + mtx->m[0xe];
}

// AUTOINJECT
void ApplyMatrixLV(_MATRIX *mtx, _VECTOR *vIn, _VECTOR *vOut) {
  vOut->x = mtx->m[0] * vIn->x + mtx->m[4] * vIn->y + mtx->m[8] * vIn->z;
  vOut->y = mtx->m[5] * vIn->y + mtx->m[1] * vIn->x + mtx->m[9] * vIn->z;
  vOut->z = mtx->m[6] * vIn->y + mtx->m[2] * vIn->x + mtx->m[10] * vIn->z;
}

// AUTOINJECT
float Vec_ScalarTripleProduct(_VECTOR *a, _VECTOR *b, _VECTOR *c) {
  _VECTOR v1;
  Vec_Cross(b, c, &v1);
  Vec_Normalise(&v1, &v1);
  return (a->x * v1.x + a->y * v1.y + a->z * v1.z);
}

// AUTOINJECT
void vecutil_cartesian_to_spherical_acc(_VECTOR *vec, float v_x, float v_y, float v_z) {
  vec->x = atan2f(v_y, SQRT(v_z * v_z + v_x * v_x));
  vec->y = atan2f(v_x, v_z);
  vec->z = 0.0f;
}

// Shortest signed angular difference (angle2 - angle1), wrapped into (-pi, pi]. Both inputs are normalised into
// [0, 2pi) first.
// AUTOINJECT
float Vec_AngleDifference(float angle1, float angle2) {
  if (angle2 < 0.0f || angle2 >= (float)M_2PI) {
    angle2 = fmodf(angle2, (float)M_2PI);
    if (angle2 < 0.0f) {
      angle2 += (float)M_2PI;
    }
  }
  if (angle1 < 0.0f || angle1 >= (float)M_2PI) {
    angle1 = fmodf(angle1, (float)M_2PI);
    if (angle1 < 0.0f) {
      angle1 += (float)M_2PI;
    }
  }

  float diff = angle2 - angle1;
  float absDiff = ABS(diff);
  if (absDiff >= (float)M_PI) {
    if (diff < 0.0f) {
      return diff + (float)M_2PI;
    }
    diff -= (float)M_2PI;
  }
  return diff;
}

// AUTOINJECT
void Vec_Spherical_2_Cartesian(float *out, float radius, float yaw, float pitch) {
  float cosPitch = cosf(pitch);
  float sinYaw = sinf(yaw);
  out[0] = sinYaw * cosPitch * radius;
  float cosYaw = cosf(yaw);
  out[2] = cosYaw * cosPitch * radius;
  float sinPitch = sinf(pitch);
  out[1] = sinPitch * radius;
}

// Coefficients for maybeAtan2's polynomial approximation of atan(ratio) over ratio in [0,1]
#define MAYBE_ATAN2_COEFF_A 1.0596788f
#define MAYBE_ATAN2_COEFF_B 0.27131295f

// Fast rational atan2 approximation (max error ~0.28 degrees)
// AUTOINJECT
float maybeAtan2(float y, float x) {
  float result;

  if (x == y) {
    result = (x == 0.0f) ? 0.0f : (float)M_PI_4;
  } else {
    float absX = ABS(x);
    float absY = ABS(y);
    if (absX <= absY) {
      float ratio = absX / absY;
      result = (float)M_PI_2 - (MAYBE_ATAN2_COEFF_A - ratio * MAYBE_ATAN2_COEFF_B) * ratio;
    } else {
      float ratio = absY / absX;
      result = (MAYBE_ATAN2_COEFF_A - ratio * MAYBE_ATAN2_COEFF_B) * ratio;
    }
  }

  if (x < 0.0f) {
    result = (float)M_PI - result;
  }
  if (y < 0.0f) {
    result = -result;
  }
  return result;
}

// Align a matrix so its forward row faces "direction", with "param_3" as the preferred up hint (falling back to
// "param_4" when direction is nearly parallel to param_3)
// AUTOINJECT
void Mat_Align2Dir(_MATRIX *mtxOut, _VECTOR *direction, _VECTOR *param_3, _VECTOR *param_4) {
  _VECTOR dir;
  Vec_Normalise(&dir, direction);

  float dot = dir.x * param_3->x + dir.y * param_3->y + dir.z * param_3->z;
  if (dot < 0.0f) {
    dot = -dot;
  }

  _VECTOR right;
  if (dot <= 0.999f) {
    right.x = dir.z * param_3->y - dir.y * param_3->z;
    right.y = dir.x * param_3->z - dir.z * param_3->x;
    right.z = dir.y * param_3->x - dir.x * param_3->y;
  } else {
    right.x = dir.z * param_4->y - dir.y * param_4->z;
    right.y = dir.x * param_4->z - dir.z * param_4->x;
    right.z = dir.y * param_4->x - dir.x * param_4->y;
  }
  Vec_Normalise(&right, &right);

  _VECTOR up;
  up.x = right.z * dir.y - right.y * dir.z;
  up.y = right.x * dir.z - right.z * dir.x;
  up.z = right.y * dir.x - right.x * dir.y;
  Vec_Normalise(&up, &up);

  mtxOut->m[8] = dir.x;
  mtxOut->m[9] = dir.y;
  mtxOut->m[10] = dir.z;
  mtxOut->m[4] = up.x;
  mtxOut->m[5] = up.y;
  mtxOut->m[6] = up.z;
  mtxOut->m[0] = right.x;
  mtxOut->m[1] = right.y;
  mtxOut->m[2] = right.z;
}

// AUTOINJECT
void Vec_Add2(_VECTOR *a, _VECTOR *b, _VECTOR *out) {
  out->x = a->x + b->x;
  out->y = a->y + b->y;
  out->z = a->z + b->z;
}

// Re-orthonormalise a rotation matrix in place: the forward row is trusted as-is (just normalised), "up" is
// normalised then used to derive "right", and "up" is finally recomputed as right x forward so all three rows
// end up orthogonal
// AUTOINJECT
void Mat_Normalize(_MATRIX *mtx) {
  _VECTOR up, forward, right;
  up.x = mtx->m[4];
  up.y = mtx->m[5];
  up.z = mtx->m[6];
  forward.x = mtx->m[8];
  forward.y = mtx->m[9];
  forward.z = mtx->m[10];
  Vec_Normalise(&forward, &forward);
  Vec_Normalise(&up, &up);

  right.x = forward.z * up.y - forward.y * up.z;
  right.y = forward.x * up.z - forward.z * up.x;
  right.z = forward.y * up.x - forward.x * up.y;
  Vec_Normalise(&right, &right);

  mtx->m[8] = forward.x;
  mtx->m[9] = forward.y;
  mtx->m[10] = forward.z;
  mtx->m[4] = right.z * forward.y - right.y * forward.z;
  mtx->m[6] = right.y * forward.x - right.x * forward.y;
  mtx->m[5] = right.x * forward.z - right.z * forward.x;
  mtx->m[0] = right.x;
  mtx->m[1] = right.y;
  mtx->m[2] = right.z;
}