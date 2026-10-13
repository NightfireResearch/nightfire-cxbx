#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAIShadowD.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "../data/AttributeSet.h"
#include "../data/Carp.h"
#include "../engine/CoreContainers.h"
#include "../game/MissionManager.h"
#include "../engine/SimRandom.h"
#include "../engine/StaticInit.h"
#include "../game/AIGroundVehicle.h"
#include "../game/AIHelicopter.h"
#include "../game/AIRoadSpawn.h"
#include "../game/AISplinePath.h"
#include "../game/AIVehicle.h"
#include "../game/AIVehicleController.h"
#include "../game/Helicopter.h"
#include "../game/Vehicle.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../game/Simulation.h"
#include "../render/PathEngine.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../world/Targeting.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.ai's package D, run once from the first simulation tick when NIGHTFIRE_GAMEAISHADOWD is set.
// Each case runs the ORIGINAL (every entry of the package swapped back together) and then the PORT from the same
// state, and compares what they leave - the synthetic vehicles and everything round them, the controller's and the
// road spawn's statics (ours, mirrored to the game's addresses round the original's run), the live player and
// traffic AI records (their vtables swapped for logging fakes during the cases), the road records' query stamps and
// counts, the spawn navigator, the Simulation's random generator - the answers, and a log of every call out:
//
//   - CARP::Instance's matrix setter, AISplinePath (all), AIVehicle (all), AIGroundVehicle's vector deleting
//     destructor (every flag);
//   - AIHelicopter: everything, on a synthetic helicopter (its PHelicopter, simple body, scene object, sound,
//     navigators and spline path synthetic) chasing the live player car, every navigate and attack mode, random
//     commands, ranges, speeds, heights, hit points, delays and edge values;
//   - AIVehicleController: everything, the agent cars and helicopters synthetic arrays and the AI elements' counts a
//     record of our own, the cars registered synthetic ones;
//   - AIRoadSpawn: everything but the length error, over the live road network and grid, the player car, the camera
//     and the mission manager's auto-drive and cinematic flags perturbed.
//
// Fakes (5-byte jumps at the callee both sides reach): the navigators' methods, the world position's face search and
// height, the collider, the animation handle's stimuli, the target beacon's calls, the allocators (a bump arena),
// the path engine's handles, the attribute lookups, PreLoad, AssertMessage, and the originals of other packages
// (AIGroundVehicle's, the AI elements', the mission manager's, the Simulation's car spawn, the random fraction, the
// C runtime's vector iterators and atexit). The Simulation's body getters, the VU0 maths, SimRandom, Util_Step,
// WGrid::FindNodes and the road network's drivable widths run for real on both sides.
//
// A mutation the test sees: GetTrafficSpawnPointOnSegment's left lane written `rightLanes - pick` (without the
// -1) - every pick past the right lanes answers another lane.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x00035610, 0x00035680, 0x00035730, 0x00035740, 0x00035780,
    0x000357b0, 0x00035840, 0x00035850, 0x00035860, 0x00035930, 0x000359d0, 0x000359e0, 0x00035a20, 0x00035b40,
    0x00035b90, 0x00035d90,
    0x00031850, 0x000318d0, 0x00031910, 0x00031990, 0x00032a20, 0x00031a50, 0x000324d0, 0x00031cd0, 0x00032af0,
    0x00032700, 0x000331c0, 0x00034600, 0x00031c80, 0x00031ca0, 0x00032280, 0x00034760, 0x00032a60, 0x000321d0,
    0x00035df0,
    0x00035c00, 0x00035c10, 0x00035e50, 0x00035e70, 0x00035e90, 0x00035ef0, 0x00035f10, 0x00035f70, 0x00035fb0,
    0x00035ff0, 0x00036060, 0x00036140, 0x00036180, 0x000361c0, 0x00036270, 0x000362e0, 0x000363f0, 0x000364d0,
    0x000365a0, 0x000365f0, 0x00036660, 0x000366a0, 0x000366e0, 0x00036990,
    0x000347e0, 0x000347f0, 0x00034830, 0x000349e0, 0x00034860, 0x00034960, 0x00034a00, 0x00034a70, 0x00034bb0,
    0x00034d30, 0x00034ed0, 0x00035130, 0x00035470, 0x00034e50,
};

typedef void (__fastcall *MethodFn)(void *, int);
typedef void (__fastcall *PtrArgFn)(void *, int, const void *);
typedef void *(__fastcall *PtrMethodFn)(void *, int);
typedef void *(__fastcall *FlagsFn)(void *, int, unsigned);
typedef bool (__fastcall *BoolMethodFn)(void *, int);

#define Orig_SetMatrix ((PtrArgFn)0x00035610)
#define Orig_SplineConstruct ((void *(__fastcall *)(void *, int, CARP::AISpline *))0x00035680)
#define Orig_SplineDestruct ((MethodFn)0x00035730)
#define Orig_SplineReset ((void (__fastcall *)(void *, int, CARP::AISpline *))0x00035740)
#define Orig_SplineSetNext ((void (__fastcall *)(void *, int, CARP::AISpline *))0x00035780)
#define Orig_VehicleConstruct ((PtrMethodFn)0x000357b0)
#define Orig_GetPhysicsObject ((PtrMethodFn)0x00035840)
#define Orig_SetPhysicsObject ((PtrArgFn)0x00035850)
#define Orig_SetSplinePath ((void (__fastcall *)(void *, int, CARP::AISpline *))0x00035860)
#define Orig_SetNextSplinePath ((void (__fastcall *)(void *, int, CARP::AISpline *))0x00035930)
#define Orig_GetSplinePath ((PtrMethodFn)0x000359d0)
#define Orig_GetPosition ((PtrMethodFn)0x000359e0)
#define Orig_GetRotPos ((PtrMethodFn)0x00035a20)
#define Orig_VehicleDestruct ((MethodFn)0x00035b40)
#define Orig_VehicleDelete ((FlagsFn)0x00035b90)
#define Orig_GroundDelete ((FlagsFn)0x00035d90)
#define Orig_EnableBeacon ((void (__fastcall *)(void *, int, int))0x00031850)
#define Orig_DisableBeacon ((MethodFn)0x000318d0)
#define Orig_DirectionToTarget ((PtrArgFn)0x00031910)
#define Orig_LaneOffsets ((PtrMethodFn)0x00031990)
#define Orig_LookAhead ((double (__fastcall *)(void *, int, float, float))0x00032a20)
#define Orig_FireRockets ((void (__fastcall *)(void *, int, float, const Coord3 *, const Coord3 *, const Coord3 *))0x00031a50)
#define Orig_FireBullets ((void (__fastcall *)(void *, int, float, float, const Coord3 *, const Coord3 *, Coord3 *))0x000324d0)
#define Orig_Maneuver ((void (__fastcall *)(void *, int, Coord3 *, float, float, bool))0x00031cd0)
#define Orig_ControlMove ((void (__fastcall *)(void *, int, bool, bool))0x00032af0)
#define Orig_NewState ((void (__fastcall *)(void *, int, AICommand *))0x00032700)
#define Orig_AgentLogic ((MethodFn)0x000331c0)
#define Orig_ProximityDestruct ((BoolMethodFn)0x00034600)
#define Orig_Engage ((void (__fastcall *)(void *, int, CARP::AISpline *))0x00031c80)
#define Orig_Disengage ((MethodFn)0x00031ca0)
#define Orig_HeliReset ((MethodFn)0x00032280)
#define Orig_HeliUpdate ((MethodFn)0x00034760)
#define Orig_HeliConstruct ((PtrMethodFn)0x00032a60)
#define Orig_HeliDestruct ((MethodFn)0x000321d0)
#define Orig_HeliDelete ((FlagsFn)0x00035df0)
#define Orig_ControllerGet ((void *(*)(void))0x00035c00)
#define Orig_ControllerInit ((void (*)(void))0x00035c10)
#define Orig_GetTraffic ((void *(__fastcall *)(void *, int, int))0x00035e50)
#define Orig_GetPlayer ((void *(__fastcall *)(void *, int, int))0x00035e70)
#define Orig_FindAgent ((void *(__fastcall *)(void *, int, PhysicsObject *))0x00035e90)
#define Orig_GetHeli ((void *(__fastcall *)(void *, int, int))0x00035ef0)
#define Orig_UpdateAgents ((MethodFn)0x00035f10)
#define Orig_UpdateTraffic ((MethodFn)0x00035f70)
#define Orig_UpdateHelis ((MethodFn)0x00035fb0)
#define Orig_UpdateVehicles ((MethodFn)0x00035ff0)
#define Orig_GreenLight ((bool (__fastcall *)(void *, int, const Coord3 *, uint32_t, uint32_t))0x00036060)
#define Orig_ApproveAgent ((PtrMethodFn)0x00036140)
#define Orig_ApproveHeli ((PtrMethodFn)0x00036180)
#define Orig_RegisterPlayer ((PtrArgFn)0x000361c0)
#define Orig_RegisterAgent ((PtrArgFn)0x00036270)
#define Orig_RegisterTraffic ((PtrArgFn)0x000362e0)
#define Orig_ControllerReset ((void (*)(void))0x000363f0)
#define Orig_ControllerShutdown ((void (*)(void))0x000364d0)
#define Orig_RegisterSimCar ((PtrArgFn)0x000365a0)
#define Orig_DeRegisterSimCar ((PtrArgFn)0x000365f0)
#define Orig_RegisterHeli ((PtrArgFn)0x00036660)
#define Orig_DeRegisterHeli ((PtrArgFn)0x000366a0)
#define Orig_CreateTraffic ((void (__fastcall *)(void *, int, bool))0x000366e0)
#define Orig_RegisterList ((MethodFn)0x00036990)
#define Orig_SpawnGet ((void *(*)(void))0x000347e0)
#define Orig_SpawnInit ((void (*)(void))0x000347f0)
#define Orig_SpawnRestart ((void (*)(void))0x00034830)
#define Orig_SpawnShutdown ((void (*)(void))0x000349e0)
#define Orig_TrafficPoint ((void (__fastcall *)(void *, int, short *, int8_t *, float *, bool *))0x00034860)
#define Orig_PedPoint ((void (__fastcall *)(void *, int, short *, int8_t *, float *))0x00034960)
#define Orig_TrafficPosCheck ((bool (__fastcall *)(void *, int, const Coord3 *, float))0x00034a00)
#define Orig_TrafficAvailable ((bool (__fastcall *)(void *, int, const Coord3 *, float))0x00034a70)
#define Orig_PedPosCheck ((bool (__fastcall *)(void *, int, const Coord3 *))0x00034bb0)
#define Orig_PedAvailable ((bool (__fastcall *)(void *, int, const Coord3 *))0x00034d30)
#define Orig_Refresh ((void (__fastcall *)(void *, int, bool, float, float))0x00034ed0)
#define Orig_RandomTraffic ((bool (__fastcall *)(void *, int, short *, int8_t *, float *, bool *))0x00035130)
#define Orig_RandomPed ((bool (__fastcall *)(void *, int, short *, int8_t *, float *, bool))0x00035470)

// ---- the game's state

#define ShadowSim ((void *)0x00233ff0)
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowStepCount I32_AT(0x00234e34)
#define ShadowCars (*(GameVector<PVehicle *> *)0x00234e3c)
#define ShadowElements PTR_AT(OwnedGlobals_Ours(0x001ddee4))
#define ShadowForceRespawn BOOL8_AT(OwnedGlobals_Ours(0x001de814))
#define ShadowSpawnNav ((uint8_t *)OwnedGlobals_Ours(0x001de820))
#define ShadowGetSimpleBody ((SimpleRigidBody *(__fastcall *)(void *, int, int))0x000b2730)
#define ShadowStaticVehicles ((uint8_t *)0x001de940)
constexpr uint32_t kStaticVehicleBytes = 0x001e1ee0 - 0x001de940;

// Our statics and the game's copies the original uses
struct Mirror {
    void *ours;
    uint32_t game;
    uint32_t size;
    const char *name;
};

const Mirror kMirrors[] = {
    {&PlayerLaneOffsets, 0x001ddf10, 0xc, "PlayerLaneOffsets"},
    {&GreenLightCycle, 0x001de8f0, 4, "GreenLightCycle"},
    {&NextGreenLightStep, 0x001de8f4, 4, "NextGreenLightStep"},
    {TrafficCarTypes, 0x001de8f8, 0x10, "TrafficCarTypes"},
    {&AIHelicopters, 0x001de908, 4, "AIHelicopters"},
    {&AgentGroundVehicles, 0x001de90c, 4, "AgentGroundVehicles"},
    {&AIVehicleControllerInstance, 0x001de910, 4, "the controller"},
    {&TrafficCarTypesChosen, 0x001de920, 2, "TrafficCarTypesChosen"},
    {&AgentUpdateTurn, 0x001e1ee0, 4, "AgentUpdateTurn"},
    {SpawnSegments, 0x001de738, 0xc8, "SpawnSegments"},
    {&NextSpawnRefreshStep, 0x001de800, 4, "NextSpawnRefreshStep"},
    {&AIRoadSpawnInstance, 0x001de804, 4, "the road spawn"},
    {&SpawnSegmentCount, 0x001de808, 4, "SpawnSegmentCount"},
    {&SpawnRestarted, 0x001de80c, 1, "SpawnRestarted"},
    {&TrafficLaneTurn, 0x001de810, 4, "TrafficLaneTurn"},
    {&SpawnNavMade, 0x001de8e0, 4, "SpawnNavMade"},
    {&SpawnNearRadius, 0x001b6614, 4, "SpawnNearRadius"},
    {&SpawnFarRadius, 0x001b6618, 4, "SpawnFarRadius"},
};
constexpr int kMirrorCount = int(sizeof(kMirrors) / sizeof(kMirrors[0]));
uint8_t g_gameCopies[kMirrorCount][0xc8];

void MirrorIn() {
    for (const Mirror &m : kMirrors)
        memcpy((void *)uintptr_t(m.game), m.ours, m.size);
}

void MirrorOut() {
    for (const Mirror &m : kMirrors)
        memcpy(m.ours, (const void *)uintptr_t(m.game), m.size);
}

// A pointer the original answers at the game's copy of one of our statics, as ours
const void *Translate(const void *p) {
    for (const Mirror &m : kMirrors)
        if (uintptr_t(p) >= m.game && uintptr_t(p) < m.game + m.size)
            return static_cast<const uint8_t *>(m.ours) + (uintptr_t(p) - m.game);
    return p;
}

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults, g_skipped;
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
        printf("[gameaishadowd]   %s #%ld: %s\n", what, index, detail);
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

uint32_t g_seed = 0x0a1d0d0d;

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
    Log(uint32_t(uintptr_t(Translate(p))));
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

// A code pointer handed to the C runtime: ours as the original's
uint32_t CodeAddress(const void *p) {
    uintptr_t at = uintptr_t(p);
    if (at == XbeAddress(&AIHelicopter::Construct))
        return 0x00032a60;
    if (at == XbeAddress(&AIHelicopter::Destruct))
        return 0x000321d0;
    if (at == XbeAddress(&AIGroundVehicle::Construct))
        return 0x0002bd40;
    if (at == XbeAddress(&AIGroundVehicle::Destruct))
        return 0x0002be70;
    if (at == uintptr_t(&DestroyStatic_001de820))
        return 0x0015cd50;
    return uint32_t(at);
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

alignas(16) uint8_t g_arena[0x6000];        // the allocators' fakes hand out blocks from here
uint32_t g_arenaUsed;

alignas(16) uint8_t g_heliBlock[sizeof(AIHelicopter)];
alignas(16) uint8_t g_vehicleBlock[0x10 + sizeof(AIGroundVehicle)];    // a new[] cookie, then the vehicle
alignas(16) uint8_t g_physBlock[sizeof(PHelicopter)];
alignas(16) uint8_t g_soundBlock[0x1f0];
alignas(16) uint8_t g_sceneBlock[0x80];
alignas(16) uint8_t g_handleBlock[0x40];
alignas(16) uint8_t g_navA[sizeof(WRoadNav)];
alignas(16) uint8_t g_navB[sizeof(WRoadNav)];
alignas(16) uint8_t g_collNav[sizeof(WRoadNav)];
alignas(16) uint8_t g_splinePath[sizeof(AISplinePath)];
alignas(16) uint8_t g_pathHandle[sizeof(RPathHandle)];
alignas(16) uint8_t g_pathInstance[sizeof(CARP::Instance)];
alignas(16) uint8_t g_spline[2][sizeof(CARP::AISpline)];
alignas(16) uint8_t g_target[sizeof(AIGroundVehicle)];
alignas(16) uint8_t g_beacon[sizeof(WTargetable)];
alignas(16) AICommand g_command;
alignas(16) uint8_t g_agents[0x10 + 4 * sizeof(AIGroundVehicle)];
alignas(16) uint8_t g_helis[0x10 + 3 * sizeof(AIHelicopter)];
alignas(16) uint8_t g_cars[3][0x270];
alignas(16) uint8_t g_carAI[3][sizeof(AIGroundVehicle)];
alignas(16) uint8_t g_carPhysics[3][0x100];
alignas(16) uint8_t g_carNav[sizeof(WRoadNav)];
float g_heliHitPoints;
struct ElementCounts {
    uint8_t unknown00[4];
    int32_t agents;
    int32_t helicopters;
} g_counts;
PVehicle *g_carList[3];

AIHelicopter *const g_heli = reinterpret_cast<AIHelicopter *>(g_heliBlock);
AIGroundVehicle *const g_vehicle = reinterpret_cast<AIGroundVehicle *>(g_vehicleBlock + 0x10);
PHelicopter *const g_phys = reinterpret_cast<PHelicopter *>(g_physBlock);
AISplinePath *const g_path = reinterpret_cast<AISplinePath *>(g_splinePath);
AIGroundVehicle *const g_agentArray = reinterpret_cast<AIGroundVehicle *>(g_agents + 0x10);
AIHelicopter *const g_heliArray = reinterpret_cast<AIHelicopter *>(g_helis + 0x10);

SimpleRigidBody *g_body;                    // the simple body the synthetic helicopter flies
int g_bodySlot;
PVehicle *g_playerCar;
SMissionManager *g_manager;

void *g_physVtable[8];
void *g_soundVtable[8];
void *g_vehicleVtable[5];
void *g_carVtable[PVehicle::kSlotCount];
const char *const kCarTypes[] = {"bus", "truck", "tnkr", "flatb", "coupe", "BUS", "taxi"};

// One case's inputs and scripted answers
struct Inputs {
    float ground;                   // HeightAtPoint's
    Coord3 navOffset;               // InitAtPoint puts the navigator's road point this far from the point
    Coord3 point;                   // InitAtSegment's road point
    double drivable[2];             // the navigator's right and left drivable widths
    double fractions[8];            // the random fractions, in turn
    int fraction;
    bool forward, sameRoad, hitWorld;
    uint32_t playing;               // the systems IsSystemPlaying says are playing (by bit)
    int lookups[2];                 // NUM_TRAFFIC_TYPES_XBOX, MAX_TRAFFIC
    bool found[2];
    uint8_t turret;
    int carClass[3];
    int carType[3];
    int isSub[3];
    int colours;
    bool noModel;
    float f[6];
    int i[4];
    bool b[3];
    short segment;
    int8_t lane;
    float along;
    bool forwards;
} g_in;

// The answers (compared)
struct Outputs {
    double d;
    const void *p;
    short segment;
    int8_t lane;
    bool b;
    float along;
    bool forwards;
    uint8_t pad[3];
} g_out;

// ---- the fakes

enum : uint32_t {
    kTagNavInitPoint = 0x4e495054, kTagNavReverse = 0x4e525645, kTagNavInc = 0x4e494e43, kTagNavLanes = 0x4e4c4e53,
    kTagNavForward = 0x4e465744, kTagNavReset = 0x4e525354, kTagNavConstruct = 0x4e434f4e,
    kTagNavDestruct = 0x4e444553, kTagNavInitSegment = 0x4e495347, kTagFace = 0x46414345, kTagHeight = 0x48475420,
    kTagCollider = 0x434c4452, kTagNormal = 0x4e524d4c, kTagColliderEnd = 0x43454e44, kTagStimuli = 0x5354494d,
    kTagStimuliAll = 0x53544941, kTagPlaying = 0x504c4159, kTagBeacon = 0x4243484e, kTagRegister = 0x52454720,
    kTagUnregister = 0x554e5247, kTagRelease = 0x524c5345, kTagAlloc = 0x414c4c43, kTagFree = 0x46524545,
    kTagNew = 0x4e455720, kTagDelete = 0x44454c20, kTagNewArray = 0x4e415252, kTagPathCreate = 0x50435254,
    kTagPathDestroy = 0x50445354, kTagPathInit = 0x50494e54, kTagLookupInt = 0x4c4b494e, kTagLookupBool = 0x4c4b424c,
    kTagNamed = 0x4e414d44, kTagRenderName = 0x524e414d, kTagColours = 0x434f4c53, kTagAttrEnd = 0x41454e44,
    kTagAssert = 0x41535254, kTagPreLoad = 0x50524c44, kTagCollNav = 0x434e4156, kTagDrivable = 0x44524956,
    kTagFraction = 0x46524143, kTagSleep = 0x534c4550, kTagEvent = 0x45564e54, kTagInfo = 0x494e464f,
    kTagGroundDestruct = 0x47445354, kTagAttackMode = 0x41544b4d, kTagNavigateMode = 0x4e41564d,
    kTagUnSpawn = 0x554e5350, kTagSpawnCar = 0x53434152, kTagVecCtor = 0x56435452, kTagVecDtor = 0x56445452,
    kTagAtexit = 0x41544558, kTagDamage = 0x444d4745, kTagLanding = 0x4c4e4447, kTagSlot = 0x534c0000,
    kTagCarSlot = 0x43530000,
};

// The navigators
void __fastcall FakeNavInitAtPoint(WRoadNav *nav, int, const Coord3 *point, const Coord3 *heading, bool noLane) {
    Log(kTagNavInitPoint);
    LogPointer(nav);
    LogBytes(point, 12);
    LogBytes(heading, 12);
    Log(noLane);
    nav->roadPoint.x = point->x + g_in.navOffset.x;
    nav->roadPoint.y = point->y + g_in.navOffset.y;
    nav->roadPoint.z = point->z + g_in.navOffset.z;
}

void __fastcall FakeNavReverse(WRoadNav *nav, int) {
    Log(kTagNavReverse);
    LogPointer(nav);
}

void __fastcall FakeNavInc(WRoadNav *nav, int, float step, Coord3 *heading, short target) {
    Log(kTagNavInc);
    LogPointer(nav);
    LogFloat(step);
    LogBytes(heading, 12);
    Log(uint32_t(uint16_t(target)));
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
    *sameRoad = g_in.sameRoad;
    return g_in.forward;
}

void __fastcall FakeNavReset(WRoadNav *nav, int) {
    Log(kTagNavReset);
    LogPointer(nav);
}

WRoadNav* __fastcall FakeNavConstruct(WRoadNav *nav, int) {
    Log(kTagNavConstruct);
    LogPointer(nav);
    return nav;
}

void __fastcall FakeNavDestruct(WRoadNav *nav, int) {
    Log(kTagNavDestruct);
    LogPointer(nav);
}

void __fastcall FakeNavInitSegment(WRoadNav *nav, int, short segment, int8_t lane, float along, bool forwards) {
    Log(kTagNavInitSegment);
    LogPointer(nav);
    Log(uint32_t(uint16_t(segment)));
    Log(uint32_t(uint8_t(lane)));
    LogFloat(along);
    Log(forwards);
    nav->roadPoint = g_in.point;
}

// The world position and the collider
bool __fastcall FakeFace(void *worldPos, int, const Coord3 *point, bool keep) {
    Log(kTagFace);
    LogPointer(worldPos);
    LogBytes(point, 12);
    Log(keep);
    return true;
}

double __fastcall FakeHeight(void *worldPos, int, const Coord3 *point, bool flag) {
    Log(kTagHeight);
    LogPointer(worldPos);
    LogBytes(point, 12);
    Log(flag);
    return g_in.ground;
}

void *__fastcall FakeColliderConstruct(void *collider, int, const Coord4 *sweep, uint32_t mask) {
    Log(kTagCollider);
    LogBytes(sweep, 32);
    Log(mask);
    return collider;
}

bool __fastcall FakeNormal(void *collider, int, const Coord4 *segment, void *info) {
    Log(kTagNormal);
    LogBytes(segment, 32);
    return g_in.hitWorld;
}

void __fastcall FakeColliderDestruct(void *collider, int) {
    Log(kTagColliderEnd);
}

void *__fastcall FakeInfoConstruct(void *info, int) {
    Log(kTagInfo);
    memset(info, 0, 0x60);
    return info;
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

void __fastcall FakeStimuliAll(void *handle, int, uint8_t stimulus, uint32_t tick, int mode) {
    Log(kTagStimuliAll);
    LogPointer(handle);
    Log(stimulus);
    Log(tick);
    Log(uint32_t(mode));
}

bool __fastcall FakePlaying(void *handle, int, uint32_t id) {
    Log(kTagPlaying);
    LogPointer(handle);
    Log(id);
    return id < 32 && (g_in.playing >> id & 1) != 0;
}

// The target beacon
void *__fastcall FakeBeaconConstruct(void *target, int, void *vehicle) {
    Log(kTagBeacon);
    LogPointer(target);
    LogPointer(vehicle);
    return target;
}

void __fastcall FakeRegister(void *picker, int, void *target) {
    Log(kTagRegister);
    LogPointer(picker);
    LogPointer(target);
}

void __fastcall FakeUnregister(void *picker, int, void *target) {
    Log(kTagUnregister);
    LogPointer(picker);
    LogPointer(target);
}

void __fastcall FakeRelease(void *target, int) {
    Log(kTagRelease);
    LogPointer(target);
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

void *FakeNew(unsigned size) {
    Log(kTagNew);
    Log(size);
    return ArenaTake(size);
}

void FakeDelete(void *block) {
    Log(kTagDelete);
    LogPointer(block);
}

void *FakeNewArray(unsigned size) {
    Log(kTagNewArray);
    Log(size);
    return ArenaTake(size);
}

// The path engine
RPathHandle *FakePathCreate(CARP::Instance *instance, void *state, float time) {
    Log(kTagPathCreate);
    LogPointer(instance);
    LogPointer(state);
    LogFloat(time);
    return reinterpret_cast<RPathHandle *>(g_pathHandle);
}

void FakePathDestroy(RPathHandle *handle) {
    Log(kTagPathDestroy);
    LogPointer(handle);
}

void __fastcall FakePathInit(RPathHandle *handle, int, float time) {
    Log(kTagPathInit);
    LogPointer(handle);
    LogFloat(time);
}

// The attributes
int32_t __fastcall FakeLookupInt(AttributeSet *set, int, const char *key, bool *found) {
    Log(kTagLookupInt);
    LogPointer(set);
    LogString(key);
    int which = strcmp(key, "MAX_TRAFFIC") == 0 ? 1 : 0;
    if (found != NULL)
        *found = g_in.found[which];
    return g_in.lookups[which];
}

uint8_t __fastcall FakeLookupBool(AttributeSet *set, int, const char *key, bool *found) {
    Log(kTagLookupBool);
    LogPointer(set);
    LogString(key);
    if (found != NULL)
        *found = true;
    return g_in.turret;
}

AttributeSet *FakeNamed(AttributeSet *result, const char *name) {
    Log(kTagNamed);
    LogString(name);
    result->collection = reinterpret_cast<AttributeCollection *>(uintptr_t(0x5eed0000 | (name[0] & 0xff)));
    return result;
}

const char *FakeRenderName(AttributeSet *attributes) {
    Log(kTagRenderName);
    Log(uint32_t(uintptr_t(attributes->collection)));
    return g_in.noModel ? NULL : "model.crp";
}

uint32_t FakeColours(AttributeSet *attributes) {
    Log(kTagColours);
    Log(uint32_t(uintptr_t(attributes->collection)));
    return uint32_t(g_in.colours);
}

void __fastcall FakeAttrDestruct(AttributeSet *set, int) {
    Log(kTagAttrEnd);
    Log(uint32_t(uintptr_t(set->collection)));
}

void FakeAssert(const char *format, ...) {
    va_list args;
    va_start(args, format);
    const char *name = va_arg(args, const char *);
    va_end(args);
    Log(kTagAssert);
    LogString(format);
    LogString(name);
}

void FakePreLoad(const char *directory, const char *file, const char *label) {
    Log(kTagPreLoad);
    LogString(directory);
    LogString(file);
    LogString(label);
}

// Other packages' originals
WRoadNav *__fastcall FakeCollNav(void *vehicle, int, const Coord3 *heading) {
    Log(kTagCollNav);
    LogPointer(vehicle);
    LogBytes(heading, 12);
    return reinterpret_cast<WRoadNav *>(g_collNav);
}

double __fastcall FakeRightDrivable(WRoadNav *nav, int) {
    Log(kTagDrivable);
    LogPointer(nav);
    return g_in.drivable[0];
}

double __fastcall FakeLeftDrivable(WRoadNav *nav, int) {
    Log(kTagDrivable + 1);
    LogPointer(nav);
    return g_in.drivable[1];
}

double __fastcall FakeFraction(SimRandom *random, int) {
    Log(kTagFraction);
    LogPointer(random);
    return g_in.fractions[g_in.fraction++ & 7];
}

void __fastcall FakeSleep(void *controller, int, void *vehicle) {
    Log(kTagSleep);
    LogPointer(controller);
    LogPointer(vehicle);
}

void __fastcall FakeEvent(void *manager, int, int event, const char *text) {
    Log(kTagEvent);
    LogPointer(manager);
    Log(uint32_t(event));
    LogString(text);
}

void __fastcall FakeGroundDestruct(void *vehicle, int) {
    Log(kTagGroundDestruct);
    LogPointer(vehicle);
}

void __fastcall FakeAttackMode(void *vehicle, int, uint8_t mode) {
    Log(kTagAttackMode);
    LogPointer(vehicle);
    Log(mode);
}

void __fastcall FakeNavigateMode(void *vehicle, int, uint8_t mode, void *target) {
    Log(kTagNavigateMode);
    LogPointer(vehicle);
    Log(mode);
    LogPointer(target);
}

void __fastcall FakeUnSpawn(void *vehicle, int) {
    Log(kTagUnSpawn);
    LogPointer(vehicle);
}

void *__fastcall FakeSpawnCar(void *simulation, int, int carClass, const char *type, uint32_t colour,
                              const Coord3 *direction, const Coord3 *position) {
    Log(kTagSpawnCar);
    Log(uint32_t(carClass));
    LogString(type);
    Log(colour);
    LogBytes(direction, 12);
    LogBytes(position, 12);
    return NULL;
}

void __stdcall FakeVecCtor(void *array, uint32_t size, int count, void *constructor, void *destructor) {
    Log(kTagVecCtor);
    LogPointer(array);
    Log(size);
    Log(uint32_t(count));
    Log(CodeAddress(constructor));
    Log(CodeAddress(destructor));
}

void __stdcall FakeVecDtor(void *array, uint32_t size, int count, void *destructor) {
    Log(kTagVecDtor);
    LogPointer(array);
    Log(size);
    Log(uint32_t(count));
    Log(CodeAddress(destructor));
}

int FakeAtexit(void (*function)(void)) {
    Log(kTagAtexit);
    Log(CodeAddress((const void *)function));
    return 0;
}

// The synthetic objects' vtables
int __fastcall FakeDamage(void *object, int, const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                          const uint32_t *sourceSig) {
    Log(kTagDamage);
    LogPointer(object);
    LogBytes(from, 12);
    LogBytes(to, 12);
    LogFloat(amount);
    LogFloat(split);
    Log(uint32_t(kind));
    LogPointer(sourceSig);
    return 0;
}

void __fastcall FakeLanding(void *sound, int, float volume, const Coord3 *position, const Coord3 *velocity) {
    Log(kTagLanding);
    LogPointer(sound);
    LogFloat(volume);
    LogBytes(position, 12);
    LogBytes(velocity, 12);
}

template <int N> void __fastcall FakeSlot(void *object, int) {
    Log(kTagSlot + N);
    LogPointer(object);
}

void *__fastcall FakeDeleteSlot(void *object, int, unsigned flags) {
    Log(kTagSlot + 0x100);
    LogPointer(object);
    Log(flags);
    return object;
}

int CarIndex(void *car) {
    for (int i = 0; i < 3; i++)
        if (car == g_cars[i])
            return i;
    return 0;
}

void __fastcall FakeCarSetAI(void *car, int, void *vehicle) {
    Log(kTagCarSlot + PVehicle::kSetAIGroundVehicle);
    LogPointer(car);
    LogPointer(vehicle);
}

void *__fastcall FakeCarGetAI(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetAIGroundVehiclePtr);
    LogPointer(car);
    return g_carAI[CarIndex(car)];
}

const char *__fastcall FakeCarType(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetCarType);
    LogPointer(car);
    return kCarTypes[g_in.carType[CarIndex(car)]];
}

int __fastcall FakeCarClass(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetCarClass);
    LogPointer(car);
    return g_in.carClass[CarIndex(car)];
}

void *__fastcall FakeCarPhysics(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetPhysics);
    LogPointer(car);
    return g_carPhysics[CarIndex(car)];
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
    {0x000cc100, (const void *)&FakeNavInitAtPoint}, {0x000c95a0, (const void *)&FakeNavReverse},
    {0x000cb4b0, (const void *)&FakeNavInc}, {0x000cbcf0, (const void *)&FakeNavLanes},
    {0x000c9810, (const void *)&FakeNavForward}, {0x000c9130, (const void *)&FakeNavReset},
    {0x000ca090, (const void *)&FakeNavConstruct}, {0x000ca100, (const void *)&FakeNavDestruct},
    {0x000cb900, (const void *)&FakeNavInitSegment}, {0x000d31f0, (const void *)&FakeFace},
    {0x000d2f90, (const void *)&FakeHeight}, {0x000be620, (const void *)&FakeColliderConstruct},
    {0x000bd890, (const void *)&FakeNormal}, {0x000be180, (const void *)&FakeColliderDestruct},
    {0x0001d9f0, (const void *)&FakeInfoConstruct}, {0x00077d70, (const void *)&FakeStimuli},
    {0x00077e00, (const void *)&FakeStimuliAll}, {0x000774a0, (const void *)&FakePlaying},
    {0x000cda20, (const void *)&FakeBeaconConstruct}, {0x000ced50, (const void *)&FakeRegister},
    {0x000ceb60, (const void *)&FakeUnregister}, {0x000cdb80, (const void *)&FakeRelease},
    {0x00114750, (const void *)&FakeAlloc}, {0x001147d0, (const void *)&FakeFree},
    {0x001146a0, (const void *)&FakeNew}, {0x001146e0, (const void *)&FakeDelete},
    {0x00114710, (const void *)&FakeNewArray}, {0x00080700, (const void *)&FakePathCreate},
    {0x000802d0, (const void *)&FakePathDestroy}, {0x00080130, (const void *)&FakePathInit},
    {0x00058180, (const void *)&FakeLookupInt}, {0x000580e0, (const void *)&FakeLookupBool},
    {0x0006f810, (const void *)&FakeNamed}, {0x0006f860, (const void *)&FakeRenderName},
    {0x0006f8c0, (const void *)&FakeColours}, {0x00057a40, (const void *)&FakeAttrDestruct},
    {0x00117db0, (const void *)&FakeAssert}, {0x000904c0, (const void *)&FakePreLoad},
    {0x0002b0c0, (const void *)&FakeCollNav}, {0x00031820, (const void *)&FakeRightDrivable},
    {0x00029cf0, (const void *)&FakeLeftDrivable}, {0x0001d9d0, (const void *)&FakeFraction},
    {0x000289f0, (const void *)&FakeSleep}, {0x000b72f0, (const void *)&FakeEvent},
    {0x0002be70, (const void *)&FakeGroundDestruct}, {0x0002a330, (const void *)&FakeAttackMode},
    {0x0002a0d0, (const void *)&FakeNavigateMode}, {0x0002dac0, (const void *)&FakeUnSpawn},
    {0x000b5700, (const void *)&FakeSpawnCar}, {0x0013326e, (const void *)&FakeVecCtor},
    {0x0013332e, (const void *)&FakeVecDtor}, {0x00132a7b, (const void *)&FakeAtexit},
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

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    g_arenaUsed = 0;
    g_in.fraction = 0;
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

const Coord3 *PlayerCarPositionPtr();

int RandomSegment() {
    return Range(0, fgRoadNetworkData.segmentCount - 1);
}

// A segment with lanes (the lane picks divide by their count)
int RandomLaneSegment() {
    for (int tries = 0; tries < 100; tries++) {
        int segment = RandomSegment();
        WRoadSegment *road = fgRoadNetworkData.segments[segment];
        if (road->leftLanes + road->rightLanes > 0)
            return segment;
    }
    return -1;
}

void RandomInputs() {
    g_in.ground = Chance(20) ? Near(0.0f) : Uniform(-20.0f, 60.0f);
    g_in.navOffset = {Uniform(-8.0f, 8.0f), Uniform(-2.0f, 2.0f), Uniform(-8.0f, 8.0f)};
    g_in.point = Around(*PlayerCarPositionPtr(), 300.0f);
    g_in.drivable[0] = Uniform(0.0f, 20.0f);
    g_in.drivable[1] = Uniform(0.0f, 20.0f);
    for (double &fraction : g_in.fractions)
        fraction = Uniform(0.0f, 1.0f);
    g_in.forward = Chance(50);
    g_in.sameRoad = Chance(50);
    g_in.hitWorld = Chance(30);
    g_in.playing = Next();
    for (float &f : g_in.f)
        f = Uniform(-50.0f, 150.0f);
    for (int &i : g_in.i)
        i = Range(-3, 20);
    for (bool &b : g_in.b)
        b = Chance(50);
}

// The synthetic helicopter: its PHelicopter on a simple body near the player car, chasing it
void MakeHelicopter() {
    RandomBytes(g_heliBlock, sizeof(g_heliBlock));
    RandomBytes(g_physBlock, sizeof(g_physBlock));
    memset(g_soundBlock, 0, sizeof(g_soundBlock));
    *reinterpret_cast<void ***>(g_soundBlock) = g_soundVtable;
    for (int i = 0xc0; i < 0xc6; i++)
        g_soundBlock[i] = uint8_t(Range(0, 2));
    memset(g_sceneBlock, 0, sizeof(g_sceneBlock));
    *reinterpret_cast<void **>(g_sceneBlock + 0x2c) = g_handleBlock;

    PHelicopter *phys = g_phys;
    phys->vtable = g_physVtable;
    phys->flags = PhysicsObject::kSimpleBody;
    phys->rigidBodySlot = int16_t(g_bodySlot);
    phys->renderObject = Chance(85) ? reinterpret_cast<RSceneObj *>(g_sceneBlock) : NULL;
    phys->audioObject = reinterpret_cast<ABaseSound *>(g_soundBlock);
    phys->hitPointLoc = Chance(90) ? &g_heliHitPoints : NULL;
    g_heliHitPoints = Chance(25) ? Near(0.0f) : Uniform(-10.0f, 100.0f);
    phys->worldPos.valid = uint8_t(Range(0, 1));
    phys->proximityDestructEnabled = uint8_t(Chance(30));
    phys->destructDistance = Uniform(0.0f, 60.0f);
    phys->controlGas = Uniform(-1.0f, 1.0f);
    phys->controlStrafe = Uniform(-1.0f, 1.0f);
    phys->controlSteer = Chance(10) ? -1.0f : Uniform(-1.0f, 1.0f);
    phys->controlAltitude = Uniform(-5.0f, 20.0f);
    phys->ai = g_heli;

    const Coord3 &player = *PlayerCarPositionPtr();
    g_body->position = Around(player, 120.0f);
    g_body->position.y = player.y + Uniform(-5.0f, 40.0f);
    g_body->velocity = {Uniform(-30.0f, 30.0f), Uniform(-5.0f, 5.0f), Uniform(-30.0f, 30.0f)};
    Coord3 axis = UnitVector();
    float half = Uniform(0.0f, 3.14159f);
    g_body->orientation = {axis.x * sinf(half), axis.y * sinf(half), axis.z * sinf(half), cosf(half)};
    g_body->radius = Uniform(1.0f, 8.0f);

    memcpy(g_navA, g_carNav, sizeof(g_navA));
    memcpy(g_navB, g_carNav, sizeof(g_navB));
    memcpy(g_collNav, g_carNav, sizeof(g_collNav));
    WRoadNav *coll = reinterpret_cast<WRoadNav *>(g_collNav);
    if (fgRoadNetworkData.loaded && fgRoadNetworkData.segmentCount > 0)
        coll->segment = int16_t(RandomSegment());
    coll->forward = uint8_t(Range(0, 1));
    coll->laneOffset = Uniform(-25.0f, 25.0f);
    coll->roadPoint = Around(player, 40.0f);
    WRoadNav *nav = reinterpret_cast<WRoadNav *>(g_navA);
    nav->roadPoint = Around(player, 60.0f);
    nav->direction = UnitVector();
    nav->segment = Chance(40) ? coll->segment : int16_t(fgRoadNetworkData.segmentCount > 0 ? RandomSegment() : 0);

    memset(g_target, 0, sizeof(g_target));
    reinterpret_cast<AIGroundVehicle *>(g_target)->physicsObject = g_playerCar;

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
    g_path->path = handle;
    for (int i = 0; i < 2; i++) {
        RandomBytes(g_spline[i], sizeof(g_spline[i]));
        CARP::AISpline *spline = reinterpret_cast<CARP::AISpline *>(g_spline[i]);
        spline->path.value = 0;
        spline->disabled = 1;
    }

    AIHelicopter *heli = g_heli;
    heli->vtable = reinterpret_cast<void **>(kAIHelicopterVtable);
    heli->physicsObject = phys;
    heli->driveToNav = reinterpret_cast<WRoadNav *>(g_navA);
    heli->localNav = reinterpret_cast<WRoadNav *>(g_navB);
    heli->splinePath = Chance(70) ? g_path : NULL;
    heli->targetVehicle = reinterpret_cast<AIGroundVehicle *>(g_target);
    heli->targetBeacon = Chance(50) ? reinterpret_cast<WTargetable *>(g_beacon) : NULL;
    heli->active = Chance(85);
    heli->commanded = uint8_t(Chance(85));
    static const int kNavigateModes[] = {0, 1, 2, 3, 3, 5, 5, 4};
    heli->navigateMode = kNavigateModes[Range(0, 7)];
    heli->attackMode = Range(0, 17);
    heli->targetMode = Chance(70) ? 0 : 1;
    heli->speedScale = Uniform(0.0f, 2.0f);
    heli->maxSpeed = Uniform(0.0f, 80.0f);
    heli->targetPosition = Around(player, 100.0f);
    heli->destination = Around(player, 150.0f);
    heli->destinationDistance = Uniform(0.0f, 200.0f);
    heli->range = Chance(15) ? 5.0f : Uniform(5.0f, 120.0f);
    heli->flatDirection = UnitVector();
    heli->flatDirection.y = 0.0f;
    heli->destinationDirection = UnitVector();
    heli->targetSegment = int16_t(Range(-1, 50));
    heli->pauseSteps = uint8_t(Chance(70) ? 0 : Range(1, 3));
    heli->shotsLeft = uint8_t(Chance(30) ? 0 : Range(1, 255));
    heli->nearSide = uint8_t(Range(0, 1));
    heli->crashStep = int8_t(Chance(60) ? 0 : Range(1, 20));
    heli->gunSide = uint8_t(Range(0, 1));
    heli->bulletDelay = int16_t(Chance(60) ? 0 : Range(-2, 10));
    heli->rocketDelay = int16_t(Chance(60) ? 0 : Range(-2, 10));
    heli->altitude = Uniform(3.0f, 30.0f);
    heli->fireDelay = Chance(10) ? -0.5f : Uniform(0.0f, 2.0f);
}

void RandomCommand() {
    RandomBytes(&g_command, sizeof(g_command));
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            g_command.orientation.mtx[r][c] = r == c ? 1.0f : Uniform(-0.3f, 0.3f);
    g_command.position = Around(*PlayerCarPositionPtr(), 150.0f);
    g_command.range = Chance(20) ? Near(Chance(50) ? 5.0f : -5.0f) : Uniform(-80.0f, 80.0f);
    g_command.altitude = Chance(20) ? Near(3.0f) : Uniform(0.0f, 40.0f);
    g_command.speedScale = Uniform(0.0f, 2.0f);
    g_command.maxSpeed = Uniform(0.0f, 80.0f);
    g_command.fireDelay = Uniform(0.0f, 2.0f);
    g_command.shotCount = Chance(30) ? 0.0f : Uniform(0.0f, 300.0f);
    g_command.targetMode = uint8_t(Range(0, 2));
    static const uint8_t kNavigate[] = {0, 1, 2, 3, 5, 5};
    g_command.navigateMode = kNavigate[Range(0, 5)];
    g_command.attackMode = uint8_t(Chance(30) ? 0 : Range(1, 17));
    g_command.navigateTarget = reinterpret_cast<CARP::AISpline *>(g_spline[Range(0, 1)]);
}

}  // namespace

namespace {

const Coord3 *PlayerCarPositionPtr() {
    return &fgSimulation.GetRigidBody(g_playerCar->rigidBodySlot)->position;
}

// ---- the cases: each runs the original or the port by g_original

void CaseSetMatrix() {
    if (g_original)
        Orig_SetMatrix(g_pathInstance, 0, &g_command.orientation);
    else
        reinterpret_cast<CARP::Instance *>(g_pathInstance)->SetMatrix(&g_command.orientation);
}

void CaseSplineConstruct() {
    CARP::AISpline *spline = reinterpret_cast<CARP::AISpline *>(g_spline[0]);
    g_out.p = g_original ? Orig_SplineConstruct(g_path, 0, spline) : g_path->Construct(spline);
}

void CaseSplineDestruct() {
    if (g_original)
        Orig_SplineDestruct(g_path, 0);
    else
        g_path->Destruct();
}

void CaseSplineReset() {
    CARP::AISpline *spline = reinterpret_cast<CARP::AISpline *>(g_spline[1]);
    if (g_original)
        Orig_SplineReset(g_path, 0, spline);
    else
        g_path->Reset(spline);
}

void CaseSplineSetNext() {
    CARP::AISpline *spline = reinterpret_cast<CARP::AISpline *>(g_spline[1]);
    if (g_original)
        Orig_SplineSetNext(g_path, 0, spline);
    else
        g_path->SetNext(spline);
}

void CaseVehicleConstruct() {
    g_out.p = g_original ? Orig_VehicleConstruct(g_heli, 0) : g_heli->AIVehicle::Construct();
}

void CaseVehicleDestruct() {
    if (g_original)
        Orig_VehicleDestruct(g_heli, 0);
    else
        g_heli->AIVehicle::Destruct();
}

void CaseVehicleDelete() {
    unsigned flags = unsigned(g_in.i[0]) & 1;
    g_out.p = g_original ? Orig_VehicleDelete(g_heli, 0, flags) : g_heli->AIVehicle::Delete(flags);
}

void CaseGroundDelete() {
    unsigned flags = unsigned(g_in.i[0]) & 3;
    g_out.p = g_original ? Orig_GroundDelete(g_vehicle, 0, flags) : g_vehicle->Delete(flags);
}

void CaseAccessors() {
    if (g_original) {
        Orig_SetPhysicsObject(g_heli, 0, g_in.b[0] ? (void *)g_playerCar : (void *)g_phys);
        g_out.p = Orig_GetPhysicsObject(g_heli, 0);
        g_out.d = double(uintptr_t(Orig_GetSplinePath(g_heli, 0)));
    } else {
        g_heli->SetPhysicsObject(g_in.b[0] ? static_cast<PhysicsObject *>(g_playerCar) : g_phys);
        g_out.p = g_heli->GetPhysicsObject();
        g_out.d = double(uintptr_t(g_heli->GetSplinePath()));
    }
}

void CaseSetSplinePath() {
    CARP::AISpline *spline = g_in.b[1] ? reinterpret_cast<CARP::AISpline *>(g_spline[0]) : NULL;
    if (g_original)
        Orig_SetSplinePath(g_heli, 0, spline);
    else
        g_heli->SetSplinePath(spline);
}

void CaseSetNextSplinePath() {
    CARP::AISpline *spline = reinterpret_cast<CARP::AISpline *>(g_spline[1]);
    if (g_original)
        Orig_SetNextSplinePath(g_heli, 0, spline);
    else
        g_heli->SetNextSplinePath(spline);
}

void CasePosition() {
    if (g_original) {
        g_out.p = Orig_GetPosition(g_heli, 0);
        g_out.d = double(uintptr_t(Orig_GetRotPos(g_heli, 0)));
    } else {
        g_out.p = g_heli->GetPosition();
        g_out.d = double(uintptr_t(g_heli->GetRotPos()));
    }
}

void CaseHeliConstruct() {
    g_out.p = g_original ? Orig_HeliConstruct(g_heli, 0) : g_heli->Construct();
}

void CaseHeliDestruct() {
    if (g_original)
        Orig_HeliDestruct(g_heli, 0);
    else
        g_heli->Destruct();
}

void CaseHeliDelete() {
    unsigned flags = unsigned(g_in.i[0]) & 3;
    AIHelicopter *heli = (flags & 2) ? g_heliArray : g_heli;
    g_out.p = g_original ? Orig_HeliDelete(heli, 0, flags) : heli->Delete(flags);
}

void CaseHeliReset() {
    if (g_original)
        Orig_HeliReset(g_heli, 0);
    else
        g_heli->Reset();
}

void CaseBeacon() {
    if (g_original) {
        if (g_in.b[0])
            Orig_EnableBeacon(g_heli, 0, g_in.i[1]);
        else
            Orig_DisableBeacon(g_heli, 0);
    } else {
        if (g_in.b[0])
            g_heli->EnableTargetBeacon(g_in.i[1]);
        else
            g_heli->DisableTargetBeacon();
    }
}

void CaseDirection() {
    if (g_original)
        Orig_DirectionToTarget(g_heli, 0, &g_in.point);
    else
        g_heli->GetDirectionToTarget(&g_in.point);
}

void CaseLaneOffsets() {
    g_out.p = g_original ? Translate(Orig_LaneOffsets(g_heli, 0)) : g_heli->GetPlayerLaneOffsets();
}

void CaseLookAhead() {
    g_out.d = g_original ? Orig_LookAhead(g_heli, 0, g_in.f[0], g_in.f[1]) : g_heli->GetLookAhead(g_in.f[0], g_in.f[1]);
}

Coord4 g_vectors[4];

void CaseFireRockets() {
    const Coord3 *a = Xyz(&g_vectors[0]), *b = Xyz(&g_vectors[1]), *c = Xyz(&g_vectors[2]);
    if (g_original)
        Orig_FireRockets(g_heli, 0, g_in.f[0], a, b, c);
    else
        g_heli->FireRockets(g_in.f[0], a, b, c);
}

void CaseFireBullets() {
    const Coord3 *a = Xyz(&g_vectors[0]), *b = Xyz(&g_vectors[1]);
    Coord3 *c = Xyz(&g_vectors[2]);
    if (g_original)
        Orig_FireBullets(g_heli, 0, g_in.f[0], g_in.f[1], a, b, c);
    else
        g_heli->FireBullets(g_in.f[0], g_in.f[1], a, b, c);
}

void CaseManeuver() {
    Coord3 *target = Xyz(&g_vectors[0]);
    if (g_original)
        Orig_Maneuver(g_heli, 0, target, g_in.f[0], g_in.f[1], g_in.b[0]);
    else
        g_heli->ManeuverToPos(target, g_in.f[0], g_in.f[1], g_in.b[0]);
}

void CaseControlMove() {
    if (g_original)
        Orig_ControlMove(g_heli, 0, g_in.b[0], g_in.b[1]);
    else
        g_heli->ControlMoveState(g_in.b[0], g_in.b[1]);
}

void CaseNewState() {
    if (g_original)
        Orig_NewState(g_heli, 0, &g_command);
    else
        g_heli->NewState(&g_command);
}

void CaseAgentLogic() {
    if (g_original)
        Orig_AgentLogic(g_heli, 0);
    else
        g_heli->DoAgentLogic();
}

void CaseProximity() {
    g_out.b = g_original ? Orig_ProximityDestruct(g_heli, 0) : g_heli->DoProximityDestruct();
}

void CaseUpdate() {
    if (g_original)
        Orig_HeliUpdate(g_heli, 0);
    else
        g_heli->Update();
}

void CaseSplineEngage() {
    CARP::AISpline *spline = reinterpret_cast<CARP::AISpline *>(g_spline[0]);
    if (g_original) {
        if (g_in.b[0])
            Orig_Engage(g_heli, 0, spline);
        else
            Orig_Disengage(g_heli, 0);
    } else {
        if (g_in.b[0])
            g_heli->EngageSplinePath(spline);
        else
            g_heli->DisengageSplinePath();
    }
}

// The controller (its instance is whatever the mirror holds)
AIVehicleController *Controller() {
    return AIVehicleControllerInstance;
}

void CaseControllerInit() {
    if (g_original)
        Orig_ControllerInit();
    else
        AIVehicleController::Init();
}

void CaseControllerGets() {
    int i = g_in.i[0] & 1, t = g_in.i[1] & 15;
    if (g_original) {
        g_out.p = Orig_ControllerGet();
        g_vectors[3].x = float(uintptr_t(Orig_GetTraffic(Controller(), 0, t)) & 0xffff);
        g_vectors[3].y = float(uintptr_t(Orig_GetPlayer(Controller(), 0, i)) & 0xffff);
        g_vectors[3].z = float(uintptr_t(Orig_GetHeli(Controller(), 0, i)) & 0xffff);
        g_out.d = double(uintptr_t(Orig_FindAgent(Controller(), 0, g_in.b[0] ? (PhysicsObject *)g_playerCar : g_phys)));
    } else {
        g_out.p = AIVehicleController::Get();
        g_vectors[3].x = float(uintptr_t(Controller()->GetTrafficVehicle(t)) & 0xffff);
        g_vectors[3].y = float(uintptr_t(Controller()->GetPlayerGroundVehiclePtr(i)) & 0xffff);
        g_vectors[3].z = float(uintptr_t(Controller()->GetHelicopter(i)) & 0xffff);
        g_out.d = double(uintptr_t(Controller()->FindAgentGroundVehiclePtr(
            g_in.b[0] ? static_cast<PhysicsObject *>(g_playerCar) : g_phys)));
    }
}

void CaseUpdates() {
    if (g_original) {
        switch (g_in.i[2] & 3) {
        case 0: Orig_UpdateVehicles(Controller(), 0); break;
        case 1: Orig_UpdateAgents(Controller(), 0); break;
        case 2: Orig_UpdateTraffic(Controller(), 0); break;
        default: Orig_UpdateHelis(Controller(), 0); break;
        }
    } else {
        switch (g_in.i[2] & 3) {
        case 0: Controller()->UpdateVehicles(); break;
        case 1: Controller()->UpdateAgentGroundVehicles(); break;
        case 2: Controller()->UpdateTrafficVehicles(); break;
        default: Controller()->UpdateHelicopters(); break;
        }
    }
}

void CaseGreenLight() {
    uint32_t phase = uint32_t(g_in.i[0] & 3), phases = uint32_t((g_in.i[1] & 3) + 1);
    g_out.b = g_original ? Orig_GreenLight(Controller(), 0, &g_in.point, phase, phases)
                         : Controller()->IntersectionGreenLight(&g_in.point, phase, phases);
}

void CaseApprove() {
    if (g_original) {
        g_out.p = Orig_ApproveAgent(Controller(), 0);
        g_out.d = double(uintptr_t(Orig_ApproveHeli(Controller(), 0)));
    } else {
        g_out.p = Controller()->ApproveAdditionalAgentGroundVehicleSpawn();
        g_out.d = double(uintptr_t(Controller()->ApproveAdditionalHelicopterSpawn()));
    }
}

void CaseRegister() {
    PVehicle *car = reinterpret_cast<PVehicle *>(g_cars[g_in.i[3] % 3 < 0 ? 0 : g_in.i[3] % 3]);
    int which = g_in.i[2] & 7;
    if (g_original) {
        switch (which) {
        case 0: Orig_RegisterPlayer(Controller(), 0, car); break;
        case 1: Orig_RegisterAgent(Controller(), 0, car); break;
        case 2: Orig_RegisterTraffic(Controller(), 0, car); break;
        case 3: Orig_RegisterSimCar(Controller(), 0, car); break;
        case 4: Orig_DeRegisterSimCar(Controller(), 0, car); break;
        case 5: Orig_RegisterHeli(Controller(), 0, g_phys); break;
        case 6: Orig_DeRegisterHeli(Controller(), 0, g_phys); break;
        default: Orig_RegisterList(Controller(), 0); break;
        }
    } else {
        switch (which) {
        case 0: Controller()->RegisterPlayerGroundVehicle(car); break;
        case 1: Controller()->RegisterAgentGroundVehicle(car); break;
        case 2: Controller()->RegisterTrafficVehicle(car); break;
        case 3: Controller()->RegisterSimCar(car); break;
        case 4: Controller()->DeRegisterSimCar(car); break;
        case 5: Controller()->RegisterHelicopter(g_phys); break;
        case 6: Controller()->DeRegisterHelicopter(g_phys); break;
        default: Controller()->RegisterSimCarList(); break;
        }
    }
}

void CaseControllerReset() {
    if (g_original) {
        if (g_in.b[0])
            Orig_ControllerReset();
        else
            Orig_ControllerShutdown();
    } else {
        if (g_in.b[0])
            AIVehicleController::Reset();
        else
            AIVehicleController::Shutdown();
    }
}

void CaseCreateTraffic() {
    if (g_original)
        Orig_CreateTraffic(Controller(), 0, g_in.b[0]);
    else
        Controller()->CreateSimTrafficCars(g_in.b[0]);
}

// The road spawn
AIRoadSpawn *Spawner() {
    return reinterpret_cast<AIRoadSpawn *>(g_handleBlock);   // `this` is not read
}

void CaseSpawnStatics() {
    int which = g_in.i[2] & 3;
    if (g_original) {
        switch (which) {
        case 0: g_out.p = Orig_SpawnGet(); break;
        case 1: Orig_SpawnInit(); break;
        case 2: Orig_SpawnRestart(); break;
        default: Orig_SpawnShutdown(); break;
        }
    } else {
        switch (which) {
        case 0: g_out.p = AIRoadSpawn::Get(); break;
        case 1: AIRoadSpawn::Init(); break;
        case 2: AIRoadSpawn::Restart(); break;
        default: AIRoadSpawn::Shutdown(); break;
        }
    }
}

void CaseSpawnPoint() {
    g_out.segment = g_in.segment;
    if (g_original) {
        if (g_in.b[0])
            Orig_TrafficPoint(Spawner(), 0, &g_out.segment, &g_out.lane, &g_out.along, &g_out.forwards);
        else
            Orig_PedPoint(Spawner(), 0, &g_out.segment, &g_out.lane, &g_out.along);
    } else {
        if (g_in.b[0])
            Spawner()->GetTrafficSpawnPointOnSegment(&g_out.segment, &g_out.lane, &g_out.along, &g_out.forwards);
        else
            Spawner()->GetPedSpawnPointOnSegment(&g_out.segment, &g_out.lane, &g_out.along);
    }
}

void CaseSpawnChecks() {
    const Coord3 *at = &g_in.point;
    int which = g_in.i[2] & 3;
    if (g_original) {
        switch (which) {
        case 0: g_out.b = Orig_TrafficPosCheck(Spawner(), 0, at, g_in.f[0]); break;
        case 1: g_out.b = Orig_TrafficAvailable(Spawner(), 0, at, g_in.f[0]); break;
        case 2: g_out.b = Orig_PedPosCheck(Spawner(), 0, at); break;
        default: g_out.b = Orig_PedAvailable(Spawner(), 0, at); break;
        }
    } else {
        switch (which) {
        case 0: g_out.b = Spawner()->TrafficRespawnPosCheck(at, g_in.f[0]); break;
        case 1: g_out.b = Spawner()->TrafficRespawnAvailable(at, g_in.f[0]); break;
        case 2: g_out.b = Spawner()->PedRespawnPosCheck(at); break;
        default: g_out.b = Spawner()->PedRespawnAvailable(at); break;
        }
    }
}

void CaseRefresh() {
    if (g_original)
        Orig_Refresh(AIRoadSpawnInstance, 0, g_in.b[0], g_in.f[0], g_in.f[1]);
    else
        AIRoadSpawnInstance->RefreshSpawnData(g_in.b[0], g_in.f[0], g_in.f[1]);
}

void CaseRandomSpawn() {
    g_out.segment = g_in.segment;
    if (g_original) {
        if (g_in.b[0])
            g_out.b = Orig_RandomTraffic(Spawner(), 0, &g_out.segment, &g_out.lane, &g_out.along, &g_out.forwards);
        else
            g_out.b = Orig_RandomPed(Spawner(), 0, &g_out.segment, &g_out.lane, &g_out.along, g_in.b[1]);
    } else {
        if (g_in.b[0])
            g_out.b = Spawner()->GetRandomTrafficSpawn(&g_out.segment, &g_out.lane, &g_out.along, &g_out.forwards);
        else
            g_out.b = Spawner()->GetRandomPedSpawn(&g_out.segment, &g_out.lane, &g_out.along, g_in.b[1]);
    }
}

// ---- the groups

void Run(const char *what, int count, void (*prepare)(int), SideFn run, const Snapshot &before) {
    for (int i = 0; i < count; i++) {
        RandomInputs();
        memset(&g_out, 0, sizeof(g_out));
        MakeHelicopter();
        prepare(i);
        RunCase(what, i, run, before);
    }
}

void PrepareNothing(int) {
}

void PrepareVectors(int i) {
    const Coord3 &player = *PlayerCarPositionPtr();
    *Xyz(&g_vectors[0]) = g_body->position;
    *Xyz(&g_vectors[1]) = i % 4 == 0 ? g_body->position : Around(player, 80.0f);
    *Xyz(&g_vectors[2]) = UnitVector();
    if (i % 5 == 0)
        g_vectors[2].y = Near(-0.01f);
    if (i % 7 == 0)
        g_vectors[2].y = Near(-0.05f);
    for (Coord4 &v : g_vectors)
        v.w = 0.0f;
    g_in.f[0] = Chance(20) ? Uniform(0.0f, 5.0f) : Uniform(0.0f, 300.0f);   // a range or an altitude
    g_in.f[1] = Chance(20) ? Near(5.0f) : Uniform(-5.0f, 60.0f);            // a speed
    if (i % 9 == 0)
        g_in.f[1] = 0.0f;
}

void PrepareLookAhead(int i) {
    static const float kEdges[] = {20.0f, 40.0f, 0.0f, -1.0f, 19.99f, 40.01f};
    g_in.f[0] = i < 6 ? kEdges[i] : Uniform(-10.0f, 100.0f);
    if (i == 6)
        g_in.f[0] = nanf("");
}

void PrepareCommand(int) {
    RandomCommand();
}

void PrepareVehicle(int i) {
    RandomBytes(g_vehicleBlock, sizeof(g_vehicleBlock));
    *reinterpret_cast<int32_t *>(g_vehicleBlock) = 1;           // the cookie's count
    g_vehicle->splinePath = Chance(50) ? g_path : NULL;
    g_vehicle->driveToNav = Chance(80) ? reinterpret_cast<WRoadNav *>(g_navA) : NULL;
    g_heli->splinePath = i % 3 == 0 ? NULL : g_path;
    if (i % 4 == 0)
        g_heli->driveToNav = NULL;
    if (i % 5 == 0)
        g_heli->physicsObject = NULL;
    else if (i % 5 == 1)
        g_heli->physicsObject = g_playerCar;
    RandomBytes(g_helis, 0x10);
    *reinterpret_cast<int32_t *>(g_helis) = 3;
}

void PrepareController(int i) {
    g_counts.agents = Range(1, 4);
    g_counts.helicopters = Range(0, 3);
    for (int a = 0; a < 4; a++) {
        AIGroundVehicle *agent = &g_agentArray[a];
        agent->vtable = g_vehicleVtable;
        agent->active = Chance(60);
        agent->physicsObject = a == 1 && Chance(50) ? static_cast<PhysicsObject *>(g_playerCar) : g_phys;
    }
    for (int h = 0; h < 3; h++) {
        g_heliArray[h].vtable = g_vehicleVtable;
        g_heliArray[h].active = Chance(60);
    }
    for (int v = 0; v < kPlayerGroundVehicles; v++) {
        PlayerGroundVehicles[v].vtable = g_vehicleVtable;
        PlayerGroundVehicles[v].active = Chance(50);
    }
    for (int v = 0; v < kTrafficVehicles; v++) {
        TrafficGroundVehicles[v].vtable = g_vehicleVtable;
        TrafficGroundVehicles[v].active = Chance(70);
    }
    if (i % 8 == 0) {
        for (int v = 0; v < kPlayerGroundVehicles; v++)
            PlayerGroundVehicles[v].active = true;
        for (int v = 0; v < kTrafficVehicles; v++)
            TrafficGroundVehicles[v].active = true;
    }
    for (int c = 0; c < 3; c++) {
        memset(g_cars[c], 0, sizeof(g_cars[c]));
        *reinterpret_cast<void ***>(g_cars[c]) = g_carVtable;
        *reinterpret_cast<void **>(g_cars[c] + 0x64) = g_carNav;   // used should a car be its own AI vehicle
        *reinterpret_cast<uint32_t *>(g_cars[c] + 0x60) = 0x5eed0100 + c;
        memset(g_carAI[c], 0, sizeof(g_carAI[c]));
        reinterpret_cast<AIGroundVehicle *>(g_carAI[c])->vtable = g_vehicleVtable;
        g_in.carClass[c] = Range(0, 4);
        g_in.carType[c] = Range(0, 6);
        *reinterpret_cast<int32_t *>(g_carPhysics[c] + 0xbc) = Chance(30) ? 0x101 : Range(0, 1);
        g_carList[c] = reinterpret_cast<PVehicle *>(g_cars[c]);
    }
    g_in.turret = uint8_t(Range(0, 2));
    ShadowElements = &g_counts;
    AgentGroundVehicles = g_agentArray;
    AIHelicopters = g_heliArray;
    if (i % 2 == 0) {
        ShadowCars.first = g_carList;
        ShadowCars.last = g_carList + Range(0, 3);
    }
    GreenLightCycle = uint32_t(Range(0, 40));
    NextGreenLightStep = ShadowStepCount + (Chance(30) ? 150 : Range(-10, 310));
    AgentUpdateTurn = Range(0, 5);
    AIVehicleControllerInstance = reinterpret_cast<AIVehicleController *>(g_handleBlock + 1);
    g_in.lookups[0] = Range(1, 7);
    g_in.lookups[1] = Range(0, 25);
    g_in.found[0] = Chance(50);
    g_in.found[1] = Chance(70);
    g_in.colours = Range(0, 5);
    g_in.noModel = Chance(20);
    TrafficCarTypesChosen = int16_t(Chance(50));
    for (int16_t &type : TrafficCarTypes)
        type = int16_t(Range(0, kTrafficCarTypes - 1));
    // A point near a moving path (the lights' test), or anywhere
    RPathHandle *path = RPathEngine::GetFirstPathHandle();
    if (path != NULL && Chance(60))
        g_in.point = Around(*path->GetPosition(), 60.0f);
}

// A free agent car and helicopter, and the Simulation's cars the synthetic ones
void PrepareRegister(int i) {
    PrepareController(i);
    g_agentArray[0].active = false;
    g_heliArray[0].active = false;
    if (g_counts.helicopters == 0)
        g_counts.helicopters = 1;
    ShadowCars.first = g_carList;
    ShadowCars.last = g_carList + Range(0, 3);
}

// The proximity destruct plays its stimulus on the scene object, which a helicopter that has one always has
void PrepareProximity(int) {
    g_phys->renderObject = reinterpret_cast<RSceneObj *>(g_sceneBlock);
}

void PrepareSpawn(int i) {
    AIRoadSpawnInstance = reinterpret_cast<AIRoadSpawn *>(g_handleBlock + 2);
    if (i % 10 == 0)
        AIRoadSpawnInstance = NULL;
    ShadowForceRespawn = uint8_t(Chance(20));
    g_manager->autoDrive = uint8_t(Chance(50));
    g_manager->cinematicMode = Chance(15) ? 1 : 0;
    ShadowStepCount = Chance(10) ? Range(0, 6) : ShadowStepCount;
    fgRoadNetworkData.unknownAC = uint8_t(Range(0, 1));
    fgRoadNetworkData.unknownAD = uint8_t(Chance(90));
    NextSpawnRefreshStep = ShadowStepCount + Range(-3, 3);
    TrafficLaneTurn = Range(0, 100);
    SpawnNavMade = uint32_t(Chance(50));
    SpawnNearRadius = Uniform(20.0f, 200.0f);
    SpawnFarRadius = Uniform(50.0f, 400.0f);
    SpawnSegmentCount = Chance(10) ? 0 : Range(1, kMaxSpawnSegments);
    for (int s = 0; s < kMaxSpawnSegments; s++)
        SpawnSegments[s] = s < SpawnSegmentCount && Chance(90) ? RandomSegment() : -1;
    for (int r = 0; r < fgRoadNetworkData.roadCount; r++) {
        WRoad *road = fgRoadNetworkData.roads[r];
        road->carCount = uint8_t(Range(0, 3));
        road->pedestrianCount = uint8_t(Range(0, 3));
        road->unknown2c = uint8_t(Range(0, 4));
        road->unknown2d = uint8_t(Range(0, 4));
    }
    int segment = RandomLaneSegment();
    g_in.segment = short(segment < 0 ? 0 : segment);
    const Coord3 &player = *PlayerCarPositionPtr();
    g_in.point = Around(player, Chance(50) ? 100.0f : 400.0f);
    g_in.f[0] = Uniform(0.0f, 5.0f);
    g_in.f[1] = Uniform(50.0f, 300.0f);
    for (int v = 0; v < kTrafficVehicles; v++) {
        TrafficGroundVehicles[v].active = Chance(50);
        TrafficGroundVehicles[v].type = Chance(70) ? kAIGroundTraffic : 1;
    }
}

void BuildRegions() {
    g_regions.clear();
    AddRegion(g_arena, sizeof(g_arena), "the arena");
    AddRegion(g_heliBlock, sizeof(g_heliBlock), "the helicopter");
    AddRegion(g_vehicleBlock, sizeof(g_vehicleBlock), "the ground vehicle");
    AddRegion(g_physBlock, sizeof(g_physBlock), "the PHelicopter");
    AddRegion(g_soundBlock, sizeof(g_soundBlock), "the sound");
    AddRegion(g_navA, sizeof(g_navA), "the navigator");
    AddRegion(g_navB, sizeof(g_navB), "the local navigator");
    AddRegion(g_collNav, sizeof(g_collNav), "the target's navigator");
    AddRegion(g_splinePath, sizeof(g_splinePath), "the spline path");
    AddRegion(g_pathHandle, sizeof(g_pathHandle), "the path handle");
    AddRegion(g_pathInstance, sizeof(g_pathInstance), "the path's instance");
    AddRegion(g_spline, sizeof(g_spline), "the splines");
    AddRegion(g_target, sizeof(g_target), "the target vehicle");
    AddRegion(g_beacon, sizeof(g_beacon), "the beacon");
    AddRegion(&g_command, sizeof(g_command), "the command");
    AddRegion(g_agents, sizeof(g_agents), "the agent cars");
    AddRegion(g_helis, sizeof(g_helis), "the helicopters");
    AddRegion(g_cars, sizeof(g_cars), "the cars");
    AddRegion(g_carAI, sizeof(g_carAI), "the cars' AI");
    AddRegion(g_carPhysics, sizeof(g_carPhysics), "the cars' physics");
    AddRegion(g_carNav, sizeof(g_carNav), "the navigator template");
    AddRegion(&g_heliHitPoints, 4, "the helicopter's hit points");
    AddRegion(&g_counts, sizeof(g_counts), "the AI elements' counts");
    AddRegion(g_carList, sizeof(g_carList), "the car list");
    AddRegion(g_vectors, sizeof(g_vectors), "the vectors");
    AddRegion(g_body, sizeof(SimpleRigidBody), "the simple body");
    for (const Mirror &m : kMirrors)
        AddRegion(m.ours, m.size, m.name);
    AddRegion(ShadowStaticVehicles, kStaticVehicleBytes, "the player and traffic AI");
    AddRegion(ShadowSpawnNav, sizeof(WRoadNav), "the spawn navigator");
    AddRegion(&ShadowForceRespawn, 1, "ForceNearRespawn");
    AddRegion(&ShadowElements, 4, "the AI elements' pointer", false);
    AddRegion(&ShadowCars, sizeof(GameVector<PVehicle *>), "the Simulation's cars", false);
    AddRegion(ShadowRandom, sizeof(SimRandom), "the Simulation's random numbers");
    AddRegion(&ShadowStepCount, 4, "SimStepCount", false);
    AddRegion(&fgRoadNetworkData, sizeof(WRoadNetworkData), "the road network's statics");
    if (fgRoadNetworkData.loaded) {
        for (int s = 0; s < fgRoadNetworkData.segmentCount; s++)
            AddRegion(fgRoadNetworkData.segments[s], sizeof(WRoadSegment), "a road segment");
        for (int r = 0; r < fgRoadNetworkData.roadCount; r++)
            AddRegion(fgRoadNetworkData.roads[r], sizeof(WRoad), "a road");
    }
    AddRegion(&fgPathIterator, sizeof(fgPathIterator), "the path iterator");
    AddRegion(&g_manager->autoDrive, 1, "the mission manager's auto-drive", false);
    AddRegion(&g_manager->cinematicMode, 4, "the mission manager's cinematic mode", false);
    AddRegion(&g_out, sizeof(g_out), "the answers");
    AddRegion(&g_in, sizeof(g_in), "the inputs", false);
}

void BuildVtables() {
    for (void *&slot : g_physVtable)
        slot = (void *)&FakeSlot<0x20>;
    g_physVtable[2] = (void *)&FakeDamage;
    for (void *&slot : g_soundVtable)
        slot = (void *)&FakeSlot<0x30>;
    g_soundVtable[4] = (void *)&FakeLanding;
    g_vehicleVtable[0] = (void *)&FakeDeleteSlot;
    g_vehicleVtable[1] = (void *)&FakeSlot<1>;
    g_vehicleVtable[2] = (void *)&FakeSlot<2>;
    g_vehicleVtable[3] = (void *)&FakeSlot<3>;
    g_vehicleVtable[4] = (void *)&FakeSlot<4>;
    for (void *&slot : g_carVtable)
        slot = (void *)&FakeCarOther;
    g_carVtable[PVehicle::kSetAIGroundVehicle] = (void *)&FakeCarSetAI;
    g_carVtable[PVehicle::kGetAIGroundVehiclePtr] = (void *)&FakeCarGetAI;
    g_carVtable[PVehicle::kGetCarType] = (void *)&FakeCarType;
    g_carVtable[PVehicle::kGetCarClass] = (void *)&FakeCarClass;
    g_carVtable[PVehicle::kGetPhysics] = (void *)&FakeCarPhysics;
}

bool InstallHooks() {
    int missing = 0;
    for (int i = 0; i < kHookCount; i++)
        if (!HookInstall(&g_hooks[i], kHooks[i].entry, kHooks[i].to)) {
            printf("[gameaishadowd] the fake at 0x%08x could not be installed\n", kHooks[i].entry);
            missing++;
        }
    return missing == 0;
}

void RemoveHooks() {
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
}

}  // namespace

void GameAIShadowD_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEAISHADOWD", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    g_playerCar = ShadowCars.first != NULL && ShadowCars.last != ShadowCars.first ? ShadowCars.first[0] : NULL;
    g_manager = glbMissionManager;
    if (g_playerCar == NULL || g_manager == NULL) {
        printf("[gameaishadowd] no player car or mission manager at the first tick - nothing run\n");
        fflush(stdout);
        return;
    }
    // An unused simple body for the synthetic helicopter (its bytes are put back)
    g_bodySlot = 95;
    for (int slot = 95; slot >= 0; slot--)
        if (fgSimulation.simpleRigidBodyOwners[slot] == NULL) {
            g_bodySlot = slot;
            break;
        }
    g_body = ShadowGetSimpleBody(ShadowSim, 0, g_bodySlot);
    if (PlayerGroundVehicles[0].driveToNav != NULL)
        memcpy(g_carNav, PlayerGroundVehicles[0].driveToNav, sizeof(g_carNav));
    for (int i = 0; i < kMirrorCount; i++)
        memcpy(g_gameCopies[i], (const void *)uintptr_t(kMirrors[i].game), kMirrors[i].size);
    printf("[gameaishadowd] live at the first tick: %d cars, %d agent cars, %d helicopters, road network %s, "
           "simple body slot %d for the helicopter\n", int(ShadowCars.last - ShadowCars.first),
           ShadowElements != NULL ? reinterpret_cast<ElementCounts *>(ShadowElements)->agents : -1,
           ShadowElements != NULL ? reinterpret_cast<ElementCounts *>(ShadowElements)->helicopters : -1,
           fgRoadNetworkData.loaded ? "loaded" : "none", g_bodySlot);

    BuildVtables();
    if (!InstallHooks()) {
        RemoveHooks();
        printf("[gameaishadowd] fakes missing - nothing run\n");
        fflush(stdout);
        return;
    }
    BuildRegions();
    Snapshot before;
    Capture(&before);
    bool roads = fgRoadNetworkData.loaded && fgRoadNetworkData.segmentCount > 0;

    Run("CARP::Instance::SetMatrix", 20, PrepareCommand, CaseSetMatrix, before);
    Run("AISplinePath::AISplinePath", 20, PrepareNothing, CaseSplineConstruct, before);
    Run("AISplinePath::~AISplinePath", 10, PrepareNothing, CaseSplineDestruct, before);
    Run("AISplinePath::Reset", 20, PrepareNothing, CaseSplineReset, before);
    Run("AISplinePath::SetNext", 10, PrepareNothing, CaseSplineSetNext, before);
    Run("AIVehicle::AIVehicle", 20, PrepareVehicle, CaseVehicleConstruct, before);
    Run("AIVehicle::~AIVehicle", 30, PrepareVehicle, CaseVehicleDestruct, before);
    Run("AIVehicle::scalar_deleting_destructor", 30, PrepareVehicle, CaseVehicleDelete, before);
    Run("AIGroundVehicle's vector deleting destructor", 40, PrepareVehicle, CaseGroundDelete, before);
    Run("AIVehicle's accessors", 20, PrepareVehicle, CaseAccessors, before);
    Run("AIVehicle::SetSplinePath", 40, PrepareVehicle, CaseSetSplinePath, before);
    Run("AIVehicle::SetNextSplinePath", 30, PrepareVehicle, CaseSetNextSplinePath, before);
    Run("AIVehicle::GetPosition, GetRotPos", 40, PrepareVehicle, CasePosition, before);
    Run("AIHelicopter::AIHelicopter", 20, PrepareNothing, CaseHeliConstruct, before);
    Run("AIHelicopter::~AIHelicopter", 20, PrepareNothing, CaseHeliDestruct, before);
    Run("AIHelicopter's vector deleting destructor", 40, PrepareVehicle, CaseHeliDelete, before);
    Run("AIHelicopter::Reset", 20, PrepareNothing, CaseHeliReset, before);
    Run("AIHelicopter's target beacon", 40, PrepareNothing, CaseBeacon, before);
    Run("AIHelicopter::GetDirectionToTarget", 40, PrepareNothing, CaseDirection, before);
    Run("AIHelicopter::GetLookAhead", 60, PrepareLookAhead, CaseLookAhead, before);
    Run("AIHelicopter::FireRockets", 300, PrepareVectors, CaseFireRockets, before);
    Run("AIHelicopter::FireBullets", 300, PrepareVectors, CaseFireBullets, before);
    Run("AIHelicopter::ManeuverToPos", 300, PrepareVectors, CaseManeuver, before);
    Run("AIHelicopter::NewState", 200, PrepareCommand, CaseNewState, before);
    Run("AIHelicopter::DoProximityDestruct", 100, PrepareProximity, CaseProximity, before);
    Run("AIHelicopter::EngageSplinePath, DisengageSplinePath", 30, PrepareNothing, CaseSplineEngage, before);
    if (roads) {
        Run("AIHelicopter::GetPlayerLaneOffsets", 40, PrepareNothing, CaseLaneOffsets, before);
        Run("AIHelicopter::ControlMoveState", 400, PrepareNothing, CaseControlMove, before);
        Run("AIHelicopter::DoAgentLogic", 600, PrepareNothing, CaseAgentLogic, before);
        Run("AIHelicopter::Update", 200, PrepareNothing, CaseUpdate, before);
    } else {
        g_skipped++;
    }

    Run("AIVehicleController::Init", 20, PrepareController, CaseControllerInit, before);
    Run("AIVehicleController's getters", 40, PrepareController, CaseControllerGets, before);
    Run("AIVehicleController's updates", 80, PrepareController, CaseUpdates, before);
    Run("AIVehicleController::IntersectionGreenLight", 100, PrepareController, CaseGreenLight, before);
    Run("AIVehicleController::Approve...Spawn", 40, PrepareController, CaseApprove, before);
    Run("AIVehicleController's registers", 200, PrepareRegister, CaseRegister, before);
    Run("AIVehicleController::Reset, Shutdown", 40, PrepareController, CaseControllerReset, before);
    Run("AIVehicleController::CreateSimTrafficCars", 60, PrepareController, CaseCreateTraffic, before);

    if (roads) {
        Run("AIRoadSpawn's statics", 40, PrepareSpawn, CaseSpawnStatics, before);
        Run("AIRoadSpawn::Get...SpawnPointOnSegment", 200, PrepareSpawn, CaseSpawnPoint, before);
        Run("AIRoadSpawn's respawn checks", 300, PrepareSpawn, CaseSpawnChecks, before);
        Run("AIRoadSpawn::RefreshSpawnData", 60, PrepareSpawn, CaseRefresh, before);
        Run("AIRoadSpawn::GetRandom...Spawn", 200, PrepareSpawn, CaseRandomSpawn, before);
    } else {
        g_skipped++;
    }

    Restore(before);
    RemoveHooks();
    for (int i = 0; i < kMirrorCount; i++)
        memcpy((void *)uintptr_t(kMirrors[i].game), g_gameCopies[i], kMirrors[i].size);
    ResetFpu();
    printf("[gameaishadowd] AI vehicles, helicopters, controller, road spawn vs originals: %ld cases, %ld checks, "
           "%ld differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameaishadowd]   %ld calls faulted\n", g_faults);
    if (g_skipped != 0)
        printf("[gameaishadowd]   %ld groups skipped (no road network)\n", g_skipped);
    fflush(stdout);
}
