#pragma fp_contract(off)

#include "CollisionInstance.h"
#include "World.h"

#include "../platform/RealMath.h"

// ---------------------------------------------------------------------------------------------------------------
// Collision instances, collision objects and triggers (0x000be6f0..0x000be8b0, 0x000cf1f0, 0x000cf260), and the
// small helpers of the collision manager's queries beside them (0x000be950..0x000bed40), ported from the listings.
//
// The float code is the original's x87 arithmetic: chains in double in the original's order, rounded where the
// original stores to a float. The functions that leave their result on the x87 stack answer a double.
// ---------------------------------------------------------------------------------------------------------------

// The name the original answers for an instance whose render instance has none (its .rdata string).
#define kUnnamedInstance ((const char *)0x001939b4)   // "<unnamed>"

namespace {

enum : uint32_t {
    kTagMap = 0x4d617020,                // 'Map ': the world's names
    kTagInstanceName = 0x694e2020,       // 'iN  ': a render instance's name, indexed by the instance
    kTagInstanceNameIndexed = 0x694e0000,
};

constexpr float kOutlineScale = 1.0f / 8192;   // 0x39000000
constexpr float kHeightScale = 1.0f / 256;     // 0x3b800000
constexpr float kTriggerSizeUnit[2] = { 0.25f, 16.0f };

const Coord3 kUp = { 0.0f, 1.0f, 0.0f };

// The rows a matrix built here shares: no w on the axes, the translation's w 1.
void SetTranslation(MATRIX4 *out, bool translate, const Coord3 &position) {
    if (translate)
        *MatrixRow(out, 3) = { position.x, position.y, position.z, 1.0f };
    else
        *MatrixRow(out, 3) = { 0.0f, 0.0f, 0.0f, 1.0f };
}

void SetAxis(Coord4 *row, const Coord3 &axis) {
    row->x = axis.x;
    row->y = axis.y;
    row->z = axis.z;
    row->w = 0.0f;
}

// The minus the dot product of an axis with the position, in the original's order
template <class Axis>
float NegatedDot(const Axis &axis, const Coord3 &position) {
    return float(-(double(axis.x) * position.x) - double(axis.y) * position.y - double(axis.z) * position.z);
}

// The signed area of the point with a triangle's edge (from b to a), in the ground plane
double EdgeArea(const Coord3 *p, const StripVertex &a, const StripVertex &b) {
    return (double(p->z) - b.z) * (double(a.x) - b.x) - (double(p->x) - b.x) * (double(a.z) - b.z);
}

}  // namespace

// ---- collision objects

// FUNC_AT(0x000be6f0)
void WCollisionObject::MakeMatrix(MATRIX4 *out, bool translate) {
    const MATRIX4 &placed = fgWorld->instances[renderIndex].Matrix();
    for (int axis = 0; axis < 3; axis++) {
        out->mtx[axis][0] = placed.mtx[axis][0];
        out->mtx[axis][1] = placed.mtx[axis][1];
        out->mtx[axis][2] = placed.mtx[axis][2];
        out->mtx[axis][3] = 0.0f;
    }
    SetTranslation(out, translate, position);
}

// ---- collision instances

// FUNC_AT(0x000be780)
MATRIX4* WCollisionInstance::GetRenderInstance() {
    return &fgWorld->instances[renderIndex].Matrix();
}

// FUNC_AT(0x000be7a0)
const char* WCollisionInstance::GetName() {
    UGroup *names = fgWorld->group->GroupLocateTag(kTagMap);
    int index = renderIndex;
    uint32_t tag = index == -1 ? kTagInstanceName : uint32_t(index) | kTagInstanceNameIndexed;   // never -1
    UData *name = names->DataLocateTag(tag);
    if (name == names->DataEnd())
        return kUnnamedInstance;
    return reinterpret_cast<const char *>(name->Data());
}

// FUNC_AT(0x000be810)
void WCollisionInstance::CalcPosition(Coord3 *local) {
    local->x = NegatedDot(right, position);
    local->z = NegatedDot(forward, position);
    if (flags & kInstanceTilted) {
        alignas(16) Coord4 up;
        VU0_v4crossprodxyz(&forward, &right, &up);
        local->y = NegatedDot(up, position);
    } else {
        local->y = -position.y;
    }
}

// FUNC_AT(0x000be8b0)
void WCollisionInstance::MakeMatrix(MATRIX4 *out, bool translate) {
    SetAxis(MatrixRow(out, 0), right);
    if (flags & kInstanceTilted)
        VU0_v4crossprodxyz(&forward, &right, out->mtx[1]);
    else
        SetAxis(MatrixRow(out, 1), kUp);
    out->mtx[1][3] = 0.0f;
    SetAxis(MatrixRow(out, 2), forward);
    SetTranslation(out, translate, position);
}

// ---- triggers

// FUNC_AT(0x000cf1f0)
double WTrigger::Size() {
    float unit = kTriggerSizeUnit[(packedSize >> 30) & 1];
    if ((packedSize & 0x80000000) == 0x80000000)
        return double((packedSize >> 20) & 0x3ff) * unit;
    double size = double(packedSize & 0x3ff) * unit;
    return size + size;
}

// FUNC_AT(0x000cf260)
void WTrigger::MakeMatrix(MATRIX4 *out, bool translate) {
    out->mtx[0][0] = right.x;
    out->mtx[0][1] = right.y;
    out->mtx[0][2] = right.z;
    out->mtx[0][3] = 0.0f;
    if (flags & kRotated)
        v3crossprod(&forward, &right, out->mtx[1]);
    else
        SetAxis(MatrixRow(out, 1), kUp);
    out->mtx[1][3] = 0.0f;
    SetAxis(MatrixRow(out, 2), forward);
    SetTranslation(out, translate, position);
}

// ---- the queries' helpers

// The squared distance in the ground plane from the point to the wall: the projection's parameter t along it
// (rounded to a float), then the distance to the nearer end or to the foot of the perpendicular.
// FUNC_AT(0x000be950)
double CollisionBarrier::DistanceSquared2D(const Coord4 *point) const {
    float dz = z1 - z0;
    float dx = x1 - x0;
    float t = float(((double(point->x) - x0) * dx + (double(point->z) - z0) * dz) * inverseLength *
                    inverseLength);
    if (t < 0.0f) {
        double z = double(z0) - point->z;
        double x = double(x0) - point->x;
        return x * x + z * z;
    }
    if (t > 1.0f) {
        double z = double(z1) - point->z;
        double x = double(x1) - point->x;
        return x * x + z * z;
    }
    double x = double(dx) * t + x0;
    float z = float(double(dz) * t + z0 - point->z);
    x = x - point->x;
    return x * x + double(z) * z;
}

// FUNC_AT(0x000bea50)
void WindowPane::MakeCorners(const Coord4 *origin, const Coord4 *along, Coord3 *corners) const {
    corners[0].x = float(double(start) * along->x * kOutlineScale + origin->x);
    corners[0].z = float(double(start) * along->z * kOutlineScale + origin->z);
    corners[0].y = float(double(top) * kHeightScale + origin->y);
    corners[1].x = float(double(width) * along->x * kOutlineScale + corners[0].x);
    corners[1].y = corners[0].y;
    corners[2].x = corners[1].x;
    corners[1].z = float(double(width) * along->z * kOutlineScale + corners[0].z);
    corners[2].z = corners[1].z;
    corners[3].x = corners[0].x;
    corners[3].z = corners[0].z;
    corners[2].y = float(double(top) * kHeightScale + origin->y - double(height) * kHeightScale);
    corners[3].y = corners[2].y;
}

// FUNC_AT(0x000beb40)
const WindowSet* WindowGroup::FindSet(uint32_t id) const {
    const WindowSet *set = Sets();
    for (uint32_t i = 0; i < setCount; i++) {
        if (set->id == id)
            return set;
        set += 1 + set->paneCount;   // past its panes (a pane is as long as a set's head)
    }
    return NULL;
}

// FUNC_AT(0x000beb70)
WindowHit* WindowHit::Construct(uint32_t kind, uint32_t hits, const Coord3 *corner0, const Coord3 *corner1) {
    this->kind = kind;
    this->hits = hits;
    unknown08 = 0;
    this->corner0 = { corner0->x, corner0->y, corner0->z, 1.0f };
    this->corner1 = { corner1->x, corner1->y, corner1->z, 1.0f };
    return this;
}

// A point on an edge counts as inside. An unordered area (NaN) sends the first test to the second branch and
// fails the others.
// FUNC_AT(0x000bebd0)
bool PointInTriangle2D(const Coord3 *point, const StripVertex *triangle) {
    if (EdgeArea(point, triangle[0], triangle[1]) <= 0.0)
        return EdgeArea(point, triangle[1], triangle[2]) <= 0.0 && EdgeArea(point, triangle[2], triangle[0]) <= 0.0;
    return EdgeArea(point, triangle[1], triangle[2]) >= 0.0 && EdgeArea(point, triangle[2], triangle[0]) >= 0.0;
}

// FUNC_AT(0x000becb0)
bool PointInTriangle2DClockwise(const Coord3 *point, const StripVertex *triangle) {
    return EdgeArea(point, triangle[0], triangle[1]) <= 0.0 && EdgeArea(point, triangle[1], triangle[2]) <= 0.0 &&
           EdgeArea(point, triangle[2], triangle[0]) <= 0.0;
}

// FUNC_AT(0x000bed40)
bool PointInTriangle2DCounter(const Coord3 *point, const StripVertex *triangle) {
    return EdgeArea(point, triangle[0], triangle[1]) >= 0.0 && EdgeArea(point, triangle[1], triangle[2]) >= 0.0 &&
           EdgeArea(point, triangle[2], triangle[0]) >= 0.0;
}
