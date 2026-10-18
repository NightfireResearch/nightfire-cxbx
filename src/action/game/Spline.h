#ifndef SPLINE_H_
#define SPLINE_H_

#include "../actionhelpers.h"
#include "../math/math.h"

#pragma pack(push, 1)
// A key of a path (Spline_Interp, KeyFrame_Interp): where it is and how it is turned
typedef struct framelist_tag {
    _VECTOR pos;
    quaternion_tag quat;        // 0x0c
} framelist_tag;
#pragma pack(pop)
static_assert(sizeof(framelist_tag) == 0x1c, "framelist_tag is 0x1c bytes");

void Spline_Eval3D(_VECTOR *p0, _VECTOR *p1, _VECTOR *p2, _VECTOR *p3, float t, _VECTOR *out);
void Spline_Interp(void *path, ushort first, ushort last, float t, _MATRIX *mtx, quaternion_tag *quat, int mode);
void KeyFrame_Interp(void *path, ushort first, ushort last, float t, _MATRIX *mtx);

#endif // SPLINE_H_
