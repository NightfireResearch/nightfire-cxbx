#ifndef MATH_H
#define MATH_H

#pragma pack(push, 1)
typedef struct {
	float q[4];
} quaternion_tag;

typedef struct {
	float m[15];
} _MATRIX;

typedef struct {
	float x;
	float y;
	float z;
} _VECTOR;

#pragma pack(pop)

#define Mat_Position(mat) ((_VECTOR *)((mat.m + 0xc)))

void Quat_Copy(quaternion_tag *target,const quaternion_tag *from);
bool Quat_IsEqual(const quaternion_tag *a, const quaternion_tag *b, float threshold);
void Quat_Mul(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *target);
void Quat_QuatTransToMat(quaternion_tag *quatIn,float *vecIn,_MATRIX *mOut);
void Mat_Copy(const _MATRIX *source, _MATRIX *target);
void Mat_CopyRot(const _MATRIX *source, _MATRIX *target);
void Mat_IdentityT(_MATRIX *mtx);
void Mat_Identity(_MATRIX *mtx);
void RotTransMat(_MATRIX *param_1,_MATRIX *param_2);
void Vec_Normalise(_VECTOR *vOut,_VECTOR *vIn);
void Vec_Subtract(const _VECTOR *a, const _VECTOR *b, _VECTOR *v_out);
void Vec_Zero(_VECTOR *v);
void Vec_Negate(const _VECTOR *vIn,_VECTOR *vOut);
void Vec_Copy(_VECTOR *src, _VECTOR *dst);
void Vec_MulR32(_VECTOR *outVec, _VECTOR *inVec, float scale);
void RotMatrixZYX(_VECTOR *param_1,_MATRIX *mtx);
void RotTransMatrix(_VECTOR *rot, _VECTOR *trans, _MATRIX *mtx);

#define M_PI 3.14159265358979323846
#define DEG2RAD(x) ((x) * (M_PI / 180.0f))

#endif // MATH_H