#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "BondCarShadowD.h"

#include "../anim/AnimEngine.h"
#include "../audio/Sound.h"
#include "../camera/PlayerCamera.h"
#include "../engine/SimRandom.h"
#include "../game/BondCar.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../render/RSceneObj.hpp"
#include "../world/Targeting.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_BONDCARSHADOWD=1, once on the first simulation tick: package V_D's Simulate (0x0006a840) and
// ApplyDamage (0x0006b6f0) against the originals.
//
// Each case starts from a copy of a live PBondCar (every car in the Simulation's table with PBondCar's vtable),
// perturbed, whose vtable, CarPhysics, render object, engine sound, target beacon, AI vehicle, hit points and shield
// are copies of our own. The car's vtable copy records slots 2 (ApplyDamage, in the Simulate cases), 14
// (DisableTargetBeacon), 23 (GetControllerInput), 43 (ResetCar), 45 (GlareOn) and 51 (SetVisualDamage); the render
// copy's slots 1 (SetViewDrawList) and 15 (TriggerFX), and the sound copy's slot 4. The case runs twice from the
// same bytes - first with V_D's originals swapped back in, then with our jumps - calling the original address both
// times. Before each run everything a run can write is put back: the copies, the car's live rigid body and its
// RigidBodyInfo, and the simulation's random generator; all are compared afterwards, with the result and the calls.
//
// The calls beyond the car are replaced for both runs by fakes that record their arguments (pointed-to vectors and
// strings by content): RVehicle::SetEMPVictimEffect, HandleTurnSignals, GlareOff, the five Process*Physics (they
// also record that the scratch pad was set; it is never read), ControlTyreTracks, PhysicsObject::Simulate,
// WTargetable::UpdatePosition, RPlayerCamera::GetForwardAimVec4, AIVehicle::GetSplinePath, the AI controllers'
// Get, FindAgentGroundVehiclePtr and ForceVehicleToSleep, the mission manager's IncKills, IncShotsHit and
// IncPlayerDamage, GHud's TheApp and TriggerDamageFlash, ABaseSound::operator new (a buffer of ours, compared),
// AOneShotSound's constructor, and the animation handle's ProcessStimuli (both), ProcessStimuliZones,
// GetSystemState and AnySystemPlaying (their answers chosen per case).
//
// Not covered: the constructor and InitAudioObject (they allocate and construct render, audio, AI and feedback
// objects; tested in game), and the Process*Physics themselves (packages V_A to V_C).
//
// A mutation this catches: Simulate's brake-light test written `speed > 0.0f` - every player case with the brake on
// records a different GlareOn/GlareOff call for glare 5.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr unsigned kRangeLo = 0x0006a840, kRangeHi = 0x0006cc20;
constexpr size_t kRenderSize = 0x370;       // RVehicle
constexpr size_t kAudioSize = 0x190;        // the engine sound, as far as Simulate writes it
constexpr size_t kAIVehicleSize = 0x90;
constexpr int kCarSlots = 84;
constexpr int kRenderSlots = 19;
constexpr int kAudioSlots = 8;

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
#define ShadowRandom (*(SimRandom **)0x00233ff0)
#define ShadowDamageSourceSig U32_AT(0x00233fe8)

// ---- five-byte jumps over the callees the fakes stand in for

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[48];
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
    uint32_t words[12];
};
constexpr int kMaxLog = 64;
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

uint32_t Word(const void *p) { return uint32_t(uintptr_t(p)); }
uint32_t Bits(float f) {
    uint32_t b;
    memcpy(&b, &f, 4);
    return b;
}
void LogVector(uint32_t *words, const Coord3 *v) {
    if (v != NULL)
        memcpy(words, v, sizeof(Coord3));
}
void LogString(uint32_t *words, const char *text, size_t bytes) {
    if (text != NULL)
        strncpy(reinterpret_cast<char *>(words), text, bytes);
}

// What the fakes answer, chosen per case
void *g_splinePath;
void *g_agent;
uint32_t g_systemState;
uint32_t g_anyPlaying;
Coord4 g_aim;
alignas(16) uint8_t g_soundBlock[0x120];

void __fastcall FakeEmpVictim(void *self, int, int on) {
    if (LogEntry *e = Log(1)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(on) & 0xff;
    }
}
void __fastcall FakeTurnSignals(void *self, int) {
    if (LogEntry *e = Log(2))
        e->words[0] = Word(self);
}
void __fastcall FakeGlareOff(void *self, int, int glare) {
    if (LogEntry *e = Log(3)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(glare);
    }
}
void __fastcall FakeGlareOn(void *self, int, int glare) {
    if (LogEntry *e = Log(4)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(glare);
    }
}
void LogProcess(uint32_t id, void *self) {
    if (LogEntry *e = Log(id)) {
        BondCarScratch *pad = BondCarScratchPad;
        e->words[0] = Word(self);
        e->words[1] = pad != NULL && (uintptr_t(pad) & 15) == 0;
    }
}
void __fastcall FakeSpline(void *self, int) { LogProcess(5, self); }
void __fastcall FakeSubmarine(void *self, int) { LogProcess(6, self); }
void __fastcall FakeSnowmobile(void *self, int) { LogProcess(7, self); }
void __fastcall FakePhysics(void *self, int) { LogProcess(8, self); }
void __fastcall FakeSimplePhysics(void *self, int) { LogProcess(9, self); }
void __fastcall FakeTyreTracks(void *self, int) {
    if (LogEntry *e = Log(10))
        e->words[0] = Word(self);
}
void __fastcall FakeObjectSimulate(void *self, int) {
    if (LogEntry *e = Log(11))
        e->words[0] = Word(self);
}
void __fastcall FakeUpdatePosition(void *self, int) {
    if (LogEntry *e = Log(12))
        e->words[0] = Word(self);
}
const Coord4 *__fastcall FakeAimVec(void *self, int, float blend) {
    if (LogEntry *e = Log(13)) {
        e->words[0] = Word(self);
        e->words[1] = Bits(blend);
    }
    return &g_aim;
}
void *__fastcall FakeSplinePath(void *self, int) {
    if (LogEntry *e = Log(14))
        e->words[0] = Word(self);
    return g_splinePath;
}
void *FakeControllerGet() {
    Log(15);
    return (void *)0x00001234;
}
void *__fastcall FakeFindAgent(void *self, int, void *vehicle) {
    if (LogEntry *e = Log(16)) {
        e->words[0] = Word(self);
        e->words[1] = Word(vehicle);
    }
    return g_agent;
}
void __fastcall FakeSleep(void *self, int, void *vehicle) {
    if (LogEntry *e = Log(17)) {
        e->words[0] = Word(self);
        e->words[1] = Word(vehicle);
    }
}
void __fastcall FakeIncKills(void *self, int, int kills) {
    if (LogEntry *e = Log(18)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(kills);
    }
}
void __fastcall FakeIncShotsHit(void *self, int, int hit) {
    if (LogEntry *e = Log(19)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(hit) & 0xff;
    }
}
void __fastcall FakeIncPlayerDamage(void *self, int, float damage) {
    if (LogEntry *e = Log(20)) {
        e->words[0] = Word(self);
        e->words[1] = Bits(damage);
    }
}
void *FakeTheApp() {
    Log(21);
    return (void *)0x00005678;
}
void __fastcall FakeDamageFlash(void *self, int, float damage) {
    if (LogEntry *e = Log(22)) {
        e->words[0] = Word(self);
        e->words[1] = Bits(damage);
    }
}
void *FakeOperatorNew(unsigned int size, const char *name) {
    if (LogEntry *e = Log(23)) {
        e->words[0] = size;
        LogString(e->words + 1, name, 16);
    }
    return g_soundBlock;
}
void *__fastcall FakeOneShot(void *self, int, int bank, int patch, const Coord3 *position, const Coord3 *velocity,
                             float volume, const char *mix) {
    if (LogEntry *e = Log(24)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(bank);
        e->words[2] = uint32_t(patch);
        LogVector(e->words + 3, position);
        LogVector(e->words + 6, velocity);
        e->words[9] = Bits(volume);
        LogString(e->words + 10, mix, 8);
    }
    return self;
}
void __fastcall FakeStimuliId(void *self, int, uint32_t id, int stimulus, uint32_t tick, int mode) {
    if (LogEntry *e = Log(25)) {
        e->words[0] = Word(self);
        e->words[1] = id;
        e->words[2] = uint32_t(stimulus) & 0xff;
        e->words[3] = tick;
        e->words[4] = uint32_t(mode);
    }
}
void __fastcall FakeStimuli(void *self, int, int stimulus, uint32_t tick, int mode) {
    if (LogEntry *e = Log(26)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(stimulus) & 0xff;
        e->words[2] = tick;
        e->words[3] = uint32_t(mode);
    }
}
void __fastcall FakeStimuliZones(void *self, int, int stimulus, int zones, uint32_t tick, int mode) {
    if (LogEntry *e = Log(27)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(stimulus) & 0xff;
        e->words[2] = uint32_t(zones) & 0xffff;
        e->words[3] = tick;
        e->words[4] = uint32_t(mode);
    }
}
uint32_t __fastcall FakeSystemState(void *self, int, uint32_t id) {
    if (LogEntry *e = Log(28)) {
        e->words[0] = Word(self);
        e->words[1] = id;
    }
    return g_systemState;
}
uint32_t __fastcall FakeAnyPlaying(void *self, int) {
    if (LogEntry *e = Log(29))
        e->words[0] = Word(self);
    return g_anyPlaying;
}

// The copies' vtable slots
int __fastcall FakeApplyDamageSlot(void *self, int, const Coord3 *from, const Coord3 *to, float amount, float split,
                                   int kind, const uint32_t *sig) {
    if (LogEntry *e = Log(30)) {
        e->words[0] = Word(self);
        LogVector(e->words + 1, from);
        LogVector(e->words + 4, to);
        e->words[7] = Bits(amount);
        e->words[8] = Bits(split);
        e->words[9] = uint32_t(kind);
        e->words[10] = Word(sig);
    }
    return 0x10;
}
void __fastcall FakeDisableBeacon(void *self, int) {
    if (LogEntry *e = Log(31))
        e->words[0] = Word(self);
}
void __fastcall FakeControllerInput(void *self, int) {
    if (LogEntry *e = Log(32))
        e->words[0] = Word(self);
}
void __fastcall FakeResetCar(void *self, int, int unknown) {
    if (LogEntry *e = Log(33)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(unknown) & 0xff;
    }
}
void __fastcall FakeVisualDamage(void *self, int, float a, float b, float c, float d) {
    if (LogEntry *e = Log(34)) {
        e->words[0] = Word(self);
        e->words[1] = Bits(a);
        e->words[2] = Bits(b);
        e->words[3] = Bits(c);
        e->words[4] = Bits(d);
    }
}
void __fastcall FakeDrawList(void *self, int, int list) {
    if (LogEntry *e = Log(35)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(list);
    }
}
void __fastcall FakeTriggerFX(void *self, int, int type, uint32_t which, uint32_t unused, const Coord3 *a,
                              const Coord3 *b, float intensity) {
    if (LogEntry *e = Log(36)) {
        e->words[0] = Word(self);
        e->words[1] = uint32_t(type);
        e->words[2] = which;
        e->words[3] = unused;
        e->words[4] = Word(a);
        e->words[5] = Word(b);
        e->words[6] = Bits(intensity);
    }
}
void __fastcall FakeAudioSlot4(void *self, int, float volume, const Coord3 *position, const Coord3 *velocity) {
    if (LogEntry *e = Log(37)) {
        e->words[0] = Word(self);
        e->words[1] = Bits(volume);
        LogVector(e->words + 2, position);
        LogVector(e->words + 5, velocity);
    }
}

void InstallFakes() {
    typedef void StimuliId(uint32_t, uint8_t, uint32_t, int);
    typedef void Stimuli(uint8_t, uint32_t, int);
    HookInstall(0x000959d0, (const void *)&FakeEmpVictim);
    HookBoth(0x00062de0, XbeAddress(&PBondCar::HandleTurnSignals), (const void *)&FakeTurnSignals);
    HookBoth(0x00062d70, XbeAddress(&PBondCar::GlareOff), (const void *)&FakeGlareOff);
    HookBoth(0x00065740, XbeAddress(&PBondCar::ProcessSplinePhysics), (const void *)&FakeSpline);
    HookBoth(0x000644c0, XbeAddress(&PBondCar::ProcessSubmarinePhysics), (const void *)&FakeSubmarine);
    HookBoth(0x00063ad0, XbeAddress(&PBondCar::ProcessSnowmobilePhysics), (const void *)&FakeSnowmobile);
    HookBoth(0x00067bd0, XbeAddress(&PBondCar::ProcessPhysics), (const void *)&FakePhysics);
    HookBoth(0x00069710, XbeAddress(&PBondCar::ProcessSimplePhysics), (const void *)&FakeSimplePhysics);
    HookBoth(0x00066ee0, XbeAddress(&PBondCar::ControlTyreTracks), (const void *)&FakeTyreTracks);
    HookBoth(0x0006f4c0, XbeAddress(&PhysicsObject::Simulate), (const void *)&FakeObjectSimulate);
    HookBoth(0x000cda90, XbeAddress(&WTargetable::UpdatePosition), (const void *)&FakeUpdatePosition);
    HookBoth(0x00081890, XbeAddress(&RPlayerCamera::GetForwardAimVec4), (const void *)&FakeAimVec);
    HookInstall(0x000359d0, (const void *)&FakeSplinePath);
    HookInstall(0x00035c00, (const void *)&FakeControllerGet);
    HookInstall(0x00035e90, (const void *)&FakeFindAgent);
    HookInstall(0x000289f0, (const void *)&FakeSleep);
    HookInstall(0x000b6770, (const void *)&FakeIncKills);
    HookInstall(0x000b6720, (const void *)&FakeIncShotsHit);
    HookInstall(0x000b66c0, (const void *)&FakeIncPlayerDamage);
    HookInstall(0x000d7c10, (const void *)&FakeTheApp);
    HookInstall(0x000d87a0, (const void *)&FakeDamageFlash);
    HookBoth(0x0011c910, XbeAddress(&ABaseSound::OperatorNew), (const void *)&FakeOperatorNew);
    HookInstall(0x000476f0, (const void *)&FakeOneShot);
    HookBoth(0x00077d70, XbeAddress(XbeOverload<StimuliId>::Of(&Handle::ProcessStimuli)), (const void *)&FakeStimuliId);
    HookBoth(0x00077e00, XbeAddress(XbeOverload<Stimuli>::Of(&Handle::ProcessStimuli)), (const void *)&FakeStimuli);
    HookBoth(0x00077e60, XbeAddress(&Handle::ProcessStimuliZones), (const void *)&FakeStimuliZones);
    HookBoth(0x00077500, XbeAddress(&Handle::GetSystemState), (const void *)&FakeSystemState);
    HookBoth(0x000768c0, XbeAddress(&Handle::AnySystemPlaying), (const void *)&FakeAnyPlaying);
}

// ---- the copies

void *g_carVtable[kCarSlots];
void *g_renderVtable[kRenderSlots];
void *g_audioVtable[kAudioSlots];

enum Op { kSimulate, kApplyDamage, kOpCount };
const char *const kOpNames[kOpCount] = { "Simulate", "ApplyDamage" };

struct Args {
    Coord4 from;
    Coord4 to;
    float amount;
    float split;
    int kind;
    uint32_t sig;
};

alignas(16) uint8_t g_car[sizeof(PBondCar)];
CarPhysics g_physics;
alignas(16) uint8_t g_render[kRenderSize];
alignas(16) uint8_t g_audio[kAudioSize];
alignas(16) uint8_t g_beacon[sizeof(WTargetable)];
alignas(16) uint8_t g_ai[kAIVehicleSize];
float g_hitPoints;
float g_shield;
Args g_args;

RigidBody *g_body;
RigidBodyInfo *g_info;

struct World {
    uint8_t body[sizeof(RigidBody)];
    uint8_t info[sizeof(RigidBodyInfo)];
    SimRandom random;
};

void Take(World *w) {
    memcpy(w->body, g_body, sizeof(w->body));
    memcpy(w->info, g_info, sizeof(w->info));
    w->random = *ShadowRandom;
}

void Put(const World &w) {
    memcpy(g_body, w.body, sizeof(w.body));
    memcpy(g_info, w.info, sizeof(w.info));
    *ShadowRandom = w.random;
}

struct Case {
    Op op;
    alignas(16) uint8_t car[sizeof(PBondCar)];
    CarPhysics physics;
    alignas(16) uint8_t render[kRenderSize];
    alignas(16) uint8_t audio[kAudioSize];
    alignas(16) uint8_t beacon[sizeof(WTargetable)];
    alignas(16) uint8_t ai[kAIVehicleSize];
    float hitPoints;
    float shield;
    Args args;
    World world;
    bool hasBeacon, hasAI, hasShield;
    void *splinePath;
    void *agent;
    uint32_t systemState;
    uint32_t anyPlaying;
    Coord4 aim;
};

struct Result {
    uint8_t car[sizeof(PBondCar)];
    CarPhysics physics;
    uint8_t render[kRenderSize];
    uint8_t audio[kAudioSize];
    uint8_t beacon[sizeof(WTargetable)];
    uint8_t ai[kAIVehicleSize];
    float hitPoints;
    float shield;
    uint8_t soundBlock[sizeof(g_soundBlock)];
    World world;
    LogEntry log[kMaxLog];
    int logCount;
    uint32_t value;
    bool faulted;
};

PBondCar *Car() { return reinterpret_cast<PBondCar *>(g_car); }

typedef void (__fastcall *SimulateFn)(PBondCar *, int);
typedef int (__fastcall *ApplyDamageFn)(PBondCar *, int, const Coord3 *, const Coord3 *, float, float, int,
                                        const uint32_t *);

uint32_t Call(const Case &c) {
    PBondCar *car = Car();
    switch (c.op) {
    case kSimulate:
        reinterpret_cast<SimulateFn>(0x0006a840)(car, 0);
        return 0;
    case kApplyDamage:
        return uint32_t(reinterpret_cast<ApplyDamageFn>(0x0006b6f0)(
            car, 0, reinterpret_cast<const Coord3 *>(&g_args.from), reinterpret_cast<const Coord3 *>(&g_args.to),
            g_args.amount, g_args.split, g_args.kind, &g_args.sig));
    default:
        return 0;
    }
}

bool SafeCall(const Case &c, uint32_t *value) {
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
    memcpy(g_render, c.render, sizeof(g_render));
    memcpy(g_audio, c.audio, sizeof(g_audio));
    memcpy(g_beacon, c.beacon, sizeof(g_beacon));
    memcpy(g_ai, c.ai, sizeof(g_ai));
    g_hitPoints = c.hitPoints;
    g_shield = c.shield;
    g_args = c.args;
    memset(g_soundBlock, 0, sizeof(g_soundBlock));
    g_splinePath = c.splinePath;
    g_agent = c.agent;
    g_systemState = c.systemState;
    g_anyPlaying = c.anyPlaying;
    g_aim = c.aim;

    PBondCar *car = Car();
    car->vtable = g_carVtable;
    car->physics = &g_physics;
    car->renderObject = reinterpret_cast<RSceneObj *>(g_render);
    *reinterpret_cast<void ***>(g_render) = g_renderVtable;
    car->audio = reinterpret_cast<ABaseSound *>(g_audio);
    *reinterpret_cast<void ***>(g_audio) = g_audioVtable;
    car->targetBeacon = c.hasBeacon ? reinterpret_cast<WTargetable *>(g_beacon) : NULL;
    car->aiGroundVehicle = c.hasAI ? reinterpret_cast<AIGroundVehicle *>(g_ai) : NULL;
    car->hitPointLoc = &g_hitPoints;
    car->shieldPointLoc = c.hasShield ? &g_shield : NULL;

    BondCarScratch *savedPad = BondCarScratchPad;
    Put(c.world);
    g_logCount = 0;
    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, true);
    r->faulted = !SafeCall(c, &r->value);
    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, false);
    BondCarScratchPad = savedPad;

    memcpy(r->car, g_car, sizeof(r->car));
    r->physics = g_physics;
    memcpy(r->render, g_render, sizeof(r->render));
    memcpy(r->audio, g_audio, sizeof(r->audio));
    memcpy(r->beacon, g_beacon, sizeof(r->beacon));
    memcpy(r->ai, g_ai, sizeof(r->ai));
    r->hitPoints = g_hitPoints;
    r->shield = g_shield;
    memcpy(r->soundBlock, g_soundBlock, sizeof(r->soundBlock));
    Take(&r->world);
    memcpy(r->log, g_log, sizeof(r->log));
    r->logCount = g_logCount;
}

int g_cases, g_checks, g_differ, g_faults, g_details;
bool g_verbose;

void Detail(const Case &c, int index, const char *what, size_t offset, uint32_t original, uint32_t ours) {
    if (g_details++ >= 10)
        return;
    printf("[bondcarD]   case %d %s: %s +0x%03x original %08x ours %08x\n", index, kOpNames[c.op], what,
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
    if (g_verbose) {
        printf("[bondcarD] case %d %s\n", index, kOpNames[c.op]);
        fflush(stdout);
    }
    RunOnce(c, &original, true);
    RunOnce(c, &ours, false);
    Put(g_live);
    if (original.faulted || ours.faulted) {
        g_faults++;
        if (original.faulted != ours.faulted) {
            g_differ++;
            Detail(c, index, "fault", 0, original.faulted, ours.faulted);
        }
        return;
    }
    bool differ = false;
    CompareBlock(c, index, "car", original.car, ours.car, sizeof(original.car), &differ);
    CompareBlock(c, index, "physics", &original.physics, &ours.physics, sizeof(original.physics), &differ);
    CompareBlock(c, index, "render", original.render, ours.render, sizeof(original.render), &differ);
    CompareBlock(c, index, "audio", original.audio, ours.audio, sizeof(original.audio), &differ);
    CompareBlock(c, index, "beacon", original.beacon, ours.beacon, sizeof(original.beacon), &differ);
    CompareBlock(c, index, "AI vehicle", original.ai, ours.ai, sizeof(original.ai), &differ);
    CompareBlock(c, index, "hit points", &original.hitPoints, &ours.hitPoints, 4, &differ);
    CompareBlock(c, index, "shield", &original.shield, &ours.shield, 4, &differ);
    CompareBlock(c, index, "new sound", original.soundBlock, ours.soundBlock, sizeof(original.soundBlock), &differ);
    CompareBlock(c, index, "body", original.world.body, ours.world.body, sizeof(original.world.body), &differ);
    CompareBlock(c, index, "body info", original.world.info, ours.world.info, sizeof(original.world.info), &differ);
    CompareBlock(c, index, "random", &original.world.random, &ours.world.random, sizeof(SimRandom), &differ);
    CompareBlock(c, index, "calls made", &original.logCount, &ours.logCount, sizeof(original.logCount), &differ);
    CompareBlock(c, index, "calls", original.log, ours.log, sizeof(LogEntry) * size_t(original.logCount), &differ);
    CompareBlock(c, index, "result", &original.value, &ours.value, sizeof(original.value), &differ);
    if (differ)
        g_differ++;
}

// ---- building the cases

Case g_case;
PBondCar *g_source;

PBondCar *Edit() { return reinterpret_cast<PBondCar *>(g_case.car); }
RigidBody *EditBody() { return reinterpret_cast<RigidBody *>(g_case.world.body); }

const char *const kTypes[] = { "tank", "tunnel", "openheli", "redheli", "cop" };

void Start(Op op) {
    g_case.op = op;
    memcpy(g_case.car, g_source, sizeof(g_case.car));
    g_case.physics = *g_source->physics;
    memcpy(g_case.render, g_source->renderObject, sizeof(g_case.render));
    memset(g_case.audio, 0, sizeof(g_case.audio));
    if (g_source->audio != NULL)
        memcpy(g_case.audio, g_source->audio, sizeof(g_case.audio));
    memset(g_case.beacon, 0, sizeof(g_case.beacon));
    g_case.hasBeacon = g_source->targetBeacon != NULL;
    if (g_case.hasBeacon)
        memcpy(g_case.beacon, g_source->targetBeacon, sizeof(g_case.beacon));
    memset(g_case.ai, 0, sizeof(g_case.ai));
    g_case.hasAI = false;
    g_case.hitPoints = g_source->hitPointLoc != NULL ? *g_source->hitPointLoc : 1.0f;
    g_case.shield = 0.0f;
    g_case.hasShield = false;
    memset(&g_case.args, 0, sizeof(g_case.args));
    g_case.world = g_live;
    g_case.splinePath = NULL;
    g_case.agent = NULL;
    g_case.systemState = 0;
    g_case.anyPlaying = 0;
    g_case.aim = { 0.0f, 0.0f, -1.0f, 0.0f };
}

// The state both functions read, shaken
void Perturb(Rng *rng) {
    PBondCar *car = Edit();
    car->carClass = rng->Range(0, 3);
    if (rng->Chance(25))
        car->carType = kTypes[rng->Range(0, 4)];
    car->control.steering = rng->Uniform(-1.0f, 1.0f);
    car->control.steeringVertical = rng->Uniform(-1.0f, 1.0f);
    car->control.gas = rng->Chance(30) ? 0.0f : rng->Uniform(0.0f, 1.0f);
    car->control.brake = rng->Chance(50) ? 0.0f : rng->Uniform(0.0f, 1.0f);
    car->control.handBrake = rng->Chance(25);
    car->control.unknown1B = rng->Chance(50);
    car->inShock = int8_t(rng->Chance(70) ? 0 : rng->Range(1, 5));
    car->damageByPlayerTimer = uint8_t(rng->Chance(50) ? 0 : rng->Range(1, 60));
    car->laserTimer = rng->Chance(50) ? 0 : rng->Range(1, 120);
    car->resetAvailable = rng->Chance(30) ? rng->Range(28, 31) : rng->Range(0, 130);
    car->controlLockTimer = rng->Chance(70) ? -1 : rng->Range(0, 0x7fffffff);
    car->empActive = rng->Chance(30);
    car->empTimer = rng->Range(0, 20000);
    car->scoreable = rng->Chance(60);
    car->immunity = rng->Chance(20);
    car->tyreBlowOuts = rng->Chance(60);
    car->carSpeed = rng->Uniform(-30.0f, 30.0f);
    car->unknown278 = rng->Uniform(-1.0f, 1.0f);
    car->unknown27C = rng->Uniform(-1.0f, 1.0f);
    car->unknown2DC = rng->Chance(30) ? -1.0f : rng->Uniform(0.0f, 0.5f);
    car->missionEditorOnMask = rng->Chance(20) ? rng->Next() : 0;
    car->missionEditorOffMask = rng->Chance(20) ? rng->Next() : 0;
    for (int wheel = 0; wheel < kCarWheels; wheel++) {
        car->suspensionCompression[wheel] = rng->Chance(30) ? 0.0f : rng->Uniform(0.0f, 0.5f);
        car->wheelSlip[wheel] = rng->Uniform(0.0f, 1.5f);
        car->tyreDamagePoints[wheel] = rng->Chance(30) ? 0 : uint32_t(rng->Range(1, 200));
        car->wheels[wheel].face.corner[2].tag.type = uint8_t(rng->Range(0, 12));
    }
    for (int zone = 0; zone < kDamageZoneCount; zone++) {
        car->damageZones[zone].unknown00 = rng->Chance(40) ? 0.0f : rng->Uniform(0.0f, 1.0f);
        car->damageZones[zone].unknown04 = rng->Chance(40) ? 0.0f : rng->Uniform(0.0f, 2.0f);
    }
    g_case.physics.isSub = rng->Chance(15);
    g_case.physics.isSnowmobile = rng->Chance(15);
    g_case.physics.subPhysics = rng->Chance(15);
    g_case.hitPoints = rng->Chance(20) ? rng->Uniform(-20.0f, 0.0f) : rng->Uniform(0.1f, 400.0f);
    g_case.hasShield = rng->Chance(20);
    g_case.shield = rng->Uniform(0.0f, 100.0f);
    // the player's class always has a beacon (Simulate updates it); a zeroed copy when the source has none
    g_case.hasBeacon = car->carClass == 0 || (g_case.hasBeacon && rng->Chance(80));
    g_case.hasAI = rng->Chance(40);
    reinterpret_cast<int32_t *>(g_case.ai)[0x88 / 4] = rng->Range(0, 2);
    g_case.splinePath = rng->Chance(30) ? (void *)0x00004321 : NULL;
    g_case.agent = rng->Chance(50) ? (void *)0x00008765 : NULL;
    g_case.systemState = uint32_t(rng->Range(0, 4));
    g_case.anyPlaying = rng->Chance(50);
    g_case.aim = { rng->Uniform(-1.0f, 1.0f), rng->Uniform(-1.0f, 1.0f), rng->Uniform(-1.0f, 1.0f), 0.0f };

    RigidBody *body = EditBody();
    body->velocity.x += rng->Uniform(-10.0f, 10.0f);
    body->velocity.y += rng->Uniform(-3.0f, 3.0f);
    body->velocity.z += rng->Uniform(-10.0f, 10.0f);
    if (rng->Chance(10))
        body->sleepState = RigidBody::kFrozen;
    else if (body->sleepState == RigidBody::kFrozen)
        body->sleepState = RigidBody::kAwake;
    if (rng->Chance(10))
        body->flags ^= RigidBody::kFlag0;
    body->groundContacts = uint8_t(rng->Range(0, 4));
}

void DamageArgs(Rng *rng) {
    const RigidBody *body = EditBody();
    Coord4 dimensions;
    g_source->renderObject->GetBoundingDimensions(&dimensions);
    float reach = 2.0f * (dimensions.x + dimensions.y + dimensions.z) + 1.0f;
    Args &a = g_case.args;
    a.from = { body->position.x + rng->Uniform(-reach, reach), body->position.y + rng->Uniform(-reach, reach),
               body->position.z + rng->Uniform(-reach, reach), 1.0f };
    a.to = { body->position.x + rng->Uniform(-0.5f, 0.5f) * dimensions.x,
             body->position.y + rng->Uniform(-0.5f, 0.5f) * dimensions.y,
             body->position.z + rng->Uniform(-0.5f, 0.5f) * dimensions.z, 1.0f };
    a.amount = rng->Chance(10) ? rng->Uniform(0.0f, 2.0f) : rng->Uniform(0.0f, 300.0f);
    a.split = rng->Uniform(0.0f, 1.0f);
    a.kind = rng->Range(0, 3);
    switch (rng->Range(0, 2)) {
    case 0:
        a.sig = ShadowDamageSourceSig;
        break;
    case 1:
        ShadowPlayerCar->GetSig(&a.sig);
        break;
    default:
        a.sig = rng->Next();
        break;
    }
}

void CarCases(Rng *rng, int count) {
    for (int n = 0; n < count; n++) {
        Start(kSimulate);
        if (n > 0)
            Perturb(rng);
        RunCase(g_case);
    }
    for (int n = 0; n < count * 2; n++) {
        Start(kApplyDamage);
        if (n > 0)
            Perturb(rng);
        DamageArgs(rng);
        RunCase(g_case);
    }
}

} // namespace

void BondCarShadowD_Run(void) {
    const char *setting = getenv("NIGHTFIRE_BONDCARSHADOWD");
    if (setting == NULL || atoi(setting) == 0)
        return;
    g_verbose = atoi(setting) >= 2;
    g_cases = g_checks = g_differ = g_faults = g_details = 0;

    memcpy(g_carVtable, (const void *)uintptr_t(kPBondCarVtable), sizeof(g_carVtable));
    g_carVtable[2] = (void *)&FakeApplyDamageSlot;
    g_carVtable[PVehicle::kDisableTargetBeacon] = (void *)&FakeDisableBeacon;
    g_carVtable[PVehicle::kGetControllerInput] = (void *)&FakeControllerInput;
    g_carVtable[PVehicle::kResetCar] = (void *)&FakeResetCar;
    g_carVtable[PVehicle::kGlareOn] = (void *)&FakeGlareOn;
    g_carVtable[PVehicle::kSetVisualDamage] = (void *)&FakeVisualDamage;
    InstallFakes();
    Rng rng = { 0x5eed0d0du };

    PBondCar *player = ShadowPlayerCar;
    int cars = 0;
    for (int slot = 0; slot < 64; slot++) {
        PhysicsObject *owner = PhysicsObjects[slot];
        if (owner == NULL || uint32_t(uintptr_t(owner->vtable)) != kPBondCarVtable)
            continue;
        PBondCar *car = static_cast<PBondCar *>(static_cast<PVehicle *>(owner));
        if (car->physics == NULL || car->renderObject == NULL || car->audio == NULL)
            continue;
        g_source = car;
        g_body = car->GetRigidBody();
        g_info = g_body->info;
        memcpy(g_renderVtable, car->renderObject->vtable, sizeof(g_renderVtable));
        g_renderVtable[1] = (void *)&FakeDrawList;
        g_renderVtable[15] = (void *)&FakeTriggerFX;
        memcpy(g_audioVtable, *reinterpret_cast<void ***>(car->audio), sizeof(g_audioVtable));
        g_audioVtable[4] = (void *)&FakeAudioSlot4;
        Take(&g_live);
        CarCases(&rng, car == player ? 40 : 12);
        Put(g_live);
        cars++;
    }
    HooksRemove();
    printf("[bondcarD] V_D Simulate and ApplyDamage on %d cars: %d cases, %d checks, %d differ, %d faulted\n", cars,
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
