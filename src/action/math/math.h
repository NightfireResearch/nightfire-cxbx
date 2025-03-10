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

typedef struct {
	union {
		struct {
			float a;
			float b;
			float c;
		};
		_VECTOR normal;
	};
	float d;
} plane_equ_tag;

#pragma pack(pop)

#define Mat_Position(mat) ((_VECTOR *)((mat.m + 0xc)))

void Quat_Copy(quaternion_tag *target,const quaternion_tag *from);
bool Quat_IsEqual(const quaternion_tag *a, const quaternion_tag *b, float threshold);
void Quat_Mul(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *target);
void Quat_QuatToMat(quaternion_tag *param_1,_MATRIX *param_2);
void Quat_QuatTransToMat(quaternion_tag *quatIn,float *vecIn,_MATRIX *mOut);
void Mat_Copy(const _MATRIX *source, _MATRIX *target);
void Mat_CopyRot(const _MATRIX *source, _MATRIX *target);
void Mat_IdentityT(_MATRIX *mtx);
void Mat_Identity(_MATRIX *mtx);
void Matrix_SetTrans(_VECTOR *vec, _MATRIX *mtx);
void RotTransMat(_MATRIX *param_1,_MATRIX *param_2);
void Vec_Normalise(_VECTOR *vOut,_VECTOR *vIn);
float Vec_NormaliseLen(_VECTOR *output, _VECTOR *input);
void Vec_Subtract(const _VECTOR *a, const _VECTOR *b, _VECTOR *v_out);
void Vec_Zero(_VECTOR *v);
void Vec_Negate(const _VECTOR *vIn,_VECTOR *vOut);
void Vec_Copy(_VECTOR *src, _VECTOR *dst);
void Vec_Cross(_VECTOR *a,_VECTOR *b,_VECTOR *vecOut);
void Vec_Copy2(_VECTOR* src, _VECTOR *dst1, _VECTOR *dst2);
void Vec_CrossNormalise(_VECTOR *a,_VECTOR *b,_VECTOR *vecOut);
void Vec_MulR32(_VECTOR *outVec, _VECTOR *inVec, float scale);
bool Vec_IsEqual(_VECTOR *param_1,_VECTOR *param_2,float epsilon);
float Vec_Dist3D(_VECTOR a, _VECTOR b);
void Vec_Swap(_VECTOR a, _VECTOR b);
void RotMatrixZYX(_VECTOR *param_1,_MATRIX *mtx);
void RotTransMatrix(_VECTOR *rot, _VECTOR *trans, _MATRIX *mtx);
bool Plane_PlaneEq(plane_equ_tag *planeEq, _VECTOR *v1, _VECTOR *v2, _VECTOR *v3);
float DistancePointToPlane(_VECTOR *point, plane_equ_tag *plane);
void auxVec_AddMulR32(_VECTOR *add, _VECTOR *vIn, float multiply, _VECTOR *vOut);
bool vecutil_point_on_poly(_VECTOR *point, _VECTOR *vtx1, _VECTOR *vtx2, _VECTOR *vtx3, plane_equ_tag *plane);

#define M_PI 3.14159265358979323846
#define DEG2RAD(x) ((x) * (M_PI / 180.0f))

// Helper functions which either didn't exist or were inlined on original code
float Vec_Dot(_VECTOR *a, _VECTOR *b);
float Vec_Magnitude(_VECTOR *a);

#endif // MATH_H