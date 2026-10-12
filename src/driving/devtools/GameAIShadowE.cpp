#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameAIShadowE.h"
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
#include "../camera/PlayerCamera.h"
#include "../game/MissionManager.h"
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../game/AIEnemy.h"
#include "../game/AIEnemyGround.h"
#include "../game/AIEnemyRiders.h"
#include "../game/AIVehicle.h"
#include "../game/BondCar.h"
#include "../game/CharacterSound.h"
#include "../game/Human.h"
#include "../physics/RigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RenderHigh.h"
#include "../render/RSceneObj.hpp"
#include "../world/Collider.h"
#include "../world/Targeting.h"
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.ai's package E, run once from the first simulation tick when NIGHTFIRE_GAMEAISHADOWE is set.
// Each case runs the ORIGINAL (every entry of the package swapped back together) and then the PORT from the same
// state, and compares what they leave - the synthetic enemy and everything round it (its actor, poser, matrices,
// controllers, human, voice, collider, target, car, car AI, the car AI's target), the Simulation's random generator,
// the mission manager's fields, the statics PlayZoneInjuryAnim and the driver keep (the original's at the game's
// addresses, the port's its own) - the answers, and a log of every call out:
//
//   - MinFloat, MaxFloat (NaNs among the inputs), SimRandom::Fraction, WorldCollisionInfo::Construct
//     (less the segment ends the original leaves as its stack had them), VectorConstructorIterator (a logging
//     constructor), IsBumping;
//   - every method of the six kinds on a synthetic enemy of the kind: the constructors (the allocations' contents
//     compared and freed), the destructors and scalar deleting destructors (flags 0), the ride callbacks
//     (GetRotPos x4), and every virtual method, from random states and sub-states, its car's steering, wheels,
//     reset, EMP and hit points scripted, the target's range and bearing perturbed (the enemy placed round a live
//     car), the mission's auto-drive and state perturbed.
//
// The enemy's own vtable is one of fakes (slot 3 answers the synthetic car; 33-35 scripted answers; the rest log),
// so each method is tested alone. The car's vtable is fakes too. The actor is synthetic down to its poser, matrices,
// controllers and character, so ActActor's small methods run for real on both sides. Fakes (5-byte jumps over the
// code both sides reach): ActActor::SetNewAnimation, SetNewCrossFadeAnimation, CurrentPositionRotateY, RotateActor;
// CrossFadeAnimationController::StartNewCrossFade; ActPoser::GetBoneIndex, Get/SetAnimationOrigin; the IK array's
// CreateIKs and SetInfo; the pose overrides' Create, Set and Set (array); ACharacter::Say; SMissionManager::IncKills;
// WTargetPicker::Register/UnregisterTarget; WTargetable::RemoveReference; WCollider::InRegion (true, so no
// collider is reallocated), GetWorldNormal, the constructor and destructor; WWorldPos::FindClosestFace and
// HeightAtPoint; RSceneObj::LookupFXHandle and LocateFX; RAnimEngine::Handle::ProcessStimuli; Human::Activate and
// Deactivate; AttributeSet::LookupInt; RPlayerCamera::GetForwardAimVec4; ActActorDatabase::KillActorByHandle.
//
// A mutation the test sees: AICharacterEnemySunroof::UpdateLeanDirection testing the car's acceleration along its
// x axis (orientation row 0) for its forward one (row 2) - the leans' directions differ (the last velocity is
// jolted by up to 60 either way, so the 40 and 45 thresholds are crossed both ways).
// ---------------------------------------------------------------------------------------------------------------

// The port's statics (game/AIEnemy.cpp, AIEnemyGround.cpp)
extern int32_t EnemyGroundLastHit;
extern int32_t EnemyGroundLastDeath;
extern int32_t EnemyGroundLastScream;
extern ActIKSolveInfo DriverHandIK[2];

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x0001c5e0, 0x0001c600, 0x0001c620, 0x0001ca50, 0x0001ca90, 0x0001d530, 0x0001d5f0, 0x0001d640, 0x0001d650,
    0x0001d680, 0x0001d7f0, 0x0001d840, 0x0001d990, 0x0001d9d0, 0x0001d9f0, 0x0001da60, 0x0001da70, 0x0001da80,
    0x0001dc50, 0x0001dc80, 0x0001de80, 0x0001dea0, 0x0001df40, 0x0001df70, 0x0001dfc0, 0x0001e010, 0x0001e070,
    0x0001e090, 0x0001e380, 0x0001e400, 0x0001e440, 0x0001e9a0, 0x0001ea60, 0x0001ee60, 0x0001f000, 0x0001f0c0,
    0x0001f180, 0x0001f3c0, 0x0001f8f0, 0x0001fac0, 0x0001fb70, 0x0001fc70, 0x0001fc80, 0x0001fcd0, 0x0001fcf0,
    0x0001fd30, 0x0001fd40, 0x0001fdd0, 0x0001fe00, 0x0001ff90, 0x00020090, 0x000203c0, 0x000205b0, 0x000205e0,
    0x000206d0, 0x00020710, 0x00020870, 0x00020970, 0x00020980, 0x000209d0, 0x00020a00, 0x00020a60, 0x00020b40,
    0x00020bb0, 0x00020c30, 0x00020c70, 0x00020e30, 0x00020ec0, 0x00020f40, 0x00020f90, 0x000210f0, 0x00021230,
    0x00021260, 0x00021300, 0x000213c0, 0x000215b0, 0x00021600, 0x00021630, 0x00021670, 0x00021710, 0x00021740,
    0x000217e0, 0x000218f0, 0x000219e0, 0x00021b70, 0x00021d40, 0x00021d50, 0x00021dc0, 0x00021df0, 0x00021e70,
    0x00021f10, 0x00021f70, 0x00022010, 0x00022050, 0x00022150, 0x00022280, 0x000222c0, 0x00022310, 0x000223a0,
    0x000224f0, 0x00022680, 0x00022770,
};

#define OrigStaticLastHit I32_AT(0x001b54c8)
#define OrigStaticLastDeath I32_AT(0x001b57f0)
#define OrigStaticLastScream I32_AT(0x001b57f4)
#define OrigDriverHandIK ((ActIKSolveInfo *)0x001b52c0)

// ---- the game's state

#define ShadowSim ((void *)0x00233ff0)
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowStepCount I32_AT(0x00234e34)
#define ShadowCars (*(GameVector<PhysicsObject *> *)0x00234e3c)
#define ShadowHumans (*(GameVector<Human *> *)0x00234e6c)
#define ShadowGetRigidBody ((RigidBody *(__fastcall *)(void *, int, int))0x000b2700)

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults, g_skipped;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameaishadowe]   %s #%ld: %s\n", what, index, detail);
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

uint32_t g_seed = 0x0a1e0e0d;

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

constexpr int kCharacterBytes = 0x1c0;      // the largest kind's
constexpr int kOtherHumans = 4;

alignas(16) uint8_t g_charBlock[kCharacterBytes];
alignas(16) uint8_t g_otherBlock[sizeof(AICharacter)];
alignas(16) uint8_t g_actorBlock[sizeof(ActActor)];
alignas(16) uint8_t g_otherActorBlock[sizeof(ActActor)];
alignas(16) uint8_t g_poserBlock[sizeof(ActPoser)];
alignas(16) uint8_t g_fadePoserBlock[sizeof(ActPoser)];
alignas(16) uint8_t g_matricesBlock[sizeof(ActActorMatrices)];
alignas(16) uint8_t g_controllerBlock[sizeof(AnimationController)];
alignas(16) uint8_t g_crossFadeBlock[sizeof(CrossFadeAnimationController)];
alignas(16) uint8_t g_characterBlock[sizeof(ActCharacter)];
alignas(16) uint8_t g_humanBlock[sizeof(Human)];
alignas(16) uint8_t g_otherHumanBlocks[kOtherHumans][sizeof(Human)];
alignas(16) uint8_t g_carBlock[sizeof(PBondCar)];
alignas(16) uint8_t g_carAIBlock[sizeof(AIGroundVehicle)];
alignas(16) uint8_t g_targetVehicleBlock[sizeof(AIVehicle)];
alignas(16) uint8_t g_colliderBlock[sizeof(WCollider)];
alignas(16) uint8_t g_voiceBlock[sizeof(ACharacter)];
alignas(16) uint8_t g_targetableBlock[sizeof(WTargetable)];
alignas(16) uint8_t g_sceneObjBlock[sizeof(RSceneObj)];
alignas(16) uint8_t g_handleBlock[0x40];
alignas(16) uint8_t g_arrayBlock[0x20];     // the IK and pose override arrays' (their methods are faked)
Human *g_humanList[kOtherHumans];
AICharacterActorLink g_link, g_otherLink, g_newLink;
AICharacterHitShape g_hitShape;
float g_hitPoints, g_carHitPoints;
uint32_t g_effects[2];                      // the car's steering effects (only their addresses are used)

AICharacter *const g_char = reinterpret_cast<AICharacter *>(g_charBlock);
AICharacter *const g_other = reinterpret_cast<AICharacter *>(g_otherBlock);
ActActor *const g_actor = reinterpret_cast<ActActor *>(g_actorBlock);
ActActor *const g_otherActor = reinterpret_cast<ActActor *>(g_otherActorBlock);
ActPoser *const g_poser = reinterpret_cast<ActPoser *>(g_poserBlock);
ActPoser *const g_fadePoser = reinterpret_cast<ActPoser *>(g_fadePoserBlock);
ActActorMatrices *const g_matrices = reinterpret_cast<ActActorMatrices *>(g_matricesBlock);
AnimationController *const g_controller = reinterpret_cast<AnimationController *>(g_controllerBlock);
CrossFadeAnimationController *const g_crossFade = reinterpret_cast<CrossFadeAnimationController *>(g_crossFadeBlock);
Human *const g_human = reinterpret_cast<Human *>(g_humanBlock);
PBondCar *const g_car = reinterpret_cast<PBondCar *>(g_carBlock);
AIGroundVehicle *const g_carAI = reinterpret_cast<AIGroundVehicle *>(g_carAIBlock);
AIVehicle *const g_targetVehicle = reinterpret_cast<AIVehicle *>(g_targetVehicleBlock);
WCollider *const g_collider = reinterpret_cast<WCollider *>(g_colliderBlock);
ACharacter *const g_voice = reinterpret_cast<ACharacter *>(g_voiceBlock);
WTargetable *const g_targetable = reinterpret_cast<WTargetable *>(g_targetableBlock);
RSceneObj *const g_sceneObj = reinterpret_cast<RSceneObj *>(g_sceneObjBlock);

void *g_charVtable[36];
void *g_otherVtable[36];
void *g_carVtable[PVehicle::kSlotCount];
void *g_voiceVtable[4];

// One case's inputs and scripted answers
struct Inputs {
    int answers[3];                 // the enemy's slots 33-35
    bool crossFadeNull;             // SetNewCrossFadeAnimation's answer is NULL
    int bones[2];
    Coord3 origin;
    bool hit;                       // GetWorldNormal's
    Coord4 hitPoint, hitNormal;
    bool faceFound;
    double height;
    bool fxFound[2];
    MATRIX4 fxMatrices[2];
    int snowmobile;
    Coord4 aim;
    bool noCarAI;
    int reset;
    float steering[2];
    float compression[4];
    uint8_t emp;
    bool otherInCar;
    float f0, f1;
    int i0, i1;
    Coord3 point;
} g_in;

int g_steerCalls, g_boneCalls, g_fxCalls;

// The answers, the allocations a constructor made, and the statics (compared)
struct Outputs {
    double d;
    int i;
    const void *p;
    float f;
    uint8_t side, high;
    uint8_t pad[2];
    Coord4 velocity;
    MATRIX4 matrix;
} g_out;

struct Built {
    uint8_t targetable[sizeof(WTargetable)];
    AICharacterHitShape shape;
    char zoneMap[60];
} g_built;

struct Statics {
    int32_t lastHit, lastDeath, lastScream;
    ActIKSolveInfo handIK[2];
} g_statics;

// ---- the fakes

enum : uint32_t {
    kTagNewAnim = 0x4e414e4d, kTagCrossFade = 0x58464e41, kTagStartFade = 0x53464144, kTagRotateY = 0x524f5459,
    kTagRotate = 0x524f5420, kTagGetOrigin = 0x474f5247, kTagSetOrigin = 0x534f5247, kTagBone = 0x424f4e45,
    kTagCreateIK = 0x43494b20, kTagSetInfo = 0x53494e46, kTagCreateOv = 0x43524f56, kTagSetOv = 0x534f5652,
    kTagSetOvs = 0x534f5653, kTagSay = 0x53415920, kTagKills = 0x4b494c53, kTagRegister = 0x52454720,
    kTagUnregister = 0x554e5247, kTagRemoveRef = 0x52524546, kTagRegion = 0x52474e20, kTagNormal = 0x4e524d4c,
    kTagFace = 0x46414345, kTagHeight = 0x48474854, kTagLookupFX = 0x4c464658, kTagLocateFX = 0x4c4f4658,
    kTagStimuli = 0x5354494d, kTagActivate = 0x41435456, kTagDeactivate = 0x44414354, kTagLookupInt = 0x4c4b4954,
    kTagAim = 0x41494d20, kTagColliderNew = 0x434f4c4e, kTagColliderDel = 0x434f4c44, kTagKill = 0x4b494c4c,
    kTagSlot = 0x534c0000, kTagOtherSlot = 0x4f530000, kTagCarSlot = 0x43530000, kTagVoiceDelete = 0x56444c20,
    kTagConstruct = 0x434f4e53,
};

// A target by what it is: the synthetic one, a fresh allocation (a constructor's), or none
void LogTarget(const WTargetable *target) {
    Log(target == NULL ? 0 : target == g_targetable ? 1 : 2);
}

AnimationController* __fastcall FakeSetNewAnimation(ActActor *actor, int, int bank, int index) {
    Log(kTagNewAnim);
    LogPointer(actor);
    Log(bank);
    Log(index);
    return g_controller;
}

AnimationController* __fastcall FakeSetNewCrossFade(ActActor *actor, int, int bank, int index, float blendTime) {
    Log(kTagCrossFade);
    LogPointer(actor);
    Log(bank);
    Log(index);
    LogFloat(blendTime);
    return g_in.crossFadeNull ? NULL : g_crossFade;
}

void __fastcall FakeStartNewCrossFade(CrossFadeAnimationController *crossFade, int, int bank, int index,
                                      float blendTime) {
    Log(kTagStartFade);
    LogPointer(crossFade);
    Log(bank);
    Log(index);
    LogFloat(blendTime);
}

void __fastcall FakeRotateY(ActActor *actor, int, float turns) {
    Log(kTagRotateY);
    LogPointer(actor);
    LogFloat(turns);
}

void __fastcall FakeRotateActor(ActActor *actor, int, float turns) {
    Log(kTagRotate);
    LogPointer(actor);
    LogFloat(turns);
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
    return g_in.bones[g_boneCalls++ & 1];
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

void __fastcall FakeSetOverride(void *array, int, int index, const void *matrix, float weight) {
    Log(kTagSetOv);
    LogPointer(array);
    Log(index);
    LogBytes(matrix, 64);
    LogFloat(weight);
}

void __fastcall FakeSetOverrides(void *array, int, int count, const void *matrices, const float *weights) {
    Log(kTagSetOvs);
    LogPointer(array);
    Log(count);
    LogBytes(matrices, 128);
    LogBytes(weights, 8);
}

void __fastcall FakeSay(ACharacter *voice, int, const char *what) {
    Log(kTagSay);
    LogPointer(voice);
    LogString(what);
}

void __fastcall FakeIncKills(void *manager, int, int kills) {
    Log(kTagKills);
    LogPointer(manager);
    Log(kills);
}

void __fastcall FakeRegister(void *picker, int, WTargetable *target) {
    Log(kTagRegister);
    LogPointer(picker);
    LogTarget(target);
}

void __fastcall FakeUnregister(void *picker, int, WTargetable *target) {
    Log(kTagUnregister);
    LogPointer(picker);
    LogTarget(target);
}

void __fastcall FakeRemoveReference(WTargetable *target, int) {
    Log(kTagRemoveRef);
    LogTarget(target);
}

bool __fastcall FakeInRegion(WCollider *collider, int, const Coord3 *position, float radius, uint32_t mask) {
    Log(kTagRegion);
    LogPointer(collider);
    LogBytes(position, 12);
    LogFloat(radius);
    Log(mask);
    return true;
}

bool __fastcall FakeWorldNormal(WCollider *collider, int, const Coord4 *segment, WorldCollisionInfo *info) {
    Log(kTagNormal);
    LogPointer(collider);
    LogBytes(segment, 32);
    // What the constructor left (less the segment ends the original leaves as its stack had them)
    LogBytes(info, 0x10);
    LogBytes(&info->instance, 0x20);
    if (g_in.hit) {
        info->point = g_in.hitPoint;
        info->normal = g_in.hitNormal;
    }
    return g_in.hit;
}

bool __fastcall FakeClosestFace(WWorldPos *worldPos, int, const Coord3 *point, bool keepCurrent) {
    Log(kTagFace);
    LogPointer(worldPos);
    LogBytes(point, 12);
    Log(keepCurrent);
    return g_in.faceFound;
}

double __fastcall FakeHeight(WWorldPos *worldPos, int, const Coord3 *point, bool flag) {
    Log(kTagHeight);
    LogPointer(worldPos);
    LogBytes(point, 12);
    Log(flag);
    return g_in.height;
}

void* __fastcall FakeLookupFX(RSceneObj *object, int, const char *name, int instance) {
    Log(kTagLookupFX);
    LogPointer(object);
    LogString(name);
    Log(instance);
    int which = g_fxCalls++ & 1;
    return g_in.fxFound[which] ? &g_effects[which] : NULL;
}

void __fastcall FakeLocateFX(RSceneObj *object, int, const void *effect, MATRIX4 *transform, bool world) {
    Log(kTagLocateFX);
    LogPointer(object);
    LogPointer(effect);
    Log(world);
    *transform = g_in.fxMatrices[effect == &g_effects[1] ? 1 : 0];
}

void __fastcall FakeProcessStimuli(void *handle, int, uint32_t id, uint8_t stimulus, uint32_t tick, int mode) {
    Log(kTagStimuli);
    LogPointer(handle);
    Log(id);
    Log(stimulus);
    Log(tick);
    Log(mode);
}

void __fastcall FakeActivate(Human *human, int) {
    Log(kTagActivate);
    LogPointer(human);
}

void __fastcall FakeDeactivate(Human *human, int) {
    Log(kTagDeactivate);
    LogPointer(human);
}

int32_t __fastcall FakeLookupInt(void *attributes, int, const char *key, bool *found) {
    Log(kTagLookupInt);
    LogPointer(attributes);
    LogString(key);
    LogPointer(found);
    return g_in.snowmobile;
}

const Coord4* __fastcall FakeAim(void *camera, int, float blend) {
    Log(kTagAim);
    LogPointer(camera);
    LogFloat(blend);
    return &g_in.aim;
}

WCollider* __fastcall FakeColliderConstruct(WCollider *collider, int, const Coord3 *position, float radius, bool flat,
                                            uint32_t mask) {
    Log(kTagColliderNew);
    LogBytes(position, 12);
    LogFloat(radius);
    Log(flat);
    Log(mask);
    return collider;
}

void __fastcall FakeColliderDestruct(WCollider *collider, int) {
    Log(kTagColliderDel);
    LogPointer(collider);
}

void FakeKillActor(PointerListNode *handle) {
    Log(kTagKill);
    LogPointer(handle);
}

void* __fastcall FakeVoiceDelete(void *voice, int, unsigned flags) {
    Log(kTagVoiceDelete);
    LogPointer(voice);
    Log(flags);
    return voice;
}

enum HookIndex {
    kHookNewAnim, kHookCrossFade, kHookStartFade, kHookRotateY, kHookRotate, kHookGetOrigin, kHookSetOrigin,
    kHookBone, kHookCreateIK, kHookSetInfo, kHookCreateOv, kHookSetOv, kHookSetOvs, kHookSay, kHookKills,
    kHookRegister, kHookUnregister, kHookRemoveRef, kHookRegion, kHookNormal, kHookFace, kHookHeight, kHookLookupFX,
    kHookLocateFX, kHookStimuli, kHookActivate, kHookDeactivate, kHookLookupInt, kHookAim, kHookColliderNew,
    kHookColliderDel, kHookKill, kHookCount
};

const uint32_t kHookEntries[kHookCount] = {
    0x00012640, 0x000126e0, 0x00011210, 0x00012280, 0x00012350, 0x00018780, 0x00018b30, 0x000186e0, 0x00016c30,
    0x000159e0, 0x00015a30, 0x00015b40, 0x00015d00, 0x0011dad0, 0x000b6770, 0x000ced50, 0x000ceb60, 0x000cdb80,
    0x000bd820, 0x000bd890, 0x000d31f0, 0x000d2f90, 0x0008ec20, 0x0008eca0, 0x00077d70, 0x0005dcc0, 0x0005dce0,
    0x00058180, 0x00081890, 0x000be540, 0x000be180, 0x00013270,
};

const void *const kHookTargets[kHookCount] = {
    (const void *)&FakeSetNewAnimation, (const void *)&FakeSetNewCrossFade, (const void *)&FakeStartNewCrossFade,
    (const void *)&FakeRotateY, (const void *)&FakeRotateActor, (const void *)&FakeGetOrigin,
    (const void *)&FakeSetOrigin, (const void *)&FakeBoneIndex, (const void *)&FakeCreateIKs,
    (const void *)&FakeSetInfo, (const void *)&FakeCreateOverrides, (const void *)&FakeSetOverride,
    (const void *)&FakeSetOverrides, (const void *)&FakeSay, (const void *)&FakeIncKills, (const void *)&FakeRegister,
    (const void *)&FakeUnregister, (const void *)&FakeRemoveReference, (const void *)&FakeInRegion,
    (const void *)&FakeWorldNormal, (const void *)&FakeClosestFace, (const void *)&FakeHeight,
    (const void *)&FakeLookupFX, (const void *)&FakeLocateFX, (const void *)&FakeProcessStimuli,
    (const void *)&FakeActivate, (const void *)&FakeDeactivate, (const void *)&FakeLookupInt, (const void *)&FakeAim,
    (const void *)&FakeColliderConstruct, (const void *)&FakeColliderDestruct, (const void *)&FakeKillActor,
};

// ---- the vtables of fakes

// The enemy's: each slot logs itself; 3 answers the car, 33-35 the scripted answers
template <int N> int __fastcall FakeCharSlot(void *object, int) {
    Log(kTagSlot + N);
    LogPointer(object);
    if (N >= AICharacterEnemy::kSlotTargetIsInRange)
        return g_in.answers[N - AICharacterEnemy::kSlotTargetIsInRange];
    return 0;
}

void* __fastcall FakeCharVehicle(void *object, int) {
    Log(kTagSlot + 3);
    LogPointer(object);
    return g_car;
}

// Another character's (a partner's): slot 3 answers the car or none
template <int N> int __fastcall FakeOtherSlot(void *object, int) {
    Log(kTagOtherSlot + N);
    LogPointer(object);
    return 0;
}

void* __fastcall FakeOtherVehicle(void *object, int) {
    Log(kTagOtherSlot + 3);
    LogPointer(object);
    return g_in.otherInCar ? g_car : NULL;
}

// The car's: each slot logs itself, the ones the enemies call answer as scripted
template <int N> void __fastcall FakeCarSlot(void *car, int) {
    Log(kTagCarSlot + N);
    LogPointer(car);
}

AIGroundVehicle* __fastcall FakeCarAI(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetAIGroundVehiclePtr);
    LogPointer(car);
    return g_in.noCarAI ? NULL : g_carAI;
}

int __fastcall FakeCarReset(void *car, int) {
    Log(kTagCarSlot + PVehicle::kGetResetAvailable);
    LogPointer(car);
    return g_in.reset;
}

void __fastcall FakeCarEnableBeacon(void *car, int, bool enable) {
    Log(kTagCarSlot + PVehicle::kEnableTargetBeacon);
    LogPointer(car);
    Log(enable);
}

BondCarControl* __fastcall FakeCarControl(void *car, int, BondCarControl *result) {
    Log(kTagCarSlot + PVehicle::kGetCarControl);
    LogPointer(car);
    memset(result, 0, sizeof(BondCarControl));
    result->steering = g_in.steering[g_steerCalls++ & 1];
    return result;
}

uint8_t __fastcall FakeCarEmp(void *car, int) {
    Log(kTagCarSlot + PVehicle::kEmpActive);
    LogPointer(car);
    return g_in.emp;
}

void __fastcall FakeCarSetScoreable(void *car, int, bool scoreable) {
    Log(kTagCarSlot + PVehicle::kSetScoreable);
    LogPointer(car);
    Log(scoreable);
}

float __fastcall FakeCarSuspension(void *car, int, int wheel) {
    Log(kTagCarSlot + PVehicle::kGetSuspensionCompression);
    LogPointer(car);
    Log(wheel);
    return g_in.compression[wheel & 3];
}

#define FAKES_4(F, n) (void *)&F<n>, (void *)&F<n + 1>, (void *)&F<n + 2>, (void *)&F<n + 3>
#define FAKES_16(F, n) FAKES_4(F, n), FAKES_4(F, n + 4), FAKES_4(F, n + 8), FAKES_4(F, n + 12)
void *const kCharSlotFakes[36] = {FAKES_16(FakeCharSlot, 0), FAKES_16(FakeCharSlot, 16), FAKES_4(FakeCharSlot, 32)};
void *const kOtherSlotFakes[36] = {FAKES_16(FakeOtherSlot, 0), FAKES_16(FakeOtherSlot, 16),
                                   FAKES_4(FakeOtherSlot, 32)};
void *const kCarSlotFakes[84] = {FAKES_16(FakeCarSlot, 0), FAKES_16(FakeCarSlot, 16), FAKES_16(FakeCarSlot, 32),
                                 FAKES_16(FakeCarSlot, 48), FAKES_16(FakeCarSlot, 64), FAKES_4(FakeCarSlot, 80)};
#undef FAKES_16
#undef FAKES_4

void BuildVtables() {
    for (int i = 0; i < 36; i++) {
        g_charVtable[i] = kCharSlotFakes[i];
        g_otherVtable[i] = kOtherSlotFakes[i];
    }
    g_charVtable[AICharacterEnemy::kSlotGetVehiclePtr] = (void *)&FakeCharVehicle;
    g_otherVtable[AICharacterEnemy::kSlotGetVehiclePtr] = (void *)&FakeOtherVehicle;
    for (int i = 0; i < PVehicle::kSlotCount; i++)
        g_carVtable[i] = kCarSlotFakes[i];
    g_carVtable[PVehicle::kGetAIGroundVehiclePtr] = (void *)&FakeCarAI;
    g_carVtable[PVehicle::kGetResetAvailable] = (void *)&FakeCarReset;
    g_carVtable[PVehicle::kEnableTargetBeacon] = (void *)&FakeCarEnableBeacon;
    g_carVtable[PVehicle::kGetCarControl] = (void *)&FakeCarControl;
    g_carVtable[PVehicle::kEmpActive] = (void *)&FakeCarEmp;
    g_carVtable[PVehicle::kSetScoreable] = (void *)&FakeCarSetScoreable;
    g_carVtable[PVehicle::kGetSuspensionCompression] = (void *)&FakeCarSuspension;
    g_voiceVtable[0] = (void *)&FakeVoiceDelete;
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

// The statics each side keeps in its own place, gathered where they are compared
void GatherStatics(bool original) {
    if (original) {
        g_statics.lastHit = OrigStaticLastHit;
        g_statics.lastDeath = OrigStaticLastDeath;
        g_statics.lastScream = OrigStaticLastScream;
        memcpy(g_statics.handIK, OrigDriverHandIK, sizeof(g_statics.handIK));
    } else {
        g_statics.lastHit = EnemyGroundLastHit;
        g_statics.lastDeath = EnemyGroundLastDeath;
        g_statics.lastScream = EnemyGroundLastScream;
        memcpy(g_statics.handIK, DriverHandIK, sizeof(g_statics.handIK));
    }
}

void RunSide(SideFn run, bool original) {
    g_original = original;
    g_steerCalls = 0;
    g_boneCalls = 0;
    g_fxCalls = 0;
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
    GatherStatics(original);
}

// The inputs are in place: both sides from them, the results compared, the inputs put back
void RunCase(const char *what, long index, SideFn run) {
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
    Restore(inputs);
}

// ---- the inputs

int CarCount() {
    return int(ShadowCars.last - ShadowCars.first);
}

PhysicsObject *RandomCar() {
    return ShadowCars.first[Range(0, CarCount() - 1)];
}

const Coord3 &BodyPosition(PhysicsObject *object) {
    return ShadowGetRigidBody(ShadowSim, 0, object->rigidBodySlot)->position;
}

Coord3 Around(const Coord3 &centre, float spread) {
    Coord3 v = {centre.x + Uniform(-spread, spread), centre.y + Uniform(-spread * 0.1f, spread * 0.1f),
                centre.z + Uniform(-spread, spread)};
    return v;
}

void RandomMatrix(MATRIX4 *m, const Coord3 &at, float spread) {
    VU0_MATRIX4Init(m);
    float turns = Uniform(0.0f, 1.0f);
    m->mtx[0][0] = cosf(turns * 6.2831853f);
    m->mtx[0][2] = -sinf(turns * 6.2831853f);
    m->mtx[2][0] = sinf(turns * 6.2831853f);
    m->mtx[2][2] = cosf(turns * 6.2831853f);
    Coord3 p = Around(at, spread);
    m->mtx[3][0] = p.x;
    m->mtx[3][1] = p.y;
    m->mtx[3][2] = p.z;
}

void PrepareScript(const Coord3 &position) {
    for (int i = 0; i < 3; i++)
        g_in.answers[i] = Chance(50);
    g_in.crossFadeNull = Chance(20);
    g_in.bones[0] = Chance(80) ? Range(0, 30) : -1;
    g_in.bones[1] = Chance(80) ? Range(0, 30) : -1;
    g_in.origin = Around(position, 2.0f);
    g_in.hit = Chance(40);
    Coord3 hit = Around(position, 3.0f);
    g_in.hitPoint = {hit.x, hit.y, hit.z, 1.0f};
    float turns = Uniform(0.0f, 1.0f);
    g_in.hitNormal = {sinf(turns * 6.2831853f), 0.0f, cosf(turns * 6.2831853f), 0.0f};
    g_in.faceFound = Chance(80);
    g_in.height = position.y + (Chance(30) ? Uniform(-4.0f, 4.0f) : Uniform(-0.5f, 0.5f));
    for (int i = 0; i < 2; i++) {
        g_in.fxFound[i] = Chance(85);
        RandomMatrix(&g_in.fxMatrices[i], position, 2.0f);
    }
    g_in.snowmobile = Chance(50);
    // The camera's aim: at the character, or near
    const float *eye = fgRenderHigh->views[0].camera->matrix.mtx[3];
    Coord4 aim = {position.x - eye[0], position.y + 0.2f - eye[1], position.z - eye[2], 0.0f};
    if (Chance(40)) {
        aim.x += Uniform(-5.0f, 5.0f);
        aim.y += Uniform(-2.0f, 2.0f);
        aim.z += Uniform(-5.0f, 5.0f);
    }
    float length = sqrtf(aim.x * aim.x + aim.y * aim.y + aim.z * aim.z);
    if (length > 0.0f) {
        aim.x /= length;
        aim.y /= length;
        aim.z /= length;
    }
    g_in.aim = aim;
    g_in.noCarAI = Chance(20);
    g_in.reset = Chance(80) ? 0 : 1;
    for (int i = 0; i < 2; i++)
        g_in.steering[i] = Chance(40) ? Near(Chance(50) ? 0.1f : -0.1f) : Uniform(-1.0f, 1.0f);
    for (int i = 0; i < 4; i++)
        g_in.compression[i] = Chance(50) ? Near(0.12f) : Uniform(0.0f, 0.3f);
    g_in.emp = uint8_t(Chance(30));
    g_in.otherInCar = Chance(70);
}

// The actor below the enemy and the partner's: posers, matrices, controllers synthetic
void PrepareActor(const MATRIX4 &transform) {
    memset(g_actorBlock, 0, sizeof(g_actorBlock));
    memset(g_poserBlock, 0, sizeof(g_poserBlock));
    memset(g_fadePoserBlock, 0, sizeof(g_fadePoserBlock));
    memset(g_controllerBlock, 0, sizeof(g_controllerBlock));
    memset(g_crossFadeBlock, 0, sizeof(g_crossFadeBlock));
    memset(g_characterBlock, 0, sizeof(g_characterBlock));
    g_actor->character = reinterpret_cast<ActCharacter *>(g_characterBlock);
    g_actor->poser = g_poser;
    g_actor->matrices = g_matrices;
    g_actor->controller = g_controller;
    g_actor->visible = Chance(60);
    g_actor->followGround = Chance(80);
    g_actor->scale = Uniform(0.8f, 1.2f);
    g_actor->baseScale = Uniform(0.8f, 1.2f);
    g_poser->mode = Chance(80) ? kPoseModeNormal : Range(0, 3);
    g_poser->time = Uniform(0.0f, 40.0f);
    g_poser->endTime = Chance(50) ? g_poser->time + Uniform(-0.5f, 3.0f) : Uniform(0.0f, 40.0f);
    g_poser->framesPerSecond = 20.0f;
    g_poser->frameTime = Uniform(0.01f, 0.05f);
    g_poser->timeScale = Uniform(0.5f, 2.0f);
    g_poser->ikSolvers = reinterpret_cast<ActIKSolverArray *>(g_arrayBlock);
    g_poser->poseOverrides = reinterpret_cast<ActGlobalPoseOverrideArray *>(g_arrayBlock + 0x10);
    g_fadePoser->mode = Chance(60) ? kPoseModeNormal : Range(0, 3);
    g_controller->poser = g_poser;
    g_crossFade->poser = g_fadePoser;
    g_matrices->local = transform;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 3; c++)
            g_matrices->local.mtx[r][c] += Uniform(-0.01f, 0.01f);
    g_matrices->world = g_matrices->local;
    g_matrices->weapon[0] = g_matrices->local;
    g_matrices->weapon[0].mtx[3][1] += Uniform(0.5f, 1.5f);
    g_matrices->weapon[0].mtx[3][0] += Uniform(-0.3f, 0.3f);
    g_matrices->weapon[1] = g_matrices->weapon[0];
    g_link.actor = g_actor;
    g_newLink.actor = g_actor;
    memset(g_otherActorBlock, 0, sizeof(g_otherActorBlock));
    g_otherActor->visible = Chance(40);
    g_otherLink.actor = g_otherActor;
}

// The car the riders sit in: a live car's body, the rest synthetic
void PrepareCar(PhysicsObject *live) {
    memset(g_carBlock, 0, sizeof(g_carBlock));
    g_car->vtable = g_carVtable;
    g_car->rigidBodySlot = live->rigidBodySlot;
    g_car->renderObject = g_sceneObj;
    g_car->hitPointLoc = Chance(90) ? &g_carHitPoints : NULL;
    g_carHitPoints = Chance(30) ? (Chance(50) ? 0.0f : -Uniform(0.0f, 50.0f)) : Uniform(0.0f, 700.0f);
    g_car->scoreable = uint8_t(Chance(50));
    memset(g_sceneObjBlock, 0, sizeof(g_sceneObjBlock));
    g_sceneObj->animHandle = reinterpret_cast<Handle *>(g_handleBlock);
    memset(g_carAIBlock, 0, sizeof(g_carAIBlock));
    g_carAI->target = Chance(80) ? static_cast<AIGroundVehicle *>(g_targetVehicle) : NULL;
    g_carAI->stopped = uint8_t(Range(0, 1));
    g_carAI->weaponsEnabled = uint8_t(Range(0, 1));
    g_carAI->turretAngle = Uniform(-0.5f, 0.5f);
    memset(g_targetVehicleBlock, 0, sizeof(g_targetVehicleBlock));
    g_targetVehicle->active = Chance(70);
    g_targetVehicle->physicsObject = Chance(60) ? RandomCar() : NULL;
    RandomMatrix(&g_targetVehicle->rotPos, BodyPosition(live), 60.0f);
}

// The fields every kind has, at random round a position
void PrepareBase(AICharacter *c, const Coord3 &at) {
    c->vtable = g_charVtable;
    RandomMatrix(&c->transform, at, Chance(50) ? 6.0f : 60.0f);
    PrepareActor(c->transform);
    static const int kSubStates[] = {0, 1, 8, 17, 20, 21, 22};
    c->state = Range(0, 22);
    c->subState = Chance(85) ? kSubStates[Range(0, 6)] : Range(0, 30);
    c->animation = Range(0, 30);
    c->weapon = Chance(85) ? Range(1, 2) : 0;
    c->female = Range(0, 1);
    c->targetRangeSquared = Chance(20) ? 1e9f : Near(Chance(50) ? 75.0f * 75.0f : Uniform(10.0f, 10000.0f));
    g_hitPoints = Chance(30) ? Near(Uniform(0.0f, 20.0f)) : Uniform(0.0f, 100.0f);
    c->hitPoints = Chance(85) ? &g_hitPoints : NULL;
    c->newDamage = Uniform(0.0f, 10.0f);
    c->initialHitPoints = Uniform(0.0f, 100.0f);
    c->human = g_human;
    c->animBank = Range(0, 17);
    c->clipSize = Chance(30) ? 0 : Range(1, 25);
    c->rounds = Range(-1, 25);
    c->targetSide = Range(0, 1);
    c->targetBearing = Chance(70) ? (Chance(50) ? 25 : 45) : Range(0, 127);
    c->shotInterval = Range(5, 60);
    c->nextShotStep = ShadowStepCount + Range(-5, 5);
    c->voice = g_voice;
    c->actorLink = &g_link;
    c->animController = g_controller;
    c->crossFade = Chance(50) ? g_crossFade : NULL;
    g_hitShape = {4, 15, kEnemyZoneMap, Uniform(0.2f, 1.0f), Uniform(-0.3f, 0.3f), Uniform(1.0f, 3.0f)};
    c->hitShape = &g_hitShape;
    c->zoneLastHit = Range(0, 7);
    c->firing = uint8_t(Chance(70));
    c->scoreable = uint8_t(Chance(50));
    c->commandTarget = Around(at, 40.0f);
    c->hasCommandTarget = uint8_t(Chance(40));
    RandomMatrix(&c->thrownTransform, c->Position(), 2.0f);
    c->thrownVelocity = {Uniform(-10.0f, 10.0f), Uniform(-5.0f, 10.0f), Uniform(-10.0f, 10.0f)};
    c->thrown = uint8_t(Chance(30));
    c->bounces = uint8_t(Chance(30));
    c->avoidRadius = Uniform(1.0f, 20.0f);
    c->avoidCentre = Around(c->Position(), 15.0f);
    c->avoiding = uint8_t(Chance(70));

    memset(g_humanBlock, 0, sizeof(g_humanBlock));
    g_human->worldPos.valid = uint8_t(Chance(85));
    memset(g_voiceBlock, 0, sizeof(g_voiceBlock));
    *reinterpret_cast<void ***>(g_voiceBlock) = g_voiceVtable;
    memset(g_colliderBlock, 0, sizeof(g_colliderBlock));
    memset(g_targetableBlock, 0, sizeof(g_targetableBlock));
    g_targetable->ownerType = kTargetCharacter;

    // The partner and the humans DoInitial walks
    memset(g_otherBlock, 0, sizeof(g_otherBlock));
    g_other->vtable = g_otherVtable;
    g_other->actorLink = &g_otherLink;
    for (int i = 0; i < kOtherHumans; i++) {
        memset(g_otherHumanBlocks[i], 0, sizeof(g_otherHumanBlocks[i]));
        Human *h = reinterpret_cast<Human *>(g_otherHumanBlocks[i]);
        h->character = i == 0 ? NULL : i == 1 ? c : g_other;
        g_humanList[i] = h;
    }
    ShadowHumans.first = g_humanList;
    ShadowHumans.last = g_humanList + Range(0, kOtherHumans);

    // The mission's state and the statics
    glbMissionManager->autoDrive = uint8_t(Chance(60));
    glbMissionManager->missionState = Chance(70) ? Range(0, 2) : Range(3, 4);
    ShadowStepCount += Range(0, 3);
    int32_t last[3] = {Range(-1, 6), Range(-1, 3), Range(-1, 6)};
    OrigStaticLastHit = EnemyGroundLastHit = last[0];
    OrigStaticLastDeath = EnemyGroundLastDeath = last[1];
    OrigStaticLastScream = EnemyGroundLastScream = last[2];
    for (int i = 0; i < 2; i++) {
        ActIKSolveInfo info;
        memset(&info, 0, sizeof(info));
        info.target = {Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), 1.0f};
        info.pole = {0.866f, -0.5f, 0.0f, 1.0f};
        info.weight = 1.0f;
        OrigDriverHandIK[i] = info;
        DriverHandIK[i] = info;
    }
    memset(&g_out, 0, sizeof(g_out));
    memset(&g_built, 0, sizeof(g_built));
    PrepareScript(c->Position());
}

enum Kind { kDriver, kGround, kSnow, kSSnow, kSunroof, kWindow, kKinds };
const char *const kKindNames[kKinds] = {"Driver", "Ground", "Snow", "SSnow", "Sunroof", "Window"};

// An enemy of the kind round a live car, every field a case reads perturbed
void PrepareEnemy(Kind kind) {
    memset(g_charBlock, 0, sizeof(g_charBlock));
    PhysicsObject *live = RandomCar();
    PrepareCar(live);
    PrepareBase(g_char, BodyPosition(live));
    switch (kind) {
    case kDriver: {
        AICharacterEnemyDriver *e = reinterpret_cast<AICharacterEnemyDriver *>(g_char);
        e->vehicle = g_car;
        e->steerLeft = Chance(80) ? reinterpret_cast<ArticleEffect *>(&g_effects[0]) : NULL;
        e->steerRight = Chance(80) ? reinterpret_cast<ArticleEffect *>(&g_effects[1]) : NULL;
        e->idleAnimation = Chance(95) ? uint32_t(Range(0, 3)) : 0xffffffffu;
        break;
    }
    case kGround: {
        AICharacterEnemyGround *e = reinterpret_cast<AICharacterEnemyGround *>(g_char);
        e->targetable = Chance(70) ? g_targetable : NULL;
        e->unknown144 = 800;
        e->threatSide = Range(0, 1);
        e->threatAngle = Uniform(-0.5f, 0.5f);
        e->threatForward = {Uniform(-1.0f, 1.0f), 0.0f, Uniform(-1.0f, 1.0f)};
        e->unknown16c = Uniform(1.0f, 1.5f);
        e->targetAbove = Range(0, 1);
        e->initialBank = Range(3, 5);
        e->unknown17c = Chance(80) ? 0 : 1;
        e->claustrophobic = Chance(30);
        e->dodgeSide = Range(0, 1);
        e->lastDodgeSide = Range(0, 1);
        e->avoided = Chance(50);
        e->groundHeight = e->transform.mtx[3][1] + (Chance(30) ? Uniform(-4.0f, 4.0f) : Uniform(-0.5f, 0.5f));
        e->previousPosition = Around(e->Position(), 0.5f);
        e->worker = Chance(30);
        e->collider = g_collider;
        e->firstShot = Chance(50);
        break;
    }
    case kSnow: {
        AICharacterEnemySnow *e = reinterpret_cast<AICharacterEnemySnow *>(g_char);
        e->vehicle = g_car;
        e->leanDirection = Range(0, 1);
        e->aimPitchDir = Range(0, 1);
        e->aimTimer = Range(0, 60);
        e->aimTimerLimit = Range(0, 60);
        e->targetAngle = Uniform(-0.5f, 0.5f);
        e->aimAngles[0] = Chance(30) ? Near(Chance(50) ? 90.0f : -90.0f) : Uniform(-100.0f, 100.0f);
        e->aimAngles[1] = Chance(30) ? Near(Chance(50) ? 120.0f : -70.0f) : Uniform(-80.0f, 130.0f);
        for (int i = 0; i < 2; i++) {
            e->blendWeights[i] = Chance(40) ? (Chance(50) ? 0.0f : 1.0f) : Uniform(-0.1f, 1.1f);
            e->blendRates[i] = Chance(30) ? 0.0f : (Chance(50) ? 0.05f : -0.03f);
        }
        break;
    }
    case kSSnow: {
        AICharacterEnemySSnow *e = reinterpret_cast<AICharacterEnemySSnow *>(g_char);
        e->vehicle = g_car;
        e->weapon = Range(0, 1);
        break;
    }
    case kSunroof: {
        AICharacterEnemySunroof *e = reinterpret_cast<AICharacterEnemySunroof *>(g_char);
        e->vehicle = g_car;
        const Coord3 &velocity = ShadowGetRigidBody(ShadowSim, 0, g_car->rigidBodySlot)->velocity;
        float jolt = Chance(50) ? 60.0f : 5.0f;
        e->lastVelocity = {velocity.x + Uniform(-jolt, jolt), velocity.y, velocity.z + Uniform(-jolt, jolt)};
        e->leanDirection = Range(1, 4);
        e->partnerA = Chance(50) ? g_other : NULL;
        e->partnerB = Chance(30) ? g_other : NULL;
        e->targetable = Chance(80) ? g_targetable : NULL;
        break;
    }
    case kWindow: {
        AICharacterEnemyWindow *e = reinterpret_cast<AICharacterEnemyWindow *>(g_char);
        e->vehicle = g_car;
        e->aimYaw = Chance(30) ? Near(Chance(50) ? 80.0f : -60.0f) : Uniform(-70.0f, 90.0f);
        e->partnerA = Chance(50) ? g_other : NULL;
        e->partnerB = Chance(30) ? g_other : NULL;
        e->aimSide = Range(0, 1);
        e->armTimer = Range(-1, 3);
        break;
    }
    default:
        break;
    }
}

// ---- the methods

// A method taking and answering nothing, or answering a value, by its address and the port's
typedef int (*PortFn)(void *object);

template <class C, void (C::*M)()> int CallVoid(void *object) {
    (static_cast<C *>(object)->*M)();
    return 0;
}
template <class C, int (C::*M)()> int CallInt(void *object) {
    return (static_cast<C *>(object)->*M)();
}
template <class C, bool (C::*M)()> int CallBool(void *object) {
    return (static_cast<C *>(object)->*M)() ? 1 : 0;
}

enum Answer { kNone, kByte, kWord };

struct Method {
    Kind kind;
    const char *name;
    uint32_t original;
    PortFn port;
    Answer answer;
};

typedef AICharacterEnemy Enemy;
typedef AICharacterEnemyRider Rider;
typedef AICharacterEnemyDriver Driver;
typedef AICharacterEnemyGround Ground;
typedef AICharacterEnemySnow Snow;
typedef AICharacterEnemySSnow SSnow;
typedef AICharacterEnemySunroof Sunroof;
typedef AICharacterEnemyWindow EnemyWindow;

const Method kMethods[] = {
    {kGround, "AICharacterEnemy::TargetIsInRange", 0x0001ca50, &CallInt<Enemy, &Enemy::TargetIsInRangeBase>, kWord},
    {kGround, "AICharacterEnemy::NeedsToReload", 0x0001c5e0, &CallInt<Enemy, &Enemy::NeedsToReloadBase>, kWord},
    {kGround, "AICharacterEnemy::TargetIsAcquired", 0x0001ca90, &CallInt<Enemy, &Enemy::TargetIsAcquiredBase>, kWord},
    {kDriver, "AICharacterEnemyDriver::DoInitial", 0x0001d680, &CallVoid<Driver, &Driver::DoInitial>, kNone},
    {kDriver, "AICharacterEnemyDriver::DoNeutral", 0x0001d7f0, &CallVoid<Driver, &Driver::DoNeutral>, kNone},
    {kDriver, "AICharacterEnemyDriver::HandleInterrupts", 0x0001d840, &CallVoid<Driver, &Driver::HandleInterrupts>, kNone},
    {kDriver, "AICharacterEnemyRider::UpdateRotPos", 0x0001d990, &CallVoid<Rider, &Rider::UpdateRotPos>, kNone},
    {kGround, "AICharacterEnemyGround::DoDodging", 0x0001da60, &CallVoid<Ground, &Ground::DoDodging>, kNone},
    {kGround, "AICharacterEnemyGround::DoTilting", 0x0001da70, &CallVoid<Ground, &Ground::DoTilting>, kNone},
    {kGround, "AICharacterEnemyGround::IsAlive", 0x0001de80, &CallInt<Ground, &Ground::IsAlive>, kWord},
    {kGround, "AICharacterEnemyGround::DoInitial", 0x0001df70, &CallVoid<Ground, &Ground::DoInitial>, kNone},
    {kGround, "AICharacterEnemyGround::DoNeutral", 0x0001dfc0, &CallVoid<Ground, &Ground::DoNeutral>, kNone},
    {kGround, "AICharacterEnemyGround::DoAvoiding", 0x0001e010, &CallVoid<Ground, &Ground::DoAvoiding>, kNone},
    {kGround, "AICharacterEnemyGround::DoArming", 0x0001e070, &CallVoid<Ground, &Ground::DoArming>, kNone},
    {kGround, "AICharacterEnemyGround::DoArmed", 0x0001e090, &CallVoid<Ground, &Ground::DoArmed>, kNone},
    {kGround, "AICharacterEnemyGround::DoDying", 0x0001e380, &CallVoid<Ground, &Ground::DoDying>, kNone},
    {kGround, "AICharacterEnemyGround::UpdateRotPos", 0x0001e400, &CallVoid<Ground, &Ground::UpdateRotPos>, kNone},
    {kGround, "AICharacterEnemyGround::ThreatenedByVehicle", 0x0001e440, &CallBool<Ground, &Ground::ThreatenedByVehicle>, kByte},
    {kGround, "AICharacterEnemyGround::DoWandering", 0x0001ee60, &CallVoid<Ground, &Ground::DoWandering>, kNone},
    {kGround, "AICharacterEnemyGround::DoAiming", 0x0001f000, &CallVoid<Ground, &Ground::DoAiming>, kNone},
    {kGround, "AICharacterEnemyGround::DoFiring", 0x0001f0c0, &CallVoid<Ground, &Ground::DoFiring>, kNone},
    {kGround, "AICharacterEnemyGround::DoRunning", 0x0001f180, &CallVoid<Ground, &Ground::DoRunning>, kNone},
    {kGround, "AICharacterEnemyGround::HandleInterrupts", 0x0001f3c0, &CallVoid<Ground, &Ground::HandleInterrupts>, kNone},
    {kGround, "AICharacterEnemyGround::TargetIsAcquired", 0x0001f8f0, &CallInt<Ground, &Ground::TargetIsAcquired>, kWord},
    {kSnow, "AICharacterEnemySnow::DoTilting", 0x0001fcd0, &CallVoid<Snow, &Snow::DoTilting>, kNone},
    {kSnow, "AICharacterEnemySnow::HandleInterrupts", 0x0001fcf0, &CallVoid<Snow, &Snow::HandleInterrupts>, kNone},
    {kSnow, "AICharacterEnemySnow::DoNeutral", 0x0001fe00, &CallVoid<Snow, &Snow::DoNeutral>, kNone},
    {kSnow, "AICharacterEnemySnow::DoArmed", 0x0001ff90, &CallVoid<Snow, &Snow::DoArmed>, kNone},
    {kSnow, "AICharacterEnemySnow::DoAiming", 0x00020090, &CallVoid<Snow, &Snow::DoAiming>, kNone},
    {kSnow, "AICharacterEnemySnow::DoDying", 0x000203c0, &CallVoid<Snow, &Snow::DoDying>, kNone},
    {kSnow, "AICharacterEnemySnow::DoRunning", 0x000205b0, &CallVoid<Snow, &Snow::DoRunning>, kNone},
    {kSnow, "AICharacterEnemySnow::DoLeaning", 0x000205e0, &CallVoid<Snow, &Snow::DoLeaning>, kNone},
    {kSnow, "AICharacterEnemySnow::UpdateRotPos", 0x000206d0, &CallVoid<Snow, &Snow::UpdateRotPos>, kNone},
    {kSnow, "AICharacterEnemySnow::TargetIsInRange", 0x00020710, &CallBool<Snow, &Snow::TargetIsInRange>, kByte},
    {kSnow, "AICharacterEnemySnow::TargetIsAcquired", 0x000224f0, &CallInt<Snow, &Snow::TargetIsAcquired>, kWord},
    {kWindow, "AICharacterEnemySnow::TargetIsAcquired (window)", 0x000224f0, &CallInt<Snow, &Snow::TargetIsAcquired>, kWord},
    {kSSnow, "AICharacterEnemySSnow::DoNeutral", 0x00020a00, &CallVoid<SSnow, &SSnow::DoNeutral>, kNone},
    {kSSnow, "AICharacterEnemySSnow::DoAiming", 0x00020a60, &CallVoid<SSnow, &SSnow::DoAiming>, kNone},
    {kSSnow, "AICharacterEnemySSnow::DoDying", 0x00020b40, &CallVoid<SSnow, &SSnow::DoDying>, kNone},
    {kSSnow, "AICharacterEnemySSnow::HandleInterrupts", 0x00020bb0, &CallVoid<SSnow, &SSnow::HandleInterrupts>, kNone},
    {kSSnow, "AICharacterEnemySSnow::TargetIsInRange", 0x00020c30, &CallBool<SSnow, &SSnow::TargetIsInRange>, kByte},
    {kSSnow, "AICharacterEnemyRider::UpdateRotPos (turret)", 0x0001d990, &CallVoid<Rider, &Rider::UpdateRotPos>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoTilting", 0x00020f40, &CallVoid<Sunroof, &Sunroof::DoTilting>, kNone},
    {kSunroof, "AICharacterEnemySunroof::UpdateLeanDirection", 0x00020f90, &CallBool<Sunroof, &Sunroof::UpdateLeanDirection>, kByte},
    {kSunroof, "AICharacterEnemySunroof::DoInitial", 0x00021260, &CallVoid<Sunroof, &Sunroof::DoInitial>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoNeutral", 0x00021300, &CallVoid<Sunroof, &Sunroof::DoNeutral>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoArmed", 0x000213c0, &CallVoid<Sunroof, &Sunroof::DoArmed>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoAiming", 0x000215b0, &CallVoid<Sunroof, &Sunroof::DoAiming>, kNone},
    {kSunroof, "AICharacterEnemyWindow::DoFiring (sunroof)", 0x00021600, &CallVoid<EnemyWindow, &EnemyWindow::DoFiring>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoUnarming", 0x00021630, &CallVoid<Sunroof, &Sunroof::DoUnarming>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoDying", 0x00021670, &CallVoid<Sunroof, &Sunroof::DoDying>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoRunning", 0x00021710, &CallVoid<Sunroof, &Sunroof::DoRunning>, kNone},
    {kSunroof, "AICharacterEnemySunroof::DoLeaning", 0x00021740, &CallVoid<Sunroof, &Sunroof::DoLeaning>, kNone},
    {kSunroof, "AICharacterEnemySunroof::HandleInterrupts", 0x000217e0, &CallVoid<Sunroof, &Sunroof::HandleInterrupts>, kNone},
    {kSunroof, "AICharacterEnemySunroof::TargetIsInRange", 0x000218f0, &CallBool<Sunroof, &Sunroof::TargetIsInRange>, kByte},
    {kSunroof, "AICharacterEnemySunroof::TargetIsAcquired", 0x000219e0, &CallInt<Sunroof, &Sunroof::TargetIsAcquired>, kWord},
    {kWindow, "AICharacterEnemyWindow::DoFiring", 0x00021600, &CallVoid<EnemyWindow, &EnemyWindow::DoFiring>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoInitial", 0x00021e70, &CallVoid<EnemyWindow, &EnemyWindow::DoInitial>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoNeutral", 0x00021f10, &CallVoid<EnemyWindow, &EnemyWindow::DoNeutral>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoIdling", 0x00021f70, &CallVoid<EnemyWindow, &EnemyWindow::DoIdling>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoArming", 0x00022010, &CallVoid<EnemyWindow, &EnemyWindow::DoArming>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoArmed", 0x00022050, &CallVoid<EnemyWindow, &EnemyWindow::DoArmed>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoAiming", 0x00022150, &CallVoid<EnemyWindow, &EnemyWindow::DoAiming>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoUnarming", 0x00022280, &CallVoid<EnemyWindow, &EnemyWindow::DoUnarming>, kNone},
    {kWindow, "AICharacterEnemyWindow::DoDying", 0x000222c0, &CallVoid<EnemyWindow, &EnemyWindow::DoDying>, kNone},
    {kWindow, "AICharacterEnemyWindow::HandleInterrupts", 0x00022310, &CallVoid<EnemyWindow, &EnemyWindow::HandleInterrupts>, kNone},
    {kWindow, "AICharacterEnemyWindow::TargetIsInRange", 0x000223a0, &CallBool<EnemyWindow, &EnemyWindow::TargetIsInRange>, kByte},
    {kWindow, "AICharacterEnemyRider::UpdateRotPos (window)", 0x0001d990, &CallVoid<Rider, &Rider::UpdateRotPos>, kNone},
};

const Method *g_method;

void RunMethod() {
    const Method *m = g_method;
    int answer;
    if (g_original)
        answer = ((int (__fastcall *)(void *, int))m->original)(g_char, 0);
    else
        answer = m->port(g_char);
    g_out.i = m->answer == kWord ? answer : m->answer == kByte ? answer & 0xff : 0;
}

// ---- the cases

typedef double (__fastcall *ZoneScaleFn)(void *, int, int, float);
typedef void (__fastcall *InjuryFn)(void *, int, int, float, int);
typedef bool (__fastcall *AvoidFn)(void *, int, uint8_t *, uint8_t *);
typedef double (__fastcall *AngleFn)(void *, int, const Coord3 *, bool);
typedef void (*RotPosFn)(void *, MATRIX4 *);
typedef void (*RotPosVelocityFn)(void *, MATRIX4 *, Coord4 *);
typedef void (__fastcall *SetActorFn)(void *, int, AICharacterActorLink *);
typedef void (__fastcall *DestructFn)(void *, int);
typedef void *(__fastcall *DeleteFn)(void *, int, unsigned);

const uint32_t kInjuryEntries[kKinds] = {0, 0x0001ea60, 0x00020870, 0x00020c70, 0x00021b70, 0x00022680};
const uint32_t kDestructEntries[kKinds] = {0x0001d640, 0x0001dea0, 0x0001fc70, 0x00020970, 0x00020ec0, 0x00021d40};
const uint32_t kDeleteEntries[kKinds] = {0x0001d650, 0x0001df40, 0x0001fdd0, 0x000209d0, 0x00021230, 0x00021dc0};

Kind g_kind;

void CaseInjury() {
    if (g_original) {
        ((InjuryFn)kInjuryEntries[g_kind])(g_char, 0, g_in.i0, g_in.f0, g_in.i1);
        return;
    }
    switch (g_kind) {
    case kGround: reinterpret_cast<Ground *>(g_char)->PlayZoneInjuryAnim(g_in.i0, g_in.f0, g_in.i1); break;
    case kSnow: reinterpret_cast<Snow *>(g_char)->PlayZoneInjuryAnim(g_in.i0, g_in.f0, g_in.i1); break;
    case kSSnow: reinterpret_cast<SSnow *>(g_char)->PlayZoneInjuryAnim(g_in.i0, g_in.f0, g_in.i1); break;
    case kSunroof: reinterpret_cast<Sunroof *>(g_char)->PlayZoneInjuryAnim(g_in.i0, g_in.f0, g_in.i1); break;
    case kWindow: reinterpret_cast<EnemyWindow *>(g_char)->PlayZoneInjuryAnim(g_in.i0, g_in.f0, g_in.i1); break;
    default: break;
    }
}

void CaseDestruct() {
    if (g_original) {
        ((DestructFn)kDestructEntries[g_kind])(g_char, 0);
        return;
    }
    switch (g_kind) {
    case kDriver: reinterpret_cast<Driver *>(g_char)->Destruct(); break;
    case kGround: reinterpret_cast<Ground *>(g_char)->Destruct(); break;
    case kSnow: reinterpret_cast<Snow *>(g_char)->Destruct(); break;
    case kSSnow: reinterpret_cast<SSnow *>(g_char)->Destruct(); break;
    case kSunroof: reinterpret_cast<Sunroof *>(g_char)->Destruct(); break;
    case kWindow: reinterpret_cast<EnemyWindow *>(g_char)->Destruct(); break;
    default: break;
    }
}

void CaseDelete() {
    if (g_original) {
        g_out.p = ((DeleteFn)kDeleteEntries[g_kind])(g_char, 0, 0);
        return;
    }
    switch (g_kind) {
    case kDriver: g_out.p = reinterpret_cast<Driver *>(g_char)->Delete(0); break;
    case kGround: g_out.p = reinterpret_cast<Ground *>(g_char)->Delete(0); break;
    case kSnow: g_out.p = reinterpret_cast<Snow *>(g_char)->Delete(0); break;
    case kSSnow: g_out.p = reinterpret_cast<SSnow *>(g_char)->Delete(0); break;
    case kSunroof: g_out.p = reinterpret_cast<Sunroof *>(g_char)->Delete(0); break;
    case kWindow: g_out.p = reinterpret_cast<EnemyWindow *>(g_char)->Delete(0); break;
    default: break;
    }
}

// After a constructor: its allocation's contents and its hit shape's kept where they are compared, the allocation
// freed, and the pointers replaced by a mark
void SettleConstruction(WTargetable **targetable) {
    AICharacter *c = g_char;
    if (c->hitShape != NULL) {
        g_built.shape = *c->hitShape;
        memcpy(g_built.zoneMap, c->hitShape->zoneMap, sizeof(g_built.zoneMap));
        g_built.shape.zoneMap = NULL;
        c->hitShape = reinterpret_cast<AICharacterHitShape *>(1);
    }
    if (targetable != NULL && *targetable != NULL) {
        memcpy(g_built.targetable, *targetable, sizeof(g_built.targetable));
        UMemory::FastFree(*targetable, sizeof(WTargetable));
        *targetable = reinterpret_cast<WTargetable *>(1);
    }
}

void CaseConstruct() {
    MATRIX4 transform;
    RandomMatrix(&transform, g_in.point, 0.0f);
    void *c = g_char;
    switch (g_kind) {
    case kDriver:
        if (g_original)
            ((void *(__fastcall *)(void *, int, const MATRIX4 *, PBondCar *))0x0001d5f0)(c, 0, &transform, g_car);
        else
            reinterpret_cast<Driver *>(c)->Construct(&transform, g_car);
        SettleConstruction(NULL);
        break;
    case kGround: {
        if (g_original)
            ((void *(__fastcall *)(void *, int, const MATRIX4 *, int, int, int, bool))0x0001dc80)(
                c, 0, &transform, g_in.i0, g_in.i1, g_in.i1 ^ 1, g_in.i0 & 1);
        else
            reinterpret_cast<Ground *>(c)->Construct(&transform, g_in.i0, g_in.i1, g_in.i1 ^ 1, g_in.i0 & 1);
        Ground *e = reinterpret_cast<Ground *>(c);
        SettleConstruction(&e->targetable);
        if (e->collider != NULL) {
            UMemory::FastFree(e->collider, sizeof(WCollider));
            e->collider = reinterpret_cast<WCollider *>(1);
        }
        break;
    }
    case kSnow:
        if (g_original)
            ((void *(__fastcall *)(void *, int, const MATRIX4 *, int, PBondCar *))0x0001fd40)(c, 0, &transform,
                                                                                            g_in.i0, g_car);
        else
            reinterpret_cast<Snow *>(c)->Construct(&transform, g_in.i0, g_car);
        SettleConstruction(NULL);
        break;
    case kSSnow:
        if (g_original)
            ((void *(__fastcall *)(void *, int, const MATRIX4 *, int, PBondCar *))0x00020980)(c, 0, &transform,
                                                                                            g_in.i0, g_car);
        else
            reinterpret_cast<SSnow *>(c)->Construct(&transform, g_in.i0, g_car);
        SettleConstruction(NULL);
        break;
    case kSunroof:
        if (g_original)
            ((void *(__fastcall *)(void *, int, const MATRIX4 *, int, int, PBondCar *))0x000210f0)(
                c, 0, &transform, g_in.i0, g_in.i1, g_car);
        else
            reinterpret_cast<Sunroof *>(c)->Construct(&transform, g_in.i0, g_in.i1, g_car);
        SettleConstruction(&reinterpret_cast<Sunroof *>(c)->targetable);
        break;
    case kWindow:
        if (g_original)
            ((void *(__fastcall *)(void *, int, const MATRIX4 *, PBondCar *))0x00021d50)(c, 0, &transform, g_car);
        else
            reinterpret_cast<EnemyWindow *>(c)->Construct(&transform, g_car);
        SettleConstruction(NULL);
        break;
    default:
        break;
    }
}

void TestHelpers() {
    for (long i = 0; i < 300; i++) {
        g_in.f0 = Chance(5) ? nanf("") : Uniform(-10.0f, 10.0f);
        g_in.f1 = Chance(5) ? nanf("") : Chance(20) ? g_in.f0 : Uniform(-10.0f, 10.0f);
        RunCase("MinFloat", i, [] {
            g_out.f = g_original ? ((float (*)(float, float))0x0001c600)(g_in.f0, g_in.f1) : MinFloat(g_in.f0, g_in.f1);
        });
        RunCase("MaxFloat", i, [] {
            g_out.f = g_original ? ((float (*)(float, float))0x0001c620)(g_in.f0, g_in.f1) : MaxFloat(g_in.f0, g_in.f1);
        });
        RunCase("SimRandom::Fraction", i, [] {
            SimRandom *random = ShadowRandom;
            g_out.d = g_original ? ((double (__fastcall *)(void *, int))0x0001d9d0)(random, 0) : random->Fraction();
        });
        RunCase("WorldCollisionInfo::Construct", i, [] {
            alignas(16) WorldCollisionInfo info;
            memset(&info, 0xa5, sizeof(info));
            if (g_original)
                ((void *(__fastcall *)(void *, int))0x0001d9f0)(&info, 0);
            else
                info.Construct();
            // The segment ends are the original's stack garbage
            memset(&info.segmentStart, 0, 0x20);
            memcpy(&g_out.matrix, &info, sizeof(g_out.matrix));
            memcpy(&g_built, reinterpret_cast<uint8_t *>(&info) + 0x40, 0x20);
        });
    }
    if (CarCount() == 0)
        return;
    for (long i = 0; i < 100; i++) {
        PrepareEnemy(kSnow);
        Snapshot prepared;
        Capture(&prepared);
        RunCase("IsBumping", i, [] {
            g_out.i = g_original ? (((int (*)(PBondCar *))0x0001fc80)(g_car) & 0xff) : IsBumping(g_car);
        });
        g_in.i0 = Range(-1, 4);
        RunCase("VectorConstructorIterator", i, [] {
            typedef void (__thiscall *ConstructFn)(void *);
            ConstructFn construct = reinterpret_cast<ConstructFn>(&FakeColliderDestruct);
            if (g_original)
                ((void (__stdcall *)(void *, uint32_t, int, ConstructFn))0x00022770)(g_charBlock, 0x10, g_in.i0, construct);
            else
                VectorConstructorIterator(g_charBlock, 0x10, g_in.i0, construct);
        });
        Restore(prepared);
    }
}

void TestKind(Kind kind) {
    if (CarCount() == 0)
        return;
    g_kind = kind;
    char name[96];
    for (long i = 0; i < 150; i++) {
        PrepareEnemy(kind);
        Snapshot prepared;
        Capture(&prepared);
        for (const Method &m : kMethods) {
            if (m.kind != kind)
                continue;
            g_method = &m;
            RunCase(m.name, i, &RunMethod);
        }

        // The ride callbacks
        if (kind == kDriver || kind == kSunroof) {
            RunCase("AICharacterEnemyRider::GetRotPos (velocity)", i, [] {
                if (g_original)
                    ((RotPosVelocityFn)0x0001d530)(g_char, &g_out.matrix, &g_out.velocity);
                else
                    Rider::GetRotPos(reinterpret_cast<Rider *>(g_char), &g_out.matrix, &g_out.velocity);
            });
            RunCase("AICharacterEnemyRider::GetRotPos", i, [] {
                if (g_original)
                    ((RotPosFn)0x00020e30)(g_char, &g_out.matrix);
                else
                    Rider::GetRotPos(reinterpret_cast<Rider *>(g_char), &g_out.matrix);
            });
            RunCase("AICharacterEnemyRider::GetVehiclePtr", i, [] {
                g_out.p = g_original ? ((void *(__fastcall *)(void *, int))0x0001fd30)(g_char, 0)
                                     : reinterpret_cast<Rider *>(g_char)->GetVehiclePtr();
            });
        }
        if (kind == kSnow) {
            RunCase("AICharacterEnemySnow::GetRotPos (velocity)", i, [] {
                if (g_original)
                    ((RotPosVelocityFn)0x0001fb70)(g_char, &g_out.matrix, &g_out.velocity);
                else
                    Snow::GetRotPos(reinterpret_cast<Snow *>(g_char), &g_out.matrix, &g_out.velocity);
            });
            RunCase("AICharacterEnemySnow::GetRotPos", i, [] {
                if (g_original)
                    ((RotPosFn)0x0001fac0)(g_char, &g_out.matrix);
                else
                    Snow::GetRotPos(reinterpret_cast<Snow *>(g_char), &g_out.matrix);
            });
        }
        if (kind == kSnow || kind == kWindow) {
            RunCase("AICharacterEnemySnow::SetActor", i, [] {
                if (g_original)
                    ((SetActorFn)0x00021df0)(g_char, 0, &g_newLink);
                else
                    reinterpret_cast<Snow *>(g_char)->SetActor(&g_newLink);
            });
        }
        if (kind == kGround) {
            RunCase("AICharacterEnemyGround::AvoidThreat", i, [] {
                if (g_original)
                    g_out.i = ((AvoidFn)0x0001da80)(g_char, 0, &g_out.side, &g_out.high) ? 1 : 0;
                else
                    g_out.i = reinterpret_cast<Ground *>(g_char)->AvoidThreat(&g_out.side, &g_out.high);
            });
            g_in.i0 = Chance(80) ? Range(0, 2) : Range(3, 7);
            g_in.f0 = Uniform(0.0f, 100.0f);
            RunCase("AICharacterEnemyGround::GetZoneHitPointScale", i, [] {
                g_out.d = g_original ? ((ZoneScaleFn)0x0001dc50)(g_char, 0, g_in.i0, g_in.f0)
                                     : reinterpret_cast<Ground *>(g_char)->GetZoneHitPointScale(g_in.i0, g_in.f0);
            });
            g_in.point = Around(g_char->Position(), 30.0f);
            g_in.i1 = Range(0, 1);
            RunCase("AICharacterEnemyGround::GetAngleToTarget", i, [] {
                g_out.d = g_original ? ((AngleFn)0x0001e9a0)(g_char, 0, &g_in.point, g_in.i1 != 0)
                                     : reinterpret_cast<Ground *>(g_char)->GetAngleToTarget(&g_in.point, g_in.i1 != 0);
            });
        }
        if (kind != kDriver) {
            g_in.i0 = Range(0, 7);
            g_in.f0 = g_char->hitPoints != NULL && Chance(40) ? Near(*g_char->hitPoints) : Uniform(0.0f, 60.0f);
            g_in.i1 = Chance(30) ? 0 : Range(1, 4);
            snprintf(name, sizeof(name), "AICharacterEnemy%s::PlayZoneInjuryAnim", kKindNames[kind]);
            RunCase(name, i, &CaseInjury);
        }

        // Construction and destruction
        g_in.point = g_char->Position();
        g_in.i0 = Range(-1, 3);
        g_in.i1 = Range(0, 1);
        snprintf(name, sizeof(name), "AICharacterEnemy%s::Construct", kKindNames[kind]);
        RunCase(name, i, &CaseConstruct);
        // No collider to free (our block is not the heap's)
        if (kind == kGround)
            reinterpret_cast<Ground *>(g_char)->collider = NULL;
        snprintf(name, sizeof(name), "AICharacterEnemy%s::Destruct", kKindNames[kind]);
        RunCase(name, i, &CaseDestruct);
        snprintf(name, sizeof(name), "AICharacterEnemy%s::Delete", kKindNames[kind]);
        RunCase(name, i, &CaseDelete);
        Restore(prepared);
    }
}

void BuildRegions() {
    AddRegion(g_charBlock, sizeof(g_charBlock), "the enemy");
    AddRegion(g_otherBlock, sizeof(g_otherBlock), "the partner");
    AddRegion(g_actorBlock, sizeof(g_actorBlock), "the actor");
    AddRegion(g_otherActorBlock, sizeof(g_otherActorBlock), "the partner's actor");
    AddRegion(g_poserBlock, sizeof(g_poserBlock), "the poser");
    AddRegion(g_fadePoserBlock, sizeof(g_fadePoserBlock), "the cross-fade's poser");
    AddRegion(g_matricesBlock, sizeof(g_matricesBlock), "the actor's matrices");
    AddRegion(g_controllerBlock, sizeof(g_controllerBlock), "the controller");
    AddRegion(g_crossFadeBlock, sizeof(g_crossFadeBlock), "the cross-fade");
    AddRegion(g_characterBlock, sizeof(g_characterBlock), "the actor's character");
    AddRegion(g_humanBlock, sizeof(g_humanBlock), "the human");
    AddRegion(g_otherHumanBlocks, sizeof(g_otherHumanBlocks), "the other humans");
    AddRegion(g_humanList, sizeof(g_humanList), "the humans' list");
    AddRegion(g_carBlock, sizeof(g_carBlock), "the car");
    AddRegion(g_carAIBlock, sizeof(g_carAIBlock), "the car's AI");
    AddRegion(g_targetVehicleBlock, sizeof(g_targetVehicleBlock), "the car AI's target");
    AddRegion(g_colliderBlock, sizeof(g_colliderBlock), "the collider");
    AddRegion(g_voiceBlock, sizeof(g_voiceBlock), "the voice");
    AddRegion(g_targetableBlock, sizeof(g_targetableBlock), "the target");
    AddRegion(g_sceneObjBlock, sizeof(g_sceneObjBlock), "the car's scene object");
    AddRegion(&g_link, sizeof(g_link), "the actor link");
    AddRegion(&g_otherLink, sizeof(g_otherLink), "the partner's actor link");
    AddRegion(&g_newLink, sizeof(g_newLink), "the new actor link");
    AddRegion(&g_hitShape, sizeof(g_hitShape), "the hit shape");
    AddRegion(&g_hitPoints, sizeof(g_hitPoints), "the hit points");
    AddRegion(&g_carHitPoints, sizeof(g_carHitPoints), "the car's hit points");
    AddRegion(ShadowRandom, sizeof(SimRandom), "the Simulation's random numbers");
    AddRegion(&glbMissionManager->missionState, 8, "the mission manager");
    AddRegion(&ShadowHumans, sizeof(ShadowHumans), "the Simulation's humans");
    AddRegion(&g_out, sizeof(g_out), "the answers");
    AddRegion(&g_built, sizeof(g_built), "the construction");
    AddRegion(&g_statics, sizeof(g_statics), "the statics");
    AddRegion(&g_in, sizeof(g_in), "the inputs", false);
    AddRegion(&ShadowStepCount, 4, "SimStepCount", false);
    AddRegion(&OrigStaticLastHit, 4, "the original's statics", false);
    AddRegion(&OrigStaticLastDeath, 8, "the original's statics", false);
    AddRegion(OrigDriverHandIK, 0x60, "the original's driver IK", false);
    AddRegion(&EnemyGroundLastHit, 4, "the port's statics", false);
    AddRegion(&EnemyGroundLastDeath, 4, "the port's statics", false);
    AddRegion(&EnemyGroundLastScream, 4, "the port's statics", false);
    AddRegion(DriverHandIK, sizeof(ActIKSolveInfo) * 2, "the port's driver IK", false);
}

bool InstallHooks() {
    int missing = 0;
    for (int i = 0; i < kHookCount; i++)
        if (!HookInstall(&g_hooks[i], kHookEntries[i], kHookTargets[i])) {
            printf("[gameaishadowe] the fake for 0x%08x could not be installed\n", kHookEntries[i]);
            missing++;
        }
    return missing == 0;
}

}  // namespace

void GameAIShadowE_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEAISHADOWE", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);
    printf("[gameaishadowe] live at the first tick: %d cars, %d humans\n", CarCount(),
           int(ShadowHumans.last - ShadowHumans.first));
    if (glbMissionManager == NULL || fgRenderHigh == NULL || fgRenderHigh->views[0].camera == NULL) {
        printf("[gameaishadowe] no mission manager or camera - nothing run\n");
        fflush(stdout);
        return;
    }
    BuildVtables();
    if (!InstallHooks()) {
        for (Hook &hook : g_hooks)
            HookRemove(&hook);
        printf("[gameaishadowe] nothing run\n");
        fflush(stdout);
        return;
    }
    BuildRegions();
    Snapshot before;
    Capture(&before);

    TestHelpers();
    if (CarCount() == 0) {
        g_skipped++;
        printf("[gameaishadowe]   no cars: the enemies not run\n");
    }
    for (int kind = 0; kind < kKinds; kind++) {
        TestKind(Kind(kind));
        Restore(before);
    }

    Restore(before);
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
    ResetFpu();
    printf("[gameaishadowe] enemy characters vs originals: %ld cases, %ld checks, %ld differ%s\n", g_cases, g_checks,
           g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameaishadowe]   %ld calls faulted\n", g_faults);
    fflush(stdout);
}
