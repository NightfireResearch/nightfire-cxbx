#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "GameEventsShadowA.h"
#include "OwnedGlobals.h"
#include "FpControl.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <map>
#include <string>

#include "../audio/Mix.h"
#include "../audio/Voice.h"
#include "../camera/PlayerCamera.h"
#include "../data/Carp.h"
#include "../engine/CoreContainers.h"
#include "../engine/Feedback.h"
#include "../game/BondCar.h"             // kCarClass3
#include "../game/EventsA.h"
#include "../game/EventsD.h"             // StingerChannel
#include "../audio/Sound.h"              // AOneShotSound
#include "../game/AICharacter.h"
#include "../game/AIElements.h"
#include "../game/AIHelicopter.h"
#include "../game/AIPedestrian.h"
#include "../game/AIRoadSpawn.h"
#include "../game/AIVehicleController.h"
#include "../game/Helicopter.h"
#include "../game/Human.h"
#include "../game/MissionManager.h"
#include "../physics/RigidBodyResolve.h"
#include "../physics/RigidBody.h"
#include "../render/RenderHigh.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionInstance.h"
#include "../world/RoadNav.h"
#include "../world/Trigger.h"
#include "../world/World.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of package GEV_A (game/EventsA.cpp and the other classes' functions placed among it), run once from the first simulation
// tick when NIGHTFIRE_GAMEEVENTSSHADOWA is set. Each case runs the ORIGINAL (the package's 173 entries swapped
// back, so the originals call each other) and then the PORT on the same state, put back between the two, and
// compares:
//
//   - every event's scalar deleting destructor (flags 1), on an event built on synthetic records (the registry's
//     fields, random values, NULL and non-NULL references), in a world of stand-ins: the AI's elements, characters
//     and vehicles, the player's car and two others (fake vtables), rigid bodies, the trigger, the mission manager
//     and its mission set, the camera and the renderer's views, the world's sound group and environment, the
//     sounds, mixes and fader, a CARP instance with its article, a scene object, commands and a spline;
//   - every MakeEvent callback on random records and event data, the ResolveEvent callbacks, the three
//     constructors (EAICommand, ECollision, EChangeMaxTraffic, EChangeStage);
//   - the other classes' functions: CollisionImpact's constructor, AVoice's destructor and PlayOnce,
//     AOneShotSound's constructors, RandomWord.
//
// Every call out of the package (the AI, the missions, the Simulation, the HUD, GFX, the camera, the sounds, the
// world, the vehicles' virtual methods, Event's operator new and delete, TagReference::Construct) is a recording
// fake, the same on both sides; the fakes answer from the world. Compared: the event's bytes after (the buffer the
// fake operator new hands out), every byte of the world, the globals the events write (both copies of the two owned
// timers: the game's after the original, ours after the port) and the call logs (values, never stack addresses).
//
// A mutation the test sees: EActivateAIElement clearing the trigger when its flags lack kRigidType1 (as EAICommand
// does): the trigger's flags differ; or ECollision's second side testing `tag` for 5, 0x50 and 0x30 as the first
// does: TriggerFX's call count differs for the random tags.
//
// One summary line: [gameeventsshadowa] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// ---- the game's globals the events read and write

#define fgAIElementControllerGame (*(void **)OwnedGlobals_Ours(0x001ddee4))
#define glbMissionManagerGame (*(void **)OwnedGlobals_Ours(0x00239220))
#define fgRenderHighGame (*(void **)OwnedGlobals_Ours(0x001ec488))
#define fgWorldGame (*(void **)OwnedGlobals_Ours(0x0023f310))
#define AIHandsGame (*(void **)OwnedGlobals_Ours(0x001dda90))
#define SimCarsGame (*(GameVector<PVehicle *> *)OwnedGlobals_Ours(0x00234e3c))
#define PhysicsObjectsGame ((PhysicsObject **)OwnedGlobals_Ours(0x002342a0))
#define SimStateGame I32_AT(OwnedGlobals_Ours(0x00234e24))
#define SimStepsPerSecondGame I32_AT(OwnedGlobals_Ours(0x00234e2c))
#define SimStepCountGame U32_AT(OwnedGlobals_Ours(0x00234e34))
#define HenchLimitGame U8_AT(OwnedGlobals_Ours(0x001b64ec))
#define StreamsGame U8_AT(OwnedGlobals_Ours(0x001b7056))
#define SuperEasyGame U8_AT(OwnedGlobals_Ours(0x001e476c))
#define FeedbackAllowedGame U8_AT(OwnedGlobals_Ours(0x001b6f50))
#define Launch978Game I32_AT(OwnedGlobals_Ours(0x00244508))
#define MaxTrafficGame I16_AT(OwnedGlobals_Ours(0x001de914))
#define FramesSinceAudioGame I32_AT(OwnedGlobals_Ours(0x00243a68))
#define StingerGame ((char *)0x001d8058)
#define TuningLevelGame ((char *)OwnedGlobals_Ours(0x00244084))
#define LastIncidentalGame U32_AT(0x001e23a0)
#define LastPlayerIncidentalGame U32_AT(0x001e23a4)
#define RandomSeedGame U32_AT(OwnedGlobals_Ours(0x001c45c4))
#define RandomMultiplierGame U32_AT(OwnedGlobals_Ours(0x001c45c8))

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
    0x0003db80, 0x0003dc00, 0x0003dc20, 0x0003dcd0, 0x0003dd10, 0x0003dd70, 0x0003de10, 0x0003de30, 0x0003ded0,
    0x0003def0, 0x0003df80, 0x0003dfb0, 0x0003e060, 0x0003e0a0, 0x0003e160, 0x0003e180, 0x0003e1e0, 0x0003e210,
    0x0003e280, 0x0003e2b0, 0x0003e320, 0x0003e340, 0x0003e390, 0x0003e3b0, 0x0003e410, 0x0003e4a0, 0x0003e4e0,
    0x0003e520, 0x0003e5e0, 0x0003e600, 0x0003e620, 0x0003e690, 0x0003e6b0, 0x0003e6d0, 0x0003e6f0, 0x0003e730,
    0x0003e7a0, 0x0003e7c0, 0x0003e7e0, 0x0003e800, 0x0003e850, 0x0003e870, 0x0003e8d0, 0x0003e8f0, 0x0003e920,
    0x0003e940, 0x0003e990, 0x0003e9b0, 0x0003ea20, 0x0003ea60, 0x0003eac0, 0x0003eae0, 0x0003eb30, 0x0003eb50,
    0x0003ecc0, 0x0003ed40, 0x0003ed70, 0x0003ede0, 0x0003ee00, 0x0003eee0, 0x0003ef10, 0x0003f020, 0x0003f060,
    0x0003f0c0, 0x0003f110, 0x0003f170, 0x0003f1c0, 0x0003f210, 0x0003f260, 0x0003f2d0, 0x0003f300, 0x0003f350,
    0x0003f370, 0x0003f390, 0x0003f3b0, 0x0003f3d0, 0x0003f400, 0x0003f420, 0x0003f440, 0x0003f470, 0x0003f4e0,
    0x0003f500, 0x0003f550, 0x0003f5e0, 0x0003f610, 0x0003f640, 0x0003f660, 0x0003f690, 0x0003f6f0, 0x0003f720,
    0x0003f7c0, 0x0003f7f0, 0x0003f890, 0x0003f8c0, 0x0003f8e0, 0x0003f900, 0x0003fa30, 0x0003fa70, 0x0003fa90,
    0x0003fab0, 0x0003fb50, 0x000472c0, 0x000472e0, 0x00047390, 0x000473b0, 0x000473d0, 0x000473f0, 0x00047410,
    0x00047430, 0x00047450, 0x00047470, 0x00047490, 0x000474b0, 0x000474d0, 0x000474f0, 0x00047510, 0x00047530,
    0x00047550, 0x00047570, 0x00047590, 0x000475b0, 0x000475d0, 0x000475f0, 0x00047610, 0x00047630, 0x00047650,
    0x00047670, 0x00047690, 0x000476b0, 0x000476d0, 0x000476f0, 0x000477d0, 0x00047930, 0x00047950, 0x00047970,
    0x00047990, 0x000479b0, 0x000479d0, 0x000479f0, 0x00047a10, 0x00047a30, 0x00047a50, 0x00048680, 0x00048ec0,
    0x000495d0, 0x00049790, 0x00049a80, 0x00049ae0, 0x00049b90, 0x00049d40, 0x00049eb0, 0x0004a820, 0x0004a890,
    0x0004a920, 0x0004a9d0, 0x0004aad0, 0x0004ab60, 0x0004abc0, 0x0004ea60, 0x0004ea80, 0x0004eaa0, 0x0004eac0,
    0x0004eae0, 0x0004eb00, 0x0004eb20, 0x0004eb40, 0x0004eb60, 0x0004eb80, 0x0004eba0, 0x0004ebc0, 0x0004ebe0,
    0x0004ec00, 0x0004ec20,
};
static_assert(sizeof(kEntries) / sizeof(kEntries[0]) == 173, "the package's entries");

// ---- small helpers

uint32_t g_seed = 0x2f6b4e1d;
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

void Differ(const char *what, long index, const char *detail) {
    g_differ++;
    if (ShouldReport(what)) {
        printf("[gameeventsshadowa] DIFF %s, case %ld: %s\n", what, index, detail);
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

// The player's car and the others: a PVehicle with a fake vtable, and what its fake methods answer
struct FakeCar {
    PVehicle car;
    uint8_t pad74[0xc];
    Coord4 position;                    // +0x80 GetPosition's answer
    Coord4 velocity;                    // +0x90 GetLinearVelocity's
    Coord4 wheels[4];                   // +0xa0 GetWheelPos's
    int32_t carClass;
    AIGroundVehicle *ai;
};

constexpr int kElements = 3;
constexpr int kCharacters = 6;
constexpr int kVehicles = 3;
constexpr int kCars = 3;
constexpr int kBodies = 4;

struct FakeWorld {
    alignas(16) uint8_t eventScratch[0x80];
    CARP::AIElement records[kElements];       // the CARP records the events hold
    CARP::AIElement elements[kElements];      // what RedirectCarpElement answers for them
    alignas(16) AICharacter characters[kCharacters];
    AIGroundVehicle vehicles[kVehicles];
    uint8_t navs[kVehicles][sizeof(WRoadNav)];
    Coord4 vehiclePositions[kVehicles];
    alignas(16) FakeCar cars[kCars];
    alignas(16) RigidBody bodies[kBodies];
    alignas(16) RigidBody simpleBodies[kBodies];
    alignas(16) RigidBodyInfo infos[kBodies];
    uint8_t owners[kBodies];            // GetOwner's answer for each body: a car's index
    alignas(16) uint8_t trigger[0x40];
    SMissionManager missionManager;
    MissionSet missionSet;
    alignas(16) uint8_t camera[sizeof(RPlayerCamera)];
    alignas(16) uint8_t renderHigh[sizeof(RRenderHigh)];
    alignas(16) uint8_t viewCamera[sizeof(RViewCamera)];
    alignas(16) uint8_t rcamera[sizeof(RCamera)];
    alignas(16) uint8_t world[sizeof(WWorld)];
    uint8_t soundGroup[0x40];
    EnviroDesc enviro;
    AMix mix;
    uint8_t fader[0x40];
    uint8_t stream[0x40];
    alignas(16) CARP::Instance instance;
    uint8_t article[0x40];
    WorldModel model;
    WorldModelInfo modelInfo;
    alignas(16) uint8_t sceneObj[sizeof(RSceneObj)];
    uint8_t animHandle[0x40];
    AICommand commands[2];
    alignas(16) uint8_t spline[sizeof(CARP::AISpline)];
    alignas(16) MuzzleFlashPoint muzzle;
    uint8_t sound[sizeof(ABaseSound)];
    IFeedback feedback;
    CarPhysics physics;
    uint8_t ioModule[0x50];
    uint8_t hands[8];
    uint8_t vehicleController[8];
    uint8_t zoneController[8];
    uint8_t elementController[8];
    uint8_t tracking[0x50];
    uint8_t hud[8];
    uint8_t weapon[8];
    alignas(16) uint8_t human[sizeof(Human)];           // a character element's characterObject
    alignas(16) uint8_t helicopter[sizeof(PHelicopter)];    // a vehicle element's object with unknown77 clear
    PhysicsObject *physicsObjects[64];
    PVehicle *carVector[kCars];
    // what the fakes answer
    int32_t magicCounter;
    uint8_t okToSpawn;
    uint8_t lookupBools[5];             // HAS_MACHINEGUNS, HAS_CANNONS, HAS_ROCKETS, HAS_MISSLES, HAS_TORPEDOS
    uint8_t sceneObjFound;
    uint8_t gfxFound;
    AIGroundVehicle *playerGround;
};

FakeWorld *W;
FakeWorld *g_initial;
FakeWorld *g_after;

void *g_carVtable[100];
void *g_aiVtable[16];
void *g_cameraVtable[16];
void *g_soundVtable[16];
void *g_sceneObjVtable[24];

int CarIndex(const void *self) {
    for (int i = 0; i < kCars; i++)
        if (self == &W->cars[i])
            return i;
    return -1;
}
int VehicleIndex(const void *self) {
    for (int i = 0; i < kVehicles; i++)
        if (self == &W->vehicles[i])
            return i;
    return -1;
}

// ---- the globals the events touch

struct Globals {
    int32_t simState;
    int32_t stepsPerSecond;
    uint32_t step;
    uint8_t henchLimit, streams, superEasy, feedbackAllowed;
    int32_t launch978;
    int16_t maxTraffic;
    int32_t framesSinceAudio;
    uint32_t lastIncidental, lastPlayerIncidental;
    char stinger[8];
    char tuningLevel[16];
    EventDynamicData dynamic;
};

Globals g_globals;

void WriteGlobals(const Globals &g) {
    SimStateGame = g.simState;
    SimStepsPerSecondGame = g.stepsPerSecond;
    SimStepCountGame = g.step;
    HenchLimitGame = g.henchLimit;
    StreamsGame = g.streams;
    SuperEasyGame = g.superEasy;
    FeedbackAllowedGame = g.feedbackAllowed;
    Launch978Game = g.launch978;
    MaxTrafficGame = g.maxTraffic;
    FramesSinceAudioGame = g.framesSinceAudio;
    LastIncidentalGame = g.lastIncidental;
    LastPlayerIncidentalGame = g.lastPlayerIncidental;
    LastIncidentalStep = g.lastIncidental;
    LastPlayerIncidentalStep = g.lastPlayerIncidental;
    memcpy(StingerGame, g.stinger, sizeof(g.stinger));
    memcpy(StingerChannel, g.stinger, sizeof(g.stinger));
    memcpy(TuningLevelGame, g.tuningLevel, sizeof(g.tuningLevel));
    gEventDynamicData = g.dynamic;
    memcpy(PhysicsObjectsGame, W->physicsObjects, sizeof(W->physicsObjects));
}

// The globals after a side: the timers from the side's own copy
void ReadGlobals(Globals *g, bool original) {
    memset(g, 0, sizeof(*g));
    g->simState = SimStateGame;
    g->stepsPerSecond = SimStepsPerSecondGame;
    g->step = SimStepCountGame;
    g->henchLimit = HenchLimitGame;
    g->streams = StreamsGame;
    g->superEasy = SuperEasyGame;
    g->feedbackAllowed = FeedbackAllowedGame;
    g->launch978 = Launch978Game;
    g->maxTraffic = MaxTrafficGame;
    g->framesSinceAudio = FramesSinceAudioGame;
    g->lastIncidental = original ? LastIncidentalGame : LastIncidentalStep;
    g->lastPlayerIncidental = original ? LastPlayerIncidentalGame : LastPlayerIncidentalStep;
    memcpy(g->stinger, original ? StingerGame : StingerChannel, sizeof(g->stinger));
    memcpy(g->tuningLevel, TuningLevelGame, sizeof(g->tuningLevel));
    g->dynamic = gEventDynamicData;
}

// =============================================================================================================
// The fakes
// =============================================================================================================

// ---- Event's allocation

void *FakeEventNew(size_t size) {
    Call("Event::new %u", unsigned(size));
    memset(W->eventScratch, 0xcd, sizeof(W->eventScratch));
    return W->eventScratch;
}
void FakeEventDelete(void *event, size_t size) {
    Call("Event::delete %d %u", int(Word(event) - Word(W->eventScratch)), unsigned(size));
}

CARP::TagReference *__fastcall FakeTagConstruct(CARP::TagReference *self, int, UGroup *shared, UGroup *local) {
    Call("TagReference %08x %08x %08x", self->value, Word(shared), Word(local));
    self->value = self->value * 3 + 0x10;
    return self;
}

// ---- the AI

CARP::AIElement *__fastcall FakeRedirect(void *self, int, CARP::AIElement *element) {
    Call("Redirect %08x %08x", Word(self), Word(element));
    for (int i = 0; i < kElements; i++)
        if (element == &W->records[i])
            return &W->elements[i];
    return element;
}
void *__fastcall FakeTracking(void *self, int, CARP::AIElement *element) {
    Call("Tracking %08x %08x", Word(self), Word(element));
    return W->tracking;
}
void __fastcall FakeSetWakeRange(void *self, int, float range) {
    Call("SetWakeRange %08x %08x", Word(self), Bits(range));
}
void __fastcall FakeForceVehicleToSleep(void *self, int, void *vehicle) {
    Call("ForceVehicleToSleep %08x %08x", Word(self), Word(vehicle));
}
bool __fastcall FakeOkToSpawnHench(void *self, int) {
    Call("OkToSpawnHench %08x", Word(self));
    return W->okToSpawn != 0;
}
void *FakeVehicleControllerGet() {
    Call("AIVehicleController::Get");
    return W->vehicleController;
}
void __fastcall FakeUpdateVehicles(void *self, int) {
    Call("UpdateVehicles %08x", Word(self));
}
void *__fastcall FakeGetPlayerGround(void *self, int, int player) {
    Call("GetPlayerGroundVehiclePtr %08x %d", Word(self), player);
    return W->playerGround;
}
void *FakeZoneControllerGet() {
    Call("AIZoneController::Get");
    return W->zoneController;
}
void __fastcall FakeZoneUpdate(void *self, int) {
    Call("AIZoneController::Update %08x", Word(self));
}
void __fastcall FakeRegisterZone(void *self, int, int type, const ECollision::Zone *zone) {
    Call("RegisterZone %08x %d", Word(self), type);
    Vec("position", &zone->position, 3);
    Vec("velocity", &zone->velocity, 3);
    Vec("normal", &zone->normal, 3);
}
void FakeManage() {
    Call("AICharacterPedestrian::Manage");
}
void __fastcall FakeShowHands(void *self, int) {
    Call("ShowHands %08x", Word(self));
}
void __fastcall FakeHideHands(void *self, int) {
    Call("HideHands %08x", Word(self));
}
void FakePassengerInit() {
    Call("AICharacterPassenger::Init");
}
void FakeBondInit() {
    Call("AICharacterBond::Init");
}
void __fastcall FakeSetTargetMode(void *self, int, uint8_t mode, const Coord3 *target) {
    Call("SetTargetMode %08x %u", Word(self), mode);
    Vec("target", target, 3);
}
void __fastcall FakeSetNavigateMode(void *self, int, uint8_t mode, void *target) {
    Call("SetNavigateMode %08x %u %08x", Word(self), mode, Word(target));
}
void __fastcall FakeSetAttackMode(void *self, int, uint8_t mode) {
    Call("SetAttackMode %08x %u", Word(self), mode);
}
void __fastcall FakeSetSecondaryTarget(void *self, int, void *target) {
    Call("SetSecondaryTarget %08x %08x", Word(self), Word(target));
}
void __fastcall FakeEngage(void *self, int) {
    Call("EngageAutoDrive %08x", Word(self));
}
void __fastcall FakeDisengage(void *self, int) {
    Call("DisengageAutoDrive %08x", Word(self));
}
void __fastcall FakeUnSpawn(void *self, int) {
    Call("UnSpawn %08x", Word(self));
}
void __fastcall FakeNewState(void *self, int, AICommand *command) {
    Call("NewState %08x %08x", Word(self), Word(command));
}
PhysicsObject *__fastcall FakeGetPhysicsObject(void *self, int) {
    Call("GetPhysicsObject %08x", Word(self));
    int i = VehicleIndex(self);
    return &W->cars[i < 0 ? 0 : i].car;
}
const Coord4 *__fastcall FakeVehicleGetPosition(void *self, int) {
    Call("AIVehicle::GetPosition %08x", Word(self));
    int i = VehicleIndex(self);
    return &W->vehiclePositions[i < 0 ? 0 : i];
}
void __fastcall FakeAIControlToSpline(void *self, int, void *spline) {
    Call("AI slot 3 %08x %08x", Word(self), Word(spline));
}
void __fastcall FakeAIEndControlToSpline(void *self, int) {
    Call("AI slot 4 %08x", Word(self));
}

// ---- the mission manager

void __fastcall FakeAddScore(void *self, int, int score, int points) {
    Call("AddScore %08x %d %d", Word(self), score, points);
}
void __fastcall FakeChangeStage(void *self, int, int stage) {
    Call("ChangeStage %08x %d", Word(self), stage);
}
void __fastcall FakeMissionSetAutoDrive(void *self, int, bool on, bool controls) {
    Call("SMissionManager::SetAutoDrive %08x %u %u", Word(self), on, controls);
}
void __fastcall FakeIncShotsHit(void *self, int, bool hit) {
    Call("IncShotsHit %08x %u", Word(self), hit);
}
void __fastcall FakeIncKills(void *self, int, int kills) {
    Call("IncKills %08x %d", Word(self), kills);
}
int __fastcall FakeGetMagicCounter(void *self, int, int counter) {
    Call("GetMagicCounter %08x %d", Word(self), counter);
    return W->magicCounter;
}
void __fastcall FakeSetMagicCounter(void *self, int, int counter, int value) {
    Call("SetMagicCounter %08x %d %08x", Word(self), counter, value);
}
void __fastcall FakeClearProgrammerEvent(void *self, int, int event) {
    Call("ClearProgrammerEvent %08x %d", Word(self), event);
}
void __fastcall FakeTallyScores(void *self, int, void *page, bool end) {
    Call("TallyScores %08x %08x %u", Word(self), Word(page), end);
}
void __fastcall FakeClearCinematicMode(void *self, int, int mask) {
    Call("ClearCinematicMode %08x %d", Word(self), mask);
}
void __fastcall FakeSetCinematicMode(void *self, int, int mask) {
    Call("SetCinematicMode %08x %d", Word(self), mask);
}
void __fastcall FakeWin(void *self, int) {
    Call("Win %08x", Word(self));
}
void __fastcall FakeCallStage(void *self, int, int stage) {
    Call("CallStage %08x %d", Word(self), stage);
}
void __fastcall FakeLoadWeaponSet(void *self, int) {
    Call("LoadWeaponSet %08x", Word(self));
    std::string bytes;
    char hex[4];
    for (int i = 0; i < 32; i++) {
        snprintf(hex, sizeof(hex), "%02x", W->missionSet.weaponEnabled[i]);
        bytes += hex;
    }
    Call(" enabled %s ammo %d %d %d", bytes.c_str(), W->missionSet.weaponAmmo[0], W->missionSet.weaponAmmo[1],
         W->missionSet.weaponAmmo[2]);
}

// ---- the Simulation

RigidBody *__fastcall FakeGetRigidBody(void *self, int, int slot) {
    Call("GetRigidBody %08x %d", Word(self), slot);
    return &W->bodies[slot & (kBodies - 1)];
}
void *__fastcall FakeGetSimpleRigidBody(void *self, int, int slot) {
    Call("GetSimpleRigidBody %08x %d", Word(self), slot);
    return &W->simpleBodies[slot & (kBodies - 1)];
}
void __fastcall FakeTrackInstance(void *self, int, void *instance, int unknown) {
    Call("TrackInstance %08x %08x %d", Word(self), Word(instance), unknown);
}
void __fastcall FakeUntrackInstance(void *self, int, void *instance) {
    Call("UntrackInstance %08x %08x", Word(self), Word(instance));
}

// ---- the HUD and GFX

void *FakeTheApp() {
    Call("GHud::TheApp");
    return W->hud;
}
void __fastcall FakeSetLevelLoadText(void *self, int) {
    Call("SetLevelLoadText %08x", Word(self));
}
void __fastcall FakeHudSetAutoDrive(void *self, int, bool on) {
    Call("GHud::SetAutoDrive %08x %u", Word(self), on);
}
void __fastcall FakeKillButtonMessage(void *self, int) {
    Call("KillButtonMessage %08x", Word(self));
}
void __fastcall FakeTriggerButtonMsg(void *self, int, int a, int b, int c, int d, float time) {
    Call("TriggerButtonMsg %08x %d %d %d %d %08x", Word(self), a, b, c, d, Bits(time));
}
void __fastcall FakeTriggerMissionMsg(void *self, int, int message, float time) {
    Call("TriggerMissionMsg %08x %d %08x", Word(self), message, Bits(time));
}
void *FakeLookupReference(const char *name) {
    Call("GFX::LookupReference %s", name);
    return W->gfxFound ? W->hud : NULL;
}
void FakeTriggerIncidental(void *reference, const Coord4 *position, const Coord4 *up, const Coord4 *velocity,
                           const Coord4 *offset, int a, int b, int c) {
    Call("GFX::TriggerIncidental %08x %d %d %d", Word(reference), a, b, c);
    Vec("position", position, 4);
    Vec("up", up, 4);
    Vec("velocity", velocity, 4);
    Vec("offset", offset, 4);
}

// ---- the camera (ported: both entries)

void __fastcall FakeAbortCinematic(void *self, int) {
    Call("AbortCinematic %08x", Word(self));
}
void __fastcall FakeCameraLockOn(void *self, int, const Coord4 *point, const Coord4 *target, int param, int quiet) {
    Call("CameraLockOn %08x %08x %d %d", Word(self), Word(point), param, quiet);
    Vec("point", point, 4);
    Vec("target", target, 4);
}
void __fastcall FakeShakeCamera(void *self, int, int duration, float range, Coord4 *origin, int keep) {
    Call("ShakeCamera %08x %d %08x %08x %d", Word(self), duration, Bits(range), Word(origin), keep);
}
void __fastcall FakeSetCameraZoom(void *self, int, int kind, float value, int steps) {
    Call("SetCameraZoom %08x %d %08x %d", Word(self), kind, Bits(value), steps);
}
void __fastcall FakeForceCameraChange(void *self, int, unsigned id, int delay, int b, int unused) {
    Call("ForceCameraChange %08x %u %d %d %d", Word(self), id, delay, b, unused);
}
void __fastcall FakeEndCameraAnim(void *self, int, unsigned short delay, int b, bool c, unsigned id) {
    Call("EndCameraAnim %08x %u %d %u %u", Word(self), delay, b, c, id);
}
void __fastcall FakeSetCinematicCamera(void *self, int, Coord4 *place, uint16_t delay, uint16_t unknown) {
    Call("SetCinematicCamera %08x %u %u", Word(self), delay, unknown);
    Vec("place", place, 3);
}
void __fastcall FakeSetLastSelectable(void *self, int, int delay, unsigned b, bool relative, bool notAutoDrive) {
    Call("SetLastSelectableCameraMode %08x %d %u %u %u", Word(self), delay, b, relative, notAutoDrive);
}
void __fastcall FakeForwardLock(void *self, int, bool lock) {
    Call("SetAutoDriveForwardLock %08x %u", Word(self), lock);
}
void __fastcall FakeControlToCPU(void *self, int, bool cpu) {
    Call("SetControlToCPU %08x %u", Word(self), cpu);
}
void __fastcall FakeSetAnchor(void *self, int, void *anchor) {
    Call("SetAnchor %08x %08x", Word(self), Word(anchor));
}
void __fastcall FakeUpdateCamera(void *self, int) {
    Call("UpdateCamera %08x", Word(self));
}

// ---- the sounds

void *FakeFaderGet(const char *name) {
    Call("AFader::Get %s", name);
    return W->fader;
}
void __fastcall FakeQueuePrimary(void *self, int, const char *name, float fade, bool effect, bool held) {
    Call("QueuePrimary %08x %s %08x %u %u", Word(self), name, Bits(fade), effect, held);
}
void __fastcall FakeCall911(void *self, int) {
    Call("Call911 %08x", Word(self));
}
void __fastcall FakeSetSecondary(void *self, int, const char *name) {
    Call("SetSecondary %08x %s", Word(self), name);
}
void __fastcall FakeSetFilter(void *self, int, float filter) {
    Call("SetFilter %08x %08x", Word(self), Bits(filter));
}
void __fastcall FakeFaderUpdate(void *self, int) {
    Call("AFader::Update %08x", Word(self));
}
void *FakeStreamGet(const char *name) {
    Call("AStream::Get %s", name);
    return W->stream;
}
void __fastcall FakeStreamEvent(void *self, int, const char *name, float fade, bool loop, bool effect, bool held) {
    Call("AStream::Event %08x %s %08x %u %u %u", Word(self), name, Bits(fade), loop, effect, held);
}
void *FakeMixGet(const char *name) {
    Call("AMix::Get %s", name);
    return &W->mix;
}
void FakeSetMode(int mode, int slot, int priority) {
    Call("AFX::SetMode %d %d %d", mode, slot, priority);
}
void FakeBuildPaths(const AListener *listener) {
    // Not the level: the camera's constructor leaves it as the allocator had it
    Call("BuildPaths view %d", listener->view);
    Vec("matrix", &listener->matrix, 16);
    Vec("position", &listener->position, 4);
    Vec("velocity", &listener->velocity, 3);
    Vec("unknown60", listener->unknown60, 4);
    Vec("bandWidth", &listener->bandWidth, 1);
}
void FakePlayVoices() {
    Call("AVoice::PlayVoices");
}
void __fastcall FakeSoundGroupUpdate(void *self, int) {
    Call("WSoundGroup::Update %08x", Word(self));
}
void __fastcall FakePlayLanding(void *self, int, float volume, const Coord3 *position, const Coord3 *velocity) {
    Call("sound slot 4 %08x %08x", Word(self), Bits(volume));
    Vec("position", position, 3);
    Vec("velocity", velocity, 3);
}
void __fastcall FakePlayCollision(void *self, int, float strength, const Coord3 *position, const Coord3 *velocity,
                                  int surface) {
    Call("sound slot 5 %08x %08x %d", Word(self), Bits(strength), surface);
    Vec("position", position, 3);
    Vec("velocity", velocity, 3);
}

// ---- the world and its objects

void *__fastcall FakeGetEnviroDesc(void *self, int, const void *position) {
    Call("GetEnviroDesc %08x %08x", Word(self), Word(position));
    return &W->enviro;
}
void *__fastcall FakeGetSceneObj(void *self, int, const void *instance) {
    Call("GetSceneObjFromInstance %08x %08x", Word(self), Word(instance));
    return W->sceneObjFound ? W->sceneObj : NULL;
}
bool __fastcall FakeFindClosestFace(void *self, int, const Coord3 *point, bool keep) {
    Call("FindClosestFace %08x %u", Word(self), keep);
    Vec("point", point, 3);
    return true;
}
void __fastcall FakeHide(void *self, int) {
    Call("RSceneObj::Hide %08x", Word(self));
}
void __fastcall FakeShow(void *self, int) {
    Call("RSceneObj::Show %08x", Word(self));
}
uint32_t __fastcall FakeGetInstancePosition(void *self, int, uint32_t index, MATRIX4 *transform, bool world) {
    Call("GetInstancePosition %08x %u %u", Word(self), index, world);
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            transform->mtx[r][c] = float(r * 4 + c) * 0.5f + float(index);
    return 0;
}
void __fastcall FakeSceneObjAddCollisionInfo(void *self, int, const Coord3 *centre, const Coord3 *extents) {
    Call("RSceneObj::AddCollisionInfo %08x", Word(self));
    Vec("centre", centre, 3);
    Vec("extents", extents, 3);
}
void __fastcall FakeTriggerFX(void *self, int, int type, uint32_t which, uint32_t unused3, const void *unused4,
                              const void *unused5, float intensity) {
    Call("TriggerFX %08x %d %u %u %08x", Word(self), type, which, unused3, Bits(intensity));
    Vec("position", unused4, 3);
    Vec("velocity", unused5, 3);
}
void __fastcall FakeStartMuzzleFlash(void *self, int, const Coord3 *position) {
    Call("StartMuzzleFlash %08x", Word(self));
    Vec("position", position, 3);
}
void FakeAnimUpdate(uint32_t tick) {
    Call("RAnimEngine::Update %u", tick);
}
void FakePathUpdate(float time) {
    Call("RPathEngine::Update %08x", Bits(time));
}
void __fastcall FakeProcessStimuli(void *self, int, uint32_t id, uint8_t stimulus, uint32_t tick, int mode) {
    Call("ProcessStimuli %08x %u %u %u %d", Word(self), id, stimulus, tick, mode);
}
void __fastcall FakeFeedbackAddCollisionInfo(void *self, int, const CollisionImpact *impact) {
    Call("IFeedback::AddCollisionInfo %08x", Word(self));
    Vec("impact", impact, 0x54 / 4);
}
void __fastcall FakeSetVibrate(void *self, int, float strength, float duration) {
    Call("SetVibrate %08x %08x %08x", Word(self), Bits(strength), Bits(duration));
}
void __fastcall FakeGenerateImpulse(void *self, int, CollisionImpact *impact, const Coord4 *normal,
                                    const Coord4 *point, float push, bool fromWorld, uint16_t tag, float scale) {
    Call("GenerateImpulse %08x %08x %u %u %08x", Word(self), Bits(push), fromWorld, tag, Bits(scale));
    Vec("impact", impact, 0x38 / 4);
    Vec("impact40", &impact->closingSpeed, 5);
    Vec("normal", normal, 4);
    Vec("point", point, 3);
}
PhysicsObject *__fastcall FakeGetOwner(RigidBody *self, int) {
    Call("GetOwner %08x", Word(self));
    for (int i = 0; i < kBodies; i++)
        if (self == &W->bodies[i])
            return &W->cars[W->owners[i] % kCars].car;
    return &W->cars[0].car;
}
void __fastcall FakeSetAngularMomentum(void *self, int, const Coord3 *local) {
    Call("SetAngularMomentum %08x", Word(self));
    Vec("momentum", local, 3);
}
uint8_t __fastcall FakeLookupBool(void *self, int, const char *key, bool *found) {
    Call("LookupBool %08x %s %08x", Word(self), key, Word(found));
    static const char *const kKeys[5] = { "HAS_MACHINEGUNS", "HAS_CANNONS", "HAS_ROCKETS", "HAS_MISSLES",
                                          "HAS_TORPEDOS" };
    for (int i = 0; i < 5; i++)
        if (strcmp(key, kKeys[i]) == 0)
            return W->lookupBools[i];
    return 0;
}
void *FakeGetIOModule() {
    Call("IOModule::GetIOModule");
    return W->ioModule;
}
int FakeTotalUnused(unsigned flags) {
    Call("MEM_totalunused %u", flags);
    return 0;
}
int FakePrintf(const char *format, ...) {
    Call("printf %s", format);
    return 0;
}
Coord3 *__fastcall FakeCarGetPosition(PhysicsObject *self, int) {
    Call("PhysicsObject::GetPosition %08x", Word(self));
    return reinterpret_cast<Coord3 *>(&reinterpret_cast<FakeCar *>(self)->position);
}
Coord3 *__fastcall FakeCarGetVelocity(PhysicsObject *self, int) {
    Call("PhysicsObject::GetLinearVelocity %08x", Word(self));
    return reinterpret_cast<Coord3 *>(&reinterpret_cast<FakeCar *>(self)->velocity);
}

// ---- the cars' virtual methods

int __fastcall FakeCarApplyDamage(FakeCar *self, int, const void *a, const void *b, float amount, float unknown4,
                                  int kind, const uint32_t *sig) {
    Call("car slot 2 %d %08x %08x %d %08x", CarIndex(self), Bits(amount), Bits(unknown4), kind, Word(sig));
    Vec("a", a, 3);
    Vec("b", b, 3);
    return 0x20;
}
AIGroundVehicle *__fastcall FakeCarGetAI(FakeCar *self, int) {
    Call("car slot 10 %d", CarIndex(self));
    return self->ai;
}
void __fastcall FakeCarChangeType(FakeCar *self, int, const char *type, bool purge) {
    Call("car slot 16 %d %s %u", CarIndex(self), type, purge);
}
int __fastcall FakeCarGetClass(FakeCar *self, int) {
    Call("car slot 18 %d", CarIndex(self));
    return self->carClass;
}
void __fastcall FakeCarSetTargetGas(FakeCar *self, int, float gas) {
    Call("car slot 35 %d %08x", CarIndex(self), Bits(gas));
}
void __fastcall FakeCarSetTargetBrake(FakeCar *self, int, float brake) {
    Call("car slot 36 %d %08x", CarIndex(self), Bits(brake));
}
void __fastcall FakeCarResetAt(FakeCar *self, int, Coord3 *position, const Coord3 *direction) {
    Call("car slot 42 %d", CarIndex(self));
    Vec("position", position, 3);
    Vec("direction", direction, 3);
}
void __fastcall FakeCarSetScoreable(FakeCar *self, int, bool scoreable) {
    Call("car slot 44 %d %u", CarIndex(self), scoreable);
}
CarPhysics *__fastcall FakeCarGetPhysics(FakeCar *self, int) {
    Call("car slot 57 %d", CarIndex(self));
    return &W->physics;
}
const Coord4 *__fastcall FakeCarGetWheelPos(FakeCar *self, int, int wheel) {
    Call("car slot 74 %d %d", CarIndex(self), wheel);
    return &self->wheels[wheel & 3];
}
void __fastcall FakeCarForceStopOff(FakeCar *self, int, uint8_t flags) {
    Call("car slot 78 %d %u", CarIndex(self), flags);
}
const char *__fastcall FakeCarGetSecondaryType(FakeCar *self, int) {
    Call("car slot 81 %d", CarIndex(self));
    return "secondary";
}
void __fastcall FakeUnexpectedSlot(void *self, int) {
    Call("UNEXPECTED virtual call %08x", Word(self));
}

// ---- the AVoice and AOneShotSound helpers' callees

void __fastcall FakeViewDestruct(AVoice::View *self, int) {
    Call("View::Destruct %08x", Word(self));
}
ABaseSound *__fastcall FakeBaseSoundConstruct(ABaseSound *self, int, const char *mix, int view) {
    Call("ABaseSound::Construct %08x %s %d", Word(self), mix, view);
    self->vtable = 0x0018a510;
    self->mix = reinterpret_cast<AMix *>(W->stream);
    return self;
}
AVoice *__fastcall FakeVoiceConstruct(AVoice *self, int, AMix *mix, int bank, int patch) {
    Call("AVoice::Construct %08x %08x %d %d", Word(self), Word(mix), bank, patch);
    for (int i = 0; i < 3; i++)
        self->views[i].loop = 1;
    return self;
}

Fake g_fakes[] = {
    { 0x0005a5a0, (const void *)&FakeEventNew },
    { 0x0005a5c0, (const void *)&FakeEventDelete },
    { 0x00117e50, (const void *)&FakeTagConstruct },
    { 0x000286d0, (const void *)&FakeRedirect },
    { 0x00028700, (const void *)&FakeTracking },
    { 0x00027cb0, (const void *)&FakeSetWakeRange },
    { 0x000289f0, (const void *)&FakeForceVehicleToSleep },
    { 0x000286b0, (const void *)&FakeOkToSpawnHench },
    { 0x00035c00, (const void *)&FakeVehicleControllerGet },
    { 0x00035ff0, (const void *)&FakeUpdateVehicles },
    { 0x00035e70, (const void *)&FakeGetPlayerGround },
    { 0x00036a10, (const void *)&FakeZoneControllerGet },
    { 0x00036ac0, (const void *)&FakeZoneUpdate },
    { 0x00036bb0, (const void *)&FakeRegisterZone },
    { 0x00026ed0, (const void *)&FakeManage },
    { 0x00022c30, (const void *)&FakeShowHands },
    { 0x00022c50, (const void *)&FakeHideHands },
    { 0x00023700, (const void *)&FakePassengerInit },
    { 0x0001cd80, (const void *)&FakeBondInit },
    { 0x0002a2c0, (const void *)&FakeSetTargetMode },
    { 0x0002a0d0, (const void *)&FakeSetNavigateMode },
    { 0x0002a330, (const void *)&FakeSetAttackMode },
    { 0x0002a380, (const void *)&FakeSetSecondaryTarget },
    { 0x0002a690, (const void *)&FakeEngage },
    { 0x0002a760, (const void *)&FakeDisengage },
    { 0x0002dac0, (const void *)&FakeUnSpawn },
    { 0x00032700, (const void *)&FakeNewState },
    { 0x00035840, (const void *)&FakeGetPhysicsObject },
    { 0x000359e0, (const void *)&FakeVehicleGetPosition },
    { 0x000b6e30, (const void *)&FakeAddScore },
    { 0x000b64b0, (const void *)&FakeChangeStage },
    { 0x000b64c0, (const void *)&FakeMissionSetAutoDrive },
    { 0x000b6720, (const void *)&FakeIncShotsHit },
    { 0x000b6770, (const void *)&FakeIncKills },
    { 0x000b67a0, (const void *)&FakeGetMagicCounter },
    { 0x000b7ac0, (const void *)&FakeSetMagicCounter },
    { 0x000b7310, (const void *)&FakeClearProgrammerEvent },
    { 0x000b7470, (const void *)&FakeTallyScores },
    { 0x000b7950, (const void *)&FakeClearCinematicMode },
    { 0x000b8160, (const void *)&FakeSetCinematicMode },
    { 0x000b84b0, (const void *)&FakeWin },
    { 0x000b85d0, (const void *)&FakeCallStage },
    { 0x000b6160, (const void *)&FakeLoadWeaponSet },
    { 0x000b2700, (const void *)&FakeGetRigidBody },
    { 0x000b2730, (const void *)&FakeGetSimpleRigidBody },
    { 0x000b4810, (const void *)&FakeTrackInstance },
    { 0x000b3000, (const void *)&FakeUntrackInstance },
    { 0x000d7c10, (const void *)&FakeTheApp },
    { 0x000d7c20, (const void *)&FakeSetLevelLoadText },
    { 0x000d9040, (const void *)&FakeHudSetAutoDrive },
    { 0x000da260, (const void *)&FakeKillButtonMessage },
    { 0x000da180, (const void *)&FakeTriggerButtonMsg },
    { 0x000da030, (const void *)&FakeTriggerMissionMsg },
    { 0x000d35f0, (const void *)&FakeLookupReference },
    { 0x000d3520, (const void *)&FakeTriggerIncidental },
    { 0x00089010, (const void *)&FakeAbortCinematic },
    { 0x00081610, (const void *)&FakeCameraLockOn },
    { 0x00083e60, (const void *)&FakeShakeCamera },
    { 0x00083940, (const void *)&FakeSetCameraZoom },
    { 0x00089a40, (const void *)&FakeForceCameraChange },
    { 0x00089ac0, (const void *)&FakeEndCameraAnim },
    { 0x000848d0, (const void *)&FakeSetCinematicCamera },
    { 0x000894d0, (const void *)&FakeSetLastSelectable },
    { 0x00081930, (const void *)&FakeForwardLock },
    { 0x00081940, (const void *)&FakeControlToCPU },
    { 0x00097530, (const void *)&FakeSetAnchor },
    { 0x00125d40, (const void *)&FakeFaderGet },
    { 0x001251f0, (const void *)&FakeQueuePrimary },
    { 0x00125230, (const void *)&FakeCall911 },
    { 0x00125200, (const void *)&FakeSetSecondary },
    { 0x00125220, (const void *)&FakeSetFilter },
    { 0x00125260, (const void *)&FakeFaderUpdate },
    { 0x001237d0, (const void *)&FakeStreamGet },
    { 0x001234d0, (const void *)&FakeStreamEvent },
    { 0x0011d720, (const void *)&FakeMixGet },
    { 0x001247d0, (const void *)&FakeSetMode },
    { 0x00120e60, (const void *)&FakeBuildPaths },
    { 0x00123a30, (const void *)&FakePlayVoices },
    { 0x000ccb40, (const void *)&FakeSoundGroupUpdate },
    { 0x000d1470, (const void *)&FakeGetEnviroDesc },
    { 0x000d14a0, (const void *)&FakeGetSceneObj },
    { 0x000d31f0, (const void *)&FakeFindClosestFace },
    { 0x0008db30, (const void *)&FakeHide },
    { 0x0008db40, (const void *)&FakeShow },
    { 0x0008dd20, (const void *)&FakeGetInstancePosition },
    { 0x0008e1f0, (const void *)&FakeSceneObjAddCollisionInfo },
    { 0x0001b270, (const void *)&FakeStartMuzzleFlash },
    { 0x00078190, (const void *)&FakeAnimUpdate },
    { 0x0007fff0, (const void *)&FakePathUpdate },
    { 0x00077d70, (const void *)&FakeProcessStimuli },
    { 0x0004fee0, (const void *)&FakeFeedbackAddCollisionInfo },
    { 0x0004f9b0, (const void *)&FakeSetVibrate },
    { 0x000afdb0, (const void *)&FakeGenerateImpulse },
    { 0x000ad100, (const void *)&FakeGetOwner },
    { 0x000ad310, (const void *)&FakeSetAngularMomentum },
    { 0x000580e0, (const void *)&FakeLookupBool },
    { 0x00051580, (const void *)&FakeGetIOModule },
    { 0x00113ac0, (const void *)&FakeTotalUnused },
    { 0x00132192, (const void *)&FakePrintf },
    { 0x0006f330, (const void *)&FakeCarGetPosition },
    { 0x0006f360, (const void *)&FakeCarGetVelocity },
    { 0x00123c50, (const void *)&FakeViewDestruct },
    { 0x0001bfa0, (const void *)&FakeBaseSoundConstruct },
    { 0x00123ca0, (const void *)&FakeVoiceConstruct },
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
    g_carVtable[2] = (void *)&FakeCarApplyDamage;
    g_carVtable[PVehicle::kGetAIGroundVehiclePtr] = (void *)&FakeCarGetAI;
    g_carVtable[PVehicle::kChangeCarType] = (void *)&FakeCarChangeType;
    g_carVtable[PVehicle::kGetCarClass] = (void *)&FakeCarGetClass;
    g_carVtable[PVehicle::kSetTargetGas] = (void *)&FakeCarSetTargetGas;
    g_carVtable[PVehicle::kSetTargetBrake] = (void *)&FakeCarSetTargetBrake;
    g_carVtable[PVehicle::kResetCarAt] = (void *)&FakeCarResetAt;
    g_carVtable[PVehicle::kSetScoreable] = (void *)&FakeCarSetScoreable;
    g_carVtable[PVehicle::kGetPhysics] = (void *)&FakeCarGetPhysics;
    g_carVtable[PVehicle::kGetWheelPos] = (void *)&FakeCarGetWheelPos;
    g_carVtable[PVehicle::kForceStopOff] = (void *)&FakeCarForceStopOff;
    g_carVtable[PVehicle::kGetSecondaryType] = (void *)&FakeCarGetSecondaryType;
    for (void *&slot : g_aiVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_aiVtable[3] = (void *)&FakeAIControlToSpline;
    g_aiVtable[4] = (void *)&FakeAIEndControlToSpline;
    for (void *&slot : g_cameraVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_cameraVtable[3] = (void *)&FakeUpdateCamera;
    for (void *&slot : g_soundVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_soundVtable[4] = (void *)&FakePlayLanding;
    g_soundVtable[5] = (void *)&FakePlayCollision;
    for (void *&slot : g_sceneObjVtable)
        slot = (void *)&FakeUnexpectedSlot;
    g_sceneObjVtable[15] = (void *)&FakeTriggerFX;
}

// =============================================================================================================
// The world
// =============================================================================================================

float RandomFloat() {
    switch (Random(8)) {
    case 0: return 0.0f;
    case 1: return 1.5f;
    case 2: return -Uniform(0.0f, 4.0f);
    case 3: return Uniform(0.0f, 0.2f);
    default: return Uniform(-30.0f, 30.0f);
    }
}

void RandomBytes(void *at, size_t size) {
    uint8_t *bytes = static_cast<uint8_t *>(at);
    for (size_t i = 0; i < size; i++)
        bytes[i] = uint8_t(Random());
}

void RandomVector(Coord4 *v, float range) {
    v->x = Uniform(-range, range);
    v->y = Uniform(-range, range);
    v->z = Uniform(-range, range);
    v->w = Uniform(-1.0f, 1.0f);
}

void RandomMatrix(MATRIX4 *m) {
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            m->mtx[r][c] = Uniform(-1.0f, 1.0f);
    m->mtx[3][0] = Uniform(-50.0f, 50.0f);
    m->mtx[3][1] = Uniform(-5.0f, 5.0f);
    m->mtx[3][2] = Uniform(-50.0f, 50.0f);
}

AICharacter *AnyCharacter() {
    return Chance(4) ? NULL : &W->characters[Random(kCharacters)];
}

void BuildWorld() {
    memset(W, 0, sizeof(*W));
    for (int i = 0; i < kElements; i++) {
        RandomBytes(&W->records[i], sizeof(CARP::AIElement));
        CARP::AIElement *e = &W->elements[i];
        RandomBytes(e, sizeof(CARP::AIElement));
        e->type = Chance(5) ? int32_t(Random(4)) : int32_t(Random(2));
        e->state = Chance(4) ? 0 : 1;
        e->flags = uint8_t(Random());
        e->memberCount = int32_t(Random(4));
        if (e->type == CARP::kAIElementCharacter)
            e->character = AnyCharacter();
        else
            e->vehicle = Chance(6) ? NULL : &W->vehicles[Random(kVehicles)];
        e->person.human = Chance(3) ? NULL : reinterpret_cast<Human *>(W->human);
        for (int m = 0; m < 3; m++)
            e->memberCharacters[m] = AnyCharacter();
        e->object = Chance(5) ? NULL : &W->cars[Random(kCars)].car;
        e->command = NULL;
        e->car.helicopter = Chance(2) ? 0 : 1;
        if (e->car.helicopter == 0 && e->object != NULL)
            e->object = reinterpret_cast<PhysicsObject *>(W->helicopter);
    }
    RandomBytes(W->human, sizeof(W->human));
    reinterpret_cast<Human *>(W->human)->character = &W->characters[0];
    RandomBytes(W->helicopter, sizeof(W->helicopter));
    *reinterpret_cast<void ***>(W->helicopter) = g_carVtable;
    reinterpret_cast<PhysicsObject *>(W->helicopter)->renderObject = reinterpret_cast<RSceneObj *>(W->sceneObj);
    for (int i = 0; i < kCharacters; i++)
        RandomBytes(&W->characters[i], sizeof(AICharacter));
    for (int i = 0; i < kVehicles; i++) {
        AIGroundVehicle *v = &W->vehicles[i];
        RandomBytes(v, sizeof(*v));
        *reinterpret_cast<void ***>(v) = g_aiVtable;
        v->vehicleType = Chance(6) ? 0 : int32_t(1 + Random(2));
        v->active = Chance(5) ? 0 : 1;
        v->command = NULL;
        RandomBytes(W->navs[i], sizeof(W->navs[i]));
        v->driveToNav = reinterpret_cast<WRoadNav *>(W->navs[i]);
        RandomVector(&W->vehiclePositions[i], 20.0f);
    }
    for (int i = 0; i < kCars; i++) {
        FakeCar *car = &W->cars[i];
        RandomBytes(car, sizeof(*car));
        car->car.vtable = g_carVtable;
        car->car.type = Chance(4) ? 0 : 1;
        car->car.rigidBodySlot = int16_t(Random(kBodies));
        car->car.renderObject = reinterpret_cast<RSceneObj *>(W->sceneObj);
        car->car.audioObject = Chance(4) ? NULL : reinterpret_cast<ABaseSound *>(W->sound);
        car->car.feedbackObject = Chance(4) ? NULL : &W->feedback;
        car->car.worldPos.valid = uint8_t(Random(2));
        car->car.worldPos.instance = Chance(3) ? NULL : reinterpret_cast<WCollisionInstance *>(W->tracking);
        car->car.worldPos.lastQueryStep = int32_t(Random(3));
        RandomVector(&car->position, 30.0f);
        RandomVector(&car->velocity, 10.0f);
        for (int w = 0; w < 4; w++)
            RandomVector(&car->wheels[w], 3.0f);
        car->carClass = Chance(2) ? kCarClass3 : int32_t(Random(5));
        car->ai = Chance(5) ? NULL : &W->vehicles[Random(kVehicles)];
        W->carVector[i] = &car->car;
    }
    W->cars[0].ai = &W->vehicles[0];
    for (int i = 0; i < kBodies; i++) {
        RandomBytes(&W->bodies[i], sizeof(RigidBody));
        RandomBytes(&W->simpleBodies[i], sizeof(RigidBody));
        RandomBytes(&W->infos[i], sizeof(RigidBodyInfo));
        W->bodies[i].info = &W->infos[i];
        W->bodies[i].mass = Uniform(100.0f, 3000.0f);
        W->bodies[i].kind = int8_t(Random(6));
        W->bodies[i].flags = Random();
        RandomVector(reinterpret_cast<Coord4 *>(&W->bodies[i].position), 30.0f);
        RandomMatrix(&W->infos[i].orientation);
        W->infos[i].halfExtents.z = Uniform(0.5f, 4.0f);
        W->owners[i] = uint8_t(Random(kCars));
    }
    RandomBytes(W->trigger, sizeof(W->trigger));
    RandomBytes(&W->missionManager, sizeof(W->missionManager));
    W->missionManager.missionSet = &W->missionSet;
    RandomBytes(&W->missionSet, sizeof(W->missionSet));

    RandomBytes(W->camera, sizeof(W->camera));
    RPlayerCamera *camera = reinterpret_cast<RPlayerCamera *>(W->camera);
    camera->vtable = reinterpret_cast<void **>(g_cameraVtable);
    RandomMatrix(&camera->matrix);
    RRenderHigh *high = reinterpret_cast<RRenderHigh *>(W->renderHigh);
    memset(high, 0, sizeof(*high));
    high->viewCount = 1 + Random(3);
    for (uint32_t v = 0; v < RRenderHigh::kMaxViews; v++) {
        high->views[v].view = reinterpret_cast<RViewCamera *>(W->viewCamera);
        high->views[v].camera = camera;
    }
    RandomBytes(W->viewCamera, sizeof(W->viewCamera));
    reinterpret_cast<RViewCamera *>(W->viewCamera)->camera = reinterpret_cast<RCamera *>(W->rcamera);
    RandomBytes(W->rcamera, sizeof(W->rcamera));
    RandomMatrix(&reinterpret_cast<RCamera *>(W->rcamera)->matrix);

    memset(W->world, 0, sizeof(W->world));
    reinterpret_cast<WWorld *>(W->world)->soundGroup = reinterpret_cast<WSoundGroup *>(W->soundGroup);
    RandomBytes(&W->enviro, sizeof(W->enviro));
    W->enviro.ambience[0] = Chance(4) ? 0 : 'a';
    W->enviro.ambience[1] = 0;
    W->enviro.level = Uniform(-10.0f, 10.0f);
    W->enviro.bandWidth = Chance(3) ? -Uniform(0.0f, 5.0f) : Uniform(0.0f, 10.0f);
    RandomBytes(&W->mix, sizeof(W->mix));
    W->mix.volume = Chance(3) ? 1.0f : Uniform(0.9f, 1.05f);

    RandomBytes(&W->instance, sizeof(W->instance));
    W->instance.articleDesc.value = Word(W->article);
    memset(W->article, 0, sizeof(W->article));
    reinterpret_cast<WorldArticle *>(W->article)->model = &W->model;
    memset(&W->model, 0, sizeof(W->model));
    W->model.info = &W->modelInfo;
    W->modelInfo.id = Chance(2) ? kUntrackedModel : int32_t(Random());
    RandomBytes(W->sceneObj, sizeof(W->sceneObj));
    RSceneObj *sceneObj = reinterpret_cast<RSceneObj *>(W->sceneObj);
    sceneObj->vtable = reinterpret_cast<RSceneObj_vtbl *>(g_sceneObjVtable);
    sceneObj->sourceInstance = &W->instance;
    sceneObj->animHandle = reinterpret_cast<Handle *>(W->animHandle);
    for (int i = 0; i < 2; i++) {
        AICommand *c = &W->commands[i];
        RandomBytes(c, sizeof(*c));
        // No NaN words: the original copies the command's floats on the x87, which quiets a signalling NaN
        uint32_t *words = reinterpret_cast<uint32_t *>(c);
        for (size_t w = 0; w < sizeof(*c) / 4; w++)
            if ((words[w] >> 23 & 0xff) == 0xff)
                words[w] ^= 0x40000000;
        RandomMatrix(&c->orientation);
        c->position = { Uniform(-50.0f, 50.0f), Uniform(-5.0f, 5.0f), Uniform(-50.0f, 50.0f) };
        c->pushForward = RandomFloat();
        c->pushSpin = Chance(2) ? 0.0f : RandomFloat();
        c->attackMode = uint8_t(Chance(3) ? 9 + Random(2) : Random(12));
        c->targetMode = uint8_t(Random(3));
        c->resetPosition = uint8_t(Random(2));
        c->unknown7e = int8_t(Random());
    }
    RandomBytes(W->spline, sizeof(W->spline));
    reinterpret_cast<CARP::AISpline *>(W->spline)->path.value = Chance(3) ? 0 : 0x1234;
    RandomBytes(&W->muzzle, sizeof(W->muzzle));
    RandomVector(&W->muzzle.position, 2.0f);
    RandomBytes(W->sound, sizeof(W->sound));
    reinterpret_cast<ABaseSound *>(W->sound)->vtable = Word(g_soundVtable);
    RandomBytes(&W->physics, sizeof(W->physics));
    W->physics.isSub = Chance(2) ? 0 : 1;
    RandomBytes(W->ioModule, sizeof(W->ioModule));
    for (int i = 0; i < 64; i++)
        W->physicsObjects[i] = Chance(4) ? NULL : &W->cars[i % kCars].car;
    W->magicCounter = int32_t(Random());
    W->okToSpawn = uint8_t(Random(2));
    for (int i = 0; i < 5; i++)
        W->lookupBools[i] = uint8_t(Random(2));
    W->sceneObjFound = uint8_t(Random(2));
    W->gfxFound = uint8_t(Random(2));
    W->playerGround = Chance(4) ? NULL : &W->vehicles[Random(kVehicles)];

    // the globals
    Globals &g = g_globals;
    memset(&g, 0, sizeof(g));
    g.simState = Chance(3) ? 3 : int32_t(Random(3));
    g.stepsPerSecond = Chance(2) ? 60 : 50;
    g.step = 100 + Random(200);
    g.henchLimit = uint8_t(Random(2));
    g.streams = uint8_t(Random(2));
    g.superEasy = uint8_t(Random(2));
    g.feedbackAllowed = 1;
    g.launch978 = int32_t(Random(4));
    g.maxTraffic = int16_t(Random(20));
    g.framesSinceAudio = int32_t(Random(10));
    g.lastIncidental = Random(400);
    g.lastPlayerIncidental = Random(400);
    static const char *const kChannels[] = { "speech", "music", "Speech", "MUSIC", "other" };
    strcpy(g.stinger, kChannels[Random(5)]);
    strcpy(g.tuningLevel, Chance(2) ? "mis07a" : "MIS07A_x");
    if (Chance(3))
        strcpy(g.tuningLevel, "paris");
    RandomBytes(&g.dynamic, sizeof(g.dynamic));
    g.dynamic.trigger = reinterpret_cast<WTrigger *>(W->trigger);
    g.dynamic.flag = uint8_t(Random(3));
}

// =============================================================================================================
// The cases
// =============================================================================================================

void *AnyRecord() {
    return Chance(4) ? NULL : &W->records[Random(kElements)];
}

// Runs `call(original)` on both sides from the same world, globals and event, and compares them
template <class Call>
void Both(const char *what, long index, size_t eventSize, const Call &call) {
    g_cases++;
    memcpy(g_initial, W, sizeof(FakeWorld));
    Globals initialGlobals = g_globals;
    std::string logOriginal, logPort;
    Globals globalsOriginal, globalsPort;
    static uint8_t eventOriginal[0x80];

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
    memcpy(eventOriginal, W->eventScratch, sizeof(eventOriginal));
    memcpy(g_after, W, sizeof(FakeWorld));

    memcpy(W, g_initial, sizeof(FakeWorld));
    WriteGlobals(initialGlobals);
    g_calls = &logPort;
    bool portFaulted = !Guarded([&] { call(false); });
    if (portFaulted)
        logPort += "<fault>";
    g_calls = NULL;
    ReadGlobals(&globalsPort, false);
    // A fault inside an original destructor unwinds through its exception funclet, which puts Event's vtable
    // back; the port (no unwinding of structured exceptions) leaves its own. Faults happen only on synthetic data.
    if (originalFaulted && portFaulted)
        memcpy(W->eventScratch, eventOriginal, 4);

    CheckBytes(what, index, "event", eventOriginal, W->eventScratch, eventSize);
    CheckBytes(what, index, "world", g_after, W, sizeof(FakeWorld));
    CheckBytes(what, index, "globals", &globalsOriginal, &globalsPort, sizeof(Globals));
    CheckLog(what, index, logOriginal, logPort);
    memcpy(W, g_initial, sizeof(FakeWorld));
}

// An event's scalar deleting destructor on the event `fill` builds in the scratch buffer
template <class T, class Fill>
void DeleteCase(const char *what, long index, uint32_t original, const Fill &fill) {
    memset(W->eventScratch, 0, sizeof(W->eventScratch));
    T *event = reinterpret_cast<T *>(W->eventScratch);
    fill(event);
    Both(what, index, sizeof(T), [&](bool runOriginal) {
        if (runOriginal)
            ((void *(__fastcall *)(void *, int, unsigned))original)(W->eventScratch, 0, 1);
        else
            reinterpret_cast<T *>(W->eventScratch)->Delete(1);
    });
}

// A MakeEvent callback on a random record
template <class Port>
void MakeCase(const char *what, long index, uint32_t original, const Port &port) {
    static uint32_t record[0x10];
    // No NaN words: the original copies the floats on the x87, which quiets a signalling NaN (the data has none)
    for (uint32_t &word : record) {
        word = Random() << 8 ^ Random();
        if ((word >> 23 & 0xff) == 0xff)
            word ^= 0x40000000;
    }
    // references among the record's words
    for (int i = 1; i < 6; i++)
        if (Chance(3))
            record[i] = Word(&W->records[Random(kElements)]);
    Both(what, index, 0x80, [&](bool runOriginal) {
        if (runOriginal)
            ((void (*)(const void *))original)(record);
        else
            port(record);
    });
}

constexpr int kRounds = 40;

void TestDeletes() {
    for (int round = 0; round < kRounds; round++) {
        BuildWorld();
        DeleteCase<E007Logo>("E007Logo", round, 0x000472c0, [](E007Logo *) {});
        DeleteCase<EAIUpdate>("EAIUpdate", round, 0x00047470, [](EAIUpdate *) {});
        DeleteCase<EAnimUpdate>("EAnimUpdate", round, 0x00047510, [](EAnimUpdate *) {});
        DeleteCase<EAudioUpdate>("EAudioUpdate", round, 0x0004eaa0, [](EAudioUpdate *) {});
        if (Chance(3)) {
            BuildWorld();
            void *savedHigh = fgRenderHighGame;
            fgRenderHighGame = NULL;
            DeleteCase<EAudioUpdate>("EAudioUpdate without the renderer", round, 0x0004eaa0, [](EAudioUpdate *) {});
            fgRenderHighGame = savedHigh;
        }
        DeleteCase<ECameraUpdate>("ECameraUpdate", round, 0x00047650, [](ECameraUpdate *) {});
        for (int i = 0; i < 6; i++) {
            BuildWorld();
            DeleteCase<EAICommand>("EAICommand", round * 6 + i, 0x0004ea60, [](EAICommand *e) {
                e->command = Chance(8) ? NULL : &W->commands[Random(2)];
                e->element = static_cast<CARP::AIElement *>(AnyRecord());
                e->target = static_cast<CARP::AIElement *>(AnyRecord());
                e->vehicle = Chance(2) ? NULL : &W->vehicles[Random(kVehicles)];
                e->trigger = Chance(3) ? NULL : reinterpret_cast<WTrigger *>(W->trigger);
                e->bySimpleBody = uint8_t(Random(2));
                e->bodySlot = int32_t(Random(kBodies));
                if (e->vehicle != NULL && Chance(4))
                    static_cast<AIGroundVehicle *>(e->vehicle)->command = e->command;
            });
        }
        BuildWorld();
        DeleteCase<EAIElementFireOff>("EAIElementFireOff", round, 0x00047390,
                                      [](EAIElementFireOff *e) { e->element = static_cast<CARP::AIElement *>(AnyRecord()); });
        DeleteCase<EAIElementFireOn>("EAIElementFireOn", round, 0x000473b0,
                                     [](EAIElementFireOn *e) { e->element = static_cast<CARP::AIElement *>(AnyRecord()); });
        DeleteCase<EAIElementSetAccuracy>("EAIElementSetAccuracy", round, 0x000473d0, [](EAIElementSetAccuracy *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
            e->accuracy = int32_t(Random());
        });
        DeleteCase<EAIElementSetCharParams>("EAIElementSetCharParams", round, 0x000473f0,
                                            [](EAIElementSetCharParams *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
            e->unknown08 = int32_t(Random());
            e->accuracy = int32_t(Random());
            e->unknown10 = RandomFloat();
        });
        DeleteCase<EAIElementSetNonScoreable>("EAIElementSetNonScoreable", round, 0x00047410,
                                              [](EAIElementSetNonScoreable *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
        });
        DeleteCase<EAIElementSetVehicleParams>("EAIElementSetVehicleParams", round, 0x00047430,
                                               [](EAIElementSetVehicleParams *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
            e->unknown08 = RandomFloat();
        });
        DeleteCase<EAIElementSetWakeRange>("EAIElementSetWakeRange", round, 0x00047450, [](EAIElementSetWakeRange *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
            e->wakeRange = RandomFloat();
        });
        for (int i = 0; i < 4; i++) {
            BuildWorld();
            DeleteCase<EActivateAIElement>("EActivateAIElement", round * 4 + i, 0x0004ea80, [](EActivateAIElement *e) {
                e->element = static_cast<CARP::AIElement *>(AnyRecord());
                e->toucherElement = static_cast<CARP::AIElement *>(AnyRecord());
                e->trigger = Chance(3) ? NULL : reinterpret_cast<WTrigger *>(W->trigger);
                e->bySimpleBody = uint8_t(Random(2));
                e->bodySlot = int32_t(Random(kBodies));
            });
        }
        DeleteCase<EDeactivateAIElement>("EDeactivateAIElement", round, 0x00047930, [](EDeactivateAIElement *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
        });
        DeleteCase<EDropMine>("EDropMine", round, 0x000479d0, [](EDropMine *e) {
            e->element = &W->records[Random(kElements)];
            e->unknown08 = Random();
        });
        DeleteCase<EActorMuzzleFlash>("EActorMuzzleFlash", round, 0x000474b0, [](EActorMuzzleFlash *e) {
            e->point = Chance(4) ? NULL : &W->muzzle;
            e->weapon = reinterpret_cast<ActWeapon *>(W->weapon);
            e->sceneObj = Chance(4) ? NULL : reinterpret_cast<RSceneObj *>(W->sceneObj);
        });

        BuildWorld();
        DeleteCase<EAutoDrive>("EAutoDrive", round, 0x00047530, [](EAutoDrive *e) { e->on = int32_t(Random(3)); });
        DeleteCase<EAutoDriveSpeedChange>("EAutoDriveSpeedChange", round, 0x0004eac0,
                                          [](EAutoDriveSpeedChange *e) { e->speed = RandomFloat(); });
        DeleteCase<EAutoDriveSteering>("EAutoDriveSteering", round, 0x0004eae0, [](EAutoDriveSteering *e) {
            e->on = int32_t(Random(2));
            if (Chance(3))
                AIHandsGame = NULL;
        });
        AIHandsGame = W->hands;
        DeleteCase<EBashCarAlongObjectAxis>("EBashCarAlongObjectAxis", round, 0x0004eb00,
                                            [](EBashCarAlongObjectAxis *e) {
            e->sceneObj = reinterpret_cast<RSceneObj *>(W->sceneObj);
            e->damage = RandomFloat();
            W->physicsObjects[W->cars[0].car.rigidBodySlot] = &W->cars[0].car;
        });
        DeleteCase<EControlToCPU>("EControlToCPU", round, 0x0004eb60, [](EControlToCPU *) {});
        DeleteCase<EControlToPlayer>("EControlToPlayer", round, 0x0004eb80, [](EControlToPlayer *) {});
        DeleteCase<EControlToSpline>("EControlToSpline", round, 0x0004eba0, [](EControlToSpline *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
            e->spline = Chance(4) ? NULL : reinterpret_cast<CARP::AISpline *>(W->spline);
        });
        DeleteCase<EEndControlToSpline>("EEndControlToSpline", round, 0x0004ec20, [](EEndControlToSpline *e) {
            e->element = Chance(2) ? NULL : &W->records[Random(kElements)];
            if (e->element != NULL)
                W->elements[e->element - W->records].vehicle = &W->vehicles[0];
        });
        DeleteCase<EEndCarStop>("EEndCarStop", round, 0x0004ec00, [](EEndCarStop *) {});
        DeleteCase<EDamagePlayer>("EDamagePlayer", round, 0x0004ebe0, [](EDamagePlayer *e) {
            e->damage = RandomFloat();
            e->unknown08 = RandomFloat();
        });
        DeleteCase<ECustomTransition>("ECustomTransition", round, 0x0004ebc0, [](ECustomTransition *e) {
            static const char *const kNames[] = { "paris", "PARIS", "berlin" };
            e->transition = kNames[Random(3)];
        });

        for (int i = 0; i < 8; i++) {
            BuildWorld();
            DeleteCase<ECollision>("ECollision", round * 8 + i, 0x0004eb40, [](ECollision *e) {
                CollisionImpact &c = e->impact;
                RandomBytes(&c, sizeof(c));
                RandomVector(&c.point, 25.0f);
                RandomVector(&c.normal, 1.0f);
                RandomVector(&c.relativeVelocity, Chance(2) ? 1.0f : 10.0f);
                c.strength = Chance(3) ? Uniform(0.0f, 0.12f) : Uniform(-1.0f, 1.0f);
                c.kindA = uint8_t(Random(9));
                c.kindB = uint8_t(Random(9));
                c.ownerA = int8_t(Random(6));
                c.ownerB = int8_t(Random(6));
                static const uint16_t kTags[] = { 0, 1, 2, 5, 6, 10, 12, 13, 14, 16, 48, 64, 80, 96, 0x30, 0x40, 7 };
                c.tag = kTags[Random(sizeof(kTags) / sizeof(kTags[0]))];
                c.unknown4a = kTags[Random(sizeof(kTags) / sizeof(kTags[0]))];
                c.unknown50 = int32_t(Random(10)) - 2;
                if (Chance(2)) {
                    // near the camera
                    const RPlayerCamera *camera = reinterpret_cast<const RPlayerCamera *>(W->camera);
                    c.point.x = camera->matrix.mtx[3][0] + Uniform(-10.0f, 10.0f);
                    c.point.y = camera->matrix.mtx[3][1] + Uniform(-10.0f, 10.0f);
                    c.point.z = camera->matrix.mtx[3][2] + Uniform(-10.0f, 10.0f);
                }
                if (Chance(2))
                    g_globals.step = g_globals.lastIncidental + Random(60);
            });
        }

        BuildWorld();
        DeleteCase<EAbortCinematic>("EAbortCinematic", round, 0x00047490, [](EAbortCinematic *) {});
        DeleteCase<ECameraLockOn>("ECameraLockOn", round, 0x00047610, [](ECameraLockOn *e) {
            e->element = static_cast<CARP::AIElement *>(AnyRecord());
            e->point = Chance(3) ? &W->vehiclePositions[0] : NULL;
            e->wheel = int32_t(Random(7)) - 1;
            e->member = int32_t(Random(6)) - 1;
            e->targetY = RandomFloat();
            e->targetZ = RandomFloat();
            e->targetX = RandomFloat();
            e->param = int32_t(Random(4));
            e->quiet = int32_t(Random(2));
        });
        DeleteCase<ECameraShake>("ECameraShake", round, 0x00047630, [](ECameraShake *e) {
            e->duration = int32_t(Random(100));
            e->range = Chance(4) ? 0x80000000u + Random() : Random(1000);
            e->keepOrigin = int32_t(Random(2));
        });
        DeleteCase<ECameraZoom>("ECameraZoom", round, 0x00047670, [](ECameraZoom *e) {
            e->percent = Random(200);
            e->time = Chance(3) ? Random(3) : (Chance(4) ? 0x90000000u + Random() : Random(1000));
        });
        DeleteCase<EChangeCarCameraView>("EChangeCarCameraView", round, 0x00047690, [](EChangeCarCameraView *e) {
            e->id = Random(20);
            e->b = Chance(3) ? Random(3) : Random(1000);
            e->delay = Chance(3) ? Random(3) : (Chance(4) ? 0x90000000u + Random() : Random(1000));
            e->unused = int32_t(Random());
        });
        DeleteCase<ECinematicCamera>("ECinematicCamera", round, 0x0004eb20, [](ECinematicCamera *e) {
            e->cinematic = int32_t(Random(2));
            e->unknown08 = Chance(3) ? int32_t(Random(3)) - 1 : int32_t(Random(1000)) - 100;
            e->delay = Chance(3) ? int32_t(Random(3)) : int32_t(Random(1000)) - 100;
            e->anchor = Chance(3) ? NULL : &W->elements[Random(kElements)];
            e->place = { RandomFloat(), RandomFloat(), RandomFloat() };
        });
        DeleteCase<EEndCameraAnim>("EEndCameraAnim", round, 0x00047a30, [](EEndCameraAnim *e) {
            e->b = Chance(3) ? Random(3) : Random(1000);
            e->delay = Chance(3) ? Random(3) : Random(70000);
            e->c = int32_t(Random(3));
            e->id = Random(20);
        });

        BuildWorld();
        DeleteCase<EDisablePowerUp>("EDisablePowerUp", round, 0x00047950, [](EDisablePowerUp *e) {
            e->instance = Chance(4) ? NULL : &W->instance;
            e->trigger = Chance(4) ? NULL : reinterpret_cast<WTrigger *>(W->trigger);
        });
        DeleteCase<EEnablePowerUp>("EEnablePowerUp", round, 0x000479f0, [](EEnablePowerUp *e) {
            e->instance = Chance(4) ? NULL : &W->instance;
            e->trigger = Chance(4) ? NULL : reinterpret_cast<WTrigger *>(W->trigger);
        });
        DeleteCase<EDisableTrigger>("EDisableTrigger", round, 0x00047970, [](EDisableTrigger *e) {
            e->trigger = Chance(4) ? NULL : reinterpret_cast<WTrigger *>(W->trigger);
        });
        DeleteCase<EEnableTrigger>("EEnableTrigger", round, 0x00047a10, [](EEnableTrigger *e) {
            e->trigger = Chance(4) ? NULL : reinterpret_cast<WTrigger *>(W->trigger);
        });

        BuildWorld();
        DeleteCase<EAddObjective>("EAddObjective", round, 0x000474d0,
                                  [](EAddObjective *e) { RandomBytes(&e->unknown04, 16); });
        DeleteCase<EAddScoreAction>("EAddScoreAction", round, 0x000474f0,
                                    [](EAddScoreAction *e) { e->points = int32_t(Random(1000)); });
        DeleteCase<EAwardHitToPlayer>("EAwardHitToPlayer", round, 0x00047550,
                                      [](EAwardHitToPlayer *e) { e->unknown04 = int32_t(Random()); });
        DeleteCase<EAwardKillToPlayer>("EAwardKillToPlayer", round, 0x00047570,
                                       [](EAwardKillToPlayer *e) { e->kills = int32_t(Random(3)); });
        DeleteCase<EBeginDownloadCode>("EBeginDownloadCode", round, 0x00048680,
                                       [](EBeginDownloadCode *e) { e->unknown04 = int32_t(Random()); });
        DeleteCase<EButtonMsgOff>("EButtonMsgOff", round, 0x00047590, [](EButtonMsgOff *) {});
        DeleteCase<EButtonMsgOn>("EButtonMsgOn", round, 0x000475b0, [](EButtonMsgOn *e) {
            e->message = int32_t(Random(100));
            e->time = RandomFloat();
            e->unknown0c = int32_t(Random());
            e->unknown10 = int32_t(Random());
            e->unknown14 = int32_t(Random());
        });
        DeleteCase<ECall911>("ECall911", round, 0x000475d0, [](ECall911 *) {});
        DeleteCase<ECallStage>("ECallStage", round, 0x000475f0, [](ECallStage *e) { e->stage = int32_t(Random(10)); });
        DeleteCase<EClearBitMagicCounter>("EClearBitMagicCounter", round, 0x000476b0, [](EClearBitMagicCounter *e) {
            e->counter = int32_t(Random(64));
            e->bits = Random();
        });
        DeleteCase<EClearProgrammerEvent>("EClearProgrammerEvent", round, 0x000476d0,
                                          [](EClearProgrammerEvent *e) { e->event = int32_t(Random(64)); });
        DeleteCase<EDisplayObjectiveText>("EDisplayObjectiveText", round, 0x00047990,
                                          [](EDisplayObjectiveText *e) { RandomBytes(&e->unknown04, 12); });
        DeleteCase<EDisplayText>("EDisplayText", round, 0x000479b0, [](EDisplayText *e) {
            e->message = int32_t(Random(100));
            e->time = RandomFloat();
        });
        DeleteCase<EEndMission>("EEndMission", round, 0x00047a50, [](EEndMission *) {});
    }
}

void TestMakes() {
    for (int round = 0; round < kRounds; round++) {
        BuildWorld();
#define MAKE(name, address) \
        MakeCase(#name "_MakeEvent_Callback", round, address, [](const void *record) { \
            name##_MakeEvent_Callback(static_cast<const name::Record *>(record)); })
#define MAKE0(name, address) \
        MakeCase(#name "_MakeEvent_Callback", round, address, [](const void *record) { \
            name##_MakeEvent_Callback(record); })
        MAKE0(E007Logo, 0x0003dc00);
        MAKE(EAICommand, 0x0003dd10);
        MAKE(EAIElementFireOff, 0x0003de10);
        MAKE(EAIElementFireOn, 0x0003ded0);
        MAKE(EAIElementSetAccuracy, 0x0003df80);
        MAKE(EAIElementSetCharParams, 0x0003e060);
        MAKE(EAIElementSetNonScoreable, 0x0003e160);
        MAKE(EAIElementSetVehicleParams, 0x0003e1e0);
        MAKE(EAIElementSetWakeRange, 0x0003e280);
        MAKE0(EAIUpdate, 0x0003e320);
        MAKE0(EAbortCinematic, 0x0003e390);
        MAKE(EActivateAIElement, 0x0003e3b0);
        MAKE(EActorMuzzleFlash, 0x0003e4a0);
        MAKE(EAddObjective, 0x0003e4e0);
        MAKE(EAddScoreAction, 0x0003e5e0);
        MAKE(EAddScoreObjective, 0x0003e600);
        MAKE0(EAnimUpdate, 0x0003e690);
        MAKE0(EAudioUpdate, 0x0003e6d0);
        MAKE(EAutoDrive, 0x0003e7a0);
        MAKE(EAutoDriveSpeedChange, 0x0003e7c0);
        MAKE(EAutoDriveSteering, 0x0003e7e0);
        MAKE(EAwardHitToPlayer, 0x0003e850);
        MAKE(EAwardKillToPlayer, 0x0003e8d0);
        MAKE(EBashCarAlongObjectAxis, 0x0003e8f0);
        MAKE(EBeginDownloadCode, 0x0003e920);
        MAKE0(EButtonMsgOff, 0x0003e990);
        MAKE(EButtonMsgOn, 0x0003ea20);
        MAKE0(ECall911, 0x0003eac0);
        MAKE(ECallStage, 0x0003eb30);
        MAKE(ECameraShake, 0x0003ed40);
        MAKE0(ECameraUpdate, 0x0003ede0);
        MAKE(ECameraZoom, 0x0003eee0);
        MAKE(EChangeCarCameraView, 0x0003f020);
        MAKE(EChangeMaxTraffic, 0x0003f0c0);
        MAKE(EChangeStage, 0x0003f170);
        MAKE(ECinematicCamera, 0x0003f1c0);
        MAKE(EClearBitMagicCounter, 0x0003f2d0);
        MAKE(EClearProgrammerEvent, 0x0003f350);
        MAKE0(EControlToCPU, 0x0003f390);
        MAKE0(EControlToPlayer, 0x0003f3b0);
        MAKE(EControlToSpline, 0x0003f3d0);
        MAKE(ECustomTransition, 0x0003f420);
        MAKE(EDamagePlayer, 0x0003f440);
        MAKE(EDeactivateAIElement, 0x0003f4e0);
        MAKE(EDisablePowerUp, 0x0003f5e0);
        MAKE(EDisableTrigger, 0x0003f640);
        MAKE(EDisplayObjectiveText, 0x0003f660);
        MAKE(EDisplayText, 0x0003f6f0);
        MAKE(EDropMine, 0x0003f7c0);
        MAKE(EEnablePowerUp, 0x0003f890);
        MAKE(EEnableTrigger, 0x0003f8e0);
        MAKE(EEndCameraAnim, 0x0003fa30);
        MAKE0(EEndCarStop, 0x0003fa70);
        MAKE(EEndControlToSpline, 0x0003fa90);
        MAKE0(EEndMission, 0x0003fb50);
#undef MAKE
#undef MAKE0
    }
}

void TestResolvesAndConstructors() {
    for (int round = 0; round < kRounds; round++) {
        BuildWorld();
        static uint8_t record[0x40];
        UGroup *shared = reinterpret_cast<UGroup *>(W->tracking);
        UGroup *parent = reinterpret_cast<UGroup *>(W->hud);
        struct Resolver {
            const char *name;
            uint32_t original;
            void (*port)(void *, UGroup *, UGroup *);
        };
        static const Resolver kResolvers[] = {
            { "EAICommand_ResolveEvent_Callback", 0x000472e0, [](void *r, UGroup *s, UGroup *p) {
                  EAICommand_ResolveEvent_Callback(static_cast<EAICommand::Record *>(r), s, p); } },
            { "ECinematicCamera_ResolveEvent_Callback", 0x0003f210, [](void *r, UGroup *s, UGroup *p) {
                  ECinematicCamera_ResolveEvent_Callback(static_cast<ECinematicCamera::Record *>(r), s, p); } },
            { "EDeactivateAIElement_ResolveEvent_Callback", 0x0003f500, [](void *r, UGroup *s, UGroup *p) {
                  EDeactivateAIElement_ResolveEvent_Callback(static_cast<EDeactivateAIElement::Record *>(r), s, p); } },
        };
        for (const Resolver &resolver : kResolvers) {
            RandomBytes(record, sizeof(record));
            static uint8_t initial[0x40];
            memcpy(initial, record, sizeof(record));
            static uint8_t after[0x40];
            Both(resolver.name, round, 0, [&](bool original) {
                memcpy(record, initial, sizeof(record));
                if (original)
                    ((void (*)(void *, UGroup *, UGroup *))resolver.original)(record, shared, parent);
                else
                    resolver.port(record, shared, parent);
                if (original)
                    memcpy(after, record, sizeof(record));
            });
            CheckBytes(resolver.name, round, "record", after, record, sizeof(record));
        }

        // The constructors
        uint32_t words[7];
        RandomBytes(words, sizeof(words));
        Both("EAICommand::Construct", round, sizeof(EAICommand), [&](bool original) {
            memset(W->eventScratch, 0, sizeof(W->eventScratch));
            if (original)
                ((void *(__fastcall *)(void *, int, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                                       uint32_t))0x0003dcd0)(W->eventScratch, 0, words[0], words[1], words[2],
                                                             words[3], words[4], words[5], words[6]);
            else
                reinterpret_cast<EAICommand *>(W->eventScratch)
                    ->Construct((AICommand *)uintptr_t(words[0]), (CARP::AIElement *)uintptr_t(words[1]),
                                (CARP::AIElement *)uintptr_t(words[2]), (AIVehicle *)uintptr_t(words[3]),
                                (WTrigger *)uintptr_t(words[4]), uint8_t(words[5]), int(words[6]));
        });
        CollisionImpact impact;
        RandomBytes(&impact, sizeof(impact));
        Both("ECollision::Construct", round, sizeof(ECollision), [&](bool original) {
            memset(W->eventScratch, 0, sizeof(W->eventScratch));
            if (original)
                ((void *(__fastcall *)(void *, int, CollisionImpact))0x0003f370)(W->eventScratch, 0, impact);
            else
                reinterpret_cast<ECollision *>(W->eventScratch)->Construct(impact);
        });
        int value = int(Random());
        Both("EChangeMaxTraffic::Construct", round, sizeof(EChangeMaxTraffic), [&](bool original) {
            memset(W->eventScratch, 0, sizeof(W->eventScratch));
            if (original)
                ((void *(__fastcall *)(void *, int, int))0x0003f060)(W->eventScratch, 0, value);
            else
                reinterpret_cast<EChangeMaxTraffic *>(W->eventScratch)->Construct(value);
        });
        Both("EChangeStage::Construct", round, sizeof(EChangeStage), [&](bool original) {
            memset(W->eventScratch, 0, sizeof(W->eventScratch));
            if (original)
                ((void *(__fastcall *)(void *, int, int))0x0003f110)(W->eventScratch, 0, value);
            else
                reinterpret_cast<EChangeStage *>(W->eventScratch)->Construct(value);
        });
    }
}

void TestOthers() {
    for (int round = 0; round < kRounds; round++) {
        BuildWorld();
        RandomBytes(W->eventScratch, sizeof(W->eventScratch));
        Both("ConstructCollisionImpact", round, sizeof(CollisionImpact), [&](bool original) {
            if (original)
                ((void *(__fastcall *)(void *, int))0x0003dc20)(W->eventScratch, 0);
            else
                reinterpret_cast<CollisionImpact *>(W->eventScratch)->Construct();
        });
        // AVoice::Destruct has the views' destructor inlined: the real one runs on both sides, on views out of the
        // active list and without an SND voice, so that it changes nothing
        RandomBytes(W->eventScratch, sizeof(W->eventScratch));
        for (AVoice::View &view : reinterpret_cast<AVoice *>(W->eventScratch)->views) {
            view.listedVolume = 0;
            view.handle = -1;
        }
        {
            FakeLifted viewDestruct(0x00123c50);
            Both("DestructVoice", round, sizeof(AVoice), [&](bool original) {
                if (original)
                    ((void (__fastcall *)(void *, int))0x0003f400)(W->eventScratch, 0);
                else
                    reinterpret_cast<AVoice *>(W->eventScratch)->Destruct();
            });
        }
        Both("PlayVoiceOnce", round, sizeof(AVoice), [&](bool original) {
            if (original)
                ((void (__fastcall *)(void *, int))0x0003e6b0)(W->eventScratch, 0);
            else
                reinterpret_cast<AVoice *>(W->eventScratch)->PlayOnce();
        });

        // The one-shot sounds, in the world's sound buffer (0x120 bytes needed: the scratch is too small)
        static uint8_t sound[0x120];
        static uint8_t soundInitial[0x120];
        static uint8_t soundAfter[0x120];
        RandomBytes(soundInitial, sizeof(soundInitial));
        Coord4 position, velocity;
        RandomVector(&position, 20.0f);
        RandomVector(&velocity, 5.0f);
        int bank = int(Random(8)), patch = int(Random(40));
        float volume = RandomFloat();
        bool loop = Chance(2);
        Both("ConstructOneShotSound", round, 0, [&](bool original) {
            memcpy(sound, soundInitial, sizeof(sound));
            if (original)
                ((void *(__fastcall *)(void *, int, int, int, const void *, const void *, float, const char *))0x000476f0)(
                    sound, 0, bank, patch, &position, &velocity, volume, "mixname");
            else
                reinterpret_cast<AOneShotSound *>(sound)->Construct(bank, patch, Xyz(&position), Xyz(&velocity), volume,
                                                                    "mixname");
            if (original)
                memcpy(soundAfter, sound, sizeof(sound));
        });
        CheckBytes("ConstructOneShotSound", round, "sound", soundAfter, sound, sizeof(sound));
        Both("ConstructOneShotSoundAt", round, 0, [&](bool original) {
            memcpy(sound, soundInitial, sizeof(sound));
            if (original)
                ((void *(__fastcall *)(void *, int, int, int, const void *, float, const char *, bool))0x000477d0)(
                    sound, 0, bank, patch, &position, volume, "mixname", loop);
            else
                reinterpret_cast<AOneShotSound *>(sound)->ConstructAt(bank, patch, Xyz(&position), volume, "mixname",
                                                                      loop);
            if (original)
                memcpy(soundAfter, sound, sizeof(sound));
        });
        CheckBytes("ConstructOneShotSoundAt", round, "sound", soundAfter, sound, sizeof(sound));

        uint32_t seed = Random(), multiplier = Random() | 1;
        uint32_t answers[2], seeds[2];
        for (int side = 0; side < 2; side++) {
            RandomSeedGame = seed;
            RandomMultiplierGame = multiplier;
            if (side == 0) {
                Originals window;
                answers[0] = ((uint32_t (*)())0x0003e6f0)();
            } else {
                answers[1] = RandomWord();
            }
            seeds[side] = RandomSeedGame;
        }
        g_cases++;
        CheckBytes("RandomWord", round, "answer", &answers[0], &answers[1], 4);
        CheckBytes("RandomWord", round, "seed", &seeds[0], &seeds[1], 4);
    }
}

} // namespace

void GameEventsShadowA_Run(void) {
    const char *env = getenv("NIGHTFIRE_GAMEEVENTSSHADOWA");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    W = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    g_initial = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    g_after = static_cast<FakeWorld *>(VirtualAlloc(NULL, sizeof(FakeWorld), MEM_COMMIT, PAGE_READWRITE));
    if (W == NULL || g_initial == NULL || g_after == NULL) {
        printf("[gameeventsshadowa] no memory for the world\n");
        fflush(stdout);
        return;
    }

    // The live state the cases replace, put back after
    Globals live;
    ReadGlobals(&live, true);
    char liveStinger[sizeof(StingerChannel)];
    memcpy(liveStinger, StingerChannel, sizeof(liveStinger));
    uint32_t liveOurs[2] = { LastIncidentalStep, LastPlayerIncidentalStep };
    void *liveController = fgAIElementControllerGame, *liveMission = glbMissionManagerGame;
    void *liveHigh = fgRenderHighGame, *liveWorld = fgWorldGame, *liveHands = AIHandsGame;
    GameVector<PVehicle *> liveCars = SimCarsGame;
    static PhysicsObject *livePhysics[64];
    memcpy(livePhysics, PhysicsObjectsGame, sizeof(livePhysics));
    uint32_t liveSeed = RandomSeedGame, liveMultiplier = RandomMultiplierGame;

    BuildVtables();
    BuildWorld();
    fgAIElementControllerGame = W->elementController;
    glbMissionManagerGame = &W->missionManager;
    fgRenderHighGame = W->renderHigh;
    fgWorldGame = W->world;
    AIHandsGame = W->hands;
    SimCarsGame.first = W->carVector;
    SimCarsGame.last = W->carVector + kCars;
    SimCarsGame.end = W->carVector + kCars;
    memcpy(PhysicsObjectsGame, W->physicsObjects, sizeof(W->physicsObjects));

    InstallFakes();
    TestDeletes();
    TestMakes();
    TestResolvesAndConstructors();
    TestOthers();
    RemoveFakes();

    fgAIElementControllerGame = liveController;
    glbMissionManagerGame = liveMission;
    fgRenderHighGame = liveHigh;
    fgWorldGame = liveWorld;
    AIHandsGame = liveHands;
    SimCarsGame = liveCars;
    WriteGlobals(live);
    memcpy(StingerChannel, liveStinger, sizeof(liveStinger));
    memcpy(PhysicsObjectsGame, livePhysics, sizeof(livePhysics));
    LastIncidentalStep = liveOurs[0];
    LastPlayerIncidentalStep = liveOurs[1];
    RandomSeedGame = liveSeed;
    RandomMultiplierGame = liveMultiplier;
    VirtualFree(W, 0, MEM_RELEASE);
    VirtualFree(g_initial, 0, MEM_RELEASE);
    VirtualFree(g_after, 0, MEM_RELEASE);

    for (const auto &d : g_differBy)
        printf("[gameeventsshadowa]   %s: %ld differ\n", d.first.c_str(), d.second);
    printf("[gameeventsshadowa] the events E007Logo..EEndMission: %ld cases, %ld checks, %ld differ (%ld faults)\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
