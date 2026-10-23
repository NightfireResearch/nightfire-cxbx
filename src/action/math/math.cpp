#include "math.h"
#include "X87Ops.h"
#include "../actionhelpers.h"
#include <bit>
#include <cmath>

inline float ABS(float n) {
	if(n < 0)
		return -n;
	return n;
}

inline bool ISNAN(float n) {
	return n != n;
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

// Each component's difference, taken on the x87 unrounded, within the threshold (a NaN difference passes)
// AUTOINJECT
bool Quat_IsEqual(const quaternion_tag *a, const quaternion_tag *b, float threshold) {
  for (int i = 0; i < 4; i++) {
    if (fabs((double)b->q[i] - a->q[i]) > threshold)
      return false;
  }
  return true;
}

// Alternate order (xyzw) - used by DirectX? Summed on the x87.
// AUTOINJECT
void MultiplyQuaternionAltOrder(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *qOut) {
  const float *p = a->q, *q = b->q;
  qOut->q[0] = (float)((((double)q[3] * p[0] + (double)q[2] * p[1]) + (double)p[3] * q[0]) - (double)q[1] * p[2]);
  qOut->q[1] = (float)((((double)p[2] * q[0] + (double)q[3] * p[1]) + (double)p[3] * q[1]) - (double)p[0] * q[2]);
  qOut->q[2] = (float)((((double)q[3] * p[2] + (double)q[1] * p[0]) + (double)p[3] * q[2]) - (double)q[0] * p[1]);
  qOut->q[3] = (float)((((double)q[3] * p[3] - (double)p[0] * q[0]) - (double)q[1] * p[1]) - (double)p[2] * q[2]);
}

// Summed on the x87
// AUTOINJECT
void Quat_QuaternionMultiply(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *qOut) {
  const float *p = a->q, *q = b->q;
  qOut->q[0] = (float)((((double)p[0] * q[0] - (double)q[1] * p[1]) - (double)q[2] * p[2]) - (double)q[3] * p[3]);
  qOut->q[1] = (float)((((double)q[3] * p[2] - (double)q[2] * p[3]) + (double)p[1] * q[0]) + (double)q[1] * p[0]);
  qOut->q[2] = (float)((((double)q[1] * p[3] - (double)q[3] * p[1]) + (double)p[2] * q[0]) + (double)q[2] * p[0]);
  qOut->q[3] = (float)((((double)q[2] * p[1] - (double)q[1] * p[2]) + (double)p[3] * q[0]) + (double)q[3] * p[0]);
}

// The rotation of a quaternion (x, y, z, w). On the x87: the doubled x and y and w's products with y and z stay
// unrounded; the doubled z and the other products are stored as floats.
static void QuatRotation(quaternion_tag *quat, _MATRIX *m) {
  const float *q = quat->q;
  double x2 = (double)q[0] + q[0];
  double y2 = (double)q[1] + q[1];
  float z2 = q[2] + q[2];
  float xx = (float)(x2 * q[0]);
  float xy = (float)(y2 * q[0]);
  float xz = z2 * q[0];
  float yy = (float)(y2 * q[1]);
  float yz = z2 * q[1];
  float zz = z2 * q[2];
  float wx = (float)(x2 * q[3]);
  double wy = y2 * q[3];
  double wz = (double)z2 * q[3];
  m->m[0] = (float)(1.0f - ((double)zz + yy));
  m->m[1] = (float)(xy + wz);
  m->m[2] = (float)(xz - wy);
  m->m[4] = (float)(xy - wz);
  m->m[5] = (float)(1.0f - ((double)zz + xx));
  m->m[6] = (float)((double)wx + yz);
  m->m[8] = (float)(wy + xz);
  m->m[9] = (float)((double)yz - wx);
  m->m[10] = (float)(1.0f - ((double)yy + xx));
}


// The rotation of a quaternion (x, y, z, w) and a translation
// AUTOINJECT
void Quat_QuatTransToMat(quaternion_tag *quatIn, float *vecIn, _MATRIX *mOut) {
  QuatRotation(quatIn, mOut);
  mOut->m[12] = vecIn[0];
  mOut->m[13] = vecIn[1];
  mOut->m[14] = vecIn[2];
}

// The rotation of a quaternion (x, y, z, w), Quat_QuatTransToMat's
void Quat_QuatToMat(quaternion_tag *quatIn, _MATRIX *mOut) {
  QuatRotation(quatIn, mOut);
}

// Convert a rotation matrix to a quaternion (Shepperd's method), summed on the x87
// AUTOINJECT
void Quat_MatToQuat(quaternion_tag *quatOut, _MATRIX *mtxIn) {
  const float *m = mtxIn->m;
  double trace = ((double)m[5] + m[0]) + m[10];
  if (trace >= 0.0) {
    double s = sqrt(trace + 1.0f);
    quatOut->q[3] = (float)(0.5f * s);
    double scale = 0.5f / s;
    quatOut->q[0] = (float)(((double)m[6] - m[9]) * scale);
    quatOut->q[1] = (float)(((double)m[8] - m[2]) * scale);
    quatOut->q[2] = (float)(((double)m[1] - m[4]) * scale);
    return;
  }

  // Diagonal indices in the 4-wide-stride matrix are i*5 (0, 5, 10)
  static const int next[3] = {1, 2, 0};
  int i = (m[5] > m[0]) ? 1 : 0;
  if (m[10] > m[i * 5]) {
    i = 2;
  }
  int j = next[i];
  int k = next[j];

  double s = sqrt(((double)m[i * 5] - ((double)m[k * 5] + m[j * 5])) + 1.0f);

  float q[4];
  q[i] = (float)(0.5f * s);
  if (s != 0.0) {
    s = 0.5f / s;
  }
  q[3] = (float)(((double)m[j * 4 + k] - m[k * 4 + j]) * s);
  q[j] = (float)(((double)m[j * 4 + i] + m[i * 4 + j]) * s);
  q[k] = (float)(((double)m[k * 4 + i] + m[i * 4 + k]) * s);

  quatOut->q[0] = q[0];
  quatOut->q[1] = q[1];
  quatOut->q[2] = q[2];
  quatOut->q[3] = q[3];
}

// Accurate (as opposed to Quat_Slerp's fast approximation) spherical interpolation between two quaternions. The angle
// comes from the XBE's own acos of the cosine as a float, its sines by FSIN; the angle, the reciprocal of its sine and
// the blended angle are stored as floats.
// AUTOINJECT
void Quat_Slerp_Acc(float blendFactor, float *quatStart, float *quatEnd, float *quatOut) {
  double dot = (((double)quatStart[0] * quatEnd[0] + (double)quatStart[2] * quatEnd[2]) +
                (double)quatStart[1] * quatEnd[1]) + (double)quatEnd[3] * quatStart[3];
  float cosOmega = (float)dot;
  bool negate = dot < 0.0;
  if (negate) {
    cosOmega = -cosOmega;
  }

  double scaleEnd, scaleStart;
  if (1.0 - cosOmega > 0.01f) {
    Extended omega = CIAcos(cosOmega);
    float angle = (float)omega;
    float reciprocal = (float)(Extended(1.0f) / FSin(omega));
    float blended = (float)((double)angle * blendFactor);
    scaleEnd = (double)(FSin((double)angle * blendFactor) * Extended(reciprocal));
    scaleStart = (double)(FSin((double)angle - blended) * Extended(reciprocal));
  } else {
    // Quaternions are nearly coincident (or the cosine a NaN) - fall back to linear interpolation
    scaleEnd = blendFactor;
    scaleStart = 1.0 - blendFactor;
  }

  if (negate) {
    scaleStart = -scaleStart;
  }

  quatOut[0] = (float)(scaleStart * quatStart[0] + scaleEnd * quatEnd[0]);
  quatOut[1] = (float)(scaleStart * quatStart[1] + scaleEnd * quatEnd[1]);
  quatOut[2] = (float)(scaleStart * quatStart[2] + scaleEnd * quatEnd[2]);
  quatOut[3] = (float)(scaleEnd * quatEnd[3] + scaleStart * quatStart[3]);
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
// re-derived translation) result into worldMtx - see the callers for confirmation of this direction. Summed on the x87.
// AUTOINJECT
void Mat_World2ViewMat(_MATRIX *viewMtx, _MATRIX *worldMtx) {
  float *w = worldMtx->m;
  const float *v = viewMtx->m;
  w[0] = v[0];
  w[4] = v[1];
  w[8] = v[2];
  w[1] = v[4];
  w[5] = v[5];
  w[9] = v[6];
  w[2] = v[8];
  w[6] = v[9];
  w[10] = v[10];

  w[0xc] = (float)-(((double)v[0xe] * w[2] + (double)w[0] * v[0xc]) + (double)w[1] * v[0xd]);
  w[0xd] = (float)-(((double)v[0xe] * w[6] + (double)w[5] * v[0xd]) + (double)v[0xc] * w[4]);
  w[0xe] = (float)-(((double)v[0xe] * w[10] + (double)v[0xd] * w[9]) + (double)w[8] * v[0xc]);
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
// orthogonal to both (eg. used to keep a turret/sensor upright while aiming roughly towards a target). The cross
// products are taken on the x87.
// AUTOINJECT
void Mat_Align2Up(float *matrix, float *up, float *direction) {
  _VECTOR right;
  right.x = (float)((double)direction[2] * up[1] - (double)direction[1] * up[2]);
  right.y = (float)((double)direction[0] * up[2] - (double)direction[2] * up[0]);
  right.z = (float)((double)direction[1] * up[0] - (double)direction[0] * up[1]);
  Vec_Normalise(&right, &right);

  _VECTOR forward;
  forward.x = (float)((double)right.y * up[2] - (double)right.z * up[1]);
  forward.y = (float)((double)right.z * up[0] - (double)right.x * up[2]);
  forward.z = (float)((double)right.x * up[1] - (double)right.y * up[0]);
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

// Combine the rotation and translation of two matrices: mat2's rows through mat1's rotation (mat2 read before it is
// written), and the translations added. Summed on the x87.
// AUTOINJECT
void RotTransMat(_MATRIX *mat1, _MATRIX *mat2) {
  const float *a = mat1->m;
  float *b = mat2->m;
  float b0 = b[0], b1 = b[1], b2 = b[2];
  float b4 = b[4], b5 = b[5], b6 = b[6];
  float b8 = b[8], b9 = b[9], b10 = b[10];
  b[0] = (float)(((double)b1 * a[4] + (double)b2 * a[8]) + (double)b0 * a[0]);
  b[1] = (float)(((double)b1 * a[5] + (double)b2 * a[9]) + (double)b0 * a[1]);
  b[2] = (float)(((double)b2 * a[10] + (double)b0 * a[2]) + (double)b1 * a[6]);
  b[4] = (float)(((double)b5 * a[4] + (double)b6 * a[8]) + (double)b4 * a[0]);
  b[5] = (float)(((double)b5 * a[5] + (double)b6 * a[9]) + (double)b4 * a[1]);
  b[6] = (float)(((double)b6 * a[10] + (double)b4 * a[2]) + (double)b5 * a[6]);
  b[8] = (float)(((double)b9 * a[4] + (double)b10 * a[8]) + (double)b8 * a[0]);
  b[9] = (float)(((double)b9 * a[5] + (double)b10 * a[9]) + (double)b8 * a[1]);
  b[10] = (float)(((double)b10 * a[10] + (double)b8 * a[2]) + (double)b9 * a[6]);
  b[12] = a[12] + b[12];
  b[13] = a[13] + b[13];
  b[14] = a[14] + b[14];
}

// AUTOINJECT
void Matrix_SetTrans(_VECTOR *vec, _MATRIX *mtx) {
  mtx->m[0xc] = vec->x;
  mtx->m[0xd] = vec->y;
  mtx->m[0xe] = vec->z;
}

// To unit length by the reciprocal of the length (both unrounded on the x87); a zero length gives zero (a NaN one
// goes through)
void Vec_Normalise(_VECTOR *vOut, _VECTOR *vIn) {
  double magnitude = Vec_Magnitude(vIn);
  if (magnitude == 0.0) {
    vOut->z = 0.0;
    vOut->y = 0.0;
    vOut->x = 0.0;
    return;
  }

  double rcpMag = 1.0f / magnitude;
  vOut->x = (float)(rcpMag * vIn->x);
  vOut->y = (float)(rcpMag * vIn->y);
  vOut->z = (float)(rcpMag * vIn->z);
}

// The length is the unrounded one
double Vec_NormaliseLen(_VECTOR *output, _VECTOR *input) {
  double len = Vec_Magnitude(input);
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

// Negated on the x87 (FCHS), so a signalling NaN comes back quiet
// AUTOINJECT
void Vec_Negate(const _VECTOR *vIn,_VECTOR *vOut) {
  vOut->x = X87Negate(vIn->x);
  vOut->y = X87Negate(vIn->y);
  vOut->z = X87Negate(vIn->z);
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

// Each product's difference taken on the x87
// AUTOINJECT
void Vec_Cross(_VECTOR *a,_VECTOR *b,_VECTOR *vecOut) {
  vecOut->x = (float)((double)b->z * a->y - (double)b->y * a->z);
  vecOut->y = (float)((double)b->x * a->z - (double)a->x * b->z);
  vecOut->z = (float)((double)a->x * b->y - (double)b->x * a->y);
}

// Copied through the x87, dst2 first
// AUTOINJECT
void Vec_Copy2(_VECTOR* src, _VECTOR *dst1, _VECTOR *dst2) {
  dst2->x = X87Copy(src->x);
  dst1->x = X87Copy(src->x);
  dst2->y = X87Copy(src->y);
  dst1->y = X87Copy(src->y);
  dst2->z = X87Copy(src->z);
  dst1->z = X87Copy(src->z);
}

// Each difference within epsilon: x's taken unrounded, y's and z's stored as floats; a NaN difference passes
// AUTOINJECT
bool Vec_IsEqual(_VECTOR *a,_VECTOR *b,float epsilon) {
  double dx = (double)b->x - a->x;
  float dy = b->y - a->y;
  float dz = b->z - a->z;
  if (fabs(dx) > epsilon || ABS(dy) > epsilon || ABS(dz) > epsilon)
    return false;
  return true;
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

// Summed on the x87 and left unrounded
// AUTOINJECT
double Vec_Magnitude(_VECTOR *a) {
  return sqrt(((double)a->x * a->x + (double)a->y * a->y) + (double)a->z * a->z);
}

// x and y go through the x87 (a signalling NaN comes back quiet), z is moved as it is
// AUTOINJECT
void Vec_Max(_VECTOR *a, _VECTOR *b, _VECTOR *vecOut) {
  vecOut->x = X87Copy(MAX(a->x, b->x));
  vecOut->y = X87Copy(MAX(a->y, b->y));
  vecOut->z = MAX(a->z, b->z);
}

// As Vec_Max
// AUTOINJECT
void Vec_Min(_VECTOR *a, _VECTOR *b, _VECTOR *vecOut) {
  vecOut->x = X87Copy(MIN(a->x, b->x));
  vecOut->y = X87Copy(MIN(a->y, b->y));
  vecOut->z = MIN(a->z, b->z);
}

// The minimums first, then the maximums; x and y through the x87, as Vec_Max
// AUTOINJECT
void Vec_MinMax(_VECTOR *a, _VECTOR *b, _VECTOR *minimums, _VECTOR *maximums) {
  minimums->x = X87Copy(MIN(a->x, b->x));
  minimums->y = X87Copy(MIN(a->y, b->y));
  minimums->z = MIN(a->z, b->z);
  maximums->x = X87Copy(MAX(a->x, b->x));
  maximums->y = X87Copy(MAX(a->y, b->y));
  maximums->z = MAX(a->z, b->z);
}

// As Vec_MinMax, of three
// AUTOINJECT
void Vec_MinMax3(_VECTOR *a, _VECTOR *b, _VECTOR *c, _VECTOR *minimums, _VECTOR *maximums) {
  minimums->x = X87Copy(MIN3(a->x, b->x, c->x));
  minimums->y = X87Copy(MIN3(a->y, b->y, c->y));
  minimums->z = MIN3(a->z, b->z, c->z);
  maximums->x = X87Copy(MAX3(a->x, b->x, c->x));
  maximums->y = X87Copy(MAX3(a->y, b->y, c->y));
  maximums->z = MAX3(a->z, b->z, c->z);
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

// The point moved along the plane's normal onto it. The distance stays unrounded on the x87, y first.
// AUTOINJECT
void Plane_PutPntOnPlane(_VECTOR *out, _VECTOR *point, plane_equ_tag *plane) {
  double k = -((double)point->y * plane->b + (double)point->x * plane->a + (double)plane->c * point->z + plane->d);
  out->x = (float)(k * plane->a + point->x);
  out->y = (float)(k * plane->b + point->y);
  out->z = (float)(k * plane->c + point->z);
}

// Multiplied and added on the x87
// AUTOINJECT
void auxVec_AddMulR32(_VECTOR *add, _VECTOR *vIn, float multiply, _VECTOR *vOut) {
  vOut->x = (float)((double)multiply * vIn->x + add->x);
  vOut->y = (float)((double)multiply * vIn->y + add->y);
  vOut->z = (float)((double)multiply * vIn->z + add->z);
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

// The point of the segment a to b nearest p, and how far along it is (0 to 1)
// AUTOINJECT
double vecutil_calculate_closest_point_on_line(_VECTOR *out, _VECTOR *a, _VECTOR *b, _VECTOR *p) {
  _VECTOR d;
  d.x = b->x - a->x;
  d.y = b->y - a->y;
  d.z = b->z - a->z;
  double lengthSq = ((double)d.y * d.y + (double)d.x * d.x) + (double)d.z * d.z;
  if (AbsFchs(lengthSq) > 1e-20f) {
    double t = ((((double)p->y - a->y) * d.y + ((double)p->z - a->z) * d.z) + ((double)p->x - a->x) * d.x) / lengthSq;
    if (t > 1.0) {
      *out = *b;
      return 1.0f;
    }
    if (!(t < 0.0)) {
      out->x = (float)(d.x * t + a->x);
      out->y = (float)(d.y * t + a->y);
      out->z = (float)(d.z * t + a->z);
      return t;
    }
  }
  *out = *a;
  return 0.0f;
}

// The point of the triangle nearest p (on its plane, planeDist from p): the plane's, or an edge's or a corner's when p
// lies outside the edges (code: a bit for each edge, from c-a). On the x87 the point's x and y stay unrounded.
struct TrianglePoint {
  double x, y;
  float z;
};

static TrianglePoint NearestOnTriangle(_VECTOR *p, _VECTOR *a, _VECTOR *b, _VECTOR *c, plane_equ_tag *plane,
                                       float planeDist, unsigned short *code) {
  _VECTOR *verts[3] = {a, b, c};
  *code = 0;
  for (unsigned i = 0; i < 3 && *code < 2; i++) {
    _VECTOR *from = verts[(i + 2) % 3], *to = verts[i];
    double edgeX = (double)to->x - from->x;
    double edgeY = (double)to->y - from->y;
    double edgeZ = (double)to->z - from->z;
    float crossX = (float)(edgeY * plane->c - edgeZ * plane->b);
    float crossY = (float)(edgeZ * plane->a - edgeX * plane->c);
    double crossZ = edgeX * plane->b - edgeY * plane->a;
    float offsetX = p->x - to->x;
    float offsetY = p->y - to->y;
    double offsetZ = (double)p->z - to->z;
    if ((offsetZ * crossZ + (double)offsetY * crossY) + (double)offsetX * crossX > 0.0)
      *code |= 1 << i;
  }
  TrianglePoint nearest;
  _VECTOR onEdge;
  switch (*code) {
  case 1:
    vecutil_calculate_closest_point_on_line(&onEdge, a, c, p);
    nearest.x = onEdge.x;
    nearest.y = onEdge.y;
    nearest.z = onEdge.z;
    break;
  case 2:
    vecutil_calculate_closest_point_on_line(&onEdge, b, a, p);
    nearest.x = onEdge.x;
    nearest.y = onEdge.y;
    nearest.z = onEdge.z;
    break;
  case 3:
    nearest.x = a->x;
    nearest.y = a->y;
    nearest.z = a->z;
    break;
  case 4:
    vecutil_calculate_closest_point_on_line(&onEdge, c, b, p);
    nearest.x = onEdge.x;
    nearest.y = onEdge.y;
    nearest.z = onEdge.z;
    break;
  case 5:
    nearest.x = c->x;
    nearest.y = c->y;
    nearest.z = c->z;
    break;
  case 6:
    nearest.x = b->x;
    nearest.y = b->y;
    nearest.z = b->z;
    break;
  default: {
    float back = -planeDist;
    nearest.x = (double)back * plane->a + p->x;
    nearest.y = (double)back * plane->b + p->y;
    nearest.z = (float)((double)back * plane->c + p->z);
    break;
  }
  }
  return nearest;
}

// How far p is from the point, which goes to closest if it is given
static double DistanceToTrianglePoint(_VECTOR *p, const TrianglePoint &nearest, _VECTOR *closest) {
  if (closest != NULL) {
    closest->x = (float)nearest.x;
    closest->z = nearest.z;
    closest->y = (float)nearest.y;
  }
  float dx = (float)(nearest.x - p->x);
  double dy = nearest.y - p->y;
  double dz = (double)nearest.z - p->z;
  return sqrt((dz * dz + dy * dy) + (double)dx * dx);
}

// How far p is from the triangle; behind its plane, the (negative) distance to the plane, and code 99
// AUTOINJECT
double vecutil_Dist2Tri(_VECTOR *p, _VECTOR *a, _VECTOR *b, _VECTOR *c, plane_equ_tag *plane, unsigned short *code, _VECTOR *closest) {
  double planeDist = (((double)p->y * plane->b + (double)plane->c * p->z) + (double)p->x * plane->a) + plane->d;
  float planeDistStored = (float)planeDist;
  if (planeDist < 0.0) {
    *code = 99;
    return planeDistStored;
  }
  return DistanceToTrianglePoint(p, NearestOnTriangle(p, a, b, c, plane, planeDistStored, code), closest);
}

// As vecutil_Dist2Tri, with the distance to the plane given, from either side
// AUTOINJECT
double vecutil_Dist2Tri_PointGeom(_VECTOR *p, _VECTOR *a, _VECTOR *b, _VECTOR *c, plane_equ_tag *plane, float planeDist, _VECTOR *closest) {
  unsigned short code;
  return DistanceToTrianglePoint(p, NearestOnTriangle(p, a, b, c, plane, planeDist, &code), closest);
}

// Where the segments p1-p2 and p3-p4 cross in plan (x and z): 0 if they do not, 1 if they lie along each other (out
// not written), 2 with the point's x and z in out
// AUTOINJECT
int vecutil_intersect_line_line_2d(_VECTOR *p1, _VECTOR *p2, _VECTOR *p3, _VECTOR *p4, _VECTOR *out) {
  float ax = p2->x - p1->x;
  float bx = p3->x - p4->x;
  float low, high;
  if (ax < 0.0f) {
    low = p2->x;
    high = p1->x;
  } else {
    high = p2->x;
    low = p1->x;
  }
  if (bx > 0.0f) {
    if (high < p4->x || p3->x < low)
      return 0;
  } else {
    if (high < p3->x || p4->x < low)
      return 0;
  }
  float az = p2->z - p1->z;
  float bz = p3->z - p4->z;
  if (az < 0.0f) {
    low = p2->z;
    high = p1->z;
  } else {
    high = p2->z;
    low = p1->z;
  }
  if (bz > 0.0f) {
    if (high < p4->z || p3->z < low)
      return 0;
  } else {
    if (high < p3->z || p4->z < low)
      return 0;
  }
  float cx = p1->x - p3->x;
  float cz = p1->z - p3->z;
  float d = (float)((double)cx * bz - (double)cz * bx);
  float f = (float)((double)az * bx - (double)bz * ax);
  if (f > 0.0f) {
    if (d < 0.0f || d > f)
      return 0;
  } else {
    if (d > 0.0f || d < f)
      return 0;
  }
  double e = (double)cz * ax - (double)cx * az;
  if (f > 0.0f) {
    if (e < 0.0 || e > f)
      return 0;
  } else {
    if (e > 0.0 || e < f)
      return 0;
  }
  if (f <= 0.0002f && f >= -0.0002f)
    return 1;
  double inverse = 1.0 / f;
  out->x = (float)((double)d * inverse * ax + p1->x);
  out->z = (float)(inverse * d * az + p1->z);
  return 2;
}

// On the x87, z's square first, and left unrounded
// AUTOINJECT
double Vec_Dist3D(_VECTOR *a, _VECTOR *b) {
  return sqrt(Vec_SqDist3D(a, b));
}

// AUTOINJECT
double Vec_SqDist3D(_VECTOR *a, _VECTOR *b) {
  double dx = (double)a->x - b->x;
  double dy = (double)a->y - b->y;
  double dz = (double)a->z - b->z;
  return (dz * dz + dx * dx) + dy * dy;
}

// b's x and y come from the x87 (a signalling NaN comes back quiet)
// AUTOINJECT
void Vec_Swap(_VECTOR *a, _VECTOR *b) {
  float tmp_x = a->x;
  float tmp_y = a->y;
  float tmp_z = a->z;
  a->x = b->x;
  a->y = b->y;
  a->z = b->z;
  b->x = X87Copy(tmp_x);
  b->y = X87Copy(tmp_y);
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

// a along the unit normal of b and c, summed on the x87 from z and left unrounded
// AUTOINJECT
double Vec_ScalarTripleProduct(_VECTOR *a, _VECTOR *b, _VECTOR *c) {
  _VECTOR v1;
  Vec_Cross(b, c, &v1);
  Vec_Normalise(&v1, &v1);
  return ((double)v1.z * a->z + (double)v1.y * a->y) + (double)v1.x * a->x;
}

// The pitch and heading of a direction by FPATAN
// AUTOINJECT
void vecutil_cartesian_to_spherical_acc(_VECTOR *vec, float v_x, float v_y, float v_z) {
  vec->z = 0.0f;
  vec->x = (float)FPatan(v_y, sqrt((double)v_x * v_x + (double)v_z * v_z));
  vec->y = (float)FPatan(v_x, v_z);
}

// Shortest signed angular difference (angle2 - angle1), wrapped into (-pi, pi]. Both inputs are brought into [0, 2pi)
// first: angle2 stored as a float, angle1 left unrounded, and so is the result. The remainder is exact either way, so
// the C runtime's fmod does what the XBE's FPREM does (for finite angles: the XBE's raises its error for the others).
// AUTOINJECT
double Vec_AngleDifference(float angle1, float angle2) {
  const float twoPi = (float)M_2PI;
  if (angle2 < 0.0f || !(angle2 < twoPi)) {
    double wrapped = fmod((double)angle2, (double)twoPi);
    if (!(wrapped >= 0.0))
      wrapped += twoPi;
    angle2 = (float)wrapped;
  }
  double first = angle1;
  if (angle1 < 0.0f || !(angle1 < twoPi)) {
    first = fmod((double)angle1, (double)twoPi);
    if (!(first >= 0.0))
      first += twoPi;
  }

  double diff = angle2 - first;
  if (!(AbsFchs(diff) < (float)M_PI)) {
    if (diff < 0.0)
      return diff + twoPi;
    return diff - twoPi;
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

// Coefficients for Atan2's polynomial approximation of atan(ratio) over ratio in [0,1]
#define ATAN2_COEFF_A 1.0596788f
#define ATAN2_COEFF_B 0.27131295f
static_assert(std::bit_cast<uint32_t>(ATAN2_COEFF_A) == 0x3f87a38e, "not the original's constant");
static_assert(std::bit_cast<uint32_t>(ATAN2_COEFF_B) == 0x3e8ae988, "not the original's constant");

// Fast rational atan2 approximation (max error ~0.28 degrees), on the x87 and left unrounded
double Atan2(float y, float x) {
  double result;

  if (x == y) {
    result = (x == 0.0f) ? 0.0f : (float)M_PI_4;
  } else {
    double absX = AbsFchs(x);
    double absY = AbsFchs(y);
    if (absY < absX) {
      double ratio = absY / absX;
      result = (ATAN2_COEFF_A - ratio * ATAN2_COEFF_B) * ratio;
    } else {
      double ratio = absX / absY;
      result = (float)M_PI_2 - (ATAN2_COEFF_A - ratio * ATAN2_COEFF_B) * ratio;
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

// Align a matrix so its forward row faces "direction", with "upHint" as the preferred up hint (falling back to
// "altHint" when direction is nearly parallel to upHint). The dot and cross products are taken on the x87.
// AUTOINJECT
void Mat_Align2Dir(_MATRIX *mtxOut, _VECTOR *direction, _VECTOR *upHint, _VECTOR *altHint) {
  _VECTOR dir;
  Vec_Normalise(&dir, direction);

  double dot = ((double)dir.x * upHint->x + (double)dir.z * upHint->z) + (double)dir.y * upHint->y;
  _VECTOR *hint = AbsFchs(dot) > 0.999f ? altHint : upHint;

  _VECTOR right;
  right.x = (float)((double)dir.z * hint->y - (double)dir.y * hint->z);
  right.y = (float)((double)dir.x * hint->z - (double)dir.z * hint->x);
  right.z = (float)((double)dir.y * hint->x - (double)dir.x * hint->y);
  Vec_Normalise(&right, &right);

  _VECTOR up;
  up.x = (float)((double)right.z * dir.y - (double)right.y * dir.z);
  up.y = (float)((double)right.x * dir.z - (double)right.z * dir.x);
  up.z = (float)((double)right.y * dir.x - (double)right.x * dir.y);
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
// end up orthogonal. The cross products are taken on the x87.
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

  right.x = (float)((double)forward.z * up.y - (double)forward.y * up.z);
  right.y = (float)((double)forward.x * up.z - (double)forward.z * up.x);
  right.z = (float)((double)forward.y * up.x - (double)forward.x * up.y);
  Vec_Normalise(&right, &right);

  mtx->m[8] = forward.x;
  mtx->m[9] = forward.y;
  mtx->m[10] = forward.z;
  mtx->m[4] = (float)((double)right.z * forward.y - (double)right.y * forward.z);
  mtx->m[6] = (float)((double)right.y * forward.x - (double)right.x * forward.y);
  mtx->m[5] = (float)((double)right.x * forward.z - (double)right.z * forward.x);
  mtx->m[0] = right.x;
  mtx->m[1] = right.y;
  mtx->m[2] = right.z;
}

// AUTOINJECT
void Mat_GetPosition(_VECTOR *posOut, _MATRIX *mtxIn) {
  *posOut = *Mat_Position(*mtxIn);
}

// AUTOINJECT
void Vec_Scalef(_VECTOR *vIn, float scale, _VECTOR *vOut) {
  vOut->x = scale * vIn->x;
  vOut->y = scale * vIn->y;
  vOut->z = scale * vIn->z;
}

// Each component by its own scale
// AUTOINJECT
void Vec_Mul_F3(_VECTOR *vecOut, _VECTOR *vecIn, float scaleX, float scaleY, float scaleZ) {
  vecOut->x = scaleX * vecIn->x;
  vecOut->y = scaleY * vecIn->y;
  vecOut->z = scaleZ * vecIn->z;
}

// vecOut += scale * vecIn, over four floats
// AUTOINJECT
void Vec4_MulAdd(float scale, float *vecIn, float *vecOut) {
  for (int i = 0; i < 4; i++)
    vecOut[i] = (float)((double)scale * vecIn[i] + vecOut[i]);
}

// Where the line from a through b, beyond a, leaves the sphere: the far root along the unit direction. 1 always
// (the root is never below zero; a miss gives NaNs). On the x87: the projection squared is the unrounded one by
// its float, and the root stays unrounded until the test.
// AUTOINJECT
int Vec_LineSphereIntersect(_VECTOR *a, _VECTOR *b, _VECTOR *centre, float radius, _VECTOR *out) {
  _VECTOR dir, unit;
  dir.x = b->x - a->x;
  dir.y = b->y - a->y;
  dir.z = b->z - a->z;
  Vec_Normalise(&unit, &dir);
  double cx = (double)centre->x - a->x;
  double cy = (double)centre->y - a->y;
  double cz = (double)centre->z - a->z;
  double along = (double)unit.z * cz + (double)unit.y * cy + cx * unit.x;
  float alongf = (float)along;
  double root = sqrt(along * alongf - (cz * cz + cy * cy + cx * cx) + (double)radius * radius);
  float rootf = (float)root;
  if (root < 0.0)
    return 0;
  double nearT = (double)alongf - rootf;
  float farT = (float)((double)rootf + alongf);
  double t = nearT > farT ? nearT : farT;
  out->x = (float)(unit.x * t + a->x);
  out->y = (float)(unit.y * t + a->y);
  out->z = (float)(unit.z * t + a->z);
  return 1;
}

// Where the segment from p to q meets the sphere: at the first root along it, which lies at or before q. A segment
// that starts inside the sphere (the second root not behind p) gives the first root too, behind p. 0 for a miss.
// Not named on PS2; the name is ours.
// AUTOINJECT
int Vec_SegmentSphereIntersect(_VECTOR *p, _VECTOR *q, _VECTOR *centre, float radius, _VECTOR *out) {
  double wx = (double)p->x - centre->x;
  double wy = (double)p->y - centre->y;
  double wz = (double)p->z - centre->z;
  _VECTOR d;
  d.x = q->x - p->x;
  d.y = q->y - p->y;
  double dz = (double)q->z - p->z;
  d.z = (float)dz;
  float a = (float)(dz * d.z + (double)d.y * d.y + (double)d.x * d.x);
  double b = (double)d.z * wz + (double)d.x * wx + (double)d.y * wy;
  float bf = (float)b;
  float disc = (float)(b * bf - (wx * wx + wz * wz + wy * wy - (double)radius * radius) * a);
  if (disc < 0.0f)
    return 0;
  double t;
  if (disc > 0.0f) {
    float root = (float)sqrt((double)disc);
    float invA = 1.0f / a;
    double t1 = (Negate((double)bf) - root) * invA;
    double t2 = ((double)root - bf) * invA;
    if (t1 > 1.0)
      return 0;
    if (t1 >= 0.0)
      t = t1;
    else if (t2 >= 0.0)
      t = t1;
    else
      return 0;
  } else {
    t = Negate((double)bf / a);
  }
  out->x = (float)(d.x * t + p->x);
  out->y = (float)(d.y * t + p->y);
  out->z = (float)(d.z * t + p->z);
  return 1;
}

// Turns current towards target by at most maxTurn, a rate's share of the way, and onto target within 0.0001. On the
// x87: the turn added to current stays unrounded into the step, and the result into the last test.
// AUTOINJECT
float Ang_RotateToBearing(float current, float target, float rate, float maxTurn) {
  double turn = Vec_AngleDifference(current, target);
  float minTurn = -maxTurn;
  if (turn < minTurn)
    turn = minTurn;
  else if (turn > maxTurn)
    turn = maxTurn;

  double end = turn + current;
  float reached = (float)end;
  double step = end - current;
  double angle = step * rate + current;
  if ((float)step >= 0.0f) {
    if (angle > reached)
      angle = reached;
  } else if (angle < reached) {
    angle = reached;
  }
  float result = (float)angle;
  if (fabs(angle - target) < 0.0001f)
    return target;
  return result;
}

// Summed on the x87, then divided by three (the float 1/3)
// AUTOINJECT
void Vec_Avg3(_VECTOR *a, _VECTOR *b, _VECTOR *c, _VECTOR *out) {
  const float third = 1.0f / 3.0f;
  out->x = (float)(((double)a->x + b->x + c->x) * third);
  out->y = (float)(((double)a->y + b->y + c->y) * third);
  out->z = (float)(((double)a->z + b->z + c->z) * third);
}

// The distance in plan (x and z), left unrounded on the x87
// AUTOINJECT
double Vec_SqDist2D(_VECTOR *a, _VECTOR *b) {
  double dz = (double)a->z - b->z;
  double dx = (double)a->x - b->x;
  return dx * dx + dz * dz;
}

// AUTOINJECT
double Vec_Dist2D(_VECTOR *a, _VECTOR *b) {
  double dz = (double)a->z - b->z;
  double dx = (double)a->x - b->x;
  return sqrt(dx * dx + dz * dz);
}

// AUTOINJECT
double Vec_SqMagnitude(_VECTOR *v) {
  return (double)v->x * v->x + (double)v->y * v->y + (double)v->z * v->z;
}

// AUTOINJECT
void Vec_UnitVector(_VECTOR *out, _VECTOR *from, _VECTOR *to) {
  out->x = to->x - from->x;
  out->y = to->y - from->y;
  out->z = to->z - from->z;
  Vec_Normalise(out, out);
}

// The pitch (x, up negative) and heading (y) that face along a normal, with FPATAN. Each angle is stored before the
// next reads the normal, so normal may be rot.
// AUTOINJECT
void Vec_NormToRot(_VECTOR *normal, _VECTOR *rot) {
  double plane = sqrt((double)normal->x * normal->x + (double)normal->z * normal->z);
  rot->x = (float)-FPatan(normal->y, plane);
  float x = normal->x, z = normal->z;
  rot->z = 0.0f;
  rot->y = (float)FPatan(x, z);
}

// As Vec_NormToRot, the heading first
// AUTOINJECT
void Vec_DirToRot(_VECTOR *dir, _VECTOR *rot) {
  rot->y = (float)FPatan(dir->x, dir->z);
  float z = dir->z, x = dir->x, y = dir->y;
  rot->z = 0.0f;
  rot->x = (float)-FPatan(y, sqrt((double)z * z + (double)x * x));
}

// AUTOINJECT
void Vec_Mul(_VECTOR *v, _VECTOR *scale) {
  v->x = v->x * scale->x;
  v->y = scale->y * v->y;
  v->z = scale->z * v->z;
}

// Two unit vectors at right angles to n and to each other, u level in the larger of n's x and y. On the x87: the
// reciprocal of the length and its product with the component that goes into v's first term stay unrounded.
// AUTOINJECT
void Vec_MakeOrthonormalBasis(_VECTOR *u, _VECTOR *v, _VECTOR *n) {
  if (AbsFchs(n->x) > AbsFchs(n->y)) {
    double scale = 1.0 / sqrt((double)n->z * n->z + (double)n->x * n->x);
    u->y = 0.0f;
    u->x = (float)(scale * n->z);
    double negX = -(scale * n->x);
    u->z = (float)negX;
    v->x = (float)(negX * n->y);
    v->y = (float)((double)n->z * u->x - (double)u->z * n->x);
    v->z = (float)-((double)u->x * n->y);
  } else {
    u->x = 0.0f;
    double scale = 1.0 / sqrt((double)n->z * n->z + (double)n->y * n->y);
    u->y = (float)-(scale * n->z);
    double y = scale * n->y;
    u->z = (float)y;
    v->x = (float)(y * n->y - (double)n->z * u->y);
    v->y = (float)-((double)u->z * n->x);
    v->z = (float)((double)u->y * n->x);
  }
}

// AUTOINJECT
void Mat_GetUp(_VECTOR *vOut, _MATRIX *mIn) {
  vOut->x = mIn->m[4];
  vOut->y = mIn->m[5];
  vOut->z = mIn->m[6];
}

// AUTOINJECT
void Mat_GetNorm(_VECTOR *vOut, _MATRIX *mIn) {
  vOut->x = mIn->m[0];
  vOut->y = mIn->m[1];
  vOut->z = mIn->m[2];
}

// AUTOINJECT
void Mat_SetDir(_VECTOR *vIn, _MATRIX *mOut) {
  mOut->m[8] = vIn->x;
  mOut->m[9] = vIn->y;
  mOut->m[10] = vIn->z;
}

// AUTOINJECT
void Mat_SetUp(_VECTOR *vIn, _MATRIX *mOut) {
  mOut->m[4] = vIn->x;
  mOut->m[5] = vIn->y;
  mOut->m[6] = vIn->z;
}

// The inverse of a rotation in place: the rotation transposed and the translation negated, not turned (so only a
// rotation about the origin comes back right). The elements and negations that go through the x87 lose a signalling
// NaN's signal.
// AUTOINJECT
void Mat_InverseSelf(_MATRIX *mtx) {
  float m4 = mtx->m[4];
  float m8 = mtx->m[8];
  mtx->m[4] = X87Copy(mtx->m[1]);
  mtx->m[1] = m4;
  float m6 = mtx->m[6];
  mtx->m[8] = X87Copy(mtx->m[2]);
  mtx->m[2] = m8;
  mtx->m[6] = X87Copy(mtx->m[9]);
  mtx->m[9] = m6;
  mtx->m[12] = X87Negate(mtx->m[12]);
  mtx->m[13] = X87Negate(mtx->m[13]);
  mtx->m[14] = X87Negate(mtx->m[14]);
}

// As Mat_InverseSelf, into out
// AUTOINJECT
void Mat_Inverse(_MATRIX *in, _MATRIX *out) {
  out->m[4] = in->m[1];
  out->m[0] = in->m[0];
  out->m[1] = in->m[4];
  out->m[8] = in->m[2];
  out->m[5] = in->m[5];
  out->m[2] = in->m[8];
  out->m[6] = in->m[9];
  out->m[10] = in->m[10];
  out->m[9] = in->m[6];
  out->m[12] = X87Negate(in->m[12]);
  out->m[13] = X87Negate(in->m[13]);
  out->m[14] = X87Negate(in->m[14]);
}

// The rotation scaled, the translation not
// AUTOINJECT
void MatrixScale(_MATRIX *mtx, float scale) {
  mtx->m[0] = scale * mtx->m[0];
  mtx->m[4] = scale * mtx->m[4];
  mtx->m[8] = scale * mtx->m[8];
  mtx->m[1] = scale * mtx->m[1];
  mtx->m[5] = scale * mtx->m[5];
  mtx->m[9] = scale * mtx->m[9];
  mtx->m[2] = scale * mtx->m[2];
  mtx->m[6] = scale * mtx->m[6];
  mtx->m[10] = scale * mtx->m[10];
}

// The rotations: out = b's rows through a (out's translation is not written). Summed on the x87.
// AUTOINJECT
void MulMatrix0(_MATRIX *a, _MATRIX *b, _MATRIX *out) {
  out->m[0] = (float)(((double)a->m[0] * b->m[0] + (double)a->m[4] * b->m[1]) + (double)b->m[2] * a->m[8]);
  out->m[1] = (float)(((double)a->m[5] * b->m[1] + (double)a->m[1] * b->m[0]) + (double)a->m[9] * b->m[2]);
  out->m[2] = (float)(((double)a->m[6] * b->m[1] + (double)a->m[2] * b->m[0]) + (double)a->m[10] * b->m[2]);
  out->m[4] = (float)(((double)a->m[4] * b->m[5] + (double)b->m[4] * a->m[0]) + (double)b->m[6] * a->m[8]);
  out->m[5] = (float)(((double)a->m[9] * b->m[6] + (double)a->m[5] * b->m[5]) + (double)a->m[1] * b->m[4]);
  out->m[6] = (float)(((double)a->m[10] * b->m[6] + (double)a->m[6] * b->m[5]) + (double)a->m[2] * b->m[4]);
  out->m[8] = (float)(((double)a->m[0] * b->m[8] + (double)a->m[4] * b->m[9]) + (double)a->m[8] * b->m[10]);
  out->m[9] = (float)(((double)a->m[9] * b->m[10] + (double)a->m[5] * b->m[9]) + (double)a->m[1] * b->m[8]);
  out->m[10] = (float)(((double)a->m[10] * b->m[10] + (double)a->m[6] * b->m[9]) + (double)a->m[2] * b->m[8]);
}

// As MulMatrix0, into b: b's rotation is read before any of it is written, a as each row goes
// AUTOINJECT
void MulMatrix2(_MATRIX *a, _MATRIX *b) {
  float b0 = b->m[0], b1 = b->m[1], b2 = b->m[2];
  float b4 = b->m[4], b5 = b->m[5], b6 = b->m[6];
  float b8 = b->m[8], b9 = b->m[9], b10 = b->m[10];
  b->m[0] = (float)(((double)b2 * a->m[8] + (double)b1 * a->m[4]) + (double)b0 * a->m[0]);
  b->m[1] = (float)(((double)b2 * a->m[9] + (double)b1 * a->m[5]) + (double)b0 * a->m[1]);
  b->m[2] = (float)(((double)b0 * a->m[2] + (double)b2 * a->m[10]) + (double)b1 * a->m[6]);
  b->m[4] = (float)(((double)b6 * a->m[8] + (double)b5 * a->m[4]) + (double)b4 * a->m[0]);
  b->m[5] = (float)(((double)b6 * a->m[9] + (double)b5 * a->m[5]) + (double)b4 * a->m[1]);
  b->m[6] = (float)(((double)b4 * a->m[2] + (double)b6 * a->m[10]) + (double)b5 * a->m[6]);
  b->m[8] = (float)(((double)b10 * a->m[8] + (double)b9 * a->m[4]) + (double)b8 * a->m[0]);
  b->m[9] = (float)(((double)b10 * a->m[9] + (double)b9 * a->m[5]) + (double)b8 * a->m[1]);
  b->m[10] = (float)(((double)b8 * a->m[2] + (double)b10 * a->m[10]) + (double)b9 * a->m[6]);
}

// The rotations about one axis write only the rotation, with FSIN and FCOS
// AUTOINJECT
void RotMatrixX(float angle, _MATRIX *mtx) {
  Extended c = FCos(angle);
  mtx->m[0] = 1.0f;
  mtx->m[4] = 0.0f;
  mtx->m[8] = 0.0f;
  mtx->m[1] = 0.0f;
  mtx->m[2] = 0.0f;
  Extended s = FSin(angle);
  mtx->m[5] = (float)c;
  mtx->m[9] = (float)-s;
  mtx->m[6] = (float)s;
  mtx->m[10] = (float)c;
}

// The source's terms multiplied by zero are kept: an infinite or NaN angle shows through them.
// AUTOINJECT
void RotMatrixY(float angle, _MATRIX *mtx) {
  Extended c = FCos(angle);
  Extended s = FSin(angle);
  Extended zero = 0.0f;
  Extended zeroSin = zero * s;
  mtx->m[0] = (float)c;
  mtx->m[4] = (float)zeroSin;
  mtx->m[8] = (float)s;
  float zeroCos = (float)(c * zero);
  mtx->m[1] = zeroCos;
  mtx->m[6] = zeroCos;
  mtx->m[5] = (float)(zero * zeroSin + Extended(1.0f));
  mtx->m[9] = (float)zeroSin;
  mtx->m[2] = (float)-s;
  mtx->m[10] = (float)c;
}

// AUTOINJECT
void RotMatrixZ(float angle, _MATRIX *mtx) {
  Extended c = FCos(angle);
  mtx->m[8] = 0.0f;
  mtx->m[9] = 0.0f;
  mtx->m[2] = 0.0f;
  mtx->m[6] = 0.0f;
  mtx->m[10] = 1.0f;
  Extended s = FSin(angle);
  mtx->m[0] = (float)c;
  mtx->m[4] = (float)-s;
  mtx->m[1] = (float)s;
  mtx->m[5] = (float)c;
}

// Pitch (about x) after heading (about y). The pitch's cosine is stored as a float; the terms multiplied by zero are
// kept, as in RotMatrixY.
// AUTOINJECT
void RotMatrixXY(float pitch, float heading, _MATRIX *mtx) {
  Extended cosY = FCos(heading);
  Extended sinY = FSin(heading);
  Extended cosX = (float)FCos(pitch);
  Extended sinX = FSin(pitch);
  Extended zero = 0.0f;
  Extended zeroSinY = sinY * zero;
  mtx->m[0] = (float)cosY;
  mtx->m[4] = (float)(sinX * sinY - cosX * zero);
  mtx->m[8] = (float)(cosX * sinY + sinX * zero);
  mtx->m[1] = (float)(cosY * zero);
  mtx->m[5] = (float)(zeroSinY * sinX + cosX);
  mtx->m[9] = (float)(zeroSinY * cosX - sinX);
  mtx->m[2] = (float)-sinY;
  mtx->m[6] = (float)(sinX * cosY);
  mtx->m[10] = (float)(cosX * cosY);
}

// (v + the translation) through the rotation, on the x87: v is read before out is written
// AUTOINJECT
void RotPreTransVec(_MATRIX *mtx, _VECTOR *v, _VECTOR *vOut) {
  double x = (double)mtx->m[12] + v->x;
  double y = (double)mtx->m[13] + v->y;
  double z = (double)mtx->m[14] + v->z;
  vOut->x = (float)((z * mtx->m[8] + y * mtx->m[4]) + x * mtx->m[0]);
  vOut->y = (float)((z * mtx->m[9] + y * mtx->m[5]) + x * mtx->m[1]);
  vOut->z = (float)((z * mtx->m[10] + y * mtx->m[6]) + x * mtx->m[2]);
}

// v turned by q (x, y, z, w): t = 2 (q x v), then v + w t + q x t. On the x87: t's z and the second cross product's
// x and y stay unrounded, the rest are stored as floats.
// AUTOINJECT
void Quat_Rot_Vec(quaternion_tag *quat, _VECTOR *vIn, _VECTOR *vOut) {
  const float *q = quat->q;
  double x = (double)vIn->x + vIn->x;
  double y = (double)vIn->y + vIn->y;
  double z = (double)vIn->z + vIn->z;
  float tx = (float)(z * q[1] - y * q[2]);
  float ty = (float)(x * q[2] - z * q[0]);
  double tz = y * q[0] - x * q[1];
  double cy = (double)tx * q[2] - tz * q[0];
  float cz = (float)((double)ty * q[0] - (double)tx * q[1]);
  double cx = tz * q[1] - (double)ty * q[2];
  vOut->x = (float)((cx + (double)tx * q[3]) + vIn->x);
  vOut->y = (float)(((double)ty * q[3] + vIn->y) + cy);
  vOut->z = (float)((tz * q[3] + vIn->z) + cz);
}

// From Euler angles (x, y, z), with FSIN and FCOS of the halves. Z's half, X's sine and cosine, Y's cosine and three
// of the products are stored as floats; Y's sine, Z's sine and cosine and the fourth product stay unrounded.
// AUTOINJECT
void Quat_EulerToQuat(_VECTOR *euler, quaternion_tag *quat) {
  double halfX = euler->x * 0.5;
  double halfY = euler->y * 0.5;
  float halfZ = (float)(euler->z * 0.5);
  Extended cosX = (float)FCos(halfX);
  Extended sinX = (float)FSin(halfX);
  Extended cosY = (float)FCos(halfY);
  Extended sinY = FSin(halfY);
  Extended cosZ = FCos(halfZ);
  Extended sinZ = FSin(halfZ);
  Extended cosZcosX = (float)(cosZ * cosX);
  Extended cosXsinZ = (float)(cosX * sinZ);
  Extended cosZsinX = (float)(cosZ * sinX);
  Extended sinZsinX = sinZ * sinX;
  quat->q[0] = (float)(cosZsinX * cosY - cosXsinZ * sinY);
  quat->q[1] = (float)(cosY * sinZsinX + cosZcosX * sinY);
  quat->q[2] = (float)(cosXsinZ * cosY - cosZsinX * sinY);
  quat->q[3] = (float)(sinZsinX * sinY + cosZcosX * cosY);
}

// From an axis (x, y, z) and an angle (w)
// AUTOINJECT
void Quat_AxisAngleToQuat(float *axisAngle, quaternion_tag *quat) {
  double half = axisAngle[3] * 0.5;
  quat->q[3] = (float)FCos(half);
  Extended s = FSin(half);
  quat->q[0] = (float)(s * Extended(axisAngle[0]));
  quat->q[1] = (float)(s * Extended(axisAngle[1]));
  quat->q[2] = (float)(s * Extended(axisAngle[2]));
}

// To unit length; all four zero (of either sign) becomes (1, 0, 0, 0). The length is summed from the last component.
// AUTOINJECT
void Quat_Normalise(quaternion_tag *quat) {
  float *q = quat->q;
  if (q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && q[3] == 0.0f) {
    q[0] = 1.0f;
    return;
  }
  double scale = 1.0 / sqrt((((double)q[3] * q[3] + (double)q[2] * q[2]) + (double)q[1] * q[1]) + (double)q[0] * q[0]);
  q[0] = (float)(scale * q[0]);
  q[1] = (float)(scale * q[1]);
  q[2] = (float)(scale * q[2]);
  q[3] = (float)(scale * q[3]);
}

// The rotation of a quaternion (w, x, y, z); the translation is not written
// AUTOINJECT
void Quat_QuaternionToMatrix(_MATRIX *mtx, quaternion_tag *quat) {
  const float *q = quat->q;
  mtx->m[0] = (float)(((double)q[0] * q[0] + (double)q[1] * q[1]) * 2.0 - 1.0f);
  mtx->m[1] = (float)(((double)q[0] * q[3] + (double)q[2] * q[1]) * 2.0);
  mtx->m[2] = (float)(((double)q[3] * q[1] - (double)q[0] * q[2]) * 2.0);
  mtx->m[4] = (float)(((double)q[2] * q[1] - (double)q[0] * q[3]) * 2.0);
  mtx->m[5] = (float)(((double)q[2] * q[2] + (double)q[0] * q[0]) * 2.0 - 1.0f);
  mtx->m[6] = (float)(((double)q[0] * q[1] + (double)q[3] * q[2]) * 2.0);
  mtx->m[8] = (float)(((double)q[0] * q[2] + (double)q[3] * q[1]) * 2.0);
  mtx->m[9] = (float)(((double)q[3] * q[2] - (double)q[0] * q[1]) * 2.0);
  mtx->m[10] = (float)(((double)q[3] * q[3] + (double)q[0] * q[0]) * 2.0 - 1.0f);
}

// A view from 'from' towards 'to': the direction, a side at right angles to it and up (or to altUp when the
// direction is within a degree or so of up), and up again from the two. The translation is 'from'.
// AUTOINJECT
void Mat_LookAt(_MATRIX *mtx, _VECTOR *from, _VECTOR *to, _VECTOR *up, _VECTOR *altUp) {
  _VECTOR dir;
  dir.x = to->x - from->x;
  dir.y = to->y - from->y;
  dir.z = to->z - from->z;
  Vec_Normalise(&dir, &dir);

  double dot = ((double)dir.z * up->z + (double)dir.y * up->y) + (double)dir.x * up->x;
  _VECTOR *axis = AbsFchs(dot) > 0.999f ? altUp : up;
  _VECTOR side;
  side.x = (float)((double)dir.z * axis->y - (double)dir.y * axis->z);
  side.y = (float)((double)dir.x * axis->z - (double)dir.z * axis->x);
  side.z = (float)((double)dir.y * axis->x - (double)dir.x * axis->y);
  Vec_Normalise(&side, &side);

  _VECTOR newUp;
  newUp.x = (float)((double)side.z * dir.y - (double)side.y * dir.z);
  newUp.y = (float)((double)side.x * dir.z - (double)side.z * dir.x);
  newUp.z = (float)((double)side.y * dir.x - (double)side.x * dir.y);
  Vec_Normalise(&newUp, &newUp);

  mtx->m[8] = dir.x;
  mtx->m[9] = dir.y;
  mtx->m[10] = dir.z;
  mtx->m[4] = newUp.x;
  mtx->m[5] = newUp.y;
  mtx->m[6] = newUp.z;
  mtx->m[0] = side.x;
  mtx->m[1] = side.y;
  mtx->m[2] = side.z;
  mtx->m[12] = from->x;
  mtx->m[13] = from->y;
  mtx->m[14] = from->z;
}

// From a rotation matrix: by the trace when it is not negative, else by its largest diagonal element. With the trace
// the quaternion's w comes out negative; on the first column's branch the sum of the other two diagonal elements is
// the one stored as a float.
// AUTOINJECT
void Quat_MatrixToQuaternion(quaternion_tag *quat, _MATRIX *mtx) {
  const float *m = mtx->m;
  float *q = quat->q;
  double sum = (double)m[5] + m[10];
  float sumStored = (float)sum;
  double trace = sum + m[0];
  if (trace >= 0.0) {
    double s = sqrt(trace + 1.0f);
    q[0] = (float)(-0.5f * s);
    double scale = 0.5f / s;
    q[1] = (float)(((double)m[9] - m[6]) * scale);
    q[2] = (float)(((double)m[2] - m[8]) * scale);
    q[3] = (float)(((double)m[4] - m[1]) * scale);
    return;
  }

  int i = m[5] > m[0] ? 1 : 0;
  if (m[10] > m[i * 5])
    i = 2;
  switch (i) {
  case 0: {
    double s = sqrt(((double)m[0] - sumStored) + 1.0f);
    q[1] = (float)(0.5f * s);
    double scale = 0.5f / s;
    q[2] = (float)(((double)m[4] + m[1]) * scale);
    q[3] = (float)(((double)m[8] + m[2]) * scale);
    q[0] = (float)-(((double)m[9] - m[6]) * scale);
    break;
  }
  case 1: {
    double s = sqrt(((double)m[5] - ((double)m[0] + m[10])) + 1.0f);
    q[2] = (float)(0.5f * s);
    double scale = 0.5f / s;
    q[3] = (float)(((double)m[9] + m[6]) * scale);
    q[1] = (float)(((double)m[4] + m[1]) * scale);
    q[0] = (float)-(((double)m[2] - m[8]) * scale);
    break;
  }
  case 2: {
    double s = sqrt(((double)m[10] - ((double)m[5] + m[0])) + 1.0f);
    q[3] = (float)(0.5f * s);
    double scale = 0.5f / s;
    q[1] = (float)(((double)m[8] + m[2]) * scale);
    q[2] = (float)(((double)m[9] + m[6]) * scale);
    q[0] = (float)-(((double)m[4] - m[1]) * scale);
    break;
  }
  }
}

// The pitch (x) and heading (y) of a direction by Atan2, and its length (z). The square of its length in plan
// goes into the pitch unrounded and into the length as a float.
// AUTOINJECT
void Vec_Cartesian_2_Spherical(_VECTOR *out, float x, float y, float z) {
  double planeSq = (double)x * x + (double)z * z;
  float planeSqStored = (float)planeSq;
  out->x = Atan2(y, (float)sqrt(planeSq));
  out->y = Atan2(x, z);
  out->z = (float)sqrt((double)y * y + planeSqStored);
}

// mtx = mtx's rows through by's rotation, by's translation added to the turned translation. Summed on the x87.
// AUTOINJECT
void Concat(_MATRIX *by, _MATRIX *mtx) {
  const float *a = by->m;
  float *b = mtx->m;
  float b12 = b[12], b13 = b[13], b14 = b[14];
  float b0 = b[0], b1 = b[1], b2 = b[2];
  float b4 = b[4], b5 = b[5], b6 = b[6];
  float b8 = b[8], b9 = b[9], b10 = b[10];
  b[12] = (float)((((double)b14 * a[8] + (double)b13 * a[4]) + (double)b12 * a[0]) + a[12]);
  b[13] = (float)((((double)b12 * a[1] + (double)b14 * a[9]) + (double)b13 * a[5]) + a[13]);
  b[14] = (float)((((double)b12 * a[2] + (double)b14 * a[10]) + (double)b13 * a[6]) + a[14]);
  b[0] = (float)(((double)b2 * a[8] + (double)b1 * a[4]) + (double)b0 * a[0]);
  b[1] = (float)(((double)b0 * a[1] + (double)b2 * a[9]) + (double)b1 * a[5]);
  b[2] = (float)(((double)b0 * a[2] + (double)b2 * a[10]) + (double)b1 * a[6]);
  b[4] = (float)(((double)b6 * a[8] + (double)b5 * a[4]) + (double)b4 * a[0]);
  b[5] = (float)(((double)b4 * a[1] + (double)b6 * a[9]) + (double)b5 * a[5]);
  b[6] = (float)(((double)b4 * a[2] + (double)b6 * a[10]) + (double)b5 * a[6]);
  b[8] = (float)(((double)b10 * a[8] + (double)b9 * a[4]) + (double)b8 * a[0]);
  b[9] = (float)(((double)b8 * a[1] + (double)b10 * a[9]) + (double)b9 * a[5]);
  b[10] = (float)(((double)b8 * a[2] + (double)b10 * a[10]) + (double)b9 * a[6]);
}

// A vector through a matrix's rotation, in place: summed on the x87, z first, from the vector as it was
// AUTOINJECT
void ApplyMatrixLVI(_MATRIX *mtx, _VECTOR *v) {
  double x = v->x, y = v->y, z = v->z;
  v->x = (float)((z * mtx->m[8] + y * mtx->m[4]) + x * mtx->m[0]);
  v->y = (float)((z * mtx->m[9] + y * mtx->m[5]) + x * mtx->m[1]);
  v->z = (float)((z * mtx->m[10] + y * mtx->m[6]) + x * mtx->m[2]);
}
