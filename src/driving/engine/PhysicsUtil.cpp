#pragma fp_contract(off)

#include "PhysicsUtil.h"
#include "UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"

#include <math.h>
#include <bit>
#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------
// The physics helpers (docs/driving/maths.md for the x87 rules): each is the original's x87 code in double, in
// its order, rounded to float where it stores a float. Where the original keeps an intermediate on the x87 stack
// and also stores it (FST), the unrounded value goes on and the stored float is what later loads see; both are
// spelled out. The VU0 functions are ours (platform/VU0Math.cpp), called as the original calls them.
// ---------------------------------------------------------------------------------------------------------------

// The C runtime's rand, the game's: its state is the game's.
#define CRT_rand ((int (*)(void))0x00133ee0)

// Util_GaussRandom's second number, kept for the next call
struct GaussSpare {
    float value;                // +0x00
    uint8_t available;          // +0x04
};
static_assert(sizeof(GaussSpare) == 8, "the kept number and its flag");
#define GaussRandomSpare (*(GaussSpare *)0x00239a60)

static constexpr float kRandToSigned = 0x1.0002p-14f;       // rand() * this - 1 is in [-1, 1]: 2 / 32767, as a float
static_assert(kRandToSigned == 2.0f / 32767.0f, "Util_GaussRandom's scale");
static constexpr float kRegionGrowth = 1.1f;
static constexpr float kTensorLongBody = 5.0f;
static constexpr float kCarTensorLongBody = 4.0f;
static constexpr float kOneTwelfth = 0.08333f;          // four decimals, as the source has them
static constexpr float kOneThird = 0.3333f;
static_assert(std::bit_cast<uint32_t>(kOneTwelfth) == 0x3daaa8eb, "the tensors' 1/12");
static_assert(std::bit_cast<uint32_t>(kOneThird) == 0x3eaaa64c, "the car tensor's 1/3");
static constexpr float kRayLength = 1000.0f;

// FPATAN's result is returned unrounded (atan_turns is assembly that leaves it in ST0); the original subtracts it
// straight from 1, so it is read here as the double it is rather than through the float declaration, which would
// round it.
typedef double (*UnroundedTurnsFn)(float, float);
static double AtanTurnsUnrounded(float y, float x) {
    return reinterpret_cast<UnroundedTurnsFn>(&atan_turns)(y, x);
}

// FLDLN2, FYL2X: the natural logarithm exactly as the x87 makes it (the C runtime's log need not agree in the last
// bit).
__declspec(naked) static double LogX87(double) {
    __asm {
        fldln2
        fld qword ptr [esp + 4]
        fyl2x
        ret
    }
}

// FUNC_AT(0x000bce10)
double Util_Step(float current, float target, float step) {
    if (!(target < current)) {          // upwards (and when either is NaN)
        double next = double(current) + step;
        if (next > target)
            return target;
        return next;
    }
    double next = double(current) - step;
    if (next < target)
        return target;
    return next;
}

// FUNC_AT(0x000bce50)
float Util_Bound(float value, float limit, float bound) {
    if (value > limit)
        return bound;
    if (-limit > value)
        return -bound;
    return value;
}

// The rows' w: row 0's and row 1's are never written in the original (VU0 cross products keep the destination's
// w), so the matrix gets whatever its stack held there; they are 0 here. Row 2's is 0, row 3 is (0, 0, 0, 1).
// FUNC_AT(0x000bce90)
MATRIX4* Util_GenerateMatrix(MATRIX4 *result, const Coord3 *direction) {
    alignas(16) const float up[4] = { 0.0f, 1.0f, 0.0f, 0.0f };
    alignas(16) MATRIX4 m = {};
    m.mtx[2][0] = direction->x;
    m.mtx[2][1] = direction->y;
    m.mtx[2][2] = direction->z;
    m.mtx[2][3] = 0.0f;
    VU0_v4unitxyz(m.mtx[2], m.mtx[2]);
    VU0_v4unitcrossprodxyz(up, m.mtx[2], m.mtx[0]);
    VU0_v4crossprodxyz(m.mtx[2], m.mtx[0], m.mtx[1]);
    VU0_v4Init(m.mtx[3]);
    *result = m;
    return result;
}

// FUNC_AT(0x000bcf40)
Coord3* Util_GenerateTensor(Coord3 *result, float mass, float sizeX, float sizeY, float sizeZ) {
    float x, y, z;
    if ((double(sizeX) + sizeZ) * kTensorLongBody < sizeY) {
        // long and thin: a cylinder along y
        double length = double(sizeZ) + sizeZ;
        float across = float(double(sizeX) * sizeX + double(sizeY) * sizeY);
        x = y = float(1.0 / (length * length * (double(mass) * kOneTwelfth) + double(mass) * 0.25f * across));
        z = float(1.0 / (double(mass) * 0.5f * across));
    } else {
        // a box; the original stores sizeX squared to a float and adds that, the other squares unrounded
        double ySquared = double(sizeY) * sizeY, zSquared = double(sizeZ) * sizeZ;
        x = float(3.0f / ((zSquared + ySquared) * mass));
        float xSquared = sizeX * sizeX;
        y = float(3.0f / ((zSquared + xSquared) * mass));
        z = float(3.0f / ((ySquared + xSquared) * mass));
    }
    result->x = x;
    result->y = y;
    result->z = z;
    return result;
}

// FUNC_AT(0x000bd030)
Coord3* Util_GenerateCarTensor(Coord3 *result, float mass, float sizeX, float sizeY, float sizeZ) {
    float x, y, z;
    if (sizeZ > kCarTensorLongBody) {
        // a box; sizeX squared is stored to a float (over sizeZ) and the third diagonal adds that
        double third = double(mass) * kOneThird;
        double ySquared = double(sizeY) * sizeY, zSquared = double(sizeZ) * sizeZ;
        x = float(1.0f / ((zSquared + ySquared) * third));
        double xSquared = double(sizeX) * sizeX;
        float xSquaredStored = float(xSquared);
        y = float(1.0f / ((xSquared + zSquared) * third));
        z = float(1.0f / ((xSquaredStored + ySquared) * third));
    } else {
        // a cylinder along z
        double length = double(sizeZ) + sizeZ;
        float across = float(double(sizeX) * sizeX + double(sizeY) * sizeY);
        x = y = float(0.5f / (length * length * (double(mass) * kOneTwelfth) + double(mass) * 0.25f * across));
        z = float(0.5f / (double(mass) * 0.5f * across));
    }
    result->x = x;
    result->y = y;
    result->z = z;
    return result;
}

// FUNC_AT(0x000bd130)
double Util_GaussRandom() {
    if (GaussRandomSpare.available) {
        GaussRandomSpare.available = 0;
        return GaussRandomSpare.value;
    }
    float u, v;
    double radiusSquared;
    do {
        u = float(double(CRT_rand()) * kRandToSigned - 1.0f);
        double vUnrounded = double(CRT_rand()) * kRandToSigned - 1.0f;
        v = float(vUnrounded);
        radiusSquared = vUnrounded * v + double(u) * u;     // v's square from the unrounded and the stored v
    } while (radiusSquared >= 1.0f);    // a NaN would end the loop, as the original's test does
    float factor = VU0_sqrt(float(LogX87(radiusSquared) / radiusSquared * -2.0f));
    GaussRandomSpare.available = 1;
    GaussRandomSpare.value = float(double(v) * factor);
    return double(factor) * u;
}

// FUNC_AT(0x000bd1e0)
Coord3* Util_PerturbVector(Coord3 *result, float yTurns, float zTurns, const Coord4 *direction) {
    alignas(16) MATRIX4 rotation;
    alignas(16) Coord4 turned = {};     // w is never read
    VU0_MATRIX4setyrot(&rotation, yTurns);
    VU0_MATRIX4_vect3rotate(direction, &rotation, &turned);
    VU0_MATRIX4setzrot(&rotation, zTurns);
    VU0_MATRIX4_vect3rotate(&turned, &rotation, &turned);
    result->x = turned.x;
    result->y = turned.y;
    result->z = turned.z;
    return result;
}

// FUNC_AT(0x000bd250)
Coord3* Util_PerturbVector(Coord3 *result, const Coord4 *direction, float spread) {
    alignas(16) MATRIX4 rotation;
    alignas(16) Coord4 turned = {};
    float yTurns = float(Util_GaussRandom() * spread);
    float zTurns = float(Util_GaussRandom() * spread);
    VU0_MATRIX4setyrot(&rotation, yTurns);
    VU0_MATRIX4_vect3rotate(direction, &rotation, &turned);
    VU0_MATRIX4setzrot(&rotation, zTurns);
    VU0_MATRIX4_vect3rotate(&turned, &rotation, &turned);
    result->x = turned.x;
    result->y = turned.y;
    result->z = turned.z;
    return result;
}

// The wrap-arounds count in double from the stored float, and the first test is on the unrounded value (which
// atan's range keeps in [0.5, 1.5], so only the second can loop).
// FUNC_AT(0x000bd2e0)
void Util_VecToPoleDepthAndLong(const Coord3 *direction, float *depth, float *longitude) {
    const float x = direction->x, y = direction->y, z = direction->z;
    *depth = float(1.0f - (double(y) + 1.0f) * 0.5f);
    double turns = 1.0f - AtanTurnsUnrounded(x, z);
    *longitude = float(turns);
    if (turns < 0.0f) {
        double wrapped = *longitude;
        do
            wrapped += 1.0f;
        while (wrapped < 0.0f);
        *longitude = float(wrapped);
    }
    if (*longitude > 1.0f) {
        double wrapped = *longitude;
        do
            wrapped -= 1.0f;
        while (wrapped > 1.0f);
        *longitude = float(wrapped);
    }
}

// CVTSS2SI rounds to nearest (MXCSR's mode), after the scale is stored to a float.
// FUNC_AT(0x000bd390)
char Util_CharMapLookup(float u, float v, const char *map, int width, int height) {
    int column = RoundToInt(float(double(width - 1) * u));
    int row = RoundToInt(float(double(height - 1) * v));
    return map[row * width + column];
}

// FUNC_AT(0x000bd3e0)
void Util_SegmentDirection(const Coord4 *from, const Coord4 *to, float *inverseLengthSquared, Coord4 *direction) {
    VU0_v4sub(to, from, direction);
    const double x = direction->x, y = direction->y, z = direction->z;
    double lengthSquared = x * x + y * y + z * z;
    if (lengthSquared == 0.0f) {
        *inverseLengthSquared = 0.0f;
        return;
    }
    *inverseLengthSquared = float(1.0f / lengthSquared);
}

// FUNC_AT(0x000bd450)
double Util_ApplyVariance(float value, float variance) {
    double gauss = Util_GaussRandom();
    return gauss * (double(value) * variance) + value;
}

// The parameter is clamped on its unrounded value against 1 and its stored float against 0.
// FUNC_AT(0x000bd470)
void Util_NearestPointOnSegment(const Coord4 *point, const Coord4 *start, float inverseLengthSquared,
                                const Coord4 *direction, Coord4 *nearest) {
    double t = ((double(point->z) - start->z) * direction->z + (double(point->y) - start->y) * direction->y +
                (double(point->x) - start->x) * direction->x) * inverseLengthSquared;
    float stored = float(t);
    double clamped;
    if (!(t < 1.0f))
        clamped = 1.0f;
    else if (stored > 0.0f)
        clamped = stored;
    else
        clamped = 0.0f;
    nearest->x = float(clamped * direction->x + start->x);
    nearest->y = float(clamped * direction->y + start->y);
    nearest->z = float(clamped * direction->z + start->z);
}

// FUNC_AT(0x000bd500)
bool Util_CollideRayWithSphere(const Coord4 *origin, const Coord4 *direction, const Coord4 *centre, float radius,
                               Coord3 *hit) {
    const float radiusSquared = radius * radius;
    if (VU0_v3distancesquare(centre, origin) < radiusSquared)
        return false;
    alignas(16) Coord4 unit = {}, far = {}, segment = {}, nearest = {};
    float inverseLengthSquared;
    VU0_v4unitxyz(direction, &unit);
    VU0_v4scaleadd(&unit, kRayLength, origin, &far);
    Util_SegmentDirection(origin, &far, &inverseLengthSquared, &segment);
    Util_NearestPointOnSegment(centre, origin, inverseLengthSquared, &segment, &nearest);
    if (!(VU0_v3distancesquare(&nearest, centre) <= radiusSquared))
        return false;
    hit->x = nearest.x;
    hit->y = nearest.y;
    hit->z = nearest.z;
    return true;
}

// FUNC_AT(0x000bd5d0)
bool Util_CollideRayWithStretchedSphere(const Coord4 *origin, const Coord4 *direction, const Coord4 *centre,
                                        float radius, float stretch, Coord3 *hit) {
    const float squeeze = 1.0f / stretch;
    alignas(16) Coord4 unit = {};
    VU0_v4unitxyz(direction, &unit);
    unit.y = unit.y * squeeze;
    alignas(16) Coord4 squeezedOrigin = { origin->x, origin->y, origin->z, 0.0f };
    squeezedOrigin.y = squeezedOrigin.y * squeeze;
    alignas(16) Coord4 squeezedCentre = { centre->x, centre->y, centre->z, 0.0f };
    squeezedCentre.y = squeezedCentre.y * squeeze;
    bool collided = Util_CollideRayWithSphere(&squeezedOrigin, &unit, &squeezedCentre, radius, hit);
    if (collided) {
        // stored once scaled, then again with the centre's height added - which is read after that store
        double height = (double(hit->y) - squeezedCentre.y) * stretch;
        hit->y = float(height);
        hit->y = float(height + centre->y);
    }
    return collided;
}

// 0x000bd690's body; the original takes its arguments in registers, which the adapter below moves into a call to this.
void Util_FitColliderRegion(uint8_t moved, const Coord4 *position, Coord4 *centre, float *regionRadius,
                            float radius, const Coord4 *previous) {
    if (!moved) {
        centre->x = position->x;
        centre->y = position->y;
        centre->z = position->z;
        *regionRadius = radius * kRegionGrowth;
        return;
    }
    alignas(16) Coord4 movement = {};
    VU0_v4sub(position, previous, &movement);
    float distance = VU0_v3length(&movement);
    if (1.0f < distance)
        distance = 1.0f;
    *regionRadius = float((double(distance) + radius) * kRegionGrowth);
    VU0_v4scale(&movement, kRegionGrowth, &movement);
    VU0_v3add(position, &movement, centre);
}

// AL = moved, ESI = position, EDI = centre, EBX = the radius out; on the stack the radius, the previous position
// and a word nothing reads. EBX, ESI, EDI and EBP kept, as the original keeps them.
// AUTOLTCG
__declspec(naked) void FUN_000bd690() {
    __asm {
        push dword ptr [esp + 8]
        push dword ptr [esp + 8]
        push ebx
        push edi
        push esi
        movzx eax, al
        push eax
        call Util_FitColliderRegion
        add esp, 24
        ret
    }
}

// FUNC_AT(0x000bd730)
void __stdcall BarrierList_Deallocate(void *block, int count) {
    if (block != NULL)
        UMemory::FastFree(block, count * 40);
}
