typedef struct {
	float q[4];
} quaternion_tag;

typedef struct {
	float m[15];
} _MATRIX;

typedef struct {
	float v[3];
} _VECTOR;

void Quat_Copy(quaternion_tag *target,const quaternion_tag *from);
bool Quat_IsEqual(const quaternion_tag *a, const quaternion_tag *b, float threshold);
void Quat_Mul(const quaternion_tag *a, const quaternion_tag *b, quaternion_tag *target);
void Mat_Copy(const _MATRIX *source, _MATRIX *target);
void Mat_CopyRot(const _MATRIX *source, _MATRIX *target);
void Mat_IdentityT(_MATRIX *mtx);
void Mat_Identity(_MATRIX *mtx);

