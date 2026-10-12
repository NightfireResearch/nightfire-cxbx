#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameEventsShadowC.h"
#include "FpControl.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <map>
#include <string>

#include "../anim/AnimEngine.h"
#include "../audio/Sound.h"
#include "../camera/PlayerCamera.h"
#include "../data/Carp.h"
#include "../engine/CoreContainers.h"
#include "../game/AICharacter.h"
#include "../game/AIElements.h"
#include "../game/AIHelicopter.h"
#include "../game/AIPedestrian.h"
#include "../game/AIRoadSpawn.h"
#include "../game/AIVehicleController.h"
#include "../game/EventsC.h"
#include "../game/Missile.h"
#include "../game/MissionManager.h"
#include "../game/WeaponManager.h"
#include "../game/Vehicle.h"
#include "../physics/RigidBody.h"
#include "../render/RenderHigh.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionInstance.h"
#include "../world/Trigger.h"
#include "../world/World.h"
#include "../Scheduler.hpp"               // RealClock_InterruptHandler
#include "../platform/RealSystem.h"      // TIMER_addtask
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GEV_C (game/EventsC.cpp), run once from the first simulation tick when
// NIGHTFIRE_GAMEEVENTSSHADOWC is set. Each case runs the ORIGINAL (the package's 221 entries swapped back, so the
// originals call each other) and then the PORT on the same state, put back between the two, and compares:
//
//   - for every event class: its MakeEvent callback on a synthetic record (each word drawn by the field's kind: the
//     world's instances, elements, effects, places, strings, random values, NULLs) and random event data, then the
//     event's scalar deleting destructor (flags 1) on what it made - so the destructor and Process run too;
//   - ECameraLockOn's MakeEvent; the two ResolveEvent callbacks; the three constructors; EPathUtil_GetFromInstanceOrAI,
//     CARP::Instance::GetMatrix4 and Event::Delete.
//
// The world is made of stand-ins: instances, scene objects with their path and animation handles, article effects,
// places, proc-anim states, a trigger, AI elements with their vehicles and characters, splines, the player's car (a
// fake vtable), rigid bodies, the car's physics and audio, a sound, the mission manager, the weapons, the camera
// and the renderer's views. Every call out of the package (the AI, the missions, the Simulation, the weapons, the
// HUD, GFX, the camera, the paths, the sounds and mixes, the world, the scheduler, the random sequence, Event's
// operator new and delete, TagReference::Construct, AOneShotSound's constructor) is a recording fake, the same on
// both sides, answering from the world. Compared: the event's bytes, every byte of the world, the globals the events
// write (the owned LastRenderClock: the game's copy after the original, ours after the port) and the call logs
// (values, never stack addresses).
//
// A mutation the test sees: EPlayerLose testing the health with `< 0` instead of `<= 0`: the launch page's end state
// (and, in auto-drive, the blood curtain's calls) differ when the fake health is 0; or the path setters passing
// `running` to SetRunning as it is rather than `running != 0`: the logged byte differs for a word like 0x100.
//
// One summary line: [gameeventsshadowc] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

using CARP::AIElement;

// ---- the game's globals the events read and write

#define fgAIElementControllerGame (*(void **)0x001ddee4)
#define glbMissionManagerGame (*(void **)0x00239220)
#define WeaponManagerGame (*(void **)0x0023923c)
#define fgRenderHighGame (*(void **)0x001ec488)
#define fgRendererGame (*(void **)0x001ebff4)
#define FogGame (*(void **)0x001ec004)
#define ColorizeGame (*(void **)0x001f6898)
#define fgCollisionMgrGame (*(void **)0x00239a70)
#define fgWorldGame (*(void **)0x0023f310)
#define fgSchedulerGame (*(void **)0x001e520c)
#define fgTriggerManagerGame (*(void **)0x0023e260)
#define AIHandsGame (*(void **)0x001dda90)
#define fgBank0Game (*(void **)0x001d8398)
#define SimCarsGame (*(GameVector<PVehicle *> *)0x00234e3c)
#define PhysicsObjectsGame ((PhysicsObject **)0x002342a0)
#define SimpleOwnersGame ((PhysicsObject **)0x002343a0)
#define SimStateGame I32_AT(0x00234e24)
#define SimStepsPerSecondGame I32_AT(0x00234e2c)
#define SimStepCountGame U32_AT(0x00234e34)
#define FeedbackAllowedGame U8_AT(0x001b6f50)
#define SoundOptionGame U8_AT(0x001b7054)
#define StreamsGame U8_AT(0x001b7056)
#define SuperEasyGame U8_AT(0x001e476c)
#define StreamHoldGame I32_AT(0x00243a80)
#define RealClockGame U32_AT(0x001e5208)
#define LastRenderClockGame U32_AT(0x001e2394)
#define CullDistanceGame U32_AT(0x001c4624)
#define FrameAnimationTimeGame U32_AT(0x001c4668)
#define VideoModeRateGame FLOAT_AT(0x001f2a44)
#define GameTickGame U32_AT(0x001f2a4c)
#define PlayerGravityGame U32_AT(0x00233fb8)
#define Launch978Game I32_AT(0x00244508)
#define LaunchRestartFlagGame U8_AT(0x002441b8)
#define LaunchRestartCountGame I32_AT(0x002441c0)
#define HealthTimes100Game U8_AT(0x00239218)
#define TuningLevelGame ((char *)0x00244084)
#define EventDataGame (*(EventDynamicData *)0x001e47e8)

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
    0x0003da90, 0x0003db00, 0x0003db30, 0x0003ec60, 0x00041850, 0x00041890, 0x00041900, 0x00041930, 0x00041990,
    0x000419c0, 0x00041a30, 0x00041a60, 0x00041ac0, 0x00041af0, 0x00041b60, 0x00041b90, 0x00041bf0, 0x00041c20,
    0x00041c90, 0x00041cc0, 0x00041d30, 0x00041d60, 0x00041dd0, 0x00041e00, 0x00041e60, 0x00041e90, 0x00041f10,
    0x00041f40, 0x00041fb0, 0x00041fe0, 0x00042050, 0x00042060, 0x000420d0, 0x000420f0, 0x000421b0, 0x000421e0,
    0x00042310, 0x00042350, 0x00042470, 0x000424b0, 0x00042500, 0x00042700, 0x00042750, 0x00042810, 0x00042840,
    0x000428b0, 0x000428d0, 0x00042950, 0x00042980, 0x000429d0, 0x00042a80, 0x00042ae0, 0x00042b50, 0x00042b90,
    0x00042bb0, 0x00042be0, 0x00042cc0, 0x00042d20, 0x00042df0, 0x00042e20, 0x00042e50, 0x00042e70, 0x00042e90,
    0x00042eb0, 0x00042ee0, 0x00042f80, 0x00042fa0, 0x00043010, 0x00043040, 0x00043070, 0x000430c0, 0x000430e0,
    0x00043260, 0x00043290, 0x000432c0, 0x000432e0, 0x00043370, 0x00043390, 0x000433f0, 0x00043410, 0x00043430,
    0x00043460, 0x00043470, 0x00043490, 0x000434a0, 0x000434c0, 0x00043530, 0x00043560, 0x00043610, 0x00043650,
    0x00043670, 0x00043690, 0x000436b0, 0x00043710, 0x00043740, 0x000437a0, 0x000437d0, 0x00043890, 0x000438d0,
    0x00043930, 0x00043960, 0x000439e0, 0x00043a00, 0x00043ac0, 0x00043af0, 0x00043b60, 0x00043b80, 0x00043ba0,
    0x00043c30, 0x00043c60, 0x00043d20, 0x00043d50, 0x00043df0, 0x00043e20, 0x00043e90, 0x00043ec0, 0x00043f40,
    0x00043f70, 0x00043fa0, 0x00043fc0, 0x00043fe0, 0x00044020, 0x00044040, 0x00044050, 0x00044070, 0x000440d0,
    0x00044100, 0x00044160, 0x00044190, 0x000441f0, 0x00044220, 0x00044250, 0x000442b0, 0x000442e0, 0x00044390,
    0x000443d0, 0x00048170, 0x000482c0, 0x000482e0, 0x00048300, 0x00048320, 0x00048340, 0x00048360, 0x00048380,
    0x000483a0, 0x000483c0, 0x000483e0, 0x00048400, 0x00048420, 0x00048440, 0x00048460, 0x00048480, 0x000484a0,
    0x000484c0, 0x000484e0, 0x00048500, 0x00048520, 0x00048540, 0x00048620, 0x00048640, 0x00048660, 0x000486a0,
    0x000486c0, 0x000486e0, 0x00048720, 0x00048740, 0x00048760, 0x00048780, 0x000487a0, 0x000487c0, 0x000487e0,
    0x00048800, 0x00048820, 0x00048840, 0x00048860, 0x00048880, 0x000488a0, 0x000488c0, 0x000488e0, 0x00048900,
    0x00048920, 0x00048940, 0x00048960, 0x00048980, 0x000489a0, 0x000489c0, 0x000489e0, 0x00048a00, 0x00048a20,
    0x00048a40, 0x00048a60, 0x00048a80, 0x00048aa0, 0x0004cda0, 0x0004cdc0, 0x0004cf30, 0x0004cf50, 0x0004d1e0,
    0x0004d240, 0x0004d3d0, 0x0004d460, 0x0004d4c0, 0x0004d620, 0x0004d7c0, 0x0004d850, 0x0004d930, 0x0004d9d0,
    0x0004da70, 0x0004ece0, 0x0004ed00, 0x0004ed20, 0x0004ed40, 0x0004ed60, 0x0004ed80, 0x0004eda0, 0x0004edc0,
    0x0004ede0, 0x0004ee00, 0x0004ee20, 0x0004ee40, 0x0004ee60,
};
static_assert(sizeof(kEntries) / sizeof(kEntries[0]) == 221, "the package's entries");

// ---- small helpers

uint32_t g_seed = 0x6d2b79f5;
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
        printf("[gameeventsshadowc] DIFF %s, case %ld: %s\n", what, index, detail);
        fflush(stdout);
    }
}

void CheckBytes(const char *what, long index, const char *part, const void *a, const void *b, size_t size) {
    g_checks++;
    if (memcmp(a, b, size) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a);
    const uint8_t *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
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

constexpr int kInstances = 2;
constexpr int kSceneObjs = 2;
constexpr int kEffects = 3;
constexpr int kElements = 3;
constexpr int kBodies = 4;
constexpr int kStrings = 4;

struct FakeWorld {
    alignas(16) uint8_t events[2][0x40];        // what the fake operator new hands out, in turn
    alignas(16) uint8_t shot[sizeof(AOneShotSound)];    // what the fake ABaseSound::operator new hands out
    alignas(16) CARP::Instance instances[kInstances];
    alignas(16) RSceneObj sceneObjs[kSceneObjs];
    alignas(16) uint8_t paths[kSceneObjs][0xd0];        // RPathHandle: velocity +0x88, running +0xcc
    uint8_t handles[kSceneObjs][0x40];                  // the scene objects' animation handles
    alignas(16) ArticleEffect effects[kEffects];
    alignas(16) EventPlace place;
    ProcAnimState procAnimStates[2];
    alignas(16) WTrigger trigger;
    uint8_t triggerPad[0x40];
    AIElement records[kElements];       // the CARP records the events hold
    AIElement elements[kElements];      // what RedirectCarpElement answers for them
    alignas(16) uint8_t vehicles[kElements][0x230];     // AIGroundVehicle: autoDriveSpeed +0x124
    uint8_t characters[kElements][0x140];               // the avoid zone +0x120..+0x13c
    uint8_t splinePaths[kElements][0x80];               // AISplinePath: path +0x60
    alignas(16) CARP::AISpline aiSpline;
    alignas(16) uint8_t car[0x200];                     // the player's car (PVehicle)
    PVehicle *carVector[1];
    alignas(16) RigidBody bodies[kBodies];
    CarPhysics physics;
    alignas(16) uint8_t audio[0x240];                   // the car's AVehicle: vehicleType +0xd0, zoom +0x190
    uint8_t zoom[0x40];
    alignas(16) uint8_t sound[0x120];                   // EPathSFXUpdate's ASound
    uint8_t taskData[0x18];
    uint8_t missionManager[0x500];
    uint8_t missile[0x710];
    uint8_t weaponManager[0x80];
    WeaponRecord weapons[32];
    alignas(16) uint8_t camera[sizeof(RPlayerCamera)];
    alignas(16) uint8_t renderHigh[sizeof(RRenderHigh)];
    uint8_t renderer[0x80];
    uint8_t fog[0x40];
    uint8_t colorize[0x40];
    uint8_t collisionMgr[0x40];
    uint8_t world[0x40];
    uint8_t triggerManager[0x10];
    Schedule *scheduler[4];
    uint8_t schedules[2][0x40];
    uint8_t mix[0x40];                                  // AMix: volume +4, previous +8
    uint8_t bank[0x40];                                 // ABank: handle +8, index +0x10
    uint8_t hands[8];
    uint8_t elementController[8];
    uint8_t vehicleController[8];
    uint8_t roadSpawn[8];
    uint8_t roadNetwork[8];
    uint8_t hud[8];
    Coord4 positions[2];
    uint8_t opaque[3][0x40];                            // CARP references the events only pass on
    char strings[kStrings][0x10];
    char carType[0x10];
    alignas(16) MATRIX4 matrix;                         // GetInstancePosition's and LocateFX's answer
    Coord4 sceneVelocity;
    PhysicsObject *physicsObjects[64];
    PhysicsObject *simpleOwners[96];
    // what the fakes answer
    int32_t newCount;
    uint8_t newFails;
    uint8_t shotFails;
    uint8_t hudPresent;
    uint8_t skipping;
    uint8_t heightHit;
    uint8_t collisionHit;
    uint8_t removeHit;
    int8_t sceneObjFor[kInstances];     // GetSceneObjFromInstance's, -1 none
    int8_t pathFor[kInstances];         // GetPathHandle's
    int8_t splineFor[kElements + 1];    // GetSplinePath's, by vehicle (the last: the player's)
    int8_t effectById;                  // FindEffectByID's
    float duration;
    float presetVolume;
    float height;
    double health;
    int32_t magicCounter;
    uint32_t carColour;
    uint32_t taskNum;
    int32_t patch;
    uint32_t randomAt;
    uint32_t randoms[8];
};

FakeWorld *W;
FakeWorld *g_initial;
FakeWorld *g_after;

void *g_carVtable[100];
void *g_sceneObjVtable[24];
void *g_soundVtable[4];
void *g_audioVtable[12];

int EventIndex(const void *p) {
    for (int i = 0; i < 2; i++)
        if (p == W->events[i])
            return i;
    return -1;
}

// ---- the globals the events touch

struct Globals {
    int32_t simState;
    int32_t stepsPerSecond;
    uint32_t step;
    uint8_t feedbackAllowed, soundOption, streams, superEasy;
    int32_t streamHold;
    uint32_t realClock;
    uint32_t lastRenderClock;
    uint32_t cullDistance;
    uint32_t frameAnimationTime;
    float videoModeRate;
    uint32_t gameTick;
    uint32_t playerGravity;
    int32_t launch978;
    uint8_t restartFlag, healthTimes100, pad[2];
    int32_t restartCount;
    char tuningLevel[16];
    uint32_t randomSeed, randomMultiplier;      // RandomShort's
    EventDynamicData dynamic;
};

Globals g_globals;

void WriteGlobals(const Globals &g) {
    SimStateGame = g.simState;
    SimStepsPerSecondGame = g.stepsPerSecond;
    SimStepCountGame = g.step;
    FeedbackAllowedGame = g.feedbackAllowed;
    SoundOptionGame = g.soundOption;
    StreamsGame = g.streams;
    SuperEasyGame = g.superEasy;
    StreamHoldGame = g.streamHold;
    RealClockGame = g.realClock;
    LastRenderClockGame = g.lastRenderClock;
    LastRenderClock = g.lastRenderClock;
    CullDistanceGame = g.cullDistance;
    FrameAnimationTimeGame = g.frameAnimationTime;
    VideoModeRateGame = g.videoModeRate;
    GameTickGame = g.gameTick;
    PlayerGravityGame = g.playerGravity;
    Launch978Game = g.launch978;
    LaunchRestartFlagGame = g.restartFlag;
    HealthTimes100Game = g.healthTimes100;
    LaunchRestartCountGame = g.restartCount;
    memcpy(TuningLevelGame, g.tuningLevel, sizeof(g.tuningLevel));
    RandomSeed = g.randomSeed;
    RandomMultiplier = g.randomMultiplier;
    EventDataGame = g.dynamic;
}

// `original`: the owned clock is read from the game's copy
void ReadGlobals(Globals *g, bool original) {
    memset(g, 0, sizeof(*g));
    g->simState = SimStateGame;
    g->stepsPerSecond = SimStepsPerSecondGame;
    g->step = SimStepCountGame;
    g->feedbackAllowed = FeedbackAllowedGame;
    g->soundOption = SoundOptionGame;
    g->streams = StreamsGame;
    g->superEasy = SuperEasyGame;
    g->streamHold = StreamHoldGame;
    g->realClock = RealClockGame;
    g->lastRenderClock = original ? LastRenderClockGame : LastRenderClock;
    g->cullDistance = CullDistanceGame;
    g->frameAnimationTime = FrameAnimationTimeGame;
    g->videoModeRate = VideoModeRateGame;
    g->gameTick = GameTickGame;
    g->playerGravity = PlayerGravityGame;
    g->launch978 = Launch978Game;
    g->restartFlag = LaunchRestartFlagGame;
    g->healthTimes100 = HealthTimes100Game;
    g->restartCount = LaunchRestartCountGame;
    memcpy(g->tuningLevel, TuningLevelGame, sizeof(g->tuningLevel));
    g->randomSeed = RandomSeed;
    g->randomMultiplier = RandomMultiplier;
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
void __fastcall FakeEventDestruct(Event *self, int) {
    Call("Event::Destruct %d", EventIndex(self));
    self->vtable = (vtable_Event *)0x0018c3ac;
}
CARP::TagReference *__fastcall FakeTagConstruct(CARP::TagReference *self, int, UGroup *shared, UGroup *local) {
    Call("TagReference::Construct %08x %08x %08x", self->value, Word(shared), Word(local));
    self->value = self->value * 3 + 1;
    return self;
}

// ---- the AI

AIElement *__fastcall FakeRedirect(void *self, int, AIElement *element) {
    Call("Redirect %08x", Word(element));
    for (int i = 0; i < kElements; i++)
        if (element == &W->records[i])
            return &W->elements[i];
    return &W->elements[0];
}
void __fastcall FakeEveryoneChill(void *self, int) {
    Call("EveryoneChill");
}
void FakeElementControllerDestruct() {
    Call("AIElementController::Destruct");
}
void FakeCharacterInit() {
    Call("AICharacter::Init");
}
void FakeCharacterShutdown() {
    Call("AICharacter::Shutdown");
}
void __fastcall FakeShowHands(void *self, int) {
    Call("ShowHands");
}
void __fastcall FakeHideHands(void *self, int) {
    Call("HideHands");
}
void *__fastcall FakeGetSplinePath(void *self, int) {
    int at = kElements;
    for (int i = 0; i < kElements; i++)
        if (self == W->vehicles[i])
            at = i;
    Call("GetSplinePath %d", at);
    return W->splineFor[at] < 0 ? NULL : W->splinePaths[W->splineFor[at]];
}
void __fastcall FakeSetNextSplinePath(void *self, int, void *spline) {
    Call("SetNextSplinePath %08x %08x", Word(self), Word(spline));
}
void *FakeVehicleControllerGet() {
    Call("AIVehicleController::Get");
    return W->vehicleController;
}
void FakeVehicleControllerReset() {
    Call("AIVehicleController::Reset");
}
void __fastcall FakeCreateSimTrafficCars(void *self, int, int unknown) {
    Call("CreateSimTrafficCars %08x %d", Word(self), unknown);
}
void __fastcall FakeRegisterSimCarList(void *self, int) {
    Call("RegisterSimCarList %08x", Word(self));
}
void *FakeRoadSpawnGet() {
    Call("AIRoadSpawn::Get");
    return W->roadSpawn;
}
void FakeRoadSpawnRestart() {
    Call("AIRoadSpawn::Restart");
}

// ---- the mission manager

const char *__fastcall FakeGetCarType(void *self, int) {
    Call("GetCarType");
    return W->carType;
}
void __fastcall FakeMissionStart(void *self, int) {
    Call("SMissionManager::Start");
}
void __fastcall FakeMissionStop(void *self, int) {
    Call("SMissionManager::Stop");
}
void __fastcall FakeMissionSetAutoDrive(void *self, int, uint32_t on, uint32_t controls) {
    Call("SMissionManager::SetAutoDrive %02x %02x", on & 0xff, controls & 0xff);
}
uint32_t __fastcall FakeIsSkippingCinematic(void *self, int) {
    Call("IsSkippingCinematic");
    return W->skipping != 0 ? 1 : 0;            // 0 or 1 in all of EAX, as the real one answers
}
void __fastcall FakeBoostPlayerHealth(void *self, int, float fraction) {
    Call("BoostPlayerHealth %08x", Bits(fraction));
}
void __fastcall FakeBoostPlayerShield(void *self, int, float fraction) {
    Call("BoostPlayerShield %08x", Bits(fraction));
}
int __fastcall FakeGetMagicCounter(void *self, int, int counter) {
    Call("GetMagicCounter %d", counter);
    return W->magicCounter;
}
void __fastcall FakeSetMagicCounterThreshold(void *self, int, int counter, int threshold) {
    Call("SetMagicCounterThreshold %d %d", counter, threshold);
}
double __fastcall FakeGetPlayerHealth(void *self, int, uint32_t fraction) {
    Call("GetPlayerHealth %02x", fraction & 0xff);
    return W->health;
}
void __fastcall FakeSetObjectiveComment(void *self, int, int slot, uint32_t comment) {
    Call("SetObjectiveComment %d %08x", slot, comment);
}
void __fastcall FakeClearObjective(void *self, int, int slot) {
    Call("ClearObjective %d", slot);
}
void __fastcall FakeStartSectionProfile(void *self, int, const char *name) {
    Call("StartSectionProfile %08x", Word(name));
}
void __fastcall FakeSetMagicCounter(void *self, int, int counter, int value) {
    Call("SetMagicCounter %d %08x", counter, value);
}
void __fastcall FakeSetObjectiveMarker(void *self, int, int slot, const void *marker) {
    Call("SetObjective(marker) %d %08x", slot, Word(marker));
}
void __fastcall FakeSetObjectiveTarget(void *self, int, int slot, const void *target) {
    Call("SetObjective(target) %d %08x", slot, Word(target));
}
void __fastcall FakeSetObjectiveElement(void *self, int, int slot, const void *element) {
    Call("SetObjective(element) %d %08x", slot, Word(element));
}
void __fastcall FakeSetDriveMissile(void *self, int, void *missile) {
    Call("SetDriveMissile %08x", Word(missile));
}
void __fastcall FakeSetCinematicMode(void *self, int, int mask) {
    Call("SetCinematicMode %d", mask);
}
void __fastcall FakeWin(void *self, int) {
    Call("SMissionManager::Win");
}
void __fastcall FakeLose(void *self, int) {
    Call("SMissionManager::Lose");
}
void __fastcall FakeAddMessage(void *self, int, int a, int b, int c) {
    Call("AddMessage %d %d %d", a, b, c);
}
void __fastcall FakeTimerIncrement(void *self, int, int amount) {
    Call("SMissionTimer::Increment %d %d", int(static_cast<uint8_t *>(self) - W->missionManager), amount);
}

// ---- the Simulation

void __fastcall FakePauseSimState(void *self, int) {
    Call("PauseSimState");
}
void __fastcall FakeUnPauseSimState(void *self, int) {
    Call("UnPauseSimState");
}
RigidBody *__fastcall FakeGetRigidBody(void *self, int, int slot) {
    Call("GetRigidBody %d", slot);
    return &W->bodies[slot & (kBodies - 1)];
}
void __fastcall FakeSetStartConditions(void *self, int, const Coord3 *position, const Coord3 *direction) {
    Call("SetStartConditions");
    Vec("position", position, 3);
    Vec("direction", direction, 3);
}
void __fastcall FakeUntrackInstance(void *self, int, void *instance) {
    Call("UntrackInstance %08x", Word(instance));
}
void __fastcall FakeSimulationReset(void *self, int) {
    Call("Simulation::Reset");
}
void __fastcall FakeSpawnExplosion(void *self, int, uint32_t a, uint32_t b, const Coord3 *direction,
                                   const Coord3 *position, float force, uint32_t f, void *source, const void *unused) {
    Call("SpawnExplosion %u %u %08x %u %08x %08x", a, b, Bits(force), f, Word(source), Word(unused));
    Vec("direction", direction, 3);
    Vec("position", position, 3);
}
void *__fastcall FakeSpawnCarObject(void *self, int, int carClass, const char *carType, uint32_t colour,
                                    const Coord3 *direction, const Coord3 *position) {
    Call("SpawnCarObject %d %08x %08x", carClass, Word(carType), colour);
    Vec("direction", direction, 3);
    Vec("position", position, 3);
    return W->car;
}

// ---- the weapons

void __fastcall FakeSetPlayerCar(void *self, int, void *car) {
    Call("SetPlayerCar %08x", Word(car));
}
void __fastcall FakeLoadWeapon(void *self, int, int weapon, int ammo, int clip, int damage) {
    Call("LoadWeapon %d %d %d %d", weapon, ammo, clip, damage);
}
void FakeRayShellReset() {
    Call("RayShell::Reset");
}
void FakeAddMuzzleFlash(const Coord4 *position, const Coord4 *direction) {
    Call("AddMuzzleFlash");
    Vec("position", position, 4);
    Vec("direction", direction, 3);
}

// ---- the HUD and GFX

void *FakeTheApp() {
    Call("TheApp");
    return W->hudPresent ? W->hud : NULL;
}
void __fastcall FakeHudReset(void *self, int) {
    Call("GHud::Reset %08x", Word(self));
}
void __fastcall FakeBloodCurtain(void *self, int) {
    Call("TriggerBloodCurtain %08x", Word(self));
}
void __fastcall FakeTriggerMissionMsg(void *self, int, int message, float time) {
    Call("TriggerMissionMsg %08x %d %08x", Word(self), message, Bits(time));
}
void __fastcall FakeSetMissionPassed(void *self, int, uint32_t flag) {
    Call("SetMissionPassed %02x", flag & 0xff);
}
void __fastcall FakeSetMissionFailed(void *self, int, uint32_t flag) {
    Call("SetMissionFailed %02x", flag & 0xff);
}
void FakeGfxTrigger(void *effect, const Coord3 *position, const Coord4 *rotation, const Coord3 *offset,
                    const Coord4 *offsetRotation, RSceneObj *owner, uint32_t instance, const Coord3 *ownerVelocity) {
    Call("GFX::Trigger %08x %08x %u %08x", Word(effect), Word(owner), instance, Word(ownerVelocity));
    Vec("position", position, 3);
    Vec("rotation", rotation, 4);
    Vec("offset", offset, 3);
    Vec("offsetRotation", offsetRotation, 4);
}
void FakeGfxTriggerByName(const char *name, const Coord3 *position, const Coord4 *rotation, const Coord3 *offset,
                          const Coord4 *offsetRotation, RSceneObj *owner, uint32_t instance,
                          const Coord3 *ownerVelocity) {
    Call("GFX::TriggerByName %08x %u %08x", Word(owner), instance, Word(ownerVelocity));   // the name by its text
    Text("name", name);
    Vec("position", position, 3);
    Vec("rotation", rotation, 4);
    Vec("offset", offset, 3);
    Vec("offsetRotation", offsetRotation, 4);
}
void FakePurgeAllEffects() {
    Call("PurgeAllEffects");
}

// ---- the world, the paths and the camera (ported callees)

RSceneObj *__fastcall FakeGetSceneObj(void *self, int, const CARP::Instance *instance) {
    Call("GetSceneObjFromInstance %08x", Word(instance));
    for (int i = 0; i < kInstances; i++)
        if (instance == &W->instances[i])
            return W->sceneObjFor[i] < 0 ? NULL : &W->sceneObjs[W->sceneObjFor[i]];
    return NULL;
}
void __fastcall FakeWorldReset(void *self, int) {
    Call("WWorld::Reset");
}
void __fastcall FakeSetAccelDelay(void *self, int, unsigned updates) {
    Call("SetAccelDelay %08x %u", Word(self), updates);
}
void __fastcall FakeSetAcceleration(void *self, int, float value) {
    Call("SetAcceleration %08x %08x", Word(self), Bits(value));
}
void __fastcall FakeSetDesiredThrottle(void *self, int, float value) {
    Call("SetDesiredThrottle %08x %08x", Word(self), Bits(value));
}
void __fastcall FakeSetRunning(void *self, int, uint32_t on) {
    Call("SetRunning %08x %02x", Word(self), on & 0xff);
}
void __fastcall FakeSetThrottle(void *self, int, float value) {
    Call("SetThrottle %08x %08x", Word(self), Bits(value));
}
void __fastcall FakeSetParametricTime(void *self, int, float time) {
    Call("SetParametricTime %08x %08x", Word(self), Bits(time));
}
float __fastcall FakeGetParametricDuration(void *self, int) {
    Call("GetParametricDuration %08x", Word(self));
    return W->duration;
}
Coord3 *__fastcall FakePathGetPosition(void *self, int) {
    Call("RPathHandle::GetPosition %08x", Word(self));
    return reinterpret_cast<Coord3 *>(&W->positions[1]);
}
void __fastcall FakeSetNextPath(void *self, int, void *path) {
    Call("SetNextPath %08x %08x", Word(self), Word(path));
}
void *FakeGetPathHandle(const CARP::Instance *instance) {
    Call("GetPathHandle %08x", Word(instance));
    for (int i = 0; i < kInstances; i++)
        if (instance == &W->instances[i])
            return W->pathFor[i] < 0 ? NULL : W->paths[W->pathFor[i]];
    return NULL;
}
void __fastcall FakeCameraLockOn(void *self, int, const Coord4 *point, const Coord4 *target, int param, int quiet) {
    Call("CameraLockOn %08x %d %d", Word(point), param, quiet);
    Vec("target", target, 4);
}
void __fastcall FakeTriggerCarAnimationCamera(void *self, int, uint32_t id, int steps, uint32_t delay, void *anchor,
                                              int flags) {
    Call("TriggerCarAnimationCamera %u %d %08x %08x %d", id, steps, delay, Word(anchor), flags);
}
void __fastcall FakePauseOff(void *self, int) {
    Call("PauseOff");
}
void __fastcall FakeSetAutoDriveCamera(void *self, int, float x, float y, int w) {
    Call("SetAutoDriveCamera %08x %08x %d", Bits(x), Bits(y), w);
}
void __fastcall FakeSetPauseCamera(void *self, int) {
    Call("SetPauseCamera");
}
void __fastcall FakeSetLastSelectable(void *self, int, int delay, unsigned b, uint32_t relative,
                                      uint32_t notAutoDrive) {
    Call("SetLastSelectableCameraMode %d %u %02x %02x", delay, b, relative & 0xff, notAutoDrive & 0xff);
}
void __fastcall FakeTriggerAnimationCamera(void *self, int, void *animation, int b, uint32_t delay,
                                           uint32_t timePath, int fov) {
    Call("TriggerAnimationCamera %08x %d %04x %02x %d", Word(animation), b, delay & 0xffff, timePath & 0xff, fov);
}
void __fastcall FakeTriggerAIPathAnimationCamera(void *self, int, void *path, int b, uint32_t delay, int fov) {
    Call("TriggerAIPathAnimationCamera %08x %d %04x %d", Word(path), b, delay & 0xffff, fov);
}
void __fastcall FakeRender(void *self, int) {
    Call("RRenderHigh::Render");
}
void FakeRestartTrackRender() {
    Call("RestartTrackRender");
}
void __fastcall FakeHide(void *self, int) {
    Call("RSceneObj::Hide %08x", Word(self));
}
uint32_t __fastcall FakeGetInstancePosition(void *self, int, uint32_t index, MATRIX4 *transform, uint32_t world) {
    Call("GetInstancePosition %08x %u %02x", Word(self), index, world & 0xff);
    *transform = W->matrix;
    return 1;
}
void __fastcall FakeLocateFX(void *self, int, const ArticleEffect *effect, MATRIX4 *transform, uint32_t world) {
    Call("LocateFX %08x %08x %02x", Word(self), Word(effect), world & 0xff);
    *transform = W->matrix;
}
void __fastcall FakeSetAreaBrightness(void *self, int, float brightness) {
    Call("SetAreaBrightness %08x", Bits(brightness));
}
void __fastcall FakeFadeScale(void *self, int, float scale, float seconds) {
    Call("FadeScale %08x %08x", Bits(scale), Bits(seconds));
}
ArticleEffect *__fastcall FakeFindEffectByID(void *self, int, uint32_t id) {
    Call("FindEffectByID %08x %u", Word(self), id);
    return W->effectById < 0 ? NULL : &W->effects[W->effectById];
}
void __fastcall FakeProcessStimuli4(void *self, int, uint32_t id, uint32_t stimulus, uint32_t tick, int mode) {
    Call("ProcessStimuli %08x %08x %02x %u %d", Word(self), id, stimulus & 0xff, tick, mode);
}
void __fastcall FakeProcessStimuli3(void *self, int, uint32_t stimulus, uint32_t tick, int mode) {
    Call("ProcessStimuli %08x %02x %u %d", Word(self), stimulus & 0xff, tick, mode);
}

// ---- the scheduler and the rest of the ported engine

uint32_t __fastcall FakeAddTask(void *self, int, int type, uint32_t data, uint32_t spacing, uint32_t unused,
                                int delay, int repeats) {
    Call("AddTask %08x %d %08x %04x %02x %d %d", Word(self), type, data, spacing & 0xffff, unused & 0xff, delay,
         repeats);
    return W->taskNum;
}
bool __fastcall FakeRemoveTask(void *self, int, uint32_t taskNum) {
    Call("RemoveTask %08x %08x", Word(self), taskNum);
    return W->removeHit != 0;
}
void FakeSchedulerReset() {
    Call("Scheduler::Reset");
}
void __fastcall FakeSetHitPointLoc(void *self, int, float *hitPoints) {
    Call("SetHitPointLoc %08x %08x", Word(self), Word(hitPoints));
}
bool __fastcall FakeGetWorldHeight(void *self, int, const Coord3 *point, float *height, uint32_t unused) {
    Call("GetWorldHeightAtPoint %02x", unused & 0xff);
    Vec("point", point, 3);
    *height = W->height;
    return W->heightHit != 0;
}
bool __fastcall FakeSetCollisionArticle(void *self, int, void *instance, uint32_t article) {
    Call("SetCollisionArticle %08x %08x", Word(instance), article);
    return W->collisionHit != 0;
}
void *FakeRoadNetworkGet() {
    Call("WRoadNetwork::Get");
    return W->roadNetwork;
}
void FakeRoadNetworkRestart() {
    Call("WRoadNetwork::Restart");
}
void __fastcall FakeActivateTargeting(void *self, int, int mode) {
    Call("ActivateTargeting %d", mode);
}
void __fastcall FakeDeactivateTargeting(void *self, int) {
    Call("DeactivateTargeting");
}
void FakeAnimPoolInit(uint32_t size) {
    Call("AnimPool_Init %08x", size);
}
void FakeAnimPoolCleanup() {
    Call("AnimPool_Cleanup");
}
void FakeOperatorDelete(void *block) {
    Call("operator delete %08x", Word(block));
}
uint32_t FakeRandomShort() {
    uint32_t value = W->randoms[W->randomAt++ & 7];
    Call("RandomShort %04x", value);
    return value;
}

// ---- the sounds

void *FakeSoundNew(unsigned size, const char *name) {
    Call("ABaseSound::new %u", size);
    Text("name", name);
    return W->shotFails ? NULL : W->shot;
}
AOneShotSound *__fastcall FakeOneShotAt(AOneShotSound *self, int, int bank, int patch, const Coord3 *position,
                                         float volume, const char *mix, uint32_t loop) {
    Call("AOneShotSound(at) %08x %d %d %08x %02x", Word(self), bank, patch, Bits(volume), loop & 0xff);
    Vec("position", position, 3);
    Text("mix", mix);
    return self;
}
void __fastcall FakeMixSetTransition(void *self, int, float volume, int steps) {
    Call("AMix::SetTransition %08x %d", Bits(volume), steps);
}
void __fastcall FakeMixRevert(void *self, int, int steps) {
    Call("AMix::Revert %d", steps);
}
float __fastcall FakeGetPresetVolume(void *self, int) {
    Call("GetPresetVolume");
    return W->presetVolume;
}
void FakeMixReset(int steps) {
    Call("AMix::Reset %d", steps);
}
void *FakeMixGet(const char *name) {
    Call("AMix::Get");
    Text("name", name);
    return W->mix;
}
void FakeMenuTriggerNamed(const char *bank, const char *patch, const char *mix, int view) {
    Call("AMenuSound::Trigger %d", view);
    Text("bank", bank);
    Text("patch", patch);
    Text("mix", mix);
}
void FakeMenuTriggerNumbered(int bank, const char *patch, const char *mix, int view) {
    Call("AMenuSound::Trigger %d %d", bank, view);
    Text("patch", patch);
    Text("mix", mix);
}
void FakeSetMissionOver() {
    Call("SetMissionOver");
}
void FakeSoundPause() {
    Call("ASoundManager::Pause");
}
void FakeSoundResume() {
    Call("ASoundManager::Resume");
}
void FakeSoundRestart() {
    Call("ASoundManager::Restart");
}
void FakeAfxSetMode(int mode, int slot, int priority) {
    Call("AFX::SetMode %d %d %d", mode, slot, priority);
}
void *FakeBankGet(const char *name) {
    Call("ABank::Get");
    Text("name", name);
    return W->bank;
}
int __fastcall FakeIndexLookup(void *self, int, const char *name) {
    Call("AIndex::Lookup %08x", Word(self));
    Text("name", name);
    return W->patch;
}
void __fastcall FakeInTheZone(void *self, int, uint32_t entering) {
    Call("InTheZone %08x %02x", Word(self), entering & 0xff);
}
void FakeFeedbackPause() {
    Call("IFeedback::Pause");
}

// ---- the virtual methods: the car's, the scene objects', the sound's and the audio's

void *__fastcall FakeCarGetAudio(void *self, int) {
    Call("car GetAudio");
    return W->audio;
}
void *__fastcall FakeCarGetAI(void *self, int) {
    Call("car GetAIGroundVehiclePtr");
    return W->vehicles[0];
}
uint32_t __fastcall FakeCarGetColour(void *self, int) {
    Call("car GetCarColour");
    return W->carColour;
}
void __fastcall FakeCarFlush(void *self, int) {
    Call("car FlushCarControls");
}
void __fastcall FakeCarSteering(void *self, int, float v) {
    Call("car SetCarControlSteering %08x", Bits(v));
}
void __fastcall FakeCarSteeringVertical(void *self, int, float v) {
    Call("car SetCarControlSteeringVertical %08x", Bits(v));
}
void __fastcall FakeCarGas(void *self, int, float v) {
    Call("car SetCarControlGas %08x", Bits(v));
}
void __fastcall FakeCarBrake(void *self, int, float v) {
    Call("car SetCarControlBrake %08x", Bits(v));
}
void __fastcall FakeCarHandBrake(void *self, int, uint32_t on) {
    Call("car SetCarControlHandBrake %02x", on & 0xff);
}
void __fastcall FakeCarFire(void *self, int, uint32_t on) {
    Call("car SetCarControlFirePrimary %02x", on & 0xff);
}
void __fastcall FakeCarLock(void *self, int) {
    Call("car LockCarControlForever");
}
void __fastcall FakeCarResetAt(void *self, int, Coord3 *position, const Coord3 *direction) {
    Call("car ResetCar(at)");
    Vec("position", position, 3);
    Vec("direction", direction, 3);
}
void __fastcall FakeCarReset(void *self, int, uint32_t unused) {
    Call("car ResetCar %02x", unused & 0xff);
}
void __fastcall FakeCarResetDamage(void *self, int) {
    Call("car ResetDamage");
}
CarPhysics *__fastcall FakeCarGetPhysics(void *self, int) {
    Call("car GetPhysics");
    return &W->physics;
}
void __fastcall FakeCarSetImmunity(void *self, int, uint32_t on) {
    Call("car SetImmunity %02x", on & 0xff);
}
void __fastcall FakeCarRollSub(void *self, int, uint8_t degrees, uint8_t subRolling) {   // bytes: the rest is not passed
    Call("car RollSub %08x %02x %02x", Word(self), degrees, subRolling);
}
Coord3 *__fastcall FakeSceneGetVelocity(void *self, int) {
    Call("scene GetVelocity %08x", Word(self));
    return reinterpret_cast<Coord3 *>(&W->sceneVelocity);
}
void __fastcall FakeSceneTriggerFX(void *self, int, int type, uint32_t which, uint32_t unused3, const void *unused4,
                                   const void *unused5, float intensity) {
    Call("scene TriggerFX %08x %d %u %u %08x", Word(self), type, which, unused3, Bits(intensity));
    Vec("unused4", unused4, 3);
    Vec("unused5", unused5, 3);
}
void *__fastcall FakeSoundDelete(void *self, int, unsigned flags) {
    Call("sound Delete %08x %u", Word(self), flags);
    return self;
}
void __fastcall FakeSetCreakLevel(void *self, int, uint32_t level, int32_t period) {
    Call("audio SetCreakLevel %08x %d", level, period);
}
void __fastcall FakeUnexpectedSlot(void *self, int) {
    Call("unexpected slot %08x", Word(self));
}

Fake g_fakes[] = {
    { 0x0005a5a0, (const void *)&FakeEventNew },
    { 0x0005a5c0, (const void *)&FakeEventDelete },
    { 0x00043640, (const void *)&FakeEventDestruct },
    { 0x00117e50, (const void *)&FakeTagConstruct },
    { 0x000286d0, (const void *)&FakeRedirect },
    { 0x00028630, (const void *)&FakeEveryoneChill },
    { 0x00028b60, (const void *)&FakeElementControllerDestruct },
    { 0x0001c190, (const void *)&FakeCharacterInit },
    { 0x0001c1b0, (const void *)&FakeCharacterShutdown },
    { 0x00022c30, (const void *)&FakeShowHands },
    { 0x00022c50, (const void *)&FakeHideHands },
    { 0x000359d0, (const void *)&FakeGetSplinePath },
    { 0x00035930, (const void *)&FakeSetNextSplinePath },
    { 0x00035c00, (const void *)&FakeVehicleControllerGet },
    { 0x000363f0, (const void *)&FakeVehicleControllerReset },
    { 0x000366e0, (const void *)&FakeCreateSimTrafficCars },
    { 0x00036990, (const void *)&FakeRegisterSimCarList },
    { 0x000347e0, (const void *)&FakeRoadSpawnGet },
    { 0x00034830, (const void *)&FakeRoadSpawnRestart },
    { 0x000b6140, (const void *)&FakeGetCarType },
    { 0x000b6380, (const void *)&FakeMissionStart },
    { 0x000b63a0, (const void *)&FakeMissionStop },
    { 0x000b64c0, (const void *)&FakeMissionSetAutoDrive },
    { 0x000b65b0, (const void *)&FakeIsSkippingCinematic },
    { 0x000b65f0, (const void *)&FakeBoostPlayerHealth },
    { 0x000b6640, (const void *)&FakeBoostPlayerShield },
    { 0x000b67a0, (const void *)&FakeGetMagicCounter },
    { 0x000b67b0, (const void *)&FakeSetMagicCounterThreshold },
    { 0x000b67d0, (const void *)&FakeGetPlayerHealth },
    { 0x000b6c30, (const void *)&FakeSetObjectiveComment },
    { 0x000b6c50, (const void *)&FakeClearObjective },
    { 0x000b7320, (const void *)&FakeStartSectionProfile },
    { 0x000b7ac0, (const void *)&FakeSetMagicCounter },
    { 0x000b7f60, (const void *)&FakeSetObjectiveMarker },
    { 0x000b7fb0, (const void *)&FakeSetObjectiveTarget },
    { 0x000b8000, (const void *)&FakeSetObjectiveElement },
    { 0x000b80b0, (const void *)&FakeSetDriveMissile },
    { 0x000b8160, (const void *)&FakeSetCinematicMode },
    { 0x000b84b0, (const void *)&FakeWin },
    { 0x000b8580, (const void *)&FakeLose },
    { 0x000b94a0, (const void *)&FakeAddMessage },
    { 0x000b9620, (const void *)&FakeTimerIncrement },
    { 0x000b25e0, (const void *)&FakePauseSimState },
    { 0x000b2600, (const void *)&FakeUnPauseSimState },
    { 0x000b2700, (const void *)&FakeGetRigidBody },
    { 0x000b2b50, (const void *)&FakeSetStartConditions },
    { 0x000b3000, (const void *)&FakeUntrackInstance },
    { 0x000b49f0, (const void *)&FakeSimulationReset },
    { 0x000b4cd0, (const void *)&FakeSpawnExplosion },
    { 0x000b5700, (const void *)&FakeSpawnCarObject },
    { 0x000ba990, (const void *)&FakeSetPlayerCar },
    { 0x000ba9f0, (const void *)&FakeLoadWeapon },
    { 0x00071d40, (const void *)&FakeRayShellReset },
    { 0x000734a0, (const void *)&FakeAddMuzzleFlash },
    { 0x000d7c10, (const void *)&FakeTheApp },
    { 0x000d7da0, (const void *)&FakeHudReset },
    { 0x000d87f0, (const void *)&FakeBloodCurtain },
    { 0x000da030, (const void *)&FakeTriggerMissionMsg },
    { 0x000dee80, (const void *)&FakeSetMissionPassed },
    { 0x000deee0, (const void *)&FakeSetMissionFailed },
    { 0x000d34e0, (const void *)&FakeGfxTrigger },
    { 0x000d3610, (const void *)&FakeGfxTriggerByName },
    { 0x000d34b0, (const void *)&FakePurgeAllEffects },
    { 0x000d14a0, (const void *)&FakeGetSceneObj },
    { 0x000d1750, (const void *)&FakeWorldReset },
    { 0x0007ff20, (const void *)&FakeSetAccelDelay },
    { 0x0007ff10, (const void *)&FakeSetAcceleration },
    { 0x0007ff00, (const void *)&FakeSetDesiredThrottle },
    { 0x0007fee0, (const void *)&FakeSetRunning },
    { 0x0007fef0, (const void *)&FakeSetThrottle },
    { 0x0007ff30, (const void *)&FakeSetParametricTime },
    { 0x0007fed0, (const void *)&FakeGetParametricDuration },
    { 0x0007ff50, (const void *)&FakePathGetPosition },
    { 0x0007fec0, (const void *)&FakeSetNextPath },
    { 0x00080030, (const void *)&FakeGetPathHandle },
    { 0x00081610, (const void *)&FakeCameraLockOn },
    { 0x00081740, (const void *)&FakeTriggerCarAnimationCamera },
    { 0x000817f0, (const void *)&FakePauseOff },
    { 0x00088b10, (const void *)&FakeSetAutoDriveCamera },
    { 0x00088cc0, (const void *)&FakeSetPauseCamera },
    { 0x000894d0, (const void *)&FakeSetLastSelectable },
    { 0x00089630, (const void *)&FakeTriggerAnimationCamera },
    { 0x000898d0, (const void *)&FakeTriggerAIPathAnimationCamera },
    { 0x0008bf30, (const void *)&FakeRender },
    { 0x0008c840, (const void *)&FakeRestartTrackRender },
    { 0x0008db30, (const void *)&FakeHide },
    { 0x0008dd20, (const void *)&FakeGetInstancePosition },
    { 0x0008eca0, (const void *)&FakeLocateFX },
    { 0x0009a4a0, (const void *)&FakeSetAreaBrightness },
    { 0x0007d7b0, (const void *)&FakeFadeScale },
    { 0x00076850, (const void *)&FakeFindEffectByID },
    { 0x00077d70, (const void *)&FakeProcessStimuli4 },
    { 0x00077e00, (const void *)&FakeProcessStimuli3 },
    { 0x0005c490, (const void *)&FakeAddTask },
    { 0x0005bc70, (const void *)&FakeRemoveTask },
    { 0x0005ba40, (const void *)&FakeSchedulerReset },
    { 0x0006f7e0, (const void *)&FakeSetHitPointLoc },
    { 0x000bf210, (const void *)&FakeGetWorldHeight },
    { 0x000c2ff0, (const void *)&FakeSetCollisionArticle },
    { 0x000c8940, (const void *)&FakeRoadNetworkGet },
    { 0x000c8de0, (const void *)&FakeRoadNetworkRestart },
    { 0x000ce0f0, (const void *)&FakeActivateTargeting },
    { 0x000ce150, (const void *)&FakeDeactivateTargeting },
    { 0x000f7d00, (const void *)&FakeAnimPoolInit },
    { 0x000f7d50, (const void *)&FakeAnimPoolCleanup },
    { 0x001146e0, (const void *)&FakeOperatorDelete },
    { 0x0001aab0, (const void *)&FakeRandomShort },
    { 0x0011c910, (const void *)&FakeSoundNew },
    { 0x000477d0, (const void *)&FakeOneShotAt },
    { 0x0011c980, (const void *)&FakeMixSetTransition },
    { 0x0011c9e0, (const void *)&FakeMixRevert },
    { 0x0011ca90, (const void *)&FakeGetPresetVolume },
    { 0x0011cb80, (const void *)&FakeMixReset },
    { 0x0011d720, (const void *)&FakeMixGet },
    { 0x0011dfd0, (const void *)&FakeMenuTriggerNamed },
    { 0x0011e010, (const void *)&FakeMenuTriggerNumbered },
    { 0x00120d80, (const void *)&FakeSetMissionOver },
    { 0x00121070, (const void *)&FakeSoundPause },
    { 0x00121150, (const void *)&FakeSoundResume },
    { 0x00121320, (const void *)&FakeSoundRestart },
    { 0x001247d0, (const void *)&FakeAfxSetMode },
    { 0x001269d0, (const void *)&FakeBankGet },
    { 0x00126d40, (const void *)&FakeIndexLookup },
    { 0x00127ed0, (const void *)&FakeInTheZone },
    { 0x0004fa00, (const void *)&FakeFeedbackPause },
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

// One fake lifted for a case whose port inlines the callee (so that both sides run the real one), then put back
struct FakeLifted {
    Fake *fake;
    explicit FakeLifted(uint32_t address) : fake(NULL) {
        for (Fake &f : g_fakes) {
            if (f.address == address) {
                fake = &f;
                HookRemove(&f.port);
                HookRemove(&f.entry);
            }
        }
    }
    ~FakeLifted() {
        if (fake == NULL)
            return;
        const void *port = JumpTarget(fake->address);
        HookInstall(&fake->entry, fake->address, fake->fake);
        if (port != NULL)
            HookInstall(&fake->port, uint32_t(uintptr_t(port)), fake->fake);
    }
};

void BuildVtables() {
    for (void *&slot : g_carVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_carVtable[PVehicle::kGetAudio] = (void *)&FakeCarGetAudio;
    g_carVtable[PVehicle::kGetAIGroundVehiclePtr] = (void *)&FakeCarGetAI;
    g_carVtable[PVehicle::kGetCarColour] = (void *)&FakeCarGetColour;
    g_carVtable[PVehicle::kFlushCarControls] = (void *)&FakeCarFlush;
    g_carVtable[PVehicle::kSetCarControlSteering] = (void *)&FakeCarSteering;
    g_carVtable[PVehicle::kSetCarControlSteeringVertical] = (void *)&FakeCarSteeringVertical;
    g_carVtable[PVehicle::kSetCarControlGas] = (void *)&FakeCarGas;
    g_carVtable[PVehicle::kSetCarControlBrake] = (void *)&FakeCarBrake;
    g_carVtable[PVehicle::kSetCarControlHandBrake] = (void *)&FakeCarHandBrake;
    g_carVtable[PVehicle::kSetCarControlFirePrimary] = (void *)&FakeCarFire;
    g_carVtable[PVehicle::kLockCarControlForever] = (void *)&FakeCarLock;
    g_carVtable[PVehicle::kResetCarAt] = (void *)&FakeCarResetAt;
    g_carVtable[PVehicle::kResetCar] = (void *)&FakeCarReset;
    g_carVtable[PVehicle::kResetDamage] = (void *)&FakeCarResetDamage;
    g_carVtable[PVehicle::kGetPhysics] = (void *)&FakeCarGetPhysics;
    g_carVtable[PVehicle::kSetImmunity] = (void *)&FakeCarSetImmunity;
    g_carVtable[PVehicle::kRollSub] = (void *)&FakeCarRollSub;
    for (void *&slot : g_sceneObjVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_sceneObjVtable[13] = (void *)&FakeSceneGetVelocity;
    g_sceneObjVtable[15] = (void *)&FakeSceneTriggerFX;
    for (void *&slot : g_soundVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_soundVtable[0] = (void *)&FakeSoundDelete;
    for (void *&slot : g_audioVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_audioVtable[9] = (void *)&FakeSetCreakLevel;
}

// =============================================================================================================
// The world
// =============================================================================================================

float RandomFloat() {
    switch (Random(10)) {
    case 0: return 0.0f;
    case 1: return 1.0f;
    case 2: return -Uniform(0.0f, 4.0f);
    case 3: return Uniform(0.0f, 0.2f);
    case 4: return Uniform(0.9f, 1.1f);
    default: return Uniform(-200.0f, 200.0f);
    }
}

void RandomVector(Coord4 *v, float range) {
    v->x = Uniform(-range, range);
    v->y = Uniform(-range, range);
    v->z = Uniform(-range, range);
    v->w = Uniform(-1.0f, 1.0f);
}

int8_t RandomIndex(int count) {
    return Chance(4) ? -1 : int8_t(Random(count));
}

void BuildWorld() {
    memset(W, 0, sizeof(FakeWorld));
    for (int i = 0; i < kInstances; i++) {
        float *words = reinterpret_cast<float *>(&W->instances[i]);
        for (int w = 0; w < 16; w++)
            words[w] = Uniform(-60.0f, 60.0f);
    }
    for (int i = 0; i < kSceneObjs; i++) {
        RSceneObj *object = &W->sceneObjs[i];
        object->vtable = reinterpret_cast<RSceneObj_vtbl *>(g_sceneObjVtable);
        object->sourceInstance = &W->instances[Random(kInstances)];
        object->animHandle = Chance(5) ? NULL : reinterpret_cast<Handle *>(W->handles[i]);
        object->pathHandle = Chance(5) ? NULL : reinterpret_cast<RPathHandle *>(W->paths[i]);
        W->paths[i][0xcc] = uint8_t(Random(2));
        float *velocity = reinterpret_cast<float *>(&W->paths[i][0x88]);
        velocity[0] = RandomFloat();
        velocity[1] = RandomFloat();
        velocity[2] = RandomFloat();
    }
    for (int i = 0; i < kEffects; i++) {
        ArticleEffect *effect = &W->effects[i];
        effect->position.x = Uniform(-5.0f, 5.0f);
        effect->position.y = Uniform(-5.0f, 5.0f);
        effect->position.z = Uniform(-5.0f, 5.0f);
        effect->flags = uint16_t(Chance(2) ? 0 : 1 << Random(16));
        effect->type = Chance(4) ? uint8_t(Random(9)) : uint8_t(ArticleEffect::kTypeGfx);
        effect->instance = uint8_t(Random(3));
        effect->id = uint8_t(Random(8));
        effect->gfx.reference = Chance(5) ? NULL : W->opaque[Random(3)];
        RandomVector(&effect->gfx.rotation, 1.0f);
    }
    RandomVector(&W->place.position, 80.0f);
    RandomVector(&W->place.direction, 1.0f);
    for (int i = 0; i < 2; i++)
        W->procAnimStates[i].sceneObj = Chance(4) ? NULL : &W->sceneObjs[Random(kSceneObjs)];
    W->trigger.position.x = Uniform(-80.0f, 80.0f);
    W->trigger.position.y = Uniform(-5.0f, 5.0f);
    W->trigger.position.z = Uniform(-80.0f, 80.0f);
    W->trigger.radius = Uniform(0.0f, 30.0f);
    for (int i = 0; i < kElements; i++) {
        AIElement *element = &W->elements[i];
        element->type = int32_t(Chance(5) ? 2 : Random(2));
        element->state = int32_t(Chance(4) ? 0 : 1);
        if (element->type == CARP::kAIElementCharacter)
            element->character = reinterpret_cast<AICharacter *>(W->characters[i]);
        else
            element->vehicle = reinterpret_cast<AIVehicle *>(W->vehicles[i]);
        element->object = Chance(4) ? NULL : reinterpret_cast<PhysicsObject *>(W->car);
        W->records[i].state = int32_t(Random());
    }
    W->aiSpline.path.value = Chance(3) ? 0 : 0x1234;

    PVehicle *car = reinterpret_cast<PVehicle *>(W->car);
    *reinterpret_cast<void ***>(car) = g_carVtable;
    car->rigidBodySlot = int16_t(Random(kBodies));
    car->renderObject = &W->sceneObjs[0];
    W->carVector[0] = car;
    for (int i = 0; i < kBodies; i++) {
        W->bodies[i].mass = Uniform(100.0f, 2000.0f);
        W->bodies[i].momentum.x = RandomFloat();
    }
    W->physics.isSub = int32_t(Random(4)) - 1;
    *reinterpret_cast<void ***>(W->audio) = g_audioVtable;
    *reinterpret_cast<int32_t *>(&W->audio[0xd0]) = int32_t(Chance(2) ? 2 : Random(8));
    *reinterpret_cast<void **>(&W->audio[0x190]) = W->zoom;
    ABaseSound *sound = reinterpret_cast<ABaseSound *>(W->sound);
    sound->vtable = Word(g_soundVtable);
    sound->minDistance = Uniform(0.0f, 20.0f);
    sound->maxDistance = Uniform(10.0f, 200.0f);

    SMissionManager *mission = reinterpret_cast<SMissionManager *>(W->missionManager);
    mission->missionState = int32_t(Random(5));
    mission->autoDrive = uint8_t(Random(2));
    mission->driveMissile = Chance(2) ? NULL : reinterpret_cast<Missile *>(W->missile);
    reinterpret_cast<SWeaponManager *>(W->weaponManager)->weapons = W->weapons;
    for (WeaponRecord &weapon : W->weapons) {
        weapon.pickupText = int32_t(Random(4000));
        weapon.ammoText = int32_t(Random(4000));
        weapon.flags = Random(4);
        weapon.ammo = int32_t(Random(5)) - 1;
    }
    W->camera[0x145] = uint8_t(Random(2));     // RPlayerCamera::unknown145
    RRenderHigh *high = reinterpret_cast<RRenderHigh *>(W->renderHigh);
    high->views[0].camera = reinterpret_cast<RPlayerCamera *>(W->camera);
    W->scheduler[0] = reinterpret_cast<Schedule *>(W->schedules[0]);
    W->scheduler[1] = reinterpret_cast<Schedule *>(W->schedules[1]);
    float *mixVolume = reinterpret_cast<float *>(&W->mix[4]);
    mixVolume[0] = Uniform(0.0f, 1.0f);
    mixVolume[1] = Uniform(0.0f, 1.0f);
    *reinterpret_cast<int32_t *>(&W->bank[8]) = int32_t(Random(16));
    for (int i = 0; i < 2; i++)
        RandomVector(&W->positions[i], 100.0f);
    for (int i = 0; i < kStrings; i++)
        snprintf(W->strings[i], sizeof(W->strings[i]), "S%u", Random(1000));
    strcpy(W->carType, "car");
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            W->matrix.mtx[r][c] = Uniform(-50.0f, 50.0f);
    RandomVector(&W->sceneVelocity, 30.0f);
    for (int i = 0; i < 64; i++)
        W->physicsObjects[i] = Chance(2) ? NULL : reinterpret_cast<PhysicsObject *>(W->car);
    for (int i = 0; i < 96; i++)
        W->simpleOwners[i] = Chance(2) ? NULL : reinterpret_cast<PhysicsObject *>(W->car);
    reinterpret_cast<PhysicsObject *>(W->car)->renderObject = &W->sceneObjs[Random(kSceneObjs)];

    W->newFails = uint8_t(Chance(16));
    W->shotFails = 0;
    W->hudPresent = uint8_t(!Chance(4));
    W->skipping = uint8_t(Random(2));
    W->heightHit = uint8_t(Random(2));
    W->collisionHit = uint8_t(Random(2));
    W->removeHit = uint8_t(Random(2));
    for (int i = 0; i < kInstances; i++) {
        W->sceneObjFor[i] = RandomIndex(kSceneObjs);
        W->pathFor[i] = RandomIndex(kSceneObjs);
    }
    for (int i = 0; i <= kElements; i++)
        W->splineFor[i] = RandomIndex(kElements);
    for (int i = 0; i < kElements; i++)
        *reinterpret_cast<RPathHandle **>(&W->splinePaths[i][0x60]) =
            Chance(4) ? NULL : reinterpret_cast<RPathHandle *>(W->paths[Random(kSceneObjs)]);
    W->effectById = RandomIndex(kEffects);
    W->duration = Uniform(0.0f, 100.0f);
    W->presetVolume = Chance(4) ? 1.0f : Uniform(0.0f, 1.2f);
    W->height = Uniform(-10.0f, 10.0f);
    switch (Random(4)) {
    case 0: W->health = 0.0; break;
    case 1: W->health = -Uniform(0.0f, 1.0f); break;
    default: W->health = Uniform(0.0f, 1.0f); break;
    }
    W->magicCounter = int32_t(Random());
    W->carColour = Random(8);
    W->taskNum = Random(1000);
    W->patch = int32_t(Random(40));
    for (uint32_t &value : W->randoms)
        value = Chance(3) ? Random(0x80) << 8 & 0x7f00 ^ 0x8000 : Random(0x10000);
    W->randoms[0] = Chance(2) ? Random(0x100) << 8 & 0x8000 : Random(0x10000);

    // The globals
    Globals &g = g_globals;
    memset(&g, 0, sizeof(g));
    g.simState = Chance(3) ? 3 : 1;
    g.stepsPerSecond = Chance(2) ? 60 : int32_t(30 + Random(60));
    g.step = Random(100000);
    g.feedbackAllowed = uint8_t(Random(2));
    g.soundOption = uint8_t(!Chance(5));
    g.streams = uint8_t(Random(2));
    g.superEasy = uint8_t(Random(2));
    g.streamHold = int32_t(Random(3));
    g.realClock = Random();
    g.lastRenderClock = Random();
    g.cullDistance = Bits(Uniform(100.0f, 900.0f));
    g.frameAnimationTime = Random();
    g.videoModeRate = Chance(2) ? 60.0f : 50.0f;
    g.gameTick = Random(100000);
    g.playerGravity = Bits(-34.0f);
    g.launch978 = int32_t(Random(5));
    g.restartFlag = uint8_t(Chance(6));
    g.healthTimes100 = uint8_t(Chance(6));
    g.restartCount = int32_t(Random(10));
    strcpy(g.tuningLevel, Chance(3) ? "paris_mis01" : Chance(2) ? "PARIS_MIS01" : "mis07a");
    g.randomSeed = Random(0x10000);
    g.randomMultiplier = RandomMultiplier;
    g.dynamic.instance = Chance(4) ? NULL : &W->instances[Random(kInstances)];
    g.dynamic.sceneObj = Chance(8) ? NULL : &W->sceneObjs[Random(kSceneObjs)];
    g.dynamic.instanceIndex = int32_t(Random(3));
    RandomVector(&g.dynamic.position, 80.0f);
    g.dynamic.trigger = &W->trigger;
    g.dynamic.flag = uint8_t(Random(2));
    g.dynamic.index = Chance(8) ? -1 : int32_t(Random(kBodies));
}

// The game's pointers at the world (set once; the world stays where it is)
void PointGameAtWorld() {
    fgAIElementControllerGame = W->elementController;
    glbMissionManagerGame = W->missionManager;
    WeaponManagerGame = W->weaponManager;
    fgRenderHighGame = W->renderHigh;
    fgRendererGame = W->renderer;
    FogGame = W->fog;
    ColorizeGame = W->colorize;
    fgCollisionMgrGame = W->collisionMgr;
    fgWorldGame = W->world;
    fgSchedulerGame = W->scheduler;
    fgTriggerManagerGame = W->triggerManager;
    AIHandsGame = W->hands;
    fgBank0Game = W->bank;
    SimCarsGame.first = W->carVector;
    SimCarsGame.last = W->carVector + 1;
    SimCarsGame.end = W->carVector + 1;
    memcpy(PhysicsObjectsGame, W->physicsObjects, sizeof(W->physicsObjects));
    memcpy(SimpleOwnersGame, W->simpleOwners, sizeof(W->simpleOwners));
}

// =============================================================================================================
// The cases
// =============================================================================================================

// Runs `call(original)` on both sides from the same world and globals, and compares them
template <class Call>
void Both(const char *what, long index, const Call &call) {
    g_cases++;
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
    if (!never && k != 'f' && k != 'n' && k != 'u' && k != 'b' && k != 'w' && k != 't' && k != 'z' && k != 'r' &&
        Chance(5))
        return 0;
    switch (k) {
    case 'i': return Word(&W->instances[Random(kInstances)]);
    case 'e': return Word(&W->records[Random(kElements)]);
    case 'a': return Word(&W->effects[Random(kEffects)]);
    case 'p': return Word(&W->place);
    case 'q': return Word(&W->procAnimStates[Random(2)]);
    case 'l': return Word(&W->aiSpline);
    case 'v': return Word(&W->positions[Random(2)]);
    case 'x': return Word(W->opaque[Random(3)]);
    case 's': return Word(W->strings[Random(kStrings)]);
    case 'h': return Word(W->paths[Random(kSceneObjs)]);
    case 'o': return Word(W->sound);
    case 'd': return Word(W->taskData);
    case 'f': {
        uint32_t word = Bits(RandomFloat());
        return word;
    }
    case 'n': return uint32_t(int32_t(Random(140)) - 20);
    case 'r': return uint32_t(int32_t(Random(400)) - 200);
    case 'z': return Random(6) == 0 ? Random(2) : Random(400);
    case 'b': return Chance(2) ? 0 : Random(3) == 0 ? Random() : 1;
    case 'w': return Random(32);
    case 't': return Random(10);
    default: return Random() << 8 ^ Random();
    }
}

struct EventClass {
    const char *name;
    uint32_t make;      // the MakeEvent callback
    uint32_t remove;    // the scalar deleting destructor; 0: another package's
    const char *record; // the kinds of the record's words after the first
};

const EventClass kClasses[] = {
    { "EPathSFXUpdate", 0x00041850, 0x0004cda0, "DHOsu" },
    { "EPathSetAccelDelayDynamic", 0x00041900, 0x000482c0, "u" },
    { "EPathSetAccelDelayStatic", 0x00041990, 0x000482e0, "ieu" },
    { "EPathSetAccelerationDynamic", 0x00041a30, 0x00048300, "f" },
    { "EPathSetAccelerationStatic", 0x00041ac0, 0x00048320, "ief" },
    { "EPathSetDesiredThrottleDynamic", 0x00041b60, 0x00048340, "f" },
    { "EPathSetDesiredThrottleStatic", 0x00041bf0, 0x00048360, "ief" },
    { "EPathSetRunningDynamic", 0x00041c90, 0x00048380, "b" },
    { "EPathSetRunningStatic", 0x00041d30, 0x000483a0, "ieb" },
    { "EPathSetThrottleDynamic", 0x00041dd0, 0x000483c0, "f" },
    { "EPathSetThrottleStatic", 0x00041e60, 0x000483e0, "ief" },
    { "EPathSetTimeDynamic", 0x00041f10, 0x00048400, "f" },
    { "EPathSetTimeStatic", 0x00041fb0, 0x00048420, "ief" },
    { "EPause", 0x000420d0, 0x00048440, "" },
    { "EPlayActorEffect", 0x000421b0, 0x00048460, "A" },
    { "EPlayCameraAnim", 0x00042310, 0x00048480, "xzzbn" },
    { "EPlayCameraSpline", 0x00042470, 0x000484a0, "xzzn" },
    { "EPlayCarCameraAnim", 0x000424b0, 0x0004ece0, "ezzn" },
    { "EPlayEffect", 0x00042700, 0x000484c0, "aiq" },
    { "EPlayGiottoEffect", 0x00042810, 0x000484e0, "Ps" },
    { "EPlayPOVDeath", 0x000428b0, 0x00048500, "" },
    { "EPlaySound", 0x00042950, 0x00048520, "ss" },
    { "EPlaySoundPos", 0x00042980, 0x0004cf30, "vssbff" },
    { "EPlaySystemAnim", 0x00042a80, 0x00048620, "nniq" },
    { "EPlayerLose", 0x00042b50, 0x0004ed00, "bnbb" },
    { "EPlayerSetImmunity", 0x00042b90, 0x0004ed20, "b" },
    { "EPlayerWin", 0x00042bb0, 0x0004ed40, "bb" },
    { "EPowerUp", 0x00042cc0, 0x00048640, "iq" },
    { "EPowerUpAmmo", 0x00042df0, 0x00048660, "wn" },
    { "EPowerUpBraking", 0x00042e20, 0, "uu" },
    { "EPowerUpHealth", 0x00042e50, 0x0004ed60, "n" },
    { "EPowerUpInvulnerable", 0x00042e70, 0, "u" },
    { "EPowerUpMultiDamage", 0x00042eb0, 0x000486a0, "uu" },
    { "EPowerUpShield", 0x00042f80, 0x000486c0, "n" },
    { "EPowerUpTimer", 0x00043010, 0x000486e0, "tf" },
    { "EPowerUpTopSpeed", 0x00043040, 0, "uu" },
    { "EProfileMissionSection", 0x000430c0, 0x00048720, "s" },
    { "ERandomExplosion", 0x00043260, 0x00048740, "uu" },
    { "EReleaseStream", 0x000432c0, 0x00048760, "" },
    { "ERenderFrame", 0x00043370, 0x00048780, "" },
    { "EResetAudioMix", 0x000433f0, 0x000487a0, "n" },
    { "EResetPlayerCar", 0x00043410, 0x0004ed80, "" },
    { "EResetPlayerCarPos", 0x00043430, 0x0004eda0, "Pf" },
    { "ERestart", 0x00043470, 0x0004edc0, "" },
    { "EResume", 0x000434a0, 0x0004ede0, "" },
    { "ERevertAudioMix", 0x00043530, 0x000487c0, "sn" },
    { "ERollSub", 0x00043610, 0x000487e0, "erb" },
    { "ESapphirePole", 0x00043650, 0x00048800, "" },
    { "EScaleFieldOfView", 0x00043690, 0x00048820, "f" },
    { "EScaleFog", 0x00043710, 0x00048840, "ff" },
    { "EScheduleEvent", 0x000437a0, 0x00048860, "unu" },
    { "ESentryMuzzleFlash", 0x00043890, 0x00048880, "a" },
    { "ESetAFXMode", 0x00043930, 0x000488a0, "nnn" },
    { "ESetAreaBrightness", 0x000439e0, 0x000488c0, "r" },
    { "ESetAudioMix", 0x00043ac0, 0x000488e0, "snf" },
    { "ESetAutoDriveCamOff", 0x00043b60, 0x00048900, "" },
    { "ESetAutoDriveCamOffMovie", 0x00043b80, 0x0004ee00, "" },
    { "ESetAutoDriveCamOn", 0x00043c30, 0x00048920, "ff" },
    { "ESetAutoDriveCamOnMovie", 0x00043d20, 0x00048940, "ff" },
    { "ESetAvoidZone", 0x00043df0, 0x00048960, "E" },
    { "ESetBitMagicCounter", 0x00043e90, 0x00048980, "nu" },
    { "ESetCollisionGeometry", 0x00043f40, 0x000489a0, "iu" },
    { "ESetCreakLevel", 0x00043f70, 0x0004ee20, "un" },
    { "ESetCullDistanceFactor", 0x00043fc0, 0x000489c0, "f" },
    { "ESetFrameAnimationTime", 0x00044020, 0x000489e0, "f" },
    { "ESetJumpGravity", 0x00044050, 0x00048a00, "f" },
    { "ESetMagicCounter", 0x000440d0, 0x00048a20, "nn" },
    { "ESetMagicCounterThreshold", 0x00044160, 0x00048a40, "nn" },
    { "ESetMissionMessage", 0x000441f0, 0x00048a60, "nnn" },
    { "ESetNextAISpline", 0x00044220, 0x0004ee40, "el" },
    { "ESetNextPath", 0x000442b0, 0x00048a80, "ix" },
    { "ESetObjective", 0x00044390, 0x00048aa0, "xxeu" },
    { "ESetRecSpeedOffset", 0x000443d0, 0x0004ee60, "ef" },
    { "ECameraLockOn", 0x0003ec60, 0, "evnnfffnn" },
};

// No signalling-NaN words: the original copies some floats on the x87, which would quiet them (the data has none)
uint32_t NoNaN(uint32_t word) {
    if ((word >> 23 & 0xff) == 0xff)
        word ^= 0x40000000;
    return word;
}

void TestEvents(int round) {
    static uint32_t record[0x10];
    for (const EventClass &type : kClasses) {
        BuildWorld();
        memset(record, 0, sizeof(record));
        record[0] = Random();
        for (int i = 0; type.record[i] != 0; i++)
            record[1 + i] = NoNaN(RecordWord(type.record[i]));
        // ERandomExplosion's original has RandomShort inlined: both sides run the real one
        FakeLifted *inlined = type.make == 0x00043260 ? new FakeLifted(0x0001aab0) : NULL;
        // EPathSFXUpdate's record is the task's data: its `self` is the data itself
        Both(type.name, round, [&](bool) {
            ((void (*)(const void *))type.make)(record);
            if (type.remove != 0 && W->newCount > 0)
                ((void *(__fastcall *)(void *, int, unsigned))type.remove)(W->events[0], 0, 1);
        });
        delete inlined;
    }
}

void TestOthers(int round) {
    // The resolvers
    static uint32_t record[0x10];
    const uint32_t resolvers[] = { 0x00041fe0, 0x00042ae0 };
    for (uint32_t resolver : resolvers) {
        BuildWorld();
        uint32_t words[0x10];
        for (uint32_t &word : words)
            word = Random();
        Both("ResolveEvent", round, [&](bool) {
            memcpy(record, words, sizeof(record));      // the same record for both sides
            ((void (*)(void *, void *, void *))resolver)(record, W->opaque[0], W->opaque[1]);
            Vec("record", record, 6);
        });
    }
    // The constructors
    const uint32_t constructors[] = { 0x00042050, 0x00043460, 0x00043490 };
    for (uint32_t constructor : constructors) {
        BuildWorld();
        Both("Construct", round, [&](bool) {
            void *self = ((void *(__fastcall *)(void *, int))constructor)(W->events[0], 0);
            Call("constructed %d", EventIndex(self));
        });
    }
    // The path helper
    for (int i = 0; i < 4; i++) {
        BuildWorld();
        const CARP::Instance *instance = Chance(2) ? NULL : &W->instances[Random(kInstances)];
        AIElement *element = Chance(3) ? NULL : &W->records[Random(kElements)];
        Both("EPathUtil_GetFromInstanceOrAI", round, [&](bool) {
            void *path = ((void *(*)(const void *, void *))0x0003da90)(instance, element);
            Call("path %08x", Word(path));
        });
    }
    // CARP::Instance's matrix
    BuildWorld();
    Both("GetInstanceMatrix4", round, [&](bool) {
        MATRIX4 *m = ((MATRIX4 *(__fastcall *)(const void *, int, MATRIX4 *))0x0003db00)(
            &W->instances[0], 0, &W->matrix);
        Call("matrix %d", m == &W->matrix);
    });
    // Event's scalar deleting destructor, which has Event's operator delete inlined: the real one runs on both sides,
    // and the queue's deletion point is put back after each
    BuildWorld();
    unsigned flags = Random(2);
    FakeLifted eventDelete(0x0005a5c0);
    Both("Event::Delete", round, [&](bool) {
        char *point = gDeletionPoint;
        void *self = ((void *(__fastcall *)(void *, int, unsigned))0x0003db30)(W->events[0], 0, flags);
        Call("deleted %d, the deletion point moved %d", EventIndex(self), int(gDeletionPoint - point));
        gDeletionPoint = point;
    });
}

constexpr int kRounds = 40;

} // namespace

void GameEventsShadowC_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEEVENTSSHADOWC");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    W = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    g_initial = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    g_after = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    if (W == NULL || g_initial == NULL || g_after == NULL) {
        printf("[gameeventsshadowc] no memory for the world\n");
        fflush(stdout);
        return;
    }

    // The live state the cases replace, put back after
    Globals live;
    // The real clock stands still while the cases run: a tick landing in one side's run would be a difference
    TIMER_removetask(RealClock_InterruptHandler);
    ReadGlobals(&live, true);
    uint32_t liveOurClock = LastRenderClock;
    void *livePointers[13] = {
        fgAIElementControllerGame, glbMissionManagerGame, WeaponManagerGame, fgRenderHighGame, fgRendererGame,
        FogGame, ColorizeGame, fgCollisionMgrGame, fgWorldGame, fgSchedulerGame, fgTriggerManagerGame, AIHandsGame,
        fgBank0Game,
    };
    GameVector<PVehicle *> liveCars = SimCarsGame;
    static PhysicsObject *livePhysics[64];
    static PhysicsObject *liveSimple[96];
    memcpy(livePhysics, PhysicsObjectsGame, sizeof(livePhysics));
    memcpy(liveSimple, SimpleOwnersGame, sizeof(liveSimple));

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
    fgRenderHighGame = livePointers[3];
    fgRendererGame = livePointers[4];
    FogGame = livePointers[5];
    ColorizeGame = livePointers[6];
    fgCollisionMgrGame = livePointers[7];
    fgWorldGame = livePointers[8];
    fgSchedulerGame = livePointers[9];
    fgTriggerManagerGame = livePointers[10];
    AIHandsGame = livePointers[11];
    fgBank0Game = livePointers[12];
    SimCarsGame = liveCars;
    memcpy(PhysicsObjectsGame, livePhysics, sizeof(livePhysics));
    memcpy(SimpleOwnersGame, liveSimple, sizeof(liveSimple));
    WriteGlobals(live);
    LastRenderClock = liveOurClock;
    TIMER_addtask(RealClock_InterruptHandler);
    VirtualFree(W, 0, MEM_RELEASE);
    VirtualFree(g_initial, 0, MEM_RELEASE);
    VirtualFree(g_after, 0, MEM_RELEASE);

    for (const auto &d : g_differBy)
        printf("[gameeventsshadowc]   %s: %ld differ\n", d.first.c_str(), d.second);
    printf("[gameeventsshadowc] the events EPathSFXUpdate..ESetRecSpeedOffset: %ld cases, %ld checks, %ld differ "
           "(%ld faults)\n", g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
