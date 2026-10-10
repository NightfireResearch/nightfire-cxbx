#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "BondCarShadowA.h"
#include "FpControl.h"

#include "../EventManager.hpp"
#include "../audio/Sound.h"
#include "../camera/PlayerCamera.h"
#include "../engine/ActionQueue.hpp"
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../game/BondCar.h"
#include "../game/BondCarSnowmobile.h"
#include "../game/BondCarState.h"
#include "../game/SoftZone.h"
#include "../physics/RigidBody.h"
#include "../physics/RigidBodyResolve.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"
#include "../world/Targeting.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"

#include <windows.h>
#include <bit>
#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_BONDCARSHADOWA=1, once on the first simulation tick: package V_A (0x00061a50-0x000644c0) against the
// originals.
//
// Each car case starts from a copy of a live PBondCar (every car in the Simulation's table with PBondCar's
// vtable), perturbed, with its CarPhysics, audio object, action queue, target beacon and hit points swapped for
// copies of their own, and a fake render object whose slots 14 (UpdatePosition) and 15 (TriggerFX) record their
// arguments. Simulate's scratch pad is a buffer of ours. The case runs twice from the same bytes - first with the
// package's originals swapped back in, then with our jumps - calling the original address both times. Before each
// run everything a run can write is put back: the copy and its swapped objects, the car's live rigid body and its
// RigidBodyInfo (perturbed: velocity, spin, position, the info's flags), the live car, the simulation's random
// generator, the collision manager's barrier mask, the cars' tuning globals and the damage threshold, the event
// queue's creation point; all of it is compared afterwards, with the result and the calls made.
//
// The calls that reach beyond the car are replaced for both runs by fakes that record their arguments (strings by
// content): RVehicle's SetEMPVictimEffect, TriggerEmpBolts and TriggerTurboBoostSmoke, AMenuSound::Trigger, the
// mission manager's ProgrammerDefinedEvent and IncShotsHit, RSceneObj::Reset, RPlayerCamera::ResetCamera,
// RTyreTrack's constructor (the blocks are allocated and then freed after the run; the copy's tyre track pointers
// are compared as NULL or not) and ECollision's constructor (the impact recorded, less the 8 bytes at +0x38 its
// constructor leaves).
//
// Not covered: EnableTargetBeacon with no beacon (it would allocate and register a target), ResetCar's freeing of
// old tracks (the copy's are NULL), and AddSnowmobileForces' last w of the scratch pad's +0x280 (the original's
// uninitialised stack).
//
// A mutation this catches: CalculateRPM comparing the gears unsigned (uint8_t) - with gearChangeTimer set and
// previousGear at -1, the port would take gear 255; the RPM cases set gearChangeTimer in half of them.
//
// NIGHTFIRE_BONDCARSHADOWA=3 skips all that and compares every live ProcessSnowmobilePhysics call instead
// (BondCarShadowA_Tick, below), printing the first differing calls with their inputs.
//
// The live cars start level, so the snowmobile physics cases pitch the body: a heading with y zero hid the forward
// speed taken from the body's velocity rather than its level copy.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr unsigned kRangeLo = 0x00061a50, kRangeHi = 0x000644c0;

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f; }
    int Range(int lo, int hi) { return lo + int(Next() % uint32_t(hi - lo + 1)); }
    bool Chance(int percent) { return int(Next() % 100) < percent; }
};

#define ShadowPlayerCar (**(PBondCar ***)0x00234e40)
#define ShadowGetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define ShadowSim ((void *)0x00233ff0)
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowCreationPoint (*(char **)0x001e47d8)

constexpr uintptr_t kTuningStart = 0x001c36cc;      // the tuning globals InitializeBondCarGlobals loads
constexpr size_t kTuningSize = 0x001c37a8 - 0x001c36cc;
constexpr size_t kAudioSize = 0x160;                // the car's audio object, as far as the package writes it
constexpr size_t kSoundSize = 0x60;                 // SetOrientation's

// ---- five-byte jumps over the callees the fakes stand in for

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[24];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old)) {
        h.on = false;
        return;
    }
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HookBoth(uint32_t at, size_t ours, const void *to) {
    HookInstall(at, to);
    if (ours != 0 && ours != at)
        HookInstall((uint32_t)ours, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

// ---- the fakes' record

struct LogEntry {
    uint32_t id;
    uint32_t words[22];
};
constexpr int kMaxLog = 24;
LogEntry g_log[kMaxLog];
int g_logCount;

LogEntry *Log(uint32_t id) {
    if (g_logCount >= kMaxLog)
        return NULL;
    LogEntry *e = &g_log[g_logCount++];
    memset(e, 0, sizeof(*e));
    e->id = id;
    return e;
}

void LogString(uint32_t *words, const char *text, size_t bytes) {
    if (text != NULL)
        strncpy(reinterpret_cast<char *>(words), text, bytes);
}

void __fastcall FakeEmpVictim(void *self, int, int on) {
    if (LogEntry *e = Log(1)) {
        e->words[0] = uint32_t(uintptr_t(self));
        e->words[1] = uint32_t(on) & 0xff;
    }
}

void __fastcall FakeEmpBolts(void *self, int) {
    if (LogEntry *e = Log(2))
        e->words[0] = uint32_t(uintptr_t(self));
}

void __fastcall FakeBoostSmoke(void *self, int, int steps) {
    if (LogEntry *e = Log(3)) {
        e->words[0] = uint32_t(uintptr_t(self));
        e->words[1] = uint32_t(steps);
    }
}

void FakeMenuSound(int bank, const char *patch, const char *mix, int view) {
    if (LogEntry *e = Log(4)) {
        e->words[0] = uint32_t(bank);
        LogString(e->words + 1, patch, 32);
        LogString(e->words + 9, mix, 32);
        e->words[17] = uint32_t(view);
    }
}

void __fastcall FakeMissionEvent(void *self, int, int event, const char *text) {
    if (LogEntry *e = Log(5)) {
        e->words[0] = uint32_t(uintptr_t(self));
        e->words[1] = uint32_t(event);
        LogString(e->words + 2, text, 16);
    }
}

void __fastcall FakeShotsHit(void *self, int, int unknown) {
    if (LogEntry *e = Log(6)) {
        e->words[0] = uint32_t(uintptr_t(self));
        e->words[1] = uint32_t(unknown);
    }
}

void __fastcall FakeSceneReset(void *self, int) {
    if (LogEntry *e = Log(7))
        e->words[0] = uint32_t(uintptr_t(self));
}

void __fastcall FakeResetCamera(void *self, int) {
    if (LogEntry *e = Log(8))
        e->words[0] = uint32_t(uintptr_t(self));
}

void *__fastcall FakeECollision(void *self, int, CollisionImpact impact) {
    memset(impact.unknown38, 0, sizeof(impact.unknown38));     // left by the impact's constructor
    if (LogEntry *e = Log(9))
        memcpy(e->words, &impact, sizeof(impact));
    return self;
}

void *__fastcall FakeTyreTrack(void *self, int, float width, bool longTrack, bool unknown) {
    if (LogEntry *e = Log(10)) {
        memcpy(&e->words[0], &width, 4);
        e->words[1] = longTrack;
        e->words[2] = unknown;
    }
    return self;
}

void __fastcall FakeUpdatePosition(void *self, int, int update) {
    if (LogEntry *e = Log(11)) {
        e->words[0] = uint32_t(uintptr_t(self));
        e->words[1] = uint32_t(update) & 0xff;
    }
}

void __fastcall FakeTriggerFX(void *self, int, int type, uint32_t which, uint32_t unused, const Coord3 *position,
                              const Coord3 *direction, float intensity) {
    if (LogEntry *e = Log(12)) {
        e->words[0] = uint32_t(uintptr_t(self));
        e->words[1] = uint32_t(type);
        e->words[2] = which;
        e->words[3] = unused;
        e->words[4] = uint32_t(uintptr_t(position));
        e->words[5] = uint32_t(uintptr_t(direction));
        memcpy(&e->words[6], &intensity, 4);
    }
}

void InstallFakes() {
    typedef void MenuTrigger(int, const char *, const char *, int);
    HookInstall(0x000959d0, (const void *)&FakeEmpVictim);
    HookInstall(0x00095a10, (const void *)&FakeEmpBolts);
    HookInstall(0x000959f0, (const void *)&FakeBoostSmoke);
    HookBoth(0x0011e010, XbeAddress(XbeOverload<MenuTrigger>::Of(&AMenuSound::Trigger)),
             (const void *)&FakeMenuSound);
    HookInstall(0x000b72f0, (const void *)&FakeMissionEvent);
    HookInstall(0x000b6720, (const void *)&FakeShotsHit);
    HookBoth(0x00090a40, XbeAddress(&RSceneObj::Reset), (const void *)&FakeSceneReset);
    HookBoth(0x00081430, XbeAddress(&RPlayerCamera::ResetCamera), (const void *)&FakeResetCamera);
    HookInstall(0x0003f370, (const void *)&FakeECollision);
    HookInstall(0x000abfc0, (const void *)&FakeTyreTrack);
}

// The render object the copies hold: its vtable's slots 14 and 15 record
void *g_fakeRenderVtable[19];
struct FakeRender {
    void **vtable;
    uint8_t rest[0x3c];
} g_fakeRender;

// ---- the world outside the copy that a run can write

RigidBody *g_body;
RigidBodyInfo *g_info;
PBondCar *g_liveCar;

struct World {
    uint8_t body[sizeof(RigidBody)];
    uint8_t info[sizeof(RigidBodyInfo)];
    uint8_t liveCar[sizeof(PBondCar)];
    SimRandom random;
    uint32_t barrierMask;
    uint8_t tuning[kTuningSize];
    float damageThreshold;
    char *creationPoint;
};

void Take(World *w) {
    memset(w, 0, sizeof(*w));
    if (g_body != NULL)
        memcpy(w->body, g_body, sizeof(w->body));
    if (g_info != NULL)
        memcpy(w->info, g_info, sizeof(w->info));
    if (g_liveCar != NULL)
        memcpy(w->liveCar, g_liveCar, sizeof(w->liveCar));
    w->random = *ShadowRandom;
    w->barrierMask = fgCollisionMgr->barrierMask;
    memcpy(w->tuning, (const void *)kTuningStart, kTuningSize);
    w->damageThreshold = fgDamageLevels.spread;
    w->creationPoint = ShadowCreationPoint;
}

void Put(const World &w) {
    if (g_body != NULL)
        memcpy(g_body, w.body, sizeof(w.body));
    if (g_info != NULL)
        memcpy(g_info, w.info, sizeof(w.info));
    if (g_liveCar != NULL)
        memcpy(g_liveCar, w.liveCar, sizeof(w.liveCar));
    *ShadowRandom = w.random;
    fgCollisionMgr->barrierMask = w.barrierMask;
    memcpy((void *)kTuningStart, w.tuning, kTuningSize);
    fgDamageLevels.spread = w.damageThreshold;
    ShadowCreationPoint = w.creationPoint;
}

// ---- the cases

enum Op {
    kGetPhysics, kSetOrientation, kGlobals, kInitTyreTracks, kInitControls, kFlushControls, kInitVariables,
    kResetDamage, kAddDamageByPlayer, kGetNumTires, kSetInShock, kResetCar, kGetDamageZones, kCalculateRPM,
    kFireLaser, kAttackWithEmp, kGetSplinePath, kEnableBeacon, kDisableBeacon, kGlareOn, kGlareOff, kTurnSignals,
    kClearEMP, kRocketBoost, kEnableTwoWheel, kDisableTwoWheel, kImproveLanding, kSnowmobileForces,
    kSnowmobilePhysics, kInsideQuad, kSoftZone, kOpCount,
};
const char *const kOpNames[kOpCount] = {
    "GetPhysics", "FUN_00061a60", "InitializeBondCarGlobals", "InitTyreTracks", "InitializeCarControls",
    "FlushCarControls", "InitializeCarVariables", "ResetDamage", "AddDamageByPlayer", "GetNumTires", "SetInShock",
    "ResetCar", "GetDamageZones", "CalculateRPM", "FireLaser", "AttackWithEmp", "GetSplinePath",
    "EnableTargetBeacon", "DisableTargetBeacon", "GlareOn", "GlareOff", "HandleTurnSignals", "ClearEMPState",
    "EnableRocketBoost", "EnableTwoWheelStunt", "DisableTwoWheelStunt", "ImproveLanding", "AddSnowmobileForces",
    "ProcessSnowmobilePhysics", "FUN_000643b0", "PBondCar_PosInSoftZone",
};
const bool kBoolResult[kOpCount] = {
    false, false, false, false, false, false, false, false, false, false, false, false, false, false, false, false,
    false, false, false, false, false, false, false, true, false, false, false, false, false, true, true,
};

// The scratch objects a run works on
alignas(16) uint8_t g_car[sizeof(PBondCar)];
CarPhysics g_physics;
alignas(16) uint8_t g_audio[kAudioSize];
alignas(16) uint8_t g_queue[sizeof(ActionQueue)];
alignas(16) uint8_t g_beacon[sizeof(WTargetable)];
float g_hitPoints;
alignas(16) BondCarScratch g_pad;
alignas(16) uint8_t g_sound[kSoundSize];
struct Args {
    BondCarWheelInput inputs[kCarWheels];
    Coord4 axes[2];
    Coord4 normals[kCarWheels];
    Coord4 wheels[kCarWheels];
    Coord4 point;
    Coord4 quad[4];
    Coord4 plane;
    Coord3 position;
    Coord3 direction;
    float loads[kCarWheels];
    uint32_t count;
};
alignas(16) Args g_args;

struct Case {
    Op op;
    alignas(16) uint8_t car[sizeof(PBondCar)];
    CarPhysics physics;
    alignas(16) uint8_t audio[kAudioSize];
    alignas(16) uint8_t queue[sizeof(ActionQueue)];
    alignas(16) uint8_t beacon[sizeof(WTargetable)];
    float hitPoints;
    alignas(16) uint8_t pad[sizeof(BondCarScratch)];
    alignas(16) uint8_t sound[kSoundSize];
    Args args;
    World world;
    bool hasQueue, hasBeacon, hasRender, clearTracks;
    int i0;
    float f0;
};

struct Result {
    uint8_t car[sizeof(PBondCar)];
    CarPhysics physics;
    uint8_t audio[kAudioSize];
    uint8_t queue[sizeof(ActionQueue)];
    uint8_t beacon[sizeof(WTargetable)];
    float hitPoints;
    uint8_t pad[sizeof(BondCarScratch)];
    uint8_t sound[kSoundSize];
    Args args;
    World world;
    LogEntry log[kMaxLog];
    int logCount;
    uint64_t value;
    bool faulted;
};

PBondCar *Car() { return reinterpret_cast<PBondCar *>(g_car); }

typedef void (__fastcall *PlainFn)(PBondCar *, int);
typedef int (__fastcall *IntFn)(PBondCar *, int);
typedef void *(__fastcall *PointerFn)(PBondCar *, int);
typedef double (__fastcall *DoubleFn)(PBondCar *, int);
typedef void (__fastcall *BoolArgFn)(PBondCar *, int, bool);
typedef void (__fastcall *FloatArgFn)(PBondCar *, int, float);
typedef void (__fastcall *IntArgFn)(PBondCar *, int, int);
typedef void *(__fastcall *ZonesFn)(PBondCar *, int, uint32_t *);
typedef void (__fastcall *ResetFn)(PBondCar *, int, Coord3 *, const Coord3 *);
typedef int (__fastcall *ForcesFn)(PBondCar *, int, const BondCarWheelInput *, const Coord4 *, const Coord4 *,
                                   const Coord4 *, float *);
typedef void (__fastcall *OrientFn)(void *, int, const Coord3 *, const Coord3 *, const Coord3 *);
typedef bool (*QuadFn)(const Coord4 *, const Coord4 *, const Coord4 *);
typedef bool (*ZoneFn)(const Coord4 *);

uint64_t Call(const Case &c) {
    PBondCar *car = Car();
    Args &a = g_args;
    switch (c.op) {
    case kGetPhysics:
        return uint32_t(uintptr_t(reinterpret_cast<PointerFn>(0x00061a50)(car, 0)));
    case kSetOrientation:
        reinterpret_cast<OrientFn>(0x00061a60)(g_sound, 0, reinterpret_cast<Coord3 *>(&a.axes[0]),
                                               reinterpret_cast<Coord3 *>(&a.axes[1]),
                                               reinterpret_cast<Coord3 *>(&a.point));
        return 0;
    case kGlobals:
        reinterpret_cast<void (*)()>(0x00061ae0)();
        return 0;
    case kInitTyreTracks:
        reinterpret_cast<PlainFn>(0x00062120)(car, 0);
        return 0;
    case kInitControls:
        reinterpret_cast<PlainFn>(0x000623b0)(car, 0);
        return 0;
    case kFlushControls:
        reinterpret_cast<PlainFn>(0x00062430)(car, 0);
        return 0;
    case kInitVariables:
        reinterpret_cast<BoolArgFn>(0x00062440)(car, 0, c.i0 != 0);
        return 0;
    case kResetDamage:
        reinterpret_cast<PlainFn>(0x00062600)(car, 0);
        return 0;
    case kAddDamageByPlayer:
        reinterpret_cast<FloatArgFn>(0x00062660)(car, 0, c.f0);
        return 0;
    case kGetNumTires:
        return uint32_t(reinterpret_cast<IntFn>(0x000626e0)(car, 0));
    case kSetInShock:
        reinterpret_cast<FloatArgFn>(0x00062740)(car, 0, c.f0);
        return 0;
    case kResetCar:
        reinterpret_cast<ResetFn>(0x000627c0)(car, 0, &a.position, &a.direction);
        return 0;
    case kGetDamageZones:
        return uint32_t(uintptr_t(reinterpret_cast<ZonesFn>(0x00062a40)(car, 0, &a.count)));
    case kCalculateRPM: {
        double rpm = reinterpret_cast<DoubleFn>(0x00062a60)(car, 0);
        uint64_t bits;
        memcpy(&bits, &rpm, 8);
        return bits;
    }
    case kFireLaser:
        reinterpret_cast<PlainFn>(0x00062b70)(car, 0);
        return 0;
    case kAttackWithEmp:
        reinterpret_cast<PlainFn>(0x00062b90)(car, 0);
        return 0;
    case kGetSplinePath:
        return uint32_t(uintptr_t(reinterpret_cast<PointerFn>(0x00062c40)(car, 0)));
    case kEnableBeacon:
        reinterpret_cast<BoolArgFn>(0x00062c60)(car, 0, c.i0 != 0);
        return 0;
    case kDisableBeacon:
        reinterpret_cast<PlainFn>(0x00062cf0)(car, 0);
        return 0;
    case kGlareOn:
        reinterpret_cast<IntArgFn>(0x00062d00)(car, 0, c.i0);
        return 0;
    case kGlareOff:
        reinterpret_cast<IntArgFn>(0x00062d70)(car, 0, c.i0);
        return 0;
    case kTurnSignals:
        reinterpret_cast<PlainFn>(0x00062de0)(car, 0);
        return 0;
    case kClearEMP:
        reinterpret_cast<PlainFn>(0x00062ec0)(car, 0);
        return 0;
    case kRocketBoost:
        return uint32_t(reinterpret_cast<IntFn>(0x00062ee0)(car, 0));
    case kEnableTwoWheel:
        reinterpret_cast<PlainFn>(0x00062f50)(car, 0);
        return 0;
    case kDisableTwoWheel:
        reinterpret_cast<PlainFn>(0x00062f60)(car, 0);
        return 0;
    case kImproveLanding:
        reinterpret_cast<PlainFn>(0x00062fe0)(car, 0);
        return 0;
    case kSnowmobileForces:
        return uint32_t(reinterpret_cast<ForcesFn>(0x000632a0)(car, 0, a.inputs, a.axes, a.normals, a.wheels,
                                                               a.loads));
    case kSnowmobilePhysics:
        reinterpret_cast<PlainFn>(0x00063ad0)(car, 0);
        return 0;
    case kInsideQuad:
        return reinterpret_cast<uint32_t (*)(const Coord4 *, const Coord4 *, const Coord4 *)>(0x000643b0)(
            &a.point, a.quad, &a.plane);
    case kSoftZone:
        return reinterpret_cast<uint32_t (*)(const Coord4 *)>(0x00064440)(&a.point);
    default:
        return 0;
    }
}

bool SafeCall(const Case &c, uint64_t *value) {
#ifdef _MSC_VER
    __try {
        *value = Call(c);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
#else
    *value = Call(c);
    return true;
#endif
}

void RunOnce(const Case &c, Result *r, bool original) {
    memcpy(g_car, c.car, sizeof(g_car));
    g_physics = c.physics;
    memcpy(g_audio, c.audio, sizeof(g_audio));
    memcpy(g_queue, c.queue, sizeof(g_queue));
    memcpy(g_beacon, c.beacon, sizeof(g_beacon));
    g_hitPoints = c.hitPoints;
    memcpy(&g_pad, c.pad, sizeof(g_pad));
    memcpy(g_sound, c.sound, sizeof(g_sound));
    g_args = c.args;
    PBondCar *car = Car();
    car->physics = &g_physics;
    car->audio = reinterpret_cast<ABaseSound *>(g_audio);
    car->actionQueue = c.hasQueue ? reinterpret_cast<ActionQueue *>(g_queue) : NULL;
    car->targetBeacon = c.hasBeacon ? reinterpret_cast<WTargetable *>(g_beacon) : NULL;
    car->renderObject = c.hasRender ? reinterpret_cast<RSceneObj *>(&g_fakeRender) : NULL;
    car->hitPointLoc = &g_hitPoints;
    if (c.clearTracks)
        memset(car->tyreTracks, 0, sizeof(car->tyreTracks));
    BondCarScratch *savedPad = BondCarScratchPad;
    BondCarScratchPad = &g_pad;
    Put(c.world);
    g_logCount = 0;
    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, true);
    r->faulted = !SafeCall(c, &r->value);
    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, false);
    BondCarScratchPad = savedPad;
    if (kBoolResult[c.op])
        r->value &= 0xff;
    // tyre tracks made by the run: freed, and compared as made or not
    if (c.clearTracks) {
        for (int wheel = 0; wheel < kCarWheels; wheel++) {
            if (car->tyreTracks[wheel] != NULL) {
                UMemory::FastFree(car->tyreTracks[wheel], 0x920);
                car->tyreTracks[wheel] = reinterpret_cast<RTyreTrack *>(1);
            }
        }
    }
    memcpy(r->car, g_car, sizeof(r->car));
    r->physics = g_physics;
    memcpy(r->audio, g_audio, sizeof(r->audio));
    memcpy(r->queue, g_queue, sizeof(r->queue));
    memcpy(r->beacon, g_beacon, sizeof(r->beacon));
    r->hitPoints = g_hitPoints;
    memcpy(r->pad, &g_pad, sizeof(r->pad));
    memcpy(r->sound, g_sound, sizeof(r->sound));
    r->args = g_args;
    Take(&r->world);
    memcpy(r->log, g_log, sizeof(r->log));
    r->logCount = g_logCount;
}

int g_cases, g_checks, g_differ, g_faults, g_details;
bool g_verbose;
int g_opCases[kOpCount], g_opDiffer[kOpCount], g_opDetails[kOpCount];

// The first two details of each kind of case, at most 60 in all
void Detail(const Case &c, int index, const char *what, size_t offset, uint32_t original, uint32_t ours) {
    if (g_opDetails[c.op] >= 2 || g_details >= 60)
        return;
    g_opDetails[c.op]++;
    g_details++;
    printf("[bondcarA]   case %d %s: %s +0x%03x original %08x ours %08x\n", index, kOpNames[c.op], what,
           unsigned(offset), original, ours);
}

void CompareBlock(const Case &c, int index, const char *what, const void *a, const void *b, size_t size,
                  bool *differ) {
    g_checks++;
    if (size == 0 || memcmp(a, b, size) == 0)
        return;
    *differ = true;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t x, y;
        memcpy(&x, static_cast<const uint8_t *>(a) + i, 4);
        memcpy(&y, static_cast<const uint8_t *>(b) + i, 4);
        if (x != y) {
            Detail(c, index, what, i, x, y);
            return;
        }
    }
    Detail(c, index, what, 0, 0, 0);
}

World g_live;

void RunCase(const Case &c) {
    static Result original, ours;
    int index = g_cases++;
    g_opCases[c.op]++;
    if (g_verbose) {
        printf("[bondcarA] case %d %s\n", index, kOpNames[c.op]);
        fflush(stdout);
    }
    RunOnce(c, &original, true);
    RunOnce(c, &ours, false);
    Put(g_live);
    if (original.faulted || ours.faulted) {
        g_faults++;
        if (original.faulted != ours.faulted) {
            g_differ++;
            g_opDiffer[c.op]++;
            Detail(c, index, "fault", 0, original.faulted, ours.faulted);
        }
        return;
    }
    // AddSnowmobileForces leaves the w of its torque local in the pad's +0x28c (the port's is zero)
    if (c.op == kSnowmobileForces || c.op == kSnowmobilePhysics)
        memcpy(ours.pad + 0x28c, original.pad + 0x28c, 4);
    // InitializeCarVariables (and ResetCar through it) copies a constructed WWorldPos temporary into each wheel's
    // record; the constructor leaves these bytes, so both copy their own stack
    if (c.op == kInitVariables || c.op == kResetCar) {
        static const struct { size_t offset, size; } kUnset[] = {{0x0c, 4}, {0x1c, 4}, {0x2e, 2}, {0x31, 3}};
        for (int wheel = 0; wheel < kCarWheels; wheel++) {
            size_t record = offsetof(PBondCar, wheels) + sizeof(WWorldPos) * size_t(wheel);
            for (const auto &unset : kUnset)
                memcpy(ours.car + record + unset.offset, original.car + record + unset.offset, unset.size);
        }
    }
    bool differ = false;
    CompareBlock(c, index, "car", original.car, ours.car, sizeof(original.car), &differ);
    CompareBlock(c, index, "physics", &original.physics, &ours.physics, sizeof(original.physics), &differ);
    CompareBlock(c, index, "audio", original.audio, ours.audio, sizeof(original.audio), &differ);
    CompareBlock(c, index, "action queue", original.queue, ours.queue, sizeof(original.queue), &differ);
    CompareBlock(c, index, "beacon", original.beacon, ours.beacon, sizeof(original.beacon), &differ);
    CompareBlock(c, index, "hit points", &original.hitPoints, &ours.hitPoints, 4, &differ);
    CompareBlock(c, index, "scratch pad", original.pad, ours.pad, sizeof(original.pad), &differ);
    CompareBlock(c, index, "sound", original.sound, ours.sound, sizeof(original.sound), &differ);
    CompareBlock(c, index, "arguments", &original.args, &ours.args, sizeof(original.args), &differ);
    const World &a = original.world, &b = ours.world;
    CompareBlock(c, index, "body", a.body, b.body, sizeof(a.body), &differ);
    CompareBlock(c, index, "body info", a.info, b.info, sizeof(a.info), &differ);
    CompareBlock(c, index, "live car", a.liveCar, b.liveCar, sizeof(a.liveCar), &differ);
    CompareBlock(c, index, "random", &a.random, &b.random, sizeof(a.random), &differ);
    CompareBlock(c, index, "barrier mask", &a.barrierMask, &b.barrierMask, 4, &differ);
    CompareBlock(c, index, "tuning", a.tuning, b.tuning, sizeof(a.tuning), &differ);
    CompareBlock(c, index, "damage threshold", &a.damageThreshold, &b.damageThreshold, 4, &differ);
    CompareBlock(c, index, "event queue", &a.creationPoint, &b.creationPoint, sizeof(a.creationPoint), &differ);
    CompareBlock(c, index, "calls made", &original.logCount, &ours.logCount, sizeof(original.logCount), &differ);
    CompareBlock(c, index, "calls", original.log, ours.log, sizeof(LogEntry) * size_t(original.logCount), &differ);
    CompareBlock(c, index, "result", &original.value, &ours.value, sizeof(original.value), &differ);
    if (differ) {
        g_differ++;
        g_opDiffer[c.op]++;
    }
}

// ---- building the cases

Case g_case;
PBondCar *g_source;

PBondCar *Edit() { return reinterpret_cast<PBondCar *>(g_case.car); }
RigidBody *EditBody() { return reinterpret_cast<RigidBody *>(g_case.world.body); }
RigidBodyInfo *EditInfo() { return reinterpret_cast<RigidBodyInfo *>(g_case.world.info); }
BondCarScratch *EditPad() { return reinterpret_cast<BondCarScratch *>(g_case.pad); }

Coord4 RandomVector(Rng *rng, float size, float w) {
    Coord4 v = {rng->Uniform(-size, size), rng->Uniform(-size, size), rng->Uniform(-size, size), w};
    return v;
}

void Start(Op op) {
    g_case.op = op;
    memcpy(g_case.car, g_source, sizeof(g_case.car));
    g_case.physics = *g_source->physics;
    memset(g_case.audio, 0, sizeof(g_case.audio));
    if (g_source->audio != NULL)
        memcpy(g_case.audio, g_source->audio, sizeof(g_case.audio));
    memset(g_case.queue, 0, sizeof(g_case.queue));
    g_case.hasQueue = g_source->actionQueue != NULL;
    if (g_case.hasQueue)
        memcpy(g_case.queue, g_source->actionQueue, sizeof(g_case.queue));
    memset(g_case.beacon, 0, sizeof(g_case.beacon));
    g_case.hasBeacon = g_source->targetBeacon != NULL;
    if (g_case.hasBeacon)
        memcpy(g_case.beacon, g_source->targetBeacon, sizeof(g_case.beacon));
    g_case.hitPoints = g_source->hitPointLoc != NULL ? *g_source->hitPointLoc : 1.0f;
    memset(g_case.pad, 0, sizeof(g_case.pad));
    memset(g_case.sound, 0, sizeof(g_case.sound));
    memset(&g_case.args, 0, sizeof(g_case.args));
    g_case.world = g_live;
    g_case.hasRender = true;
    g_case.clearTracks = false;
    g_case.i0 = 0;
    g_case.f0 = 0.0f;
}

// The car's driving state shaken: controls, speed, wheels, shock, the body's motion
void Perturb(Rng *rng) {
    PBondCar *car = Edit();
    car->control.steering = rng->Uniform(-1.5f, 1.5f);
    car->control.gas = rng->Chance(30) ? 0.0f : rng->Uniform(0.0f, 1.0f);
    car->control.brake = rng->Chance(50) ? 0.0f : rng->Uniform(0.0f, 1.0f);
    car->control.handBrake = rng->Chance(25);
    car->control.gear = rng->Chance(20);
    car->carSpeed = rng->Chance(10) ? rng->Uniform(0.0f, 0.2f) : rng->Uniform(0.0f, 30.0f);
    car->numWheelsOnGround = int8_t(rng->Range(0, 4));
    car->inShock = int8_t(rng->Chance(70) ? 0 : rng->Range(1, 5));
    car->carClass = rng->Range(0, 3);
    car->landingFlag = uint8_t(rng->Range(0, 30));
    car->unknown2EC = rng->Uniform(0.0f, 1.5f);
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        car->suspensionCompression[wheel] = rng->Chance(30) ? 0.0f : rng->Uniform(0.0f, 0.5f);
        car->wheelRoadNormal[wheel].w = rng->Uniform(-0.6f, 0.4f);
    }
    car->wheelSpinAngle[1] = rng->Uniform(-0.5f, 1.5f);
    RigidBody *body = EditBody();
    body->velocity.x += rng->Uniform(-5.0f, 5.0f);
    body->velocity.y += rng->Uniform(-2.0f, 2.0f);
    body->velocity.z += rng->Uniform(-5.0f, 5.0f);
    body->angularVelocity.x += rng->Uniform(-1.0f, 1.0f);
    body->angularVelocity.y += rng->Uniform(-1.0f, 1.0f);
    body->angularVelocity.z += rng->Uniform(-1.0f, 1.0f);
    body->angularMomentum.x += rng->Uniform(-50.0f, 50.0f);
    body->angularMomentum.z += rng->Uniform(-50.0f, 50.0f);
    RigidBodyInfo *info = EditInfo();
    info->unknown4fd = rng->Chance(15);
    info->unknown4fe = rng->Chance(15);
    info->unknown4de = uint16_t(rng->Chance(50) ? rng->Range(0, 49) : rng->Range(51, 200));
    if (rng->Chance(10))
        body->flags ^= RigidBody::kFlag0;
}

const char kType750[] = "750";
const char kTypeBombVan[] = "paris_bombvan";
const char kTypeSmallSnowmobile[] = "small_snowmobile";

void CarCases(Rng *rng, int perOp) {
    for (int op = kGetPhysics; op <= kSnowmobilePhysics; op++) {
        if (op == kGlobals || op == kSetOrientation || (op == kFlushControls && g_source->actionQueue == NULL))
            continue;
        for (int n = 0; n < perOp; n++) {
            Start(Op(op));
            PBondCar *car = Edit();
            if (n > 0)
                Perturb(rng);
            g_case.hasRender = !(op == kResetDamage && rng->Chance(25));
            switch (op) {
            case kInitTyreTracks:
            case kResetCar:
                g_case.clearTracks = true;
                car->carClass = rng->Range(0, 4);
                if (rng->Chance(30))
                    car->carType = kType750;
                g_case.physics.isSnowmobile = rng->Chance(20);
                g_case.physics.subPhysics = rng->Chance(15);
                if (op == kResetCar) {
                    g_case.args.position = EditBody()->position;
                    g_case.args.position.x += rng->Uniform(-3.0f, 3.0f);
                    g_case.args.position.y += rng->Uniform(-1.0f, 3.0f);
                    g_case.args.position.z += rng->Uniform(-3.0f, 3.0f);
                    g_case.args.direction.x = rng->Uniform(-1.0f, 1.0f);
                    g_case.args.direction.y = rng->Uniform(-0.1f, 0.1f);
                    g_case.args.direction.z = rng->Uniform(-1.0f, 1.0f);
                }
                break;
            case kInitVariables:
                g_case.i0 = rng->Chance(50);
                break;
            case kAddDamageByPlayer:
                g_case.f0 = rng->Uniform(0.0f, 300.0f);
                car->damageByPlayer = rng->Uniform(0.0f, 600.0f);
                g_case.hitPoints = rng->Uniform(-10.0f, 500.0f);
                break;
            case kGetNumTires:
                for (int wheel = 0; wheel < kCarWheels; wheel++)
                    car->tyreDamagePoints[wheel] = rng->Chance(30) ? 0 : uint32_t(rng->Range(1, 100));
                break;
            case kSetInShock:
                g_case.f0 = rng->Uniform(-0.5f, 2.5f);
                break;
            case kCalculateRPM:
                g_case.physics.subPhysics = rng->Chance(10);
                car->gearChangeTimer = rng->Chance(50);
                car->previousGear = int8_t(rng->Chance(50) ? -1 : rng->Range(0, 5));
                car->unknown238 = rng->Uniform(0.0f, 1.0f);
                break;
            case kFireLaser:
                car->laserActive = rng->Chance(50);
                break;
            case kAttackWithEmp:
                car->empActive = rng->Chance(30);
                if (rng->Chance(30))
                    car->carType = kTypeBombVan;
                break;
            case kGetSplinePath:
                if (rng->Chance(50))
                    car->aiGroundVehicle = NULL;
                break;
            case kEnableBeacon:
            case kDisableBeacon:
                g_case.hasBeacon = rng->Chance(70);
                g_case.beacon[0x40] = uint8_t(rng->Chance(50));
                g_case.i0 = g_case.hasBeacon ? rng->Chance(50) : 0;   // no beacon and enabled: not covered
                break;
            case kGlareOn:
            case kGlareOff:
                g_case.i0 = rng->Range(0, 15);
                car->glareOn[g_case.i0] = uint8_t(rng->Range(0, 2));
                break;
            case kTurnSignals:
                car->turnSignalMode = rng->Range(0, 3);
                car->turnSignalPeriod = rng->Range(1, 90);
                car->turnSignalStart = rng->Range(0, 100000);
                car->turnSignalLit = rng->Uniform(0.0f, 1.0f);
                car->turnSignalSteering = rng->Uniform(-1.0f, 1.0f);
                for (int glare = 3; glare <= 4; glare++)
                    car->glareOn[glare] = uint8_t(rng->Range(0, 1));
                break;
            case kRocketBoost:
                car->rocketBoost = rng->Range(0, 400);
                break;
            case kImproveLanding:
                EditPad()->forceInfo = g_info;
                g_case.physics.numBlendSteps = rng->Range(1, 10);
                g_case.physics.noseJumpAngle = rng->Uniform(-0.1f, 0.1f);
                if (rng->Chance(15))
                    EditInfo()->orientation.mtx[1][1] = rng->Uniform(0.0f, 0.4f);
                break;
            case kSnowmobileForces: {
                Args &a = g_case.args;
                for (int wheel = 0; wheel < kCarWheels; wheel++) {
                    a.inputs[wheel] = {rng->Uniform(0.0f, 8.0f), rng->Uniform(-50.0f, 50.0f),
                                        rng->Uniform(5.0f, 25.0f), rng->Chance(40) ? 0.0f : rng->Uniform(0.0f, 2.0f)};
                    a.normals[wheel] = car->wheelRoadNormal[wheel];
                    a.normals[wheel].x += rng->Uniform(-0.2f, 0.2f);
                    a.normals[wheel].z += rng->Uniform(-0.2f, 0.2f);
                    a.wheels[wheel] = car->wheelPos[wheel];
                    a.wheels[wheel].y += rng->Uniform(-0.3f, 0.3f);
                }
                for (int axis = 0; axis < 2; axis++)
                    a.axes[axis] = RandomVector(rng, 1.0f, 0.0f);
                if (rng->Chance(50))
                    car->carType = kTypeSmallSnowmobile;
                break;
            }
            case kSnowmobilePhysics:
                EditPad()->rollingResistance = {rng->Uniform(-5.0f, 5.0f), rng->Uniform(-5.0f, 5.0f),
                                                rng->Uniform(-5.0f, 5.0f)};
                if (rng->Chance(70)) {
                    // pitched about its own x axis (the live cars start level, their heading's y zero)
                    MATRIX4 &m = EditInfo()->orientation;
                    const float angle = rng->Uniform(-0.5f, 0.5f), c = cosf(angle), s = sinf(angle);
                    for (int column = 0; column < 3; column++) {
                        const float up = m.mtx[1][column], ahead = m.mtx[2][column];
                        m.mtx[1][column] = c * up + s * ahead;
                        m.mtx[2][column] = c * ahead - s * up;
                    }
                }
                if (rng->Chance(50))
                    car->carType = kTypeSmallSnowmobile;
                break;
            default:
                break;
            }
            RunCase(g_case);
        }
    }
}

void SoundCases(Rng *rng) {
    for (int n = 0; n < 16; n++) {
        Start(kSetOrientation);
        for (size_t i = 0; i < kSoundSize; i++)
            g_case.sound[i] = uint8_t(rng->Next());
        g_case.args.axes[0] = RandomVector(rng, 1.0f, 0.0f);
        g_case.args.axes[1] = RandomVector(rng, 1.0f, 0.0f);
        g_case.args.point = RandomVector(rng, 1.0f, 0.0f);
        RunCase(g_case);
    }
}

void ZoneCases(Rng *rng) {
    for (int zone = 0; zone < kSoftZones; zone++) {
        const SoftZone &softZone = SoftZones[zone];
        for (int n = 0; n < 40; n++) {
            Start(kSoftZone);
            const Coord4 &a = softZone.corners[rng->Range(0, 3)];
            const Coord4 &b = softZone.corners[rng->Range(0, 3)];
            float t = rng->Uniform(-0.2f, 1.2f);
            g_case.args.point = {a.x + (b.x - a.x) * t + rng->Uniform(-20.0f, 20.0f),
                                 a.y - softZone.plane.y * rng->Uniform(-0.2f, 1.2f) * softZone.plane.w,
                                 a.z + (b.z - a.z) * t + rng->Uniform(-20.0f, 20.0f), 1.0f};
            RunCase(g_case);
            g_case.op = kInsideQuad;
            memcpy(g_case.args.quad, softZone.corners, sizeof(g_case.args.quad));
            g_case.args.plane = softZone.plane;
            if (rng->Chance(30))
                g_case.args.plane = RandomVector(rng, 1.0f, 0.0f);
            RunCase(g_case);
        }
    }
}

// ---- NIGHTFIRE_BONDCARSHADOWA=3: live
//
// The port's ProcessSnowmobilePhysics entry jumps here; each call runs the original (with every V_A function it
// reaches - AddSnowmobileForces, ImproveLanding - as shipped) and then the port, on the car's state exactly as the
// game hands it over, compares the car, its body and body info, its CarPhysics record, the scratch pad and the
// landing impacts raised, and leaves the port's result (so the game goes on as the port build does). The
// original's impact event is dropped; the port's is constructed.

namespace live {

constexpr unsigned kOriginalLo = 0x00062fe0, kOriginalHi = 0x000643b0;   // ImproveLanding to the snowmobile physics
constexpr uint32_t kOriginalSnowmobile = 0x00063ad0;
constexpr uint32_t kECollision = 0x0003f370;
constexpr size_t kPadAxleW = 0x28c;     // AddSnowmobileForces' torque local's w: the original's stack, ours zero
constexpr int kMaxImpacts = 4;

#define LiveSimStepCount (*(const int32_t *)0x00234e34)

typedef void *(__fastcall *ECollisionFn)(void *, int, CollisionImpact);

struct Patch5 {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Patch5 g_snowHook, g_eventHook;

void Patch(Patch5 *hook, const void *to) {
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)hook->at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(hook->saved, (void *)(uintptr_t)hook->at, 5);
    uint8_t jump[5] = {0xe9};
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (hook->at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)hook->at, jump, 5);
    VirtualProtect((void *)(uintptr_t)hook->at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)hook->at, 5);
    hook->on = true;
}

void Unpatch(Patch5 *hook) {
    if (!hook->on)
        return;
    DWORD old;
    VirtualProtect((void *)(uintptr_t)hook->at, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)(uintptr_t)hook->at, hook->saved, 5);
    VirtualProtect((void *)(uintptr_t)hook->at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)hook->at, 5);
    hook->on = false;
}

struct Impacts {
    int count;
    CollisionImpact impact[kMaxImpacts];
};

struct State {
    alignas(16) uint8_t car[sizeof(PBondCar)];
    alignas(16) uint8_t body[sizeof(RigidBody)];
    alignas(16) uint8_t info[sizeof(RigidBodyInfo)];
    CarPhysics physics;
    alignas(16) uint8_t pad[sizeof(BondCarScratch)];
    char *creationPoint;
    Impacts impacts;
};

PBondCar *g_car;
RigidBody *g_body;
State g_start, g_result[2];
Impacts *g_impacts;         // the run's record
bool g_construct;           // the port's run: the event constructed after it is recorded
unsigned int g_x87, g_sse;
bool g_on, g_done;
int g_ticks, g_tickLimit = 7000;
int g_calls, g_differ, g_reported, g_faults, g_impactCalls, g_airborneCalls;

void Take(State *s) {
    memcpy(s->car, g_car, sizeof(s->car));
    memcpy(s->body, g_body, sizeof(s->body));
    memcpy(s->info, g_body->info, sizeof(s->info));
    s->physics = *g_car->physics;
    memcpy(s->pad, BondCarScratchPad, sizeof(s->pad));
    s->creationPoint = ShadowCreationPoint;
}

void Put(const State &s) {
    RigidBodyInfo *info = g_body->info;
    CarPhysics *physics = g_car->physics;
    memcpy(g_car, s.car, sizeof(s.car));
    memcpy(g_body, s.body, sizeof(s.body));
    memcpy(info, s.info, sizeof(s.info));
    *physics = s.physics;
    memcpy(BondCarScratchPad, s.pad, sizeof(s.pad));
    ShadowCreationPoint = s.creationPoint;
}

void *__fastcall RecordECollision(void *self, int, CollisionImpact impact) {
    if (g_impacts != NULL) {
        if (g_impacts->count < kMaxImpacts) {
            CollisionImpact &kept = g_impacts->impact[g_impacts->count];
            kept = impact;
            memset(kept.unknown38, 0, sizeof(kept.unknown38));     // left by the impact's constructor
        }
        g_impacts->count++;
    }
    if (!g_construct)
        return self;
    Unpatch(&g_eventHook);
    ((ECollisionFn)(uintptr_t)kECollision)(self, 0, impact);
    Patch(&g_eventHook, (const void *)&RecordECollision);
    return self;
}

bool RunOriginal(PBondCar *car) {
#ifdef _MSC_VER
    __try {
        ((PlainFn)(uintptr_t)kOriginalSnowmobile)(car, 0);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        _fpreset();
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    ((PlainFn)(uintptr_t)kOriginalSnowmobile)(car, 0);
    return true;
#endif
}

void PrintFloat(const char *name, float f) {
    printf(" %s=%.9g(%08x)", name, f, std::bit_cast<uint32_t>(f));
}

void PrintVector(const char *name, const float *f, int n) {
    printf("[bondcarA-live]     %s", name);
    for (int i = 0; i < n; i++)
        printf(" %.9g(%08x)", f[i], std::bit_cast<uint32_t>(f[i]));
    printf("\n");
}

// Every differing 32-bit word of a region, the first 12
void PrintRegion(const char *region, const void *a, const void *b, size_t bytes) {
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    int shown = 0, count = 0;
    for (size_t at = 0; at + 4 <= bytes; at += 4) {
        if (memcmp(x + at, y + at, 4) == 0)
            continue;
        count++;
        if (shown++ >= 12)
            continue;
        uint32_t u, v;
        memcpy(&u, x + at, 4);
        memcpy(&v, y + at, 4);
        printf("[bondcarA-live]     %s +0x%03x: original %08x (%.9g), port %08x (%.9g)\n", region, unsigned(at), u,
               std::bit_cast<float>(u), v, std::bit_cast<float>(v));
    }
    if (count > 12)
        printf("[bondcarA-live]     %s: %d words differ in all\n", region, count);
}

// What the snowmobile physics reads, from the state the call started with
void PrintInputs(const State &s) {
    const PBondCar *car = reinterpret_cast<const PBondCar *>(s.car);
    const RigidBody *body = reinterpret_cast<const RigidBody *>(s.body);
    const RigidBodyInfo *info = reinterpret_cast<const RigidBodyInfo *>(s.info);
    const BondCarControl &c = car->control;
    printf("[bondcarA-live]   inputs:");
    PrintFloat("steering", c.steering);
    PrintFloat("gas", c.gas);
    PrintFloat("brake", c.brake);
    printf(" handBrake=%d class=%d inShock=%d landingFlag=%d wheelsOnGround=%d type=%s\n", c.handBrake,
           car->carClass, car->inShock, car->landingFlag, car->numWheelsOnGround,
           car->carType != NULL ? car->carType : "(none)");
    printf("[bondcarA-live]    ");
    PrintFloat("carSpeed", car->carSpeed);
    PrintFloat("unknown2EC", car->unknown2EC);
    PrintFloat("spinAngle1", car->wheelSpinAngle[1]);
    PrintFloat("hitPoints", car->hitPointLoc != NULL ? *car->hitPointLoc : 1.0f);
    printf(" unknown4de=%u unknown4fd=%d unknown4fe=%d groundContacts=%d flags=%08x\n", info->unknown4de,
           info->unknown4fd, info->unknown4fe, body->groundContacts, unsigned(body->flags));
    PrintVector("suspension", car->suspensionCompression, kCarWheels);
    PrintVector("position", &body->position.x, 3);
    PrintVector("velocity", &body->velocity.x, 3);
    PrintVector("angularVelocity", &body->angularVelocity.x, 3);
    PrintVector("angularMomentum", &body->angularMomentum.x, 3);
    PrintVector("force", &body->force.x, 3);
    for (int row = 0; row < 3; row++) {
        char name[24];
        snprintf(name, sizeof(name), "orientation[%d]", row);
        PrintVector(name, info->orientation.mtx[row], 4);
    }
}

void __fastcall LiveSnowmobile(PBondCar *car, int) {
    g_car = car;
    g_body = car->GetRigidBody();
    Take(&g_start);
    Patch(&g_eventHook, (const void *)&RecordECollision);

    g_impacts = &g_result[0].impacts;
    g_impacts->count = 0;
    g_construct = false;
    XbeOriginal_RestoreRange(kOriginalLo, kOriginalHi, true);
    const bool ran = RunOriginal(car);
    XbeOriginal_RestoreRange(kOriginalLo, kOriginalHi, false);
    Take(&g_result[0]);
    Put(g_start);

    g_impacts = &g_result[1].impacts;
    g_impacts->count = 0;
    g_construct = true;
    Unpatch(&g_snowHook);
    car->ProcessSnowmobilePhysics();
    Patch(&g_snowHook, (const void *)&LiveSnowmobile);
    g_impacts = NULL;
    Unpatch(&g_eventHook);
    Take(&g_result[1]);

    g_calls++;
    g_faults += !ran;
    g_impactCalls += g_result[1].impacts.count != 0;
    g_airborneCalls += car->numWheelsOnGround == 0;
    State &a = g_result[0], &b = g_result[1];
    memcpy(b.pad + kPadAxleW, a.pad + kPadAxleW, 4);
    const int impacts = a.impacts.count < kMaxImpacts ? a.impacts.count : kMaxImpacts;
    const bool same = ran && memcmp(a.car, b.car, sizeof(a.car)) == 0 &&
                      memcmp(a.body, b.body, sizeof(a.body)) == 0 && memcmp(a.info, b.info, sizeof(a.info)) == 0 &&
                      memcmp(&a.physics, &b.physics, sizeof(a.physics)) == 0 &&
                      memcmp(a.pad, b.pad, sizeof(a.pad)) == 0 && a.impacts.count == b.impacts.count &&
                      memcmp(a.impacts.impact, b.impacts.impact, sizeof(CollisionImpact) * size_t(impacts)) == 0;
    if (same)
        return;
    g_differ++;
    if (g_reported++ < 20)
        printf("[bondcarA-live] ProcessSnowmobilePhysics differs: tick %d (shadow tick %d), car %p%s\n",
               LiveSimStepCount, g_ticks, (void *)car, ran ? "" : " - the original faulted");
    if (g_reported <= 3) {
        PrintInputs(g_start);
        PrintRegion("car", a.car, b.car, sizeof(a.car));
        PrintRegion("body", a.body, b.body, sizeof(a.body));
        PrintRegion("info", a.info, b.info, sizeof(a.info));
        PrintRegion("physics", &a.physics, &b.physics, sizeof(a.physics));
        PrintRegion("scratch pad", a.pad, b.pad, sizeof(a.pad));
        if (a.impacts.count != b.impacts.count)
            printf("[bondcarA-live]     impacts: original %d, port %d\n", a.impacts.count, b.impacts.count);
        else
            PrintRegion("impacts", a.impacts.impact, b.impacts.impact, sizeof(CollisionImpact) * size_t(impacts));
    }
    fflush(stdout);
}

int Mode() {
    const char *setting = getenv("NIGHTFIRE_BONDCARSHADOWA");
    return setting != NULL ? atoi(setting) : 0;
}

void Tick() {
    if (g_done)
        return;
    if (!g_on) {
        if (Mode() != 3) {
            g_done = true;
            return;
        }
        const char *ticks = getenv("NIGHTFIRE_BONDCARSHADOWA_TICKS");
        if (ticks != NULL && atoi(ticks) > 0)
            g_tickLimit = atoi(ticks);
        FpControlGet(&g_x87, &g_sse);
        g_snowHook.at = uint32_t(XbeAddress(&PBondCar::ProcessSnowmobilePhysics));
        g_eventHook.at = kECollision;
        Patch(&g_snowHook, (const void *)&LiveSnowmobile);
        g_on = true;
        printf("[bondcarA-live] comparing every snowmobile physics call for %d ticks from tick %d\n", g_tickLimit,
               LiveSimStepCount);
        fflush(stdout);
    }
    if (++g_ticks < g_tickLimit)
        return;
    Unpatch(&g_snowHook);
    g_done = true;
    printf("[bondcarA-live] %d ticks to tick %d: %d calls (%d airborne, %d with a landing impact), %d differ%s\n",
           g_ticks, LiveSimStepCount, g_calls, g_airborneCalls, g_impactCalls, g_differ,
           g_faults != 0 ? " (the original faulted)" : "");
    fflush(stdout);
}

} // namespace live

} // namespace

void BondCarShadowA_Tick(void) {
    live::Tick();
}

void BondCarShadowA_Run(void) {
    const char *setting = getenv("NIGHTFIRE_BONDCARSHADOWA");
    if (setting == NULL || atoi(setting) == 0 || atoi(setting) == 3)   // 3: the live comparison only (_Tick)
        return;
    g_verbose = atoi(setting) >= 2;
    g_cases = g_checks = g_differ = g_faults = g_details = 0;
    memset(g_opCases, 0, sizeof(g_opCases));
    memset(g_opDiffer, 0, sizeof(g_opDiffer));
    memset(g_opDetails, 0, sizeof(g_opDetails));

    g_fakeRender.vtable = g_fakeRenderVtable;
    g_fakeRenderVtable[14] = (void *)&FakeUpdatePosition;
    g_fakeRenderVtable[15] = (void *)&FakeTriggerFX;
    InstallFakes();
    Rng rng = {0x5eed0a11u};

    // the cars-free cases first, with no car's body in the world
    g_body = NULL;
    g_info = NULL;
    g_liveCar = NULL;
    Take(&g_live);
    g_source = NULL;
    PBondCar *player = ShadowPlayerCar;
    int cars = 0;
    for (int slot = 0; slot < 64; slot++) {
        PhysicsObject *owner = PhysicsObjects[slot];
        if (owner == NULL || uint32_t(uintptr_t(owner->vtable)) != kPBondCarVtable)
            continue;
        PBondCar *car = static_cast<PBondCar *>(static_cast<PVehicle *>(owner));
        if (car->physics == NULL)
            continue;
        g_source = car;
        g_liveCar = car;
        g_body = car->GetRigidBody();
        g_info = g_body->info;
        Take(&g_live);
        CarCases(&rng, car == player ? 12 : 6);
        Put(g_live);
        cars++;
        if (cars == 1) {
            SoundCases(&rng);
            ZoneCases(&rng);
            Start(kGlobals);
            RunCase(g_case);
        }
    }
    HooksRemove();
    printf("[bondcarA] V_A package on %d cars: %d cases, %d checks, %d differ, %d faulted\n", cars, g_cases,
           g_checks, g_differ, g_faults);
    for (int op = 0; op < kOpCount; op++) {
        if (g_opDiffer[op] != 0)
            printf("[bondcarA]   %s: %d of %d cases differ\n", kOpNames[op], g_opDiffer[op], g_opCases[op]);
    }
    fflush(stdout);
}
