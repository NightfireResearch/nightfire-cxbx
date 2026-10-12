#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAIShadowC.h"
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

#include "../anim/Actor.h"
#include "../anim/Controllers.h"
#include "../anim/IK.h"
#include "../anim/Poser.h"
#include "../engine/SimRandom.h"
#include "../game/AICharacter.h"
#include "../game/AIPedestrian.h"
#include "../game/CharacterSound.h"
#include "../game/Human.h"
#include "../game/Vehicle.h"
#include "../platform/RealMath.h"
#include "../world/Collider.h"
#include "../world/CollisionManager.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.ai's package C, run once from the first simulation tick when NIGHTFIRE_GAMEAISHADOWC is set.
// Each case runs the ORIGINAL (every entry of the package swapped back together) and then the PORT from the same
// state, and compares what they leave - the synthetic character or pedestrian and everything round it, the live
// pedestrians, the pedestrians' statics, the road records' pedestrian counts, the Simulation's random generator,
// the AI zones - the answers, and a log of every call out:
//
//   - GetDegFromAng (edges of every bucket), GetZoneHitPointScale, IsAlive, GetPedBase, GetRandomConvAnim,
//     PedInRange, GetRandomPedTexture, the barrier count;
//   - AICharacter's NotifyZoneDamage (random hit shapes and points), SetHitPointLoc, MoveToPoint, UpdateRotPos,
//     SetActor, GetZoneLastHit, GetACharacter (with a voice), and Execute on a character with a vtable of fakes
//     (every state, thrown or not, bouncing or not, its voice silent or not);
//   - the passenger's DoNeutral and HandleInterrupts;
//   - every pedestrian method but Init, Destroy, Manage, LoadPedTextures and the destructors, on a pedestrian copied
//     from a live one (its actor, navigator, ground, collider and voice synthetic), every state, sub-state and flag
//     perturbed, the threats the live cars, the other pedestrians the live ones.
//
// The actor is synthetic down to its poser, matrices and controller, so the small ActActor and AnimationController
// methods (which our build may inline) run for real on both sides. Fakes (5-byte jumps over the code both sides
// reach, at the deepest level, so that what our build inlines above them runs the same on both sides):
// ActActor::SetNewAnimation and CurrentPositionRotateY, ActPoser::Change/Get/SetAnimationOrigin and GetBoneIndex,
// the pose overrides' Create and Set, ActActorDatabase::KillActorByHandle and GetNewActorHandle, ACharacter::Say,
// WRoadNav::IncNavPosition and InitAtSegment, WWorldPos::FindClosestFace (the face kept: HeightAtPoint runs on a
// made-up face), WCollider::InRegion
// (always true, so no collider is reallocated), AIRoadSpawn::GetRandomPedSpawn, PedRespawnPosCheck and
// PedRespawnAvailable, AIZoneController::InZone, and the road counts' 0x00023990 and 0x000239b0.
//
// A mutation the test sees: GetDegFromAng testing `degrees < 22.5` for `degrees <= 22.5` (the edge cases' answers
// differ); DoAiming's `neckWeight <= 0` written `!(neckWeight > 0)` would not show (no NaN weights), so a NaN
// weight is among the inputs.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x0001c190, 0x0001c1b0, 0x0001c1d0, 0x0001c200, 0x0001c220, 0x0001c230, 0x0001c2b0, 0x0001c5a0, 0x0001c5b0,
    0x0001c640, 0x0001c690, 0x0001c850, 0x0001c870, 0x0001c9c0, 0x0001da50, 0x00023670, 0x000236c0, 0x000236d0,
    0x00023700, 0x00023890, 0x000238c0, 0x000239d0, 0x00023a90, 0x00023ad0, 0x00023b70, 0x00023ba0, 0x00023bc0,
    0x00023ce0, 0x00023e60, 0x00023f20, 0x00023f50, 0x00023fd0, 0x00024060, 0x000240e0, 0x00024140, 0x00024280,
    0x00024700, 0x00024730, 0x00024870, 0x000248b0, 0x00024910, 0x00024b30, 0x00024c30, 0x00024db0, 0x00024e50,
    0x00024ee0, 0x00024f80, 0x000253b0, 0x00025430, 0x000254c0, 0x00025d10, 0x00025e00, 0x00025ef0, 0x00025f40,
    0x00025fc0, 0x00026290, 0x000263d0, 0x00026620, 0x00026760, 0x00026800, 0x00026ed0, 0x000270b0, 0x00027150,
    0x00027240, 0x000274c0, 0x00027800, 0x00027c80,
};

typedef void (__fastcall *MethodFn)(void *, int);
typedef bool (__fastcall *BoolMethodFn)(void *, int);
typedef int (__fastcall *IntMethodFn)(void *, int);
typedef void (__fastcall *IntArgFn)(void *, int, int);
typedef bool (__fastcall *BoolIntArgFn)(void *, int, int);

#define Orig_GetDegFromAng ((double (*)(float, bool))0x00023bc0)
#define Orig_GetZoneHitPointScale ((double (__fastcall *)(void *, int, int, float))0x0001c5b0)
#define Orig_IsAlive ((IntMethodFn)0x0001c200)
#define Orig_GetPedBase ((int (*)(void))0x000239d0)
#define Orig_GetRandomConvAnim ((int (*)(bool))0x00023a90)
#define Orig_PedInRange ((bool (*)(const Coord3 *, float, bool))0x00023ad0)
#define Orig_GetRandomPedTexture ((const char *(*)(void))0x000240e0)
#define Orig_BarrierCount ((int (__fastcall *)(void *, int))0x00023f20)
#define Orig_NotifyZoneDamage ((void (__fastcall *)(void *, int, const Coord3 *))0x0001c690)
#define Orig_SetHitPointLoc ((void (__fastcall *)(void *, int, float *))0x00027c80)
#define Orig_MoveToPoint ((void (__fastcall *)(void *, int, const Coord3 *, bool))0x0001c9c0)
#define Orig_UpdateRotPosBase ((MethodFn)0x0001c850)
#define Orig_SetActorBase ((void (__fastcall *)(void *, int, AICharacterActorLink *))0x0001c220)
#define Orig_GetZoneLastHit ((IntMethodFn)0x0001c5a0)
#define Orig_GetACharacter ((ACharacter *(__fastcall *)(void *, int))0x0001c230)
#define Orig_Execute ((MethodFn)0x0001c2b0)
#define Orig_PassengerDoNeutral ((MethodFn)0x00023890)
#define Orig_PassengerHandleInterrupts ((MethodFn)0x000238c0)
#define Orig_SetTarget ((void (__fastcall *)(void *, int, const Coord3 *))0x00023ba0)
#define Orig_UpdateClosestPed ((int (__fastcall *)(void *, int, bool))0x00023ce0)
#define Orig_UpdateThreatRelativeDirection ((void (__fastcall *)(void *, int, const Coord3 *, float *, float *))0x00023e60)
#define Orig_PlayAnim ((int (__fastcall *)(void *, int, int, float))0x000253b0)
#define Orig_UpdateAnimSpeed ((double (__fastcall *)(void *, int))0x00024db0)
#define Orig_FaceTarget ((BoolIntArgFn)0x00024c30)
#define Orig_ReachedTarget ((bool (__fastcall *)(void *, int, bool))0x00024e50)
#define Orig_FindClosestCar ((PhysicsObject *(__fastcall *)(void *, int, float))0x00024ee0)
#define Orig_CheckVehicleThreat ((int (__fastcall *)(void *, int, PhysicsObject *, bool))0x00024280)
#define Orig_ThreatenedByVehicle ((IntMethodFn)0x00026760)
#define Orig_FindPointAgainstWall ((int (__fastcall *)(void *, int, float))0x00024f80)
#define Orig_PreventFalling ((MethodFn)0x00025d10)
#define Orig_SetNewTargetBasedOnThreat ((IntArgFn)0x00025e00)
#define Orig_MaintainHeading ((IntArgFn)0x00026620)
#define Orig_SetActor ((void (__fastcall *)(void *, int, AICharacterActorLink *))0x000248b0)
#define Orig_UpdateRotPos ((MethodFn)0x00024870)
#define Orig_TargetIsInRange ((BoolMethodFn)0x00024b30)
#define Orig_KillSelf ((void (__fastcall *)(void *, int, bool))0x00024140)
#define Orig_Recycle ((BoolMethodFn)0x000254c0)
#define Orig_InvalidateFurthestPed ((int (*)(int))0x00025430)
#define Orig_ForceRecycle ((void (*)(int))0x00024730)

// The pedestrian methods without arguments, by name: original, port
struct PedMethod {
    const char *name;
    uint32_t original;
    void (AICharacterPedestrian::*port)();
};

const PedMethod kPedMethods[] = {
    {"AICharacterPedestrian::DoAiming", 0x00024910, &AICharacterPedestrian::DoAiming},
    {"AICharacterPedestrian::DoNeutral", 0x00025ef0, &AICharacterPedestrian::DoNeutral},
    {"AICharacterPedestrian::DoIdling", 0x00025f40, &AICharacterPedestrian::DoIdling},
    {"AICharacterPedestrian::DoWalking", 0x000270b0, &AICharacterPedestrian::DoWalking},
    {"AICharacterPedestrian::DoAvoiding", 0x00027240, &AICharacterPedestrian::DoAvoiding},
    {"AICharacterPedestrian::DoStartled", 0x00027150, &AICharacterPedestrian::DoStartled},
    {"AICharacterPedestrian::DoDodging", 0x000274c0, &AICharacterPedestrian::DoDodging},
    {"AICharacterPedestrian::DoDying", 0x00025fc0, &AICharacterPedestrian::DoDying},
    {"AICharacterPedestrian::DoTurning", 0x00026290, &AICharacterPedestrian::DoTurning},
    {"AICharacterPedestrian::DoLeaning", 0x000263d0, &AICharacterPedestrian::DoLeaning},
    {"AICharacterPedestrian::DoDead", 0x00023b70, &AICharacterPedestrian::DoDead},
    {"AICharacterPedestrian::HandleInterrupts", 0x00027800, &AICharacterPedestrian::HandleInterrupts},
    {"AICharacterPedestrian::PreventFalling", 0x00025d10, &AICharacterPedestrian::PreventFalling},
    {"AICharacterPedestrian::UpdateRotPos", 0x00024870, &AICharacterPedestrian::UpdateRotPos},
};

// ---- the callees faked

enum HookIndex {
    kHookSetNewAnimation, kHookRotateY, kHookChangeOrigin, kHookGetOrigin, kHookSetOrigin, kHookBoneIndex,
    kHookCreateOverrides, kHookSetOverride, kHookKillActor, kHookNewActor, kHookSay, kHookIncNav, kHookInitNav,
    kHookClosestFace, kHookInRegion, kHookPedSpawn, kHookPosCheck, kHookRespawn, kHookInZone,
    kHookRoadDown, kHookRoadUp, kHookCount
};

const uint32_t kHookEntries[kHookCount] = {
    0x00012640, 0x00012280, 0x00018750, 0x00018780, 0x00018b30, 0x000186e0, 0x00015a30, 0x00015b40, 0x00013270,
    0x000139c0, 0x0011dad0, 0x000cb4b0, 0x000cb900, 0x000d31f0, 0x000bd820, 0x00035470, 0x00034bb0,
    0x00034d30, 0x00036b20, 0x000239b0, 0x00023990,
};

// ---- the game's state

#define ShadowSim ((void *)0x00233ff0)
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowStepCount I32_AT(0x00234e34)
#define ShadowCars (*(PVehicle ***)0x00234e40)
#define ShadowCarsEnd (*(PVehicle ***)0x00234e44)
#define ShadowZones ((uint8_t *)0x001e1f20)
#define ShadowGetRigidBody ((RigidBody *(__fastcall *)(void *, int, int))0x000b2700)
constexpr uint32_t kZoneBytes = 30 * 0x1c;

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults, g_skipped;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameaishadowc]   %s #%ld: %s\n", what, index, detail);
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

uint32_t g_seed = 0x0a1c0c0d;

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
    Coord3 v = {centre.x + Uniform(-spread, spread), centre.y + Uniform(-spread * 0.1f, spread * 0.1f),
                centre.z + Uniform(-spread, spread)};
    return v;
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

alignas(16) uint8_t g_pedBlock[sizeof(AICharacterPedestrian)];
alignas(16) uint8_t g_aiBlock[sizeof(AICharacterPassenger)];
alignas(16) uint8_t g_actorBlock[sizeof(ActActor)];
alignas(16) uint8_t g_poserBlock[sizeof(ActPoser)];
alignas(16) uint8_t g_matricesBlock[sizeof(ActActorMatrices)];
alignas(16) uint8_t g_controllerBlock[sizeof(AnimationController)];
alignas(16) uint8_t g_navBlock[sizeof(WRoadNav)];
alignas(16) uint8_t g_worldPosBlock[sizeof(WWorldPos)];
alignas(16) uint8_t g_colliderBlock[sizeof(WCollider)];
alignas(16) uint8_t g_voiceBlock[sizeof(ACharacter)];
alignas(16) uint8_t g_humanBlock[sizeof(Human)];
alignas(16) BarrierListEntry g_barriers[8];
AICharacterActorLink g_link, g_newLink;
AICharacterHitShape g_hitShape;
char g_zoneMap[72];
float g_hitPoints;

AICharacterPedestrian *const g_ped = reinterpret_cast<AICharacterPedestrian *>(g_pedBlock);
AICharacterPassenger *const g_ai = reinterpret_cast<AICharacterPassenger *>(g_aiBlock);
ActActor *const g_actor = reinterpret_cast<ActActor *>(g_actorBlock);
ActPoser *const g_poser = reinterpret_cast<ActPoser *>(g_poserBlock);
ActActorMatrices *const g_matrices = reinterpret_cast<ActActorMatrices *>(g_matricesBlock);
AnimationController *const g_controller = reinterpret_cast<AnimationController *>(g_controllerBlock);
WRoadNav *const g_nav = reinterpret_cast<WRoadNav *>(g_navBlock);
WWorldPos *const g_worldPos = reinterpret_cast<WWorldPos *>(g_worldPosBlock);
WCollider *const g_collider = reinterpret_cast<WCollider *>(g_colliderBlock);
ACharacter *const g_voice = reinterpret_cast<ACharacter *>(g_voiceBlock);
Human *const g_human = reinterpret_cast<Human *>(g_humanBlock);

AICharacterPedestrian *g_template;      // the live pedestrian copied, NULL none
Human *g_liveBody;
void *g_aiVtable[AICharacter::kSlotCount];
void *g_voiceVtable[4];

// One case's inputs and scripted answers
struct Inputs {
    int bone;
    int surface;
    bool faceFound, inRegion, spawnOk, posOk, respawnAvailable;
    uint32_t zoneMask;
    Coord3 origin;                  // GetAnimationOrigin's
    int16_t spawnSegment;
    int8_t spawnLane;
    float spawnAlong;
    float f0, f1;
    int i0, i1;
    Coord3 point;
} g_in;

// The answers (compared)
struct Outputs {
    double d;
    float f[2];
    int i;
    uint32_t text;
    const void *p;
    bool b;
    uint8_t pad[3];
} g_out;

// ---- the fakes

enum : uint32_t {
    kTagNewAnim = 0x4e414e4d, kTagRotate = 0x524f5459, kTagChange = 0x43484f52, kTagGetOrigin = 0x474f5247,
    kTagSetOrigin = 0x534f5247, kTagBone = 0x424f4e45, kTagCreate = 0x43524f56, kTagOverride = 0x534f5652,
    kTagKill = 0x4b494c4c, kTagNewActor = 0x4e414354, kTagSay = 0x53415920, kTagIncNav = 0x494e4e56,
    kTagInitNav = 0x49544e56, kTagFace = 0x46414345, kTagRegion = 0x52474e20,
    kTagSpawn = 0x5350574e, kTagPosCheck = 0x50434b20, kTagRespawn = 0x52535057, kTagZone = 0x5a4f4e45,
    kTagRoadDown = 0x52444e20, kTagRoadUp = 0x52555020, kTagSlot = 0x534c0000, kTagVoiceDelete = 0x56444c20,
};

AnimationController* __fastcall FakeSetNewAnimation(ActActor *actor, int, int bank, int index) {
    Log(kTagNewAnim);
    LogPointer(actor);
    Log(bank);
    Log(index);
    return g_controller;
}

void __fastcall FakeRotateY(ActActor *actor, int, float turns) {
    Log(kTagRotate);
    LogPointer(actor);
    LogFloat(turns);
}

void __fastcall FakeChangeOrigin(ActPoser *poser, int, const Coord3 *origin) {
    Log(kTagChange);
    LogPointer(poser);
    LogBytes(origin, 12);
}

void __fastcall FakeGetOrigin(ActPoser *poser, int, Coord3 *origin) {
    Log(kTagGetOrigin);
    LogPointer(poser);
    *origin = g_in.origin;
}

void __fastcall FakeSetOrigin(ActPoser *poser, int, const Coord3 *origin) {
    Log(kTagSetOrigin);
    LogPointer(poser);
    LogBytes(origin, 12);
}

int __fastcall FakeBoneIndex(ActPoser *poser, int, const char *name) {
    Log(kTagBone);
    LogPointer(poser);
    LogString(name);
    return g_in.bone;
}

void __fastcall FakeCreateOverrides(void *array, int, int count, const int *bones, bool local) {
    Log(kTagCreate);
    LogPointer(array);
    Log(count);
    LogBytes(bones, 4 * (count > 0 && count < 4 ? count : 1));
    Log(local);
}

void __fastcall FakeSetOverride(void *array, int, int index, const void *matrix, float weight) {
    Log(kTagOverride);
    LogPointer(array);
    Log(index);
    LogBytes(matrix, 64);
    LogFloat(weight);
}

void FakeKillActor(PointerListNode *handle) {
    Log(kTagKill);
    LogPointer(handle);
}

PointerListNode** FakeNewActor(PointerListNode **result, int id, const char *name, const char *weapon,
                               ActActorTransformCallback callback, const MATRIX4 *transform, bool unmirrored) {
    Log(kTagNewActor);
    Log(uint32_t(id));
    LogString(name);
    LogString(weapon);
    // the passenger's: the port hands over Bond's ride callback itself where the original hands over its address
    if ((const void *)callback == (const void *)&AICharacterBond::GetRideRotPos)
        Log(0x0001ccc0);
    else
        LogPointer((const void *)callback);
    LogBytes(transform, 64);
    Log(unmirrored);
    *result = reinterpret_cast<PointerListNode *>(&g_newLink);
    return result;
}

void __fastcall FakeSay(ACharacter *voice, int, const char *what) {
    Log(kTagSay);
    LogPointer(voice);
    LogString(what);
}

void __fastcall FakeIncNav(WRoadNav *nav, int, float step, Coord3 *heading, short target) {
    Log(kTagIncNav);
    LogPointer(nav);
    LogFloat(step);
    LogBytes(heading, 12);
    Log(uint32_t(target));
}

void __fastcall FakeInitNav(WRoadNav *nav, int, short segment, int8_t lane, float along, bool forwards) {
    Log(kTagInitNav);
    LogPointer(nav);
    Log(uint32_t(segment));
    Log(uint32_t(lane));
    LogFloat(along);
    Log(forwards);
}

bool __fastcall FakeClosestFace(WWorldPos *worldPos, int, const Coord3 *point, bool keepCurrent) {
    Log(kTagFace);
    LogPointer(worldPos);
    LogBytes(point, 12);
    Log(keepCurrent);
    worldPos->face.corner[2].tag.type = uint8_t(g_in.surface);
    return g_in.faceFound;
}

bool __fastcall FakeInRegion(WCollider *collider, int, const Coord3 *position, float radius, uint32_t mask) {
    Log(kTagRegion);
    LogPointer(collider);
    LogBytes(position, 12);
    LogFloat(radius);
    Log(mask);
    return true;
}

bool __fastcall FakePedSpawn(void *spawn, int, int16_t *segment, int8_t *lane, float *along, bool flag) {
    Log(kTagSpawn);
    LogPointer(spawn);
    Log(flag);
    *segment = g_in.spawnSegment;
    *lane = g_in.spawnLane;
    *along = g_in.spawnAlong;
    return g_in.spawnOk;
}

bool __fastcall FakePosCheck(void *spawn, int, const Coord3 *position) {
    Log(kTagPosCheck);
    LogPointer(spawn);
    LogBytes(position, 12);
    return g_in.posOk;
}

bool __fastcall FakeRespawn(void *spawn, int, const Coord3 *position) {
    Log(kTagRespawn);
    LogPointer(spawn);
    LogBytes(position, 12);
    return g_in.respawnAvailable;
}

bool __fastcall FakeInZone(void *controller, int, int zone, const Coord3 *point, float scale) {
    Log(kTagZone);
    LogPointer(controller);
    Log(zone);
    LogBytes(point, 12);
    LogFloat(scale);
    return (g_in.zoneMask >> (zone & 31)) & 1;
}

void __fastcall FakeRoadDown(void *network, int, int road) {
    Log(kTagRoadDown);
    LogPointer(network);
    Log(road);
}

void __fastcall FakeRoadUp(void *network, int, int road) {
    Log(kTagRoadUp);
    LogPointer(network);
    Log(road);
}

const void *const kHookTargets[kHookCount] = {
    (const void *)&FakeSetNewAnimation, (const void *)&FakeRotateY, (const void *)&FakeChangeOrigin,
    (const void *)&FakeGetOrigin, (const void *)&FakeSetOrigin, (const void *)&FakeBoneIndex,
    (const void *)&FakeCreateOverrides, (const void *)&FakeSetOverride, (const void *)&FakeKillActor,
    (const void *)&FakeNewActor, (const void *)&FakeSay, (const void *)&FakeIncNav, (const void *)&FakeInitNav,
    (const void *)&FakeClosestFace, (const void *)&FakeInRegion,
    (const void *)&FakePedSpawn, (const void *)&FakePosCheck, (const void *)&FakeRespawn, (const void *)&FakeInZone,
    (const void *)&FakeRoadDown, (const void *)&FakeRoadUp,
};

// The synthetic character's slots: each logs itself
template <int N> void __fastcall FakeSlot(void *object, int) {
    Log(kTagSlot + N);
    LogPointer(object);
}

#define SLOTS_8(n) &FakeSlot<n>, &FakeSlot<n + 1>, &FakeSlot<n + 2>, &FakeSlot<n + 3>, &FakeSlot<n + 4>, \
                   &FakeSlot<n + 5>, &FakeSlot<n + 6>, &FakeSlot<n + 7>
void (__fastcall *const kSlotFakes[AICharacter::kSlotCount])(void *, int) = {
    SLOTS_8(0), SLOTS_8(8), SLOTS_8(16), SLOTS_8(24), &FakeSlot<32>,
};
#undef SLOTS_8

void *__fastcall FakeVoiceDelete(void *voice, int, unsigned flags) {
    Log(kTagVoiceDelete);
    LogPointer(voice);
    Log(flags);
    return voice;
}

// ---- hooks: a jump over the first five bytes of our code (where the entry jumps to it) or of the original

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
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    for (int i = 0; i < 5; i++)
        if (code[i] == 0xc3 || code[i] == 0xc2 || code[i] == 0xcc)    // too short to take a jump
            return false;
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

int PedCount() {
    return Pedestrians.count;
}

int RandomPed(bool allowNone) {
    int count = PedCount();
    if (count == 0 || (allowNone && Chance(30)))
        return -1;
    int i = Range(0, count - 1);
    return Pedestrians.peds[i] != NULL ? i : -1;
}

int CarCount() {
    return int(ShadowCarsEnd - ShadowCars);
}

PVehicle *RandomCar() {
    return ShadowCars[Range(0, CarCount() - 1)];
}

int RandomSegment() {
    return Range(0, fgRoadNetworkData.segmentCount - 1);
}

// A segment that is not a special one: a spawn on a special segment needs the special road pending, which the game
// keeps consistent and random inputs would not
int RandomPlainSegment() {
    for (int tries = 0; tries < 50; tries++) {
        int segment = RandomSegment();
        if (!(fgRoadNetworkData.segments[segment]->flags & kSegmentSpecial))
            return segment;
    }
    return 0;
}

// A ground face under the point, for HeightAtPoint
void MakeFace(WWorldPos *worldPos, const Coord3 &point) {
    memset(worldPos, 0, sizeof(WWorldPos));
    for (int i = 0; i < 3; i++) {
        StripVertex &v = worldPos->face.corner[i];
        v.x = point.x + Uniform(-3.0f, 3.0f);
        v.y = point.y + Uniform(-3.0f, 1.0f);
        v.z = point.z + Uniform(-3.0f, 3.0f);
    }
    worldPos->valid = uint8_t(Chance(90));
}

// The actor below a character: its poser and matrices synthetic
void PrepareActor(const MATRIX4 &transform) {
    memset(g_actorBlock, 0, sizeof(g_actorBlock));
    memset(g_poserBlock, 0, sizeof(g_poserBlock));
    memset(g_controllerBlock, 0, sizeof(g_controllerBlock));
    g_actor->poser = g_poser;
    g_actor->matrices = g_matrices;
    g_actor->controller = g_controller;
    g_actor->visible = Chance(70);
    g_actor->scale = Uniform(0.8f, 1.2f);
    g_actor->baseScale = Uniform(0.8f, 1.2f);
    g_poser->mode = Range(0, 3);
    g_poser->time = Uniform(0.0f, 40.0f);
    g_poser->endTime = Chance(50) ? g_poser->time + Uniform(-0.5f, 3.0f) : Uniform(0.0f, 40.0f);
    g_poser->framesPerSecond = 20.0f;
    g_poser->frameTime = Uniform(0.01f, 0.05f);
    g_poser->timeScale = Uniform(0.5f, 2.0f);
    g_poser->poseOverrides = reinterpret_cast<ActGlobalPoseOverrideArray *>(&g_hitPoints);
    g_controller->poser = g_poser;
    g_matrices->local = transform;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++)
            g_matrices->local.mtx[r][c] += Uniform(-0.01f, 0.01f);
    g_matrices->world = g_matrices->local;
    g_link.actor = g_actor;
    g_newLink.actor = g_actor;
}

void PrepareFakes() {
    g_in.bone = Chance(80) ? Range(0, 30) : -1;
    g_in.surface = Chance(50) ? (Chance(50) ? 6 : 12) : Range(0, 15);
    g_in.faceFound = Chance(50);
    g_in.inRegion = true;
    g_in.spawnOk = Chance(80);
    g_in.posOk = Chance(80);
    g_in.respawnAvailable = Chance(15);
    g_in.zoneMask = Chance(50) ? Next() : 0;
    g_in.origin = {Uniform(-100.0f, 100.0f), Uniform(-5.0f, 5.0f), Uniform(-100.0f, 100.0f)};
    g_in.spawnSegment = int16_t(fgRoadNetworkData.segmentCount > 0 ? RandomPlainSegment() : 0);
    g_in.spawnLane = int8_t(Range(-2, 2));
    g_in.spawnAlong = Uniform(0.0f, 1.0f);
}

// A pedestrian from the live template (or made up), every field a case reads perturbed
void PreparePed() {
    if (g_template != NULL)
        memcpy(g_pedBlock, g_template, sizeof(g_pedBlock));
    else
        memset(g_pedBlock, 0, sizeof(g_pedBlock));
    AICharacterPedestrian *p = g_ped;
    p->vtable = reinterpret_cast<void **>(kAICharacterPedestrianVtable);
    if (g_template == NULL) {
        VU0_MATRIX4Init(&p->transform);
        p->transform.mtx[3][0] = Uniform(-500.0f, 500.0f);
        p->transform.mtx[3][2] = Uniform(-500.0f, 500.0f);
        p->index = PedCount();
    }
    if (CarCount() > 0 && Chance(60)) {
        // Near a car, so its threat is weighed
        const Coord3 *car = RandomCar()->GetPosition();
        Coord3 at = Around(*car, Chance(50) ? 8.0f : 40.0f);
        p->transform.mtx[3][0] = at.x;
        p->transform.mtx[3][1] = car->y + Uniform(-1.0f, 1.0f);
        p->transform.mtx[3][2] = at.z;
    }
    float turns = Uniform(0.0f, 1.0f);
    p->transform.mtx[2][0] = sinf(turns * 6.2831853f);
    p->transform.mtx[2][2] = cosf(turns * 6.2831853f);
    p->transform.mtx[0][0] = p->transform.mtx[2][2];
    p->transform.mtx[0][2] = -p->transform.mtx[2][0];
    PrepareActor(p->transform);
    p->actorLink = &g_link;
    p->playAnimController = g_controller;
    if (g_template != NULL && g_template->nav != NULL)
        memcpy(g_navBlock, g_template->nav, sizeof(g_navBlock));
    else {
        memset(g_navBlock, 0, sizeof(g_navBlock));
        g_nav->roadPoint = p->Position();
    }
    g_nav->roadPoint = Around(g_nav->roadPoint, 4.0f);
    g_nav->lane = int8_t(Range(-2, 2));
    p->nav = g_nav;
    MakeFace(g_worldPos, p->Position());
    p->worldPos = g_worldPos;
    p->collider = g_collider;
    p->voice = g_voice;
    p->body = g_liveBody != NULL ? g_liveBody : g_human;
    p->hitShape = NULL;

    // The collider's barriers: walls near the pedestrian
    memset(g_colliderBlock, 0, sizeof(g_colliderBlock));
    int barriers = Range(0, 8);
    const Coord3 &position = p->Position();
    for (int i = 0; i < barriers; i++) {
        CollisionBarrier &b = g_barriers[i].barrier;
        Coord3 a = Around(position, 5.0f), c = Around(position, 5.0f);
        b.x0 = a.x;
        b.y0 = position.y + Uniform(-2.0f, 2.0f);
        b.z0 = a.z;
        b.x1 = c.x;
        b.y1 = Chance(30) ? b.y0 + Uniform(-0.2f, 0.2f) : position.y + Uniform(-2.0f, 2.0f);
        b.z1 = c.z;
    }
    g_collider->barriers.first = barriers > 0 || Chance(50) ? g_barriers : NULL;
    g_collider->barriers.last = g_collider->barriers.first != NULL ? g_barriers + barriers : NULL;
    g_collider->barriers.end = g_collider->barriers.last;

    memset(g_voiceBlock, 0, sizeof(g_voiceBlock));
    p->state = Range(0, 22);
    p->subState = Chance(80) ? Range(0, 4) : Range(98, 110);
    p->female = Range(0, 1);
    p->pedFlags = uint8_t(Range(0, 7));
    p->dodges = uint8_t(Chance(70) ? Range(0, 4) : Range(23, 27));
    p->buddy = RandomPed(true);
    p->specialRoad = Pedestrians.specialRoadCount > 0 && Chance(30) ? Range(0, Pedestrians.specialRoadCount - 1)
                                                                     : -1;
    p->specialType = Chance(50) ? 0 : Range(1, 6);
    p->closestPed = RandomPed(true);
    p->closestPedDist = Near(Uniform(0.0f, 20.0f));
    p->previousClosestPedDist = Uniform(0.0f, 20.0f);
    p->homeTarget = Around(position, 30.0f);
    p->nextTarget = Around(position, 30.0f);
    p->target = Around(position, Chance(30) ? 1.2f : 30.0f);
    p->stateTimer = Chance(80) ? Range(0, 50) : Range(195, 205);
    p->animTimer = Range(0, 8);
    p->heading = Uniform(-0.5f, 0.5f);
    p->playerDistance = Near(Chance(50) ? 75.0f : Uniform(0.0f, 150.0f));
    p->active = uint8_t(Chance(85));
    p->road = Chance(30) ? -1 : Range(-1, fgRoadNetworkData.roadCount);
    p->previousThreat = NULL;
    p->threat = NULL;
    p->nextThreatCheck = ShadowStepCount + Range(-3, 3);
    p->threatLevel = Range(0, 3);
    p->threatOnRight = Range(0, 1);
    p->threatAngle = Near(Chance(30) ? 180.0f : Uniform(0.0f, 360.0f));
    p->threatDistance = Near(Chance(30) ? 8.0f : Uniform(0.0f, 40.0f));
    p->threatPosition = Around(position, 20.0f);
    p->threatForward = {Uniform(-1.0f, 1.0f), 0.0f, Uniform(-1.0f, 1.0f)};
    p->threatRight = {p->threatForward.z, 0.0f, -p->threatForward.x};
    p->lastThreatDistance = Uniform(0.0f, 40.0f);
    p->wallResult = Range(0, 1);
    p->nextWallCheck = ShadowStepCount + Range(-3, 3);
    p->lastWallRange = Chance(50) ? 3.2f : Uniform(1.0f, 5.0f);
    p->turnTowards = Chance(30) ? -2 : RandomPed(true);
    p->aiming = uint8_t(Chance(40));
    p->aimTarget = Around(position, 20.0f);
    p->aimTimer = Range(0, 200);
    p->aimTime = Range(0, 200);
    p->neckYaw = Near(Chance(30) ? 80.0f : Uniform(-90.0f, 90.0f));
    p->neckWeight = Chance(3) ? nanf("") : Near(Chance(50) ? Uniform(0.0f, 1.0f) : (Chance(50) ? 0.0f : 1.0f));
    p->neckWeightStep = Chance(50) ? 0.1f : -0.1f;
    p->wallPoint = Around(position, 3.0f);
    p->wallOffset = {Uniform(-1.0f, 1.0f), 0.0f, Uniform(-1.0f, 1.0f)};
    p->leanFrame = Range(0, 10);

    // The statics the methods read
    Pedestrians.voiceDelay = uint8_t(Chance(50) ? 0 : Range(1, 60));
    Pedestrians.pendingSpecialRoad =
        Pedestrians.specialRoadCount > 0 && Chance(30) ? Range(0, Pedestrians.specialRoadCount - 1) : -1;
    Pedestrians.spawnPhaseDone = uint8_t(Chance(70));
    if (fgRoadNetworkData.segmentCount > 0)
        Pedestrians.spawnSegment = int16_t(RandomPlainSegment());
    InvalidatedPed = Chance(70) ? -1 : (Chance(30) ? p->index : RandomPed(true));
    ShadowStepCount += Range(0, 3);
    for (uint32_t i = 0; i < kZoneBytes; i += 0x1c)
        I32_AT(uintptr_t(ShadowZones) + i + 0x18) = Chance(30) ? Range(2, 3) : Range(0, 4);
    PrepareFakes();
}

// The character for the base's methods and the passenger's
void PrepareCharacter() {
    memset(g_aiBlock, 0, sizeof(g_aiBlock));
    AICharacterPassenger *c = g_ai;
    for (int i = 0; i < AICharacter::kSlotCount; i++)
        g_aiVtable[i] = (void *)kSlotFakes[i];
    c->vtable = g_aiVtable;
    VU0_MATRIX4Init(&c->transform);
    float turns = Uniform(0.0f, 1.0f);
    c->transform.mtx[0][0] = cosf(turns * 6.2831853f);
    c->transform.mtx[0][2] = -sinf(turns * 6.2831853f);
    c->transform.mtx[2][0] = sinf(turns * 6.2831853f);
    c->transform.mtx[2][2] = cosf(turns * 6.2831853f);
    c->transform.mtx[3][0] = Uniform(-100.0f, 100.0f);
    c->transform.mtx[3][1] = Uniform(-5.0f, 5.0f);
    c->transform.mtx[3][2] = Uniform(-100.0f, 100.0f);
    PrepareActor(c->transform);
    c->actorLink = &g_link;
    c->state = Chance(90) ? Range(0, 22) : Range(23, 30);
    c->kind = Range(0, 4);
    c->animBank = Range(0, 16);
    c->thrown = uint8_t(Chance(60));
    c->bounces = uint8_t(Chance(60));
    VU0_MATRIX4Init(&c->thrownTransform);
    c->thrownTransform.mtx[3][0] = c->transform.mtx[3][0] + Uniform(-2.0f, 2.0f);
    c->thrownTransform.mtx[3][1] = c->transform.mtx[3][1] + Uniform(-2.0f, 2.0f);
    c->thrownTransform.mtx[3][2] = c->transform.mtx[3][2] + Uniform(-2.0f, 2.0f);
    c->thrownVelocity = {Uniform(-10.0f, 10.0f), Uniform(-10.0f, 10.0f), Uniform(-10.0f, 10.0f)};
    memset(g_humanBlock, 0, sizeof(g_humanBlock));
    MakeFace(&g_human->worldPos, c->Position());
    c->human = g_human;
    memset(g_voiceBlock, 0, sizeof(g_voiceBlock));
    *reinterpret_cast<void ***>(g_voiceBlock) = g_voiceVtable;
    g_voiceVtable[0] = (void *)&FakeVoiceDelete;
    g_voice->voice = Chance(50) ? reinterpret_cast<AVoice *>(&g_hitPoints) : NULL;
    c->voice = Chance(70) ? g_voice : NULL;

    // A hit shape: a map of random zones
    g_hitShape.width = Range(1, 8);
    g_hitShape.height = Range(1, 8);
    for (int i = 0; i < 64; i++)
        g_zoneMap[i] = char(Range(0, 4));
    g_hitShape.zoneMap = g_zoneMap;
    g_hitShape.radius = Uniform(0.3f, 1.5f);
    g_hitShape.lift = Uniform(-0.5f, 1.5f);
    g_hitShape.stretch = Uniform(0.5f, 3.0f);
    c->hitShape = Chance(85) ? &g_hitShape : NULL;
    PrepareFakes();
}

// ---- the cases

void TestPure(const Snapshot &before) {
    static const float kEdges[] = {-157.5f, -135.0f, -112.5f, -90.0f, -67.5f, -45.0f, -22.5f, 0.0f, 22.5f, 45.0f,
                                   67.5f, 90.0f, 112.5f, 135.0f, 157.5f, 180.0f};
    for (long i = 0; i < 400; i++) {
        float degrees = Chance(60) ? kEdges[Range(0, 15)] : Uniform(-200.0f, 200.0f);
        g_in.f0 = Chance(70) ? degrees / 360.0f : Near(degrees / 360.0f);
        g_in.i0 = Range(0, 1);
        RunCase("AICharacterPedestrian::GetDegFromAng", i, [] {
            g_out.d = g_original ? Orig_GetDegFromAng(g_in.f0, g_in.i0 != 0)
                                 : AICharacterPedestrian::GetDegFromAng(g_in.f0, g_in.i0 != 0);
        }, before);
    }
    for (long i = 0; i < 40; i++) {
        PrepareCharacter();
        g_in.i0 = Range(-1, 4);
        g_in.f0 = Uniform(0.0f, 100.0f);
        RunCase("AICharacter::GetZoneHitPointScale", i, [] {
            g_out.d = g_original ? Orig_GetZoneHitPointScale(g_ai, 0, g_in.i0, g_in.f0)
                                 : g_ai->GetZoneHitPointScaleBase(g_in.i0, g_in.f0);
        }, before);
        RunCase("AICharacter::IsAlive", i, [] {
            g_out.i = g_original ? Orig_IsAlive(g_ai, 0) : g_ai->IsAliveBase();
        }, before);
    }
    RunCase("AICharacterPedestrian::GetPedBase", 0, [] {
        g_out.i = g_original ? Orig_GetPedBase() : AICharacterPedestrian::GetPedBase();
    }, before);
    for (long i = 0; i < 40; i++) {
        g_in.i0 = Range(0, 1);
        RunCase("AICharacterPedestrian::GetRandomConvAnim", i, [] {
            g_out.i = g_original ? Orig_GetRandomConvAnim(g_in.i0 != 0)
                                 : AICharacterPedestrian::GetRandomConvAnim(g_in.i0 != 0);
        }, before);
    }
    for (long i = 0; i < 60; i++) {
        int ped = RandomPed(false);
        Coord3 centre = ped >= 0 ? Pedestrians.peds[ped]->Position() : Coord3{0.0f, 0.0f, 0.0f};
        g_in.point = Around(centre, 20.0f);
        g_in.f0 = Uniform(0.0f, 25.0f);
        g_in.i0 = Range(0, 1);
        RunCase("AICharacterPedestrian::PedInRange", i, [] {
            g_out.b = g_original ? Orig_PedInRange(&g_in.point, g_in.f0, g_in.i0 != 0)
                                 : AICharacterPedestrian::PedInRange(&g_in.point, g_in.f0, g_in.i0 != 0);
        }, before);
    }
    if (PedSets[Pedestrians.pedBase].count != 0 || PedSets[Pedestrians.pedBase].specialCount != 0) {
        for (long i = 0; i < 30; i++) {
            RunCase("AICharacterPedestrian::GetRandomPedTexture", i, [] {
                const char *description = g_original ? Orig_GetRandomPedTexture()
                                                     : AICharacterPedestrian::GetRandomPedTexture();
                LogString(description);
            }, before);
        }
    }
    for (long i = 0; i < 20; i++) {
        PreparePed();
        RunCase("BarrierList::Count", i, [] {
            BarrierList *list = &g_collider->barriers;
            g_out.i = g_original ? Orig_BarrierCount(list, 0) : list->Count();
        }, before);
    }
}

void TestCharacter(const Snapshot &before) {
    for (long i = 0; i < 300; i++) {
        PrepareCharacter();
        g_in.point = Around(g_ai->Position(), 2.0f);
        RunCase("AICharacter::NotifyZoneDamage", i, [] {
            if (g_original)
                Orig_NotifyZoneDamage(g_ai, 0, &g_in.point);
            else
                g_ai->NotifyZoneDamageBase(&g_in.point);
        }, before);
    }
    for (long i = 0; i < 200; i++) {
        PrepareCharacter();
        RunCase("AICharacter::Execute", i, [] {
            if (g_original)
                Orig_Execute(g_ai, 0);
            else
                g_ai->Execute();
        }, before);
    }
    for (long i = 0; i < 30; i++) {
        PrepareCharacter();
        g_in.point = Around(g_ai->Position(), 5.0f);
        g_in.i0 = Range(0, 1);
        RunCase("AICharacter::MoveToPoint", i, [] {
            if (g_original)
                Orig_MoveToPoint(g_ai, 0, &g_in.point, g_in.i0 != 0);
            else
                g_ai->MoveToPoint(&g_in.point, g_in.i0 != 0);
        }, before);
        g_hitPoints = Uniform(0.0f, 100.0f);
        g_in.i1 = Chance(80);
        RunCase("AICharacter::SetHitPointLoc", i, [] {
            float *hitPoints = g_in.i1 ? &g_hitPoints : NULL;
            if (g_original)
                Orig_SetHitPointLoc(g_ai, 0, hitPoints);
            else
                g_ai->SetHitPointLoc(hitPoints);
        }, before);
        RunCase("AICharacter::UpdateRotPos", i, [] {
            if (g_original)
                Orig_UpdateRotPosBase(g_ai, 0);
            else
                g_ai->UpdateRotPos();
        }, before);
        RunCase("AICharacter::SetActor", i, [] {
            if (g_original)
                Orig_SetActorBase(g_ai, 0, &g_newLink);
            else
                g_ai->SetActor(&g_newLink);
        }, before);
        g_ai->zoneLastHit = Range(-5, 5);
        RunCase("AICharacter::GetZoneLastHit", i, [] {
            g_out.i = g_original ? Orig_GetZoneLastHit(g_ai, 0) : g_ai->GetZoneLastHit();
        }, before);
        g_ai->voice = g_voice;
        RunCase("AICharacter::GetACharacter", i, [] {
            g_out.p = g_original ? Orig_GetACharacter(g_ai, 0) : g_ai->GetACharacter();
        }, before);
        RunCase("AICharacterPassenger::DoNeutral", i, [] {
            if (g_original)
                Orig_PassengerDoNeutral(g_ai, 0);
            else
                g_ai->DoNeutral();
        }, before);
        RunCase("AICharacterPassenger::HandleInterrupts", i, [] {
            if (g_original)
                Orig_PassengerHandleInterrupts(g_ai, 0);
            else
                g_ai->HandleInterrupts();
        }, before);
    }
}

void TestPedestrian(const Snapshot &before) {
    bool network = fgRoadNetworkData.loaded && fgRoadNetworkData.segmentCount > 0;
    for (long i = 0; i < 2400; i++) {
        PreparePed();
        // Only what the game can reach: the states' own handlers in their states, the turning one towards a
        // pedestrian or the threat
        const PedMethod &method = kPedMethods[i % (sizeof(kPedMethods) / sizeof(kPedMethods[0]))];
        if (method.original == 0x00026290 && g_ped->turnTowards == -1)
            g_ped->turnTowards = -2;
        if ((method.original == 0x00027800 || method.original == 0x00027240 || method.original == 0x000274c0 ||
             method.original == 0x000270b0 || method.original == 0x00025fc0) && !network)
            continue;
        static const PedMethod *s_method;
        s_method = &method;
        RunCase(method.name, i, [] {
            if (g_original)
                ((MethodFn)uintptr_t(s_method->original))(g_ped, 0);
            else
                (g_ped->*s_method->port)();
        }, before);
    }
    for (long i = 0; i < 300; i++) {
        PreparePed();
        g_in.i0 = Range(0, 4);
        g_in.i1 = Range(0, 1);
        g_in.f0 = Chance(50) ? 3.2f : Uniform(1.0f, 6.0f);
        g_in.point = Around(g_ped->Position(), 30.0f);
        RunCase("AICharacterPedestrian::FaceTarget", i, [] {
            g_out.b = g_original ? Orig_FaceTarget(g_ped, 0, g_in.i0) : g_ped->FaceTarget(g_in.i0);
        }, before);
        RunCase("AICharacterPedestrian::ReachedTarget", i, [] {
            g_out.b = g_original ? Orig_ReachedTarget(g_ped, 0, g_in.i1 != 0) : g_ped->ReachedTarget(g_in.i1 != 0);
        }, before);
        RunCase("AICharacterPedestrian::TargetIsInRange", i, [] {
            g_out.b = g_original ? Orig_TargetIsInRange(g_ped, 0) : g_ped->TargetIsInRange();
        }, before);
        RunCase("AICharacterPedestrian::UpdateClosestPed", i, [] {
            g_out.i = g_original ? Orig_UpdateClosestPed(g_ped, 0, g_in.i1 != 0)
                                 : g_ped->UpdateClosestPed(g_in.i1 != 0);
        }, before);
        RunCase("AICharacterPedestrian::UpdateThreatRelativeDirection", i, [] {
            if (g_original)
                Orig_UpdateThreatRelativeDirection(g_ped, 0, &g_in.point, &g_out.f[0], &g_out.f[1]);
            else
                g_ped->UpdateThreatRelativeDirection(&g_in.point, &g_out.f[0], &g_out.f[1]);
        }, before);
        RunCase("AICharacterPedestrian::PlayAnim", i, [] {
            g_out.i = g_original ? Orig_PlayAnim(g_ped, 0, g_in.i0 * 7, g_in.f0) : g_ped->PlayAnim(g_in.i0 * 7, g_in.f0);
        }, before);
        RunCase("AICharacterPedestrian::UpdateAnimSpeed", i, [] {
            g_out.d = g_original ? Orig_UpdateAnimSpeed(g_ped, 0) : g_ped->UpdateAnimSpeed();
        }, before);
        RunCase("AICharacterPedestrian::FindPointAgainstWall", i, [] {
            g_out.i = g_original ? Orig_FindPointAgainstWall(g_ped, 0, g_in.f0) : g_ped->FindPointAgainstWall(g_in.f0);
        }, before);
        RunCase("AICharacterPedestrian::SetNewTargetBasedOnThreat", i, [] {
            if (g_original)
                Orig_SetNewTargetBasedOnThreat(g_ped, 0, g_in.i0);
            else
                g_ped->SetNewTargetBasedOnThreat(g_in.i0);
        }, before);
        RunCase("AICharacterPedestrian::SetTarget", i, [] {
            if (g_original)
                Orig_SetTarget(g_ped, 0, &g_in.point);
            else
                g_ped->SetTarget(&g_in.point);
        }, before);
        RunCase("AICharacterPedestrian::SetActor", i, [] {
            if (g_original)
                Orig_SetActor(g_ped, 0, &g_newLink);
            else
                g_ped->SetActor(&g_newLink);
        }, before);
        RunCase("AICharacterPedestrian::KillSelf", i, [] {
            if (g_original)
                Orig_KillSelf(g_ped, 0, g_in.i1 != 0);
            else
                g_ped->KillSelf(g_in.i1 != 0);
        }, before);
        if (network) {
            RunCase("AICharacterPedestrian::MaintainHeading", i, [] {
                if (g_original)
                    Orig_MaintainHeading(g_ped, 0, g_in.i0);
                else
                    g_ped->MaintainHeading(g_in.i0);
            }, before);
            RunCase("AICharacterPedestrian::Recycle", i, [] {
                g_out.b = g_original ? Orig_Recycle(g_ped, 0) : g_ped->Recycle();
            }, before);
        }
        if (CarCount() > 0) {
            g_in.i0 = Range(0, CarCount() - 1);
            RunCase("AICharacterPedestrian::CheckVehicleThreat", i, [] {
                PhysicsObject *car = ShadowCars[g_in.i0];
                g_out.i = g_original ? Orig_CheckVehicleThreat(g_ped, 0, car, g_in.i1 != 0)
                                     : g_ped->CheckVehicleThreat(car, g_in.i1 != 0);
            }, before);
            g_in.f0 = Chance(50) ? 1000.0f : Uniform(10.0f, 3000.0f);
            RunCase("AICharacterPedestrian::FindClosestCar", i, [] {
                g_out.p = g_original ? Orig_FindClosestCar(g_ped, 0, g_in.f0) : g_ped->FindClosestCar(g_in.f0);
            }, before);
            RunCase("AICharacterPedestrian::ThreatenedByVehicle", i, [] {
                g_out.i = g_original ? Orig_ThreatenedByVehicle(g_ped, 0) : g_ped->ThreatenedByVehicle();
            }, before);
        }
        if (PedCount() > 0) {
            g_in.i0 = Range(-1, PedCount() - 1);
            RunCase("AICharacterPedestrian::InvalidateFurthestPed", i, [] {
                g_out.i = g_original ? Orig_InvalidateFurthestPed(g_in.i0)
                                     : AICharacterPedestrian::InvalidateFurthestPed(g_in.i0);
            }, before);
            g_in.i0 = Range(0, PedCount() - 1);
            if (Pedestrians.peds[g_in.i0] != NULL) {
                RunCase("AICharacterPedestrian::ForceRecycle", i, [] {
                    if (g_original)
                        Orig_ForceRecycle(g_in.i0);
                    else
                        AICharacterPedestrian::ForceRecycle(g_in.i0);
                }, before);
            }
        }
    }
}

void BuildRegions() {
    AddRegion(g_pedBlock, sizeof(g_pedBlock), "the pedestrian");
    AddRegion(g_aiBlock, sizeof(g_aiBlock), "the character");
    AddRegion(g_actorBlock, sizeof(g_actorBlock), "the actor");
    AddRegion(g_poserBlock, sizeof(g_poserBlock), "the poser");
    AddRegion(g_matricesBlock, sizeof(g_matricesBlock), "the actor's matrices");
    AddRegion(g_controllerBlock, sizeof(g_controllerBlock), "the controller");
    AddRegion(g_navBlock, sizeof(g_navBlock), "the navigator");
    AddRegion(g_worldPosBlock, sizeof(g_worldPosBlock), "the ground");
    AddRegion(g_colliderBlock, sizeof(g_colliderBlock), "the collider");
    AddRegion(g_barriers, sizeof(g_barriers), "the barriers");
    AddRegion(g_voiceBlock, sizeof(g_voiceBlock), "the voice");
    AddRegion(g_humanBlock, sizeof(g_humanBlock), "the human");
    AddRegion(&g_link, sizeof(g_link), "the actor link");
    AddRegion(&g_newLink, sizeof(g_newLink), "the new actor link");
    AddRegion(&g_hitShape, sizeof(g_hitShape), "the hit shape");
    AddRegion(&g_hitPoints, sizeof(g_hitPoints), "the hit points");
    for (int i = 0; i < PedCount(); i++)
        AddRegion(Pedestrians.peds[i], sizeof(AICharacterPedestrian), "a live pedestrian");
    AddRegion(&Pedestrians, sizeof(PedestrianStatics), "the pedestrians' statics");
    AddRegion(&InvalidatedPed, 4, "InvalidatedPed");
    if (fgRoadNetworkData.loaded)
        for (int i = 0; i < fgRoadNetworkData.roadCount; i++)
            AddRegion(fgRoadNetworkData.roads[i], sizeof(WRoad), "a road");
    AddRegion(ShadowRandom, sizeof(SimRandom), "the Simulation's random numbers");
    AddRegion(ShadowZones, kZoneBytes, "the AI zones");
    AddRegion(&fgCollisionMgr->barrierMask, 4, "the barrier mask");
    AddRegion(&g_out, sizeof(g_out), "the answers");
    AddRegion(&g_in, sizeof(g_in), "the inputs", false);
    AddRegion(&ShadowStepCount, 4, "SimStepCount", false);
}

bool InstallHooks() {
    int missing = 0;
    for (int i = 0; i < kHookCount; i++)
        if (!HookInstall(&g_hooks[i], kHookEntries[i], kHookTargets[i]))
            missing++;
    if (missing != 0)
        printf("[gameaishadowc] %d of the %d fakes could not be installed - nothing run\n", missing, int(kHookCount));
    return missing == 0;
}

}  // namespace

void GameAIShadowC_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEAISHADOWC", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    for (int i = 0; i < PedCount(); i++)
        if (Pedestrians.peds[i] != NULL && (g_template == NULL || Pedestrians.peds[i]->state != kStateDead))
            g_template = Pedestrians.peds[i];
    g_liveBody = g_template != NULL ? g_template->body : NULL;
    printf("[gameaishadowc] live at the first tick: %d pedestrians (template %s), %d cars, road network %s\n",
           PedCount(), g_template != NULL ? "live" : "made up", CarCount(), fgRoadNetworkData.loaded ? "loaded" : "none");
    if (!InstallHooks()) {
        for (Hook &hook : g_hooks)
            HookRemove(&hook);
        fflush(stdout);
        return;
    }
    BuildRegions();
    Snapshot before;
    Capture(&before);

    TestPure(before);
    TestCharacter(before);
    TestPedestrian(before);

    Restore(before);
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
    ResetFpu();
    printf("[gameaishadowc] characters, passenger, pedestrians vs originals: %ld cases, %ld checks, %ld differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameaishadowc]   %ld calls faulted\n", g_faults);
    if (g_skipped != 0)
        printf("[gameaishadowc]   %ld groups skipped\n", g_skipped);
    fflush(stdout);
}
