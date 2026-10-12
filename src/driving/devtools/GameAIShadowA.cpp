#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAIShadowA.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "../data/AttributeSet.h"
#include "../data/AttributeValue.h"
#include "../data/Carp.h"
#include "../engine/CoreContainers.h"
#include "../game/MissionManager.h"
#include "../engine/SimRandom.h"
#include "../game/AIGroundVehicle.h"
#include "../game/AISplinePath.h"
#include "../game/AIVehicleController.h"
#include "../game/Vehicle.h"
#include "../game/VehicleSound.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../game/Simulation.h"
#include "../render/PathEngine.h"
#include "../world/CollisionTypes.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.ai's package A (AIGroundVehicle, part 1), run once from the first simulation tick when
// NIGHTFIRE_GAMEAISHADOWA is set. Each case runs the ORIGINAL (every entry of the package swapped back together) and
// then the PORT from the same state, and compares what they leave - the synthetic vehicle and its target, their cars,
// bodies, navigators, sound, spline path, the AI zone table, the traffic spawn count, the road network's statics and
// road counts, the Simulation's random generator and step count, our two statics (mirrored to the game's addresses
// round the original's run) - the answers, and a log of every call out. Every function of the package is run:
//
//   - the constructor, destructor and vector deleting destructor (every flag), Reset;
//   - the modes (every mode value, the traffic type, the lookups' answers), GetTargetPos, UpdateNavPos (the traffic
//     and agent ways, curved and straight segments, the navigator's heading flags), auto-drive and spline paths
//     engaged and disengaged, DoPlayerLogic, DoSplinePathUpdate;
//   - DrivableVec, BarriersInPath, UpdateStuck, the collision navigators (steps round their 15-step refresh, lanes),
//     TestForCollision, the three lane-weight functions;
//   - DoAttackMode (every attack mode and vehicle type, hit points, EMP, the target near and far, hidden and seen),
//     TurnTurret (angles either side of 0, the turret range found or not), DoAutoDriveUpdate, DoAgentStateUpdate
//     (zones, charge rolls, the line-of-sight steps, the goal's every source, navigate mode changes), UnSpawn.
//
// The cars are synthetic PhysicsObjects whose vtables are logging fakes; their bodies copies of the live player car's
// (the Simulation's body getter is faked to hand them out). Fakes (5-byte jumps at the callee both sides reach): the
// navigators' methods, the curve tangent, the road network's lane weights, the collision manager's and the collider's
// world queries, the AI element and zone lookups, the mission manager's event and glue factor, the attribute lookups,
// AVehicle::Reset, SetSplinePath, RigidBody::ResetObject, the animation handle's SetFrame, the allocators and the C
// runtime's vector iterator. The maths, the world position, the collision record's constructor, MinFloat, MaxFloat,
// SimRandom and the other AIVehicle methods run for real on both sides.
//
// A mutation the test sees: GetBestWeightedLane's `weight < bestWeight` written `<=` - every tie (the lane weights
// are often equal) moves the answer to the later stretch.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x00029d60, 0x0002a0d0, 0x0002a2c0, 0x0002a330, 0x0002a380, 0x0002a3c0, 0x0002a3e0, 0x0002a690, 0x0002a760,
    0x0002a7c0, 0x0002a800, 0x0002a850, 0x0002a8b0, 0x0002a9e0, 0x0002aaf0, 0x0002af50, 0x0002b0c0, 0x0002b210,
    0x0002b4d0, 0x0002b5e0, 0x0002b660, 0x0002b7a0, 0x0002bd40, 0x0002be70, 0x0002bf40, 0x0002c6e0, 0x0002c9d0,
    0x0002cd80, 0x0002dac0, 0x00035d90,
};

typedef void (__fastcall *MethodFn)(void *, int);

#define Orig_Construct ((void *(__fastcall *)(void *, int))0x0002bd40)
#define Orig_Destruct ((MethodFn)0x0002be70)
#define Orig_Delete ((void *(__fastcall *)(void *, int, unsigned))0x00035d90)
#define Orig_Reset ((MethodFn)0x00029d60)
#define Orig_SetNavigateMode ((void (__fastcall *)(void *, int, int, void *))0x0002a0d0)
#define Orig_SetTargetMode ((void (__fastcall *)(void *, int, int, const Coord3 *))0x0002a2c0)
#define Orig_SetAttackMode ((void (__fastcall *)(void *, int, int))0x0002a330)
#define Orig_SetSecondaryTarget ((void (__fastcall *)(void *, int, void *))0x0002a380)
#define Orig_GetTargetPos ((Coord3 *(__fastcall *)(void *, int))0x0002a3c0)
#define Orig_UpdateNavPos ((void (__fastcall *)(void *, int, Coord3 *, float))0x0002a3e0)
#define Orig_EngageAutoDrive ((MethodFn)0x0002a690)
#define Orig_DisengageAutoDrive ((MethodFn)0x0002a760)
#define Orig_DoPlayerLogic ((MethodFn)0x0002a7c0)
#define Orig_EngageSplinePath ((void (__fastcall *)(void *, int, void *))0x0002a800)
#define Orig_DisengageSplinePath ((MethodFn)0x0002a850)
#define Orig_DoSplinePathUpdate ((MethodFn)0x0002a8b0)
#define Orig_DrivableVec ((bool (__fastcall *)(void *, int, const Coord3 *, const Coord3 *))0x0002a9e0)
#define Orig_BarriersInPath ((bool (__fastcall *)(void *, int, bool))0x0002aaf0)
#define Orig_UpdateStuck ((MethodFn)0x0002af50)
#define Orig_GetHeliCollNav ((WRoadNav *(__fastcall *)(void *, int, const Coord3 *))0x0002b0c0)
#define Orig_GetCollNav ((WRoadNav *(__fastcall *)(void *, int, const Coord3 *, bool, int8_t))0x0002b210)
#define Orig_TestForCollision ((bool (*)(PhysicsObject *, const Coord3 *, const Coord3 *, float))0x0002b4d0)
#define Orig_GetBestWeightedLane ((int (__fastcall *)(void *, int, int))0x0002b5e0)
#define Orig_GetRoadNetworkLaneWeights ((MethodFn)0x0002b660)
#define Orig_GetDirectLaneWeights ((void (__fastcall *)(void *, int, const Coord3 *))0x0002b7a0)
#define Orig_DoAttackMode ((MethodFn)0x0002bf40)
#define Orig_TurnTurret ((MethodFn)0x0002c6e0)
#define Orig_DoAutoDriveUpdate ((MethodFn)0x0002c9d0)
#define Orig_DoAgentStateUpdate ((MethodFn)0x0002cd80)
#define Orig_UnSpawn ((MethodFn)0x0002dac0)

// ---- the game's state

#define ShadowSim ((uint8_t *)0x00233ff0)
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowStepCount I32_AT(0x00234e34)
#define ShadowStepsPerSecond I32_AT(0x00234e2c)
#define ShadowCars (*(GameVector<PVehicle *> *)0x00234e3c)
#define ShadowSpawnCount I16_AT(0x001de918)
#define ShadowZones ((uint8_t *)0x001e1f20)
constexpr uint32_t kZoneTableBytes = 30 * 0x1c;

// Our statics and the game's copies the original uses
struct Mirror {
    void *ours;
    uint32_t game;
    uint32_t size;
    const char *name;
};

const Mirror kMirrors[] = {
    {&NextHeightPhase, 0x001ddf00, 4, "NextHeightPhase"},
    {&NextParkingOffset, 0x001ddf04, 4, "NextParkingOffset"},
};
constexpr int kMirrorCount = int(sizeof(kMirrors) / sizeof(kMirrors[0]));
uint8_t g_gameCopies[kMirrorCount][4];

void MirrorIn() {
    for (const Mirror &m : kMirrors)
        memcpy((void *)uintptr_t(m.game), m.ours, m.size);
}

void MirrorOut() {
    for (const Mirror &m : kMirrors)
        memcpy(m.ours, (const void *)uintptr_t(m.game), m.size);
}

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults, g_skipped;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameaishadowa]   %s #%ld: %s\n", what, index, detail);
}

void CompareBytes(const char *what, long index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[112];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Report(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

// ---- random inputs (a generator of our own)

uint32_t g_seed = 0x0a1d0a0a;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

int Range(int lo, int hi) {
    return lo + int(Next() % uint32_t(hi - lo + 1));
}

bool Chance(int percent) {
    return Range(0, 99) < percent;
}

// A float near `value`: the value itself, a step either side, or a little off
float Near(float value) {
    switch (Range(0, 3)) {
    case 0:
        return value;
    case 1:
        return nextafterf(value, 1e30f);
    case 2:
        return nextafterf(value, -1e30f);
    default:
        return value * Uniform(0.98f, 1.02f);
    }
}

Coord3 Around(const Coord3 &centre, float spread) {
    Coord3 v = {centre.x + Uniform(-spread, spread), centre.y + Uniform(-spread * 0.2f, spread * 0.2f),
                centre.z + Uniform(-spread, spread)};
    return v;
}

Coord3 UnitVector() {
    for (;;) {
        Coord3 v = {Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f)};
        float length = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
        if (length > 0.1f) {
            v.x /= length;
            v.y /= length;
            v.z /= length;
            return v;
        }
    }
}

void RandomBytes(void *at, size_t size) {
    uint8_t *p = static_cast<uint8_t *>(at);
    for (size_t i = 0; i < size; i++)
        p[i] = uint8_t(Next());
}

// ---- the regions a case may change

struct Region {
    uint8_t *at;
    uint32_t size;
    bool compare;
    const char *name;
};

std::vector<Region> g_regions;

void AddRegion(const void *at, uint32_t size, const char *name, bool compare = true) {
    if (at != NULL)
        g_regions.push_back({ (uint8_t *)at, size, compare, name });
}

struct Snapshot {
    std::vector<uint8_t> bytes;
};

void Capture(Snapshot *s) {
    s->bytes.clear();
    for (const Region &r : g_regions)
        s->bytes.insert(s->bytes.end(), r.at, r.at + r.size);
}

void Restore(const Snapshot &s) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        memcpy(r.at, &s.bytes[at], r.size);
        at += r.size;
    }
}

void CompareResults(const char *what, long index, const Snapshot &original, const Snapshot &port) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        if (r.compare) {
            char label[112];
            snprintf(label, sizeof(label), "%s: %s", what, r.name);
            CompareBytes(label, index, &original.bytes[at], &port.bytes[at], r.size);
        }
        at += r.size;
    }
}

// ---- the call logs

std::vector<uint32_t> g_logs[2];

void Log(uint32_t word) {
    g_logs[g_original ? 0 : 1].push_back(word);
}

void LogFloat(float value) {
    uint32_t word;
    memcpy(&word, &value, 4);
    Log(word);
}

void LogBytes(const void *bytes, uint32_t size) {
    if (bytes == NULL) {
        Log(0xdeadbeef);
        return;
    }
    const uint8_t *p = static_cast<const uint8_t *>(bytes);
    for (uint32_t i = 0; i < size; i += 4) {
        uint32_t word = 0;
        memcpy(&word, p + i, size - i < 4 ? size - i : 4);
        Log(word);
    }
}

void LogPointer(const void *p) {
    Log(uint32_t(uintptr_t(p)));
}

// Strings by content: the port's literals are not the game's
void LogString(const char *s) {
    if (s == NULL) {
        Log(0);
        return;
    }
    uint32_t hash = 2166136261u;
    for (; *s != 0; s++)
        hash = (hash ^ uint8_t(*s)) * 16777619u;
    Log(hash);
}

void CompareLogs(const char *what, long index) {
    g_checks++;
    const std::vector<uint32_t> &a = g_logs[0], &b = g_logs[1];
    if (a == b)
        return;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        at++;
    char detail[128];
    snprintf(detail, sizeof(detail), "call log word %u of %u/%u: original %08x, port %08x", unsigned(at),
             unsigned(a.size()), unsigned(b.size()), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
    Report(what, index, detail);
}

// ---- the synthetic world

constexpr int kNavs = 5;                    // driveToNav, collNav, laneCollNav, heliCollNav, targetNav
constexpr int16_t kBodySlots[2] = {0x7a10, 0x7a11};

alignas(16) uint8_t g_arena[0x1000];        // the allocators' fakes hand out blocks from here
uint32_t g_arenaUsed;

alignas(16) uint8_t g_vehicleBlock[0x10 + sizeof(AIGroundVehicle)];    // a new[] cookie, then the vehicle
alignas(16) uint8_t g_targetBlock[sizeof(AIGroundVehicle)];
alignas(16) uint8_t g_cars[2][0x420];
alignas(16) uint8_t g_bodies[2][sizeof(RigidBody)];
alignas(16) uint8_t g_infos[2][sizeof(RigidBodyInfo)];
alignas(16) uint8_t g_navs[2][kNavs][sizeof(WRoadNav)];
alignas(16) uint8_t g_soundBlock[sizeof(ATrafficVehicle)];
alignas(16) uint8_t g_sceneBlock[0x80];
alignas(16) uint8_t g_handleBlock[0x40];
alignas(16) uint8_t g_element[0x60];
alignas(16) uint8_t g_otherVehicle[sizeof(AIVehicle)];
alignas(16) uint8_t g_splinePath[sizeof(AISplinePath)];
alignas(16) uint8_t g_pathHandle[sizeof(RPathHandle)];
alignas(16) uint8_t g_pathInstance[sizeof(CARP::Instance)];
alignas(16) uint8_t g_spline[sizeof(CARP::AISpline)];
alignas(16) uint8_t g_navTemplate[sizeof(WRoadNav)];
alignas(16) uint8_t g_bodyTemplate[sizeof(RigidBody)];
alignas(16) uint8_t g_infoTemplate[sizeof(RigidBodyInfo)];
alignas(16) AttributeVector g_turretRange;
float g_hitPoints[2];

AIGroundVehicle *const g_vehicle = reinterpret_cast<AIGroundVehicle *>(g_vehicleBlock + 0x10);
AIGroundVehicle *const g_target = reinterpret_cast<AIGroundVehicle *>(g_targetBlock);
PVehicle *g_playerCar;
Coord3 g_player;

void *g_carVtable[PVehicle::kSlotCount];

// One case's inputs and scripted answers
struct Inputs {
    Coord3 navOffset;               // InitAtPoint puts the navigator's road point this far from the point
    int16_t navSegment;             // ... and on this segment
    int hits[4];                    // CheckHitWorld's answers, in turn
    int hit;
    bool normals[2];                // GetWorldNormal's
    Coord3 normalPoints[2];
    int normal;
    uint32_t zones;                 // InZone's, by zone (bit)
    bool forward, sameRoad;         // PathForwardRoadSegment's
    uint8_t heli, siren;            // the IS_HELI and HAS_SIREN lookups
    bool rangeFound;                // TURRET_RANGE's
    int carClass[2];
    float gas, brake;
    int inShock;
    uint8_t emp;
    int wheels;
    float glue;
    float segmentSpeed;
    double leftDrivable;
    uint8_t weights[64];            // the road network's lane weights
    Coord4 tangent;
    float f[4];
    int i[4];
    bool b[4];
    Coord3 points[2];
} g_in;

// The answers (compared)
struct Outputs {
    const void *p;
    int i;
    bool b;
    uint8_t pad[3];
    Coord3 point;
} g_out;

// ---- the fakes

enum : uint32_t {
    kTagBody = 0x424f4459, kTagNavInitPoint = 0x4e495054, kTagNavReverse = 0x4e525645, kTagNavInc = 0x4e494e43,
    kTagNavLanes = 0x4e4c4e53, kTagNavForward = 0x4e465744, kTagNavReset = 0x4e525354, kTagNavConstruct = 0x4e434f4e,
    kTagNavDestruct = 0x4e444553, kTagTangent = 0x54414e47, kTagLaneWeights = 0x4c4e5747,
    kTagSegmentSpeed = 0x53505044, kTagDrivable = 0x44524956, kTagHitWorld = 0x48495457, kTagNormal = 0x4e524d4c,
    kTagCollider = 0x434c4452, kTagColliderEnd = 0x43454e44, kTagElement = 0x454c454d, kTagInZone = 0x5a4f4e45,
    kTagEvent = 0x45564e54, kTagGlue = 0x474c5545, kTagSoundReset = 0x53525354, kTagSplinePath = 0x53504c4e,
    kTagResetObject = 0x524f424a, kTagSetFrame = 0x46524d45, kTagLookupBool = 0x4c4b424c,
    kTagLookupVector = 0x4c4b5643, kTagAlloc = 0x414c4c43, kTagFree = 0x46524545, kTagDelete = 0x44454c20,
    kTagVecDtor = 0x56445452, kTagCarSlot = 0x43530000,
};

int CarIndex(const void *car) {
    return car == g_cars[1] ? 1 : 0;
}

// The Simulation's body getter: our copies for the synthetic cars' slots, the Simulation's tables for the rest (as the
// original reads them)
RigidBody *__fastcall FakeGetRigidBody(uint8_t *simulation, int, int slot) {
    for (int i = 0; i < 2; i++)
        if (slot == kBodySlots[i])
            return reinterpret_cast<RigidBody *>(g_bodies[i]);
    Log(kTagBody);
    Log(uint32_t(slot));
    uint8_t *table = *reinterpret_cast<uint8_t **>(simulation + (simulation[4] != 0 ? 0x28 : 0x1c));
    return reinterpret_cast<RigidBody *>(table + (slot << 7));
}

// The navigators
void __fastcall FakeNavInitAtPoint(WRoadNav *nav, int, const Coord3 *point, const Coord3 *heading, bool noLane) {
    Log(kTagNavInitPoint);
    LogPointer(nav);
    LogBytes(point, 12);
    LogBytes(heading, 12);
    Log(noLane);
    Coord3 at = *point;
    nav->roadPoint.x = at.x + g_in.navOffset.x;
    nav->roadPoint.y = at.y + g_in.navOffset.y;
    nav->roadPoint.z = at.z + g_in.navOffset.z;
    nav->position = nav->roadPoint;
    nav->segment = g_in.navSegment;
}

void __fastcall FakeNavReverse(WRoadNav *nav, int) {
    Log(kTagNavReverse);
    LogPointer(nav);
    nav->direction.x = -nav->direction.x;
    nav->direction.y = -nav->direction.y;
    nav->direction.z = -nav->direction.z;
}

void __fastcall FakeNavInc(WRoadNav *nav, int, float step, Coord3 *heading, short target) {
    Log(kTagNavInc);
    LogPointer(nav);
    LogFloat(step);
    LogBytes(heading, 12);
    Log(uint32_t(uint16_t(target)));
    nav->roadPoint.x += step;
    nav->position.z += step;
}

void __fastcall FakeNavLanes(WRoadNav *nav, int, float offset, float distance) {
    Log(kTagNavLanes);
    LogPointer(nav);
    LogFloat(offset);
    LogFloat(distance);
}

bool __fastcall FakeNavForward(WRoadNav *nav, int, short other, bool *sameRoad) {
    Log(kTagNavForward);
    LogPointer(nav);
    Log(uint32_t(uint16_t(other)));
    Log(*sameRoad);
    *sameRoad = g_in.sameRoad;
    return g_in.forward;
}

void __fastcall FakeNavReset(WRoadNav *nav, int) {
    Log(kTagNavReset);
    LogPointer(nav);
}

WRoadNav *__fastcall FakeNavConstruct(WRoadNav *nav, int) {
    Log(kTagNavConstruct);
    LogPointer(nav);
    return nav;
}

void __fastcall FakeNavDestruct(WRoadNav *nav, int) {
    Log(kTagNavDestruct);
    LogPointer(nav);
}

void __fastcall FakeTangent(void *spline, int, float t, Coord4 *tangent) {
    Log(kTagTangent);
    LogPointer(spline);
    LogFloat(t);
    *tangent = g_in.tangent;
}

void __fastcall FakeLaneWeights(void *network, int, int segment, int8_t end, int8_t *weights, int size, int count) {
    Log(kTagLaneWeights);
    Log(uint32_t(segment));
    Log(uint8_t(end));
    LogPointer(weights);
    Log(uint32_t(size));
    Log(uint32_t(count));
    memcpy(weights, g_in.weights, size_t(size));
}

float __fastcall FakeSegmentSpeed(WRoadNav *nav, int) {
    Log(kTagSegmentSpeed);
    LogPointer(nav);
    return g_in.segmentSpeed;
}

double __fastcall FakeLeftDrivable(WRoadNav *nav, int) {
    Log(kTagDrivable);
    LogPointer(nav);
    return g_in.leftDrivable;
}

// The world
int __fastcall FakeHitWorld(void *manager, int, const Coord4 *segment, void *info) {
    Log(kTagHitWorld);
    LogPointer(manager);
    LogBytes(segment, 32);
    return g_in.hits[g_in.hit++ & 3];
}

bool __fastcall FakeNormal(void *collider, int, const Coord4 *segment, WorldCollisionInfo *info) {
    Log(kTagNormal);
    LogBytes(segment, 32);
    int which = g_in.normal++ & 1;
    *Xyz(&info->point) = g_in.normalPoints[which];
    return g_in.normals[which];
}

void *__fastcall FakeColliderConstruct(void *collider, int, const Coord3 *position, float radius, bool flat,
                                       uint32_t mask) {
    Log(kTagCollider);
    LogBytes(position, 12);
    LogFloat(radius);
    Log(flat);
    Log(mask);
    return collider;
}

void __fastcall FakeColliderDestruct(void *collider, int) {
    Log(kTagColliderEnd);
}

// The AI elements and zones, the mission manager
void *__fastcall FakeElement(void *controller, int, void *element) {
    Log(kTagElement);
    LogPointer(controller);
    LogPointer(element);
    return g_element;
}

bool __fastcall FakeInZone(void *zones, int, int zone, const Coord3 *point, float scale) {
    Log(kTagInZone);
    Log(uint32_t(zone));
    LogBytes(point, 12);
    LogFloat(scale);
    return zone < 32 && (g_in.zones >> zone & 1) != 0;
}

void __fastcall FakeEvent(void *manager, int, int event, const char *text) {
    Log(kTagEvent);
    LogPointer(manager);
    Log(uint32_t(event));
    LogString(text);
}

float __fastcall FakeGlue(void *manager, int) {
    Log(kTagGlue);
    LogPointer(manager);
    return g_in.glue;
}

// The sound, the spline path, the body, the animation
void __fastcall FakeSoundReset(void *sound, int) {
    Log(kTagSoundReset);
    LogPointer(sound);
}

void __fastcall FakeSetSplinePath(void *vehicle, int, void *spline) {
    Log(kTagSplinePath);
    LogPointer(vehicle);
    LogPointer(spline);
}

void __fastcall FakeResetObject(RigidBody *body, int, const MATRIX4 *orientation, const Coord3 *position) {
    Log(kTagResetObject);
    LogPointer(body);
    LogBytes(orientation, sizeof(MATRIX4));
    LogBytes(position, 12);
}

void __fastcall FakeSetFrame(void *handle, int, uint32_t id, uint8_t stimulus, uint32_t time) {
    Log(kTagSetFrame);
    LogPointer(handle);
    Log(id);
    Log(stimulus);
    Log(time);
}

// The attributes
uint8_t __fastcall FakeLookupBool(AttributeSet *set, int, const char *key, bool *found) {
    Log(kTagLookupBool);
    LogPointer(set);
    LogString(key);
    if (found != NULL)
        *found = true;
    return strcmp(key, "IS_HELI") == 0 ? g_in.heli : g_in.siren;
}

const AttributeVector *__fastcall FakeLookupVector(AttributeSet *set, int, const char *key, bool *found) {
    Log(kTagLookupVector);
    LogPointer(set);
    LogString(key);
    if (found != NULL)
        *found = g_in.rangeFound;
    return &g_turretRange;
}

// The allocators: a bump arena, the same blocks for both sides
void *ArenaTake(uint32_t size) {
    uint32_t at = (g_arenaUsed + 15) & ~15u;
    if (at + size > sizeof(g_arena))
        return NULL;
    g_arenaUsed = at + size;
    return g_arena + at;
}

void *FakeAlloc(unsigned size, const char *name) {
    Log(kTagAlloc);
    Log(size);
    LogString(name);
    return ArenaTake(size);
}

void FakeFree(void *block, unsigned size) {
    Log(kTagFree);
    LogPointer(block);
    Log(size);
}

void FakeDelete(void *block) {
    Log(kTagDelete);
    LogPointer(block);
}

void __stdcall FakeVecDtor(void *array, uint32_t size, int count, void *destructor) {
    Log(kTagVecDtor);
    LogPointer(array);
    Log(size);
    Log(uint32_t(count));
    LogPointer(destructor);
}

// The cars' vtable
ABaseSound *__fastcall FakeCarAudio(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetAudio);
    LogPointer(car);
    return reinterpret_cast<ABaseSound *>(g_soundBlock);
}

void __fastcall FakeCarSetClass(void *car, int, int carClass) {
    Log(kTagCarSlot + PVehicle::kSetCarClass);
    LogPointer(car);
    Log(uint32_t(carClass));
}

int __fastcall FakeCarGetClass(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetCarClass);
    LogPointer(car);
    return g_in.carClass[CarIndex(car)];
}

void __fastcall FakeCarControls(void *car, int) {
    Log(kTagCarSlot + PVehicle::kInitializeCarControls);
    LogPointer(car);
}

BondCarControl *__fastcall FakeCarControl(void *car, int, BondCarControl *result) {
    Log(kTagCarSlot + PVehicle::kGetCarControl);
    LogPointer(car);
    memset(result, 0, sizeof(*result));
    result->gas = g_in.gas;
    result->brake = g_in.brake;
    return result;
}

uint8_t __fastcall FakeCarEmp(void *car, int) {
    Log(kTagCarSlot + PVehicle::kEmpActive);
    LogPointer(car);
    return g_in.emp;
}

void __fastcall FakeCarResetAt(void *car, int, Coord3 *position, const Coord3 *direction) {
    Log(kTagCarSlot + PVehicle::kResetCarAt);
    LogPointer(car);
    LogBytes(position, 12);
    LogBytes(direction, 12);
}

int __fastcall FakeCarInShock(void *car, int) {
    Log(kTagCarSlot + PVehicle::kInShock);
    LogPointer(car);
    return g_in.inShock;
}

int __fastcall FakeCarWheels(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetNumWheelsOnGround);
    LogPointer(car);
    return g_in.wheels;
}

void __fastcall FakeCarOther(void *car, int) {
    Log(kTagCarSlot + 0xff);
    LogPointer(car);
}

// ---- hooks: a jump over the first five bytes of our code (where the entry jumps to it) or of the original

struct HookSpec {
    uint32_t entry;
    const void *to;
};

const HookSpec kHooks[] = {
    {0x000b2700, (const void *)&FakeGetRigidBody}, {0x000cc100, (const void *)&FakeNavInitAtPoint},
    {0x000c95a0, (const void *)&FakeNavReverse}, {0x000cb4b0, (const void *)&FakeNavInc},
    {0x000cbcf0, (const void *)&FakeNavLanes}, {0x000c9810, (const void *)&FakeNavForward},
    {0x000c9130, (const void *)&FakeNavReset}, {0x000ca090, (const void *)&FakeNavConstruct},
    {0x000ca100, (const void *)&FakeNavDestruct}, {0x0007a5c0, (const void *)&FakeTangent},
    {0x000c9e00, (const void *)&FakeLaneWeights}, {0x00029cd0, (const void *)&FakeSegmentSpeed},
    {0x00029cf0, (const void *)&FakeLeftDrivable}, {0x000c3b40, (const void *)&FakeHitWorld},
    {0x000bd890, (const void *)&FakeNormal}, {0x000be540, (const void *)&FakeColliderConstruct},
    {0x000be180, (const void *)&FakeColliderDestruct}, {0x000286d0, (const void *)&FakeElement},
    {0x00036b20, (const void *)&FakeInZone}, {0x000b72f0, (const void *)&FakeEvent},
    {0x000b7290, (const void *)&FakeGlue}, {0x0011e230, (const void *)&FakeSoundReset},
    {0x00035860, (const void *)&FakeSetSplinePath}, {0x000adcd0, (const void *)&FakeResetObject},
    {0x00077c50, (const void *)&FakeSetFrame}, {0x000580e0, (const void *)&FakeLookupBool},
    {0x00058360, (const void *)&FakeLookupVector}, {0x00114750, (const void *)&FakeAlloc},
    {0x001147d0, (const void *)&FakeFree}, {0x001146e0, (const void *)&FakeDelete},
    {0x0013332e, (const void *)&FakeVecDtor},
};
constexpr int kHookCount = int(sizeof(kHooks) / sizeof(kHooks[0]));

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

Hook g_hooks[kHookCount];

uint32_t JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return 0;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return at + 5 + uint32_t(relative);
}

bool HookInstall(Hook *hook, uint32_t entry, const void *to) {
    hook->on = false;
    uint32_t at = JumpTarget(entry);
    if (at == 0)
        at = entry;
    hook->at = at;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(hook->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t relative = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &relative, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    hook->on = true;
    return true;
}

void HookRemove(Hook *hook) {
    if (!hook->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(hook->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(hook->at), hook->saved, 5);
    VirtualProtect((void *)uintptr_t(hook->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(hook->at), 5);
    hook->on = false;
}

bool InstallHooks() {
    int missing = 0;
    for (int i = 0; i < kHookCount; i++)
        if (!HookInstall(&g_hooks[i], kHooks[i].entry, kHooks[i].to)) {
            printf("[gameaishadowa] the fake at 0x%08x could not be installed\n", kHooks[i].entry);
            missing++;
        }
    return missing == 0;
}

void RemoveHooks() {
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
}

void BuildVtable() {
    for (void *&slot : g_carVtable)
        slot = (void *)&FakeCarOther;
    g_carVtable[PVehicle::kGetAudio] = (void *)&FakeCarAudio;
    g_carVtable[PVehicle::kSetCarClass] = (void *)&FakeCarSetClass;
    g_carVtable[PVehicle::kGetCarClass] = (void *)&FakeCarGetClass;
    g_carVtable[PVehicle::kInitializeCarControls] = (void *)&FakeCarControls;
    g_carVtable[PVehicle::kGetCarControl] = (void *)&FakeCarControl;
    g_carVtable[PVehicle::kEmpActive] = (void *)&FakeCarEmp;
    g_carVtable[PVehicle::kResetCarAt] = (void *)&FakeCarResetAt;
    g_carVtable[PVehicle::kInShock] = (void *)&FakeCarInShock;
    g_carVtable[PVehicle::kGetNumWheelsOnGround] = (void *)&FakeCarWheels;
}

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    g_arenaUsed = 0;
    g_in.hit = 0;
    g_in.normal = 0;
    if (original) {
        MirrorIn();
        Window(true);
    }
#ifdef _MSC_VER
    __try {
        run();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
    }
#else
    run();
#endif
    if (original) {
        Window(false);
        MirrorOut();
    }
    ResetFpu();
}

// The inputs are in place: both sides from them, the results compared, everything put back
void RunCase(const char *what, long index, SideFn run, const Snapshot &before) {
    Snapshot inputs, original, port;
    memset(g_arena, 0, sizeof(g_arena));
    Capture(&inputs);
    g_logs[0].clear();
    g_logs[1].clear();
    RunSide(run, true);
    Capture(&original);
    Restore(inputs);
    RunSide(run, false);
    Capture(&port);
    g_cases++;
    CompareResults(what, index, original, port);
    CompareLogs(what, index);
    Restore(before);
}

// ---- the inputs

int RandomSegment() {
    if (!fgRoadNetworkData.loaded || fgRoadNetworkData.segmentCount <= 0)
        return 0;
    return Range(0, fgRoadNetworkData.segmentCount - 1);
}

void RandomInputs() {
    g_in.navOffset = {Uniform(-8.0f, 8.0f), Uniform(-2.0f, 2.0f), Uniform(-8.0f, 8.0f)};
    g_in.navSegment = int16_t(Chance(10) ? -1 : RandomSegment());
    static const int kHits[] = {-1, 0, 1, 2, 3, 0, 1};
    for (int &hit : g_in.hits)
        hit = kHits[Range(0, 6)];
    for (int i = 0; i < 2; i++) {
        g_in.normals[i] = Chance(50);
        g_in.normalPoints[i] = Around(g_player, 40.0f);
    }
    g_in.zones = Next() | Next() << 24;
    g_in.forward = Chance(50);
    g_in.sameRoad = Chance(50);
    g_in.heli = uint8_t(Chance(30));
    g_in.siren = uint8_t(Chance(50));
    g_in.rangeFound = Chance(50);
    for (int &carClass : g_in.carClass)
        carClass = Range(0, 4);
    g_in.gas = Chance(20) ? Near(0.5f) : Uniform(-1.0f, 1.0f);
    g_in.brake = Chance(20) ? Near(0.5f) : Uniform(-1.0f, 1.0f);
    g_in.inShock = Chance(20) ? 1 : 0;
    g_in.emp = uint8_t(Chance(10));
    g_in.wheels = Chance(30) ? 0 : Range(1, 4);
    g_in.glue = Chance(20) ? 1.0f : Uniform(0.2f, 3.0f);
    g_in.segmentSpeed = Uniform(0.0f, 60.0f);
    g_in.leftDrivable = Uniform(0.0f, 20.0f);
    for (uint8_t &weight : g_in.weights)
        weight = uint8_t(Chance(15) ? 0xff : Chance(30) ? 0 : Range(0, 254));
    g_in.tangent = {Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), 1.0f};
    for (float &f : g_in.f)
        f = Uniform(-50.0f, 150.0f);
    for (int &i : g_in.i)
        i = Range(-3, 20);
    for (bool &b : g_in.b)
        b = Chance(50);
    for (Coord3 &point : g_in.points)
        point = Around(g_player, Chance(30) ? 400.0f : 120.0f);
    g_turretRange = {Uniform(-30.0f, 200.0f), Uniform(100.0f, 370.0f), Uniform(150.0f, 300.0f),
                     Uniform(250.0f, 370.0f)};
}

void MakeNav(WRoadNav *nav) {
    memcpy(nav, g_navTemplate, sizeof(WRoadNav));
    nav->mode = Range(0, 5);
    nav->unknown04 = uint8_t(Range(0, 3));
    nav->position = Around(g_player, 60.0f);
    nav->roadPoint = Around(g_player, 60.0f);
    nav->direction = UnitVector();
    nav->forward = uint8_t(Range(0, 1));
    nav->segment = int16_t(RandomSegment());
    nav->t = Uniform(0.0f, 1.0f);
    nav->laneOffset = Chance(20) ? Near(0.5f) : Uniform(-30.0f, 30.0f);
    nav->unknownB1 = uint8_t(Chance(25));
    nav->unknownB2 = uint8_t(Chance(25));
    nav->spline = reinterpret_cast<RCameraSpline *>(g_spline);
}

void MakeBody(int i) {
    RigidBody *body = reinterpret_cast<RigidBody *>(g_bodies[i]);
    RigidBodyInfo *info = reinterpret_cast<RigidBodyInfo *>(g_infos[i]);
    memcpy(body, g_bodyTemplate, sizeof(RigidBody));
    memcpy(info, g_infoTemplate, sizeof(RigidBodyInfo));
    body->info = info;
    body->position = Around(g_player, i == 0 ? 20.0f : 150.0f);
    body->velocity = {Uniform(-40.0f, 40.0f), Uniform(-3.0f, 3.0f), Uniform(-40.0f, 40.0f)};
    if (Chance(15))
        body->velocity = {Uniform(-3.0f, 3.0f), 0.0f, Uniform(-3.0f, 3.0f)};
    body->mass = Uniform(500.0f, 3000.0f);
    body->sleepState = uint8_t(Range(1, 2));
    Coord3 forward = UnitVector();
    Coord3 up = {0.0f, 1.0f, 0.0f};
    Coord3 side = {up.y * forward.z - up.z * forward.y, up.z * forward.x - up.x * forward.z,
                   up.x * forward.y - up.y * forward.x};
    float length = sqrtf(side.x * side.x + side.y * side.y + side.z * side.z);
    if (length > 0.01f) {
        side.x /= length;
        side.y /= length;
        side.z /= length;
    }
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            info->orientation.mtx[r][c] = r == c ? 1.0f : 0.0f;
    info->orientation.mtx[0][0] = side.x;
    info->orientation.mtx[0][1] = side.y;
    info->orientation.mtx[0][2] = side.z;
    info->orientation.mtx[2][0] = forward.x;
    info->orientation.mtx[2][1] = forward.y;
    info->orientation.mtx[2][2] = forward.z;
    info->halfExtents = {Uniform(0.8f, 2.0f), Uniform(0.5f, 1.5f), Uniform(1.5f, 6.0f), 0.0f};
    info->unknown4fc = uint8_t(Range(0, 1));
}

void MakeCar(int i) {
    RandomBytes(g_cars[i], sizeof(g_cars[i]));
    PhysicsObject *car = reinterpret_cast<PhysicsObject *>(g_cars[i]);
    car->vtable = g_carVtable;
    car->flags = 0;
    car->rigidBodySlot = kBodySlots[i];
    car->renderObject = reinterpret_cast<RSceneObj *>(g_sceneBlock);    // TurnTurret reads it unchecked
    car->hitPointLoc = Chance(85) ? &g_hitPoints[i] : NULL;
    g_hitPoints[i] = Chance(25) ? Near(Chance(50) ? 0.0f : 0.5f) : Uniform(-10.0f, 100.0f);
    MakeBody(i);
}

void MakeVehicle(AIGroundVehicle *vehicle, int car, uint8_t (*navs)[sizeof(WRoadNav)]) {
    RandomBytes(vehicle, sizeof(AIGroundVehicle));
    vehicle->vtable = reinterpret_cast<void **>(kAIGroundVehicleVtable);
    vehicle->vehicleType = kAIGroundVehicle;
    vehicle->physicsObject = reinterpret_cast<PhysicsObject *>(g_cars[car]);
    vehicle->splinePath = Chance(50) ? reinterpret_cast<AISplinePath *>(g_splinePath) : NULL;
    for (int n = 0; n < kNavs; n++)
        MakeNav(reinterpret_cast<WRoadNav *>(navs[n]));
    vehicle->driveToNav = reinterpret_cast<WRoadNav *>(navs[0]);
    vehicle->collNav = reinterpret_cast<WRoadNav *>(navs[1]);
    vehicle->laneCollNav = reinterpret_cast<WRoadNav *>(navs[2]);
    vehicle->heliCollNav = reinterpret_cast<WRoadNav *>(navs[3]);
    vehicle->targetNav = reinterpret_cast<WRoadNav *>(navs[4]);
    vehicle->active = Chance(80);
    vehicle->type = Range(0, 6);
    vehicle->navigateModeStep = Chance(60) ? 0 : ShadowStepCount + Range(-5, 5);
    static const int kNavigateModes[] = {0, 1, 2, 3, 3, 4, 5, 6};
    vehicle->navigateMode = kNavigateModes[Range(0, 7)];
    vehicle->targetMode = Range(0, 2);
    static const int kAttackModes[] = {0, 1, 9, 10, 12, 13, 13, 14, 14, 15, 15, 16};
    vehicle->attackMode = kAttackModes[Range(0, 11)];
    vehicle->targetSpeed = Uniform(0.0f, 60.0f);
    vehicle->road = int16_t(Chance(30) ? -1 : Range(-2, fgRoadNetworkData.roadCount + 1));
    vehicle->skipFullUpdate = Chance(75) ? 0 : 1;
    vehicle->laneSpan = uint8_t(Chance(60) ? Range(3, 4) : Range(0, 9));
    vehicle->hitByCollision = uint8_t(Chance(60));
    vehicle->collisionKind = uint8_t(Chance(60) ? 6 : Range(0, 8));
    int step = ShadowStepCount;
    vehicle->collNavStep = Chance(20) ? 0 : step - Range(10, 20);
    vehicle->laneCollNavStep = Chance(20) ? 0 : step - Range(10, 20);
    vehicle->heliCollNavStep = Chance(20) ? 0 : step - Range(10, 20);
    vehicle->isSub = uint8_t(Chance(20));
    vehicle->followTargetHeight = uint8_t(Chance(50));
    vehicle->useLanes = uint8_t(Range(0, 1));
    vehicle->unknown145 = uint8_t(Chance(60));
    vehicle->autoDriveSpeed = Chance(20) ? 1.0f : Uniform(-0.5f, 2.0f);
    vehicle->maxSpeed = Uniform(0.0f, 80.0f);
    vehicle->speedScale = Uniform(0.0f, 2.0f);
    vehicle->chargeRollStep = Chance(50) ? Range(0, 1) : Range(2, 300);
    vehicle->chargeSteps = Chance(50) ? 0 : Range(1, 100);
    vehicle->chargeChance = Uniform(0.0f, 1.0f);
    vehicle->unknown140 = Uniform(0.0f, 3.0f);
    vehicle->airSteps = Chance(50) ? Range(0, 2) : ShadowStepsPerSecond + Range(-2, 2);
    vehicle->zoneStopSteps = int16_t(Chance(60) ? 0 : Range(-1, 60));
    vehicle->stopped = uint8_t(Chance(10));
    vehicle->pathCheckStep = step - Range(0, 30);
    vehicle->minSimUpdate = uint8_t(Chance(40));
    vehicle->stopping = uint8_t(Chance(30));
    vehicle->target = NULL;
    vehicle->targetPosition = Around(g_player, 150.0f);
    vehicle->goal = Around(g_player, 150.0f);
    vehicle->goalDistance = Chance(20) ? Uniform(0.0f, 5.0f) : Uniform(0.0f, 250.0f);
    vehicle->goalDirection = UnitVector();
    vehicle->targetSegment = int16_t(Range(-1, 50));
    vehicle->secondaryTarget = NULL;
    vehicle->goalHidden = Chance(15) ? Range(2, 3) : Range(0, 1);
    vehicle->goalInSight = uint8_t(Range(0, 1));
    vehicle->navHeading = UnitVector();
    vehicle->navInSight = uint8_t(Range(0, 1));
    vehicle->attackRange = Chance(15) ? Near(10.0f) : Uniform(0.0f, 120.0f);
    vehicle->turretAngle = Chance(20) ? Near(Chance(50) ? 0.0f : 1.0f) : Uniform(0.0f, 1.0f);
    vehicle->stuckEventStep = int16_t(Range(-2, 3));
    vehicle->stuckSteps = int16_t(Chance(30) ? 0 : Range(1, 5 * ShadowStepsPerSecond + 2));
    vehicle->stuck = uint8_t(Chance(30));
    vehicle->stuckPosition = Around(reinterpret_cast<RigidBody *>(g_bodies[car])->position, Chance(50) ? 6.0f : 3.0f);
    vehicle->facingNav = uint8_t(Range(0, 1));
}

void MakeWorld() {
    RandomInputs();
    memset(&g_out, 0, sizeof(g_out));
    MakeCar(0);
    MakeCar(1);
    memset(g_soundBlock, 0, sizeof(g_soundBlock));
    memset(g_sceneBlock, 0, sizeof(g_sceneBlock));
    *reinterpret_cast<void **>(g_sceneBlock + 0x2c) = Chance(80) ? g_handleBlock : NULL;
    RandomBytes(g_pathInstance, sizeof(g_pathInstance));
    CARP::Instance *instance = reinterpret_cast<CARP::Instance *>(g_pathInstance);
    for (int i = 0; i < 3; i++) {
        instance->axisX[i] = Uniform(-1.0f, 1.0f);
        instance->axisY[i] = Uniform(-1.0f, 1.0f);
        instance->axisZ[i] = Uniform(-1.0f, 1.0f);
        instance->position[i] = Uniform(-500.0f, 500.0f);
    }
    memset(g_pathHandle, 0, sizeof(g_pathHandle));
    RPathHandle *handle = reinterpret_cast<RPathHandle *>(g_pathHandle);
    handle->instance = instance;
    handle->velocity = {Uniform(-20.0f, 20.0f), Uniform(-5.0f, 5.0f), Uniform(-20.0f, 20.0f)};
    RandomBytes(g_splinePath, sizeof(g_splinePath));
    AISplinePath *path = reinterpret_cast<AISplinePath *>(g_splinePath);
    path->path = handle;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            path->placement.mtx[r][c] = Uniform(-1.5f, 1.5f);
    RandomBytes(g_spline, sizeof(g_spline));

    MakeVehicle(g_target, 1, g_navs[1]);
    MakeVehicle(g_vehicle, 0, g_navs[0]);
    g_vehicle->target = Chance(75) ? g_target : NULL;
    g_vehicle->secondaryTarget = Chance(40) ? g_spline : NULL;

    RandomBytes(g_otherVehicle, sizeof(g_otherVehicle));
    reinterpret_cast<AIVehicle *>(g_otherVehicle)->vehicleType = kAIHelicopter;
    memset(g_element, 0, sizeof(g_element));
    *reinterpret_cast<int32_t *>(g_element + 0x48) = Chance(75) ? 1 : Range(0, 3);
    *reinterpret_cast<void **>(g_element + 0x50) = Chance(75) ? (void *)g_target : (void *)g_otherVehicle;

    for (int zone = 0; zone < 30; zone++)
        *reinterpret_cast<int32_t *>(ShadowZones + zone * 0x1c + 0x18) = Chance(15) ? 4 : Range(0, 3);
    ShadowSpawnCount = int16_t(Chance(20) ? 0 : Range(-2, 30));
    fgRoadNetworkData.unknownAD = uint8_t(Chance(85));
    NextHeightPhase = Chance(30) ? Near(1.0f) : Uniform(0.0f, 1.25f);
    NextParkingOffset = Chance(20) ? Near(Chance(50) ? 4950.0f : -50.0f) : Uniform(0.0f, 5000.0f);
}

void BuildRegions() {
    g_regions.clear();
    AddRegion(g_arena, sizeof(g_arena), "the arena");
    AddRegion(g_vehicleBlock, sizeof(g_vehicleBlock), "the vehicle");
    AddRegion(g_targetBlock, sizeof(g_targetBlock), "the target vehicle");
    AddRegion(g_cars, sizeof(g_cars), "the cars");
    AddRegion(g_bodies, sizeof(g_bodies), "the bodies");
    AddRegion(g_infos, sizeof(g_infos), "the bodies' infos");
    AddRegion(g_navs, sizeof(g_navs), "the navigators");
    AddRegion(g_soundBlock, sizeof(g_soundBlock), "the sound");
    AddRegion(g_sceneBlock, sizeof(g_sceneBlock), "the scene object");
    AddRegion(g_handleBlock, sizeof(g_handleBlock), "the animation handle");
    AddRegion(g_element, sizeof(g_element), "the AI element");
    AddRegion(g_otherVehicle, sizeof(g_otherVehicle), "the other vehicle");
    AddRegion(g_splinePath, sizeof(g_splinePath), "the spline path");
    AddRegion(g_pathHandle, sizeof(g_pathHandle), "the path handle");
    AddRegion(g_pathInstance, sizeof(g_pathInstance), "the path's instance");
    AddRegion(g_spline, sizeof(g_spline), "the spline");
    AddRegion(&g_turretRange, sizeof(g_turretRange), "the turret range");
    AddRegion(g_hitPoints, sizeof(g_hitPoints), "the hit points");
    for (const Mirror &m : kMirrors)
        AddRegion(m.ours, m.size, m.name);
    AddRegion(ShadowZones, kZoneTableBytes, "the AI zones");
    AddRegion(&ShadowSpawnCount, 2, "the traffic spawn count");
    AddRegion(ShadowRandom, sizeof(SimRandom), "the Simulation's random numbers");
    AddRegion(&ShadowStepCount, 4, "SimStepCount", false);
    AddRegion(&fgRoadNetworkData, sizeof(WRoadNetworkData), "the road network's statics");
    if (fgRoadNetworkData.loaded)
        for (int r = 0; r < fgRoadNetworkData.roadCount; r++)
            AddRegion(fgRoadNetworkData.roads[r], sizeof(WRoad), "a road");
    AddRegion(&g_out, sizeof(g_out), "the answers");
    AddRegion(&g_in, sizeof(g_in), "the inputs", false);
}

// ---- the cases: each runs the original or the port by g_original

void CaseConstruct() {
    g_vehicle->splinePath = NULL;
    g_out.p = g_original ? Orig_Construct(g_vehicle, 0) : g_vehicle->Construct();
}

void CaseDestruct() {
    g_vehicle->splinePath = NULL;
    if (g_original)
        Orig_Destruct(g_vehicle, 0);
    else
        g_vehicle->Destruct();
}

void CaseDelete() {
    g_vehicle->splinePath = NULL;
    *reinterpret_cast<int32_t *>(g_vehicleBlock) = Range(0, 3);
    unsigned flags = unsigned(g_in.i[0]) & 3;
    g_out.p = g_original ? Orig_Delete(g_vehicle, 0, flags) : g_vehicle->Delete(flags);
}

void CaseReset() {
    if (g_original)
        Orig_Reset(g_vehicle, 0);
    else
        g_vehicle->Reset();
}

void CaseSetNavigateMode() {
    int mode = g_in.i[0] % 7;
    void *spline = g_in.b[0] ? g_spline : NULL;
    if (g_original)
        Orig_SetNavigateMode(g_vehicle, 0, mode, spline);
    else
        g_vehicle->SetNavigateMode(mode, reinterpret_cast<CARP::AISpline *>(spline));
}

void CaseSetTargetMode() {
    int mode = g_in.i[0] % 3;
    if (g_original)
        Orig_SetTargetMode(g_vehicle, 0, mode, &g_in.points[0]);
    else
        g_vehicle->SetTargetMode(mode, &g_in.points[0]);
}

void CaseSetAttackMode() {
    int mode = g_in.b[0] ? 13 : g_in.i[0];
    if (g_original)
        Orig_SetAttackMode(g_vehicle, 0, mode);
    else
        g_vehicle->SetAttackMode(mode);
}

void CaseSetSecondaryTarget() {
    void *element = g_in.b[0] ? NULL : g_spline;
    if (g_original)
        Orig_SetSecondaryTarget(g_vehicle, 0, element);
    else
        g_vehicle->SetSecondaryTarget(element);
}

void CaseGetTargetPos() {
    g_out.p = g_original ? Orig_GetTargetPos(g_vehicle, 0) : g_vehicle->GetTargetPos();
}

void CaseUpdateNavPos() {
    // The original reads the target in the navigator's heading cases without checking it is there
    if (g_vehicle->target == NULL)
        g_vehicle->driveToNav->unknownB1 = g_vehicle->driveToNav->unknownB2 = 0;
    float distance = Chance(20) ? 0.0f : Uniform(0.0f, 80.0f);
    if (g_original)
        Orig_UpdateNavPos(g_vehicle, 0, &g_out.point, distance);
    else
        g_vehicle->UpdateNavPos(&g_out.point, distance);
}

void CaseEngage() {
    void *spline = g_in.b[1] ? g_spline : NULL;
    switch (g_in.i[1] & 7) {
    case 0:
        g_original ? Orig_EngageAutoDrive(g_vehicle, 0) : g_vehicle->EngageAutoDrive();
        break;
    case 1:
        g_original ? Orig_DisengageAutoDrive(g_vehicle, 0) : g_vehicle->DisengageAutoDrive();
        break;
    case 2:
        g_original ? Orig_DoPlayerLogic(g_vehicle, 0) : g_vehicle->DoPlayerLogic();
        break;
    case 3:
        g_original ? Orig_EngageSplinePath(g_vehicle, 0, spline)
                   : g_vehicle->EngageSplinePath(reinterpret_cast<CARP::AISpline *>(spline));
        break;
    case 4:
        g_original ? Orig_DisengageSplinePath(g_vehicle, 0) : g_vehicle->DisengageSplinePath();
        break;
    default:
        g_vehicle->splinePath = reinterpret_cast<AISplinePath *>(g_splinePath);
        g_original ? Orig_DoSplinePathUpdate(g_vehicle, 0) : g_vehicle->DoSplinePathUpdate();
        break;
    }
}

void CaseDrivableVec() {
    const Coord3 *from = &g_in.points[0];
    const Coord3 *to = g_in.b[0] ? &reinterpret_cast<RigidBody *>(g_bodies[0])->position : &g_in.points[1];
    g_out.b = g_original ? Orig_DrivableVec(g_vehicle, 0, from, to) : g_vehicle->DrivableVec(from, to);
}

void CaseBarriersInPath() {
    bool reversing = g_in.b[0];
    g_out.b = g_original ? Orig_BarriersInPath(g_vehicle, 0, reversing) : g_vehicle->BarriersInPath(reversing);
}

void CaseUpdateStuck() {
    if (g_original)
        Orig_UpdateStuck(g_vehicle, 0);
    else
        g_vehicle->UpdateStuck();
}

void CaseCollNavs() {
    Coord3 heading = UnitVector();
    g_out.point = heading;
    AIGroundVehicle *vehicle = g_in.b[3] ? g_target : g_vehicle;
    if (g_in.b[0]) {
        g_out.p = g_original ? Orig_GetHeliCollNav(vehicle, 0, &g_out.point) : vehicle->GetHeliCollNav(&g_out.point);
    } else {
        int8_t lane = int8_t(g_in.i[2] & 3);
        if (g_in.i[2] < 0)
            lane = int8_t(g_in.i[2]);
        g_out.p = g_original ? Orig_GetCollNav(vehicle, 0, &g_out.point, g_in.b[1], lane)
                             : vehicle->GetCollNav(&g_out.point, g_in.b[1], lane);
    }
}

void CaseTestForCollision() {
    PhysicsObject *object = reinterpret_cast<PhysicsObject *>(g_cars[g_in.b[0] ? 1 : 0]);
    const Coord3 *point = &g_in.points[0];
    const Coord3 *other = &g_in.points[1];
    float range = Chance(10) ? Near(10.0f) : Uniform(0.0f, 300.0f);
    g_out.b = g_original ? Orig_TestForCollision(object, point, other, range)
                         : TestForCollision(object, point, other, range);
}

void CaseLaneWeights() {
    switch (g_in.i[1] % 3) {
    case 0:
        g_out.i = g_original ? Orig_GetBestWeightedLane(g_vehicle, 0, g_in.i[0])
                             : g_vehicle->GetBestWeightedLane(g_in.i[0]);
        break;
    case 1:
        g_original ? Orig_GetRoadNetworkLaneWeights(g_vehicle, 0) : g_vehicle->GetRoadNetworkLaneWeights();
        break;
    default:
        g_original ? Orig_GetDirectLaneWeights(g_vehicle, 0, &g_in.points[0])
                   : g_vehicle->GetDirectLaneWeights(&g_in.points[0]);
        break;
    }
}

void CaseDoAttackMode() {
    if (g_original)
        Orig_DoAttackMode(g_vehicle, 0);
    else
        g_vehicle->DoAttackMode();
}

void CaseTurnTurret() {
    if (g_original)
        Orig_TurnTurret(g_vehicle, 0);
    else
        g_vehicle->TurnTurret();
}

void CaseDoAutoDriveUpdate() {
    if (g_original)
        Orig_DoAutoDriveUpdate(g_vehicle, 0);
    else
        g_vehicle->DoAutoDriveUpdate();
}

void CaseDoAgentStateUpdate() {
    if (g_original)
        Orig_DoAgentStateUpdate(g_vehicle, 0);
    else
        g_vehicle->DoAgentStateUpdate();
}

void CaseUnSpawn() {
    if (g_original)
        Orig_UnSpawn(g_vehicle, 0);
    else
        g_vehicle->UnSpawn();
}

// ---- the preparations

void PrepareNothing(int) {
}

// A target always there where the original reads it unchecked
void PrepareTarget(int) {
    if (g_vehicle->target == NULL && (g_vehicle->attackMode == kGroundAttack14 ||
                                      g_vehicle->attackMode == kGroundAttackChase))
        g_vehicle->target = g_target;
}

void PrepareLanes(int i) {
    // Ties are common: runs of equal weights
    if (i % 3 == 0)
        for (int lane = 0; lane < 0x40; lane++)
            g_vehicle->laneWeights[lane] = uint8_t(Range(0, 2) * 10);
    g_vehicle->driveToNav->laneOffset = Uniform(-34.0f, 34.0f);
}

void PrepareTurret(int i) {
    // The goal straight ahead, behind, either side, and near the turret's angle
    RigidBody *body = reinterpret_cast<RigidBody *>(g_bodies[0]);
    const Coord3 &forward = *Xyz(MatrixRow(&body->info->orientation, 2));
    switch (i % 4) {
    case 0:
        g_vehicle->goalDirection = forward;
        break;
    case 1:
        g_vehicle->goalDirection = {-forward.x, -forward.y, -forward.z};
        break;
    default:
        break;
    }
    if (Chance(30)) {
        g_vehicle->goalHidden = 0;
        g_vehicle->targetMode = Range(0, 1);
    }
}

void PrepareStuck(int) {
    int seconds = 5 * ShadowStepsPerSecond;
    if (Chance(40))
        g_vehicle->stuckSteps = int16_t(seconds - Range(0, 2));
}

void PrepareAgent(int) {
    PrepareTarget(0);
    // The line-of-sight step and the zones' entry conditions, often met
    if (Chance(50))
        g_vehicle->pathCheckStep = ShadowStepCount - ShadowStepsPerSecond / 4 + Range(-1, 1);
    if (Chance(40)) {
        g_vehicle->isSub = 0;
        g_vehicle->skipFullUpdate = 0;
        g_vehicle->zoneStopSteps = 0;
    }
    if (Chance(30))
        g_vehicle->navigateModeStep = 0;
    if (Chance(40))
        g_vehicle->attackMode = kGroundAttackChase;
    // The secondary target keeps the target there where the original reads it unchecked
    if (g_vehicle->attackMode == kGroundAttackChase || g_vehicle->attackMode == kGroundAttack14) {
        g_vehicle->target = g_target;
        *reinterpret_cast<int32_t *>(g_element + 0x48) = 1;
        *reinterpret_cast<void **>(g_element + 0x50) = g_target;
    }
}

void Run(const char *what, int count, void (*prepare)(int), SideFn run, const Snapshot &before) {
    for (int i = 0; i < count; i++) {
        MakeWorld();
        prepare(i);
        RunCase(what, i, run, before);
    }
}

}  // namespace

void GameAIShadowA_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEAISHADOWA", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    g_playerCar = ShadowCars.first != NULL && ShadowCars.last != ShadowCars.first ? ShadowCars.first[0] : NULL;
    if (g_playerCar == NULL || glbMissionManager == NULL) {
        printf("[gameaishadowa] no player car or mission manager at the first tick - nothing run\n");
        fflush(stdout);
        return;
    }
    // The templates: the live player car's body and its AI's navigator
    RigidBody *playerBody = fgSimulation.GetRigidBody(g_playerCar->rigidBodySlot);
    memcpy(g_bodyTemplate, playerBody, sizeof(RigidBody));
    memcpy(g_infoTemplate, playerBody->info, sizeof(RigidBodyInfo));
    g_player = playerBody->position;
    AIGroundVehicle *liveAI = AIVehicleController::Get()->GetPlayerGroundVehiclePtr(0);
    if (liveAI->driveToNav != NULL)
        memcpy(g_navTemplate, liveAI->driveToNav, sizeof(g_navTemplate));
    for (int i = 0; i < kMirrorCount; i++)
        memcpy(g_gameCopies[i], (const void *)uintptr_t(kMirrors[i].game), kMirrors[i].size);
    printf("[gameaishadowa] live at the first tick: %d cars, the player's AI type %d, navigate mode %d, road network "
           "%s\n", int(ShadowCars.last - ShadowCars.first), liveAI->type, liveAI->navigateMode,
           fgRoadNetworkData.loaded ? "loaded" : "none");

    BuildVtable();
    if (!InstallHooks()) {
        RemoveHooks();
        printf("[gameaishadowa] fakes missing - nothing run\n");
        fflush(stdout);
        return;
    }
    BuildRegions();
    Snapshot before;
    Capture(&before);
    bool roads = fgRoadNetworkData.loaded && fgRoadNetworkData.segmentCount > 0;

    Run("AIGroundVehicle::AIGroundVehicle", 20, PrepareNothing, CaseConstruct, before);
    Run("AIGroundVehicle::~AIGroundVehicle", 20, PrepareNothing, CaseDestruct, before);
    Run("AIGroundVehicle's vector deleting destructor", 40, PrepareNothing, CaseDelete, before);
    Run("AIGroundVehicle::Reset", 20, PrepareNothing, CaseReset, before);
    Run("AIGroundVehicle::SetNavigateMode", 200, PrepareNothing, CaseSetNavigateMode, before);
    Run("AIGroundVehicle::SetTargetMode", 40, PrepareNothing, CaseSetTargetMode, before);
    Run("AIGroundVehicle::SetAttackMode", 60, PrepareNothing, CaseSetAttackMode, before);
    Run("AIGroundVehicle::SetSecondaryTarget", 60, PrepareNothing, CaseSetSecondaryTarget, before);
    Run("AIGroundVehicle::GetTargetPos", 40, PrepareNothing, CaseGetTargetPos, before);
    Run("AIGroundVehicle's engage, disengage, player logic, spline path update", 200, PrepareNothing, CaseEngage,
        before);
    Run("AIGroundVehicle::DrivableVec", 100, PrepareNothing, CaseDrivableVec, before);
    Run("AIGroundVehicle::BarriersInPath", 200, PrepareNothing, CaseBarriersInPath, before);
    Run("AIGroundVehicle::UpdateStuck", 200, PrepareStuck, CaseUpdateStuck, before);
    Run("AIGroundVehicle::GetHeliCollNav, GetCollNav", 200, PrepareNothing, CaseCollNavs, before);
    Run("TestForCollision", 200, PrepareNothing, CaseTestForCollision, before);
    Run("AIGroundVehicle's lane weights", 300, PrepareLanes, CaseLaneWeights, before);
    Run("AIGroundVehicle::DoAttackMode", 600, PrepareTarget, CaseDoAttackMode, before);
    Run("AIGroundVehicle::TurnTurret", 300, PrepareTurret, CaseTurnTurret, before);
    Run("AIGroundVehicle::DoAutoDriveUpdate", 200, PrepareNothing, CaseDoAutoDriveUpdate, before);
    Run("AIGroundVehicle::UnSpawn", 60, PrepareNothing, CaseUnSpawn, before);
    if (roads) {
        Run("AIGroundVehicle::UpdateNavPos", 300, PrepareNothing, CaseUpdateNavPos, before);
        Run("AIGroundVehicle::DoAgentStateUpdate", 600, PrepareAgent, CaseDoAgentStateUpdate, before);
    } else {
        g_skipped++;
    }

    Restore(before);
    RemoveHooks();
    for (int i = 0; i < kMirrorCount; i++)
        memcpy((void *)uintptr_t(kMirrors[i].game), g_gameCopies[i], kMirrors[i].size);
    ResetFpu();
    printf("[gameaishadowa] AIGroundVehicle part 1 vs originals: %ld cases, %ld checks, %ld differ%s\n", g_cases,
           g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameaishadowa]   %ld calls faulted\n", g_faults);
    if (g_skipped != 0)
        printf("[gameaishadowa]   %ld groups skipped (no road network)\n", g_skipped);
    fflush(stdout);
}
