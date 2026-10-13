#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameMissionsShadowD.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <cmath>
#include <initializer_list>
#include <vector>

#include "../game/MissionRules.h"
#include "../game/Vehicle.h"              // PVehicle
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../game/Simulation.h"
#include "../world/Trigger.h"             // TriggerEvents, gEventDynamicData
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of game.missions' package D, run once from the first simulation tick when
// NIGHTFIRE_GAMEMISSIONSSHADOWD is set. Each case runs the ORIGINAL (every entry of 0x000b9500-0x000ba7f0 swapped
// back together) and then the PORT on the same inputs, and compares what they return, the bytes they leave (the
// rule, the mission manager's timers, the event data, the blocks allocated) and a log of every call they make out
// of the package:
//
//   - the fifteen rules' constructors on random records; Instantiate for every kind and out-of-range ones; the six
//     deleting destructors, SMissionRule's own and its destructor, with and without freeing;
//   - every CheckRule over every target type (player, AI figures, AI element, two unknown ones), both directions,
//     last values on, either side of and away from the thresholds, fake answers at and across integer edges, NaN;
//   - DoRule over every kind and flag combination (enabled, once, inverted), with event lists of 0..5 events, some
//     without a callback, and RunEvents alone (also without events);
//   - SMissionRuleTarget's GetSpeed, GetPos, GetMatrix and GetProperty (the three names, others, element answers);
//   - SMissionTimer's eight methods over random states, rates and step counts (the beep in GetValue);
//   - RuleCrossing::Crossed and CompareWithinDegrees over random, boundary and NaN values; StubError.
// The player's body, the mission manager and the weapon manager's slots start as copies of the live ones, then are
// perturbed.
//
// Fakes: Simulation::GetRigidBody and GetPlayerObject (private bodies, a private player car whose vtable answers
// GetIsInTwoWheelMode), AIElementController::GetStats, GetCarpElementTracking and RedirectCarpElement,
// AIElementTracking::GetProperty, SMissionManager::CheckProgrammerEvent and GetMagicCounter, AMenuSound::Trigger,
// RegisterEvent::LookupEvent (answering a recording callback or none), UMemory::FastAlloc/FastFree (a private
// arena). The globals the code reads (the player, the mission manager, the weapon manager, the step counters) point
// at private copies during the test and are put back after it.
//
// A mutation the test sees: RuleIntCrossing::Crossed testing `last > threshold` instead of `last >= threshold` (the
// cases with last on the threshold), or SRuleProgCounter's kAnyBits and kNoBits swapped.
//
// One summary line: [gamemissionsshadowd] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the originals

typedef void *(__fastcall *ConstructFn)(void *, int, const MissionRuleRecord *);
typedef bool (__fastcall *CheckFn)(void *, int);
typedef void *(__fastcall *DeleteFn)(void *, int, unsigned);

#define Orig_Destruct ((void (__fastcall *)(void *, int))0x000b9ac0)
#define Orig_RunEvents ((void (__fastcall *)(void *, int))0x000b9500)
#define Orig_DoRule ((void (__fastcall *)(void *, int))0x000b9590)
#define Orig_Instantiate ((void *(*)(const MissionRuleRecord *))0x000ba150)
#define Orig_StubError ((bool (*)(void))0x000b98f0)
#define Orig_Compare ((float (*)(float, float, float))0x000b9ad0)
#define Orig_Crossed ((bool (__fastcall *)(void *, int, float))0x000b9c50)
#define Orig_GetSpeed ((int (__fastcall *)(void *, int))0x000ba390)
#define Orig_GetPos ((void (__fastcall *)(void *, int, Coord4 *))0x000ba3e0)
#define Orig_GetMatrix ((bool (__fastcall *)(void *, int, MATRIX4 *))0x000ba430)
#define Orig_GetProperty ((bool (__fastcall *)(void *, int, const char *))0x000ba4a0)
#define Orig_TimerConstruct ((void *(__fastcall *)(void *, int))0x000b95d0)
#define Orig_TimerReset ((void (__fastcall *)(void *, int, int))0x000b95e0)
#define Orig_TimerStart ((void (__fastcall *)(void *, int, int))0x000b9600)
#define Orig_TimerIncrement ((void (__fastcall *)(void *, int, int))0x000b9620)
#define Orig_TimerGetValue ((int (__fastcall *)(void *, int))0x000b9630)
#define Orig_TimerIsReverse ((bool (__fastcall *)(void *, int))0x000b96a0)
#define Orig_TimerSetTicking ((void (__fastcall *)(void *, int, bool))0x000b96b0)
#define Orig_TimerStop ((void (__fastcall *)(void *, int))0x000b9ce0)

// Each kind: its constructor, its CheckRule (0: not the package's), its deleting destructor, its size
struct Kind {
    int type;
    uint32_t construct;
    uint32_t check;
    uint32_t destroy;
    unsigned size;
};
const Kind kKinds[] = {
    { kRuleAlways, 0x000b96c0, 0x000b98f0, 0x000b9d00, 0x0c },
    { kRuleAmmo, 0x000b9d20, 0x000b9db0, 0x000b9d90, 0x1c },
    { kRuleDamage, 0x000b9700, 0x000b9770, 0x000b9d90, 0x1c },
    { kRuleDeath, 0x000b97f0, 0x000b9850, 0x000b9df0, 0x14 },
    { kRuleHealth, 0x000b9e10, 0x000b9eb0, 0x000b9e90, 0x20 },
    { kRulePIP, 0x000b98a0, 0x000b98f0, 0x000b9f40, 0x18 },
    { kRuleProg, 0x000b9900, 0x000b9950, 0x000b9f60, 0x10 },
    { kRuleRange, 0x000b9f80, 0x000ba570, 0x000b9e90, 0x20 },
    { kRuleSpeed, 0x000ba000, 0x000ba620, 0x000b9e90, 0x20 },
    { kRuleTimer, 0x000ba080, 0x000ba0f0, 0x000b9d90, 0x1c },
    { kRuleProperty, 0x000b9960, 0x000ba560, 0x000b9d90, 0x1c },
    { kRuleCollision, 0x000b9b30, 0, 0x000b9df0, 0x14 },
    { kRulePlayerDir, 0x000b9a50, 0x000ba690, 0x000b9d90, 0x1c },
    { kRuleDifficulty, 0x000b99d0, 0x000b9a20, 0x000b9f60, 0x10 },
    { kRuleProgCounter, 0x000b9b90, 0x000b9be0, 0x000b9f40, 0x18 },
};
const int kKindCount = int(sizeof(kKinds) / sizeof(kKinds[0]));

// The globals the code reads, at the game's addresses
#define PlayerSlotPointer (*(PVehicle ***)0x00234e40)
#define MissionManagerPointer PTR_AT(OwnedGlobals_Ours(0x00239220))
#define WeaponManagerPointer PTR_AT(OwnedGlobals_Ours(0x0023923c))
#define AIControllerPointer PTR_AT(OwnedGlobals_Ours(0x001ddee4))
#define StepCount I32_AT(0x00234e34)
#define StepsPerSecond I32_AT(0x00234e2c)

bool g_original;
long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[gamemissionsshadowd] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x5bd1e995;
uint32_t NextRandom() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}
int RandomInt(int lo, int hi) {
    return lo + int((NextRandom() >> 8) % uint32_t(hi - lo + 1));
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(NextRandom() >> 8) * (1.0f / 16777216.0f);
}
bool Chance(int percent) {
    return RandomInt(0, 99) < percent;
}
uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}
float NaN() {
    return std::bit_cast<float>(0x7fc00000u);
}
uint32_t Hash(const void *data, size_t size) {
    uint32_t h = 2166136261u;
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
uint32_t HashString(const char *s) {
    return s == NULL ? 0 : Hash(s, strlen(s));
}
uint32_t Word(const void *p) {
    return uint32_t(uintptr_t(p));
}

// A float a rule will truncate: whole numbers, halves, values just under and over, now and then NaN
float EdgeFloat(int around) {
    switch (RandomInt(0, 6)) {
    case 0:
        return float(around);
    case 1:
        return float(around) + 0.5f;
    case 2:
        return float(around) - 0.25f;
    case 3:
        return std::nextafter(float(around), 1e9f);
    case 4:
        return Chance(20) ? NaN() : -float(around) - 0.75f;
    default:
        return Uniform(-40.0f, 160.0f);
    }
}

template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    call();
#endif
    return true;
}

void Originals(bool on) {
    XbeOriginal_RestoreRange(0x000b9500, 0x000ba7f0, on);
}

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[32];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
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

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
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

// ---- the private objects: everything a case may write, snapshotted whole

constexpr int kBodies = 4;
constexpr int kSlots = 32;
constexpr int kTimerOffset = 0x360;

struct Private {
    alignas(16) uint8_t rule[0x40];
    alignas(16) uint8_t missionManager[0x800];
    alignas(16) uint8_t bodies[kBodies][0x80];
    alignas(16) uint8_t infos[kBodies][0x500];
    alignas(16) uint8_t playerObject[0x80];     // GetPlayerObject's
    alignas(16) uint8_t playerCar[0x200];       // *PlayerSlotPointer, vtable g_carVtable
    PVehicle *playerSlot;
    float stats[8];
    alignas(16) uint8_t tracking[2][0x40];
    alignas(16) uint8_t tracked[0x60];
    alignas(16) uint8_t redirected[0x100];
    alignas(16) uint8_t elementObject[0x80];
    alignas(16) uint8_t weaponManager[0x20];
    alignas(16) uint8_t weaponSlots[kSlots][0x54];
    alignas(16) uint8_t events[0x100];
    alignas(16) uint8_t arena[0x400];
    uint32_t arenaUsed;
    alignas(16) uint8_t out[0x40];
    MissionRuleRecord record;
    char name[16];
};
Private g_p;
Private g_saved;
Private g_after[2];
uint8_t g_eventData[2][0x64];
uint8_t g_eventSaved[0x64];
int32_t g_stepSaved[2];

// The fakes' answers, set by each case (not written by the code)
struct Answers {
    bool twoWheels;
    uint32_t propertyPattern;
    uint32_t progPattern;
    int counter;
    uint32_t lookupPattern;
};
Answers g_answers;
int g_propertyCalls, g_progCalls, g_lookupCalls;

void *g_carVtable[0x60];
uint8_t g_liveBody[0x80];
uint8_t g_liveOrientation[0x40];
uint8_t g_liveMissions[0x720];
uint8_t g_liveSlots[kSlots][0x54];
bool g_haveBody, g_haveMissions, g_haveSlots;

RigidBody *Body(int i) {
    return reinterpret_cast<RigidBody *>(g_p.bodies[i]);
}

// ---- the call log and the fakes

enum : uint32_t {
    kGetRigidBody = 1, kGetPlayerObject, kTwoWheels, kGetStats, kGetTracking, kRedirect, kTrackingProperty,
    kProgEvent, kMagicCounter, kTrigger, kLookup, kCallback, kAlloc, kFree,
};

std::vector<uint32_t> g_log;
std::vector<uint32_t> g_logs[2];
void Log(uint32_t type, std::initializer_list<uint32_t> words) {
    g_log.push_back(type);
    g_log.push_back(uint32_t(words.size()));
    g_log.insert(g_log.end(), words.begin(), words.end());
}

void *__fastcall FakeGetRigidBody(void *sim, int, int slot) {
    Log(kGetRigidBody, { Word(sim), uint32_t(slot) });
    return g_p.bodies[slot & (kBodies - 1)];
}
void *__fastcall FakeGetPlayerObject(void *sim, int) {
    Log(kGetPlayerObject, { Word(sim) });
    return g_p.playerObject;
}
bool __fastcall FakeTwoWheels(void *car, int) {
    Log(kTwoWheels, { Word(car) });
    return g_answers.twoWheels;
}
void *__fastcall FakeGetStats(void *controller, int) {
    Log(kGetStats, { Word(controller) });
    return g_p.stats;
}
void *__fastcall FakeGetTracking(void *controller, int, uint32_t element) {
    Log(kGetTracking, { Word(controller), element });
    return g_p.tracking[element & 1];
}
void *__fastcall FakeRedirect(void *controller, int, uint32_t element) {
    Log(kRedirect, { Word(controller), element });
    return g_p.redirected;
}
bool __fastcall FakeTrackingProperty(void *tracking, int, const char *name) {
    Log(kTrackingProperty, { Word(tracking), HashString(name) });
    return ((g_answers.propertyPattern >> (g_propertyCalls++ & 31)) & 1) != 0;
}
bool __fastcall FakeProgEvent(void *manager, int, int event) {
    Log(kProgEvent, { Word(manager), uint32_t(event) });
    return ((g_answers.progPattern >> (g_progCalls++ & 31)) & 1) != 0;
}
int __fastcall FakeMagicCounter(void *manager, int, int counter) {
    Log(kMagicCounter, { Word(manager), uint32_t(counter) });
    return g_answers.counter;
}
void FakeTrigger(int bank, const char *patch, const char *mix, int view) {
    Log(kTrigger, { uint32_t(bank), HashString(patch), HashString(mix), uint32_t(view) });
}
void FakeCallback(uint32_t data) {
    Log(kCallback, { data, Hash(&gEventDynamicData, sizeof(EventDynamicData)) });
}
void *FakeLookup(int event) {
    Log(kLookup, { uint32_t(event) });
    if (((g_answers.lookupPattern >> (g_lookupCalls++ & 31)) & 1) != 0)
        return NULL;
    return (void *)&FakeCallback;
}
void *FakeFastAlloc(unsigned size, const char *name) {
    Log(kAlloc, { size, HashString(name) });
    uint32_t need = (size + 15) & ~15u;
    if (g_p.arenaUsed + need > sizeof(g_p.arena))
        return NULL;
    void *block = g_p.arena + g_p.arenaUsed;
    g_p.arenaUsed += need;
    return block;
}
void FakeFastFree(void *block, unsigned size) {
    Log(kFree, { Word(block), size });
}

// ---- a case: the original, then the port, from the same state

template <class O, class P>
void RunCase(const char *what, const O &original, const P &port) {
    long index = g_cases++;
    memcpy(&g_saved, &g_p, sizeof(Private));
    memcpy(g_eventSaved, &gEventDynamicData, sizeof(g_eventSaved));
    int32_t steps = StepCount;
    uint32_t results[2] = { 0, 0 };
    bool ok[2];
    for (int side = 0; side < 2; side++) {
        memcpy(&g_p, &g_saved, sizeof(Private));
        memcpy(&gEventDynamicData, g_eventSaved, sizeof(g_eventSaved));
        StepCount = steps;
        g_log.clear();
        g_propertyCalls = g_progCalls = g_lookupCalls = 0;
        g_original = side == 0;
        if (g_original) {
            Originals(true);
            ok[side] = Guarded([&] { results[side] = original(); });
            Originals(false);
        } else {
            ok[side] = Guarded([&] { results[side] = port(); });
        }
        memcpy(&g_after[side], &g_p, sizeof(Private));
        memcpy(g_eventData[side], &gEventDynamicData, sizeof(g_eventData[side]));
        g_stepSaved[side] = StepCount;
        g_logs[side] = g_log;
        OwnedGlobals_CanonicalWords(g_logs[side].data(), g_logs[side].size() * 4);   // our vtables and the game's alike
    }
    memcpy(&g_p, &g_saved, sizeof(Private));
    memcpy(&gEventDynamicData, g_eventSaved, sizeof(g_eventSaved));
    StepCount = steps;

    char label[96];
    snprintf(label, sizeof(label), "%s: faults", what);
    Check(ok[0] == ok[1], label, index);
    snprintf(label, sizeof(label), "%s: result", what);
    Check(OwnedGlobals_SameWord(results[0], results[1]), label, index);
    snprintf(label, sizeof(label), "%s: state", what);   // a vtable or callback: ours, or the game's
    Check(OwnedGlobals_FirstDifference(&g_after[0], &g_after[1], sizeof(Private)) == sizeof(Private), label, index);
    snprintf(label, sizeof(label), "%s: event data", what);
    Check(memcmp(g_eventData[0], g_eventData[1], sizeof(g_eventData[0])) == 0, label, index);
    snprintf(label, sizeof(label), "%s: calls", what);
    Check(g_logs[0] == g_logs[1], label, index);
}

// ---- setting up

void RandomTimer(SMissionTimer *timer) {
    timer->running = Chance(70);
    timer->ticking = Chance(50);
    timer->startStep = RandomInt(0, 4000);
    timer->value = Chance(20) ? 0 : RandomInt(-3000, 6000);
    timer->rate = Chance(10) ? 0 : (Chance(50) ? -1 : RandomInt(-3, 3));
    timer->lastBeep = RandomInt(-2, 120);
}

SMissionTimer *Timers() {
    return reinterpret_cast<SMissionTimer *>(g_p.missionManager + kTimerOffset);
}

void Perturb() {
    memset(&g_p, 0, sizeof(Private));

    // the mission manager
    if (g_haveMissions)
        memcpy(g_p.missionManager, g_liveMissions, sizeof(g_liveMissions));
    float health = EdgeFloat(RandomInt(-2, 120));
    memcpy(g_p.missionManager + 0x49c, &health, 4);
    int32_t difficulty = RandomInt(0, 3);
    memcpy(g_p.missionManager + 0x4dc, &difficulty, 4);
    for (int i = 0; i < kMissionTimers; i++)
        RandomTimer(&Timers()[i]);

    // the bodies: the live player's, moved
    for (int i = 0; i < kBodies; i++) {
        if (g_haveBody)
            memcpy(g_p.bodies[i], g_liveBody, sizeof(g_liveBody));
        RigidBody *body = Body(i);
        body->position.x += Uniform(-80.0f, 80.0f);
        body->position.y += Uniform(-5.0f, 5.0f);
        body->position.z += Uniform(-80.0f, 80.0f);
        body->velocity.x = EdgeFloat(RandomInt(-30, 30));
        body->velocity.y = Uniform(-2.0f, 2.0f);
        body->velocity.z = Uniform(-40.0f, 40.0f);
        body->info = reinterpret_cast<RigidBodyInfo *>(g_p.infos[i]);
        float *matrix = reinterpret_cast<float *>(g_p.infos[i]);
        if (g_haveBody && i == 0)
            memcpy(matrix, g_liveOrientation, sizeof(g_liveOrientation));
        else
            for (int k = 0; k < 16; k++)
                matrix[k] = Uniform(-1.0f, 1.0f);
    }

    // the player
    PhysicsObject *player = reinterpret_cast<PhysicsObject *>(g_p.playerObject);
    player->flags = uint16_t(RandomInt(0, 7));
    player->rigidBodySlot = int16_t(RandomInt(-1, 6));
    *reinterpret_cast<void ***>(g_p.playerCar) = g_carVtable;
    reinterpret_cast<PhysicsObject *>(g_p.playerCar)->rigidBodySlot = int16_t(RandomInt(0, 3));
    g_p.playerSlot = reinterpret_cast<PVehicle *>(g_p.playerCar);

    // the AI
    for (int i = 0; i < 7; i++)
        g_p.stats[i] = EdgeFloat(RandomInt(-5, 100));
    for (int i = 0; i < 2; i++) {
        uint8_t *tracking = g_p.tracking[i];
        void *tracked = g_p.tracked;
        memcpy(tracking + 0x1c, &tracked, 4);
        float position[4] = { Uniform(-200.0f, 200.0f), Uniform(-10.0f, 10.0f), Uniform(-200.0f, 200.0f),
                              Uniform(-1.0f, 1.0f) };
        if (g_haveBody && Chance(50)) {
            memcpy(position, g_liveBody + 0x10, 12);
            position[0] += Uniform(-30.0f, 30.0f);
            position[2] += Uniform(-30.0f, 30.0f);
        }
        memcpy(tracking + 0x30, position, 12);
        float speed = EdgeFloat(RandomInt(0, 60));
        memcpy(tracking + 0x3c, &speed, 4);
    }
    float trackedHealth = EdgeFloat(RandomInt(-1, 100));
    memcpy(g_p.tracked + 0x4c, &trackedHealth, 4);
    int32_t one44 = Chance(80) ? 1 : RandomInt(0, 2);
    int32_t one48 = Chance(80) ? 1 : RandomInt(0, 2);
    memcpy(g_p.redirected + 0x44, &one44, 4);
    memcpy(g_p.redirected + 0x48, &one48, 4);
    void *element = Chance(85) ? g_p.elementObject : NULL;
    memcpy(g_p.redirected + 0xfc, &element, 4);
    reinterpret_cast<PhysicsObject *>(g_p.elementObject)->rigidBodySlot = int16_t(RandomInt(0, 3));

    // the weapon manager
    void *slots = g_p.weaponSlots;
    memcpy(g_p.weaponManager + 0x10, &slots, 4);
    if (g_haveSlots)
        memcpy(g_p.weaponSlots, g_liveSlots, sizeof(g_liveSlots));
    for (int i = 0; i < kSlots; i++) {
        int32_t ammo = Chance(30) ? RandomInt(-1, 3) : RandomInt(0, 200);
        int32_t unknown50 = RandomInt(0, 2);
        memcpy(g_p.weaponSlots[i] + 0x18, &ammo, 4);
        memcpy(g_p.weaponSlots[i] + 0x50, &unknown50, 4);
    }

    // the events: 0..5, their data in the block
    TriggerEvents *events = reinterpret_cast<TriggerEvents *>(g_p.events);
    events->count = RandomInt(0, 5);
    for (int i = 0; i < events->count; i++) {
        TriggerEvent *event = &events->Events()[i];
        event->type = RandomInt(0, 0x30);
        event->dataOffset = 0x80 + 8 * i - int(sizeof(TriggerEvents)) - int(sizeof(TriggerEvent)) * i;
    }

    g_answers.twoWheels = Chance(50);
    g_answers.propertyPattern = NextRandom();
    g_answers.progPattern = NextRandom();
    g_answers.counter = Chance(30) ? int(NextRandom()) : RandomInt(-3, 12);
    g_answers.lookupPattern = NextRandom() & NextRandom();
    StepCount = RandomInt(0, 9000);
    StepsPerSecond = Chance(80) ? 60 : RandomInt(1, 61);
}

const int kTargetTypes[] = { kTargetPlayer, kTargetStats, kTargetElement, 1, 7 };

void RandomRecord(int type) {
    MissionRuleRecord &r = g_p.record;
    memset(&r, 0, sizeof(r));
    r.type = type;
    r.flags = uint32_t(RandomInt(0, 7)) | (Chance(10) ? 0xfff8u : 0u);
    r.stage = NextRandom();
    r.events = Chance(85) ? reinterpret_cast<TriggerEvents *>(g_p.events) : NULL;
    r.args[0] = kTargetTypes[RandomInt(0, 4)];
    r.args[1] = Chance(50) ? RandomInt(0, 3) : int32_t(NextRandom());
    r.args[2] = RandomInt(-5, 100);
    r.args[3] = RandomInt(-5, 100);
    switch (type) {
    case kRuleAmmo:
        r.args[0] = RandomInt(0, kSlots - 1);
        r.args[1] = Chance(50) ? 0 : RandomInt(1, 3);
        break;
    case kRuleTimer:
        r.args[0] = RandomInt(0, kMissionTimers - 1);
        r.args[1] = Chance(50) ? 0 : RandomInt(1, 3);
        r.floatArgs[2] = Chance(5) ? NaN() : EdgeFloat(RandomInt(0, 100));
        break;
    case kRuleProperty: {
        static const char *const names[] = { "NOBOOST", "ONEEMP", "2WHEELS", "NOBOOSTX", "ONEEM", "TURBO", "" };
        const char *name = names[RandomInt(0, 6)];
        strncpy(&r.text[8], name, 8);
        break;
    }
    case kRuleProgCounter:
        r.args[0] = RandomInt(0, 20);
        r.args[1] = RandomInt(-1, 5);
        r.args[2] = Chance(30) ? int32_t(NextRandom()) : RandomInt(-3, 12);
        break;
    case kRuleDifficulty:
        r.args[0] = RandomInt(0, 15);
        break;
    case kRulePlayerDir:
        r.args[0] = Chance(80) ? kTargetElement : kTargetTypes[RandomInt(0, 4)];
        r.args[2] = RandomInt(-360, 360);
        r.args[3] = RandomInt(-360, 360);
        break;
    default:
        break;
    }
}

// A rule built by the port's constructor, its kept values then put near the thresholds
void BuildRule(const Kind &kind) {
    RandomRecord(kind.type);
    memset(g_p.rule, 0xcd, sizeof(g_p.rule));
    reinterpret_cast<ConstructFn>((uintptr_t)kind.construct)(g_p.rule, 0, &g_p.record);
    int32_t *words = reinterpret_cast<int32_t *>(g_p.rule);
    switch (kind.type) {
    case kRuleDamage:
        words[6] = RandomInt(-5, 120);
        break;
    case kRuleAmmo:
    case kRuleHealth:
    case kRuleRange:
    case kRuleSpeed: {
        int at = kind.type == kRuleAmmo ? 4 : 5;     // the crossing's words
        int choice = RandomInt(0, 3);
        if (choice == 1)
            words[at + 2] = words[at + 1];
        else if (choice == 2)
            words[at + 2] = words[at + 1] + RandomInt(-3, 3);
        else if (choice == 3)
            words[at + 2] = RandomInt(-10, 150);
        break;
    }
    case kRuleTimer: {
        float *floats = reinterpret_cast<float *>(g_p.rule);
        int choice = RandomInt(0, 3);
        if (choice == 1)
            floats[6] = floats[5];
        else if (choice == 2)
            floats[6] = Chance(10) ? NaN() : EdgeFloat(RandomInt(0, 100));
        break;
    }
    default:
        break;
    }
}

// ---- the tests

void TestConstructors() {
    for (int n = 0; n < 40; n++) {
        for (const Kind &kind : kKinds) {
            Perturb();
            RandomRecord(kind.type);
            memset(g_p.rule, 0xcd, sizeof(g_p.rule));
            ConstructFn construct = reinterpret_cast<ConstructFn>((uintptr_t)kind.construct);
            RunCase("constructor",
                    [&] { return Word(construct(g_p.rule, 0, &g_p.record)); },
                    [&] {
                        const MissionRuleRecord *r = &g_p.record;
                        void *b = g_p.rule;
                        switch (kind.type) {
                        case kRuleAlways: return Word(static_cast<SRuleAlways *>(b)->Construct(r));
                        case kRuleAmmo: return Word(static_cast<SRuleAmmo *>(b)->Construct(r));
                        case kRuleDamage: return Word(static_cast<SRuleDamage *>(b)->Construct(r));
                        case kRuleDeath: return Word(static_cast<SRuleDeath *>(b)->Construct(r));
                        case kRuleHealth: return Word(static_cast<SRuleHealth *>(b)->Construct(r));
                        case kRulePIP: return Word(static_cast<SRulePIP *>(b)->Construct(r));
                        case kRuleProg: return Word(static_cast<SRuleProg *>(b)->Construct(r));
                        case kRuleRange: return Word(static_cast<SRuleRange *>(b)->Construct(r));
                        case kRuleSpeed: return Word(static_cast<SRuleSpeed *>(b)->Construct(r));
                        case kRuleTimer: return Word(static_cast<SRuleTimer *>(b)->Construct(r));
                        case kRuleProperty: return Word(static_cast<SRuleProperty *>(b)->Construct(r));
                        case kRuleCollision: return Word(static_cast<SRuleCollision *>(b)->Construct(r));
                        case kRulePlayerDir: return Word(static_cast<SRulePlayerDir *>(b)->Construct(r));
                        case kRuleDifficulty: return Word(static_cast<SRuleDifficulty *>(b)->Construct(r));
                        default: return Word(static_cast<SRuleProgCounter *>(b)->Construct(r));
                        }
                    });
        }
    }
}

void TestInstantiate() {
    for (int n = 0; n < 300; n++) {
        Perturb();
        int type = Chance(90) ? RandomInt(0, 14) : RandomInt(-3, 20);
        RandomRecord(type);
        if (Chance(5))
            g_p.arenaUsed = sizeof(g_p.arena) - 8;      // the allocation fails
        RunCase("Instantiate",
                [&] { return Word(Orig_Instantiate(&g_p.record)); },
                [&] { return Word(SMissionRule::Instantiate(&g_p.record)); });
    }
}

void TestDestructors() {
    const uint32_t deletes[] = { 0x000b9cb0, 0x000b9d00, 0x000b9d90, 0x000b9df0, 0x000b9e90, 0x000b9f40,
                                 0x000b9f60 };
    for (int n = 0; n < 10; n++) {
        for (uint32_t at : deletes) {
            for (unsigned flags = 0; flags < 4; flags++) {
                Perturb();
                BuildRule(kKinds[RandomInt(0, kKindCount - 1)]);
                DeleteFn destroy = reinterpret_cast<DeleteFn>((uintptr_t)at);
                RunCase("Delete",
                        [&] { return Word(destroy(g_p.rule, 0, flags)); },
                        [&] {
                            void *b = g_p.rule;
                            switch (at) {
                            case 0x000b9cb0: return Word(static_cast<SMissionRule *>(b)->Delete(flags));
                            case 0x000b9d00: return Word(static_cast<SRuleAlways *>(b)->Delete(flags));
                            case 0x000b9d90: return Word(static_cast<SRuleAmmo *>(b)->Delete(flags));
                            case 0x000b9df0: return Word(static_cast<SRuleDeath *>(b)->Delete(flags));
                            case 0x000b9e90: return Word(static_cast<SRuleRange *>(b)->Delete(flags));
                            case 0x000b9f40: return Word(static_cast<SRuleProgCounter *>(b)->Delete(flags));
                            default: return Word(static_cast<SRuleProg *>(b)->Delete(flags));
                            }
                        });
            }
        }
        Perturb();
        BuildRule(kKinds[RandomInt(0, kKindCount - 1)]);
        RunCase("Destruct",
                [&] { Orig_Destruct(g_p.rule, 0); return 0u; },
                [&] { reinterpret_cast<SMissionRule *>(g_p.rule)->Destruct(); return 0u; });
    }
}

uint32_t PortCheck(int type) {
    void *b = g_p.rule;
    switch (type) {
    case kRuleAlways:
    case kRulePIP: return StubError();
    case kRuleAmmo: return static_cast<SRuleAmmo *>(b)->CheckRule();
    case kRuleDamage: return static_cast<SRuleDamage *>(b)->CheckRule();
    case kRuleDeath: return static_cast<SRuleDeath *>(b)->CheckRule();
    case kRuleHealth: return static_cast<SRuleHealth *>(b)->CheckRule();
    case kRuleProg: return static_cast<SRuleProg *>(b)->CheckRule();
    case kRuleRange: return static_cast<SRuleRange *>(b)->CheckRule();
    case kRuleSpeed: return static_cast<SRuleSpeed *>(b)->CheckRule();
    case kRuleTimer: return static_cast<SRuleTimer *>(b)->CheckRule();
    case kRuleProperty: return static_cast<SRuleProperty *>(b)->CheckRule();
    case kRulePlayerDir: return static_cast<SRulePlayerDir *>(b)->CheckRule();
    case kRuleDifficulty: return static_cast<SRuleDifficulty *>(b)->CheckRule();
    default: return static_cast<SRuleProgCounter *>(b)->CheckRule();
    }
}

void TestCheckRules() {
    for (int n = 0; n < 300; n++) {
        for (const Kind &kind : kKinds) {
            if (kind.check == 0)
                continue;
            Perturb();
            BuildRule(kind);
            CheckFn check = reinterpret_cast<CheckFn>((uintptr_t)kind.check);
            RunCase("CheckRule",
                    [&] { return uint32_t(check(g_p.rule, 0)); },
                    [&] { return PortCheck(kind.type); });
        }
    }
}

void TestDoRule() {
    for (int n = 0; n < 600; n++) {
        Perturb();
        BuildRule(kKinds[RandomInt(0, kKindCount - 1)]);
        RunCase("DoRule",
                [&] { Orig_DoRule(g_p.rule, 0); return 0u; },
                [&] { reinterpret_cast<SMissionRule *>(g_p.rule)->DoRule(); return 0u; });
    }
    for (int n = 0; n < 100; n++) {
        Perturb();
        BuildRule(kKinds[RandomInt(0, kKindCount - 1)]);
        RunCase("RunEvents",
                [&] { Orig_RunEvents(g_p.rule, 0); return 0u; },
                [&] { reinterpret_cast<SMissionRule *>(g_p.rule)->RunEvents(); return 0u; });
    }
}

void TestTarget() {
    static const char *const names[] = { "NOBOOST", "ONEEMP", "2WHEELS", "NOBOOS", "ONEEMPS", "2WHEEL", "", "X" };
    for (int n = 0; n < 300; n++) {
        Perturb();
        SMissionRuleTarget *target = reinterpret_cast<SMissionRuleTarget *>(g_p.rule);
        target->type = kTargetTypes[RandomInt(0, 4)];
        target->element = Chance(50) ? RandomInt(0, 3) : NextRandom();
        RunCase("GetSpeed", [&] { return uint32_t(Orig_GetSpeed(target, 0)); },
                [&] { return uint32_t(target->GetSpeed()); });
        Coord4 *position = reinterpret_cast<Coord4 *>(g_p.out);
        RunCase("GetPos", [&] { Orig_GetPos(target, 0, position); return 0u; },
                [&] { target->GetPos(position); return 0u; });
        MATRIX4 *matrix = reinterpret_cast<MATRIX4 *>(g_p.out);
        RunCase("GetMatrix", [&] { return uint32_t(Orig_GetMatrix(target, 0, matrix)); },
                [&] { return uint32_t(target->GetMatrix(matrix)); });
        strcpy(g_p.name, names[RandomInt(0, 7)]);
        RunCase("GetProperty", [&] { return uint32_t(Orig_GetProperty(target, 0, g_p.name)); },
                [&] { return uint32_t(target->GetProperty(g_p.name)); });
    }
}

void TestTimers() {
    for (int n = 0; n < 300; n++) {
        Perturb();
        SMissionTimer *timer = &Timers()[RandomInt(0, kMissionTimers - 1)];
        int argument = Chance(50) ? RandomInt(-3, 3) : RandomInt(-5000, 5000);
        bool on = Chance(50);
        if (Chance(30))
            StepCount = timer->startStep + RandomInt(0, 600);
        RunCase("Timer::Construct", [&] { return Word(Orig_TimerConstruct(timer, 0)); },
                [&] { return Word(timer->Construct()); });
        RunCase("Timer::Reset", [&] { Orig_TimerReset(timer, 0, argument); return 0u; },
                [&] { timer->Reset(argument); return 0u; });
        RunCase("Timer::Start", [&] { Orig_TimerStart(timer, 0, argument); return 0u; },
                [&] { timer->Start(argument); return 0u; });
        RunCase("Timer::Increment", [&] { Orig_TimerIncrement(timer, 0, argument); return 0u; },
                [&] { timer->Increment(argument); return 0u; });
        RunCase("Timer::GetValue", [&] { return uint32_t(Orig_TimerGetValue(timer, 0)); },
                [&] { return uint32_t(timer->GetValue()); });
        RunCase("Timer::IsReverse", [&] { return uint32_t(Orig_TimerIsReverse(timer, 0)); },
                [&] { return uint32_t(timer->IsReverse()); });
        RunCase("Timer::SetTicking", [&] { Orig_TimerSetTicking(timer, 0, on); return 0u; },
                [&] { timer->SetTicking(on); return 0u; });
        RunCase("Timer::Stop", [&] { Orig_TimerStop(timer, 0); return 0u; },
                [&] { timer->Stop(); return 0u; });
    }
}

float AngleValue() {
    switch (RandomInt(0, 7)) {
    case 0: return float(RandomInt(-4, 4) * 90);
    case 1: return 180.0f;
    case 2: return std::nextafter(180.0f, 0.0f);
    case 3: return Chance(30) ? NaN() : -0.0f;
    case 4: return Uniform(-1000.0f, 1000.0f);
    default: return Uniform(-360.0f, 360.0f);
    }
}

void TestMaths() {
    for (int n = 0; n < 3000; n++) {
        Perturb();
        float angle = AngleValue(), from = AngleValue(), to = AngleValue();
        RunCase("CompareWithinDegrees", [&] { return Bits(Orig_Compare(angle, from, to)); },
                [&] { return Bits(CompareWithinDegrees(angle, from, to)); });
        RuleCrossing *crossing = reinterpret_cast<RuleCrossing *>(g_p.rule);
        crossing->direction = Chance(50) ? 0 : RandomInt(-2, 2);
        crossing->threshold = Chance(5) ? NaN() : EdgeFloat(RandomInt(0, 60));
        crossing->last = Chance(30) ? crossing->threshold : (Chance(5) ? NaN() : EdgeFloat(RandomInt(0, 60)));
        float value = Chance(30) ? crossing->threshold : (Chance(5) ? NaN() : EdgeFloat(RandomInt(0, 60)));
        RunCase("Crossed", [&] { return uint32_t(Orig_Crossed(crossing, 0, value)); },
                [&] { return uint32_t(crossing->Crossed(value)); });
    }
    RunCase("StubError", [&] { return uint32_t(Orig_StubError()); }, [&] { return uint32_t(StubError()); });
}

// ---- the live objects, copied before the globals are pointed at the private ones

void CopyLive() {
    Guarded([&] {
        PVehicle **slot = PlayerSlotPointer;
        if (slot != NULL && *slot != NULL) {
            RigidBody *body = fgSimulation.GetRigidBody((*slot)->rigidBodySlot);
            if (body != NULL) {
                memcpy(g_liveBody, body, sizeof(g_liveBody));
                if (body->info != NULL)
                    memcpy(g_liveOrientation, body->info, sizeof(g_liveOrientation));
                g_haveBody = true;
            }
        }
    });
    Guarded([&] {
        if (MissionManagerPointer != NULL) {
            memcpy(g_liveMissions, MissionManagerPointer, sizeof(g_liveMissions));
            g_haveMissions = true;
        }
    });
    Guarded([&] {
        uint8_t *manager = static_cast<uint8_t *>(WeaponManagerPointer);
        if (manager != NULL) {
            void *slots;
            memcpy(&slots, manager + 0x10, 4);
            if (slots != NULL) {
                memcpy(g_liveSlots, slots, sizeof(g_liveSlots));
                g_haveSlots = true;
            }
        }
    });
}

} // namespace

void GameMissionsShadowD_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEMISSIONSSHADOWD");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    FpControlGet(&g_x87, &g_sse);

    CopyLive();

    PVehicle **savedPlayer = PlayerSlotPointer;
    void *savedMissions = MissionManagerPointer;
    void *savedWeapons = WeaponManagerPointer;
    int32_t savedSteps = StepCount;
    int32_t savedRate = StepsPerSecond;
    uint8_t savedEventData[0x64];
    memcpy(savedEventData, &gEventDynamicData, sizeof(savedEventData));

    PlayerSlotPointer = &g_p.playerSlot;
    MissionManagerPointer = g_p.missionManager;
    WeaponManagerPointer = g_p.weaponManager;
    g_carVtable[PVehicle::kGetIsInTwoWheelMode] = (void *)&FakeTwoWheels;

    HookInstall(0x000b2700, (const void *)&FakeGetRigidBody);
    HookInstall(0x000b2d30, (const void *)&FakeGetPlayerObject);
    HookInstall(0x00028620, (const void *)&FakeGetStats);
    HookInstall(0x00028700, (const void *)&FakeGetTracking);
    HookInstall(0x000286d0, (const void *)&FakeRedirect);
    HookInstall(0x00027ce0, (const void *)&FakeTrackingProperty);
    HookInstall(0x000b7300, (const void *)&FakeProgEvent);
    HookInstall(0x000b67a0, (const void *)&FakeMagicCounter);
    HookInstall(0x0011e010, (const void *)&FakeTrigger);
    HookInstall(0x0005a5e0, (const void *)&FakeLookup);
    HookInstall(0x00114750, (const void *)&FakeFastAlloc);
    HookInstall(0x001147d0, (const void *)&FakeFastFree);

    TestConstructors();
    TestInstantiate();
    TestDestructors();
    TestCheckRules();
    TestDoRule();
    TestTarget();
    TestTimers();
    TestMaths();

    HooksRemove();
    PlayerSlotPointer = savedPlayer;
    MissionManagerPointer = savedMissions;
    WeaponManagerPointer = savedWeapons;
    StepCount = savedSteps;
    StepsPerSecond = savedRate;
    memcpy(&gEventDynamicData, savedEventData, sizeof(savedEventData));
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);

    printf("[gamemissionsshadowd] rules, targets, timers%s%s%s: %ld cases, %ld checks, %ld differ (%ld faults)\n",
           g_haveBody ? "" : " (no live body)", g_haveMissions ? "" : " (no live missions)",
           g_haveSlots ? "" : " (no live slots)", g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
