#include "../engine/Camera.h"
#include "Spline.h"

#include "../math/math.h"
#include "../math/X87Ops.h"

#include <math.h>
#include <stdint.h>
#include <bit>

#pragma fp_contract(off)

// Break.cpp's port is not patched in yet

// The key nearest below t is found by truncation, from above by rounding up (t + 0.999999)
static constexpr float kRoundUp = 0.999999f; // the original's float at 0x00161d18
static_assert(std::bit_cast<uint32_t>(kRoundUp) == 0x3f7fffef, "kRoundUp");

// A point of the Catmull-Rom spline through p1 and p2 (p0 and p3 the keys either side), t from 0 to 1. Left
// unrounded on the x87 to each store.
// AUTOINJECT
void Spline_Eval3D(_VECTOR *p0, _VECTOR *p1, _VECTOR *p2, _VECTOR *p3, float t, _VECTOR *out) {
    double t1 = (double)t - 1.0f;
    double t1Sq = t1 * t1;
    double a = (double)t * 3.0f - 5.0f;
    out->x = (float)(((((a * p1->x + (double)p2->x * 4.0f) - (double)t * p2->x * 3.0f + t1 * p3->x) * t +
                       ((double)p2->x - t1Sq * p0->x)) * t + ((double)p1->x + p1->x)) * 0.5f);
    out->y = (float)(((((a * p1->y + (double)p2->y * 4.0f) - (double)t * p2->y * 3.0f + t1 * p3->y) * t +
                       ((double)p2->y - t1Sq * p0->y)) * t + ((double)p1->y + p1->y)) * 0.5f);
    out->z = (float)(((((a * p1->z + (double)p2->z * 4.0f) - (double)t * p2->z * 3.0f + t1 * p3->z) * t +
                       ((double)p2->z - t1Sq * p0->z)) * t + ((double)p1->z + p1->z)) * 0.5f);
}

// t kept within lo and hi (the second test only when not below lo; a NaN stays)
static float ClampFrame(float t, ushort lo, ushort hi) {
    if (t < (float)lo)
        return (float)lo;
    if (t > (float)hi)
        return (float)hi;
    return t;
}

// An index kept within lo and hi
static int ClampKey(int key, int lo, int hi) {
    if (key < lo)
        return lo;
    if (key > hi)
        return hi;
    return key;
}

// The matrix (and quaternion) at frame t of a path of keys, first to last (either way round): the position on the
// spline through the keys about t (the key itself where the four are one point), the rotation slerped between the
// two keys either side for modes 1 to 3, or none for mode 0. The quaternion is written only when slerped.
// AUTOINJECT
void Spline_Interp(void *path, ushort first, ushort last, float t, _MATRIX *mtx, quaternion_tag *quat, int mode) {
    if (path == NULL)
        return;
    framelist_tag *keys = (framelist_tag *)path;
    int k0, k1, k2, k3;
    float frac;
    if (last > first) {
        t = ClampFrame(t, first, last);
        ushort key = (ushort)Ftol(t);
        k1 = key < first ? first : (key > last ? last : key);
        k2 = ClampKey(k1 + 1, first, last);
        k0 = ClampKey(k1 - 1, first, last);
        k3 = ClampKey(k1 + 2, first, last);
        frac = (float)((double)t - k1);
    } else {
        t = ClampFrame(t, last, first);
        ushort key = (ushort)Ftol((double)t + kRoundUp);
        k1 = key < last ? last : (key > first ? first : key);
        k2 = ClampKey(k1 - 1, last, first);
        k0 = ClampKey(k1 + 1, last, first);
        k3 = ClampKey(k1 - 2, last, first);
        frac = (float)((double)k1 - t);
    }
    _VECTOR p0 = keys[(ushort)k0].pos;
    _VECTOR p1 = keys[(ushort)k1].pos;
    _VECTOR p2 = keys[(ushort)k2].pos;
    _VECTOR p3 = keys[(ushort)k3].pos;
    _VECTOR pos;
    if (Vec_IsEqual(&p0, &p1, 0.0f) && Vec_IsEqual(&p1, &p2, 0.0f) && Vec_IsEqual(&p2, &p3, 0.0f))
        pos = p0;
    else
        Spline_Eval3D(&p0, &p1, &p2, &p3, frac, &pos);
    quaternion_tag from = keys[(ushort)k1].quat;
    quaternion_tag to = keys[(ushort)k2].quat;

    if (mode == 0) {
        mtx->m[0] = 1.0f;
        mtx->m[5] = 1.0f;
        mtx->m[10] = 1.0f;
        mtx->m[12] = pos.x;
        mtx->m[4] = 0.0f;
        mtx->m[8] = 0.0f;
        mtx->m[1] = 0.0f;
        mtx->m[9] = 0.0f;
        mtx->m[2] = 0.0f;
        mtx->m[6] = 0.0f;
        mtx->m[13] = pos.y;
        mtx->m[14] = pos.z;
        return;
    }
    if (Quat_IsEqual(&from, &to, 0.0f)) {
        Quat_QuatToMat(&from, mtx);
        *Mat_Position(*mtx) = pos;
        return;
    }
    if (mode <= 0 || mode > 3)
        return;
    quaternion_tag q;
    Quat_Slerp_Acc(frac, from.q, to.q, q.q);
    Quat_QuatToMat(&q, mtx);
    *Mat_Position(*mtx) = pos;
    if (quat != NULL)
        *quat = q;
}

// KeyFrame_Interp's last answer: the keys either side, how far between, the positions and rotations of the two and
// the result. Statics of the original's.
// XBE_GLOBAL(0x0029d790, 0x2)
static ushort KeyFrom;
// XBE_GLOBAL(0x0029d77c, 0x2)
static ushort KeyTo;
// XBE_GLOBAL(0x0029d78c, 0x4)
static float KeyBlend;
// XBE_GLOBAL(0x0029d780, 0xc)
static _VECTOR KeyFromPos;
// XBE_GLOBAL(0x0029d770, 0xc)
static _VECTOR KeyToPos;
// XBE_GLOBAL(0x0029d764, 0xc)
static _VECTOR KeyPos;
// XBE_GLOBAL(0x0029d754, 0x10)
static quaternion_tag KeyFromRot;
// XBE_GLOBAL(0x0029d744, 0x10)
static quaternion_tag KeyToRot;
// XBE_GLOBAL(0x0029d734, 0x10)
static quaternion_tag KeyRot;

// The matrix at frame t of a path of keys, first to last (either way round): the position moved from the key below
// t towards the next by the fraction between them (moveTowardsValue, so never past it), the rotation slerped
// AUTOINJECT
void KeyFrame_Interp(void *path, ushort first, ushort last, float t, _MATRIX *mtx) {
    if (path == NULL)
        return;
    framelist_tag *keys = (framelist_tag *)path;
    if (last > first) {
        t = ClampFrame(t, first, last);
        ushort key = (ushort)Ftol(t);
        KeyFrom = key < first ? first : (key > last ? last : key);
        KeyTo = (ushort)(KeyFrom + 1 > last ? last : KeyFrom + 1);
        KeyBlend = (float)((double)t - KeyFrom);
    } else {
        t = ClampFrame(t, last, first);
        ushort key = (ushort)Ftol((double)t + kRoundUp);
        KeyFrom = key < last ? last : (key > first ? first : key);
        KeyTo = (ushort)ClampKey(KeyFrom - 1, last, first);
        KeyBlend = (float)((double)KeyFrom - t);
    }
    KeyFromPos = keys[KeyFrom].pos;
    KeyToPos = keys[KeyTo].pos;
    if (Vec_IsEqual(&KeyFromPos, &KeyToPos, 0.0f)) {
        KeyPos = KeyFromPos;
    } else {
        KeyPos.x = moveTowardsValue(KeyFromPos.x, KeyToPos.x, KeyBlend);
        KeyPos.y = moveTowardsValue(KeyFromPos.y, KeyToPos.y, KeyBlend);
        KeyPos.z = moveTowardsValue(KeyFromPos.z, KeyToPos.z, KeyBlend);
    }
    KeyFromRot = keys[KeyFrom].quat;
    KeyToRot = keys[KeyTo].quat;
    if (Quat_IsEqual(&KeyFromRot, &KeyToRot, 0.0f)) {
        Quat_QuatToMat(&KeyFromRot, mtx);
    } else {
        Quat_Slerp_Acc(KeyBlend, KeyFromRot.q, KeyToRot.q, KeyRot.q);
        Quat_QuatToMat(&KeyRot, mtx);
    }
    *Mat_Position(*mtx) = KeyPos;
}
