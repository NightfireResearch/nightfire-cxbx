#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAIShadowF.h"
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
#include "../anim/Character.h"
#include "../anim/Controllers.h"
#include "../anim/IK.h"
#include "../anim/Poser.h"
#include "../anim/Weapon.h"
#include "../camera/Camera.h"
#include "../camera/PlayerCamera.h"
#include "../data/UData.h"
#include "../engine/CoreContainers.h"
#include "../engine/GameLoop.h"
#include "../game/MissionManager.h"
#include "../engine/SimRandom.h"
#include "../game/AICharacter.h"
#include "../game/AIElements.h"
#include "../game/AIEnemyRiders.h"            // the riders' actor callbacks
#include "../game/AIHands.h"
#include "../game/AIVehicle.h"
#include "../game/Helicopter.h"
#include "../game/Human.h"
#include "../game/Vehicle.h"
#include "../game/WeaponManager.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RenderHigh.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../world/Targeting.h"
#include "../world/World.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.ai's package F, run once from the first simulation tick when NIGHTFIRE_GAMEAISHADOWF is set.
// Each case runs the ORIGINAL (every entry of the package swapped back together) and then the PORT from the same
// state, and compares what they leave - the synthetic elements, their tracking and controller, the characters,
// humans, cars, car AI and actors round them, the hands and Bond, the allocations (an arena both sides draw from in
// the same order), the zone, hands' and Bond's statics at the game's addresses, fgAIElementController, the mission
// manager's fields, the Simulation's random generator, the road records - the answers, and a log of every call out:
//
//   - CompareRanges (NaNs), FloatCeil and FloatFloor (halves, large values, infinities, NaN), BlendVectors (in
//     place too), RCamera's field of view setter (2 and its neighbours), the road counts (roads out of range), the
//     navigator's SegmentSpeed and LeftDrivableDist on every real segment both ways;
//   - the zones: AIZone's constructor, Get, Init, Update, Shutdown, InZone and RegisterZone on random zone tables;
//   - the tracking: SetWakeRange, GetProperty (every name and one unknown), UpdatePosition and Update (wake ranges
//     put exactly at the element's distance among the cases);
//   - the controller: the constructor on a synthetic 'AIEl' record set (snow2a_mis4 on the hardest difficulty
//     among the cases), Create, Destroy, GetStats, EveryoneChill, OkToSpawnHench, RedirectCarpElement and
//     GetCarpElementTracking (records before and after the array), CalcMightAsWellBeDead, GetNumMightAsWellBeDead,
//     GoToSleep, ForceVehicleToSleep, SyncRuleInfo, WakeUp (every difficulty filter, every seat, characters of
//     every kind, the actor database there or not) and Update (the slots too few and enough);
//   - the hands: every method, Init (the hand weapons' loaded flags at random) and Shutdown, on a synthetic
//     AICharacterHands with the game's vtable (so DoNeutral, DoIdling and DoFiring reach the other state handlers
//     as the game does);
//   - Bond: every method and Init (the mission set's choice at random), on a synthetic AICharacterBond with the
//     game's vtable.
//
// The elements' characters and cars have vtables of fakes (scripted answers); the actors are synthetic down to their
// poser, matrices, controller, character and weapon, so ActActor's small methods (IsAnimationDone, SetTimeScale,
// GetActorLocalPosOri, SetIKInfoArray...) run for real on both sides. Fakes (5-byte jumps over the code both sides
// reach): ActActor::SetNewAnimation, RotateActor and RotateActorX; ActPoser::GetBoneIndex; the IK array's CreateIKs
// and SetInfo; the pose overrides' Create and Set (array); ActActorDatabase::GetNewActorHandle (both) and
// KillActorByHandle; ActCharacter::LoadCharacter; RSceneObj::LookupFXHandle, LocateFX and PreLoad;
// AttributeSet::LookupVector, LookupInt and Destruct; PVehicle::GetNamedAttribs and RenderNameAttrib;
// AssertMessage; Human::SetAICharacter, Activate and Deactivate; the Simulation's SpawnCarObject, SpawnHumanObject,
// SpawnHelicopterObject, DeletePhysicsObject and GetSimpleRigidBody; SMissionManager's ClearProgrammerEvent and
// ProgrammerDefinedEvent; EAICommand's constructor; Event::operator new; AIVehicle::GetPosition and GetRotPos;
// AIVehicleController::Get and its (De)RegisterSimCar and (De)RegisterHelicopter; AIGroundVehicle::SetNavigateMode,
// SetAttackMode and UnSpawn; the six enemy constructors; AICharacter::SetHitPointLoc and Destruct;
// AMenuSound::Trigger; RPlayerCamera::UpdateCamera and GetAimMatrix4; RPlayerCamState::AimZoom and AimRelease; and,
// only while a side runs, OperatorNew, OperatorNewArray, OperatorDelete, UMemory::FastAlloc and FastFree.
//
// A mutation the test sees: AIElementController::GoToSleep's `hitPoints > 0` written `hitPoints >= 0` (cars with
// exactly 0 hit points go back to sleep rather than dead); and AIElementTracking::Update's `distance < wakeRange`
// written `<=` (wake ranges equal to the distance are among the cases).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x00011000, 0x0001cbb0, 0x0001cbf0, 0x0001cc00, 0x0001cc30, 0x0001ccc0, 0x0001cd80, 0x0001d090, 0x0001d0c0,
    0x0001d0e0, 0x0001d1c0, 0x0001d2a0, 0x000227a0, 0x000227c0, 0x00022820, 0x00022830, 0x00022860, 0x00022870,
    0x000228b0, 0x000228e0, 0x00022930, 0x00022c30, 0x00022c50, 0x00022cc0, 0x00022dd0, 0x00022e20, 0x00022e70,
    0x00022f10, 0x00022f70, 0x00022fb0, 0x000230a0, 0x00023170, 0x00023230, 0x00023280, 0x000232b0, 0x00023950,
    0x00023990, 0x000239b0, 0x00027cb0, 0x00027ce0, 0x00027f00, 0x00028190, 0x000285b0, 0x00028620, 0x00028630,
    0x000286b0, 0x000286d0, 0x00028700, 0x00028730, 0x000287c0, 0x000287d0, 0x000289f0, 0x00028a40, 0x00028a90,
    0x00028ab0, 0x00028b60, 0x00028bb0, 0x00029880, 0x00029c90, 0x00029cb0, 0x00029cd0, 0x00029cf0, 0x00029d20,
    0x00029d40, 0x00036a00, 0x00036a10, 0x00036a20, 0x00036ac0, 0x00036b00, 0x00036b20, 0x00036bb0,
};

// ---- the game's state

#define ShadowSim ((void *)0x00233ff0)
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowStepCount I32_AT(0x00234e34)
#define ShadowCars (*(GameVector<PVehicle *> *)0x00234e3c)
#define ShadowLaunch (*(LaunchPage *)0x00243b90)
#define ShadowWeaponManager (*(SWeaponManager **)0x0023923c)
#define ShadowGetRigidBody ((RigidBody *(__fastcall *)(void *, int, int))0x000b2700)
#define ShadowBondButtons ((uint8_t *)0x001dda2c)
#define ShadowBondHandIK ((uint8_t *)0x001b5240)

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameaishadowf]   %s #%ld: %s\n", what, index, detail);
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

uint32_t g_seed = 0x0a1c0f0f;

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

float NaN() {
    uint32_t bits = 0x7fc00000;
    float value;
    memcpy(&value, &bits, 4);
    return value;
}

void RandomMatrix(MATRIX4 *m, const Coord3 &at, float spread) {
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++)
            m->mtx[r][c] = c == 3 ? 0.0f : Uniform(-1.0f, 1.0f);
    m->mtx[3][0] = at.x + Uniform(-spread, spread);
    m->mtx[3][1] = at.y + Uniform(-spread * 0.1f, spread * 0.1f);
    m->mtx[3][2] = at.z + Uniform(-spread, spread);
    m->mtx[3][3] = 1.0f;
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

// An actor callback: the port hands over its own functions where the original hands over their addresses
void LogCallback(ActActorTransformCallback callback) {
    const void *p = (const void *)callback;
    if (p == (const void *)&AICharacterBond::GetRideRotPos)
        Log(0x0001ccc0);
    else if (p == (const void *)&AICharacterHands::GetRotPos)
        Log(0x000228e0);
    else if (p == (const void *)static_cast<void (*)(AICharacterEnemyRider *, MATRIX4 *, Coord4 *)>(
                      &AICharacterEnemyRider::GetRotPos))
        Log(0x0001d530);
    else if (p == (const void *)static_cast<void (*)(AICharacterEnemySnow *, MATRIX4 *, Coord4 *)>(
                      &AICharacterEnemySnow::GetRotPos))
        Log(0x0001fb70);
    else
        LogPointer(p);
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

constexpr int kElements = 6;
constexpr int kHumans = 12;             // the elements' (0-5), then the spawns'
constexpr int kActors = 10;
constexpr int kCarSlots = 83;
constexpr int kCharacterSlots = 36;

alignas(16) AIElement g_elements[kElements];
alignas(16) AIElement g_carp[kElements];
AIElementTracking g_tracking[kElements];
int32_t g_wakeList[kElements];
AIElementRange g_rangeList[kElements];
alignas(16) uint8_t g_controllerBlock[sizeof(AIElementController)];
alignas(16) uint8_t g_newControllerBlock[sizeof(AIElementController)];
alignas(16) uint8_t g_charBlocks[kElements][0x1c0];
alignas(16) uint8_t g_humanBlocks[kHumans][sizeof(Human)];
alignas(16) uint8_t g_carBlocks[3][sizeof(PHelicopter)];       // an element's car, its helicopter, a spawned car
alignas(16) uint8_t g_carAIBlock[sizeof(AIGroundVehicle)];
alignas(16) uint8_t g_vehicleAIBlock[sizeof(AIVehicle)];
alignas(16) uint8_t g_simpleBodyBlock[sizeof(SimpleRigidBody)];
alignas(16) uint8_t g_actorBlocks[kActors][sizeof(ActActor)];
alignas(16) uint8_t g_poserBlock[sizeof(ActPoser)];
alignas(16) uint8_t g_matricesBlock[sizeof(ActActorMatrices)];
alignas(16) uint8_t g_actCharacterBlock[sizeof(ActCharacter)];
alignas(16) uint8_t g_weaponBlock[sizeof(ActWeapon)];
alignas(16) uint8_t g_animControllerBlock[sizeof(AnimationController)];
alignas(16) uint8_t g_arrayBlock[0x20];
PointerListNode g_nodes[kActors];
alignas(16) uint8_t g_handsBlock[sizeof(AICharacterHands)];
alignas(16) uint8_t g_bondBlock[sizeof(AICharacterBond)];
alignas(16) uint8_t g_arena[0x8000];
alignas(16) uint8_t g_navBlock[sizeof(WRoadNav)];
alignas(16) uint8_t g_cameraBlock[sizeof(RCamera)];
uint8_t g_zoneInstance[4];
uint8_t g_command[4];
int g_effects[2];

struct DataBlock {
    UData header;
    AIElement records[kElements];
};
alignas(16) DataBlock g_data;

AIElementController *const g_controller = reinterpret_cast<AIElementController *>(g_controllerBlock);
AIElementController *const g_newController = reinterpret_cast<AIElementController *>(g_newControllerBlock);
AICharacterHands *const g_hands = reinterpret_cast<AICharacterHands *>(g_handsBlock);
AICharacterBond *const g_bond = reinterpret_cast<AICharacterBond *>(g_bondBlock);
ActPoser *const g_poser = reinterpret_cast<ActPoser *>(g_poserBlock);
ActActorMatrices *const g_matrices = reinterpret_cast<ActActorMatrices *>(g_matricesBlock);
ActCharacter *const g_actCharacter = reinterpret_cast<ActCharacter *>(g_actCharacterBlock);
AnimationController *const g_animController = reinterpret_cast<AnimationController *>(g_animControllerBlock);
AIGroundVehicle *const g_carAI = reinterpret_cast<AIGroundVehicle *>(g_carAIBlock);
WRoadNav *const g_nav = reinterpret_cast<WRoadNav *>(g_navBlock);

ActActor *Actor(int i) {
    return reinterpret_cast<ActActor *>(g_actorBlocks[i]);
}
AICharacter *Character(int i) {
    return reinterpret_cast<AICharacter *>(g_charBlocks[i]);
}
Human *HumanBlock(int i) {
    return reinterpret_cast<Human *>(g_humanBlocks[i]);
}
PhysicsObject *CarBlock(int i) {
    return reinterpret_cast<PhysicsObject *>(g_carBlocks[i]);
}

void *g_carVtable[kCarSlots];
void *g_charVtable[kCharacterSlots];
Coord3 g_player;                        // the live player car's position
int g_playerSlot;

// One case's inputs and scripted answers
struct Inputs {
    bool alive[2];                      // IsAlive: the elements' characters, the spawned ones
    bool carAI;
    int reset;
    bool carIsHelicopter;
    int carClass;
    int tires;
    bool shredded[4];
    AttributeVector vectors[8];
    bool intFound[3];
    int32_t intValue[3];
    int renderName;                     // 0 NULL, 1 "paris_hench...", 2 another
    bool spawnCarNull;
    bool humanNull[4];
    bool fxFound[2];
    MATRIX4 fxMatrices[2];
    int bones[2];
    Coord4 vehiclePosition;
    MATRIX4 vehicleRotPos;
    MATRIX4 aim;
    Coord4 player;
    float f0, f1, f2;
    int i0, i1;
    Coord3 point;
    Coord4 a, b;
    void *p0;
} g_in;

// The answers (compared)
struct Outputs {
    double d;
    MATRIX4 m;
    Coord4 v;
    int i;
    const void *p;
    bool b;
    uint8_t pad[3];
} g_out;

// The fakes' counters, from 0 for each side
struct Counters {
    uint32_t arena, nodes, humans, vectors, ints, fx, bones;
} g_count;

// ---- the fakes

enum : uint32_t {
    kTagNewAnim = 0x4e414e4d, kTagBone = 0x424f4e45, kTagCreateIK = 0x43494b20, kTagSetInfo = 0x53494e46,
    kTagCreateOv = 0x43524f56, kTagSetOvs = 0x534f5653, kTagRotateX = 0x524f5458, kTagRotate = 0x524f5420,
    kTagNewActor = 0x4e414354, kTagNewActor2 = 0x4e414332, kTagKill = 0x4b494c4c, kTagLoad = 0x4c4f4144,
    kTagLookupFX = 0x4c464658, kTagLocateFX = 0x4c4f4658, kTagPreLoad = 0x50524c44, kTagVector = 0x56454354,
    kTagInt = 0x494e5420, kTagAttrDel = 0x41444c20, kTagNamed = 0x4e414d44, kTagRender = 0x524e4452,
    kTagAssert = 0x41535254, kTagSetAI = 0x53414920, kTagActivate = 0x41435456, kTagDeactivate = 0x44414354,
    kTagSpawnCar = 0x53434152, kTagSpawnHuman = 0x5348554d, kTagSpawnHeli = 0x53484c49, kTagDelete = 0x44454c20,
    kTagSimple = 0x53494d50, kTagClearEvent = 0x434c4556, kTagEvent = 0x45564e54, kTagCommand = 0x434d4e44,
    kTagEventNew = 0x45564e57, kTagNew = 0x4e455720, kTagNewArray = 0x4e45575b, kTagFree = 0x46524545,
    kTagFastAlloc = 0x46414c43, kTagFastFree = 0x46465245, kTagPosition = 0x504f5320, kTagRotPos = 0x524f5450,
    kTagRegCar = 0x52434152, kTagDeregCar = 0x44434152, kTagRegHeli = 0x52484c49,
    kTagDeregHeli = 0x44484c49, kTagNavMode = 0x4e415620, kTagAttack = 0x41544b20, kTagUnSpawn = 0x554e5350,
    kTagEnemy = 0x454e4d00, kTagHitLoc = 0x48495450, kTagCharDestruct = 0x43445354, kTagSound = 0x534e4420,
    kTagUpdateCamera = 0x55434d52, kTagAim = 0x41494d20, kTagZoom = 0x5a4f4f4d, kTagRelease = 0x524c5345,
    kTagCarSlot = 0x43530000, kTagCharSlot = 0x43480000,
};

// An allocation from the arena (16-byte aligned, the same addresses on both sides)
void *ArenaAlloc(uint32_t size) {
    uint32_t at = (g_count.arena + 15) & ~15u;
    if (at + size > sizeof(g_arena))
        return NULL;
    g_count.arena = at + size;
    return g_arena + at;
}

AnimationController* __fastcall FakeSetNewAnimation(ActActor *actor, int, int bank, int index) {
    Log(kTagNewAnim);
    LogPointer(actor);
    Log(bank);
    Log(index);
    return g_animController;
}

int __fastcall FakeBoneIndex(ActPoser *poser, int, const char *name) {
    Log(kTagBone);
    LogPointer(poser);
    LogString(name);
    return g_in.bones[g_count.bones++ & 1];
}

void __fastcall FakeCreateIKs(void *array, int, int count, const int *bones, const Coord4 *axes,
                              const bool *transformTargets) {
    Log(kTagCreateIK);
    LogPointer(array);
    Log(count);
    LogBytes(bones, 8);
    LogBytes(axes, 32);
    LogBytes(transformTargets, 2);
}

void __fastcall FakeSetInfo(void *array, int, int index, const ActIKSolveInfo *info) {
    Log(kTagSetInfo);
    LogPointer(array);
    Log(index);
    LogBytes(info, 0x28);
}

void __fastcall FakeCreateOverrides(void *array, int, int count, const int *bones, bool local) {
    Log(kTagCreateOv);
    LogPointer(array);
    Log(count);
    LogBytes(bones, 8);
    Log(local);
}

void __fastcall FakeSetOverrides(void *array, int, int count, const void *matrices, const float *weights) {
    Log(kTagSetOvs);
    LogPointer(array);
    Log(count);
    LogBytes(matrices, 128);
    LogBytes(weights, 8);
}

void __fastcall FakeRotateX(ActActor *actor, int, float turns) {
    Log(kTagRotateX);
    LogPointer(actor);
    LogFloat(turns);
}

void __fastcall FakeRotate(ActActor *actor, int, float turns) {
    Log(kTagRotate);
    LogPointer(actor);
    LogFloat(turns);
}

PointerListNode *NextNode() {
    PointerListNode *node = &g_nodes[g_count.nodes++ % kActors];
    return node;
}

PointerListNode** FakeNewActor(PointerListNode **result, int id, const char *name, const char *weapon,
                               ActActorTransformCallback callback, const MATRIX4 *transform, bool unmirrored) {
    Log(kTagNewActor);
    Log(uint32_t(id));
    LogString(name);
    LogString(weapon);
    LogCallback(callback);
    LogBytes(transform, 64);
    Log(unmirrored);
    *result = NextNode();
    return result;
}

PointerListNode** FakeNewActor2(PointerListNode **result, int id, const char *name, const char *weapon1,
                                const char *weapon2, ActActorTransformCallback callback, const MATRIX4 *transform,
                                bool unmirrored) {
    Log(kTagNewActor2);
    Log(uint32_t(id));
    LogString(name);
    LogString(weapon1);
    LogString(weapon2);
    LogCallback(callback);
    LogBytes(transform, 64);
    Log(unmirrored);
    *result = NextNode();
    return result;
}

void FakeKillActor(PointerListNode *handle) {
    Log(kTagKill);
    LogPointer(handle);
}

void FakeLoadCharacter(const char *description, const char *weapon1, const char *weapon2) {
    Log(kTagLoad);
    LogString(description);
    LogString(weapon1);
    LogString(weapon2);
}

void* __fastcall FakeLookupFX(void *object, int, const char *name, int instance) {
    Log(kTagLookupFX);
    LogPointer(object);
    LogString(name);
    Log(instance);
    int which = g_count.fx++ & 1;
    return g_in.fxFound[which] ? &g_effects[which] : NULL;
}

void __fastcall FakeLocateFX(void *object, int, const void *effect, MATRIX4 *transform, bool world) {
    Log(kTagLocateFX);
    LogPointer(object);
    LogPointer(effect);
    Log(world);
    *transform = g_in.fxMatrices[effect == &g_effects[1] ? 1 : 0];
}

void FakePreLoad(const char *directory, const char *file, const char *label) {
    Log(kTagPreLoad);
    LogString(directory);
    LogString(file);
    LogString(label);
}

const AttributeVector* __fastcall FakeLookupVector(void *attributes, int, const char *key, bool *found) {
    Log(kTagVector);
    LogPointer(attributes);
    LogString(key);
    LogPointer(found);
    return &g_in.vectors[g_count.vectors++ & 7];
}

int32_t __fastcall FakeLookupInt(void *attributes, int, const char *key, bool *found) {
    Log(kTagInt);
    LogPointer(attributes);
    LogString(key);
    int which = g_count.ints++ % 3;
    if (found != NULL && g_in.intFound[which])
        *found = true;
    return g_in.intValue[which];
}

void __fastcall FakeAttributeDestruct(void *attributes, int) {
    Log(kTagAttrDel);
    LogBytes(attributes, 4);
}

AttributeSet* FakeNamedAttribs(AttributeSet *result, const char *name) {
    Log(kTagNamed);
    LogString(name);
    result->collection = reinterpret_cast<AttributeCollection *>(uintptr_t(0x00c0ffee));
    return result;
}

const char* FakeRenderName(AttributeSet *attributes) {
    Log(kTagRender);
    LogBytes(attributes, 4);
    switch (g_in.renderName) {
    case 0:
        return NULL;
    case 1:
        return "paris_hench_a.crp";
    default:
        return "other.crp";
    }
}

void FakeAssert(const char *format, ...) {
    Log(kTagAssert);
    LogString(format);
}

void __fastcall FakeSetAICharacter(Human *human, int, AICharacter *character) {
    Log(kTagSetAI);
    LogPointer(human);
    LogPointer(character);
}

void __fastcall FakeActivate(Human *human, int) {
    Log(kTagActivate);
    LogPointer(human);
}

void __fastcall FakeDeactivate(Human *human, int) {
    Log(kTagDeactivate);
    LogPointer(human);
}

PhysicsObject* __fastcall FakeSpawnCar(void *sim, int, int carClass, const char *type, uint32_t colour,
                                       const Coord3 *direction, const Coord3 *position) {
    Log(kTagSpawnCar);
    LogPointer(sim);
    Log(carClass);
    LogString(type);
    Log(colour & 0xff);
    LogBytes(direction, 12);
    LogBytes(position, 12);
    return g_in.spawnCarNull ? NULL : CarBlock(2);
}

Human* __fastcall FakeSpawnHuman(void *sim, int, int kind, const Coord3 *direction, const Coord3 *position,
                                 uint32_t ownerSig) {
    Log(kTagSpawnHuman);
    LogPointer(sim);
    Log(kind);
    LogBytes(direction, 12);
    LogBytes(position, 12);
    Log(ownerSig);
    uint32_t n = g_count.humans++;
    if (g_in.humanNull[n & 3])
        return NULL;
    return HumanBlock(kElements + int(n % (kHumans - kElements)));
}

PHelicopter* __fastcall FakeSpawnHeli(void *sim, int, const char *type, const Coord3 *direction,
                                      const Coord3 *position, int unknown) {
    Log(kTagSpawnHeli);
    LogPointer(sim);
    LogString(type);
    LogBytes(direction, 12);
    LogBytes(position, 12);
    Log(unknown);
    return reinterpret_cast<PHelicopter *>(CarBlock(1));
}

void __fastcall FakeDeleteObject(void *sim, int, PhysicsObject *object) {
    Log(kTagDelete);
    LogPointer(sim);
    LogPointer(object);
}

SimpleRigidBody* __fastcall FakeSimpleBody(void *sim, int, int slot) {
    Log(kTagSimple);
    LogPointer(sim);
    Log(slot);
    return reinterpret_cast<SimpleRigidBody *>(g_simpleBodyBlock);
}

void __fastcall FakeClearEvent(void *manager, int, int event) {
    Log(kTagClearEvent);
    LogPointer(manager);
    Log(event);
}

void __fastcall FakeEvent(void *manager, int, int event, const char *text) {
    Log(kTagEvent);
    LogPointer(manager);
    Log(event);
    LogString(text);
}

void* __fastcall FakeCommand(void *event, int, void *command, void *element, void *target, void *vehicle,
                             void *trigger, uint32_t bySimpleBody, int slot) {
    Log(kTagCommand);
    LogPointer(event);
    LogPointer(command);
    LogPointer(element);
    LogPointer(target);
    LogPointer(vehicle);
    LogPointer(trigger);
    Log(bySimpleBody & 0xff);
    Log(slot);
    return event;
}

void* FakeEventNew(size_t size) {
    Log(kTagEventNew);
    Log(uint32_t(size));
    return ArenaAlloc(uint32_t(size));
}

void* FakeOperatorNew(unsigned int size) {
    Log(kTagNew);
    Log(size);
    return ArenaAlloc(size);
}

void* FakeOperatorNewArray(unsigned int size) {
    Log(kTagNewArray);
    Log(size);
    return ArenaAlloc(size);
}

void FakeOperatorDelete(void *block) {
    Log(kTagFree);
    LogPointer(block);
}

void* FakeFastAlloc(unsigned int size, const char *name) {
    Log(kTagFastAlloc);
    Log(size);
    LogString(name);
    return ArenaAlloc(size);
}

void FakeFastFree(void *block, unsigned int size) {
    Log(kTagFastFree);
    LogPointer(block);
    Log(size);
}

Coord4* __fastcall FakeVehiclePosition(void *vehicle, int) {
    Log(kTagPosition);
    LogPointer(vehicle);
    return &g_in.vehiclePosition;
}

MATRIX4* __fastcall FakeVehicleRotPos(void *vehicle, int) {
    Log(kTagRotPos);
    LogPointer(vehicle);
    return &g_in.vehicleRotPos;
}

void __fastcall FakeRegisterCar(void *controller, int, PhysicsObject *car) {
    Log(kTagRegCar);
    LogPointer(controller);
    LogPointer(car);
}

void __fastcall FakeDeregisterCar(void *controller, int, PhysicsObject *car) {
    Log(kTagDeregCar);
    LogPointer(controller);
    LogPointer(car);
}

void __fastcall FakeRegisterHeli(void *controller, int, PhysicsObject *heli) {
    Log(kTagRegHeli);
    LogPointer(controller);
    LogPointer(heli);
}

void __fastcall FakeDeregisterHeli(void *controller, int, PhysicsObject *heli) {
    Log(kTagDeregHeli);
    LogPointer(controller);
    LogPointer(heli);
}

void __fastcall FakeNavigateMode(void *vehicle, int, int mode, void *spline) {
    Log(kTagNavMode);
    LogPointer(vehicle);
    Log(mode);
    LogPointer(spline);
}

void __fastcall FakeAttackMode(void *vehicle, int, int mode) {
    Log(kTagAttack);
    LogPointer(vehicle);
    Log(mode);
}

void __fastcall FakeUnSpawn(void *vehicle, int) {
    Log(kTagUnSpawn);
    LogPointer(vehicle);
}

// An enemy made by a fake constructor: the fields WakeUp reads after it, the vtable of fakes
AICharacter *MakeEnemy(void *block, const MATRIX4 *transform) {
    AICharacter *character = static_cast<AICharacter *>(block);
    if (character == NULL)
        return NULL;
    character->vtable = g_charVtable;
    character->transform = *transform;
    character->targetRangeSquared = 5625.0f;
    character->actorLink = NULL;
    character->scoreable = 1;
    return character;
}

AICharacter* __fastcall FakeDriver(void *block, int, const MATRIX4 *transform, PhysicsObject *vehicle) {
    Log(kTagEnemy + 1);
    LogPointer(block);
    LogBytes(transform, 64);
    LogPointer(vehicle);
    return MakeEnemy(block, transform);
}

AICharacter* __fastcall FakeGround(void *block, int, const MATRIX4 *transform, int weapon, int female,
                                   int unknown17c, uint32_t registerTarget) {
    Log(kTagEnemy + 2);
    LogPointer(block);
    LogBytes(transform, 64);
    Log(weapon);
    Log(female);
    Log(unknown17c);
    Log(registerTarget & 0xff);
    return MakeEnemy(block, transform);
}

AICharacter* __fastcall FakeSnow(void *block, int, const MATRIX4 *transform, int weapon, PhysicsObject *vehicle) {
    Log(kTagEnemy + 3);
    LogPointer(block);
    LogBytes(transform, 64);
    Log(weapon);
    LogPointer(vehicle);
    return MakeEnemy(block, transform);
}

AICharacter* __fastcall FakeSSnow(void *block, int, const MATRIX4 *transform, int weapon, PhysicsObject *vehicle) {
    Log(kTagEnemy + 4);
    LogPointer(block);
    LogBytes(transform, 64);
    Log(weapon);
    LogPointer(vehicle);
    return MakeEnemy(block, transform);
}

AICharacter* __fastcall FakeSunroof(void *block, int, const MATRIX4 *transform, int weapon, int female,
                                    PhysicsObject *vehicle) {
    Log(kTagEnemy + 5);
    LogPointer(block);
    LogBytes(transform, 64);
    Log(weapon);
    Log(female);
    LogPointer(vehicle);
    return MakeEnemy(block, transform);
}

AICharacter* __fastcall FakeWindow(void *block, int, const MATRIX4 *transform, PhysicsObject *vehicle) {
    Log(kTagEnemy + 6);
    LogPointer(block);
    LogBytes(transform, 64);
    LogPointer(vehicle);
    return MakeEnemy(block, transform);
}

void __fastcall FakeHitPointLoc(AICharacter *character, int, float *hitPoints) {
    Log(kTagHitLoc);
    LogPointer(character);
    LogPointer(hitPoints);
}

void __fastcall FakeCharacterDestruct(AICharacter *character, int) {
    Log(kTagCharDestruct);
    LogPointer(character);
}

void FakeTrigger(int bank, const char *patch, const char *mix, int view) {
    Log(kTagSound);
    Log(bank);
    LogString(patch);
    LogString(mix);
    Log(view);
}

void __fastcall FakeUpdateCamera(void *camera, int) {
    Log(kTagUpdateCamera);
    LogPointer(camera);
}

MATRIX4* __fastcall FakeAimMatrix(void *camera, int, float yawScale, float pitchScale) {
    Log(kTagAim);
    LogPointer(camera);
    LogFloat(yawScale);
    LogFloat(pitchScale);
    return &g_in.aim;
}

void __fastcall FakeAimZoom(void *state, int) {
    Log(kTagZoom);
    LogPointer(state);
}

void __fastcall FakeAimRelease(void *state, int) {
    Log(kTagRelease);
    LogPointer(state);
}

// ---- the vtables of fakes

template <int N> void __fastcall FakeCarSlot(void *object, int) {
    Log(kTagCarSlot + N);
    LogPointer(object);
}

void* __fastcall FakeCarAI(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetAIGroundVehiclePtr);
    LogPointer(car);
    return g_in.carAI ? g_carAI : NULL;
}

int __fastcall FakeCarReset(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetResetAvailable);
    LogPointer(car);
    return g_in.reset;
}

const char* __fastcall FakeCarType(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetCarType);
    LogPointer(car);
    return g_in.carIsHelicopter ? "helicopter" : "car";
}

int __fastcall FakeCarClass(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetCarClass);
    LogPointer(car);
    return g_in.carClass;
}

void __fastcall FakeCarGas(void *car, int, float value) {
    Log(kTagCarSlot + PVehicle::kSetCarControlGas);
    LogPointer(car);
    LogFloat(value);
}

void __fastcall FakeCarBrake(void *car, int, float value) {
    Log(kTagCarSlot + PVehicle::kSetCarControlBrake);
    LogPointer(car);
    LogFloat(value);
}

void __fastcall FakeCarHandBrake(void *car, int, uint32_t on) {
    Log(kTagCarSlot + PVehicle::kSetCarControlHandBrake);
    LogPointer(car);
    Log(on & 0xff);
}

int __fastcall FakeCarTires(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetNumTires);
    LogPointer(car);
    return g_in.tires;
}

bool __fastcall FakeCarShredded(void *car, int, uint32_t wheel) {
    Log(kTagCarSlot + PVehicle::kIsTyreShredded);
    LogPointer(car);
    Log(wheel & 0xff);
    return g_in.shredded[wheel & 3];
}

void __fastcall FakeCarResetAt(void *car, int, const Coord3 *position, const Coord3 *direction) {
    Log(kTagCarSlot + PVehicle::kResetCarAt);
    LogPointer(car);
    LogBytes(position, 12);
    LogBytes(direction, 12);
}

void __fastcall FakeCarScoreable(void *car, int, uint32_t scoreable) {
    Log(kTagCarSlot + PVehicle::kSetScoreable);
    LogPointer(car);
    Log(scoreable & 0xff);
}

template <int N> void __fastcall FakeCharSlot(void *object, int) {
    Log(kTagCharSlot + N);
    LogPointer(object);
}

bool __fastcall FakeIsAlive(void *character, int) {
    Log(kTagCharSlot + AICharacter::kSlotIsAlive);
    LogPointer(character);
    bool spawned = character >= static_cast<void *>(g_arena) && character < static_cast<void *>(g_arena + sizeof(g_arena));
    return g_in.alive[spawned ? 1 : 0];
}

void __fastcall FakeSetActor(AICharacter *character, int, AICharacterActorLink *link) {
    Log(kTagCharSlot + AICharacter::kSlotSetActor);
    LogPointer(character);
    LogPointer(link);
    character->actorLink = link;
}

#define SLOTS_8(F, n) (void *)&F<n>, (void *)&F<n + 1>, (void *)&F<n + 2>, (void *)&F<n + 3>, (void *)&F<n + 4>, \
                      (void *)&F<n + 5>, (void *)&F<n + 6>, (void *)&F<n + 7>

void BuildVtables() {
    void *const carSlots[88] = {
        SLOTS_8(FakeCarSlot, 0), SLOTS_8(FakeCarSlot, 8), SLOTS_8(FakeCarSlot, 16), SLOTS_8(FakeCarSlot, 24),
        SLOTS_8(FakeCarSlot, 32), SLOTS_8(FakeCarSlot, 40), SLOTS_8(FakeCarSlot, 48), SLOTS_8(FakeCarSlot, 56),
        SLOTS_8(FakeCarSlot, 64), SLOTS_8(FakeCarSlot, 72), SLOTS_8(FakeCarSlot, 80),
    };
    for (int i = 0; i < kCarSlots; i++)
        g_carVtable[i] = carSlots[i];
    g_carVtable[PVehicle::kGetAIGroundVehiclePtr] = (void *)&FakeCarAI;
    g_carVtable[PVehicle::kGetResetAvailable] = (void *)&FakeCarReset;
    g_carVtable[PVehicle::kGetCarType] = (void *)&FakeCarType;
    g_carVtable[PVehicle::kGetCarClass] = (void *)&FakeCarClass;
    g_carVtable[PVehicle::kSetCarControlGas] = (void *)&FakeCarGas;
    g_carVtable[PVehicle::kSetCarControlBrake] = (void *)&FakeCarBrake;
    g_carVtable[PVehicle::kSetCarControlHandBrake] = (void *)&FakeCarHandBrake;
    g_carVtable[PVehicle::kGetNumTires] = (void *)&FakeCarTires;
    g_carVtable[PVehicle::kIsTyreShredded] = (void *)&FakeCarShredded;
    g_carVtable[PVehicle::kResetCarAt] = (void *)&FakeCarResetAt;
    g_carVtable[PVehicle::kSetScoreable] = (void *)&FakeCarScoreable;

    void *const charSlots[40] = {
        SLOTS_8(FakeCharSlot, 0), SLOTS_8(FakeCharSlot, 8), SLOTS_8(FakeCharSlot, 16), SLOTS_8(FakeCharSlot, 24),
        SLOTS_8(FakeCharSlot, 32),
    };
    for (int i = 0; i < kCharacterSlots; i++)
        g_charVtable[i] = charSlots[i];
    g_charVtable[AICharacter::kSlotIsAlive] = (void *)&FakeIsAlive;
    g_charVtable[AICharacter::kSlotSetActor] = (void *)&FakeSetActor;
}

#undef SLOTS_8

// ---- hooks: a jump over the first five bytes of our code (where the entry jumps to it) or of the original

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

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

struct HookSpec {
    uint32_t entry;
    const void *fake;
};

// For the whole test
const HookSpec kHooks[] = {
    {0x00012640, (const void *)&FakeSetNewAnimation},
    {0x000186e0, (const void *)&FakeBoneIndex},
    {0x00016c30, (const void *)&FakeCreateIKs},
    {0x000159e0, (const void *)&FakeSetInfo},
    {0x00015a30, (const void *)&FakeCreateOverrides},
    {0x00015d00, (const void *)&FakeSetOverrides},
    {0x00012420, (const void *)&FakeRotateX},
    {0x00012350, (const void *)&FakeRotate},
    {0x000139c0, (const void *)&FakeNewActor},
    {0x00013ab0, (const void *)&FakeNewActor2},
    {0x00013270, (const void *)&FakeKillActor},
    {0x00014490, (const void *)&FakeLoadCharacter},
    {0x0008ec20, (const void *)&FakeLookupFX},
    {0x0008eca0, (const void *)&FakeLocateFX},
    {0x000904c0, (const void *)&FakePreLoad},
    {0x00058360, (const void *)&FakeLookupVector},
    {0x00058180, (const void *)&FakeLookupInt},
    {0x00057a40, (const void *)&FakeAttributeDestruct},
    {0x0006f810, (const void *)&FakeNamedAttribs},
    {0x0006f860, (const void *)&FakeRenderName},
    {0x00117db0, (const void *)&FakeAssert},
    {0x0005ded0, (const void *)&FakeSetAICharacter},
    {0x0005dcc0, (const void *)&FakeActivate},
    {0x0005dce0, (const void *)&FakeDeactivate},
    {0x000b5700, (const void *)&FakeSpawnCar},
    {0x000b5890, (const void *)&FakeSpawnHuman},
    {0x000b5ee0, (const void *)&FakeSpawnHeli},
    {0x000b4740, (const void *)&FakeDeleteObject},
    {0x000b2730, (const void *)&FakeSimpleBody},
    {0x000b7310, (const void *)&FakeClearEvent},
    {0x000b72f0, (const void *)&FakeEvent},
    {0x0003dcd0, (const void *)&FakeCommand},
    {0x0005a5a0, (const void *)&FakeEventNew},
    {0x000359e0, (const void *)&FakeVehiclePosition},
    {0x00035a20, (const void *)&FakeVehicleRotPos},
    {0x000365a0, (const void *)&FakeRegisterCar},
    {0x000365f0, (const void *)&FakeDeregisterCar},
    {0x00036660, (const void *)&FakeRegisterHeli},
    {0x000366a0, (const void *)&FakeDeregisterHeli},
    {0x0002a0d0, (const void *)&FakeNavigateMode},
    {0x0002a330, (const void *)&FakeAttackMode},
    {0x0002dac0, (const void *)&FakeUnSpawn},
    {0x0001d5f0, (const void *)&FakeDriver},
    {0x0001dc80, (const void *)&FakeGround},
    {0x0001fd40, (const void *)&FakeSnow},
    {0x00020980, (const void *)&FakeSSnow},
    {0x000210f0, (const void *)&FakeSunroof},
    {0x00021d50, (const void *)&FakeWindow},
    {0x00027c80, (const void *)&FakeHitPointLoc},
    {0x0001c1d0, (const void *)&FakeCharacterDestruct},
    {0x0011e010, (const void *)&FakeTrigger},
    {0x000877e0, (const void *)&FakeUpdateCamera},
    {0x000876b0, (const void *)&FakeAimMatrix},
    {0x0008a310, (const void *)&FakeAimZoom},
    {0x0008a390, (const void *)&FakeAimRelease},
};
constexpr int kHookCount = sizeof(kHooks) / sizeof(kHooks[0]);

// Only while a side runs (the allocators)
const HookSpec kSideHooks[] = {
    {0x001146a0, (const void *)&FakeOperatorNew},
    {0x00114710, (const void *)&FakeOperatorNewArray},
    {0x001146e0, (const void *)&FakeOperatorDelete},
    {0x00114750, (const void *)&FakeFastAlloc},
    {0x001147d0, (const void *)&FakeFastFree},
};
constexpr int kSideHookCount = sizeof(kSideHooks) / sizeof(kSideHooks[0]);

Hook g_hooks[kHookCount];
Hook g_sideHooks[kSideHookCount];
bool g_sideHooksOk = true;

// ---- running a case

typedef void (*SideFn)(void);

void Window(bool original) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, original);
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    memset(&g_count, 0, sizeof(g_count));
    for (int i = 0; i < kSideHookCount; i++)
        if (!HookInstall(&g_sideHooks[i], kSideHooks[i].entry, kSideHooks[i].fake))
            g_sideHooksOk = false;
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
    for (int i = 0; i < kSideHookCount; i++)
        HookRemove(&g_sideHooks[i]);
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

// ---- preparing the inputs

// The state before the test: each round of cases starts from it
Snapshot g_before;

void PrepareActors() {
    memset(g_poserBlock, 0, sizeof(g_poserBlock));
    memset(g_matricesBlock, 0, sizeof(g_matricesBlock));
    memset(g_actCharacterBlock, 0, sizeof(g_actCharacterBlock));
    memset(g_weaponBlock, 0, sizeof(g_weaponBlock));
    memset(g_animControllerBlock, 0, sizeof(g_animControllerBlock));
    g_poser->mode = Chance(80) ? kPoseModeNormal : Range(0, 3);
    g_poser->time = Uniform(0.0f, 20.0f);
    g_poser->endTime = Chance(50) ? g_poser->time + Uniform(-0.5f, 3.0f) : Uniform(0.0f, 20.0f);
    g_poser->framesPerSecond = 20.0f;
    g_poser->secondsPerFrame = 0.05f;
    g_poser->frameTime = Uniform(0.01f, 0.05f);
    g_poser->timeScale = Uniform(0.5f, 2.0f);
    g_poser->ikSolvers = reinterpret_cast<ActIKSolverArray *>(g_arrayBlock);
    g_poser->poseOverrides = reinterpret_cast<ActGlobalPoseOverrideArray *>(g_arrayBlock + 0x10);
    // the frame windows the hands test
    static const float kFrames[] = {4.9f, 5.0f, 5.3f, 13.9f, 14.0f, 14.1f};
    if (Chance(40))
        g_poser->time = Near(kFrames[Range(0, 5)]);
    g_animController->poser = g_poser;
    MATRIX4 local;
    RandomMatrix(&local, g_player, 5.0f);
    g_matrices->local = local;
    g_actCharacter->weapons[0] = Chance(80) ? reinterpret_cast<ActWeapon *>(g_weaponBlock) : NULL;
    for (int i = 0; i < kActors; i++) {
        memset(g_actorBlocks[i], 0, sizeof(g_actorBlocks[i]));
        ActActor *actor = Actor(i);
        actor->character = g_actCharacter;
        actor->poser = g_poser;
        actor->matrices = g_matrices;
        actor->controller = g_animController;
        actor->visible = Chance(50);
        actor->baseScale = Uniform(0.8f, 1.2f);
        g_nodes[i].next = &g_nodes[(i + 1) % kActors];
        g_nodes[i].prev = &g_nodes[(i + kActors - 1) % kActors];
        g_nodes[i].value = actor;
    }
}

void PrepareInputs() {
    g_in.alive[0] = Chance(60);
    g_in.alive[1] = Chance(60);
    g_in.carAI = Chance(80);
    g_in.reset = Chance(70) ? 0 : 1;
    g_in.carIsHelicopter = Chance(15);
    g_in.carClass = Chance(60) ? 3 : Range(0, 5);
    g_in.tires = Chance(70) ? 4 : Range(0, 5);
    for (int i = 0; i < 4; i++)
        g_in.shredded[i] = Chance(15);
    for (int i = 0; i < 8; i++)
        g_in.vectors[i] = {Uniform(-2.0f, 2.0f), Uniform(-1.0f, 2.0f), Uniform(-2.0f, 2.0f),
                           Chance(80) ? 1.0f : Uniform(0.0f, 1.0f)};
    for (int i = 0; i < 3; i++) {
        g_in.intFound[i] = Chance(60);
        g_in.intValue[i] = Range(0, 12);
    }
    g_in.renderName = Range(0, 2);
    g_in.spawnCarNull = Chance(10);
    for (int i = 0; i < 4; i++)
        g_in.humanNull[i] = Chance(15);
    for (int i = 0; i < 2; i++) {
        g_in.fxFound[i] = Chance(85);
        RandomMatrix(&g_in.fxMatrices[i], g_player, 3.0f);
        g_in.bones[i] = Range(0, 40);
    }
    g_in.vehiclePosition = {g_player.x + Uniform(-300.0f, 300.0f), g_player.y + Uniform(-5.0f, 5.0f),
                            g_player.z + Uniform(-300.0f, 300.0f), 1.0f};
    RandomMatrix(&g_in.vehicleRotPos, g_player, 300.0f);
    RandomMatrix(&g_in.aim, g_player, 3.0f);
    g_in.player = {g_player.x + Uniform(-2.0f, 2.0f), g_player.y, g_player.z + Uniform(-2.0f, 2.0f), 0.0f};
}

void PrepareCharacter(AICharacter *character) {
    memset(character, 0, 0x1c0);
    character->vtable = g_charVtable;
    RandomMatrix(&character->transform, g_player, 200.0f);
    character->state = Range(0, 22);
    character->targetRangeSquared = Uniform(100.0f, 10000.0f);
    character->scoreable = uint8_t(Range(0, 1));
}

void PrepareHuman(Human *human) {
    memset(human, 0, sizeof(Human));
    human->rigidBodySlot = int16_t(g_playerSlot);
}

void PrepareCar(PhysicsObject *car, float *hitPoints) {
    memset(car, 0, sizeof(PHelicopter));
    car->vtable = g_carVtable;
    car->rigidBodySlot = int16_t(g_playerSlot);
    car->hitPointLoc = hitPoints;
    reinterpret_cast<PHelicopter *>(car)->ai = reinterpret_cast<AIHelicopter *>(g_vehicleAIBlock);
    reinterpret_cast<PHelicopter *>(car)->scoreable = Chance(50);
}

void RandomCharacterInfo(AIElementCharacterInfo *info, bool member) {
    static const int kSeats[] = {3, 4, 14, 15, 16, 17, 5, 9, 2};
    static const int kKinds[] = {0, 10, 18, 5};
    info->kind = member ? kSeats[Range(0, 8)] : kKinds[Range(0, 3)];
    info->description = Range(0, 32);
    info->weapon = Range(0, 4);
    info->aimScale = Range(0, 100);
    info->accuracy = Range(0, 100);
    info->hitPoints = Chance(20) ? 0.0f : Chance(10) ? -Uniform(0.0f, 10.0f) : Chance(5) ? NaN() : Uniform(1.0f, 60.0f);
    info->animation = Chance(50) ? Range(28, 52) : Range(0, 60);
    info->skill = Range(0, 6);
}

void PrepareElement(int i) {
    AIElement &e = g_elements[i];
    memset(&e, 0, sizeof(e));
    RandomMatrix(&e.transform, g_player, Chance(50) ? 150.0f : 400.0f);
    e.flags = uint16_t(Next() & 0x7f);
    if (Chance(60))
        e.flags &= ~(kElementDeactivated | kElementFlag01);
    e.difficulty = int16_t(Chance(50) ? 0 : Range(-1, 7));
    e.type = Chance(10) ? 2 : Chance(40) ? kAIElementCharacter : kAIElementVehicle;
    e.state = Chance(45) ? kElementAwake : Chance(70) ? kElementAsleep : kElementDead;
    e.hitPoints = Chance(20) ? 0.0f : Chance(10) ? -Uniform(0.0f, 10.0f) : Chance(4) ? NaN() : Uniform(1.0f, 500.0f);
    e.command = Chance(50) ? reinterpret_cast<AICommand *>(g_command) : NULL;
    if (e.type == kAIElementCharacter || e.type == 2) {
        RandomCharacterInfo(&e.person.info, false);
        PrepareCharacter(Character(i));
        PrepareHuman(HumanBlock(i));
        if (e.state == kElementAwake || Chance(20)) {
            e.character = Character(i);
            e.person.human = HumanBlock(i);
        }
    } else {
        snprintf(e.car.name, sizeof(e.car.name), Chance(50) ? "paris_hench" : "car%d", i);
        e.car.colour = uint8_t(Range(0, 255));
        e.car.helicopter = uint8_t(Chance(25));
        e.memberCount = Chance(10) ? 0 : Range(1, 3);
        for (int m = 0; m < 3; m++) {
            RandomCharacterInfo(&e.members[m], true);
            // A helicopter's seated riders are made on whatever the original's stack held (the port: the
            // helicopter): only seats that make no rider
            if (e.car.helicopter && e.members[m].kind != 5 && e.members[m].kind != 9)
                e.members[m].kind = 2;
        }
        PhysicsObject *car = CarBlock(e.car.helicopter ? 1 : 0);
        PrepareCar(car, &e.hitPoints);
        if (e.state == kElementAwake) {
            e.vehicle = reinterpret_cast<AIVehicle *>(g_vehicleAIBlock);
            e.object = car;
        }
        for (int m = 0; m < e.memberCount; m++) {
            if (e.state == kElementAwake ? Chance(70) : Chance(25)) {
                int other = (i + m + 1) % kElements;
                PrepareCharacter(Character(other));
                e.memberCharacters[m] = Character(other);
                e.memberHumans[m] = HumanBlock(other);
            }
        }
    }
}

void PrepareElements() {
    Restore(g_before);
    PrepareInputs();
    PrepareActors();
    for (int i = 0; i < kElements; i++)
        PrepareElement(i);
    memcpy(g_carp, g_elements, sizeof(g_carp));
    memset(g_carAIBlock, 0, sizeof(g_carAIBlock));
    g_carAI->enteredZone = uint8_t(Chance(20));
    g_carAI->oiled = uint8_t(Chance(20));
    memset(g_vehicleAIBlock, 0, sizeof(g_vehicleAIBlock));
    memset(g_simpleBodyBlock, 0, sizeof(g_simpleBodyBlock));

    AIElementController *c = g_controller;
    memset(c, 0, sizeof(*c));
    c->characterSlots = Range(0, 10);
    c->carSlots = Range(0, 4);
    c->helicopterSlots = Range(0, 3);
    c->elements = g_elements;
    c->carpElements = g_carp;
    c->tracking = g_tracking;
    c->trackingBlock = g_tracking;
    c->stats = {Uniform(0.0f, 100.0f), Uniform(0.0f, 100.0f), Uniform(0.0f, 50.0f), Uniform(0.0f, 50.0f),
                Uniform(0.0f, 50.0f), Uniform(0.0f, 500.0f), Uniform(0.0f, 500.0f)};
    c->wakeList = g_wakeList;
    c->rangeList = g_rangeList;
    c->count = kElements;
    c->awakeCharacters = Range(0, 6);
    c->awakeCars = Range(0, 3);
    c->awakeHelicopters = Range(0, 2);
    c->cars = Range(0, 4);
    c->mightAsWellBeDead = Range(0, 5);
    fgAIElementController = c;

    for (int i = 0; i < kElements; i++) {
        AIElementTracking &t = g_tracking[i];
        memset(&t, 0, sizeof(t));
        t.nextUpdate = ShadowStepCount + Range(-2, 2);
        t.interval = Range(1, 5);
        t.wakeRange = Uniform(50.0f, 350.0f);
        if (Chance(30)) {
            // exactly at the element's distance (an asleep element's position is its transform's)
            float distance = vec3distance(g_elements[i].transform.mtx[3], &g_in.player);
            t.wakeRange = Chance(60) ? distance : Near(distance);
        }
        t.sleepRange = t.wakeRange + 20.0f;
        if (Chance(20))
            t.sleepRange = Near(vec3distance(g_elements[i].transform.mtx[3], &g_in.player));
        t.inRange = uint8_t(Chance(50));
        t.wake = uint8_t(Chance(50));
        t.element = &g_elements[i];
        t.controller = c;
        t.lastHitPoints = Uniform(0.0f, 600.0f);
        t.distance = Uniform(0.0f, 300.0f);
        t.closeness = Uniform(0.0f, 1.0f);
    }
}

// ---- the tests

void TestPure(const Snapshot &before) {
    for (long i = 0; i < 200; i++) {
        float values[] = {0.0f, 0.5f, 1.0f, -0.25f, NaN()};
        g_in.a.x = Chance(30) ? values[Range(0, 4)] : Uniform(-1.0f, 1.0f);
        g_in.a.y = Chance(30) ? g_in.a.x : Chance(10) ? NaN() : Uniform(-1.0f, 1.0f);
        RunCase("CompareRanges", i, [] {
            AIElementRange a = {1, g_in.a.x}, b = {2, g_in.a.y};
            g_out.i = g_original ? ((int (*)(const void *, const void *))0x00028a90)(&a, &b) : CompareRanges(&a, &b);
        }, before);
    }
    for (long i = 0; i < 300; i++) {
        float special[] = {0.5f, -0.5f, 1.5f, -1.5f, 2.0f, -0.0f, 8388607.5f, 16777216.0f, 1e30f, -1e30f, NaN(),
                           INFINITY, -INFINITY, 1e-40f};
        g_in.f0 = Chance(40) ? special[Range(0, 13)] : Uniform(-1000.0f, 1000.0f);
        RunCase("FloatCeil", i, [] {
            g_out.d = g_original ? ((double (*)(float))0x00029d20)(g_in.f0) : FloatCeil(g_in.f0);
        }, before);
        RunCase("FloatFloor", i, [] {
            g_out.d = g_original ? ((double (*)(float))0x00029d40)(g_in.f0) : FloatFloor(g_in.f0);
        }, before);
    }
    for (long i = 0; i < 200; i++) {
        g_in.a = {Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f), Uniform(-1.0f, 1.0f)};
        g_in.b = {Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f), Uniform(-100.0f, 100.0f), Uniform(-1.0f, 1.0f)};
        g_in.f0 = Chance(20) ? Range(0, 1) : Uniform(-0.5f, 1.5f);
        g_in.i0 = Range(0, 1);
        g_out.v = {Uniform(-5.0f, 5.0f), 0.0f, 0.0f, Uniform(-5.0f, 5.0f)};
        RunCase("BlendVectors", i, [] {
            typedef void (*BlendFn)(void *, const void *, const void *, float);
            void *out = g_in.i0 ? static_cast<void *>(&g_in.a) : &g_out.v;
            if (g_original)
                ((BlendFn)0x00022870)(out, &g_in.a, &g_in.b, g_in.f0);
            else
                BlendVectors(out, &g_in.a, &g_in.b, g_in.f0);
            g_out.m.mtx[0][0] = g_in.a.x;
            g_out.m.mtx[0][1] = g_in.a.y;
            g_out.m.mtx[0][2] = g_in.a.z;
            g_out.m.mtx[0][3] = g_in.a.w;
        }, before);
    }
    for (long i = 0; i < 100; i++) {
        memset(g_cameraBlock, 0, sizeof(g_cameraBlock));
        reinterpret_cast<RCamera *>(g_cameraBlock)->fieldOfView = 60.0f;
        float special[] = {2.0f, nextafterf(2.0f, 3.0f), nextafterf(2.0f, 1.0f), -5.0f, NaN()};
        g_in.f0 = Chance(50) ? special[Range(0, 4)] : Uniform(0.0f, 120.0f);
        RunCase("RCamera::SetFieldOfView", i, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int, float))0x00011000)(g_cameraBlock, 0, g_in.f0);
            else
                reinterpret_cast<RCamera *>(g_cameraBlock)->SetFieldOfView(g_in.f0);
        }, before);
    }
    if (fgRoadNetworkData.loaded && fgRoadNetworkData.roadCount > 0) {
        for (long i = 0; i < 120; i++) {
            g_in.i0 = Chance(15) ? (Chance(50) ? -1 : fgRoadNetworkData.roadCount) : Range(0, fgRoadNetworkData.roadCount - 1);
            g_in.i1 = Range(0, 3);
            RunCase("the road counts", i, [] {
                typedef void (__fastcall *CountFn)(void *, int, int);
                static const uint32_t kOriginals[4] = {0x00029c90, 0x00029cb0, 0x00023990, 0x000239b0};
                WRoadNetwork *roads = WRoadNetwork::Get();
                if (g_original) {
                    ((CountFn)kOriginals[g_in.i1])(roads, 0, g_in.i0);
                    return;
                }
                switch (g_in.i1) {
                case 0: roads->AddRoadCar(g_in.i0); break;
                case 1: roads->RemoveRoadCar(g_in.i0); break;
                case 2: roads->AddRoadPedestrian(g_in.i0); break;
                default: roads->RemoveRoadPedestrian(g_in.i0); break;
                }
            }, before);
        }
    }
    if (fgRoadNetworkData.loaded) {
        for (int segment = 0; segment < fgRoadNetworkData.segmentCount; segment++) {
            for (int forward = 0; forward < 2; forward++) {
                memset(g_navBlock, 0, sizeof(g_navBlock));
                g_nav->segment = int16_t(segment);
                g_nav->forward = uint8_t(forward);
                RunCase("WRoadNav::SegmentSpeed", segment * 2 + forward, [] {
                    g_out.d = g_original ? ((float (__fastcall *)(void *, int))0x00029cd0)(g_nav, 0)
                                         : g_nav->SegmentSpeed();
                }, before);
                RunCase("WRoadNav::LeftDrivableDist", segment * 2 + forward, [] {
                    g_out.d = g_original ? ((double (__fastcall *)(void *, int))0x00029cf0)(g_nav, 0)
                                         : g_nav->LeftDrivableDist();
                }, before);
            }
        }
    }
}

void PrepareZones() {
    Restore(g_before);
    AIZoneStatics &z = AIZoneData;
    for (int t = 0; t < kZoneTypes; t++) {
        z.radiusSquared[t] = Chance(70) ? Uniform(1.0f, 1500.0f) : 0.0f;
        z.mergeSteps[t] = int16_t(Chance(20) ? Range(-2, 0) : Range(1, 100));
        z.lifeSteps[t] = int16_t(Range(0, 200));
    }
    z.instance = Chance(80) ? reinterpret_cast<AIZoneController *>(g_zoneInstance) : NULL;
    int active = 0;
    for (int i = 0; i < kZones; i++) {
        AIZone &zone = z.zones[i];
        zone.active = uint8_t(Chance(50));
        active += zone.active;
        zone.position = {g_player.x + Uniform(-60.0f, 60.0f), g_player.y + Uniform(-3.0f, 3.0f),
                         g_player.z + Uniform(-60.0f, 60.0f)};
        zone.radiusSquared = Uniform(0.0f, 1500.0f);
        zone.stepsLeft = int16_t(Chance(20) ? Range(-2, 2) : Range(0, 200));
        zone.type = Chance(5) ? 0x10000 + Range(0, 4) : Range(0, 4);
    }
    z.activeCount = int16_t(Chance(70) ? active : Range(-1, 31));
}

void TestZones() {
    for (long i = 0; i < 120; i++) {
        PrepareZones();
        Snapshot prepared;
        Capture(&prepared);
        g_in.i0 = Range(0, kZones - 1);
        RunCase("AIZone::Construct", i, [] {
            AIZone *zone = &AIZoneData.zones[g_in.i0];
            g_out.p = g_original ? ((AIZone *(__fastcall *)(void *, int))0x00036a00)(zone, 0) : zone->Construct();
        }, prepared);
        RunCase("AIZoneController::Get", i, [] {
            g_out.p = g_original ? ((void *(*)(void))0x00036a10)() : AIZoneController::Get();
        }, prepared);
        RunCase("AIZoneController::Init", i, [] {
            if (g_original)
                ((void (*)(void))0x00036a20)();
            else
                AIZoneController::Init();
        }, prepared);
        RunCase("AIZoneController::Update", i, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int))0x00036ac0)(g_zoneInstance, 0);
            else
                reinterpret_cast<AIZoneController *>(g_zoneInstance)->Update();
        }, prepared);
        RunCase("AIZoneController::Shutdown", i, [] {
            if (g_original)
                ((void (*)(void))0x00036b00)();
            else
                AIZoneController::Shutdown();
        }, prepared);
        for (int k = 0; k < 6; k++) {
            g_in.i0 = Range(0, kZones - 1);
            const AIZone &zone = AIZoneData.zones[g_in.i0];
            g_in.point = {zone.position.x + Uniform(-40.0f, 40.0f), zone.position.y + Uniform(-2.0f, 2.0f),
                          zone.position.z + Uniform(-40.0f, 40.0f)};
            g_in.f0 = Chance(30) ? 0.0f : Uniform(-200.0f, 1500.0f);
            RunCase("AIZoneController::InZone", i * 6 + k, [] {
                g_out.b = g_original ? ((bool (__fastcall *)(void *, int, int, const Coord3 *, float))0x00036b20)(
                                           g_zoneInstance, 0, g_in.i0, &g_in.point, g_in.f0)
                                     : reinterpret_cast<AIZoneController *>(g_zoneInstance)->InZone(g_in.i0, &g_in.point,
                                                                                                    g_in.f0);
            }, prepared);
            g_in.i1 = Range(0, 4);
            RunCase("AIZoneController::RegisterZone", i * 6 + k, [] {
                if (AIZoneData.instance == NULL)
                    AIZoneData.instance = reinterpret_cast<AIZoneController *>(g_zoneInstance);
                if (g_original)
                    ((void (__fastcall *)(void *, int, int, const Coord3 *))0x00036bb0)(AIZoneData.instance, 0, g_in.i1,
                                                                                         &g_in.point);
                else
                    AIZoneData.instance->RegisterZone(g_in.i1, &g_in.point);
            }, prepared);
        }
    }
}

void TestTracking() {
    static const char *const kNames[] = {"EMPTY", "BLOWOUT", "SMOKED", "OILED", "AWAKE", "OTHER"};
    for (long round = 0; round < 60; round++) {
        PrepareElements();
        Snapshot prepared;
        Capture(&prepared);
        for (int i = 0; i < kElements; i++) {
            long index = round * kElements + i;
            g_in.i0 = i;
            g_in.f0 = Chance(20) ? (Chance(50) ? 0.0f : -Uniform(0.0f, 10.0f)) : Uniform(10.0f, 400.0f);
            RunCase("AIElementTracking::SetWakeRange", index, [] {
                if (g_original)
                    ((void (__fastcall *)(void *, int, float))0x00027cb0)(&g_tracking[g_in.i0], 0, g_in.f0);
                else
                    g_tracking[g_in.i0].SetWakeRange(g_in.f0);
            }, prepared);
            for (int n = 0; n < 6; n++) {
                g_in.i1 = n;
                RunCase("AIElementTracking::GetProperty", index * 6 + n, [] {
                    g_out.b = g_original ? ((bool (__fastcall *)(void *, int, const char *))0x00027ce0)(
                                               &g_tracking[g_in.i0], 0, kNames[g_in.i1])
                                         : g_tracking[g_in.i0].GetProperty(kNames[g_in.i1]);
                }, prepared);
            }
            RunCase("AIElementTracking::UpdatePosition", index, [] {
                if (g_original)
                    ((void (__fastcall *)(void *, int, const Coord4 *))0x00027f00)(&g_tracking[g_in.i0], 0, &g_in.player);
                else
                    g_tracking[g_in.i0].UpdatePosition(&g_in.player);
            }, prepared);
            RunCase("AIElementTracking::Update", index, [] {
                if (g_original)
                    ((void (__fastcall *)(void *, int, const Coord4 *))0x00028ab0)(&g_tracking[g_in.i0], 0, &g_in.player);
                else
                    g_tracking[g_in.i0].Update(&g_in.player);
            }, prepared);
        }
    }
}

typedef void (__fastcall *ElementFn)(void *, int, AIElement *);

void PrepareData() {
    memset(&g_data, 0, sizeof(g_data));
    memcpy(g_data.records, g_elements, sizeof(g_data.records));
    g_data.header.tag = 0x4149456c;
    g_data.header.count = kElements;
    if (Chance(50)) {
        g_data.header.flags = kDataRelative | (sizeof(g_data.records) << 8);
        g_data.header.data = uint32_t(offsetof(DataBlock, records));
    } else {
        g_data.header.flags = sizeof(g_data.records) << 8;
        g_data.header.data = uint32_t(uintptr_t(g_data.records));
    }
    if (Chance(30))
        fgWorld->trackName = const_cast<char *>("snow2a_mis4");
    ShadowLaunch.difficulty = Chance(50) ? 2 : Range(0, 3);
}

void TestController(const Snapshot &before) {
    Restore(before);
    for (long round = 0; round < 60; round++) {
        PrepareElements();
        glbMissionManager->difficulty = Range(0, 2);
        if (Chance(15))
            ActorDatabase = NULL;
        Snapshot prepared;
        Capture(&prepared);
        RunCase("AIElementController::GetStats", round, [] {
            g_out.p = g_original ? ((void *(__fastcall *)(void *, int))0x00028620)(g_controller, 0)
                                 : g_controller->GetStats();
        }, prepared);
        RunCase("AIElementController::EveryoneChill", round, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int))0x00028630)(g_controller, 0);
            else
                g_controller->EveryoneChill();
        }, prepared);
        RunCase("AIElementController::OkToSpawnHench", round, [] {
            g_out.b = g_original ? ((bool (__fastcall *)(void *, int))0x000286b0)(g_controller, 0)
                                 : g_controller->OkToSpawnHench();
        }, prepared);
        for (int k = 0; k < 4; k++) {
            g_in.p0 = reinterpret_cast<uint8_t *>(g_carp) + (Range(-3, kElements - 1) * 0x100) + (Chance(20) ? Range(1, 255) : 0);
            RunCase("AIElementController::RedirectCarpElement", round * 4 + k, [] {
                g_out.p = g_original ? ((void *(__fastcall *)(void *, int, void *))0x000286d0)(g_controller, 0, g_in.p0)
                                     : g_controller->RedirectCarpElement(static_cast<AIElement *>(g_in.p0));
            }, prepared);
            RunCase("AIElementController::GetCarpElementTracking", round * 4 + k, [] {
                g_out.p = g_original ? ((void *(__fastcall *)(void *, int, void *))0x00028700)(g_controller, 0, g_in.p0)
                                     : g_controller->GetCarpElementTracking(static_cast<AIElement *>(g_in.p0));
            }, prepared);
        }
        RunCase("AIElementController::CalcMightAsWellBeDead", round, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int))0x00028730)(g_controller, 0);
            else
                g_controller->CalcMightAsWellBeDead();
        }, prepared);
        RunCase("AIElementController::GetNumMightAsWellBeDead", round, [] {
            g_out.i = g_original ? ((int (__fastcall *)(void *, int))0x000287c0)(g_controller, 0)
                                 : g_controller->GetNumMightAsWellBeDead();
        }, prepared);
        for (int i = 0; i < kElements; i++) {
            long index = round * kElements + i;
            g_in.i0 = i;
            RunCase("AIElementController::GoToSleep", index, [] {
                if (g_original)
                    ((ElementFn)0x000287d0)(g_controller, 0, &g_elements[g_in.i0]);
                else
                    g_controller->GoToSleep(&g_elements[g_in.i0]);
            }, prepared);
            RunCase("AIElementController::SyncRuleInfo", index, [] {
                if (g_original)
                    ((ElementFn)0x00028a40)(g_controller, 0, &g_elements[g_in.i0]);
                else
                    g_controller->SyncRuleInfo(&g_elements[g_in.i0]);
            }, prepared);
            RunCase("AIElementController::WakeUp", index, [] {
                if (g_original)
                    ((ElementFn)0x00028bb0)(g_controller, 0, &g_elements[g_in.i0]);
                else
                    g_controller->WakeUp(&g_elements[g_in.i0]);
            }, prepared);
        }
        g_in.p0 = Chance(70) ? static_cast<void *>(g_vehicleAIBlock) : static_cast<void *>(g_carAIBlock);
        RunCase("AIElementController::ForceVehicleToSleep", round, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int, void *))0x000289f0)(g_controller, 0, g_in.p0);
            else
                g_controller->ForceVehicleToSleep(static_cast<AIVehicle *>(g_in.p0));
        }, prepared);
        RunCase("AIElementController::Update", round, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int))0x00029880)(g_controller, 0);
            else
                g_controller->Update();
        }, prepared);
        // Again with room for everyone
        g_controller->characterSlots = 40;
        g_controller->carSlots = 10;
        g_controller->helicopterSlots = 10;
        for (int i = 0; i < kElements; i++)
            g_tracking[i].nextUpdate = ShadowStepCount;
        Capture(&prepared);
        RunCase("AIElementController::Update (room)", round, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int))0x00029880)(g_controller, 0);
            else
                g_controller->Update();
        }, prepared);

        PrepareData();
        Capture(&prepared);
        RunCase("AIElementController::Construct", round, [] {
            memset(g_newControllerBlock, 0, sizeof(g_newControllerBlock));
            g_out.p = g_original ? ((void *(__fastcall *)(void *, int, UData *))0x00028190)(g_newController, 0,
                                                                                             &g_data.header)
                                 : g_newController->Construct(&g_data.header);
        }, prepared);
        RunCase("AIElementController::Create", round, [] {
            if (g_original)
                ((void (*)(UData *))0x000285b0)(&g_data.header);
            else
                AIElementController::Create(&g_data.header);
        }, prepared);
        RunCase("AIElementController::Destroy", round, [] {
            if (g_original)
                ((void (*)(void))0x00028b60)();
            else
                AIElementController::Destroy();
        }, prepared);
    }
}

// ---- the hands

void PrepareHands() {
    Restore(g_before);
    PrepareInputs();
    PrepareActors();
    AICharacterHands *h = g_hands;
    memset(h, 0, sizeof(*h));
    h->vtable = reinterpret_cast<void **>(kAICharacterHandsVtable);
    RandomMatrix(&h->transform, g_player, 2.0f);
    static const int kStates[] = {0, 1, 2, 8, 9, 10, 11, 12, 13};
    h->state = kStates[Range(0, 8)];
    h->subState = kStates[Range(0, 8)];
    h->weapon = Range(1, 7);
    h->animBank = Range(0, 16);
    h->animation = Range(0, 9);
    h->reloadPending = uint8_t(Chance(30));
    h->hideOnSwitch = uint8_t(Chance(70));
    h->nextWeapon = Chance(50) ? 0 : Range(0, 8);
    h->spread = Chance(30) ? 0.0f : Chance(20) ? Near(-0.0125f) : Uniform(-0.03f, 0.01f);
    h->aimFraction = Uniform(0.0f, 1.0f);
    h->shown = uint8_t(Chance(50));
    h->animController = g_animController;
    AICharacterHandsStatics &s = HandsStatics;
    for (int w = 0; w < kHandWeapons; w++) {
        s.fireSide[w] = Range(0, 1);
        s.handles[w] = reinterpret_cast<AICharacterActorLink *>(&g_nodes[w]);
    }
    h->actorLink = Chance(80) ? s.handles[h->weapon] : reinterpret_cast<AICharacterActorLink *>(&g_nodes[9]);
    s.instance = Chance(90) ? h : NULL;
    s.human = Chance(70) ? HumanBlock(0) : NULL;
    s.lockOn = uint8_t(Chance(40));
    s.aiming = uint8_t(Chance(40));
    s.swayOffset = {Uniform(-0.3f, 0.3f), Uniform(-0.3f, 0.3f), Uniform(-0.3f, 0.3f), 0.0f};
    s.cameraOffset = {Uniform(-3.0f, 3.0f), Uniform(-1.0f, 2.0f), Uniform(-3.0f, 3.0f), 0.0f};
    glbMissionManager->autoDrive = uint8_t(Chance(60));
    for (int w = 0; w < kHandWeapons; w++) {
        WeaponRecord &record = ShadowWeaponManager->weapons[kFirstHandWeapon + w];
        record.flags = (record.flags & ~kWeaponLoaded) | (Chance(50) ? kWeaponLoaded : 0);
    }
}

typedef void (__fastcall *HandsFn)(void *, int);

struct HandsMethod {
    const char *name;
    uint32_t original;
    void (AICharacterHands::*port)();
};

const HandsMethod kHandsMethods[] = {
    {"AICharacterHands::ShowHands", 0x00022c30, &AICharacterHands::ShowHands},
    {"AICharacterHands::HideHands", 0x00022c50, &AICharacterHands::HideHands},
    {"AICharacterHands::Fire", 0x00022dd0, &AICharacterHands::Fire},
    {"AICharacterHands::Reload", 0x00022e20, &AICharacterHands::Reload},
    {"AICharacterHands::DoNeutral", 0x00022e70, &AICharacterHands::DoNeutral},
    {"AICharacterHands::DoIdling", 0x00022f10, &AICharacterHands::DoIdling},
    {"AICharacterHands::DoArming", 0x00022f70, &AICharacterHands::DoArming},
    {"AICharacterHands::DoArmed", 0x00022fb0, &AICharacterHands::DoArmed},
    {"AICharacterHands::DoAiming", 0x000230a0, &AICharacterHands::DoAiming},
    {"AICharacterHands::DoFiring", 0x00023170, &AICharacterHands::DoFiring},
    {"AICharacterHands::DoReloading", 0x00023230, &AICharacterHands::DoReloading},
    {"AICharacterHands::DoUnarming", 0x00023280, &AICharacterHands::DoUnarming},
    {"AICharacterHands::UpdateRotPos", 0x000232b0, &AICharacterHands::UpdateRotPos},
    {"AICharacterHands::Destruct", 0x00022820, &AICharacterHands::Destruct},
};
const HandsMethod *g_handsMethod;

void TestHands() {
    for (long round = 0; round < 150; round++) {
        PrepareHands();
        Snapshot prepared;
        Capture(&prepared);
        for (const HandsMethod &method : kHandsMethods) {
            g_handsMethod = &method;
            RunCase(method.name, round, [] {
                if (g_original)
                    ((HandsFn)g_handsMethod->original)(g_hands, 0);
                else
                    (g_hands->*g_handsMethod->port)();
            }, prepared);
        }
        RunCase("AICharacterHands::AbleToFire", round, [] {
            g_out.b = g_original ? ((bool (__fastcall *)(void *, int))0x00022830)(g_hands, 0) : g_hands->AbleToFire();
        }, prepared);
        RunCase("AICharacterHands::GetWeapon", round, [] {
            g_out.i = g_original ? ((int (__fastcall *)(void *, int))0x00022860)(g_hands, 0) : g_hands->GetWeapon();
        }, prepared);
        RunCase("AICharacterHands::GetAimFraction", round, [] {
            g_out.d = g_original ? ((float (*)(void))0x000227a0)() : AICharacterHands::GetAimFraction();
        }, prepared);
        RunCase("AICharacterHands::GetRotPos", round, [] {
            if (g_original)
                ((void (*)(void *, MATRIX4 *, Coord4 *))0x000228e0)(g_hands, &g_out.m, &g_out.v);
            else
                AICharacterHands::GetRotPos(g_hands, &g_out.m, &g_out.v);
        }, prepared);
        g_in.i0 = Chance(20) ? Range(-1, 9) : Range(1, 7);
        RunCase("AICharacterHands::ChangeWeapon", round, [] {
            if (g_original)
                ((void (__fastcall *)(void *, int, int))0x00022cc0)(g_hands, 0, g_in.i0);
            else
                g_hands->ChangeWeapon(g_in.i0);
        }, prepared);
        g_in.i0 = Range(0, 1);
        RunCase("AICharacterHands::Delete", round, [] {
            g_out.p = g_original ? ((void *(__fastcall *)(void *, int, unsigned))0x000228b0)(g_hands, 0, g_in.i0)
                                 : g_hands->Delete(g_in.i0);
        }, prepared);
        RunCase("AICharacterHands::Init", round, [] {
            if (g_original)
                ((void (*)(void))0x00022930)();
            else
                AICharacterHands::Init();
        }, prepared);
        RunCase("AICharacterHands::Shutdown", round, [] {
            if (g_original)
                ((void (*)(void))0x000227c0)();
            else
                AICharacterHands::Shutdown();
        }, prepared);
    }
}

// ---- Bond

struct BondMissionSet {
    uint8_t unknown00[0x24];
    int32_t bond;
};
#define ShadowMissionSet (*(BondMissionSet **)(uintptr_t(glbMissionManager) + 0x2c))

void PrepareBond() {
    Restore(g_before);
    PrepareInputs();
    PrepareActors();
    AICharacterBond *b = g_bond;
    memset(b, 0, sizeof(*b));
    b->vtable = reinterpret_cast<void **>(kAICharacterBondVtable);
    RandomMatrix(&b->transform, g_player, 2.0f);
    static const int kStates[] = {0, 1, 2, 8, 11, 13};
    b->state = kStates[Range(0, 5)];
    b->subState = kStates[Range(0, 5)];
    b->side = Range(0, 1);
    b->animBank = Range(0, 3);
    b->steerLeft = reinterpret_cast<ArticleEffect *>(&g_effects[0]);
    b->steerRight = reinterpret_cast<ArticleEffect *>(&g_effects[1]);
    b->actorLink = reinterpret_cast<AICharacterActorLink *>(&g_nodes[0]);
    ShadowBondButtons[0] = uint8_t(Chance(30));
    ShadowBondButtons[1] = uint8_t(Chance(30));
    glbMissionManager->autoDrive = uint8_t(Chance(30));
    if (ShadowMissionSet != NULL)
        ShadowMissionSet->bond = Chance(70) ? Range(1, 3) : Range(-1, 5);
}

typedef void (__fastcall *BondFn)(void *, int);

struct BondMethod {
    const char *name;
    uint32_t original;
    void (AICharacterBond::*port)();
};

const BondMethod kBondMethods[] = {
    {"AICharacterBond::DoInitial", 0x0001d0c0, &AICharacterBond::DoInitial},
    {"AICharacterBond::DoNeutral", 0x0001d090, &AICharacterBond::DoNeutral},
    {"AICharacterBond::DoArming", 0x0001d0e0, &AICharacterBond::DoArming},
    {"AICharacterBond::DoFiring", 0x0001d1c0, &AICharacterBond::DoFiring},
    {"AICharacterBond::HandleInterrupts", 0x0001d2a0, &AICharacterBond::HandleInterrupts},
    {"AICharacterBond::UpdateRotPos", 0x00023950, &AICharacterBond::UpdateRotPos},
    {"AICharacterBond::Destruct", 0x0001cbf0, &AICharacterBond::Destruct},
};
const BondMethod *g_bondMethod;

void TestBond() {
    for (long round = 0; round < 120; round++) {
        PrepareBond();
        Snapshot prepared;
        Capture(&prepared);
        for (const BondMethod &method : kBondMethods) {
            g_bondMethod = &method;
            RunCase(method.name, round, [] {
                if (g_original)
                    ((BondFn)g_bondMethod->original)(g_bond, 0);
                else
                    (g_bond->*g_bondMethod->port)();
            }, prepared);
        }
        RunCase("AICharacterBond::GetRotPos", round, [] {
            if (g_original)
                ((void (*)(void *, MATRIX4 *))0x0001cc30)(g_bond, &g_out.m);
            else
                AICharacterBond::GetRotPos(g_bond, &g_out.m);
        }, prepared);
        RunCase("AICharacterBond::GetRideRotPos", round, [] {
            if (g_original)
                ((void (*)(void *, MATRIX4 *, Coord4 *))0x0001ccc0)(g_bond, &g_out.m, &g_out.v);
            else
                AICharacterBond::GetRideRotPos(g_bond, &g_out.m, &g_out.v);
        }, prepared);
        g_in.i0 = Range(0, 1);
        RandomMatrix(&g_in.aim, g_player, 2.0f);
        Capture(&prepared);
        RunCase("AICharacterBond::Construct", round, [] {
            g_out.p = g_original ? ((void *(__fastcall *)(void *, int, const MATRIX4 *, int))0x0001cbb0)(
                                       g_bond, 0, &g_in.aim, g_in.i0)
                                 : g_bond->Construct(&g_in.aim, g_in.i0);
        }, prepared);
        RunCase("AICharacterBond::Delete", round, [] {
            g_out.p = g_original ? ((void *(__fastcall *)(void *, int, unsigned))0x0001cc00)(g_bond, 0, g_in.i0)
                                 : g_bond->Delete(g_in.i0);
        }, prepared);
        RunCase("AICharacterBond::Init", round, [] {
            if (g_original)
                ((void (*)(void))0x0001cd80)();
            else
                AICharacterBond::Init();
        }, prepared);
    }
}

// ---- the regions

void BuildRegions() {
    AddRegion(g_elements, sizeof(g_elements), "the elements");
    AddRegion(g_carp, sizeof(g_carp), "the level's elements");
    AddRegion(g_tracking, sizeof(g_tracking), "the tracking");
    AddRegion(g_wakeList, sizeof(g_wakeList), "the wake list");
    AddRegion(g_rangeList, sizeof(g_rangeList), "the range list");
    AddRegion(g_controllerBlock, sizeof(g_controllerBlock), "the controller");
    AddRegion(g_newControllerBlock, sizeof(g_newControllerBlock), "the constructed controller");
    AddRegion(g_charBlocks, sizeof(g_charBlocks), "the characters");
    AddRegion(g_humanBlocks, sizeof(g_humanBlocks), "the humans");
    AddRegion(g_carBlocks, sizeof(g_carBlocks), "the cars");
    AddRegion(g_carAIBlock, sizeof(g_carAIBlock), "the car AI");
    AddRegion(g_vehicleAIBlock, sizeof(g_vehicleAIBlock), "the vehicle AI");
    AddRegion(g_simpleBodyBlock, sizeof(g_simpleBodyBlock), "the simple body");
    AddRegion(g_actorBlocks, sizeof(g_actorBlocks), "the actors");
    AddRegion(g_poserBlock, sizeof(g_poserBlock), "the poser");
    AddRegion(g_matricesBlock, sizeof(g_matricesBlock), "the actor's matrices");
    AddRegion(g_actCharacterBlock, sizeof(g_actCharacterBlock), "the actor's character");
    AddRegion(g_weaponBlock, sizeof(g_weaponBlock), "the actor's weapon");
    AddRegion(g_animControllerBlock, sizeof(g_animControllerBlock), "the animation controller");
    AddRegion(g_nodes, sizeof(g_nodes), "the actor handles");
    AddRegion(g_handsBlock, sizeof(g_handsBlock), "the hands");
    AddRegion(g_bondBlock, sizeof(g_bondBlock), "Bond");
    AddRegion(g_arena, sizeof(g_arena), "the allocations");
    AddRegion(g_navBlock, sizeof(g_navBlock), "the navigator");
    AddRegion(g_cameraBlock, sizeof(g_cameraBlock), "the camera");
    AddRegion(&g_data, sizeof(g_data), "the element records");
    AddRegion(&AIZoneData, sizeof(AIZoneStatics), "the zone statics");
    AddRegion(&HandsStatics, sizeof(AICharacterHandsStatics), "the hands' statics");
    AddRegion(ShadowBondHandIK, 0x60, "Bond's hand IK");
    AddRegion(ShadowBondButtons, 2, "the weapon buttons");
    AddRegion(&fgAIElementController, 4, "fgAIElementController");
    AddRegion(&ActorDatabase, 4, "the actor database", false);
    AddRegion(&glbMissionManager->autoDrive, 1, "the mission manager's auto-drive");
    AddRegion(&glbMissionManager->difficulty, 0x28, "the mission manager's difficulty and enemies");
    if (ShadowMissionSet != NULL)
        AddRegion(&ShadowMissionSet->bond, 4, "the mission set's Bond");
    AddRegion(&ShadowLaunch.difficulty, 4, "the launch page's difficulty", false);
    AddRegion(&fgWorld->trackName, 4, "the track's name", false);
    AddRegion(&ShadowWeaponManager->weapons[kFirstHandWeapon], kHandWeapons * sizeof(WeaponRecord),
              "the hand weapons' records");
    if (fgRoadNetworkData.loaded)
        for (int i = 0; i < fgRoadNetworkData.roadCount; i++)
            AddRegion(fgRoadNetworkData.roads[i], sizeof(WRoad), "a road");
    AddRegion(ShadowRandom, sizeof(SimRandom), "the Simulation's random numbers");
    AddRegion(&g_out, sizeof(g_out), "the answers");
    AddRegion(&g_in, sizeof(g_in), "the inputs", false);
    AddRegion(&ShadowStepCount, 4, "SimStepCount", false);
}

bool InstallHooks() {
    int missing = 0;
    for (int i = 0; i < kHookCount; i++)
        if (!HookInstall(&g_hooks[i], kHooks[i].entry, kHooks[i].fake)) {
            printf("[gameaishadowf] the fake for 0x%08x could not be installed\n", kHooks[i].entry);
            missing++;
        }
    // The allocators' fakes go in only while a side runs: see that they can
    for (int i = 0; i < kSideHookCount; i++) {
        if (!HookInstall(&g_sideHooks[i], kSideHooks[i].entry, kSideHooks[i].fake)) {
            printf("[gameaishadowf] the fake for 0x%08x could not be installed\n", kSideHooks[i].entry);
            missing++;
        }
        HookRemove(&g_sideHooks[i]);
    }
    return missing == 0;
}

}  // namespace

void GameAIShadowF_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEAISHADOWF", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    if (ShadowCars.first == NULL || ShadowCars.first == ShadowCars.last || fgRenderHigh == NULL ||
        glbMissionManager == NULL || ShadowWeaponManager == NULL || fgWorld == NULL) {
        printf("[gameaishadowf] no player car, camera, mission manager, weapon manager or world - nothing run\n");
        fflush(stdout);
        return;
    }
    g_playerSlot = ShadowCars.first[0]->rigidBodySlot;
    g_player = ShadowGetRigidBody(ShadowSim, 0, g_playerSlot)->position;
    printf("[gameaishadowf] live at the first tick: %d cars, road network %s, %s, actor database %s\n",
           int(ShadowCars.last - ShadowCars.first), fgRoadNetworkData.loaded ? "loaded" : "none",
           fgAIElementController != NULL ? "an element controller" : "no element controller",
           ActorDatabase != NULL ? "there" : "none");
    BuildVtables();
    if (!InstallHooks()) {
        for (Hook &hook : g_hooks)
            HookRemove(&hook);
        printf("[gameaishadowf] nothing run\n");
        fflush(stdout);
        return;
    }
    BuildRegions();
    Capture(&g_before);
    const Snapshot &before = g_before;

    TestPure(before);
    TestZones();
    TestTracking();
    TestController(before);
    TestHands();
    TestBond();

    Restore(before);
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
    ResetFpu();
    printf("[gameaishadowf] elements, zones, hands, Bond vs originals: %ld cases, %ld checks, %ld differ%s\n",
           g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameaishadowf]   %ld calls faulted\n", g_faults);
    if (!g_sideHooksOk)
        printf("[gameaishadowf]   an allocator's fake could not be installed for some case\n");
    fflush(stdout);
}
