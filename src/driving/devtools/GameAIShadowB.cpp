#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAIShadowB.h"
#include "OwnedGlobals.h"
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

#include "../data/Carp.h"
#include "../engine/CoreContainers.h"
#include "../game/MissionManager.h"
#include "../game/AIGroundVehicle.h"
#include "../game/AIGroundVehicleDriving.h"
#include "../game/AISplinePath.h"
#include "../game/AIVehicleController.h"
#include "../game/BondCar.h"
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
// A shadow test of game.ai's package B (AIGroundVehicle, part 2), run once from the first simulation tick when
// NIGHTFIRE_GAMEAISHADOWB is set. Each case runs the ORIGINAL (every entry of the package swapped back together) and
// then the PORT from the same state, and compares what they leave - four synthetic vehicles (this one, its target and
// two others) and their cars, bodies, navigators, sounds, the scene object, the traffic spawn count, the road
// network's statics and road counts - the answers, and a log of every call out. Every function of the package is run:
//
//   - GetLookAhead (its unrounded answer compared bit for bit), the three DriveToPoint functions (cars, traffic,
//     subs following a height; reversing, stopping, the zone stop and the chase let-off), GetCollisionStopPoint,
//     InCollisionRange, ClearShot;
//   - FireBullets, FireRockets, FireMissiles, DoWeaponUpdate (turrets, the attack modes, the reload counters, the
//     systems still playing, no animation handle);
//   - Spawn and DoTrafficStateUpdate (the spawner's answers, the view test in auto-drive, the road counts, shock);
//   - CheckAgentRoadNetworkCollision, CheckAgentDirectCollision, CheckTrafficCollision (cars placed round this one,
//     the target and the player among them, junction segments with the light red or green);
//   - DoMoveMode (every navigate mode and vehicle type, the stop when dead, wrecked or in an EMP), Update (every type,
//     its jump table), and FUN_00031820 (WRoadNav::RightDrivableDist) over the real road segments.
//
// The cars are synthetic PhysicsObjects whose vtables are logging fakes; their bodies copies of the live player car's
// (the Simulation's body getter and RigidBody::GetOwner are faked to hand them out), and the Simulation's car list is
// pointed at them for each case. Fakes (5-byte jumps at the callee both sides reach): part 1's methods (UpdateNavPos,
// BarriersInPath, GetCollNav, TestForCollision, the lane weights, SetNavigateMode, UnSpawn, TurnTurret and the
// per-step updates), the animation handle's stimuli, the road spawner, the navigators' InitAtSegment and ChangeLanes,
// the junction light, the view test, the road counts and RigidBody::ResetObject. The maths, the road network's
// queries, the AI element controller's ceil and floor, MinFloat, MaxFloat and the path handle run for real.
//
// A mutation the test sees: GetLookAhead answering float(ahead) instead of the unrounded product when the speed term
// wins - its answer is compared as a double.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x0002dc90, 0x0002dd80, 0x0002e0a0, 0x0002e580, 0x0002e6b0, 0x0002e810, 0x0002e910, 0x0002eb40, 0x0002ee00,
    0x0002f0e0, 0x0002f190, 0x0002f4c0, 0x0002f610, 0x0002fde0, 0x000306b0, 0x00030b80, 0x00030fb0, 0x00031750,
    0x00031820,
};

typedef void (__fastcall *MethodFn)(void *, int);
typedef void (__fastcall *PointFn)(void *, int, Coord3 *);
typedef void (__fastcall *DriveFn)(void *, int, Coord3 *, float);

#define Orig_GetLookAhead ((double (__fastcall *)(void *, int, float))0x0002dc90)
#define Orig_DriveToPointTraffic ((DriveFn)0x0002dd80)
#define Orig_DriveToPointSub ((DriveFn)0x0002e0a0)
#define Orig_GetCollisionStopPoint ((void (*)(PhysicsObject *, Coord3 *, const Coord3 *, float))0x0002e580)
#define Orig_InCollisionRange ((bool (*)(const Coord3 *, const Coord3 *, const Coord3 *, float, float, bool))0x0002e6b0)
#define Orig_ClearShot ((bool (*)(int, const Coord3 *, const Coord3 *, float))0x0002e810)
#define Orig_FireBullets ((MethodFn)0x0002e910)
#define Orig_FireRockets ((MethodFn)0x0002eb40)
#define Orig_FireMissiles ((MethodFn)0x0002ee00)
#define Orig_DoWeaponUpdate ((MethodFn)0x0002f0e0)
#define Orig_Spawn ((void (__fastcall *)(void *, int, int))0x0002f190)
#define Orig_DoTrafficStateUpdate ((MethodFn)0x0002f4c0)
#define Orig_DriveToPoint ((DriveFn)0x0002f610)
#define Orig_CheckAgentRoadNetworkCollision ((PointFn)0x0002fde0)
#define Orig_CheckAgentDirectCollision ((PointFn)0x000306b0)
#define Orig_CheckTrafficCollision ((PointFn)0x00030b80)
#define Orig_DoMoveMode ((MethodFn)0x00030fb0)
#define Orig_Update ((MethodFn)0x00031750)
#define Orig_RightDrivableDist ((double (__fastcall *)(void *, int))0x00031820)

// ---- the game's state

#define ShadowSim ((uint8_t *)OwnedGlobals_Ours(0x00233ff0))
#define ShadowStepCount I32_AT(OwnedGlobals_Ours(0x00234e34))
#define ShadowStepsPerSecond I32_AT(OwnedGlobals_Ours(0x00234e2c))
#define ShadowCars (*(GameVector<PhysicsObject *> *)OwnedGlobals_Ours(0x00234e3c))
#define ShadowSpawnCount I16_AT(OwnedGlobals_Ours(0x001de918))
#define ShadowMaxTraffic I16_AT(OwnedGlobals_Ours(0x001de914))

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

// The first three differences of each kind (a kind is its "what" text), up to 200 in all
void Report(const char *what, long index, const char *detail) {
    g_differ++;
    static const char *kinds[64];
    static int counts[64];
    int k = 0;
    while (k < 64 && kinds[k] != NULL && strcmp(kinds[k], what) != 0)
        k++;
    if (k == 64)
        return;
    kinds[k] = what;
    if (counts[k]++ < 3 && g_reported++ < 200)
        printf("[gameaishadowb]   %s #%ld: %s\n", what, index, detail);
}

void CompareBytes(const char *what, long index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    size_t at = OwnedGlobals_FirstDifference(a, b, bytes);   // a vtable or callback: ours, or the game's
    if (at == bytes)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
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

uint32_t g_seed = 0x0a1d0b0b;

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

void CompareLogs(const char *what, long index) {
    g_checks++;
    for (std::vector<uint32_t> &log : g_logs)   // vtables and callbacks alike on either side
        OwnedGlobals_CanonicalWords(log.data(), log.size() * 4);
    const std::vector<uint32_t> &a = g_logs[0], &b = g_logs[1];
    if (a == b)
        return;
    size_t at = 0;
    while (at < a.size() && at < b.size() && a[at] == b[at])
        at++;
    // The words from ten before the difference to one after, each side
    char detail[400];
    int length = snprintf(detail, sizeof(detail), "call log word %u of %u/%u:", unsigned(at), unsigned(a.size()),
                          unsigned(b.size()));
    for (int side = 0; side < 2; side++) {
        const std::vector<uint32_t> &log = side == 0 ? a : b;
        length += snprintf(detail + length, sizeof(detail) - length, side == 0 ? " original" : "; port");
        for (size_t i = at >= 10 ? at - 10 : 0; i <= at + 1 && i < log.size() && length < 380; i++)
            length += snprintf(detail + length, sizeof(detail) - length, i == at ? " [%08x]" : " %08x", log[i]);
    }
    Report(what, index, detail);
}

// ---- the synthetic world

constexpr int kVehicles = 4;                // this one, its target, two others
constexpr int kNavs = 5;                    // driveToNav, collNav, laneCollNav, heliCollNav, targetNav
constexpr int16_t kBodySlots[kVehicles] = {0x7b10, 0x7b11, 0x7b12, 0x7b13};

alignas(16) uint8_t g_vehicleBlocks[kVehicles][sizeof(AIGroundVehicle)];
alignas(16) uint8_t g_cars[kVehicles][0x420];
alignas(16) uint8_t g_bodies[kVehicles][sizeof(RigidBody)];
alignas(16) uint8_t g_infos[kVehicles][sizeof(RigidBodyInfo)];
alignas(16) uint8_t g_navs[kVehicles][kNavs][sizeof(WRoadNav)];
alignas(16) uint8_t g_collNav[sizeof(WRoadNav)];
alignas(16) uint8_t g_sounds[kVehicles][sizeof(ATrafficVehicle)];
alignas(16) uint8_t g_sceneBlock[0x80];
alignas(16) uint8_t g_handleBlock[0x40];
alignas(16) uint8_t g_physics[kVehicles][sizeof(CarPhysics)];
alignas(16) uint8_t g_splinePath[sizeof(AISplinePath)];
alignas(16) uint8_t g_pathHandle[sizeof(RPathHandle)];
alignas(16) uint8_t g_pathInstance[sizeof(CARP::Instance)];
alignas(16) uint8_t g_navTemplate[sizeof(WRoadNav)];
alignas(16) uint8_t g_bodyTemplate[sizeof(RigidBody)];
alignas(16) uint8_t g_infoTemplate[sizeof(RigidBodyInfo)];
float g_hitPoints[kVehicles];
PhysicsObject *g_carList[kVehicles];
int g_carCount;

AIGroundVehicle *Vehicle(int i) {
    return reinterpret_cast<AIGroundVehicle *>(g_vehicleBlocks[i]);
}

RigidBody *Body(int i) {
    return reinterpret_cast<RigidBody *>(g_bodies[i]);
}

PhysicsObject *CarOf(int i) {
    return reinterpret_cast<PhysicsObject *>(g_cars[i]);
}

PVehicle *g_playerCar;
Coord3 g_player;
std::vector<int16_t> g_junctionSegments;    // segments CheckTrafficCollision's light test reaches
std::vector<int16_t> g_safeSegments;        // ... and every segment it can read without faulting

void *g_carVtable[PVehicle::kSlotCount];

// One case's inputs and scripted answers
struct Inputs {
    int carClass[kVehicles];
    int inShock[kVehicles];
    uint8_t emp[kVehicles];
    uint8_t tyres[kVehicles];               // IsTyreShredded's, by wheel (bit)
    float steering;                         // GetCarControl's
    bool barriers;                          // BarriersInPath's
    uint32_t collisions;                    // TestForCollision's, by call (bit)
    int collision;
    int bestLane;                           // GetBestWeightedLane's
    uint8_t weights[64];                    // the lane weights' scripted rows
    Coord3 navPoint;                        // UpdateNavPos's
    uint32_t playing;                       // IsSystemPlaying's, by system (bit, mod 32)
    bool respawnAvailable, posCheck, spawnFound, inFrustum, greenLight;
    int16_t spawnSegment;
    int8_t spawnLane;
    float spawnAlong;
    bool spawnForwards;
    float f[4];
    int i[4];
    bool b[4];
    Coord3 points[2];
} g_in;

// The answers (compared)
struct Outputs {
    double d;
    const void *p;
    int i;
    bool b;
    uint8_t pad[3];
    Coord3 point;
    uint32_t pad2;
} g_out;

// ---- the fakes

enum : uint32_t {
    kTagBody = 0x424f4459, kTagOwner = 0x4f574e52, kTagStimuli = 0x5354494d, kTagPlaying = 0x504c4159,
    kTagRespawn = 0x52535041, kTagPosCheck = 0x50534348, kTagRandomSpawn = 0x52535057, kTagInitSegment = 0x4e495347,
    kTagLanes = 0x4e4c4e53, kTagGreen = 0x47524e4c, kTagFrustum = 0x46525354, kTagRoadAdd = 0x52414444,
    kTagRoadRemove = 0x5252454d, kTagResetObject = 0x524f424a, kTagNavPos = 0x4e415650, kTagBarriers = 0x42415252,
    kTagCollNav = 0x434c4e56, kTagTestCollision = 0x54434f4c, kTagBestLane = 0x4245534c, kTagRoadWeights = 0x52445747,
    kTagDirectWeights = 0x44525747, kTagSetNavigate = 0x534e4156, kTagUnSpawn = 0x554e5350, kTagTurret = 0x54555252,
    kTagPlayerLogic = 0x504c4f47, kTagStuck = 0x5354434b, kTagAutoDrive = 0x4155544f, kTagAttack = 0x4154544b,
    kTagAgent = 0x4147454e, kTagSplineUpdate = 0x53504c55, kTagCarSlot = 0x43530000,
};

int CarIndex(const void *car) {
    for (int i = 0; i < kVehicles; i++)
        if (car == g_cars[i])
            return i;
    return 0;
}

// The Simulation's body getter: our copies for the synthetic cars' slots, the Simulation's tables for the rest
RigidBody *__fastcall FakeGetRigidBody(uint8_t *simulation, int, int slot) {
    for (int i = 0; i < kVehicles; i++)
        if (slot == kBodySlots[i])
            return Body(i);
    Log(kTagBody);
    Log(uint32_t(slot));
    uint8_t *table = *reinterpret_cast<uint8_t **>(simulation + (simulation[4] != 0 ? 0x28 : 0x1c));
    return reinterpret_cast<RigidBody *>(table + (slot << 7));
}

PhysicsObject *__fastcall FakeGetOwner(RigidBody *body, int) {
    for (int i = 0; i < kVehicles; i++)
        if (body == Body(i))
            return CarOf(i);
    Log(kTagOwner);
    LogPointer(body);
    return CarOf(0);
}

// The animation handle
void __fastcall FakeStimuli(void *handle, int, uint32_t id, uint8_t stimulus, uint32_t tick, int mode) {
    Log(kTagStimuli);
    LogPointer(handle);
    Log(id);
    Log(stimulus);
    Log(tick);
    Log(uint32_t(mode));
}

bool __fastcall FakePlaying(void *handle, int, uint32_t id) {
    Log(kTagPlaying);
    LogPointer(handle);
    Log(id);
    return (g_in.playing >> (id & 31) & 1) != 0;
}

// The road spawner, the navigators, the junction light, the view test, the road counts
bool __fastcall FakeRespawnAvailable(void *spawner, int, const Coord3 *position, float unused) {
    Log(kTagRespawn);
    LogPointer(spawner);
    LogBytes(position, 12);
    LogFloat(unused);
    return g_in.respawnAvailable;
}

bool __fastcall FakePosCheck(void *spawner, int, const Coord3 *position, float margin) {
    Log(kTagPosCheck);
    LogPointer(spawner);
    LogBytes(position, 12);
    LogFloat(margin);
    return g_in.posCheck;
}

bool __fastcall FakeRandomSpawn(void *spawner, int, short *segment, int8_t *lane, float *along, bool *forwards) {
    Log(kTagRandomSpawn);
    LogPointer(spawner);
    Log(uint32_t(uint16_t(*segment)));
    Log(uint8_t(*lane));
    LogFloat(*along);
    Log(*forwards);
    if (g_in.spawnFound) {
        *segment = g_in.spawnSegment;
        *lane = g_in.spawnLane;
        *along = g_in.spawnAlong;
        *forwards = g_in.spawnForwards;
    }
    return g_in.spawnFound;
}

void __fastcall FakeInitAtSegment(WRoadNav *nav, int, short segment, int8_t lane, float along, bool forwards) {
    Log(kTagInitSegment);
    LogPointer(nav);
    Log(uint32_t(uint16_t(segment)));
    Log(uint8_t(lane));
    LogFloat(along);
    Log(forwards);
    nav->roadPoint.x += along;
    nav->segment = segment;
}

void __fastcall FakeChangeLanes(WRoadNav *nav, int, float offset, float distance) {
    Log(kTagLanes);
    LogPointer(nav);
    LogFloat(offset);
    LogFloat(distance);
}

bool __fastcall FakeGreenLight(void *controller, int, const Coord3 *position, uint32_t phase, uint32_t phases) {
    Log(kTagGreen);
    LogPointer(controller);
    LogBytes(position, 12);
    Log(phase);
    Log(phases);
    return g_in.greenLight;
}

bool __fastcall FakeFrustum(void *culling, int, const Coord4 *sphere, float radius, bool checkFar, float farScale,
                            float *distance) {
    Log(kTagFrustum);
    LogPointer(culling);
    LogBytes(sphere, 16);
    LogFloat(radius);
    Log(checkFar);
    LogFloat(farScale);
    LogPointer(distance);
    return g_in.inFrustum;
}

void __fastcall FakeRoadAdd(void *network, int, int road) {
    Log(kTagRoadAdd);
    LogPointer(network);
    Log(uint32_t(road));
}

void __fastcall FakeRoadRemove(void *network, int, int road) {
    Log(kTagRoadRemove);
    LogPointer(network);
    Log(uint32_t(road));
}

void __fastcall FakeResetObject(RigidBody *body, int, const MATRIX4 *orientation, const Coord3 *position) {
    Log(kTagResetObject);
    LogPointer(body);
    LogBytes(orientation, sizeof(MATRIX4));
    LogBytes(position, 12);
}

// Part 1's methods
void __fastcall FakeNavPos(AIGroundVehicle *vehicle, int, Coord3 *point, float distance) {
    Log(kTagNavPos);
    LogPointer(vehicle);
    LogPointer(point);
    LogFloat(distance);
    *point = g_in.navPoint;
}

// The original hands on the `reversing` byte raw, ours as a bool: the made-up 2 is logged as true
bool __fastcall FakeBarriers(AIGroundVehicle *vehicle, int, uint8_t reversing) {
    Log(kTagBarriers);
    LogPointer(vehicle);
    Log(reversing != 0);
    return g_in.barriers;
}

WRoadNav *__fastcall FakeCollNav(AIGroundVehicle *vehicle, int, const Coord3 *heading, bool inLane, int8_t lane) {
    Log(kTagCollNav);
    LogPointer(vehicle);
    LogBytes(heading, 12);
    Log(inLane);
    Log(uint8_t(lane));
    return reinterpret_cast<WRoadNav *>(g_collNav);
}

bool FakeTestCollision(PhysicsObject *object, const Coord3 *point, const Coord3 *other, float range) {
    Log(kTagTestCollision);
    LogPointer(object);
    LogBytes(point, 12);
    LogBytes(other, 12);
    LogFloat(range);
    return (g_in.collisions >> (g_in.collision++ & 31) & 1) != 0;
}

int __fastcall FakeBestLane(AIGroundVehicle *vehicle, int, int lane) {
    Log(kTagBestLane);
    LogPointer(vehicle);
    Log(uint32_t(lane));
    LogBytes(vehicle->laneWeights, sizeof(vehicle->laneWeights));
    return g_in.bestLane;
}

void __fastcall FakeRoadWeights(AIGroundVehicle *vehicle, int) {
    Log(kTagRoadWeights);
    LogPointer(vehicle);
    memcpy(vehicle->laneWeights, g_in.weights, sizeof(vehicle->laneWeights));
}

void __fastcall FakeDirectWeights(AIGroundVehicle *vehicle, int, const Coord3 *point) {
    Log(kTagDirectWeights);
    LogPointer(vehicle);
    LogBytes(point, 12);
    memcpy(vehicle->laneWeights, g_in.weights, sizeof(vehicle->laneWeights));
}

void __fastcall FakeSetNavigate(AIGroundVehicle *vehicle, int, int mode, void *spline) {
    Log(kTagSetNavigate);
    LogPointer(vehicle);
    Log(uint32_t(mode));
    LogPointer(spline);
}

void __fastcall FakeUnSpawn(AIGroundVehicle *vehicle, int) {
    Log(kTagUnSpawn);
    LogPointer(vehicle);
}

void __fastcall FakeTurret(AIGroundVehicle *vehicle, int) {
    Log(kTagTurret);
    LogPointer(vehicle);
}

void __fastcall FakePlayerLogic(AIGroundVehicle *vehicle, int) {
    Log(kTagPlayerLogic);
    LogPointer(vehicle);
}

void __fastcall FakeStuck(AIGroundVehicle *vehicle, int) {
    Log(kTagStuck);
    LogPointer(vehicle);
}

void __fastcall FakeAutoDrive(AIGroundVehicle *vehicle, int) {
    Log(kTagAutoDrive);
    LogPointer(vehicle);
}

void __fastcall FakeAttack(AIGroundVehicle *vehicle, int) {
    Log(kTagAttack);
    LogPointer(vehicle);
}

void __fastcall FakeAgent(AIGroundVehicle *vehicle, int) {
    Log(kTagAgent);
    LogPointer(vehicle);
}

void __fastcall FakeSplineUpdate(AIGroundVehicle *vehicle, int) {
    Log(kTagSplineUpdate);
    LogPointer(vehicle);
}

// The cars' vtable
void LogSlot(int slot, const void *car) {
    Log(kTagCarSlot + uint32_t(slot));
    LogPointer(car);
}

ABaseSound *__fastcall FakeCarAudio(void *car, int) {
    LogSlot(PVehicle::kGetAudio, car);
    return reinterpret_cast<ABaseSound *>(g_sounds[CarIndex(car)]);
}

AIGroundVehicle *__fastcall FakeCarAI(void *car, int) {
    LogSlot(PVehicle::kGetAIGroundVehiclePtr, car);
    return Vehicle(CarIndex(car));
}

int __fastcall FakeCarGetClass(void *car, int) {
    LogSlot(PVehicle::kGetCarClass, car);
    return g_in.carClass[CarIndex(car)];
}

void __fastcall FakeCarSteering(void *car, int, float value) {
    LogSlot(PVehicle::kSetCarControlSteering, car);
    LogFloat(value);
}

void __fastcall FakeCarSteeringVertical(void *car, int, float value) {
    LogSlot(PVehicle::kSetCarControlSteeringVertical, car);
    LogFloat(value);
}

void __fastcall FakeCarGas(void *car, int, float value) {
    LogSlot(PVehicle::kSetCarControlGas, car);
    LogFloat(value);
}

void __fastcall FakeCarBrake(void *car, int, float value) {
    LogSlot(PVehicle::kSetCarControlBrake, car);
    LogFloat(value);
}

void __fastcall FakeCarHandBrake(void *car, int, bool value) {
    LogSlot(PVehicle::kSetCarControlHandBrake, car);
    Log(value);
}

BondCarControl *__fastcall FakeCarControl(void *car, int, BondCarControl *result) {
    LogSlot(PVehicle::kGetCarControl, car);
    memset(result, 0, sizeof(*result));
    result->steering = g_in.steering;
    return result;
}

uint8_t __fastcall FakeCarEmp(void *car, int) {
    LogSlot(PVehicle::kEmpActive, car);
    return g_in.emp[CarIndex(car)];
}

bool __fastcall FakeCarTyre(void *car, int, int8_t wheel) {
    LogSlot(PVehicle::kIsTyreShredded, car);
    Log(uint8_t(wheel));
    return (g_in.tyres[CarIndex(car)] >> (wheel & 7) & 1) != 0;
}

void __fastcall FakeCarResetAt(void *car, int, Coord3 *position, const Coord3 *direction) {
    LogSlot(PVehicle::kResetCarAt, car);
    LogBytes(position, 12);
    LogBytes(direction, 12);
}

void __fastcall FakeCarGlare(void *car, int, int glare) {
    LogSlot(PVehicle::kGlareOn, car);
    Log(uint32_t(glare));
}

int __fastcall FakeCarInShock(void *car, int) {
    LogSlot(PVehicle::kInShock, car);
    return g_in.inShock[CarIndex(car)];
}

void __fastcall FakeCarResetDamage(void *car, int) {
    LogSlot(PVehicle::kResetDamage, car);
}

void __fastcall FakeCarClearEmp(void *car, int) {
    LogSlot(PVehicle::kClearEMPState, car);
}

CarPhysics *__fastcall FakeCarPhysics(void *car, int) {
    LogSlot(PVehicle::kGetPhysics, car);
    return reinterpret_cast<CarPhysics *>(g_physics[CarIndex(car)]);
}

void __fastcall FakeCarOther(void *car, int) {
    LogSlot(0xff, car);
}

// ---- hooks: a jump over the first five bytes of our code (where the entry jumps to it) or of the original

struct HookSpec {
    uint32_t entry;
    const void *to;
};

const HookSpec kHooks[] = {
    {0x000b2700, (const void *)&FakeGetRigidBody}, {0x000ad100, (const void *)&FakeGetOwner},
    {0x00077d70, (const void *)&FakeStimuli}, {0x000774a0, (const void *)&FakePlaying},
    {0x00034a70, (const void *)&FakeRespawnAvailable}, {0x00034a00, (const void *)&FakePosCheck},
    {0x00035130, (const void *)&FakeRandomSpawn}, {0x000cb900, (const void *)&FakeInitAtSegment},
    {0x000cbcf0, (const void *)&FakeChangeLanes}, {0x00036060, (const void *)&FakeGreenLight},
    {0x0008d180, (const void *)&FakeFrustum}, {0x00029c90, (const void *)&FakeRoadAdd},
    {0x00029cb0, (const void *)&FakeRoadRemove}, {0x000adcd0, (const void *)&FakeResetObject},
    {0x0002a3e0, (const void *)&FakeNavPos}, {0x0002aaf0, (const void *)&FakeBarriers},
    {0x0002b210, (const void *)&FakeCollNav}, {0x0002b4d0, (const void *)&FakeTestCollision},
    {0x0002b5e0, (const void *)&FakeBestLane}, {0x0002b660, (const void *)&FakeRoadWeights},
    {0x0002b7a0, (const void *)&FakeDirectWeights}, {0x0002a0d0, (const void *)&FakeSetNavigate},
    {0x0002dac0, (const void *)&FakeUnSpawn}, {0x0002c6e0, (const void *)&FakeTurret},
    {0x0002a7c0, (const void *)&FakePlayerLogic}, {0x0002af50, (const void *)&FakeStuck},
    {0x0002c9d0, (const void *)&FakeAutoDrive}, {0x0002bf40, (const void *)&FakeAttack},
    {0x0002cd80, (const void *)&FakeAgent}, {0x0002a8b0, (const void *)&FakeSplineUpdate},
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
            printf("[gameaishadowb] the fake at 0x%08x could not be installed\n", kHooks[i].entry);
            missing++;
        }
    return missing == 0;
}

void RemoveHooks() {
    for (int i = kHookCount - 1; i >= 0; i--)
        HookRemove(&g_hooks[i]);
}

void BuildVtable() {
    for (void *&slot : g_carVtable)
        slot = (void *)&FakeCarOther;
    g_carVtable[PVehicle::kGetAudio] = (void *)&FakeCarAudio;
    g_carVtable[PVehicle::kGetAIGroundVehiclePtr] = (void *)&FakeCarAI;
    g_carVtable[PVehicle::kGetCarClass] = (void *)&FakeCarGetClass;
    g_carVtable[PVehicle::kSetCarControlSteering] = (void *)&FakeCarSteering;
    g_carVtable[PVehicle::kSetCarControlSteeringVertical] = (void *)&FakeCarSteeringVertical;
    g_carVtable[PVehicle::kSetCarControlGas] = (void *)&FakeCarGas;
    g_carVtable[PVehicle::kSetCarControlBrake] = (void *)&FakeCarBrake;
    g_carVtable[PVehicle::kSetCarControlHandBrake] = (void *)&FakeCarHandBrake;
    g_carVtable[PVehicle::kGetCarControl] = (void *)&FakeCarControl;
    g_carVtable[PVehicle::kEmpActive] = (void *)&FakeCarEmp;
    g_carVtable[PVehicle::kIsTyreShredded] = (void *)&FakeCarTyre;
    g_carVtable[PVehicle::kResetCarAt] = (void *)&FakeCarResetAt;
    g_carVtable[PVehicle::kGlareOn] = (void *)&FakeCarGlare;
    g_carVtable[PVehicle::kInShock] = (void *)&FakeCarInShock;
    g_carVtable[PVehicle::kResetDamage] = (void *)&FakeCarResetDamage;
    g_carVtable[PVehicle::kClearEMPState] = (void *)&FakeCarClearEmp;
    g_carVtable[PVehicle::kGetPhysics] = (void *)&FakeCarPhysics;
}

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    g_in.collision = 0;
    if (original)
        Window(true);
#ifdef _MSC_VER
    __try {
        run();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
    }
#else
    run();
#endif
    if (original)
        Window(false);
    ResetFpu();
}

// The inputs are in place: both sides from them, the results compared, everything put back
void RunCase(const char *what, long index, SideFn run, const Snapshot &before) {
    Snapshot inputs, original, port;
    Capture(&inputs);
    g_logs[0].clear();
    g_logs[1].clear();
    uint32_t seed = g_seed;     // a case's own draws repeat on the port's side
    RunSide(run, true);
    Capture(&original);
    Restore(inputs);
    g_seed = seed;
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

// The segments where CheckTrafficCollision looks at the junction's light (going either way along them): a valid
// intersection at that end
void FindJunctionSegments() {
    g_junctionSegments.clear();
    g_safeSegments.clear();
    if (!fgRoadNetworkData.loaded)
        return;
    for (int s = 0; s < fgRoadNetworkData.segmentCount; s++) {
        WRoadSegment *segment = fgRoadNetworkData.segments[s];
        if (!(segment->flags & kSegmentFlag80)) {
            g_safeSegments.push_back(int16_t(s));
            continue;
        }
        bool usable = true;
        for (int end = 0; end < 2; end++) {
            WRoadNode *node = fgRoadNetworkData.nodes[segment->node[end]];
            if (node->segmentCount > 2 &&
                (segment->intersection[end] < 0 || segment->intersection[end] >= fgRoadNetworkData.intersectionCount))
                usable = false;
        }
        if (usable) {
            g_junctionSegments.push_back(int16_t(s));
            g_safeSegments.push_back(int16_t(s));
        }
    }
}

int16_t SafeSegment() {
    if (g_safeSegments.empty())
        return 0;
    return g_safeSegments[Range(0, int(g_safeSegments.size()) - 1)];
}

void RandomInputs() {
    for (int i = 0; i < kVehicles; i++) {
        g_in.carClass[i] = Range(0, 4);
        g_in.inShock[i] = Chance(20) ? 1 : 0;
        g_in.emp[i] = uint8_t(Chance(8));
        g_in.tyres[i] = uint8_t(Chance(70) ? 0 : 1 << Range(0, 3));
    }
    g_in.steering = Chance(30) ? 0.0f : Uniform(-1.0f, 1.0f);
    g_in.barriers = Chance(30);
    g_in.collisions = Next() | Next() << 24;
    g_in.bestLane = Range(0, 63);
    for (uint8_t &weight : g_in.weights)
        weight = uint8_t(Chance(15) ? 0xff : Chance(30) ? 0 : Range(0, 254));
    g_in.navPoint = Around(g_player, 60.0f);
    g_in.playing = Next();
    g_in.respawnAvailable = Chance(60);
    g_in.posCheck = Chance(60);
    g_in.spawnFound = Chance(70);
    g_in.inFrustum = Chance(50);
    g_in.greenLight = Chance(50);
    g_in.spawnSegment = int16_t(RandomSegment());
    g_in.spawnLane = int8_t(Range(-3, 3));
    g_in.spawnAlong = Uniform(0.0f, 50.0f);
    g_in.spawnForwards = Chance(50);
    for (float &f : g_in.f)
        f = Uniform(-50.0f, 150.0f);
    for (int &i : g_in.i)
        i = Range(-3, 20);
    for (bool &b : g_in.b)
        b = Chance(50);
    for (Coord3 &point : g_in.points)
        point = Around(g_player, Chance(30) ? 150.0f : 40.0f);
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
    // Lane offsets on the halves too, where the rounding turns
    nav->laneOffset = Chance(25) ? float(Range(-30, 30)) + 0.5f : Chance(20) ? float(Range(-3, 3))
                                                                              : Uniform(-30.0f, 30.0f);
    nav->laneChangeTo = Chance(80) ? nav->laneOffset : Uniform(-30.0f, 30.0f);
    nav->atDeadEnd = uint8_t(Chance(15));
}

void MakeBody(int i) {
    RigidBody *body = Body(i);
    RigidBodyInfo *info = reinterpret_cast<RigidBodyInfo *>(g_infos[i]);
    memcpy(body, g_bodyTemplate, sizeof(RigidBody));
    memcpy(info, g_infoTemplate, sizeof(RigidBodyInfo));
    body->info = info;
    body->position = Around(i == 0 ? g_player : Body(0)->position, i == 0 ? 20.0f : 40.0f);
    body->velocity = {Uniform(-40.0f, 40.0f), Uniform(-3.0f, 3.0f), Uniform(-40.0f, 40.0f)};
    if (Chance(15))
        body->velocity = {Uniform(-1.5f, 1.5f), 0.0f, Uniform(-1.5f, 1.5f)};
    body->mass = Uniform(500.0f, 3000.0f);
    body->kind = int8_t(Chance(10) ? 4 : 1);
    body->sleepState = uint8_t(Chance(10) ? RigidBody::kFrozen : Range(1, 2));
    body->ownerIndex = int8_t(Range(0, 40));
    Coord3 forward = UnitVector();
    if (Chance(60)) {
        forward.y *= 0.1f;
        float length = sqrtf(forward.x * forward.x + forward.y * forward.y + forward.z * forward.z);
        forward.x /= length;
        forward.y /= length;
        forward.z /= length;
    }
    Coord3 side = {forward.z, 0.0f, -forward.x};
    float length = sqrtf(side.x * side.x + side.z * side.z);
    if (length > 0.01f) {
        side.x /= length;
        side.z /= length;
    }
    Coord3 up = {forward.y * side.z - forward.z * side.y, forward.z * side.x - forward.x * side.z,
                 forward.x * side.y - forward.y * side.x};
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            info->orientation.mtx[r][c] = r == c ? 1.0f : 0.0f;
    const Coord3 *rows[3] = {&side, &up, &forward};
    for (int r = 0; r < 3; r++) {
        info->orientation.mtx[r][0] = rows[r]->x;
        info->orientation.mtx[r][1] = rows[r]->y;
        info->orientation.mtx[r][2] = rows[r]->z;
    }
    info->halfExtents = {Uniform(0.8f, 2.0f), Uniform(0.5f, 1.5f), Uniform(1.5f, 6.0f), 0.0f};
    if (Chance(10))
        info->halfExtents.x = info->halfExtents.z;
    for (int axis = 0; axis < 3; axis++)
        info->zone[axis] = i == 0 ? Range(10, 20) : reinterpret_cast<RigidBodyInfo *>(g_infos[0])->zone[axis] +
                                                        Range(-4, 4);
}

void MakeCar(int i) {
    RandomBytes(g_cars[i], sizeof(g_cars[i]));
    PhysicsObject *car = CarOf(i);
    car->vtable = g_carVtable;
    car->flags = 0;
    car->rigidBodySlot = kBodySlots[i];
    car->renderObject = Chance(90) ? reinterpret_cast<RSceneObj *>(g_sceneBlock) : NULL;
    car->audioObject = reinterpret_cast<ABaseSound *>(g_sounds[i]);
    car->hitPointLoc = Chance(85) ? &g_hitPoints[i] : NULL;
    g_hitPoints[i] = Chance(25) ? Near(Chance(50) ? 0.0f : 0.5f) : Uniform(-10.0f, 100.0f);
    memset(g_physics[i], 0, sizeof(g_physics[i]));
    reinterpret_cast<CarPhysics *>(g_physics[i])->subPhysics = Chance(15) ? 1 : 0;
    memset(g_sounds[i], 0, sizeof(g_sounds[i]));
    MakeBody(i);
}

void MakeVehicle(int i) {
    AIGroundVehicle *vehicle = Vehicle(i);
    RandomBytes(vehicle, sizeof(AIGroundVehicle));
    vehicle->vtable = reinterpret_cast<void **>(kAIGroundVehicleVtable);
    vehicle->vehicleType = kAIGroundVehicle;
    vehicle->physicsObject = CarOf(i);
    vehicle->splinePath = reinterpret_cast<AISplinePath *>(g_splinePath);
    WRoadNav **navs[kNavs] = {&vehicle->driveToNav, &vehicle->collNav, &vehicle->laneCollNav, &vehicle->heliCollNav,
                              &vehicle->targetNav};
    for (int n = 0; n < kNavs; n++) {
        *navs[n] = reinterpret_cast<WRoadNav *>(g_navs[i][n]);
        MakeNav(*navs[n]);
    }
    vehicle->active = Chance(85);
    vehicle->commanded = uint8_t(Chance(85));
    vehicle->type = Range(0, 6);
    static const int kNavigateModes[] = {0, 1, 2, 2, 3, 3, 3, 4, 4, 5, 6};
    vehicle->navigateMode = kNavigateModes[Range(0, 10)];
    vehicle->targetMode = Range(0, 1);
    static const int kAttackModes[] = {0, 1, 9, 10, 12, 12, 13, 13, 14, 14, 15};
    vehicle->attackMode = kAttackModes[Range(0, 10)];
    vehicle->targetSpeed = Chance(15) ? Near(0.05f) : Uniform(0.0f, 60.0f);
    vehicle->road = int16_t(Chance(40) ? -1 : Range(0, fgRoadNetworkData.roadCount > 0 ? fgRoadNetworkData.roadCount - 1
                                                                                       : 0));
    vehicle->skipFullUpdate = Chance(75) ? 0 : 1;
    vehicle->laneSpan = uint8_t(Chance(70) ? Range(3, 4) : Range(0, 9));
    vehicle->shocked = uint8_t(Chance(30));
    vehicle->isSub = uint8_t(Chance(20));
    vehicle->followTargetHeight = uint8_t(Chance(50));
    vehicle->maxHeight = Uniform(-20.0f, 20.0f);
    vehicle->minHeight = Uniform(-20.0f, 20.0f);
    vehicle->heightPhase = Chance(20) ? Near(1.0f) : Uniform(0.0f, 1.0f);
    vehicle->heightSwing = Chance(30) ? 0.0f : Uniform(-1.0f, 10.0f);
    vehicle->hasTurret = uint8_t(Chance(40));
    vehicle->useGuns = uint8_t(Chance(50));
    vehicle->useRockets = uint8_t(Chance(50));
    vehicle->useMissiles = uint8_t(Chance(50));
    vehicle->reversing = uint8_t(Chance(10) ? 2 : Range(0, 1));
    vehicle->chargeSteps = Chance(50) ? 0 : Range(1, 100);
    vehicle->useLanes = uint8_t(Range(0, 1));
    vehicle->airSteps = Chance(70) ? 0 : Range(1, 100);
    vehicle->autoDriveSteering = uint8_t(Chance(20));
    vehicle->zoneStopSteps = int16_t(Chance(70) ? 0 : Range(1, 60));
    vehicle->stopped = uint8_t(Chance(10));
    vehicle->minSimUpdate = uint8_t(Chance(30));
    vehicle->stopping = uint8_t(Chance(30));
    vehicle->atRedLight = uint8_t(Chance(30));
    vehicle->heldStill = uint8_t(Chance(30));
    vehicle->newCommand = uint8_t(Chance(30));
    vehicle->target = NULL;
    vehicle->goal = Around(g_player, 150.0f);
    vehicle->goalInSight = uint8_t(Chance(70));
    vehicle->goalDistance = Chance(20) ? Uniform(0.0f, 5.0f) : Uniform(0.0f, 250.0f);
    vehicle->goalDirection = UnitVector();
    vehicle->steerPoint = Around(g_player, 60.0f);
    vehicle->secondaryTarget = Chance(30) ? (void *)g_pathInstance : NULL;
    vehicle->goalHidden = Chance(60) ? 0 : Range(1, 2);
    vehicle->weaponsEnabled = uint8_t(Chance(85));
    vehicle->gunSteps = int16_t(Chance(50) ? 0 : Range(-1, 5));
    vehicle->fireDelay = Chance(15) ? Near(0.0f) : Uniform(-0.5f, 3.0f);
    vehicle->rocketSteps = int16_t(Chance(50) ? 0 : Range(-1, 5));
    vehicle->rocketSide = uint8_t(Chance(10) ? 2 : Range(0, 1));
    vehicle->missileSteps = int16_t(Chance(50) ? 0 : Range(-1, 5));
    vehicle->missileSide = uint8_t(Chance(10) ? 2 : Range(0, 1));
    vehicle->turretOnTarget = uint8_t(Chance(10) ? 2 : Range(0, 1));
    vehicle->stuckEventStep = int16_t(Chance(50) ? 0 : Range(-2, ShadowStepsPerSecond + 2));
    vehicle->stuckEvent = uint8_t(Range(0, 1));
    vehicle->maySpawn = uint8_t(Chance(30));
    vehicle->spawnStep = ShadowStepCount + Range(-30, 30);
}

void MakeWorld() {
    RandomInputs();
    memset(&g_out, 0, sizeof(g_out));
    for (int i = 0; i < kVehicles; i++)
        MakeCar(i);
    memset(g_sceneBlock, 0, sizeof(g_sceneBlock));
    *reinterpret_cast<void **>(g_sceneBlock + 0x2c) = Chance(85) ? g_handleBlock : NULL;
    memset(g_handleBlock, 0, sizeof(g_handleBlock));
    RandomBytes(g_collNav, sizeof(g_collNav));
    reinterpret_cast<WRoadNav *>(g_collNav)->laneOffset =
        Chance(30) ? float(Range(-30, 30)) + 0.5f : Chance(20) ? float(Range(-3, 3)) : Uniform(-30.0f, 30.0f);

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

    for (int i = 0; i < kVehicles; i++)
        MakeVehicle(i);
    AIGroundVehicle *self = Vehicle(0);
    self->target = Chance(75) ? Vehicle(1) : NULL;
    for (int i = 1; i < kVehicles; i++)
        Vehicle(i)->target = Chance(40) ? self : NULL;
    if (Chance(30))
        Vehicle(Range(1, 3))->type = kAIGroundPlayer;

    // The Simulation's car list: our cars, in a random order
    g_carCount = Range(1, kVehicles);
    for (int i = 0; i < kVehicles; i++)
        g_carList[i] = CarOf(i);
    for (int i = kVehicles - 1; i > 0; i--) {
        int j = Range(0, i);
        PhysicsObject *swap = g_carList[i];
        g_carList[i] = g_carList[j];
        g_carList[j] = swap;
    }
    ShadowCars.first = g_carList;
    ShadowCars.last = g_carList + g_carCount;

    ShadowSpawnCount = int16_t(Chance(20) ? 0 : Range(-2, 30));
    ShadowMaxTraffic = int16_t(Range(0, 30));
    glbMissionManager->autoDrive = uint8_t(Chance(40));
}

void BuildRegions() {
    g_regions.clear();
    AddRegion(g_vehicleBlocks, sizeof(g_vehicleBlocks), "the vehicles");
    AddRegion(g_cars, sizeof(g_cars), "the cars");
    AddRegion(g_bodies, sizeof(g_bodies), "the bodies");
    AddRegion(g_infos, sizeof(g_infos), "the bodies' infos");
    AddRegion(g_navs, sizeof(g_navs), "the navigators");
    AddRegion(g_collNav, sizeof(g_collNav), "the collision navigator");
    AddRegion(g_sounds, sizeof(g_sounds), "the sounds");
    AddRegion(g_sceneBlock, sizeof(g_sceneBlock), "the scene object");
    AddRegion(g_handleBlock, sizeof(g_handleBlock), "the animation handle");
    AddRegion(g_physics, sizeof(g_physics), "the car physics");
    AddRegion(g_splinePath, sizeof(g_splinePath), "the spline path");
    AddRegion(g_pathHandle, sizeof(g_pathHandle), "the path handle");
    AddRegion(g_pathInstance, sizeof(g_pathInstance), "the path's instance");
    AddRegion(g_hitPoints, sizeof(g_hitPoints), "the hit points");
    AddRegion(g_carList, sizeof(g_carList), "the car list", false);
    AddRegion(&ShadowCars, sizeof(GameVector<PhysicsObject *>), "the Simulation's car list", false);
    AddRegion(&ShadowSpawnCount, 2, "the traffic spawn count");
    AddRegion(&ShadowMaxTraffic, 2, "the traffic limit", false);
    AddRegion(&glbMissionManager->autoDrive, 1, "the mission manager's auto-drive", false);
    AddRegion(&ShadowStepCount, 4, "SimStepCount", false);
    AddRegion(&fgRoadNetworkData, sizeof(WRoadNetworkData), "the road network's statics");
    if (fgRoadNetworkData.loaded)
        for (int r = 0; r < fgRoadNetworkData.roadCount; r++)
            AddRegion(fgRoadNetworkData.roads[r], sizeof(WRoad), "a road");
    AddRegion(&g_out, sizeof(g_out), "the answers");
    AddRegion(&g_in, sizeof(g_in), "the inputs", false);
}

// ---- the cases: each runs the original or the port by g_original

AIGroundVehicle *Self() {
    return Vehicle(0);
}

void CaseGetLookAhead() {
    float speed = g_in.f[0];
    g_out.d = g_original ? Orig_GetLookAhead(Self(), 0, speed) : Self()->GetLookAhead(speed);
}

void CaseDriveToPoint() {
    g_out.point = g_in.points[0];
    float speed = Chance(20) ? Near(0.05f) : Uniform(-5.0f, 60.0f);
    switch (g_in.i[1] % 3) {
    case 0:
        g_original ? Orig_DriveToPoint(Self(), 0, &g_out.point, speed) : Self()->DriveToPoint(&g_out.point, speed);
        break;
    case 1:
        g_original ? Orig_DriveToPointTraffic(Self(), 0, &g_out.point, speed)
                   : Self()->DriveToPointTraffic(&g_out.point, speed);
        break;
    default:
        g_original ? Orig_DriveToPointSub(Self(), 0, &g_out.point, speed) : Self()->DriveToPointSub(&g_out.point, speed);
        break;
    }
}

void CaseStopPoint() {
    g_out.point = g_in.points[0];
    PhysicsObject *object = CarOf(g_in.i[0] & 3);
    float reach = Chance(30) ? 0.0f : Uniform(0.0f, 6.0f);
    if (g_original)
        Orig_GetCollisionStopPoint(object, &g_out.point, &g_in.points[1], reach);
    else
        GetCollisionStopPoint(object, &g_out.point, &g_in.points[1], reach);
}

void CaseInCollisionRange() {
    Coord3 heading = UnitVector();
    float speed = Chance(20) ? Near(10.0f) : Uniform(0.0f, 30.0f);
    float otherSpeed = Chance(30) ? 0.0f : Chance(20) ? Near(speed + 1.0f) : Uniform(-5.0f, 40.0f);
    const Coord3 *to = Chance(50) ? &Body(1)->position : &g_in.points[1];
    g_out.b = g_original ? Orig_InCollisionRange(&g_in.points[0], to, &heading, speed, otherSpeed, g_in.b[0])
                         : InCollisionRange(&g_in.points[0], to, &heading, speed, otherSpeed, g_in.b[0]);
}

void CaseClearShot() {
    int slot = Chance(70) ? kBodySlots[g_in.i[0] & 3] : 0;
    Coord3 heading = UnitVector();
    if (Chance(50)) {
        // Straight at another car
        Coord3 to = Body(Range(1, 3))->position;
        Coord3 from = Body(0)->position;
        heading = {to.x - from.x, to.y - from.y, to.z - from.z};
        float length = sqrtf(heading.x * heading.x + heading.y * heading.y + heading.z * heading.z);
        if (length > 0.01f) {
            heading.x /= length;
            heading.y /= length;
            heading.z /= length;
        }
    }
    float range = Uniform(0.0f, 120.0f);
    g_out.b = g_original ? Orig_ClearShot(slot, &heading, &Body(0)->position, range)
                         : ClearShot(slot, &heading, &Body(0)->position, range);
}

void CaseWeapons() {
    switch (g_in.i[1] & 3) {
    case 0:
        g_original ? Orig_FireBullets(Self(), 0) : Self()->FireBullets();
        break;
    case 1:
        g_original ? Orig_FireRockets(Self(), 0) : Self()->FireRockets();
        break;
    case 2:
        g_original ? Orig_FireMissiles(Self(), 0) : Self()->FireMissiles();
        break;
    default:
        g_original ? Orig_DoWeaponUpdate(Self(), 0) : Self()->DoWeaponUpdate();
        break;
    }
}

void CaseSpawn() {
    int16_t limit = int16_t(Range(-1, 30));
    if (g_original)
        Orig_Spawn(Self(), 0, limit);
    else
        Self()->Spawn(limit);
}

void CaseTrafficState() {
    if (g_original)
        Orig_DoTrafficStateUpdate(Self(), 0);
    else
        Self()->DoTrafficStateUpdate();
}

void CaseRoadNetworkCollision() {
    g_out.point = g_in.points[0];
    if (g_original)
        Orig_CheckAgentRoadNetworkCollision(Self(), 0, &g_out.point);
    else
        Self()->CheckAgentRoadNetworkCollision(&g_out.point);
}

void CaseDirectCollision() {
    g_out.point = g_in.points[0];
    if (g_original)
        Orig_CheckAgentDirectCollision(Self(), 0, &g_out.point);
    else
        Self()->CheckAgentDirectCollision(&g_out.point);
}

void CaseTrafficCollision() {
    g_out.point = g_in.points[0];
    if (g_original)
        Orig_CheckTrafficCollision(Self(), 0, &g_out.point);
    else
        Self()->CheckTrafficCollision(&g_out.point);
}

void CaseDoMoveMode() {
    if (g_original)
        Orig_DoMoveMode(Self(), 0);
    else
        Self()->DoMoveMode();
}

void CaseUpdate() {
    if (g_original)
        Orig_Update(Self(), 0);
    else
        Self()->Update();
}

void CaseRightDrivable() {
    WRoadNav *nav = Self()->driveToNav;
    g_out.d = g_original ? Orig_RightDrivableDist(nav, 0) : nav->RightDrivableDist();
}

// ---- the preparations

void PrepareNothing(int) {
}

// The target there where the original reads it unchecked: a sub following a height
void PrepareDrive(int i) {
    AIGroundVehicle *self = Self();
    if (self->isSub && self->followTargetHeight && self->target == NULL)
        self->target = Vehicle(1);
    // The reverse countdown's edge, and the chase let-off
    if (i % 4 == 0)
        self->stuckEventStep = int16_t(ShadowStepsPerSecond / 2 + Range(-1, 1));
    if (i % 5 == 0) {
        self->target = Vehicle(1);
        self->attackMode = kGroundAttackChase;
        g_in.carClass[1] = kCarClass1;
        Body(1)->position = Around(Body(0)->position, 12.0f);
    }
}

void PrepareWeapons(int) {
    AIGroundVehicle *self = Self();
    if (Chance(70)) {
        self->target = Vehicle(1);
        self->goalHidden = 0;
        self->goalInSight = 1;
    }
    // Facing the goal often enough to fire
    if (Chance(50))
        self->goalDirection = *Xyz(MatrixRow(&Body(0)->info->orientation, 2));
    if (Chance(40))
        self->turretOnTarget = 1;
}

void PrepareTraffic(int) {
    // A valid segment under the navigator (the road counts read it)
    Self()->driveToNav->segment = int16_t(RandomSegment());
    if (Chance(50))
        Self()->attackMode = kGroundAttack12;
}

void PrepareCollision(int i) {
    AIGroundVehicle *self = Self();
    // The others close by, some ahead
    for (int v = 1; v < kVehicles; v++)
        Body(v)->position = Around(Body(0)->position, i % 2 == 0 ? 15.0f : 40.0f);
    if (Chance(40)) {
        self->target = Vehicle(Range(1, 3));
        self->goalHidden = 0;
    }
    if (Chance(50))
        self->useLanes = 1;
    if (Chance(50))
        self->driveToNav->laneChangeTo = self->driveToNav->laneOffset;
    if (!g_junctionSegments.empty() && Chance(60))
        self->driveToNav->segment = g_junctionSegments[Range(0, int(g_junctionSegments.size()) - 1)];
    else
        self->driveToNav->segment = SafeSegment();
}

void PrepareMove(int i) {
    PrepareDrive(i);
    PrepareCollision(i);
    // Not stopped, more often than not
    if (Chance(70)) {
        g_hitPoints[0] = Uniform(1.0f, 100.0f);
        Self()->stopped = 0;
        g_in.emp[0] = 0;
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

void GameAIShadowB_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEAISHADOWB", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    GameVector<PhysicsObject *> &cars = ShadowCars;
    g_playerCar = cars.first != NULL && cars.last != cars.first ? static_cast<PVehicle *>(cars.first[0]) : NULL;
    if (g_playerCar == NULL || glbMissionManager == NULL) {
        printf("[gameaishadowb] no player car or mission manager at the first tick - nothing run\n");
        fflush(stdout);
        return;
    }
    if (!fgRoadNetworkData.loaded || fgRoadNetworkData.segmentCount <= 0) {
        printf("[gameaishadowb] no road network at the first tick - nothing run\n");
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
    FindJunctionSegments();
    printf("[gameaishadowb] live at the first tick: %d cars, the player's AI type %d, %d road segments, %d at "
           "junctions\n", int(cars.last - cars.first), liveAI->type, fgRoadNetworkData.segmentCount,
           int(g_junctionSegments.size()));

    BuildVtable();
    BuildRegions();
    Snapshot before;
    Capture(&before);
    if (!InstallHooks()) {
        RemoveHooks();
        printf("[gameaishadowb] fakes missing - nothing run\n");
        fflush(stdout);
        return;
    }

    Run("AIGroundVehicle::GetLookAhead", 200, PrepareNothing, CaseGetLookAhead, before);
    Run("AIGroundVehicle's DriveToPoint, DriveToPointTraffic, DriveToPointSub", 900, PrepareDrive, CaseDriveToPoint,
        before);
    Run("GetCollisionStopPoint", 200, PrepareNothing, CaseStopPoint, before);
    Run("InCollisionRange", 300, PrepareNothing, CaseInCollisionRange, before);
    Run("ClearShot", 300, PrepareNothing, CaseClearShot, before);
    Run("AIGroundVehicle's weapons", 800, PrepareWeapons, CaseWeapons, before);
    Run("AIGroundVehicle::Spawn", 300, PrepareTraffic, CaseSpawn, before);
    Run("AIGroundVehicle::DoTrafficStateUpdate", 300, PrepareTraffic, CaseTrafficState, before);
    Run("AIGroundVehicle::CheckAgentRoadNetworkCollision", 500, PrepareCollision, CaseRoadNetworkCollision, before);
    Run("AIGroundVehicle::CheckAgentDirectCollision", 400, PrepareCollision, CaseDirectCollision, before);
    Run("AIGroundVehicle::CheckTrafficCollision", 500, PrepareCollision, CaseTrafficCollision, before);
    Run("AIGroundVehicle::DoMoveMode", 800, PrepareMove, CaseDoMoveMode, before);
    Run("AIGroundVehicle::Update", 400, PrepareMove, CaseUpdate, before);
    Run("WRoadNav::RightDrivableDist", 200, PrepareTraffic, CaseRightDrivable, before);

    Restore(before);
    RemoveHooks();
    ResetFpu();
    printf("[gameaishadowb] AIGroundVehicle part 2 vs originals: %ld cases, %ld checks, %ld differ%s\n", g_cases,
           g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameaishadowb]   %ld calls faulted\n", g_faults);
    fflush(stdout);
}
