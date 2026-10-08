#pragma fp_contract(off)

#include "CameraSpline.h"

#include "../engine/UMemory.hpp"
#include "../world/CollisionTypes.h"   // MatrixRow
#include "../platform/RealMath.h"
#include "../../helpers.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <xmmintrin.h>

// ---------------------------------------------------------------------------------------------------------------
// RCameraMath, RCameraSpline and its point list, FindPointInList, and three functions of other units the linker
// put after them. See CameraSpline.h.
// ---------------------------------------------------------------------------------------------------------------

#define RadiansToTurns FLOAT_AT(0x001ebcb0)            // 1 / (2 pi), set by a static initialiser
#define MaterialData PTR_AT(0x001ebce4)                // RCARPFile::LoadEAGLMaterials' file

constexpr uint32_t kGameSymbolTableVtable = 0x00190e18;

namespace {

const Coord4 kUp = { 0.0f, 1.0f, 0.0f, 0.0f };

const MATRIX4 kIdentity = { {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
} };

// The cubic bases, rows by power of t (t^3, t^2, t, 1) against the four control points
const MATRIX4 kBezierBasis = { {
    { -1.0f, 3.0f, -3.0f, 1.0f },
    { 3.0f, -6.0f, 3.0f, 0.0f },
    { -3.0f, 3.0f, 0.0f, 0.0f },
    { 1.0f, 0.0f, 0.0f, 0.0f },
} };
// ... Catmull-Rom's, doubled (the point is halved after)
const MATRIX4 kCatmullRomBasis = { {
    { -1.0f, 3.0f, -3.0f, 1.0f },
    { 2.0f, -5.0f, 4.0f, -1.0f },
    { -1.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 2.0f, 0.0f, 0.0f },
} };
// Their derivatives' (the t^3 row empty)
const MATRIX4 kBezierTangentBasis = { {
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { -3.0f, 9.0f, -9.0f, 3.0f },
    { 6.0f, -12.0f, 6.0f, 0.0f },
    { -3.0f, 3.0f, 0.0f, 0.0f },
} };
const MATRIX4 kCatmullRomTangentBasis = { {
    { 0.0f, 0.0f, 0.0f, 0.0f },
    { -3.0f, 6.0f, 3.0f, 0.0f },
    { 6.0f, -10.0f, 0.0f, 4.0f },
    { -3.0f, 4.0f, 1.0f, 0.0f },
} };

// (t^3, t^2, t, 1): t^2 kept unrounded for the cube
Coord4 Powers(float t) {
    double square = double(t) * t;
    Coord4 powers = { float(square * t), float(square), t, 1.0f };
    return powers;
}

// a + t (b - a) in four lanes: the VU0 lerp the original inlines (SSE, so each step rounds to a float)
void Lerp(const Coord4 *a, const Coord4 *b, float t, Coord4 *out) {
    __m128 from = _mm_loadu_ps(&a->x);
    __m128 step = _mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(_mm_loadu_ps(&b->x), from));
    _mm_storeu_ps(&out->x, _mm_add_ps(step, from));
}

} // namespace

// ---- the C runtime's arc cosine and sine

__declspec(naked) double CrtAcosTimes(float, const float *) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x001328d0
        call eax
        mov eax, dword ptr [esp + 8]
        fmul dword ptr [eax]
        ret
    }
}

__declspec(naked) double CrtAsinTimes(float, const float *) {
    __asm {
        fld dword ptr [esp + 4]
        mov eax, 0x00133f94
        call eax
        mov eax, dword ptr [esp + 8]
        fmul dword ptr [eax]
        ret
    }
}

// ---- RCameraMath

// FUNC_AT(0x0007a1e0)
void RCameraMath::BuildRotationQuat(Coord4 *quat, float turns, const Coord4 *axis) {
    float half = turns * 0.5f;
    float angle = half;
    if (half < 0.0f)
        angle = half + 1.0f;
    VU0_v4scale(axis, sin_fractionalangle(angle), quat);
    angle = half;
    if (half < 0.0f)
        angle = half + 1.0f;
    quat->w = cos_fractionalangle(angle);
    VU0_v4unit(quat, quat);
}

// FUNC_AT(0x0007a280)
void RCameraMath::BuildRotationMat4(MATRIX4 *matrix, float turns, const Coord4 *axis) {
    Coord4 quat;
    BuildRotationQuat(&quat, turns, axis);
    VU0_quattom4(matrix, &quat);
}

// FUNC_AT(0x0007a2b0)
void RCameraMath::VU0_GenerateMatrix4Unit(const Coord4 *forward, MATRIX4 *matrix) {
    Coord4 up = kUp;
    VU0_v4unitcrossprodxyz(&up, forward, MatrixRow(matrix, 0));
    VU0_v4crossprodxyz(MatrixRow(matrix, 2), MatrixRow(matrix, 0), MatrixRow(matrix, 1));
    VU0_v4Init(MatrixRow(matrix, 3));
    MatrixRow(matrix, 2)->w = 0.0f;
    MatrixRow(matrix, 1)->w = 0.0f;
    MatrixRow(matrix, 0)->w = 0.0f;
}

// FUNC_AT(0x0007a320)
void RCameraMath::VU0_GenerateMatrix4UnitAt(const Coord4 *forward, const Coord4 *position, MATRIX4 *matrix) {
    Coord4 up = kUp;
    VU0_v4unitcrossprodxyz(&up, forward, MatrixRow(matrix, 0));
    VU0_v4crossprodxyz(MatrixRow(matrix, 2), MatrixRow(matrix, 0), MatrixRow(matrix, 1));
    VU0_v4copy(position, MatrixRow(matrix, 3));
    MatrixRow(matrix, 2)->w = 0.0f;
    MatrixRow(matrix, 1)->w = 0.0f;
    MatrixRow(matrix, 0)->w = 0.0f;
    MatrixRow(matrix, 3)->w = 1.0f;
}

// FUNC_AT(0x0007a3b0)
double RCameraMath::ACosTurns(float cosine) {
    if (cosine > 1.0f)
        cosine = 1.0f;
    else if (cosine < -1.0f)
        cosine = -1.0f;
    return CrtAcosTimes(cosine, &RadiansToTurns);
}

// FUNC_AT(0x0007a410)
void RCameraMath::MatrixFromDirection(const Coord4 *direction, MATRIX4 *matrix) {
    Coord4 *forward = MatrixRow(matrix, 2);
    VU0_v4unitxyz(direction, forward);
    forward->w = 0.0f;
    VU0_GenerateMatrix4Unit(forward, matrix);
}

// FUNC_AT(0x0007a440)
void RCameraMath::MatrixFromDirectionAt(const Coord4 *direction, const Coord4 *position, MATRIX4 *matrix) {
    Coord4 *forward = MatrixRow(matrix, 2);
    VU0_v4unitxyz(direction, forward);
    forward->w = 0.0f;
    VU0_GenerateMatrix4UnitAt(forward, position, matrix);
}

// FUNC_AT(0x0007a470)
double RCameraMath::AngleTurns(const Coord4 *a, const Coord4 *b) {
    float angle = float(ACosTurns(v3dotprod(a, b)));
    Coord4 cross;
    VU0_v4crossprodxyz(a, b, &cross);
    if (cross.y < 0.0f)
        return 1.0 - angle;
    return angle;
}

// FUNC_AT(0x0007a4e0)
void RCameraMath::VU0_GenerateQuat(const Coord4 *direction, Coord4 *quat) {
    MATRIX4 frame;
    Coord4 *forward = MatrixRow(&frame, 2);
    VU0_v4unitxyz(direction, forward);
    forward->w = 0.0f;
    VU0_GenerateMatrix4Unit(forward, &frame);
    VU0_m4toquat(quat, &frame);
}

// ---- RCameraSpline

// FUNC_AT(0x0007a530)
void RCameraSpline::EvaluateSpline(float t, Coord4 *point) {
    Coord4 powers = Powers(t);
    MATRIX4 coefficients;
    VU0_MATRIX4_mult(&coefficients, type == kSplineCatmullRom ? &kCatmullRomBasis : &kBezierBasis, points);
    VU0_MATRIX4_vect4mult(&powers, &coefficients, point);
    point->w = 1.0f;
    if (type == kSplineCatmullRom)
        VU0_v4scale(point, 0.5f, point);
}

// FUNC_AT(0x0007a5c0)
void RCameraSpline::EvaluateTangent(float t, Coord4 *tangent) {
    Coord4 powers = Powers(t);
    MATRIX4 coefficients;
    VU0_MATRIX4_mult(&coefficients, type == kSplineCatmullRom ? &kCatmullRomTangentBasis : &kBezierTangentBasis,
                     points);
    VU0_MATRIX4_vect4mult(&powers, &coefficients, tangent);
    VU0_v4unitxyz(tangent, tangent);
    tangent->w = 1.0f;
}

// FUNC_AT(0x0007a640)
uint32_t RCameraSpline::GetPointListSize() {
    return pointList.size;
}

// FUNC_AT(0x0007a650)
void RCameraSpline::BuildSplineEx(const Coord3 *start, const Coord3 *startControl, const Coord3 *end,
                                  const Coord3 *endControl) {
    if (pointList.size > 1)
        return;
    type = kSplineBezier;
    points[0].x = start->x;
    points[0].y = start->y;
    points[0].z = start->z;
    points[1].x = startControl->x;
    points[1].y = startControl->y;
    points[1].z = startControl->z;
    points[2].x = endControl->x;
    points[2].y = endControl->y;
    points[2].z = endControl->z;
    points[3].x = end->x;
    points[3].y = end->y;
    points[3].z = end->z;
    points[0].w = 0.0f;
    points[1].w = 0.0f;
    points[2].w = 0.0f;
    points[3].w = 0.0f;
}

// FUNC_AT(0x0007a6d0)
void RCameraSpline::EvaluateSpline(float t, MATRIX4 *frame, bool blendRotations) {
    Coord4 powers = Powers(t);
    MATRIX4 coefficients;
    VU0_MATRIX4_mult(&coefficients, type == kSplineCatmullRom ? &kCatmullRomBasis : &kBezierBasis, points);
    Coord4 point;
    VU0_MATRIX4_vect4mult(&powers, &coefficients, &point);
    point.w = 1.0f;
    if (type == kSplineCatmullRom)
        VU0_v4scale(&point, 0.5f, &point);
    if (blendRotations) {
        Coord4 rotation;
        VU0_fastqslerp(&rotations[0], &rotations[1], &rotation, t);
        VU0_quattom4(frame, &rotation);
        *MatrixRow(frame, 3) = point;
        return;
    }
    // Looking along the blend of the two ends' z axes
    Coord4 fromAxis = {}, toAxis = {}, direction;
    VU0_ExtractZAxis3FromQuat(&rotations[0], &fromAxis);
    VU0_ExtractZAxis3FromQuat(&rotations[1], &toAxis);
    Lerp(&fromAxis, &toAxis, t, &direction);
    RCameraMath::MatrixFromDirectionAt(&direction, &point, frame);
}

// FUNC_AT(0x0007a850)
RCameraSpline::PointNode* RCameraSpline::PointList::BuyNode(PointNode *next, PointNode *prev, const Coord4 *value) {
    return BuyNodeT(next, prev, value);
}

// FUNC_AT(0x0007a8a0)
RCameraSpline::PointNode** FindPointInList(RCameraSpline::PointNode **result, RCameraSpline::PointList *list,
                                           const Coord4 *point) {
    RCameraSpline::PointNode *node = list->Begin();
    for (uint32_t i = 0; i < list->size; i++) {
        if (point->x == node->value.x && point->y == node->value.y && point->z == node->value.z)
            break;
        node = node->next;
    }
    *result = node;
    return result;
}

// FUNC_AT(0x0007a900)
RCameraSpline::PointNode** RCameraSpline::PointList::Erase(PointNode **result, PointNode *first, PointNode *last) {
    return EraseT(result, first, last);
}

// FUNC_AT(0x0007a950)
RCameraSpline::PointNode* RCameraSpline::PointList::BuyHead() {
    return BuyHeadT();
}

// FUNC_AT(0x0007a9b0)
void RCameraSpline::Destruct() {
    pointList.DestructT();
}

// FUNC_AT(0x0007a9f0)
void RCameraSpline::ClearSplinePtList() {
    PointNode *end;
    pointList.Erase(&end, pointList.Begin(), pointList.head);
}

// FUNC_AT(0x0007aa20)
void RCameraSpline::BuildSpline(const Coord4 *from, Coord4 *fromDirection, const Coord4 *to, Coord4 *toDirection,
                                const Coord4 *fromRotation, const Coord4 *toRotation, float tension) {
    if (fromDirection->x != 0.0f || fromDirection->y != 0.0f || fromDirection->z != 0.0f)
        VU0_v4unitxyz(fromDirection, fromDirection);
    if (toDirection->x != 0.0f || toDirection->y != 0.0f || toDirection->z != 0.0f)
        VU0_v4unitxyz(toDirection, toDirection);
    float reach = vec3distance(from, to) * tension;
    if (pointList.size > 1) {
        type = kSplineCatmullRom;
        PointNode *node;
        FindPointInList(&node, &pointList, to);
        if (node == pointList.head)
            return;
        if (node == pointList.Begin()) {
            // the path's first segment: a point behind `from` stands in for the one before it
            VU0_v4scaleadd(fromDirection, -reach, from, &points[0]);
            points[1] = *from;
            points[2] = *to;
            points[3] = node->next->value;
        } else {
            points[0] = points[1];
            points[1] = *from;
            points[2] = *to;
            if (node->next == pointList.head)
                VU0_v4scaleadd(toDirection, reach, to, &points[3]);   // the last: a point beyond `to`
            else
                points[3] = node->next->value;
        }
    } else {
        type = kSplineBezier;
        points[0] = *from;
        VU0_v4scaleadd(fromDirection, reach, from, &points[1]);
        VU0_v4scaleadd(toDirection, -reach, to, &points[2]);
        points[3] = *to;
        PointNode *end;
        pointList.Erase(&end, pointList.Begin(), pointList.head);
    }
    points[0].w = 0.0f;
    points[1].w = 0.0f;
    points[2].w = 0.0f;
    points[3].w = 0.0f;
    if (fromRotation != NULL && toRotation != NULL) {
        VU0_v4copy(fromRotation, &rotations[0]);
        VU0_v4copy(toRotation, &rotations[1]);
    }
}

// FUNC_AT(0x0007acd0)
RCameraSpline* RCameraSpline::Construct() {
    pointList.head = pointList.BuyHead();
    pointList.size = 0;
    type = kSplineUnbuilt;
    memcpy(points, &kIdentity, sizeof(points));
    VU0_v4Init(&rotations[0]);
    VU0_v4Init(&rotations[1]);
    return this;
}

// FUNC_AT(0x0007ad50)
void RCameraSpline::PointList::IncreaseSize(uint32_t count) {
    IncreaseSizeT(count);
}

// FUNC_AT(0x0007ae00)
void RCameraSpline::AddToSplinePtList(const Coord4 *point) {
    PointNode *head = pointList.head;
    PointNode *node = pointList.BuyNode(head, head->prev, point);
    pointList.IncreaseSize(1);
    head->prev = node;
    node->prev->next = node;
}

// ---- placed here by the linker

// FUNC_AT(0x0007ae40)
GameSymbolTable* GameSymbolTable::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(GameSymbolTable));
    return this;
}

// FUNC_AT(0x0007ae60)
void GameSymbolTable::Destruct() {
    vtable = reinterpret_cast<void *>(uintptr_t(kGameSymbolTableVtable));
    USymbolTable::Destruct();
}

// FUNC_AT(0x0007ae70)
uint32_t UDataRecord::MatchTag() const {
    return UData::MatchTag();
}

// FUNC_AT(0x0007ae90)
void* NoSymbolCallback(const char *name, bool *found) {
    *found = false;
    return NULL;
}

// FUNC_AT(0x0007aee0)
void FreeEAGLMaterials() {
    UMemory::Free(MaterialData);
}
