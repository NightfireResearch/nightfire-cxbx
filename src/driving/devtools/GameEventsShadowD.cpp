#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameEventsShadowD.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <initializer_list>
#include <map>
#include <string>

#include "../audio/Sound.h"
#include "../audio/SoundManager.h"      // StreamNameList
#include "../data/Carp.h"
#include "../engine/CoreContainers.h"
#include "../game/AICharacter.h"
#include "../game/AIElements.h"
#include "../game/AIHelicopter.h"
#include "../game/AIPedestrian.h"
#include "../game/AIRoadSpawn.h"
#include "../game/AIVehicleController.h"
#include "../game/EventsD.h"
#include "../game/Human.h"
#include "../game/Subtitles.h"
#include "../game/Vehicle.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionInstance.h"
#include "../world/Trigger.h"
#include "../world/World.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GEV_D (game/EventsD.cpp), run once from the first simulation tick when
// NIGHTFIRE_GAMEEVENTSSHADOWD is set. Each case runs the ORIGINAL (the package's 144 entries swapped back, so the
// originals call each other) and then the PORT on the same state, put back between the two, and compares:
//
//   - for every event class: its MakeEvent callback on a synthetic record (each word drawn by the field's kind: the
//     world's instances, elements, effects, positions, groups, strings, random values, NULLs) and random event data,
//     then the event's scalar deleting destructor (flags 1) on what it made - so the constructor, the destructor and
//     Process run too (EStartTimedAction, EStartTimer and EStopTimer through the other packages' deleting destructors,
//     which reach Event's own destructor, ported here);
//   - the six ResolveEvent callbacks, and Event's destructor called directly.
//
// The world is made of stand-ins: instances, scene objects (a fake vtable: GetVelocity, TriggerFX), article effects,
// the third references, a trigger, AI elements, traffic cars, the player's car (a fake vtable), rigid and simple
// bodies with their information, a model group, the mission manager and its timers, the scheduler, the renderer, the
// sounds, the failed streams' list. Every call out of the package (the AI, the missions, the Simulation, the weapons,
// the HUD, the subtitles, the movie player and its viewport, the input, GFX, the scene objects, the sounds, streams
// and fader, the random word, Event's operator new and delete, TagReference::Construct) is a recording fake, the same
// on both sides, answering from the world. Compared: the event's bytes, every byte of the world, the globals the
// events write (the owned explosion slots: the game's copies after the original, ours after the port) and the call
// logs (values, never stack addresses).
//
// A mutation the test sees: EStreamEvent testing GoingToSkipCinematic's whole EAX rather than its low byte - the fake
// answers 0x100 for "no", so the speech stream's QueuePrimary goes missing from the port's log; or ESetSimRate clamping
// with `<` for `>`: the scheduler's time scale in the world differs for any rate but 4.
//
// One summary line: [gameeventsshadowd] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

using CARP::AIElement;

// ---- the game's globals the events read and write

#define fgAIElementControllerGame (*(void **)OwnedGlobals_Ours(0x001ddee4))
#define glbMissionManagerGame (*(void **)OwnedGlobals_Ours(0x00239220))
#define WeaponManagerGame (*(void **)OwnedGlobals_Ours(0x0023923c))
#define fgSchedulerGame (*(void **)OwnedGlobals_Ours(0x001e520c))
#define fgRendererGame (*(void **)OwnedGlobals_Ours(0x001ebff4))
#define fgRenderHighGame (*(void **)OwnedGlobals_Ours(0x001ec488))
#define fgWorldGame (*(void **)OwnedGlobals_Ours(0x0023f310))
#define fgTriggerManagerGame (*(void **)OwnedGlobals_Ours(0x0023e260))
#define ActorDatabaseGame (*(void **)OwnedGlobals_Ours(0x001dd9a0))
#define fgLightManagerGame (*(void **)OwnedGlobals_Ours(0x001ec260))
#define fgBank4Game (*(void **)0x001d83a8)
#define FailedStreamsGame (*(void **)OwnedGlobals_Ours(0x00243a5c))
#define SimCarsGame (*(GameVector<PVehicle *> *)OwnedGlobals_Ours(0x00234e3c))
#define PhysicsObjectsGame ((PhysicsObject **)OwnedGlobals_Ours(0x002342a0))
#define SimpleOwnersGame ((PhysicsObject **)OwnedGlobals_Ours(0x002343a0))
#define RigidSigsGame ((uint32_t *)OwnedGlobals_Ours(0x00234020))
#define SimpleSigsGame ((uint32_t *)OwnedGlobals_Ours(0x00234120))
#define SimStateGame I32_AT(OwnedGlobals_Ours(0x00234e24))
#define SimStepsPerSecondGame I32_AT(OwnedGlobals_Ours(0x00234e2c))
#define SimTimeStepGame FLOAT_AT(OwnedGlobals_Ours(0x00234e30))
#define SimStepCountGame I32_AT(OwnedGlobals_Ours(0x00234e34))
#define StreamsGame U8_AT(OwnedGlobals_Ours(0x001b7056))
#define PlayMoviesGame U8_AT(OwnedGlobals_Ours(0x001b7057))
#define SubtitlesGame U8_AT(OwnedGlobals_Ours(0x001e476d))
#define FramesSinceAudioGame I32_AT(OwnedGlobals_Ours(0x00243a68))
#define AudioClockGame U32_AT(OwnedGlobals_Ours(0x00243a6c))
#define LimitedSoundCountGame I32_AT(OwnedGlobals_Ours(0x00243b30))
#define StingerChannelGame ((char *)0x001d8058)
#define TrafficSpawnCountGame I16_AT(OwnedGlobals_Ours(0x001de918))
#define MissionNameGame ((char *)OwnedGlobals_Ours(0x00244084))
#define EffectsVolumeGame I32_AT(OwnedGlobals_Ours(0x002444f4))
#define ControllerPortGame I32_AT(OwnedGlobals_Ours(0x00244520))
#define ExplosionSoundDueGame ((int32_t *)0x001e23b4)
#define ExplosionSoundSlotGame U32_AT(0x001e23c4)
#define ExplosionEffectSlotGame U32_AT(0x001e23c8)
#define EventDataGame (*(EventDynamicData *)OwnedGlobals_Ours(0x001e47e8))

long g_cases, g_checks, g_differ, g_faults;
int g_reported;
std::map<std::string, long> g_differBy;     // differences by case, listed after the summary

// The first two differences of each case are printed, up to 60 in all
bool ShouldReport(const char *what) {
    return ++g_differBy[what] <= 2 && g_reported++ < 60;
}
unsigned g_x87, g_sse;

// ---- the package's entries

const unsigned kEntries[] = {
    0x00043640, 0x00044400, 0x00044430, 0x00044450, 0x000444f0, 0x00044510, 0x000445e0, 0x00044610, 0x00044640,
    0x00044660, 0x000446c0, 0x000446f0, 0x00044740, 0x00044760, 0x000447e0, 0x00044810, 0x00044830, 0x000448b0,
    0x00044910, 0x00044970, 0x00044bb0, 0x00044be0, 0x00044c20, 0x00044cb0, 0x00044ce0, 0x00044d00, 0x00044d20,
    0x00044d40, 0x00044dc0, 0x00044df0, 0x00044e40, 0x00044e60, 0x00044e80, 0x00044ef0, 0x00044f40, 0x00044fe0,
    0x00045090, 0x00045120, 0x00045250, 0x000452a0, 0x00045330, 0x000453a0, 0x00045720, 0x000457a0, 0x000457f0,
    0x00045880, 0x00045940, 0x000459b0, 0x00045a20, 0x00045a70, 0x00045cb0, 0x00045d00, 0x00045d70, 0x00046020,
    0x00046050, 0x00046120, 0x00046180, 0x000461b0, 0x00046230, 0x00046290, 0x00046320, 0x00046380, 0x00046450,
    0x000464b0, 0x00046540, 0x00046590, 0x000465b0, 0x00046600, 0x00046620, 0x00046680, 0x000466d0, 0x00046720,
    0x000467f0, 0x00046850, 0x000468c0, 0x000468f0, 0x00046960, 0x00046990, 0x000469f0, 0x00046a10, 0x00046ab0,
    0x00046ae0, 0x00046b40, 0x00046b60, 0x00046bc0, 0x00046be0, 0x00046c00, 0x00046c20, 0x00046c70, 0x00046c90,
    0x00046d10, 0x00047230, 0x00048ac0, 0x00048ae0, 0x00048b00, 0x00048b20, 0x00048b40, 0x00048b60, 0x00048b80,
    0x00048ba0, 0x00048bc0, 0x00048be0, 0x00048c00, 0x00048c20, 0x00048c40, 0x00048c60, 0x00048c80, 0x00048ca0,
    0x00048cc0, 0x00048ce0, 0x00048d00, 0x00048d20, 0x00048d40, 0x00048d60, 0x00048d80, 0x00048da0, 0x00048dc0,
    0x00048de0, 0x00048e00, 0x00048e20, 0x00048e40, 0x00048e60, 0x00048e80, 0x00048ea0, 0x0004db10, 0x0004db70,
    0x0004dbd0, 0x0004dcb0, 0x0004dd10, 0x0004de70, 0x0004e860, 0x0004e9c0, 0x0004ea10, 0x0004ee80, 0x0004eea0,
    0x0004eec0, 0x0004eee0, 0x0004ef00, 0x0004ef20, 0x0004ef40, 0x0004ef60, 0x0004f110, 0x0004f130, 0x0004f150,
};
static_assert(sizeof(kEntries) / sizeof(kEntries[0]) == 144, "the package's entries");

// ---- small helpers

uint32_t g_seed = 0x2545f491;
uint32_t Random() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}
uint32_t Random(uint32_t n) {
    return n == 0 ? 0 : Random() % n;
}
bool Chance(uint32_t in) {
    return Random(in) == 0;
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Random() & 0xffff) * (1.0f / 65536.0f);
}
uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}
uint32_t Word(const void *p) {
    return uint32_t(uintptr_t(p));
}

std::string *g_calls;

void Call(const char *format, ...) {
    if (g_calls == NULL)
        return;
    char line[512];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    *g_calls += line;
    *g_calls += '\n';
}

void Vec(const char *name, const void *v, int count) {
    if (v == NULL) {
        Call(" %s NULL", name);
        return;
    }
    const float *f = static_cast<const float *>(v);
    std::string line = std::string(" ") + name;
    char word[12];
    for (int i = 0; i < count; i++) {
        snprintf(word, sizeof(word), " %08x", Bits(f[i]));
        line += word;
    }
    Call("%s", line.c_str());
}

void Text(const char *name, const char *s) {
    Call(" %s %s", name, s != NULL ? s : "(null)");
}

void Differ(const char *what, long index, const char *detail) {
    g_differ++;
    if (ShouldReport(what)) {
        printf("[gameeventsshadowd] DIFF %s, case %ld: %s\n", what, index, detail);
        fflush(stdout);
    }
}

void CheckBytes(const char *what, long index, const char *part, const void *a, const void *b, size_t size) {
    g_checks++;
    size_t at = OwnedGlobals_FirstDifference(a, b, size);   // a vtable or callback: ours, or the game's
    if (at == size)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a);
    const uint8_t *y = static_cast<const uint8_t *>(b);
    char detail[160];
    snprintf(detail, sizeof(detail), "%s at +0x%x: %02x, port %02x", part, unsigned(at), x[at], y[at]);
    Differ(what, index, detail);
}

void CheckLog(const char *what, long index, const std::string &original, const std::string &port) {
    g_checks++;
    if (original == port)
        return;
    size_t at = 0;
    while (at < original.size() && at < port.size() && original[at] == port[at])
        at++;
    size_t from = at > 50 ? at - 50 : 0;
    char detail[400];
    snprintf(detail, sizeof(detail), "calls at %u:\n  orig ...%s\n  port ...%s", unsigned(at),
             original.substr(from, 140).c_str(), port.substr(from, 140).c_str());
    Differ(what, index, detail);
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

struct Originals {
    Originals() {
        for (unsigned entry : kEntries)
            XbeOriginal_Restore(entry, true);
    }
    ~Originals() {
        for (unsigned entry : kEntries)
            XbeOriginal_Restore(entry, false);
    }
};

// ---- jumps to the fakes

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};

bool HookInstall(Hook *h, uint32_t at, const void *to) {
    h->at = at;
    h->on = false;
    DWORD old;
    if (!VirtualProtect((void *)uintptr_t(at), 5, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(h->saved, (void *)uintptr_t(at), 5);
    uint8_t jump[5] = { 0xe9 };
    int32_t rel = int32_t(uint32_t(uintptr_t(to)) - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)uintptr_t(at), jump, 5);
    VirtualProtect((void *)uintptr_t(at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(at), 5);
    h->on = true;
    return true;
}

void HookRemove(Hook *h) {
    if (!h->on)
        return;
    DWORD old;
    VirtualProtect((void *)uintptr_t(h->at), 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy((void *)uintptr_t(h->at), h->saved, 5);
    VirtualProtect((void *)uintptr_t(h->at), 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)uintptr_t(h->at), 5);
    h->on = false;
}

const void *JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return NULL;
    int32_t relative;
    memcpy(&relative, code + 1, 4);
    return reinterpret_cast<const void *>(uintptr_t(at + 5 + relative));
}

// A callee faked for both sides: the jump at its address and, where that address jumps to a port, the port's entry
struct Fake {
    uint32_t address;
    const void *fake;
    Hook entry, port;
};

// ---- the stand-ins

constexpr int kInstances = 3;
constexpr int kSceneObjs = 2;
constexpr int kEffects = 3;
constexpr int kRefs = 2;
constexpr int kElements = 3;
constexpr int kBodies = 4;
constexpr int kTraffic = 20;
constexpr int kStrings = 4;

struct FakeWorld {
    alignas(16) uint8_t events[2][0x60];        // what the fake operator new hands out, in turn
    alignas(16) uint8_t sounds[2][0x120];       // what the fake ABaseSound::operator new hands out
    alignas(16) uint8_t autonomous[0x80];       // what the fake FastAlloc hands out
    alignas(16) CARP::Instance instances[kInstances];
    alignas(16) RSceneObj sceneObjs[kSceneObjs];
    alignas(16) ArticleEffect effects[kEffects];
    EventSceneRef refs[kRefs];
    alignas(16) WTrigger trigger;
    uint8_t triggerPad[0x40];
    AIElement records[kElements];       // the CARP records the events hold
    AIElement elements[kElements];      // what RedirectCarpElement answers for them
    alignas(16) uint8_t car[0x200];     // the player's car (PVehicle), also the elements' and the traffic's objects
    alignas(16) uint8_t spawned[0x80];  // what the spawns answer (a physics object with a fake vtable)
    PVehicle *carVector[1];
    alignas(16) RigidBody bodies[kBodies];
    alignas(16) RigidBodyInfo infos[kBodies];
    alignas(16) SimpleRigidBody simpleBodies[kBodies];
    CarPhysics physics;
    alignas(16) uint8_t audio[0x200];   // the car's AVehicle: terrain +0x178
    alignas(16) uint8_t traffic[kTraffic][0x270];
    uint8_t nav[0x60];
    uint8_t feedback[0x40];
    uint8_t model[0x10];                // a UGroup: flags +4, count +8
    UData groupItems[4];
    uint8_t missionManager[0x720];
    uint8_t weaponManager[0x40];
    uint8_t scheduler[0x40];
    uint8_t renderer[0x80];
    uint8_t renderHigh[0x40];
    uint8_t world[0x10];
    uint8_t triggerManager[0x10];
    uint8_t actorDatabase[0x10];
    uint8_t lightManager[0x300];
    uint8_t bank[0x40];                 // ABank: handle +8, index +0x10
    uint8_t hud[8], subtitles[8], viewport[8], io[0x10], actionQueues[8], fader[8], stream[8];
    uint8_t elementController[8], vehicleController[8], zoneController[8];
    StreamNameList failed;
    PointerListNode failedHead;
    PointerListNode failedNodes[2];
    Coord4 positions[2];
    uint8_t opaque[3][0x40];            // CARP references the events only pass on
    char strings[kStrings][0x10];
    alignas(16) MATRIX4 matrix;         // GetInstancePosition's answer
    Coord4 sceneVelocity;
    PhysicsObject *physicsObjects[64];
    PhysicsObject *simpleOwners[96];
    uint32_t rigidSigs[64];
    uint32_t simpleSigs[96];
    // what the fakes answer
    int32_t newCount;
    int32_t soundCount;
    uint8_t newFails, soundFails, allocFails, hudPresent;
    uint8_t skipping, skippingSoon, useArticle, spawnFails;
    uint8_t smackablePhysics, velocityNull, playerIsSource, lookupFails;
    int8_t sceneObjFor[kInstances];     // GetSceneObjFromInstance's, -1 none
    int8_t listFound;                   // DataLocateTag finds the list
    double health;
    int32_t tick;
    uint32_t randomWord;
    int32_t patch;
    uint32_t sig;
};

FakeWorld *W;
FakeWorld *g_initial;
FakeWorld *g_after;

void *g_carVtable[100];
void *g_sceneObjVtable[24];
void *g_spawnedVtable[8];

int EventIndex(const void *p) {
    for (int i = 0; i < 2; i++)
        if (p == W->events[i])
            return i;
    return -1;
}

// A world address as an offset into the world (so that logs compare), or the word itself
uint32_t At(const void *p) {
    uintptr_t a = uintptr_t(p), w = uintptr_t(W);
    return a >= w && a < w + sizeof(FakeWorld) ? uint32_t(a - w) | 0x80000000u : uint32_t(a);
}

// ---- the globals the events touch

struct Globals {
    int32_t simState;
    int32_t stepsPerSecond;
    float timeStep;
    int32_t step;
    uint8_t streams, playMovies, subtitles, pad;
    int32_t framesSinceAudio;
    uint32_t audioClock;
    int32_t limitedSounds;
    char stinger[sizeof(StingerChannel)];   // as long as ours, which is copied whole
    int16_t trafficCount, pad2;
    char missionName[16];
    int32_t effectsVolume;
    int32_t controllerPort;
    int32_t soundDue[4];
    uint32_t soundSlot;
    uint32_t effectSlot;
    EventDynamicData dynamic;
};

Globals g_globals;

void WriteGlobals(const Globals &g) {
    SimStateGame = g.simState;
    SimStepsPerSecondGame = g.stepsPerSecond;
    SimTimeStepGame = g.timeStep;
    SimStepCountGame = g.step;
    StreamsGame = g.streams;
    PlayMoviesGame = g.playMovies;
    SubtitlesGame = g.subtitles;
    FramesSinceAudioGame = g.framesSinceAudio;
    AudioClockGame = g.audioClock;
    LimitedSoundCountGame = g.limitedSounds;
    memcpy(StingerChannelGame, g.stinger, sizeof(g.stinger));
    memcpy(StingerChannel, g.stinger, sizeof(g.stinger));
    TrafficSpawnCountGame = g.trafficCount;
    memcpy(MissionNameGame, g.missionName, sizeof(g.missionName));
    EffectsVolumeGame = g.effectsVolume;
    ControllerPortGame = g.controllerPort;
    memcpy(ExplosionSoundDueGame, g.soundDue, sizeof(g.soundDue));
    memcpy(ExplosionSoundDue, g.soundDue, sizeof(g.soundDue));
    ExplosionSoundSlotGame = g.soundSlot;
    ExplosionSoundSlot = g.soundSlot;
    ExplosionEffectSlotGame = g.effectSlot;
    ExplosionEffectSlot = g.effectSlot;
    EventDataGame = g.dynamic;
}

// `original`: the owned explosion slots are read from the game's copies
void ReadGlobals(Globals *g, bool original) {
    memset(g, 0, sizeof(*g));
    g->simState = SimStateGame;
    g->stepsPerSecond = SimStepsPerSecondGame;
    g->timeStep = SimTimeStepGame;
    g->step = SimStepCountGame;
    g->streams = StreamsGame;
    g->playMovies = PlayMoviesGame;
    g->subtitles = SubtitlesGame;
    g->framesSinceAudio = FramesSinceAudioGame;
    g->audioClock = AudioClockGame;
    g->limitedSounds = LimitedSoundCountGame;
    memcpy(g->stinger, original ? StingerChannelGame : StingerChannel, sizeof(g->stinger));
    g->trafficCount = TrafficSpawnCountGame;
    memcpy(g->missionName, MissionNameGame, sizeof(g->missionName));
    g->effectsVolume = EffectsVolumeGame;
    g->controllerPort = ControllerPortGame;
    memcpy(g->soundDue, original ? ExplosionSoundDueGame : ExplosionSoundDue, sizeof(g->soundDue));
    g->soundSlot = original ? ExplosionSoundSlotGame : ExplosionSoundSlot;
    g->effectSlot = original ? ExplosionEffectSlotGame : ExplosionEffectSlot;
    g->dynamic = EventDataGame;
}

// =============================================================================================================
// The fakes
// =============================================================================================================

// ---- the event buffer and the CARP references

void *FakeEventNew(size_t size) {
    Call("Event::new %u", unsigned(size));
    if (W->newFails || W->newCount >= 2)
        return NULL;
    return W->events[W->newCount++];
}
void FakeEventDelete(void *event, size_t size) {
    Call("Event::delete %d %u", EventIndex(event), unsigned(size));
}
CARP::TagReference *__fastcall FakeTagConstruct(CARP::TagReference *self, int, UGroup *shared, UGroup *local) {
    Call("TagReference::Construct %08x %08x %08x", self->value, At(shared), At(local));
    self->value = self->value * 3 + 1;
    return self;
}

// ---- the AI

AIElement *__fastcall FakeRedirect(void *self, int, AIElement *element) {
    Call("Redirect %08x", At(element));
    for (int i = 0; i < kElements; i++)
        if (element == &W->records[i])
            return &W->elements[i];
    return &W->elements[0];
}
void __fastcall FakeElementUpdate(void *self, int) {
    Call("AIElementController::Update");
}
void *FakeVehicleControllerGet() {
    Call("AIVehicleController::Get");
    return W->vehicleController;
}
void *__fastcall FakeGetTrafficVehicle(void *self, int, int index) {
    Call("GetTrafficVehicle %d", index);
    return W->traffic[index % kTraffic];
}
PhysicsObject *__fastcall FakeGetPhysicsObject(void *self, int) {
    Call("GetPhysicsObject %08x", At(self));
    return reinterpret_cast<PhysicsObject *>(W->car);
}
void __fastcall FakeSetAttackMode(void *self, int, uint32_t mode) {
    Call("SetAttackMode %08x %02x", At(self), mode & 0xff);
}
void __fastcall FakeSetNavigateMode(void *self, int, uint32_t mode, void *target) {
    Call("SetNavigateMode %08x %02x %08x", At(self), mode & 0xff, At(target));
}
void *FakeZoneControllerGet() {
    Call("AIZoneController::Get");
    return W->zoneController;
}
void __fastcall FakeRegisterZone(void *self, int, int type, const Coord4 *position) {
    Call("RegisterZone %d", type);
    Vec("at", position, 4);
}
void __fastcall FakeInitAtPoint(void *self, int, const Coord3 *point, const Coord3 *heading, uint32_t noLane) {
    Call("InitAtPoint %08x %02x", At(self), noLane & 0xff);
    Vec("point", point, 3);
    Vec("heading", heading, 3);
}

// ---- the mission manager and the weapons

uint32_t __fastcall FakeGoingToSkip(void *self, int) {
    Call("GoingToSkipCinematic");
    return W->skippingSoon ? 1 : 0;             // 0 or 1 in all of EAX, as the real one answers
}
uint32_t __fastcall FakeIsSkipping(void *self, int) {
    Call("IsSkippingCinematic");
    return W->skipping ? 1 : 0;
}
double __fastcall FakeGetPlayerHealth(void *self, int, uint32_t fraction) {
    Call("GetPlayerHealth %02x", fraction & 0xff);
    return W->health;
}
void __fastcall FakeStartTimedAction(void *self, int, int action, float full, float limit, int lo, int hi) {
    Call("StartTimedAction %d %08x %08x %d %d", action, Bits(full), Bits(limit), lo, hi);
}
void __fastcall FakeClearObjective(void *self, int, int slot) {
    Call("ClearObjective %d", slot);
}
void __fastcall FakeCompleteTimedAction(void *self, int, int action) {
    Call("CompleteTimedAction %d", action);
}
void __fastcall FakeSetObjectiveMarker(void *self, int, int slot, const void *marker) {
    Call("SetObjective marker %d %08x", slot, At(marker));
}
void __fastcall FakeSetObjectiveTarget(void *self, int, int slot, const void *target) {
    Call("SetObjective target %d %08x", slot, At(target));
}
void __fastcall FakeSetObjectiveElement(void *self, int, int slot, const void *element) {
    Call("SetObjective element %d %08x", slot, At(element));
}
void __fastcall FakeMissionUpdate(void *self, int) {
    Call("SMissionManager::Update");
}
int TimerIndex(const void *self) {
    return int((uintptr_t(self) - uintptr_t(W->missionManager + 0x360)) / 0x14);
}
void __fastcall FakeTimerReset(void *self, int, int value) {
    Call("timer %d Reset %d", TimerIndex(self), value);
}
void __fastcall FakeTimerStart(void *self, int, int rate) {
    Call("timer %d Start %d", TimerIndex(self), rate);
}
void __fastcall FakeTimerSetTicking(void *self, int, uint32_t on) {
    Call("timer %d SetTicking %02x", TimerIndex(self), on & 0xff);
}
void __fastcall FakeTimerStop(void *self, int) {
    Call("timer %d Stop", TimerIndex(self));
}
void __fastcall FakeUnloadWeapon(void *self, int, int weapon) {
    Call("UnloadWeapon %d", weapon);
}
void __fastcall FakeSetSpecialWeapon(void *self, int, int weapon, const void *trigger, float range) {
    Call("SetSpecialWeapon %d %08x %08x", weapon, At(trigger), Bits(range));
}
void __fastcall FakeWeaponUpdate(void *self, int) {
    Call("SWeaponManager::Update");
}

// ---- the Simulation

void __fastcall FakeAdvanceStep(void *self, int) {
    Call("AdvanceStep");
}
RigidBody *__fastcall FakeGetRigidBody(void *self, int, int slot) {
    Call("GetRigidBody %d", slot);
    return &W->bodies[uint32_t(slot) % kBodies];
}
SimpleRigidBody *__fastcall FakeGetSimpleRigidBody(void *self, int, int slot) {
    Call("GetSimpleRigidBody %d", slot);
    return &W->simpleBodies[uint32_t(slot) % kBodies];
}
void __fastcall FakeSetReflectiveObject(void *self, int, uint32_t signature) {
    Call("SetReflectiveObject %08x", signature);
}
void __fastcall FakeSetStartConditions(void *self, int, const Coord4 *position, const Coord4 *direction) {
    Call("SetStartConditions");
    Vec("position", position, 4);
    Vec("direction", direction, 4);
}
PhysicsObject *__fastcall FakeGetPlayerObject(void *self, int) {
    Call("GetPlayerObject");
    if (W->playerIsSource)
        return reinterpret_cast<PhysicsObject *>(W->opaque[0]);
    return reinterpret_cast<PhysicsObject *>(W->car);
}
void __fastcall FakeDeleteDeadObjects(void *self, int) {
    Call("DeleteDeadObjects");
}
void __fastcall FakeSimulateGame(void *self, int) {
    Call("SimulateGame");
}
void FakeResetRigidBodySP() {
    Call("ResetRigidBodySP");
}
PhysicsObject *__fastcall FakeSpawnSmackable(void *self, int, RSceneObj *object, void *trigger, const Coord3 *velocity,
                                             int unknown4) {
    Call("SpawnSmackableObject %08x %08x %d", At(object), At(trigger), unknown4);
    Vec("velocity", velocity, 3);
    if (object == reinterpret_cast<RSceneObj *>(W->autonomous))
        object->physics = W->smackablePhysics ? reinterpret_cast<PhysicsObject *>(W->spawned) : NULL;
    return W->spawnFails ? NULL : reinterpret_cast<PhysicsObject *>(W->spawned);
}
PhysicsObject *__fastcall FakeSpawnExplosionObject(void *self, int, uint32_t type, uint32_t unused2,
                                                   const Coord4 *direction, const Coord4 *position, float force,
                                                   void *source, const void *sig) {
    Call("SpawnExplosionObject %u %u %08x %08x %08x", type, unused2, Bits(force), At(source), At(sig));
    Vec("direction", direction, 4);
    Vec("position", position, 4);
    return NULL;
}
PhysicsObject *__fastcall FakeSpawnNewton(void *self, int, const float *direction, const float *position,
                                          const Coord3 *momentum, const Coord3 *spin, void *instances, int count,
                                          float mass, float lifetime) {
    Call("SpawnNewtonObject %08x %08x %08x %d %08x %08x", At(direction), At(position), At(instances), count,
         Bits(mass), Bits(lifetime));
    Vec("momentum", momentum, 3);
    Vec("spin", spin, 3);
    return W->spawnFails ? NULL : reinterpret_cast<PhysicsObject *>(W->spawned);
}
PhysicsObject *__fastcall FakeSpawnSentry(void *self, int, void *instance, SentrySetup setup,
                                          uint32_t ownerSig, uint32_t unknown14, uint32_t unknown15,
                                          uint32_t unknown16) {
    Call("SpawnSentryObject %08x %08x %08x %08x %02x", At(instance), ownerSig, unknown14, unknown15,
         unknown16 & 0xff);
    const uint32_t *words = reinterpret_cast<const uint32_t *>(&setup);
    for (size_t i = 0; i < sizeof(setup) / 4; i++)
        Call(" %08x", words[i]);
    return NULL;
}

// ---- the HUD, the subtitles, the loading screen and the movies

void *FakeHudTheApp() {
    Call("GHud::TheApp");
    return W->hudPresent ? W->hud : NULL;
}
void __fastcall FakeTimerOn(void *self, int, int timer) {
    Call("TriggerTimerOn %d", timer);
}
void __fastcall FakeTimerOff(void *self, int, int timer) {
    Call("TriggerTimerOff %d", timer);
}
void *FakeSubtitlesTheApp() {
    Call("GSubtitles::TheApp");
    return W->subtitles;
}
void __fastcall FakeDisplaySubtitle(void *self, int, uint32_t text, float time) {
    Call("DisplaySubtitle %u %08x", text, Bits(time));
}
void __fastcall FakeLoadSubtitles(void *self, int, const char *file) {
    Text("LoadSubtitles", file);
}
void __fastcall FakeClearSubtitles(void *self, int) {
    Call("ClearSubtitles");
}
void FakeStatus(const char *message, ...) {
    Text("Status", message);
}
const char *FakeGetSubDirectory() {
    Call("GetSubDirectory");
    return "pal\\eng";
}
unsigned FakeClearFp() {
    Call("_clearfp");
    return 0;
}
void *__fastcall FakePlayerConstruct(void *self, int, void *device, void *context, int port, int flags) {
    Call("PlayMPC %08x %08x %d %d", At(device), At(context), port, flags);
    return self;
}
void __fastcall FakePlayerInit(void *self, int, const char *path, uint32_t volume, uint32_t mode) {
    Call("PlayMPC::Init %u %02x", volume, mode & 0xff);
    Text("path", path);
}
void __fastcall FakePlayerPlay(void *self, int, void *view, uint32_t widescreen, void *subtitles) {
    // The original hands over GSubtitles::DrawFrame's game address, the port ours: both logged as the game's
    if (subtitles == (void *)&GSubtitles::DrawFrame)
        subtitles = (void *)0x000e3ab0;
    Call("PlayMPC::Play %08x %02x %08x", At(view), widescreen & 0xff, Word(subtitles));
}
void __fastcall FakePlayerDestruct(void *self, int) {
    Call("~PlayMPC");
}
void *__fastcall FakeNewViewPort(void *self, int) {
    Call("NewViewPort %08x", At(self));
    return W->viewport;
}
void __fastcall FakeSetShape(void *self, int, float x, float y, float w, float h, float minZ, float maxZ) {
    Call("SetShape %08x %08x %08x %08x %08x %08x", Bits(x), Bits(y), Bits(w), Bits(h), Bits(minZ), Bits(maxZ));
}
void __fastcall FakeSetOrthographic(void *self, int, float nearZ, float farZ) {
    Call("SetOrthographic %08x %08x", Bits(nearZ), Bits(farZ));
}
void __fastcall FakeDeleteViewPort(void *self, int, void *view) {
    Call("DeleteViewPort %08x", At(view));
}
void *FakeGetIOModule() {
    Call("GetIOModule");
    return W->io;
}
uint32_t __fastcall FakeEnableUpdating(void *self, int, uint32_t enable) {
    Call("EnableUpdating %02x", enable & 0xff);
    return 1;
}
void FakeFeedbackPause() {
    Call("IFeedback::Pause");
}
void *FakeGetActionQueueManager() {
    Call("GetActionQueueManager");
    return W->actionQueues;
}
void __fastcall FakeFlushAllQueues(void *self, int) {
    Call("FlushAllQueues");
}
void FakeSoundPause() {
    Call("ASoundManager::Pause");
}
void FakeSoundResume() {
    Call("ASoundManager::Resume");
}
void FakeStopSoundPos(const Coord3 *position) {
    Call("StopSoundPos %08x", At(position));
}
void *FakeFaderGet(const char *name) {
    Text("AFader::Get", name);
    return W->fader;
}
void __fastcall FakeQueuePrimary(void *self, int, const char *name, float fade, uint32_t effect, uint32_t held) {
    Call("QueuePrimary %08x %02x %02x", Bits(fade), effect & 0xff, held & 0xff);
    Text("name", name);
}
void *FakeStreamGet(const char *name) {
    Text("AStream::Get", name);
    return W->stream;
}
void __fastcall FakeStreamEvent(void *self, int, const char *name, float fade, uint32_t loop, uint32_t effect,
                                uint32_t held) {
    Call("AStream::Event %08x %02x %02x %02x", Bits(fade), loop & 0xff, effect & 0xff, held & 0xff);
    Text("name", name);
}

// ---- the world, the scene objects and GFX

RSceneObj *__fastcall FakeGetSceneObj(void *self, int, const CARP::Instance *instance) {
    Call("GetSceneObjFromInstance %08x", At(instance));
    for (int i = 0; i < kInstances; i++)
        if (instance == &W->instances[i])
            return W->sceneObjFor[i] < 0 ? NULL : &W->sceneObjs[W->sceneObjFor[i]];
    return NULL;
}
uint32_t __fastcall FakeUseArticle(void *self, int, void *model, uint32_t article) {
    Call("UseArticle %08x %08x %08x", At(self), At(model), article);
    return W->useArticle ? 1 : 0x300;
}
void __fastcall FakeShow(void *self, int) {
    Call("Show %08x", At(self));
}
CARP::Instance *__fastcall FakeConvertInstance(void *self, int, CARP::Instance *instance) {
    Call("ConvertInstanceToLocal %08x %08x", At(self), At(instance));
    return &W->instances[kInstances - 1];
}
uint32_t __fastcall FakeGetInstancePosition(void *self, int, uint32_t index, MATRIX4 *transform, uint32_t world) {
    Call("GetInstancePosition %08x %08x %02x", At(self), index, world & 0xff);
    *transform = W->matrix;
    return 0;
}
UData *__fastcall FakeDataLocateTag(void *self, int, uint32_t tag) {
    Call("DataLocateTag %08x %08x", At(self), tag);
    return W->listFound ? &W->groupItems[0] : &W->groupItems[2];
}
void *FakeLookupReference(const char *name) {
    Text("GFX::LookupReference", name);
    return W->lookupFails ? NULL : W->opaque[2];
}
void FakeGfxTrigger(void *effect, const Coord3 *position, const Coord4 *rotation, const Coord3 *offset,
                    const Coord4 *offsetRotation, RSceneObj *owner, uint32_t instance, const Coord3 *velocity) {
    Call("GFX::Trigger %08x %08x %08x %08x", At(effect), At(owner), instance, At(velocity));
    Vec("position", position, 3);
    Vec("rotation", rotation, 4);
    Vec("offset", offset, 3);
    Vec("offsetRotation", offsetRotation, 4);
}
void *FakeFastAlloc(uint32_t size, const char *name) {
    Call("FastAlloc %u", size);
    Text("name", name);
    return W->allocFails ? NULL : W->autonomous;
}
void *__fastcall FakeSceneConstruct(void *self, int, void *instance) {
    Call("RSceneObj::Construct %08x %08x", At(self), At(instance));
    return self;
}
void __fastcall FakeSetTransform(void *self, int, const MATRIX4 *transform) {
    Call("SetTransform %08x", At(self));
    Vec("transform", transform, 16);
}
void *__fastcall FakeAutonomousDelete(void *self, int, unsigned flags) {
    Call("RAutonomousObj::Delete %08x %u", At(self), flags);
    return self;
}
Coord3 *__fastcall FakeSceneGetVelocity(void *self, int) {
    Call("scene GetVelocity %08x", At(self));
    return W->velocityNull ? NULL : reinterpret_cast<Coord3 *>(&W->sceneVelocity);
}
void __fastcall FakeSceneTriggerFX(void *self, int, int type, uint32_t which, uint32_t unused3, const void *unused4,
                                   const void *unused5, float intensity) {
    Call("scene TriggerFX %08x %d %08x %u %08x", At(self), type, which, unused3, Bits(intensity));
    Vec("unused4", unused4, 3);
    Vec("unused5", unused5, 3);
}
uint32_t FakeRandomWord() {
    Call("RandomWord");
    return W->randomWord;
}
void __fastcall FakeApplyForces(void *self, int, const Coord3 *force, const Coord3 *torque) {
    Call("ApplyForces %08x", At(self));
    Vec("force", force, 3);
    Vec("torque", torque, 3);
}
uint32_t *__fastcall FakeGetSig(void *self, int, uint32_t *result) {
    Call("GetSig %08x", At(self));
    *result = W->sig ^ At(self);
    return result;
}
int __fastcall FakeApplyDamage(void *self, int, const void *a, const void *b, float amount, float scale, int kind,
                               const uint32_t *sig) {
    Call("ApplyDamage %08x %08x %08x %d %08x", At(self), Bits(amount), Bits(scale), kind, At(sig));
    Vec("a", a, 3);
    Vec("b", b, 3);
    return 0x20;
}
void FakeSpawnFromEvent(float mass, float lifetime, void *instance, uint32_t simple, int body, void *sceneObj,
                        uint32_t flags) {
    Call("SpawnFromEvent %08x %08x %08x %02x %d %08x %02x", Bits(mass), Bits(lifetime), At(instance), simple & 0xff,
         body, At(sceneObj), flags & 0xff);
}
void __fastcall FakeMissionEditorSwitch(void *self, int, uint32_t on, int which) {
    Call("MissionEditorSwitch %08x %02x %d", At(self), on & 0xff, which);
}
void __fastcall FakeApplyJolt(void *self, int, float strength) {
    Call("ApplyJolt %08x %08x", At(self), Bits(strength));
}
void __fastcall FakeUpdateAll(void *self, int) {
    Call("ActActorDatabase::UpdateAll");
}
void __fastcall FakeReceiveCameraInput(void *self, int) {
    Call("ReceiveCameraInput");
}
void __fastcall FakeTriggerManagerUpdate(void *self, int) {
    Call("WTriggerManager::Update");
}
void FakeUpdateDynamicNodes() {
    Call("UpdateDynamicNodes");
}
void FakeNullFunction() {
    Call("NullFunction");
}
void FakeScorchGround(void *place, const Coord4 *point, float size, int type) {
    Call("ScorchGround %08x %d", Bits(size), type);
    Vec("point", point, 4);
}
void __fastcall FakeAddExplosion(void *self, int, const Coord4 *position, float size) {
    Call("AddExplosion %08x %08x", At(self), Bits(size));
    Vec("position", position, 4);
}
void *__fastcall FakeWorldPosConstruct(void *self, int) {
    Call("WWorldPos");
    return self;
}
int FakeTimerGetTick() {
    Call("TIMER_gettick");
    return W->tick;
}
void *FakeSoundNew(unsigned size, const char *name) {
    Call("ABaseSound::new %u", size);
    Text("name", name);
    if (W->soundFails || W->soundCount >= 2)
        return NULL;
    return W->sounds[W->soundCount++];
}
void *__fastcall FakeOneShotAt(void *self, int, int bank, int patch, const Coord3 *position, float volume,
                               const char *mix, uint32_t loop) {
    Call("AOneShotSound %08x %d %d %08x %02x", At(self), bank, patch, Bits(volume), loop & 0xff);
    Vec("position", position, 3);
    Text("mix", mix);
    return self;
}
int __fastcall FakeIndexLookup(void *self, int, const char *name) {
    Text("AIndex::Lookup", name);
    return W->patch;
}
float __fastcall FakeGetScalarVelocity(void *self, int) {
    Call("GetScalarVelocity %08x", At(self));
    return 3.0f;
}

// ---- the car's virtual methods (its fake vtable) and the spawned object's

void *__fastcall FakeCarGetAudio(void *self, int) {
    Call("car GetAudio");
    return W->audio;
}
void __fastcall FakeCarGetControllerInput(void *self, int) {
    Call("car GetControllerInput");
}
void __fastcall FakeCarForceRollDirection(void *self, int, uint32_t direction) {
    Call("car ForceRollDirection %08x", direction);
}
void __fastcall FakeCarSetVisualDamage(void *self, int, float a, float b, float c, float d) {
    Call("car SetVisualDamage %08x %08x %08x %08x", Bits(a), Bits(b), Bits(c), Bits(d));
}
void __fastcall FakeCarEnableTwoWheels(void *self, int) {
    Call("car EnableTwoWheelStunt");
}
void __fastcall FakeCarDisableTwoWheels(void *self, int) {
    Call("car DisableTwoWheelStunt");
}
void __fastcall FakeCarResetDamage(void *self, int) {
    Call("car ResetDamage");
}
void __fastcall FakeCarClearEMP(void *self, int) {
    Call("car ClearEMPState");
}
void __fastcall FakeCarResetAt(void *self, int, const float *position, const float *direction) {
    Call("car ResetCarAt");
    Vec("position", position, 3);
    Vec("direction", direction, 3);
}
void __fastcall FakeCarEnableBeacon(void *self, int, int unknown) {
    Call("car EnableTargetBeacon %d", unknown);
}
void __fastcall FakeCarDisableBeacon(void *self, int) {
    Call("car DisableTargetBeacon");
}
CarPhysics *__fastcall FakeCarGetPhysics(void *self, int) {
    Call("car GetPhysics");
    return &W->physics;
}
void __fastcall FakeUnexpectedSlot(void *self, int) {
    Call("unexpected slot %08x", At(self));
}

Fake g_fakes[] = {
    { 0x0005a5a0, (const void *)&FakeEventNew },
    { 0x0005a5c0, (const void *)&FakeEventDelete },
    { 0x00117e50, (const void *)&FakeTagConstruct },
    { 0x000286d0, (const void *)&FakeRedirect },
    { 0x00029880, (const void *)&FakeElementUpdate },
    { 0x00035c00, (const void *)&FakeVehicleControllerGet },
    { 0x00035e50, (const void *)&FakeGetTrafficVehicle },
    { 0x00035840, (const void *)&FakeGetPhysicsObject },
    { 0x0002a330, (const void *)&FakeSetAttackMode },
    { 0x0002a0d0, (const void *)&FakeSetNavigateMode },
    { 0x00036a10, (const void *)&FakeZoneControllerGet },
    { 0x00036bb0, (const void *)&FakeRegisterZone },
    { 0x000cc100, (const void *)&FakeInitAtPoint },
    { 0x000b6590, (const void *)&FakeGoingToSkip },
    { 0x000b65b0, (const void *)&FakeIsSkipping },
    { 0x000b67d0, (const void *)&FakeGetPlayerHealth },
    { 0x000b6bd0, (const void *)&FakeStartTimedAction },
    { 0x000b6c50, (const void *)&FakeClearObjective },
    { 0x000b7de0, (const void *)&FakeCompleteTimedAction },
    { 0x000b7f60, (const void *)&FakeSetObjectiveMarker },
    { 0x000b7fb0, (const void *)&FakeSetObjectiveTarget },
    { 0x000b8000, (const void *)&FakeSetObjectiveElement },
    { 0x000b8680, (const void *)&FakeMissionUpdate },
    { 0x000b95e0, (const void *)&FakeTimerReset },
    { 0x000b9600, (const void *)&FakeTimerStart },
    { 0x000b96b0, (const void *)&FakeTimerSetTicking },
    { 0x000b9ce0, (const void *)&FakeTimerStop },
    { 0x000baa70, (const void *)&FakeUnloadWeapon },
    { 0x000bae50, (const void *)&FakeSetSpecialWeapon },
    { 0x000bbdc0, (const void *)&FakeWeaponUpdate },
    { 0x000b2620, (const void *)&FakeAdvanceStep },
    { 0x000b2700, (const void *)&FakeGetRigidBody },
    { 0x000b2730, (const void *)&FakeGetSimpleRigidBody },
    { 0x000b2910, (const void *)&FakeSetReflectiveObject },
    { 0x000b2b50, (const void *)&FakeSetStartConditions },
    { 0x000b2d30, (const void *)&FakeGetPlayerObject },
    { 0x000b3fc0, (const void *)&FakeDeleteDeadObjects },
    { 0x000b4a60, (const void *)&FakeSimulateGame },
    { 0x000b2810, (const void *)&FakeResetRigidBodySP },
    { 0x000b57b0, (const void *)&FakeSpawnSmackable },
    { 0x000b5d50, (const void *)&FakeSpawnExplosionObject },
    { 0x000b5e20, (const void *)&FakeSpawnNewton },
    { 0x000b5fb0, (const void *)&FakeSpawnSentry },
    { 0x000d7c10, (const void *)&FakeHudTheApp },
    { 0x000d8800, (const void *)&FakeTimerOn },
    { 0x000d8810, (const void *)&FakeTimerOff },
    { 0x000e3180, (const void *)&FakeSubtitlesTheApp },
    { 0x000e3790, (const void *)&FakeDisplaySubtitle },
    { 0x000e3840, (const void *)&FakeLoadSubtitles },
    { 0x000e3630, (const void *)&FakeClearSubtitles },
    { 0x000e2ff0, (const void *)&FakeStatus },
    { 0x000e3140, (const void *)&FakeGetSubDirectory },
    { 0x00133c9e, (const void *)&FakeClearFp },
    { 0x00130780, (const void *)&FakePlayerConstruct },
    { 0x001307d0, (const void *)&FakePlayerInit },
    { 0x001308a0, (const void *)&FakePlayerPlay },
    { 0x00130b50, (const void *)&FakePlayerDestruct },
    { 0x000ee010, (const void *)&FakeNewViewPort },
    { 0x000e4340, (const void *)&FakeSetShape },
    { 0x000e4900, (const void *)&FakeSetOrthographic },
    { 0x000ee0a0, (const void *)&FakeDeleteViewPort },
    { 0x00051580, (const void *)&FakeGetIOModule },
    { 0x00051710, (const void *)&FakeEnableUpdating },
    { 0x0004fa00, (const void *)&FakeFeedbackPause },
    { 0x0004f7d0, (const void *)&FakeGetActionQueueManager },
    { 0x0004f3b0, (const void *)&FakeFlushAllQueues },
    { 0x00121070, (const void *)&FakeSoundPause },
    { 0x00121150, (const void *)&FakeSoundResume },
    { 0x001211d0, (const void *)&FakeStopSoundPos },
    { 0x00125d40, (const void *)&FakeFaderGet },
    { 0x001251f0, (const void *)&FakeQueuePrimary },
    { 0x001237d0, (const void *)&FakeStreamGet },
    { 0x001234d0, (const void *)&FakeStreamEvent },
    { 0x000d14a0, (const void *)&FakeGetSceneObj },
    { 0x000908d0, (const void *)&FakeUseArticle },
    { 0x0008db40, (const void *)&FakeShow },
    { 0x0008edd0, (const void *)&FakeConvertInstance },
    { 0x0008dd20, (const void *)&FakeGetInstancePosition },
    { 0x00117b50, (const void *)&FakeDataLocateTag },
    { 0x000d35f0, (const void *)&FakeLookupReference },
    { 0x000d34e0, (const void *)&FakeGfxTrigger },
    { 0x00114750, (const void *)&FakeFastAlloc },
    { 0x0008eec0, (const void *)&FakeSceneConstruct },
    { 0x0008dcb0, (const void *)&FakeSetTransform },
    { 0x0001b180, (const void *)&FakeAutonomousDelete },
    { 0x0003e6f0, (const void *)&FakeRandomWord },
    { 0x0006f2a0, (const void *)&FakeApplyForces },
    { 0x0006f390, (const void *)&FakeGetSig },
    { 0x00061310, (const void *)&FakeSpawnFromEvent },
    { 0x00071000, (const void *)&FakeMissionEditorSwitch },
    { 0x0004f8c0, (const void *)&FakeApplyJolt },
    { 0x00013060, (const void *)&FakeUpdateAll },
    { 0x0008bad0, (const void *)&FakeReceiveCameraInput },
    { 0x000d0c90, (const void *)&FakeTriggerManagerUpdate },
    { 0x000c7200, (const void *)&FakeUpdateDynamicNodes },
    { 0x000d3580, (const void *)&FakeNullFunction },
    { 0x000a5530, (const void *)&FakeScorchGround },
    { 0x0007e620, (const void *)&FakeAddExplosion },
    { 0x000d2fd0, (const void *)&FakeWorldPosConstruct },
    { 0x0010a6c0, (const void *)&FakeTimerGetTick },
    { 0x0011c910, (const void *)&FakeSoundNew },
    { 0x000477d0, (const void *)&FakeOneShotAt },
    { 0x00126d40, (const void *)&FakeIndexLookup },
    { 0x000b1f30, (const void *)&FakeGetScalarVelocity },
};

void InstallFakes() {
    for (Fake &f : g_fakes) {
        const void *port = JumpTarget(f.address);
        HookInstall(&f.entry, f.address, f.fake);
        f.port.on = false;
        if (port != NULL)
            HookInstall(&f.port, uint32_t(uintptr_t(port)), f.fake);
    }
}

void RemoveFakes() {
    for (int i = int(sizeof(g_fakes) / sizeof(g_fakes[0])) - 1; i >= 0; i--) {
        HookRemove(&g_fakes[i].port);
        HookRemove(&g_fakes[i].entry);
    }
}

void BuildVtables() {
    for (void *&slot : g_carVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_carVtable[PVehicle::kGetAudio] = (void *)&FakeCarGetAudio;
    g_carVtable[PVehicle::kGetControllerInput] = (void *)&FakeCarGetControllerInput;
    g_carVtable[PVehicle::kForceRollDirection] = (void *)&FakeCarForceRollDirection;
    g_carVtable[PVehicle::kSetVisualDamage] = (void *)&FakeCarSetVisualDamage;
    g_carVtable[PVehicle::kEnableTwoWheelStunt] = (void *)&FakeCarEnableTwoWheels;
    g_carVtable[PVehicle::kDisableTwoWheelStunt] = (void *)&FakeCarDisableTwoWheels;
    g_carVtable[PVehicle::kResetDamage] = (void *)&FakeCarResetDamage;
    g_carVtable[PVehicle::kClearEMPState] = (void *)&FakeCarClearEMP;
    g_carVtable[PVehicle::kResetCarAt] = (void *)&FakeCarResetAt;
    g_carVtable[PVehicle::kEnableTargetBeacon] = (void *)&FakeCarEnableBeacon;
    g_carVtable[PVehicle::kDisableTargetBeacon] = (void *)&FakeCarDisableBeacon;
    g_carVtable[PVehicle::kGetPhysics] = (void *)&FakeCarGetPhysics;
    for (void *&slot : g_sceneObjVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_sceneObjVtable[13] = (void *)&FakeSceneGetVelocity;
    g_sceneObjVtable[15] = (void *)&FakeSceneTriggerFX;
    for (void *&slot : g_spawnedVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_spawnedVtable[2] = (void *)&FakeApplyDamage;
}

// =============================================================================================================
// The world
// =============================================================================================================

float RandomFloat() {
    switch (Random(12)) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return 4.0f;
    case 3: return -Uniform(0.0f, 4.0f);
    case 4: return Uniform(0.0f, 0.2f);
    case 5: return Uniform(3.5f, 4.5f);
    case 6: return Uniform(150.0f, 700.0f);
    default: return Uniform(-200.0f, 200.0f);
    }
}

void RandomVector(Coord4 *v, float range) {
    v->x = Uniform(-range, range);
    v->y = Uniform(-range, range);
    v->z = Uniform(-range, range);
    v->w = Uniform(-1.0f, 1.0f);
}

void BuildWorld() {
    memset(W, 0, sizeof(FakeWorld));
    for (int i = 0; i < kInstances; i++) {
        float *words = reinterpret_cast<float *>(&W->instances[i]);
        for (int w = 0; w < 16; w++)
            words[w] = Uniform(-60.0f, 60.0f);
        W->instances[i].flags = uint8_t(Random(256));
        // the article reference: a BaseDesc whose model reference leads to a pointer to the model group
        W->instances[i].articleDesc.value = Chance(5) ? 0 : Word(W->opaque[0]);
        W->sceneObjFor[i] = Chance(3) ? -1 : int8_t(Random(kSceneObjs));
    }
    *reinterpret_cast<uint32_t *>(W->opaque[0]) = Word(W->opaque[1]);
    *reinterpret_cast<void **>(W->opaque[1]) = W->model;
    for (int i = 0; i < kSceneObjs; i++) {
        RSceneObj *object = &W->sceneObjs[i];
        object->vtable = reinterpret_cast<RSceneObj_vtbl *>(g_sceneObjVtable);
        object->sourceInstance = &W->instances[Random(kInstances)];
    }
    for (int i = 0; i < kEffects; i++) {
        ArticleEffect *effect = &W->effects[i];
        effect->position.x = Uniform(-5.0f, 5.0f);
        effect->position.y = Uniform(-5.0f, 5.0f);
        effect->position.z = Uniform(-5.0f, 5.0f);
        effect->unknown0C = Uniform(-1.0f, 1.0f);
        effect->flags = uint16_t(Chance(2) ? 0 : 1 << Random(16));
        effect->type = uint8_t(ArticleEffect::kTypeGfx);
        effect->instance = uint8_t(Random(3));
        effect->id = uint8_t(Random(8));
        effect->gfx.reference = Chance(3) ? NULL : reinterpret_cast<UGroup *>(W->opaque[Random(3)]);
        RandomVector(&effect->gfx.rotation, 1.0f);
    }
    for (int i = 0; i < kRefs; i++) {
        W->refs[i].flags = Random(4) == 0 ? 0 : Chance(2) ? EventSceneRef::kSmackable : EventSceneRef::kNewton;
        if (Chance(4))
            W->refs[i].flags |= EventSceneRef::kSmackable | EventSceneRef::kNewton;
        W->refs[i].sceneObj = Chance(4) ? NULL : &W->sceneObjs[Random(kSceneObjs)];
    }
    W->trigger.position.x = Uniform(-80.0f, 80.0f);
    W->trigger.position.y = Uniform(-5.0f, 5.0f);
    W->trigger.position.z = Uniform(-80.0f, 80.0f);
    W->trigger.radius = Uniform(0.0f, 30.0f);
    W->trigger.forward.x = Uniform(-1.0f, 1.0f);
    W->trigger.forward.z = Uniform(-1.0f, 1.0f);
    W->trigger.depth = Uniform(0.0f, 10.0f);
    W->trigger.flags = uint16_t(Random(0x10000));
    for (AIElement *list : { W->records, W->elements }) {
        for (int i = 0; i < kElements; i++) {
            AIElement *element = &list[i];
            element->flags = uint8_t(Random(256));
            element->type = int32_t(Chance(5) ? 2 : Random(2));
            element->state = int32_t(Chance(4) ? 0 : 1);
            element->person.human = reinterpret_cast<Human *>(W->car);
            element->car.helicopter = uint8_t(Chance(3));
            element->object = Chance(4) ? NULL : reinterpret_cast<PhysicsObject *>(W->car);
        }
    }

    PVehicle *car = reinterpret_cast<PVehicle *>(W->car);
    *reinterpret_cast<void ***>(car) = g_carVtable;
    car->rigidBodySlot = int16_t(Random(kBodies));
    car->feedbackObject = Chance(5) ? NULL : reinterpret_cast<IFeedback *>(W->feedback);
    W->carVector[0] = car;
    *reinterpret_cast<void ***>(W->spawned) = g_spawnedVtable;
    reinterpret_cast<PhysicsObject *>(W->spawned)->rigidBodySlot = int16_t(Random(kBodies));
    for (int i = 0; i < kBodies; i++) {
        RigidBody *body = &W->bodies[i];
        body->position = { Uniform(-80.0f, 80.0f), Uniform(-5.0f, 5.0f), Uniform(-80.0f, 80.0f) };
        body->velocity = { RandomFloat(), RandomFloat(), RandomFloat() };
        body->angularVelocity = { RandomFloat(), RandomFloat(), RandomFloat() };
        body->mass = Uniform(100.0f, 2000.0f);
        body->info = &W->infos[i];
        body->sleepState = uint8_t(Chance(3) ? RigidBody::kFrozen : RigidBody::kAwake);
        body->radius = Uniform(1.0f, 6.0f);
        float *m = &W->infos[i].orientation.mtx[0][0];
        for (int w = 0; w < 16; w++)
            m[w] = Uniform(-1.0f, 1.0f);
        W->infos[i].halfExtents = { Uniform(0.5f, 3.0f), Uniform(0.5f, 3.0f), Uniform(0.5f, 3.0f), 0.0f };
        W->simpleBodies[i].velocity = { RandomFloat(), RandomFloat(), RandomFloat() };
        W->simpleBodies[i].acceleration = { RandomFloat(), RandomFloat(), RandomFloat() };
    }
    W->physics.isSub = int32_t(Random(3)) - 1;
    for (int i = 0; i < kTraffic; i++)
        *reinterpret_cast<void **>(&W->traffic[i][0x64]) = W->nav;
    uint32_t *model = reinterpret_cast<uint32_t *>(W->model);
    model[1] = uint32_t(Random(2)) << 5;        // subgroups
    model[2] = Random(2);                        // data items
    model[3] = Word(W->groupItems);              // the items (not relative): the real GetArray, which the port
                                                 // inlines, answers them
    W->listFound = int8_t(Random(2));
    W->groupItems[0].flags = Random(2) * kDataRelative;
    W->groupItems[0].data = W->groupItems[0].flags ? 0x10 : Word(&W->instances[Random(kInstances)]);
    W->groupItems[1].tag = 0;                    // the relative data: an instance's article at +0x1c
    *reinterpret_cast<uint32_t *>(reinterpret_cast<uint8_t *>(&W->groupItems[1]) + 0xc) = Random();

    *reinterpret_cast<int32_t *>(&W->bank[8]) = int32_t(Random(16));
    int32_t *renderer = reinterpret_cast<int32_t *>(W->renderer);
    renderer[0x10] = 640;                        // screenWidth +0x40
    renderer[0x11] = 480;
    W->renderer[0x4c] = uint8_t(Random(2));     // widescreen
    *reinterpret_cast<void **>(&W->renderer[0x60]) = W->opaque[1];
    *reinterpret_cast<void **>(&W->renderer[0x64]) = W->opaque[2];

    // the failed streams: a list whose two names may match the events' sound names
    W->failed.head = Chance(5) ? NULL : &W->failedHead;
    W->failedHead.next = &W->failedNodes[0];
    W->failedNodes[0].next = &W->failedNodes[1];
    W->failedNodes[1].next = &W->failedHead;
    W->failedNodes[0].value = W->strings[Random(kStrings)];
    W->failedNodes[1].value = Chance(2) ? W->strings[3] : W->strings[2];

    for (int i = 0; i < 2; i++)
        RandomVector(&W->positions[i], 100.0f);
    const char *names[] = { "speech", "nis", "music", "SPEECH", "Music", "S1", "S2" };
    for (int i = 0; i < kStrings; i++)
        strcpy(W->strings[i], names[Random(7)]);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            W->matrix.mtx[r][c] = Uniform(-2.0f, 2.0f);
    RandomVector(&W->sceneVelocity, 30.0f);
    for (int i = 0; i < 64; i++) {
        W->physicsObjects[i] = Chance(2) ? NULL : reinterpret_cast<PhysicsObject *>(W->car);
        W->rigidSigs[i] = Random();
    }
    for (int i = 0; i < 96; i++) {
        W->simpleOwners[i] = Chance(2) ? NULL : reinterpret_cast<PhysicsObject *>(W->car);
        W->simpleSigs[i] = Random();
    }
    reinterpret_cast<PhysicsObject *>(W->car)->renderObject = &W->sceneObjs[Random(kSceneObjs)];

    W->newFails = uint8_t(Chance(16));
    W->soundFails = 0;
    W->allocFails = 0;
    W->hudPresent = uint8_t(!Chance(4));
    W->skipping = uint8_t(Random(2));
    W->skippingSoon = uint8_t(Random(2));
    W->useArticle = uint8_t(!Chance(3));
    W->spawnFails = uint8_t(Chance(4));
    W->smackablePhysics = uint8_t(!Chance(4));
    W->velocityNull = uint8_t(Chance(3));
    W->playerIsSource = uint8_t(Chance(3));
    W->lookupFails = uint8_t(Chance(4));
    switch (Random(4)) {
    case 0: W->health = 0.0; break;
    case 1: W->health = -Uniform(0.0f, 1.0f); break;
    default: W->health = Uniform(0.0f, 1.0f); break;
    }
    W->tick = int32_t(Random(1000));
    W->randomWord = Random() << 8 ^ Random();
    W->patch = int32_t(Random(40));
    W->sig = Random();

    // The globals
    Globals &g = g_globals;
    memset(&g, 0, sizeof(g));
    g.simState = Chance(3) ? 3 : 1;
    g.stepsPerSecond = Chance(2) ? 60 : int32_t(30 + Random(60));
    g.timeStep = 1.0f / float(g.stepsPerSecond);
    g.step = int32_t(Random(100000));
    g.streams = uint8_t(!Chance(4));
    g.playMovies = uint8_t(Random(2));
    g.subtitles = uint8_t(Random(2));
    g.framesSinceAudio = int32_t(Random(4));
    g.audioClock = Bits(Uniform(0.0f, 500.0f));
    g.limitedSounds = int32_t(Random(4));
    strcpy(g.stinger, "Speech");
    g.trafficCount = int16_t(Chance(4) ? 32767 : Random(100));
    strcpy(g.missionName, Chance(3) ? "jungleb_mis13b" : Chance(2) ? "JUNGLEB_MIS13B" : "mis07a");
    g.effectsVolume = int32_t(Random(101));
    g.controllerPort = int32_t(Random(4));
    for (int i = 0; i < 4; i++)
        g.soundDue[i] = W->tick - 40 + int32_t(Random(80));
    g.soundSlot = Random(8);
    g.effectSlot = Random(3);
    g.dynamic.instance = &W->instances[Random(kInstances)];
    g.dynamic.sceneObj = Chance(4) ? NULL : &W->sceneObjs[Random(kSceneObjs)];
    g.dynamic.instanceIndex = int32_t(Random(3));
    RandomVector(&g.dynamic.position, 80.0f);
    RandomVector(reinterpret_cast<Coord4 *>(g.dynamic.unknown20), 1.0f);
    g.dynamic.trigger = &W->trigger;
    g.dynamic.flag = uint8_t(Random(2));
    g.dynamic.index = Chance(5) ? -1 : int32_t(Random(kBodies));
    g.dynamic.hasRayShell = uint8_t(Random(2));
    g.dynamic.rayStart = { Uniform(-80.0f, 80.0f), Uniform(-5.0f, 5.0f), Uniform(-80.0f, 80.0f) };
    if (Chance(2))
        g.dynamic.rayStart = W->bodies[reinterpret_cast<PhysicsObject *>(W->spawned)->rigidBodySlot].position;
    g.dynamic.rayEnd = { Uniform(-80.0f, 80.0f), Uniform(-5.0f, 5.0f), Uniform(-80.0f, 80.0f) };
    g.dynamic.rayValue = Uniform(50.0f, 200.0f);
}

void PointGameAtWorld() {
    fgAIElementControllerGame = W->elementController;
    glbMissionManagerGame = W->missionManager;
    WeaponManagerGame = W->weaponManager;
    fgSchedulerGame = W->scheduler;
    fgRendererGame = W->renderer;
    fgRenderHighGame = W->renderHigh;
    fgWorldGame = W->world;
    fgTriggerManagerGame = W->triggerManager;
    ActorDatabaseGame = Chance(4) ? NULL : W->actorDatabase;
    fgLightManagerGame = W->lightManager;
    fgBank4Game = W->bank;
    FailedStreamsGame = &W->failed;
    SimCarsGame.first = W->carVector;
    SimCarsGame.last = W->carVector + 1;
    SimCarsGame.end = W->carVector + 1;
    memcpy(PhysicsObjectsGame, W->physicsObjects, sizeof(W->physicsObjects));
    memcpy(SimpleOwnersGame, W->simpleOwners, sizeof(W->simpleOwners));
    memcpy(RigidSigsGame, W->rigidSigs, sizeof(W->rigidSigs));
    memcpy(SimpleSigsGame, W->simpleSigs, sizeof(W->simpleSigs));
}

// =============================================================================================================
// The cases
// =============================================================================================================

// Runs `call(original)` on both sides from the same world and globals, and compares them
template <class Call>
void Both(const char *what, long index, const Call &call) {
    g_cases++;
    uint32_t seed = g_seed;
    PointGameAtWorld();
    memcpy(g_initial, W, sizeof(FakeWorld));
    Globals initialGlobals = g_globals;
    std::string logOriginal, logPort;
    Globals globalsOriginal, globalsPort;

    WriteGlobals(initialGlobals);
    g_calls = &logOriginal;
    bool originalFaulted = false;
    {
        Originals window;
        if (!Guarded([&] { call(true); })) {
            logOriginal += "<fault>";
            originalFaulted = true;
        }
    }
    g_calls = NULL;
    ReadGlobals(&globalsOriginal, true);
    memcpy(g_after, W, sizeof(FakeWorld));

    memcpy(W, g_initial, sizeof(FakeWorld));
    g_seed = seed;
    PointGameAtWorld();
    WriteGlobals(initialGlobals);
    g_calls = &logPort;
    bool portFaulted = !Guarded([&] { call(false); });
    if (portFaulted)
        logPort += "<fault>";
    g_calls = NULL;
    ReadGlobals(&globalsPort, false);
    // A fault inside an original destructor unwinds through its exception funclet, which puts Event's vtable
    // back; the port (no unwinding of structured exceptions) leaves its own. Faults happen only on made-up data.
    if (originalFaulted && portFaulted)
        for (int i = 0; i < 2; i++)
            memcpy(W->events[i], g_after->events[i], 4);

    CheckBytes(what, index, "world", g_after, W, sizeof(FakeWorld));
    CheckBytes(what, index, "globals", &globalsOriginal, &globalsPort, sizeof(Globals));
    CheckLog(what, index, logOriginal, logPort);
    memcpy(W, g_initial, sizeof(FakeWorld));
}

// A record word by its kind: an upper-case kind is never NULL
uint32_t RecordWord(char kind) {
    bool never = kind >= 'A' && kind <= 'Z';
    char k = char(never ? kind - 'A' + 'a' : kind);
    if (!never && (k == 'e' || k == 'i' || k == 'a' || k == 'g' || k == 'v' || k == 'x' || k == 's' || k == 'r' ||
                   k == 'o' || k == 'p') && Chance(5))
        return 0;
    switch (k) {
    case 'i': return Word(&W->instances[Random(kInstances)]);
    case 'e': return Word(&W->records[Random(kElements)]);
    case 'a': return Word(&W->effects[Random(kEffects)]);
    case 'r': return Word(&W->refs[Random(kRefs)]);
    case 'o': return Word(&W->sceneObjs[Random(kSceneObjs)]);
    case 'g': return Word(W->model);
    case 'v': return Word(&W->positions[Random(2)]);
    case 'x': return Word(W->opaque[Random(3)]);
    case 's': return Word(W->strings[Random(kStrings)]);
    case 'p': return Chance(2) ? Word(W->opaque[0]) : Word(W->car);
    case 'f': return Bits(RandomFloat());
    case 'n': return uint32_t(int32_t(Random(140)) - 20);
    case 'b': return Chance(2) ? 0 : Random(3) == 0 ? Random() : 1;
    case 't': return Random(10);
    case 'z': return Chance(3) ? 0xffffffff : Random(4);
    case 'k': {
        const uint32_t surfaces[] = { 1, 2, 3, 4, 5, 6, 7, 9, 10, 0x50, 0x60, 0x61 };
        return surfaces[Random(12)];
    }
    case 'd': return Chance(3) ? 0x1e : Random(40);
    default: return Random() << 8 ^ Random();
    }
}

struct EventClass {
    const char *name;
    uint32_t make;      // the MakeEvent callback
    uint32_t remove;    // the scalar deleting destructor (another package's for three)
    const char *record; // the kinds of the record's words after the first
};

const EventClass kClasses[] = {
    { "ESetRecordedGameTime", 0x00044430, 0x00048ac0, "" },
    { "ESetReflectiveObject", 0x000444f0, 0x00048ae0, "e" },
    { "ESetRenderVariation", 0x000445e0, 0x00048b00, "iz" },
    { "ESetSimRate", 0x00044640, 0x00048b20, "f" },
    { "ESetSpecialWeapon", 0x000446c0, 0x00048b40, "nvf" },
    { "ESetSubRollDirection", 0x00044740, 0x0004ee80, "u" },
    { "ESetSubtitle", 0x000447e0, 0x00048b60, "nf" },
    { "ESetTerrain", 0x00044810, 0x0004eea0, "u" },
    { "ESetTimer", 0x00044910, 0x00048b80, "tfb" },
    { "ESetVideo", 0x00044bb0, 0x00048ba0, "ss" },
    { "ESetVisualDamage", 0x00044be0, 0x0004eec0, "ennnn" },
    { "ESetWaypoint", 0x00044cb0, 0x00048bc0, "xxe" },
    { "EShake", 0x00044ce0, 0x0004eee0, "" },
    { "EShowInstance", 0x00044d20, 0x00048be0, "" },
    { "EShowInstanceStatic", 0x00044dc0, 0x00048c00, "i" },
    { "ESimEndFrame", 0x00044e40, 0x00048c20, "" },
    { "ESimFrameUpdate", 0x00044e60, 0x0004ef00, "" },
    { "ESpawnDramaticSmackable", 0x00044ef0, 0x00048c40, "fif" },
    { "ESpawnExplosion", 0x00044fe0, 0x0004ef20, "pudfkaus" },
    { "ESpawnExplosionStatic", 0x00045250, 0x00048c60, "viudfks" },
    { "ESpawnForceEffect", 0x00045720, 0x00048c80, "fka" },
    { "ESpawnSentry", 0x00045940, 0x00048ca0, "iuuuuuuuuuuuuub" },
    { "ESpawnSimplePhysics", 0x00045a20, 0x00048cc0, "fif" },
    { "ESpawnSmackable", 0x00045cb0, 0x00048ce0, "gni" },
    { "ESpawnTraffic", 0x00046020, 0x00048d00, "f" },
    { "EStartEffect", 0x00046120, 0x00048d20, "axrn" },
    { "EStartMission", 0x00046180, 0x0004ef40, "f" },
    { "EStartTimedAction", 0x00046230, 0x00047ed0, "nffnn" },
    { "EStartTimer", 0x00046320, 0x00048700, "tn" },
    { "EStopEffect", 0x00046450, 0x00048d40, "axrn" },
    { "EStopSoundPos", 0x00046590, 0x00048d60, "v" },
    { "EStopTimedAction", 0x00046600, 0x00048d80, "n" },
    { "EStopTimer", 0x00046680, 0x00048680, "t" },
    { "EStreamEvent", 0x000466d0, 0x0004f150, "Ssnbbb" },
    { "ESuppressEffect", 0x000467f0, 0x00048da0, "axrn" },
    { "ESwitchEffectOff", 0x000468c0, 0x00048dc0, "en" },
    { "ESwitchEffectOn", 0x00046960, 0x00048de0, "en" },
    { "ESwitchStingerChannel", 0x000469f0, 0x00048e00, "S" },
    { "ETargetBeaconOnOff", 0x00046ab0, 0x00048e20, "eb" },
    { "ETimerDrawOff", 0x00046b40, 0x00048e40, "t" },
    { "ETimerDrawOn", 0x00046bc0, 0x00048e60, "t" },
    { "ETwoWheelsOff", 0x00046be0, 0x0004f110, "" },
    { "ETwoWheelsOn", 0x00046c00, 0x0004f130, "" },
    { "EUnloadWeapon", 0x00046c70, 0x00048e80, "n" },
    { "EWakeupSmackable", 0x00047230, 0x00048ea0, "oiR" },
};

// No signalling-NaN words: the original copies some floats on the x87, which would quiet them (the data has none)
uint32_t NoNaN(uint32_t word) {
    if ((word >> 23 & 0xff) == 0xff)
        word ^= 0x40000000;
    return word;
}

// ESpawnForceEffect leaves its effect field pointing at its own stack: not a value to compare
void MaskStackPointers(const char *name) {
    if (strcmp(name, "ESpawnForceEffect") != 0)
        return;
    for (int i = 0; i < 2; i++) {
        uint32_t *field = reinterpret_cast<uint32_t *>(&W->events[i][0x30]);
        if (*field != 0 && At(reinterpret_cast<void *>(uintptr_t(*field))) == *field)
            *field = 0xdeadbeef;
    }
}

// ESpawnExplosionStatic with neither a position nor an instance: the original's explosion position is what its
// stack held (the port's is zero), so it is not compared
void MaskUnwritten(const char *name, const uint32_t *record) {
    if (strcmp(name, "ESpawnExplosionStatic") != 0 || record[1] != 0 || record[2] != 0 || W->newCount < 2)
        return;
    memset(&W->events[1][0x04], 0xee, 0x10);
}

void TestEvents(int round) {
    static uint32_t record[0x14];
    for (const EventClass &type : kClasses) {
        BuildWorld();
        memset(record, 0, sizeof(record));
        record[0] = Random();
        for (int i = 0; type.record[i] != 0; i++)
            record[1 + i] = NoNaN(RecordWord(type.record[i]));
        Both(type.name, round, [&](bool) {
            ((void (*)(const void *))type.make)(record);
            if (W->newCount > 0)
                ((void *(__fastcall *)(void *, int, unsigned))type.remove)(W->events[0], 0, 1);
            MaskStackPointers(type.name);
            MaskUnwritten(type.name, record);
        });
    }
}

void TestOthers(int round) {
    static uint32_t record[0x10];
    const uint32_t resolvers[] = { 0x000446f0, 0x00045090, 0x000452a0, 0x000457a0, 0x00045d00, 0x000464b0 };
    for (uint32_t resolver : resolvers) {
        BuildWorld();
        uint32_t words[0x10];
        for (uint32_t &word : words)
            word = Random();
        Both("ResolveEvent", round, [&](bool) {
            memcpy(record, words, sizeof(record));      // the same record for both sides
            ((void (*)(void *, void *, void *))resolver)(record, W->opaque[0], W->opaque[1]);
            Vec("record", record, 9);
        });
    }
    BuildWorld();
    Both("Event::Destruct", round, [&](bool) {
        ((void (__fastcall *)(void *, int))0x00043640)(W->events[1], 0);
    });
}

constexpr int kRounds = 40;

} // namespace

void GameEventsShadowD_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEEVENTSSHADOWD");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    W = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    g_initial = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    g_after = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    if (W == NULL || g_initial == NULL || g_after == NULL) {
        printf("[gameeventsshadowd] no memory for the world\n");
        fflush(stdout);
        return;
    }

    // The live state the cases replace, put back after
    Globals live;
    ReadGlobals(&live, true);
    char liveStinger[sizeof(StingerChannel)];
    memcpy(liveStinger, StingerChannel, sizeof(liveStinger));
    int32_t liveSoundDue[4];
    memcpy(liveSoundDue, ExplosionSoundDue, sizeof(liveSoundDue));
    uint32_t liveSoundSlot = ExplosionSoundSlot;
    uint32_t liveEffectSlot = ExplosionEffectSlot;
    void *livePointers[12] = {
        fgAIElementControllerGame, glbMissionManagerGame, WeaponManagerGame, fgSchedulerGame, fgRendererGame,
        fgRenderHighGame, fgWorldGame, fgTriggerManagerGame, ActorDatabaseGame, fgLightManagerGame, fgBank4Game,
        FailedStreamsGame,
    };
    GameVector<PVehicle *> liveCars = SimCarsGame;
    static PhysicsObject *livePhysics[64];
    static PhysicsObject *liveSimple[96];
    static uint32_t liveRigidSigs[64];
    static uint32_t liveSimpleSigs[96];
    memcpy(livePhysics, PhysicsObjectsGame, sizeof(livePhysics));
    memcpy(liveSimple, SimpleOwnersGame, sizeof(liveSimple));
    memcpy(liveRigidSigs, RigidSigsGame, sizeof(liveRigidSigs));
    memcpy(liveSimpleSigs, SimpleSigsGame, sizeof(liveSimpleSigs));

    BuildVtables();
    InstallFakes();
    for (int round = 0; round < kRounds; round++) {
        TestEvents(round);
        TestOthers(round);
    }
    RemoveFakes();

    fgAIElementControllerGame = livePointers[0];
    glbMissionManagerGame = livePointers[1];
    WeaponManagerGame = livePointers[2];
    fgSchedulerGame = livePointers[3];
    fgRendererGame = livePointers[4];
    fgRenderHighGame = livePointers[5];
    fgWorldGame = livePointers[6];
    fgTriggerManagerGame = livePointers[7];
    ActorDatabaseGame = livePointers[8];
    fgLightManagerGame = livePointers[9];
    fgBank4Game = livePointers[10];
    FailedStreamsGame = livePointers[11];
    SimCarsGame = liveCars;
    memcpy(PhysicsObjectsGame, livePhysics, sizeof(livePhysics));
    memcpy(SimpleOwnersGame, liveSimple, sizeof(liveSimple));
    memcpy(RigidSigsGame, liveRigidSigs, sizeof(liveRigidSigs));
    memcpy(SimpleSigsGame, liveSimpleSigs, sizeof(liveSimpleSigs));
    WriteGlobals(live);
    memcpy(StingerChannel, liveStinger, sizeof(liveStinger));
    memcpy(ExplosionSoundDue, liveSoundDue, sizeof(liveSoundDue));
    ExplosionSoundSlot = liveSoundSlot;
    ExplosionEffectSlot = liveEffectSlot;
    VirtualFree(W, 0, MEM_RELEASE);
    VirtualFree(g_initial, 0, MEM_RELEASE);
    VirtualFree(g_after, 0, MEM_RELEASE);

    for (const auto &d : g_differBy)
        printf("[gameeventsshadowd]   %s: %ld differ\n", d.first.c_str(), d.second);
    printf("[gameeventsshadowd] the events ESetRecordedGameTime..EWakeupSmackable: %ld cases, %ld checks, %ld differ "
           "(%ld faults)\n", g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
