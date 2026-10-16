#include "math.h"
#include "X87Ops.h"
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

// The body of Quat_QuatToMat - see the wrapper below for why the two are split.
void _Quat_QuatToMat(quaternion_tag *param_1, _MATRIX *param_2) {
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

// The original (0x000d5800) only ever touches EAX and the x87 stack, and the game's own callers were compiled
// knowing that: KeyFrame_Interp and Spline_Interp keep the output matrix in ECX across the call and then write
// the translation through it (mov [ecx+0x30], ...). Our compiled body is entitled to clobber ECX and EDX, and
// clang leaves the quaternion pointer in ECX - so the translation went 0x30 bytes past a stack local instead,
// onto Spline_Interp's own return address, and loading a level with a swinging door (Door_Create ->
// Door_SetupSwing -> Spline_Interp) jumped to a float. ECX and EDX are saved here so any caller keeps what it
// had.
// AUTOLTCG
void __declspec(naked) Quat_QuatToMat(quaternion_tag *param_1, _MATRIX *param_2) {
  _asm {
    push ecx
    push edx
    push dword ptr [esp + 16]   // param_2
    push dword ptr [esp + 16]   // param_1
    call _Quat_QuatToMat
    add esp, 8
    pop edx
    pop ecx
    ret
  }
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

// Set up a matrix with the given Euler rotations. On the x87: X's sine and cosine and Y's sine stay unrounded, the
// rest are stored as floats, and so is -sinY * cosX.
// AUTOINJECT
void RotMatrix(_VECTOR *vIn, _MATRIX *mtxOut) {
  Extended cosX = FCos(vIn->x);
  Extended sinX = FSin(vIn->x);
  float cosY = (float)FCos(vIn->y);
  Extended sinY = FSin(vIn->y);
  float cosZ = (float)FCos(vIn->z);
  float sinZ = (float)FSin(vIn->z);
  Extended sinYsinX = sinY * sinX;
  float negSinYcosX = (float)-(sinY * cosX);

  mtxOut->m[0] = (float)(Extended(cosZ) * cosY);
  mtxOut->m[4] = (float)-(Extended(sinZ) * cosY);
  mtxOut->m[8] = (float)sinY;
  mtxOut->m[1] = (float)(cosZ * sinYsinX + sinZ * cosX);
  mtxOut->m[5] = (float)(cosZ * cosX - sinYsinX * sinZ);
  mtxOut->m[9] = (float)-(cosY * sinX);
  mtxOut->m[2] = (float)(Extended(negSinYcosX) * cosZ + sinZ * sinX);
  mtxOut->m[6] = (float)(cosZ * sinX - Extended(negSinYcosX) * sinZ);
  mtxOut->m[10] = (float)(cosY * cosX);
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

static void Vec_NormaliseBody(_VECTOR *vOut, _VECTOR *vIn) {
  
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

// The original writes neither ECX nor EDX, and its callers rely on it: Mat_LookAt and 0x000d8590 keep EDX across the call. So the
// game reaches this through an entry that keeps them (tools/caller_saved.py).
// AUTOINJECT
void __declspec(naked) Vec_Normalise(_VECTOR *vOut, _VECTOR *vIn) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call Vec_NormaliseBody
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

static float Vec_NormaliseLenBody(_VECTOR *output, _VECTOR *input) {
  float len = Vec_Magnitude(input);
  Vec_Normalise(output,input);
  return len;
}

// The original writes neither ECX nor EDX, and its callers rely on it: Cel_ObjectLeftCel keeps EDX across the call. So the
// game reaches this through an entry that keeps them (tools/caller_saved.py).
// AUTOINJECT
float __declspec(naked) Vec_NormaliseLen(_VECTOR *output, _VECTOR *input) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call Vec_NormaliseLenBody
        add esp, 8
        pop edx
        pop ecx
        ret
    }
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

// Set up a matrix with the given Euler rotations, Z then Y then X. On the x87: only Y's cosine and sinZ * sinY are
// stored as floats.
// AUTOINJECT
void RotMatrixZYX(_VECTOR *angles, _MATRIX *mtx) {
  Extended cosX = FCos(angles->x);
  Extended sinX = FSin(angles->x);
  float cosY = (float)FCos(angles->y);
  Extended sinY = FSin(angles->y);
  Extended cosZ = FCos(angles->z);
  Extended sinZ = FSin(angles->z);
  Extended cosZsinY = cosZ * sinY;
  float sinZsinY = (float)(sinZ * sinY);

  mtx->m[0] = (float)(cosZ * cosY);
  mtx->m[4] = (float)(cosZsinY * sinX - sinZ * cosX);
  mtx->m[8] = (float)(sinZ * sinX + cosZsinY * cosX);
  mtx->m[1] = (float)(sinZ * cosY);
  mtx->m[5] = (float)(sinZsinY * sinX + cosZ * cosX);
  mtx->m[9] = (float)(sinZsinY * cosX - cosZ * sinX);
  mtx->m[2] = (float)-sinY;
  mtx->m[6] = (float)(cosY * sinX);
  mtx->m[10] = (float)(cosY * cosX);
}

// Set up a matrix with the given Euler rotations and translation
// AUTOINJECT
void RotTransMatrix(_VECTOR *rot, _VECTOR *trans, _MATRIX *mtx) {
  RotMatrix(rot, mtx);
  mtx->m[12] = trans->x;
  mtx->m[13] = trans->y;
  mtx->m[14] = trans->z;
}

// Summed on the x87, z first, as the game's inlined copies have it
double Vec_Dot(_VECTOR *a, _VECTOR *b) {
  return (double)a->z * b->z + (double)a->y * b->y + (double)a->x * b->x;
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

// Find the equation of the plane defined by the three given points. On the x87: the first edge, the second edge's
// z, the normal's length and the scaled normal stay unrounded; the normal itself is stored as floats.
// AUTOINJECT
bool Plane_PlaneEq(plane_equ_tag *planeEq, _VECTOR *v1, _VECTOR *v2, _VECTOR *v3) {
  double edge1x = (double)v2->x - v1->x;
  double edge1y = (double)v2->y - v1->y;
  double edge1z = (double)v2->z - v1->z;
  float edge2x = v3->x - v2->x;
  float edge2y = v3->y - v2->y;
  double edge2z = (double)v3->z - v2->z;

  _VECTOR normal;
  normal.x = (float)(edge2z * edge1y - edge2y * edge1z);
  normal.y = (float)(edge1z * edge2x - edge2z * edge1x);
  normal.z = (float)(edge2y * edge1x - edge1y * edge2x);

  // Collinear or coincident points
  double length = sqrt((double)normal.z * normal.z + (double)normal.y * normal.y + (double)normal.x * normal.x);
  if (length == 0.0) {
    planeEq->d = 0;
    planeEq->c = 0;
    planeEq->b = 0;
    planeEq->a = 0;
    return false;
  }

  double scale = 1.0 / length;
  double a = normal.x * scale;
  double b = normal.y * scale;
  double c = normal.z * scale;
  planeEq->a = (float)a;
  planeEq->b = (float)b;
  planeEq->c = (float)c;
  planeEq->d = (float)-(b * v1->y + a * v1->x + c * v1->z);
  return true;
}

// Left unrounded on the x87, z first
// AUTOINJECT
double DistancePointToPlane(_VECTOR *point, plane_equ_tag *plane) {
  return (double)point->z * plane->c + (double)point->y * plane->b + (double)point->x * plane->a + plane->d;
}

// AUTOINJECT
void auxVec_AddMulR32(_VECTOR *add, _VECTOR *vIn, float multiply, _VECTOR *vOut) {
  vOut->x = multiply * vIn->x + add->x;
  vOut->y = multiply * vIn->y + add->y;
  vOut->z = multiply * vIn->z + add->z;
}

// Whether the point is outside the edge from 'from' to 'to': on the side the edge's cross product with the plane's
// normal points to. On the x87: the edge, the cross product's z and the point's offset stay unrounded; the cross
// product's x and y are stored as floats.
static bool OutsideEdge(_VECTOR *point, _VECTOR *from, _VECTOR *to, _VECTOR *normal) {
  double edgeX = (double)to->x - from->x;
  double edgeY = (double)to->y - from->y;
  double edgeZ = (double)to->z - from->z;
  float crossX = (float)(edgeY * normal->z - edgeZ * normal->y);
  float crossY = (float)(edgeZ * normal->x - edgeX * normal->z);
  double crossZ = edgeX * normal->y - edgeY * normal->x;

  double offsetX = (double)point->x - to->x;
  double offsetY = (double)point->y - to->y;
  double offsetZ = (double)point->z - to->z;
  return offsetZ * crossZ + offsetY * crossY + offsetX * crossX > 0.0;
}

// Whether a point on the triangle's plane is inside the triangle
// AUTOINJECT
bool vecutil_point_on_poly(_VECTOR *point, _VECTOR *vtx1, _VECTOR *vtx2, _VECTOR *vtx3, plane_equ_tag *plane) {
  if (OutsideEdge(point, vtx3, vtx1, &plane->normal))
    return false;
  if (OutsideEdge(point, vtx1, vtx2, &plane->normal))
    return false;
  if (OutsideEdge(point, vtx2, vtx3, &plane->normal))
    return false;
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

// Summed on the x87, z first; each component stored before the next is read, so vecIn may be vecOut
// AUTOINJECT
void MatrixMultiplyVector(_MATRIX *mtx, _VECTOR *vecIn, _VECTOR *vecOut) {
  vecOut->x = (float)((double)mtx->m[8] * vecIn->z + (double)mtx->m[4] * vecIn->y + (double)mtx->m[0] * vecIn->x + mtx->m[0xc]);
  vecOut->y = (float)((double)mtx->m[9] * vecIn->z + (double)mtx->m[1] * vecIn->x + (double)mtx->m[5] * vecIn->y + mtx->m[0xd]);
  vecOut->z = (float)((double)mtx->m[10] * vecIn->z + (double)mtx->m[2] * vecIn->x + (double)mtx->m[6] * vecIn->y + mtx->m[0xe]);
}

// Summed on the x87, z first; each component stored before the next is read, so vIn may be vOut
// AUTOINJECT
void ApplyMatrixLV(_MATRIX *mtx, _VECTOR *vIn, _VECTOR *vOut) {
  vOut->x = (float)((double)mtx->m[8] * vIn->z + (double)mtx->m[4] * vIn->y + (double)mtx->m[0] * vIn->x);
  vOut->y = (float)((double)mtx->m[9] * vIn->z + (double)mtx->m[1] * vIn->x + (double)mtx->m[5] * vIn->y);
  vOut->z = (float)((double)mtx->m[10] * vIn->z + (double)mtx->m[2] * vIn->x + (double)mtx->m[6] * vIn->y);
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

// On the x87: the pitch's cosine and the products stay unrounded
// AUTOINJECT
void Vec_Spherical_2_Cartesian(float *out, float radius, float yaw, float pitch) {
  Extended cosPitch = FCos(pitch);
  out[0] = (float)(FSin(yaw) * cosPitch * radius);
  out[2] = (float)(FCos(yaw) * cosPitch * radius);
  out[1] = (float)(FSin(pitch) * radius);
}

// Coefficients for maybeAtan2's polynomial approximation of atan(ratio) over ratio in [0,1]
#define MAYBE_ATAN2_COEFF_A 1.0596788f
#define MAYBE_ATAN2_COEFF_B 0.27131295f

// Fast rational atan2 approximation (max error ~0.28 degrees)
static float maybeAtan2Body(float y, float x) {
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

// The original writes neither ECX nor EDX, and its callers rely on it: Vec_Cartesian_2_Spherical keeps ECX across the call. So the
// game reaches this through an entry that keeps them (tools/caller_saved.py).
// AUTOINJECT
float __declspec(naked) maybeAtan2(float y, float x) {
    _asm {
        push ecx
        push edx
        push dword ptr [esp + 16]
        push dword ptr [esp + 16]
        call maybeAtan2Body
        add esp, 8
        pop edx
        pop ecx
        ret
    }
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

// AUTOGEN
void MulMatrix0(float *a, float *b, float *out);

// AUTOGEN
void Mat_GetPosition(_VECTOR *posOut, _MATRIX *mtxIn);

// AUTOGEN
void Vec_Scalef(_VECTOR *vIn, float scale, _VECTOR *vOut);

// Rotates a vector by a matrix, in place
// AUTOGEN
void ApplyMatrixLVI(float *matrix, float *vector);

// AUTOGEN
void Mat_GetUp(_VECTOR *vOut, _MATRIX *mIn);
