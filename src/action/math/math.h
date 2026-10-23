#ifndef MATH_H
#define MATH_H

#pragma pack(push, 1)
typedef struct {
	float q[4]; // wxyz order
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

// Pi and the angles the game's code uses, as floats
#define PI                  3.1415927f
#define HALF_PI             1.5707964f
#define QUARTER_PI          0.7853982f
#define THREE_QUARTER_PI    2.3561945f
#define THREE_HALF_PI       4.712389f
#define TWO_PI              6.2831855f
#define DEG_TO_RAD          0.017453292f
#define RAD_TO_DEG          57.295776f
// An angle in degrees as radians: the float nearest it, as the original's constants are
#define DEG2RAD(degrees)    ((float)((degrees) * 3.14159265358979323846 / 180.0))

#pragma pack(pop)

#define Mat_Position(mat) ((_VECTOR *)(((mat).m + 0xc)))

void MultiplyQuaternionAltOrder(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *target);
void Quat_Copy(quaternion_tag *target,const quaternion_tag *from);
bool Quat_IsEqual(const quaternion_tag *a, const quaternion_tag *b, float threshold);
void Quat_QuatToMat(quaternion_tag *quatIn,_MATRIX *mOut);
void Quat_QuatTransToMat(quaternion_tag *quatIn,float *vecIn,_MATRIX *mOut);
void Quat_MatToQuat(quaternion_tag *quatOut, _MATRIX *mtxIn);
void Quat_QuaternionMultiply(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *qOut);
void Quat_Slerp(float progress, quaternion_tag *qStart, quaternion_tag *qEnd, quaternion_tag *qOut);
void Quat_Slerp_Acc(float blendFactor, float *quatStart, float *quatEnd, float *quatOut);
void Vec_Track(float blendFactor, float *targetPos, float *currentPos, _MATRIX *matrix, quaternion_tag *quatOut);
void Mat_GetDir(_VECTOR *dirOut, _MATRIX *mtxIn);
void Mat_Copy(const _MATRIX *source, _MATRIX *target);
void Mat_CopyRot(const _MATRIX *source, _MATRIX *target);
void Mat_Align2Up(float *matrix, float *up, float *direction);
void Mat_IdentityT(_MATRIX *mtx);
void Mat_Identity(_MATRIX *mtx);
void Mat_World2ViewMat(_MATRIX *viewMtx, _MATRIX *worldMtx);
void Mat_Scale3f(_MATRIX *m_out,_MATRIX *m_in,float scale_x,float scale_y,float scale_z);
void Matrix_SetTrans(_VECTOR *vec, _MATRIX *mtx);
void RotTransMat(_MATRIX *mat1,_MATRIX *mat2);
void Vec_Normalise(_VECTOR *vOut,_VECTOR *vIn);
double Vec_NormaliseLen(_VECTOR *output, _VECTOR *input);
void Vec_Subtract(const _VECTOR *a, const _VECTOR *b, _VECTOR *v_out);
void Vec_Zero(_VECTOR *v);
void Vec_Negate(const _VECTOR *vIn,_VECTOR *vOut);
void Vec_Copy(_VECTOR *src, _VECTOR *dst);
void Vec_Cross(_VECTOR *a,_VECTOR *b,_VECTOR *vecOut);
void Vec_Copy2(_VECTOR* src, _VECTOR *dst1, _VECTOR *dst2);
void Vec_CrossNormalise(_VECTOR *a,_VECTOR *b,_VECTOR *vecOut);
void Vec_Max(_VECTOR *a, _VECTOR *b, _VECTOR *vecOut);
void Vec_Min(_VECTOR *a, _VECTOR *b, _VECTOR *vecOut);
void Vec_MinMax(_VECTOR *a, _VECTOR *b, _VECTOR *maximums, _VECTOR *minimums);
void Vec_MinMax3(_VECTOR *a, _VECTOR *b, _VECTOR *c, _VECTOR *maximums, _VECTOR *minimums);
void Vec_MulR32(_VECTOR *outVec, _VECTOR *inVec, float scale);
bool Vec_IsEqual(_VECTOR *a,_VECTOR *b,float epsilon);
double Vec_Dist3D(_VECTOR *a, _VECTOR *b);
double Vec_SqDist3D(_VECTOR *a, _VECTOR *b);
void Vec_Swap(_VECTOR *a, _VECTOR *b);
void RotMatrixZYX(_VECTOR *angles,_MATRIX *mtx);
void RotTransMatrix(_VECTOR *rot, _VECTOR *trans, _MATRIX *mtx);
bool Plane_PlaneEq(plane_equ_tag *planeEq, _VECTOR *v1, _VECTOR *v2, _VECTOR *v3);
double DistancePointToPlane(_VECTOR *point, plane_equ_tag *plane);
void Plane_PutPntOnPlane(_VECTOR *out, _VECTOR *point, plane_equ_tag *plane);
void auxVec_AddMulR32(_VECTOR *add, _VECTOR *vIn, float multiply, _VECTOR *vOut);
bool vecutil_point_on_poly(_VECTOR *point, _VECTOR *vtx1, _VECTOR *vtx2, _VECTOR *vtx3, plane_equ_tag *plane);
double vecutil_calculate_closest_point_on_line(_VECTOR *out, _VECTOR *a, _VECTOR *b, _VECTOR *p);
double vecutil_Dist2Tri(_VECTOR *p, _VECTOR *a, _VECTOR *b, _VECTOR *c, plane_equ_tag *plane, unsigned short *code, _VECTOR *closest);
double vecutil_Dist2Tri_PointGeom(_VECTOR *p, _VECTOR *a, _VECTOR *b, _VECTOR *c, plane_equ_tag *plane, float planeDist, _VECTOR *closest);
int vecutil_intersect_line_line_2d(_VECTOR *p1, _VECTOR *p2, _VECTOR *p3, _VECTOR *p4, _VECTOR *out);
void RotMatrix(_VECTOR *vIn,_MATRIX *mtxOut);
void RotMatrixZYX(_VECTOR *angles,_MATRIX *mtx);
void RotTransMatrix(_VECTOR *rot, _VECTOR *trans, _MATRIX *mtx);
void MatrixMultiplyVector(_MATRIX *mtx,_VECTOR *vecIn,_VECTOR *vecOut);
void ApplyMatrixLV(_MATRIX *mtx,_VECTOR *vIn,_VECTOR *vOut);
double Vec_ScalarTripleProduct(_VECTOR *a,_VECTOR *b,_VECTOR *c);
void vecutil_cartesian_to_spherical_acc(_VECTOR *vec, float v_x, float v_y, float v_z);

#define M_PI 3.14159265358979323846
#define M_PI_2 1.5707963267948966  // π/2
#define M_PI_4 0.7853981633974483  // π/4
#define M_2PI 6.283185307179586    // 2π
#define DEG2RAD(x) ((x) * ((float)M_PI / 180.0f))

// Helper functions which either didn't exist or were inlined on original code
double Vec_Dot(_VECTOR *a, _VECTOR *b);
double Vec_Magnitude(_VECTOR *a);

double Vec_AngleDifference(float angle1, float angle2);
void Vec_Spherical_2_Cartesian(float *out, float radius, float yaw, float pitch);
double Atan2(float y, float x);
void Mat_Align2Dir(_MATRIX *mtxOut, _VECTOR *direction, _VECTOR *upHint, _VECTOR *altHint);
void Vec_Add2(_VECTOR *a, _VECTOR *b, _VECTOR *out);

void Mat_Normalize(_MATRIX *mtx);

float Ang_RotateToBearing(float current, float target, float rate, float maxTurn);
void Vec_Avg3(_VECTOR *a, _VECTOR *b, _VECTOR *c, _VECTOR *out);
double Vec_SqDist2D(_VECTOR *a, _VECTOR *b);
double Vec_Dist2D(_VECTOR *a, _VECTOR *b);
double Vec_SqMagnitude(_VECTOR *v);
void Vec_UnitVector(_VECTOR *out, _VECTOR *from, _VECTOR *to);
void Vec_NormToRot(_VECTOR *normal, _VECTOR *rot);
void Vec_DirToRot(_VECTOR *dir, _VECTOR *rot);
void Vec_Mul(_VECTOR *v, _VECTOR *scale);
void Vec_MakeOrthonormalBasis(_VECTOR *u, _VECTOR *v, _VECTOR *n);
void Mat_GetUp(_VECTOR *vOut, _MATRIX *mIn);
void Mat_GetNorm(_VECTOR *vOut, _MATRIX *mIn);
void Mat_SetDir(_VECTOR *vIn, _MATRIX *mOut);
void Mat_SetUp(_VECTOR *vIn, _MATRIX *mOut);
void Mat_InverseSelf(_MATRIX *mtx);
void Mat_Inverse(_MATRIX *in, _MATRIX *out);
void MatrixScale(_MATRIX *mtx, float scale);
void MulMatrix0(_MATRIX *a, _MATRIX *b, _MATRIX *out);
void MulMatrix2(_MATRIX *a, _MATRIX *b);
void RotMatrixX(float angle, _MATRIX *mtx);
void RotMatrixY(float angle, _MATRIX *mtx);
void RotMatrixZ(float angle, _MATRIX *mtx);
void RotMatrixXY(float pitch, float heading, _MATRIX *mtx);
void RotPreTransVec(_MATRIX *mtx, _VECTOR *v, _VECTOR *vOut);
void Quat_Rot_Vec(quaternion_tag *quat, _VECTOR *vIn, _VECTOR *vOut);
void Quat_EulerToQuat(_VECTOR *euler, quaternion_tag *quat);
void Quat_AxisAngleToQuat(float *axisAngle, quaternion_tag *quat);
void Quat_Normalise(quaternion_tag *quat);
void Quat_QuaternionToMatrix(_MATRIX *mtx, quaternion_tag *quat);
void Mat_LookAt(_MATRIX *mtx, _VECTOR *from, _VECTOR *to, _VECTOR *up, _VECTOR *altUp);
void Quat_MatrixToQuaternion(quaternion_tag *quat, _MATRIX *mtx);
void Vec_Cartesian_2_Spherical(_VECTOR *out, float x, float y, float z);
void Concat(_MATRIX *by, _MATRIX *mtx);
void ApplyMatrixLVI(_MATRIX *mtx, _VECTOR *v);

// Not reimplemented yet: AUTOGEN stubs that call the original (math.cpp)
void Mat_GetPosition(_VECTOR *posOut, _MATRIX *mtxIn);
void Vec_Scalef(_VECTOR *vIn, float scale, _VECTOR *vOut);
void Vec_Mul_F3(_VECTOR *vecOut, _VECTOR *vecIn, float scaleX, float scaleY, float scaleZ);
void Vec4_MulAdd(float scale, float *vecIn, float *vecOut);
int Vec_LineSphereIntersect(_VECTOR *a, _VECTOR *b, _VECTOR *centre, float radius, _VECTOR *out);
int Vec_SegmentSphereIntersect(_VECTOR *p, _VECTOR *q, _VECTOR *centre, float radius, _VECTOR *out);

#endif // MATH_H
