#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameWeaponsShadowD.h"
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

#include "../anim/Actor.h"
#include "../anim/AnimEngine.h"
#include "../anim/Character.h"
#include "../engine/UMemory.hpp"
#include "../game/AICharacter.h"
#include "../game/Human.h"
#include "../game/Mine.h"
#include "../game/Sentry.h"
#include "../game/Smackable.h"
#include "../game/WeaponSound.h"
#include "../physics/PhysicsNamespace.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../game/Simulation.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionInstance.h"
#include "../world/Targeting.h"
#include "../world/World.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.weapons' package D, run once from the first simulation tick when NIGHTFIRE_GAMEWEAPONSSHADOWD
// is set. Each case runs the ORIGINAL (every entry of the package swapped back together) and then the PORT from the
// same state, and compares what they leave - the object, the synthetic objects round it, the simulation body it
// uses, the muzzle flashes (ours against the game's copies) - the answers, and a log of every call out:
//
//   - Sentry::ComputeNewRotation and ComputeTargetAngle on random and edge inputs;
//   - Sentry::AddMuzzleFlash and DrawMuzzleFlashes over random fills and sizes (about the 0.1 limit);
//   - Sentry::RotateSRB, RotateAnim, Shoot, TrackTarget, GetTarget, ApplyDamage and Simulate on a synthetic sentry
//     (its settings copied from a live one where the mission has one), every axis, animation state, frame count,
//     shot count, hold countdown and hit point level, the target the player's object (or a dead signature) placed
//     in and out of range by the faked aim instance;
//   - Mine::ApplyDamage, CheckTimer, CheckForProximity and Simulate on synthetic mines of each kind, placed by live
//     rigid bodies (so the proximity test meets them, owned or not) and round the arming step;
//   - Smackable::ApplyDamage, Simulate and GoToSleep on a synthetic smackable (with and without a trigger, moving or
//     not, asleep or awake, its sound and animations active or not);
//   - Human::Activate, Deactivate, Simulate, SetAICharacter and ApplyDamage on a synthetic human and AI character
//     (every kind about the 5 and 20 limits, visible or not, dead or not, hits handed to the character or not).
//
// The synthetic objects sit on a free slot of the Simulation's simple and rigid tables; their render object, sound
// and AI character have vtables of fakes. Fakes (5-byte jumps over the code both sides reach): the RAnimEngine
// handle's queries and stimuli, RSceneObj::GetInstancePosition, RMuzzleFlash::Draw, ActWeapon::SetOwner,
// PhysicsObject::ApplyForces, WCollisionMgr::CheckHitWorld, WTargetable::Construct and RemoveReference,
// WTargetPicker::RegisterTarget and UnregisterTarget, WTrigger::UpdateRotPos, and the originals Simulation::SpawnExplosion and
// DeletePhysicsObject, SMissionManager::IncShotsHit and IncKills, AICharacter::Execute.
//
// Not here (lockstep runs reach them where a mission makes the objects): the four constructors, destructors and
// scalar deleting destructors.
//
// A mutation the test sees: ComputeNewRotation answering 1 - limit where it answers limit (the rotation cases'
// answers differ); Smackable::ApplyDamage testing the rounded hit against the first impact level (the stimulus
// log differs where the product rounds onto 0.75).
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

const unsigned kEntries[] = {
    0x0005dc60, 0x0005dcc0, 0x0005dce0, 0x0005dd00, 0x0005dd90, 0x0005dea0, 0x0005ded0, 0x0005dfa0, 0x0005e180,
    0x0005e200, 0x0005e3e0, 0x0005e400, 0x0005e480, 0x0005e6e0, 0x0005e710, 0x00073150, 0x000731e0, 0x00073330,
    0x000733f0, 0x000734a0, 0x00073500, 0x000735d0, 0x00073600, 0x00073730, 0x000738b0, 0x00073a90, 0x00073f80,
    0x00074280, 0x000744d0, 0x00075300, 0x00075370, 0x00075420, 0x00075530, 0x00075560, 0x00075920,
};

typedef int (__fastcall *ApplyDamageFn)(void *, int, const Coord3 *, const Coord3 *, float, float, int,
                                        const uint32_t *);
typedef void (__fastcall *MethodFn)(void *, int);

#define Orig_HumanActivate ((MethodFn)0x0005dcc0)
#define Orig_HumanDeactivate ((MethodFn)0x0005dce0)
#define Orig_HumanSimulate ((MethodFn)0x0005dd00)
#define Orig_HumanSetAICharacter ((void (__fastcall *)(void *, int, AICharacter *))0x0005ded0)
#define Orig_HumanApplyDamage ((ApplyDamageFn)0x0005dfa0)
#define Orig_MineCheckForProximity ((MethodFn)0x0005e200)
#define Orig_MineApplyDamage ((ApplyDamageFn)0x0005e3e0)
#define Orig_MineCheckTimer ((MethodFn)0x0005e400)
#define Orig_MineSimulate ((MethodFn)0x0005e710)
#define Orig_SentryComputeTargetAngle ((double (__fastcall *)(void *, int, const Coord4 *, const Coord4 *, const Coord4 *, int))0x000731e0)
#define Orig_SentryGetTarget ((WTargetable *(__fastcall *)(void *, int))0x00073330)
#define Orig_SentryShoot ((MethodFn)0x000733f0)
#define Orig_SentryAddMuzzleFlash ((void (*)(const Coord4 *, const Coord3 *))0x000734a0)
#define Orig_SentryDrawMuzzleFlashes ((void (*)(void))0x00073500)
#define Orig_SentryComputeNewRotation ((double (__fastcall *)(void *, int, float, float, float, float))0x00073600)
#define Orig_SentryRotateSRB ((bool (__fastcall *)(void *, int, float))0x00073730)
#define Orig_SentryRotateAnim ((bool (__fastcall *)(void *, int, float))0x000738b0)
#define Orig_SentryApplyDamage ((ApplyDamageFn)0x00073f80)
#define Orig_SentryTrackTarget ((bool (__fastcall *)(void *, int, bool *))0x00074280)
#define Orig_SentrySimulate ((MethodFn)0x000744d0)
#define Orig_SmackableGoToSleep ((MethodFn)0x00075370)
#define Orig_SmackableSimulate ((MethodFn)0x00075420)
#define Orig_SmackableApplyDamage ((ApplyDamageFn)0x00075560)

// The callees faked: ours (the entry jumps to our code) or originals (the entry itself)
enum HookIndex {
    kHookSystemState, kHookSystemPlaying, kHookSetFrameRate, kHookNumFrames, kHookSetFrame, kHookStimuliId,
    kHookStimuli, kHookStimuliZones, kHookAnyPlaying, kHookInstancePosition, kHookFlashDraw, kHookSetOwner,
    kHookApplyForces, kHookCheckHitWorld, kHookTargetConstruct, kHookRemoveReference, kHookRegister,
    kHookUnregister, kHookUpdateRotPos, kHookSpawnExplosion, kHookDelete, kHookShotsHit, kHookKills, kHookExecute, kHookCount
};

const uint32_t kHookEntries[kHookCount] = {
    0x00077500, 0x000774a0, 0x00077cb0, 0x00077d10, 0x00077c50, 0x00077d70, 0x00077e00, 0x00077e60, 0x000768c0,
    0x0008dd20, 0x000a1870, 0x0001ad70, 0x0006f2a0, 0x000c3b40, 0x000cdba0, 0x000cdb80, 0x000ced50, 0x000ceb60,
    0x000cf490, 0x000b4cd0, 0x000b4250, 0x000b6720, 0x000b6770, 0x0001c2b0,
};

// ---- the game's state

#define ShadowSimStepCount I32_AT(0x00234e34)
#define ShadowSimpleOwners ((PhysicsObject **)0x002343a0)
#define ShadowRigidOwners ((PhysicsObject **)0x002342a0)

const uint32_t kHumanVtableAddress = 0x0018edf4;
const uint32_t kMineVtableAddress = 0x0018ee28;
const uint32_t kSentryVtableAddress = 0x00190034;
const uint32_t kSmackableVtableAddress = 0x00190248;

// ---- results

bool g_original;
long g_cases, g_checks, g_differ, g_faults, g_skipped;
int g_reported;
unsigned g_x87, g_sse;

void Report(const char *what, long index, const char *detail) {
    g_differ++;
    if (g_reported++ < 10)
        printf("[gameweaponsshadowd]   %s #%ld: %s\n", what, index, detail);
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

uint32_t g_seed = 0x5e7a1d0d;

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

Coord4 RandomVector(float scale, float w) {
    Coord4 v = { Uniform(-scale, scale), Uniform(-scale, scale), Uniform(-scale, scale), w };
    return v;
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
    uint8_t *game;
    uint8_t *ours;          // NULL: both sides use the game's address
    uint32_t size;
    bool compare;
    const char *name;
};

std::vector<Region> g_regions;

void AddShared(const void *at, uint32_t size, const char *name, bool compare = true) {
    if (at != NULL)
        g_regions.push_back({ (uint8_t *)at, NULL, size, compare, name });
}

void AddOwned(void *ours, uint32_t game, uint32_t size, const char *name) {
    g_regions.push_back({ reinterpret_cast<uint8_t *>(uintptr_t(game)), static_cast<uint8_t *>(ours), size, true,
                          name });
}

struct Snapshot {
    std::vector<uint8_t> bytes;
};

void Capture(Snapshot *s) {
    s->bytes.clear();
    for (const Region &r : g_regions) {
        s->bytes.insert(s->bytes.end(), r.game, r.game + r.size);
        if (r.ours != NULL)
            s->bytes.insert(s->bytes.end(), r.ours, r.ours + r.size);
    }
}

void Restore(const Snapshot &s) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        memcpy(r.game, &s.bytes[at], r.size);
        at += r.size;
        if (r.ours != NULL) {
            memcpy(r.ours, &s.bytes[at], r.size);
            at += r.size;
        }
    }
}

// Our globals into the game's copies, so an original starts where the port does
void Lend() {
    for (const Region &r : g_regions)
        if (r.ours != NULL)
            memcpy(r.game, r.ours, r.size);
}

void CompareResults(const char *what, long index, const Snapshot &original, const Snapshot &port) {
    size_t at = 0;
    for (const Region &r : g_regions) {
        const uint8_t *a = &original.bytes[at];
        const uint8_t *b = r.ours != NULL ? &port.bytes[at + r.size] : &port.bytes[at];
        at += r.ours != NULL ? 2 * r.size : r.size;
        if (!r.compare)
            continue;
        char label[96];
        snprintf(label, sizeof(label), "%s: %s", what, r.name);
        CompareBytes(label, index, a, b, r.size);
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

void LogDouble(double value) {
    uint64_t bits;
    memcpy(&bits, &value, 8);
    Log(uint32_t(bits));
    Log(uint32_t(bits >> 32));
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

void LogXyz(const void *v) {
    LogBytes(v, 12);
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
    char detail[128];
    snprintf(detail, sizeof(detail), "call log word %u of %u/%u: original %08x, port %08x", unsigned(at),
             unsigned(a.size()), unsigned(b.size()), at < a.size() ? a[at] : 0, at < b.size() ? b[at] : 0);
    Report(what, index, detail);
}

// ---- the synthetic world

alignas(16) uint8_t g_objectBlock[0x100];       // the object under test
alignas(16) uint8_t g_renderBlock[sizeof(RAutonomousObj)];
alignas(16) uint8_t g_handleBlock[0x80];
alignas(16) uint8_t g_soundBlock[0x160];
alignas(16) uint8_t g_aiBlock[sizeof(AICharacter)];
alignas(16) uint8_t g_actorLinkBlock[sizeof(AICharacterActorLink)];
alignas(16) uint8_t g_actorBlock[sizeof(ActActor)];
alignas(16) uint8_t g_characterBlock[sizeof(ActCharacter)];
alignas(16) uint8_t g_weaponBlocks[2][0x20];
alignas(16) uint8_t g_hitReachBlock[sizeof(AICharacterHitShape)];
alignas(16) uint8_t g_triggerBlock[sizeof(WTrigger)];
alignas(16) uint8_t g_dataBlock[sizeof(PhysicsData)];
alignas(16) uint8_t g_procAnimBlock[sizeof(ProcAnimState)];
alignas(16) uint8_t g_targetableBlock[sizeof(WTargetable)];
alignas(16) uint8_t g_infoBlock[sizeof(RigidBodyInfo)];
float g_aiHitPoints;

Sentry *const g_sentry = reinterpret_cast<Sentry *>(g_objectBlock);
Mine *const g_mine = reinterpret_cast<Mine *>(g_objectBlock);
Smackable *const g_smackable = reinterpret_cast<Smackable *>(g_objectBlock);
Human *const g_human = reinterpret_cast<Human *>(g_objectBlock);
RSceneObj *const g_render = reinterpret_cast<RSceneObj *>(g_renderBlock);
ASceneObj *const g_sound = reinterpret_cast<ASceneObj *>(g_soundBlock);
AICharacter *const g_ai = reinterpret_cast<AICharacter *>(g_aiBlock);
ActActor *const g_actor = reinterpret_cast<ActActor *>(g_actorBlock);
ActCharacter *const g_character = reinterpret_cast<ActCharacter *>(g_characterBlock);
WTrigger *const g_trigger = reinterpret_cast<WTrigger *>(g_triggerBlock);
PhysicsData *const g_data = reinterpret_cast<PhysicsData *>(g_dataBlock);
ProcAnimState *const g_procAnim = reinterpret_cast<ProcAnimState *>(g_procAnimBlock);
WTargetable *const g_targetable = reinterpret_cast<WTargetable *>(g_targetableBlock);

int g_simpleSlot = -1, g_rigidSlot = -1;
SimpleRigidBody *g_simpleBody;
RigidBody *g_rigidBody;

void *g_renderVtable[19];
void *g_soundVtable[8];
void *g_aiVtable[8];

// One case's inputs and scripted answers
struct Inputs {
    uint32_t aimState, fireState;   // GetSystemState's answers for the aim system and the others
    bool systemPlaying, anyPlaying, soundActive, placed;
    uint32_t numFrames;
    int hitWorld;
    double zoneScale;
    alignas(16) MATRIX4 aim;        // GetInstancePosition's
    float angle, current, target, rate, limit;
    Coord4 axis, direction, side;
    int which;
    Coord3 from, to;
    float amount, split;
    int kind;
    uint32_t sourceSig;
} g_in;

// The answers (compared)
struct Outputs {
    double rotation;
    int result;
    bool flag, inRange;
    uint8_t pad[2];
} g_out;

// ---- the fakes

enum : uint32_t {
    kTagState = 0x53545445, kTagPlaying = 0x504c4159, kTagFrameRate = 0x46524154, kTagNumFrames = 0x4e46524d,
    kTagSetFrame = 0x53465241, kTagStimuliId = 0x53544931, kTagStimuli = 0x53544932, kTagZones = 0x53544933,
    kTagAny = 0x414e5950, kTagInstance = 0x494e5354, kTagFlash = 0x464c5348, kTagOwner = 0x4f574e52,
    kTagForces = 0x464f5243, kTagHitWorld = 0x48495457, kTagTarget = 0x54524754, kTagRemove = 0x52454d56,
    kTagRegister = 0x52454752, kTagUnregister = 0x554e5247, kTagExplosion = 0x45585030, kTagDelete = 0x44454c30,
    kTagShotsHit = 0x53484f54, kTagKills = 0x4b494c4c, kTagExecute = 0x45584543, kTagUpdate = 0x55504454,
    kTagFX = 0x54524658, kTagCollision = 0x434f4c4c, kTagActive = 0x41435456, kTagAIDelete = 0x41494430,
    kTagNotify = 0x4e4f5459, kTagScale = 0x5343414c, kTagInjury = 0x494e4a55, kTagRotPos = 0x524f5450,
};

uint32_t __fastcall FakeSystemState(Handle *handle, int, uint32_t id) {
    Log(kTagState);
    LogPointer(handle);
    Log(id);
    return id == g_sentry->aimSystem ? g_in.aimState : g_in.fireState;
}

bool __fastcall FakeSystemPlaying(Handle *handle, int, uint32_t id) {
    Log(kTagPlaying);
    LogPointer(handle);
    Log(id);
    return g_in.systemPlaying;
}

void __fastcall FakeSetFrameRate(Handle *, int, uint32_t id, uint32_t stimulus, uint32_t rate) {
    Log(kTagFrameRate);
    Log(id);
    Log(stimulus & 0xff);
    Log(rate & 0xff);
}

uint32_t __fastcall FakeNumFrames(Handle *, int, uint32_t id, uint32_t stimulus) {
    Log(kTagNumFrames);
    Log(id);
    Log(stimulus & 0xff);
    return g_in.numFrames;
}

void __fastcall FakeSetFrame(Handle *, int, uint32_t id, uint32_t stimulus, uint32_t time) {
    Log(kTagSetFrame);
    Log(id);
    Log(stimulus & 0xff);
    Log(time);
}

void __fastcall FakeStimuliId(Handle *, int, uint32_t id, uint32_t stimulus, uint32_t tick, int mode) {
    Log(kTagStimuliId);
    Log(id);
    Log(stimulus & 0xff);
    Log(tick);
    Log(uint32_t(mode));
}

void __fastcall FakeStimuli(Handle *, int, uint32_t stimulus, uint32_t tick, int mode) {
    Log(kTagStimuli);
    Log(stimulus & 0xff);
    Log(tick);
    Log(uint32_t(mode));
}

void __fastcall FakeStimuliZones(Handle *, int, uint32_t stimulus, uint32_t zones, uint32_t tick, int mode) {
    Log(kTagZones);
    Log(stimulus & 0xff);
    Log(zones & 0xffff);
    Log(tick);
    Log(uint32_t(mode));
}

bool __fastcall FakeAnyPlaying(Handle *handle, int) {
    Log(kTagAny);
    LogPointer(handle);
    return g_in.anyPlaying;
}

uint32_t __fastcall FakeInstancePosition(RSceneObj *render, int, uint32_t index, MATRIX4 *transform, bool world) {
    Log(kTagInstance);
    LogPointer(render);
    Log(index);
    Log(world ? 1 : 0);
    *transform = g_in.aim;
    return 0;
}

void FakeFlashDraw(const MATRIX4 *transform, const Coord4 *position, const Coord4 *direction, int type,
                   float size) {
    Log(kTagFlash);
    LogBytes(transform, sizeof(MATRIX4));
    LogBytes(position, sizeof(Coord4));
    LogXyz(direction);          // the original's w is uninitialised
    Log(uint32_t(type));
    LogFloat(size);
}

void __fastcall FakeSetOwner(void *weapon, int, PhysicsObject *owner) {
    Log(kTagOwner);
    LogPointer(weapon);
    LogPointer(owner);
}

void __fastcall FakeApplyForces(PhysicsObject *object, int, const Coord3 *force, const Coord3 *torque) {
    Log(kTagForces);
    LogPointer(object);
    LogXyz(force);
    LogXyz(torque);
}

int __fastcall FakeCheckHitWorld(void *manager, int, const Coord4 *segment, void *) {
    Log(kTagHitWorld);
    LogPointer(manager);
    LogBytes(segment, 2 * sizeof(Coord4));
    return g_in.hitWorld;
}

WTargetable *__fastcall FakeTargetConstruct(WTargetable *target, int, PhysicsObject *physics) {
    Log(kTagTarget);
    LogPointer(physics);
    memset(target, 0, sizeof(WTargetable));
    target->ownerType = kTargetPhysicsObject;
    target->physics = physics;
    return target;
}

void __fastcall FakeRemoveReference(WTargetable *target, int) {
    Log(kTagRemove);
    LogPointer(target);
}

void __fastcall FakeRegister(WTargetPicker *, int, WTargetable *target) {
    Log(kTagRegister);
    // a new target (each side's own block) by value
    if (target != NULL && target != g_targetable)
        LogBytes(target, sizeof(WTargetable));
    else
        LogPointer(target);
}

void __fastcall FakeUnregister(WTargetPicker *, int, WTargetable *target) {
    Log(kTagUnregister);
    LogPointer(target);
}

// (the real one files the trigger in the world's grid by its index in the trigger manager's table)
bool __fastcall FakeUpdateRotPos(WTrigger *trigger, int, const Coord4 *rotation, const Coord3 *centre) {
    Log(kTagRotPos);
    LogPointer(trigger);
    LogBytes(rotation, sizeof(Coord4));
    LogXyz(centre);
    return g_in.placed;
}

void __fastcall FakeSpawnExplosion(void *, int, int type, int weapon, const Coord3 *direction,
                                   const Coord3 *position, float damage, int unknown6, PhysicsObject *owner,
                                   const uint32_t *sourceSig) {
    Log(kTagExplosion);
    Log(uint32_t(type));
    Log(uint32_t(weapon));
    LogXyz(direction);
    LogPointer(position);
    LogXyz(position);
    LogFloat(damage);
    Log(uint32_t(unknown6));
    LogPointer(owner);
    LogPointer(sourceSig);
}

void __fastcall FakeDelete(void *, int, PhysicsObject *object) {
    Log(kTagDelete);
    LogPointer(object);
}

void __fastcall FakeShotsHit(void *, int, uint32_t hit) {
    Log(kTagShotsHit);
    Log(hit & 0xff);
}

void __fastcall FakeKills(void *, int, int kills) {
    Log(kTagKills);
    Log(uint32_t(kills));
}

void __fastcall FakeExecute(void *ai, int) {
    Log(kTagExecute);
    LogPointer(ai);
}

// The synthetic render object's vtable
void __fastcall FakeUpdatePosition(RSceneObj *render, int, int update) {
    Log(kTagUpdate);
    LogPointer(render);
    Log(uint32_t(update));
}

void __fastcall FakeTriggerFX(RSceneObj *render, int, int type, uint32_t which, uint32_t unused3,
                              const Coord3 *position, const Coord3 *direction, float intensity) {
    Log(kTagFX);
    LogPointer(render);
    Log(uint32_t(type));
    Log(which);
    Log(unused3);
    LogXyz(position);
    LogXyz(direction);
    LogFloat(intensity);
}

// The synthetic sound's
void __fastcall FakePlayCollision(ABaseSound *sound, int, float impact, const Coord3 *position,
                                  const Coord3 *velocity, int surface) {
    Log(kTagCollision);
    LogPointer(sound);
    LogFloat(impact);
    LogPointer(position);
    LogXyz(position);
    LogXyz(velocity);
    Log(uint32_t(surface));
}

bool __fastcall FakeIsStillActive(ABaseSound *sound, int) {
    Log(kTagActive);
    LogPointer(sound);
    return g_in.soundActive;
}

// The synthetic AI character's
void *__fastcall FakeAIDelete(void *ai, int, unsigned flags) {
    Log(kTagAIDelete);
    LogPointer(ai);
    Log(flags);
    return ai;
}

void __fastcall FakeNotify(void *, int, const Coord3 *point) {
    Log(kTagNotify);
    LogXyz(point);
}

double __fastcall FakeZoneScale(void *, int, int zone, float amount) {
    Log(kTagScale);
    Log(uint32_t(zone));
    LogFloat(amount);
    return g_in.zoneScale;
}

void __fastcall FakeInjury(void *, int, int zone, float amount, int kind) {
    Log(kTagInjury);
    Log(uint32_t(zone));
    LogFloat(amount);
    Log(uint32_t(kind));
}

const void *const kHookTargets[kHookCount] = {
    (const void *)&FakeSystemState, (const void *)&FakeSystemPlaying, (const void *)&FakeSetFrameRate,
    (const void *)&FakeNumFrames, (const void *)&FakeSetFrame, (const void *)&FakeStimuliId,
    (const void *)&FakeStimuli, (const void *)&FakeStimuliZones, (const void *)&FakeAnyPlaying,
    (const void *)&FakeInstancePosition, (const void *)&FakeFlashDraw, (const void *)&FakeSetOwner,
    (const void *)&FakeApplyForces, (const void *)&FakeCheckHitWorld, (const void *)&FakeTargetConstruct,
    (const void *)&FakeRemoveReference, (const void *)&FakeRegister, (const void *)&FakeUnregister,
    (const void *)&FakeUpdateRotPos, (const void *)&FakeSpawnExplosion, (const void *)&FakeDelete, (const void *)&FakeShotsHit,
    (const void *)&FakeKills, (const void *)&FakeExecute,
};

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
    // Too short to take a jump: a return and then the padding before the next function (a 0xc3 byte inside an
    // address or a jump's offset is not one)
    for (int i = 0; i < 5; i++)
        if (code[0] == 0xcc || (code[i] == 0xc3 && (code[i + 1] == 0xcc || code[i + 1] == 0x90)) ||
            (code[i] == 0xc2 && (code[i + 3] == 0xcc || code[i + 3] == 0x90)))
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

bool AllHooked() {
    for (const Hook &hook : g_hooks)
        if (!hook.on)
            return false;
    return true;
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

// The inputs are in place (ours): both sides from them, the results compared, everything put back
void RunCase(const char *what, long index, SideFn run, const Snapshot &before) {
    Lend();
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

// A target the side allocated (GetTarget's, Simulate's): logged by value, freed, its pointer replaced by a marker
const uint32_t kAllocatedMarker = 0x7a7a7a7a;

WTargetable *SettleTarget(WTargetable *target) {
    if (target == NULL || target == g_targetable)
        return target;
    LogBytes(target, sizeof(WTargetable));
    UMemory::FastFree(target, sizeof(WTargetable));
    return reinterpret_cast<WTargetable *>(uintptr_t(kAllocatedMarker));
}

// ---- building the synthetic objects

uint32_t PlayerSig() {
    PhysicsObject *player = fgSimulation.GetPlayerObject();
    if (player == NULL)
        return 0;
    uint32_t sig;
    return *player->GetSig(&sig);
}

void BuildRender() {
    memset(g_renderBlock, 0, sizeof(g_renderBlock));
    memset(g_handleBlock, 0, sizeof(g_handleBlock));
    g_render->vtable = reinterpret_cast<RSceneObj_vtbl *>(g_renderVtable);
    g_render->animHandle = reinterpret_cast<Handle *>(g_handleBlock);
    g_render->flags = uint8_t(Next());
}

void BuildSound() {
    memset(g_soundBlock, 0, sizeof(g_soundBlock));
    g_sound->vtable = uint32_t(uintptr_t(g_soundVtable));
    for (int i = 0; i < 6; i++)
        g_soundBlock[0xc0 + i] = uint8_t(Range(0, 2));
}

// The base fields: the body's slot, the render object and sound, the hit points
void BuildBase(PhysicsObject *object, uint32_t vtable, bool simple, float *hitPoints) {
    memset(g_objectBlock, 0, sizeof(g_objectBlock));
    object->vtable = reinterpret_cast<void **>(uintptr_t(vtable));
    object->flags = simple ? PhysicsObject::kSimpleBody : 0;
    object->rigidBodySlot = int16_t(simple ? g_simpleSlot : g_rigidSlot);
    object->renderObject = g_render;
    object->audioObject = g_sound;
    object->hitPointLoc = hitPoints;
    object->ownerSig = Chance(30) ? PlayerSig() : 0;
    BuildRender();
    BuildSound();
}

void BuildSimpleBody() {
    memset(g_simpleBody, 0, sizeof(SimpleRigidBody));
    Coord4 q = RandomVector(1.0f, Uniform(-1.0f, 1.0f));
    float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (length > 0.01f) {
        q.x /= length;
        q.y /= length;
        q.z /= length;
        q.w /= length;
    }
    g_simpleBody->orientation = q;
    Coord4 position = RandomVector(200.0f, 0.0f);
    g_simpleBody->position = {position.x, position.y, position.z};
    Coord4 velocity = RandomVector(Chance(20) ? 0.0f : 30.0f, 0.0f);
    g_simpleBody->velocity = {velocity.x, velocity.y, velocity.z};
    g_simpleBody->slot = int8_t(g_simpleSlot);
    g_simpleBody->flags = uint16_t(Next());
    g_simpleBody->radius = Uniform(0.1f, 3.0f);
    g_simpleBody->mass = Uniform(0.5f, 100.0f);
}

// A live rigid body's position, or a random one
Coord3 NearLiveBody(float spread) {
    std::vector<int> live;
    for (int i = 0; i < 64; i++)
        if (ShadowRigidOwners[i] != NULL)
            live.push_back(i);
    Coord4 offset = RandomVector(spread, 0.0f);
    if (live.empty())
        return {offset.x, offset.y, offset.z};
    const RigidBody *body = fgSimulation.GetRigidBody(live[size_t(Range(0, int(live.size()) - 1))]);
    return {body->position.x + offset.x, body->position.y + offset.y, body->position.z + offset.z};
}

// ---- Sentry

Sentry g_liveSentry;
bool g_haveLiveSentry;

void BuildSentry() {
    BuildBase(g_sentry, kSentryVtableAddress, true, &g_sentry->hitPoints);
    BuildSimpleBody();
    if (g_haveLiveSentry && Chance(50)) {
        memcpy(reinterpret_cast<uint8_t *>(g_sentry) + sizeof(PhysicsObject),
               reinterpret_cast<const uint8_t *>(&g_liveSentry) + sizeof(PhysicsObject),
               sizeof(Sentry) - sizeof(PhysicsObject));
    } else {
        g_sentry->srbAxis = Range(0, 2);
        g_sentry->animAxis = Range(0, 2);
        g_sentry->srbRate = Uniform(0.0f, 0.05f);
        g_sentry->animRate = Uniform(0.0f, 0.05f);
        g_sentry->srbLimit = Uniform(0.0f, 0.5f);
        g_sentry->animLimit = Uniform(0.0f, 0.5f);
        g_sentry->rangeSq = Uniform(0.0f, 300.0f) * Uniform(0.0f, 300.0f);
        g_sentry->hitPoints = Chance(10) ? 0.0f : Uniform(1.0f, 500.0f);
        g_sentry->inverseHitPoints = 1.0f / Uniform(1.0f, 500.0f);
        g_sentry->animFrames = Chance(40) ? -1.0f : float(Range(1, 200));
        g_sentry->animReversed = uint8_t(Range(0, 1));
        g_sentry->scoreable = uint8_t(Range(0, 1));
        g_sentry->shotsLeft = uint32_t(Range(0, 3));
        g_sentry->aimSystem = uint32_t(Range(1, 40));
        g_sentry->fireSystem = uint32_t(Range(1, 40));
        g_sentry->aimInstance = Range(0, 3);
        g_sentry->holdSteps = uint32_t(Range(0, 5));
        g_sentry->holdCountdown = uint32_t(Range(0, 6));
        g_sentry->shotInterval = Range(1, 30);
        g_sentry->nextShotStep = Chance(20) ? 0 : ShadowSimStepCount + Range(-20, 20);
    }
    g_sentry->hitPointLoc = &g_sentry->hitPoints;
    g_sentry->srbAngle = Uniform(0.0f, 1.0f);
    g_sentry->animAngle = Uniform(0.0f, 1.0f);
    g_sentry->targetSig = Chance(70) ? PlayerSig() : Chance(50) ? 0 : (Next() << 8);
    g_sentry->targetable = Chance(60) ? g_targetable : NULL;
    g_in.aimState = Chance(15) ? 4 : Chance(70) ? 2 : uint32_t(Range(0, 5));
    g_in.fireState = Chance(15) ? 4 : uint32_t(Range(0, 3));
    g_in.systemPlaying = Chance(30);
    g_in.numFrames = Chance(10) ? 0 : uint32_t(Range(1, 120));
    // The aim instance near the player (if any), in range or out of it
    memset(&g_in.aim, 0, sizeof(g_in.aim));
    g_in.aim.mtx[0][0] = g_in.aim.mtx[1][1] = g_in.aim.mtx[2][2] = g_in.aim.mtx[3][3] = 1.0f;
    PhysicsObject *player = fgSimulation.GetPlayerObject();
    Coord4 offset = RandomVector(Chance(50) ? 50.0f : 400.0f, 0.0f);
    if (player != NULL) {
        const Coord3 *at = player->GetPosition();
        offset.x += at->x;
        offset.y += at->y;
        offset.z += at->z;
    }
    if (Chance(10))
        offset.x = offset.z = 0.0f;     // straight above or below
    g_in.aim.mtx[3][0] = offset.x;
    g_in.aim.mtx[3][1] = offset.y;
    g_in.aim.mtx[3][2] = offset.z;
    ShadowSimStepCount = ShadowSimStepCount + Range(-30, 30);
}

void SideRotation() {
    if (g_original)
        g_out.rotation = Orig_SentryComputeNewRotation(g_sentry, 0, g_in.current, g_in.target, g_in.rate, g_in.limit);
    else
        g_out.rotation = g_sentry->ComputeNewRotation(g_in.current, g_in.target, g_in.rate, g_in.limit);
}

void SideTargetAngle() {
    if (g_original)
        g_out.rotation = Orig_SentryComputeTargetAngle(g_sentry, 0, &g_in.axis, &g_in.direction, &g_in.side, g_in.which);
    else
        g_out.rotation = g_sentry->ComputeTargetAngle(&g_in.axis, &g_in.direction, &g_in.side, g_in.which);
}

void SideAddFlash() {
    Coord3 direction = {g_in.direction.x, g_in.direction.y, g_in.direction.z};
    if (g_original)
        Orig_SentryAddMuzzleFlash(&g_in.axis, &direction);
    else
        Sentry::AddMuzzleFlash(&g_in.axis, &direction);
}

void SideDrawFlashes() {
    if (g_original)
        Orig_SentryDrawMuzzleFlashes();
    else
        Sentry::DrawMuzzleFlashes();
}

void SideRotateSRB() {
    g_out.flag = g_original ? Orig_SentryRotateSRB(g_sentry, 0, g_in.angle) : g_sentry->RotateSRB(g_in.angle);
}

void SideRotateAnim() {
    g_out.flag = g_original ? Orig_SentryRotateAnim(g_sentry, 0, g_in.angle) : g_sentry->RotateAnim(g_in.angle);
}

void SideShoot() {
    if (g_original)
        Orig_SentryShoot(g_sentry, 0);
    else
        g_sentry->Shoot();
}

void SideTrack() {
    bool inRange = true;
    g_out.flag = g_original ? Orig_SentryTrackTarget(g_sentry, 0, &inRange) : g_sentry->TrackTarget(&inRange);
    g_out.inRange = inRange;
}

void SideGetTarget() {
    WTargetable *target = g_original ? Orig_SentryGetTarget(g_sentry, 0) : g_sentry->GetTarget();
    g_out.result = int(uintptr_t(SettleTarget(target)));
}

void SideSentryDamage() {
    g_out.result = g_original ? Orig_SentryApplyDamage(g_sentry, 0, &g_in.from, &g_in.to, g_in.amount, g_in.split,
                                                       g_in.kind, &g_in.sourceSig)
                              : g_sentry->ApplyDamage(&g_in.from, &g_in.to, g_in.amount, g_in.split, g_in.kind,
                                                      &g_in.sourceSig);
}

void SideSentrySimulate() {
    if (g_original)
        Orig_SentrySimulate(g_sentry, 0);
    else
        g_sentry->Simulate();
    g_sentry->targetable = SettleTarget(g_sentry->targetable);
}

void RandomDamage() {
    Coord4 from = RandomVector(100.0f, 0.0f), to = RandomVector(100.0f, 0.0f);
    g_in.from = {from.x, from.y, from.z};
    g_in.to = {to.x, to.y, to.z};
    float limits[] = {5.0f, 10.0f, 20.0f, 0.0f};
    g_in.amount = Chance(40) ? Near(limits[Range(0, 3)]) : Uniform(0.0f, 300.0f);
    g_in.split = Uniform(0.0f, 1.0f);
    g_in.kind = Range(0, 3);
    g_in.sourceSig = Chance(50) ? PlayerSig() : Chance(50) ? 0 : (Next() << 8);
}

void TestSentry(const Snapshot &before) {
    // The rotations, pure
    for (long i = 0; i < 3000; i++) {
        g_in.current = Chance(10) ? 0.0f : Uniform(0.0f, 1.0f);
        g_in.target = Chance(10) ? Near(g_in.current) : Uniform(-0.2f, 1.2f);
        g_in.rate = Chance(10) ? 0.0f : Uniform(0.0f, 0.6f);
        g_in.limit = Chance(20) ? Near(Uniform(0.0f, 0.5f)) : Uniform(-0.1f, 1.1f);
        RunCase("Sentry::ComputeNewRotation", i, SideRotation, before);
    }
    for (long i = 0; i < 2000; i++) {
        g_in.axis = Chance(50) ? Coord4{0.0f, 0.0f, 1.0f, 0.01f} : RandomVector(2.0f, 0.0f);
        g_in.direction = RandomVector(Chance(5) ? 0.0f : 50.0f, Uniform(-1.0f, 1.0f));
        if (Chance(10))
            g_in.direction = g_in.axis;
        g_in.side = RandomVector(Chance(10) ? 0.0f : 1.0f, 0.0f);
        g_in.which = Range(0, 2);
        RunCase("Sentry::ComputeTargetAngle", i, SideTargetAngle, before);
    }
    // The muzzle flashes
    for (long i = 0; i < 200 && g_hooks[kHookFlashDraw].on; i++) {
        SentryMuzzleFlashCount = Range(0, kSentryMuzzleFlashes);
        for (int f = 0; f < kSentryMuzzleFlashes; f++) {
            Coord4 at = RandomVector(100.0f, Uniform(0.0f, 1.0f));
            Coord4 direction = RandomVector(1.0f, 0.0f);
            SentryMuzzleFlashes[f].position = at;
            SentryMuzzleFlashes[f].direction = {direction.x, direction.y, direction.z};
            SentryMuzzleFlashes[f].size = Chance(30) ? Near(0.1f / 0.33f) : Uniform(0.0f, 6.0f);
        }
        g_in.axis = RandomVector(100.0f, 1.0f);
        g_in.direction = RandomVector(1.0f, 0.0f);
        RunCase(i & 1 ? "Sentry::DrawMuzzleFlashes" : "Sentry::AddMuzzleFlash", i,
                i & 1 ? SideDrawFlashes : SideAddFlash, before);
    }
    if (!AllHooked()) {
        g_skipped++;
        return;
    }
    // The methods on a synthetic sentry
    for (long i = 0; i < 400; i++) {
        BuildSentry();
        g_in.angle = Chance(20) ? Near(g_sentry->srbAngle) : Uniform(0.0f, 1.0f);
        RunCase("Sentry::RotateSRB", i, SideRotateSRB, before);
        BuildSentry();
        g_in.angle = Chance(20) ? Near(g_sentry->animAngle) : Uniform(0.0f, 1.0f);
        RunCase("Sentry::RotateAnim", i, SideRotateAnim, before);
        BuildSentry();
        RunCase("Sentry::Shoot", i, SideShoot, before);
        BuildSentry();
        RunCase("Sentry::TrackTarget", i, SideTrack, before);
        BuildSentry();
        RunCase("Sentry::GetTarget", i, SideGetTarget, before);
        BuildSentry();
        RandomDamage();
        RunCase("Sentry::ApplyDamage", i, SideSentryDamage, before);
        BuildSentry();
        RunCase("Sentry::Simulate", i, SideSentrySimulate, before);
    }
}

// ---- Mine

void BuildMine() {
    BuildBase(g_mine, kMineVtableAddress, true, NULL);
    BuildSimpleBody();
    if (Chance(60))
        g_simpleBody->position = NearLiveBody(Chance(50) ? 2.0f : 12.0f);
    int kinds[] = {kWeaponTimeMine, kWeaponProxMine, kWeaponRemMine, kWeaponFloatingMine, 3};
    g_mine->kind = kinds[Range(0, 4)];
    g_mine->radius = Chance(80) ? Uniform(0.0f, 4.0f) : 0.0f;
    g_mine->timer = Range(-2, 4);
    g_mine->armStep = ShadowSimStepCount + Range(-3, 3);
    g_mine->detonate = uint8_t(Chance(20) ? 1 : 0);
    g_mine->effectsStarted = uint8_t(Range(0, 1));
    g_in.hitWorld = Chance(30) ? Range(-1, 2) : 0;
}

void SideMineDamage() {
    g_out.result = g_original ? Orig_MineApplyDamage(g_mine, 0, &g_in.from, &g_in.to, g_in.amount, g_in.split,
                                                     g_in.kind, &g_in.sourceSig)
                              : g_mine->ApplyDamage(&g_in.from, &g_in.to, g_in.amount, g_in.split, g_in.kind,
                                                    &g_in.sourceSig);
}

void SideMineTimer() {
    if (g_original)
        Orig_MineCheckTimer(g_mine, 0);
    else
        g_mine->CheckTimer();
}

void SideMineProximity() {
    if (g_original)
        Orig_MineCheckForProximity(g_mine, 0);
    else
        g_mine->CheckForProximity();
}

void SideMineSimulate() {
    if (g_original)
        Orig_MineSimulate(g_mine, 0);
    else
        g_mine->Simulate();
}

void TestMine(const Snapshot &before) {
    if (!AllHooked()) {
        g_skipped++;
        return;
    }
    for (long i = 0; i < 400; i++) {
        BuildMine();
        RandomDamage();
        RunCase("Mine::ApplyDamage", i, SideMineDamage, before);
        BuildMine();
        RunCase("Mine::CheckTimer", i, SideMineTimer, before);
        BuildMine();
        RunCase("Mine::CheckForProximity", i, SideMineProximity, before);
        BuildMine();
        RunCase("Mine::Simulate", i, SideMineSimulate, before);
    }
}

// ---- Smackable

void BuildSmackable() {
    BuildBase(g_smackable, kSmackableVtableAddress, false, &g_smackable->hitPoints);
    memset(g_rigidBody, 0, sizeof(RigidBody));
    memset(g_infoBlock, 0, sizeof(g_infoBlock));
    Coord4 q = RandomVector(1.0f, 1.0f);
    g_rigidBody->orientation = q;
    Coord3 at = NearLiveBody(30.0f);
    g_rigidBody->position = at;
    Coord4 velocity = RandomVector(Chance(40) ? 0.05f : 5.0f, 0.0f);
    g_rigidBody->velocity = {velocity.x, velocity.y, velocity.z};
    g_rigidBody->info = reinterpret_cast<RigidBodyInfo *>(g_infoBlock);
    g_rigidBody->sleepState = uint8_t(Range(1, 3));
    g_rigidBody->flags = Next();
    g_rigidBody->ownerIndex = int8_t(g_rigidSlot);

    memset(g_dataBlock, 0, sizeof(g_dataBlock));
    g_data->mass = Uniform(1.0f, 200.0f);
    g_data->hitPoints = float(Range(1, 400));
    g_data->forceDetach = uint8_t(Range(0, 1));
    memset(g_procAnimBlock, 0, sizeof(g_procAnimBlock));
    g_procAnim->damage = uint8_t(Next());
    memset(g_triggerBlock, 0, sizeof(g_triggerBlock));
    g_trigger->position = at;
    g_trigger->radius = Uniform(0.5f, 4.0f);
    g_trigger->shape = uint8_t(Range(1, 3));
    g_trigger->flags = uint16_t(Next());
    g_trigger->height = Uniform(0.5f, 4.0f);
    g_trigger->right = {1.0f, 0.0f, 0.0f};
    g_trigger->forward = {0.0f, 0.0f, 1.0f};
    g_trigger->packedSize = Next();

    g_smackable->sleepDelay = uint32_t(Range(0, 2));
    g_smackable->moveCountdown = Range(0, 3);
    g_smackable->data = g_data;
    g_smackable->hitPoints = Chance(30) ? g_data->hitPoints : Chance(10) ? 0.0f : Uniform(0.0f, g_data->hitPoints);
    g_smackable->trigger = Chance(70) ? g_trigger : NULL;
    g_smackable->procAnimState = g_procAnim;
    g_smackable->sound = reinterpret_cast<ASmackable *>(g_sound);
    g_in.anyPlaying = Chance(30);
    g_in.soundActive = Chance(30);
    g_in.placed = Chance(80);
    if (Chance(20))
        g_smackable->audioObject = NULL;
}

void SideSmackDamage() {
    g_out.result = g_original ? Orig_SmackableApplyDamage(g_smackable, 0, &g_in.from, &g_in.to, g_in.amount,
                                                          g_in.split, g_in.kind, &g_in.sourceSig)
                              : g_smackable->ApplyDamage(&g_in.from, &g_in.to, g_in.amount, g_in.split, g_in.kind,
                                                         &g_in.sourceSig);
}

void SideSmackSimulate() {
    if (g_original)
        Orig_SmackableSimulate(g_smackable, 0);
    else
        g_smackable->Simulate();
}

void SideSmackSleep() {
    if (g_original)
        Orig_SmackableGoToSleep(g_smackable, 0);
    else
        g_smackable->GoToSleep();
}

void TestSmackable(const Snapshot &before) {
    if (!AllHooked() || g_rigidSlot < 0) {
        g_skipped++;
        return;
    }
    for (long i = 0; i < 400; i++) {
        BuildSmackable();
        RandomDamage();
        if (Chance(30))     // about the first impact level: amount * 32 / hit points near 0.75
            g_in.amount = Near(0.75f * g_data->hitPoints / 32.0f);
        RunCase("Smackable::ApplyDamage", i, SideSmackDamage, before);
        BuildSmackable();
        RunCase("Smackable::Simulate", i, SideSmackSimulate, before);
        BuildSmackable();
        RunCase("Smackable::GoToSleep", i, SideSmackSleep, before);
    }
}

// ---- Human

void BuildHuman() {
    BuildBase(g_human, kHumanVtableAddress, true, &g_aiHitPoints);
    BuildSimpleBody();
    memset(g_aiBlock, 0, sizeof(g_aiBlock));
    memset(g_actorLinkBlock, 0, sizeof(g_actorLinkBlock));
    memset(g_actorBlock, 0, sizeof(g_actorBlock));
    memset(g_characterBlock, 0, sizeof(g_characterBlock));
    memset(g_hitReachBlock, 0, sizeof(g_hitReachBlock));
    g_ai->vtable = g_aiVtable;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            g_ai->transform.mtx[r][c] = Uniform(-100.0f, 100.0f);
    g_ai->hitPoints = Chance(80) ? &g_aiHitPoints : NULL;
    g_ai->actorLink = reinterpret_cast<AICharacterActorLink *>(g_actorLinkBlock);
    g_ai->actorLink->actor = g_actor;
    g_actor->character = g_character;
    g_actor->visible = Chance(80);
    g_character->weapons[0] = Chance(70) ? reinterpret_cast<ActWeapon *>(g_weaponBlocks[0]) : NULL;
    g_character->weapons[1] = Chance(50) ? reinterpret_cast<ActWeapon *>(g_weaponBlocks[1]) : NULL;
    g_ai->hitShape = Chance(60) ? reinterpret_cast<AICharacterHitShape *>(g_hitReachBlock) : NULL;
    reinterpret_cast<AICharacterHitShape *>(g_hitReachBlock)->radius = Uniform(-2.0f, 5.0f);
    g_ai->zoneLastHit = Range(0, 15);
    g_ai->scoreable = uint8_t(Range(0, 1));
    g_aiHitPoints = Chance(10) ? 0.0f : Uniform(0.0f, 100.0f);
    g_human->unknown6c = Chance(20) ? 1 : Range(0, 3);
    g_human->character = Chance(90) ? g_ai : NULL;
    g_human->dead = uint8_t(Chance(15) ? 1 : 0);
    g_human->unknown8c = uint8_t(Next());
    g_in.zoneScale = Chance(30) ? 1.0 : Uniform(0.0f, 6.0f);
}

void SideHumanActivate() {
    if (g_original)
        Orig_HumanActivate(g_human, 0);
    else
        g_human->Activate();
}

void SideHumanDeactivate() {
    if (g_original)
        Orig_HumanDeactivate(g_human, 0);
    else
        g_human->Deactivate();
}

void SideHumanSimulate() {
    if (g_original)
        Orig_HumanSimulate(g_human, 0);
    else
        g_human->Simulate();
}

void SideHumanSetAI() {
    if (g_original)
        Orig_HumanSetAICharacter(g_human, 0, g_ai);
    else
        g_human->SetAICharacter(g_ai);
}

void SideHumanDamage() {
    g_out.result = g_original ? Orig_HumanApplyDamage(g_human, 0, &g_in.from, &g_in.to, g_in.amount, g_in.split,
                                                      g_in.kind, &g_in.sourceSig)
                              : g_human->ApplyDamage(&g_in.from, &g_in.to, g_in.amount, g_in.split, g_in.kind,
                                                     &g_in.sourceSig);
}

void TestHuman(const Snapshot &before) {
    if (!AllHooked()) {
        g_skipped++;
        return;
    }
    for (long i = 0; i < 300; i++) {
        BuildHuman();
        RunCase("Human::Activate", i, SideHumanActivate, before);
        BuildHuman();
        RunCase("Human::Deactivate", i, SideHumanDeactivate, before);
        BuildHuman();
        RunCase("Human::Simulate", i, SideHumanSimulate, before);
        BuildHuman();
        RunCase("Human::SetAICharacter", i, SideHumanSetAI, before);
        BuildHuman();
        if (g_human->character == NULL)
            g_human->character = g_ai;
        RandomDamage();
        RunCase("Human::ApplyDamage", i, SideHumanDamage, before);
    }
}

// ---- the live objects, the slots, the regions

void FindLive(int *counts) {
    for (int i = 0; i < 96 + 64; i++) {
        PhysicsObject *object = i < 96 ? ShadowSimpleOwners[i] : ShadowRigidOwners[i - 96];
        if (object == NULL)
            continue;
        uint32_t vtable = uint32_t(uintptr_t(object->vtable));
        if (vtable == kSentryVtableAddress) {
            counts[0]++;
            if (!g_haveLiveSentry) {
                memcpy(&g_liveSentry, object, sizeof(Sentry));
                g_haveLiveSentry = true;
            }
        } else if (vtable == kMineVtableAddress) {
            counts[1]++;
        } else if (vtable == kSmackableVtableAddress) {
            counts[2]++;
        } else if (vtable == kHumanVtableAddress) {
            counts[3]++;
        }
    }
    for (int i = 0; i < 96 && g_simpleSlot < 0; i++)
        if (ShadowSimpleOwners[i] == NULL)
            g_simpleSlot = i;
    for (int i = 0; i < 64 && g_rigidSlot < 0; i++)
        if (ShadowRigidOwners[i] == NULL)
            g_rigidSlot = i;
    if (g_simpleSlot >= 0)
        g_simpleBody = fgSimulation.GetSimpleRigidBody(g_simpleSlot);
    if (g_rigidSlot >= 0)
        g_rigidBody = fgSimulation.GetRigidBody(g_rigidSlot);
}

void BuildRegions() {
    g_regions.clear();
    AddOwned(SentryMuzzleFlashes, 0x001e9170, sizeof(SentryMuzzleFlashes), "SentryMuzzleFlashes");
    AddOwned(&SentryMuzzleFlashCount, 0x001e9278, 4, "SentryMuzzleFlashCount");
    AddShared(g_objectBlock, sizeof(g_objectBlock), "the object");
    AddShared(g_renderBlock, sizeof(g_renderBlock), "the render object");
    AddShared(g_handleBlock, sizeof(g_handleBlock), "the animation handle");
    AddShared(g_soundBlock, sizeof(g_soundBlock), "the sound");
    AddShared(g_aiBlock, sizeof(g_aiBlock), "the AI character");
    AddShared(g_actorLinkBlock, sizeof(g_actorLinkBlock), "the actor link");
    AddShared(g_actorBlock, sizeof(g_actorBlock), "the actor");
    AddShared(g_characterBlock, sizeof(g_characterBlock), "the actor's character");
    AddShared(g_weaponBlocks, sizeof(g_weaponBlocks), "the weapons");
    AddShared(g_hitReachBlock, sizeof(g_hitReachBlock), "the hit reach");
    AddShared(g_triggerBlock, sizeof(g_triggerBlock), "the trigger");
    AddShared(g_dataBlock, sizeof(g_dataBlock), "the physics data");
    AddShared(g_procAnimBlock, sizeof(g_procAnimBlock), "the proc-anim state");
    AddShared(g_targetableBlock, sizeof(g_targetableBlock), "the targetable");
    AddShared(g_infoBlock, sizeof(g_infoBlock), "the rigid body's info");
    AddShared(&g_aiHitPoints, sizeof(g_aiHitPoints), "the AI's hit points");
    AddShared(g_simpleBody, sizeof(SimpleRigidBody), "the simple body");
    AddShared(g_rigidBody, sizeof(RigidBody), "the rigid body");
    AddShared(&g_out, sizeof(g_out), "the answers");
    AddShared(&g_in, sizeof(g_in), "the inputs", false);
    AddShared(&ShadowSimStepCount, 4, "SimStepCount", false);
}

void InstallHooks() {
    for (int i = 0; i < kHookCount; i++)
        HookInstall(&g_hooks[i], kHookEntries[i], kHookTargets[i]);
    int missing = 0;
    for (const Hook &hook : g_hooks)
        missing += hook.on ? 0 : 1;
    if (missing != 0)
        printf("[gameweaponsshadowd] %d of the %d fakes could not be installed - the object cases skipped\n",
               missing, int(kHookCount));
    g_renderVtable[14] = (void *)&FakeUpdatePosition;
    g_renderVtable[15] = (void *)&FakeTriggerFX;
    g_soundVtable[5] = (void *)&FakePlayCollision;
    g_soundVtable[6] = (void *)&FakeIsStillActive;
    g_aiVtable[0] = (void *)&FakeAIDelete;
    g_aiVtable[4] = (void *)&FakeNotify;
    g_aiVtable[5] = (void *)&FakeZoneScale;
    g_aiVtable[6] = (void *)&FakeInjury;
}

}  // namespace

void GameWeaponsShadowD_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_GAMEWEAPONSSHADOWD", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    int counts[4] = {0, 0, 0, 0};
    FindLive(counts);
    printf("[gameweaponsshadowd] live at the first tick: %d sentries, %d mines, %d smackables, %d humans\n",
           counts[0], counts[1], counts[2], counts[3]);
    if (g_simpleSlot < 0) {
        printf("[gameweaponsshadowd] no free simple body slot - nothing run\n");
        fflush(stdout);
        return;
    }
    InstallHooks();
    BuildRegions();
    Snapshot before;
    Capture(&before);

    TestSentry(before);
    TestMine(before);
    TestSmackable(before);
    TestHuman(before);

    Restore(before);
    for (Hook &hook : g_hooks)
        HookRemove(&hook);
    ResetFpu();
    printf("[gameweaponsshadowd] sentries, mines, smackables, humans vs originals: %ld cases, %ld checks, %ld differ%s"
           "\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[gameweaponsshadowd]   %ld calls faulted\n", g_faults);
    if (g_skipped != 0)
        printf("[gameweaponsshadowd]   %ld groups skipped (fakes or slots missing)\n", g_skipped);
    fflush(stdout);
}
