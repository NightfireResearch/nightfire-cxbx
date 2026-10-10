#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "BondCarShadowC.h"
#include "FpControl.h"

#include "../EventManager.hpp"
#include "../anim/AnimEngine.h"         // Handle
#include "../audio/SoundManager.h"      // fgSoundList
#include "../camera/PlayerCamera.h"     // RPlayerCamera
#include "../engine/ActionQueue.hpp"
#include "../engine/GameInterfaces.hpp" // GHud
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../game/BondCar.h"
#include "../game/BondCarPhysics.h"
#include "../game/BondCarState.h"       // the BondCar_* tuning
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../render/RenderHigh.h"       // fgRenderHigh
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"       // fgRoadNetworkData
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <bit>
#include <type_traits>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_BONDCARSHADOWC=1, from the first simulation tick: package V_C's PBondCar methods against the originals
// (the entry bytes of all 26 ported functions swapped back in for each original run, so an original reaches the
// other originals), on every live PBondCar in place. Before each case the state a call can write is taken - the
// car, its rigid body and body info, its CarPhysics record, the first 0x180 bytes of its audio object, its four
// tyre tracks, Simulate's scratch pad (pointed at a buffer of this file's for the whole test), its AI's first
// 0x84 bytes and road navigator, the player's camera and action queue, its scene object's animation handle and the
// animation engine's active list, the renderer's random seed, the collision manager's barrier mask and the event
// queue's pointers - and it is put back after each run and at the end. The scene object is replaced by a copy
// whose vtable records UpdatePosition and TriggerFX (their vectors by value); InitTyreTracks,
// InitializeCarVariables, ImproveLanding and AMenuSound::Trigger (DisableTwoWheelStunt's tyre sound, reached from
// TwoWheelStunt) are redirected to recorders (at the original's address and at our entry, which our code calls
// directly). Events raised are compared by their bytes in the queue, then dropped. Any sound a run still adds to
// fgSoundList is counted, compared and deleted through its own vtable: left there it plays on the next audio
// frames and holds a DirectSound buffer (before the menu sound was redirected, 21 cars left 850 of them and the
// sound driver ran out of buffers).
//
//   - The 15 accessors with the car's fields filled at random.
//   - SetVisualDamage with ranges inside, outside and reversed, from random seeds.
//   - ResetCar (with fresh tyre-track blocks for it to free) with the car's class, orientation and its AI's road
//     state perturbed.
//   - GetControllerInput (the player's car) with 0-8 random actions queued, the forced stop flags, the inputs,
//     wheels on the ground, speed and tuning perturbed.
//   - TwoWheelStunt with the roll, spin, speed, drive state and the TWO_WHEEL_* tuning perturbed.
//   - ControlTyreTracks with the surfaces, compression, slip, shredded tyres and spin perturbed.
//   - AddWheelForces and AddSimpleWheelForces on random wheel inputs, ground normals, compressions, surfaces,
//     gears, classes and tuning.
//   - ProcessPhysics and ProcessSimplePhysics with the controls, timers, boost (around its end, with an audio
//     object), hand brake, landing state, class, body velocities and orientation perturbed.
// ChangeCarType and the deleting destructor are not run here (they replace and free the car's objects): in game.
//
// One mutation this catches: ProcessPhysics' wheel drive scaled by the surface's friction from the drive rounded to
// a float (in.drive * friction) instead of the unrounded drive x grip changes the drive's last bit in the scratch
// pad's wheel inputs on most ProcessPhysics cases.
// ---------------------------------------------------------------------------------------------------------------

namespace {

const uint32_t kPorted[] = {
    0x00066060, 0x00066070, 0x00066080, 0x00066090, 0x000660a0, 0x000660b0, 0x000660c0, 0x000660d0, 0x000660e0,
    0x000660f0, 0x00066110, 0x00066130, 0x00066140, 0x00066160, 0x00066170, 0x00066180, 0x00066390, 0x00066640,
    0x00066c90, 0x00066ee0, 0x000670e0, 0x00067bd0, 0x000690a0, 0x00069710, 0x0006a460, 0x0006a810,
};
const uint32_t kBondCarVtable = 0x0018f580;
const int kOwners = 0x40;
const size_t kAudioBytes = 0x180;
const size_t kTyreTrackBytes = 0x920;
const size_t kAIBytes = 0x84;
const size_t kRenderBytes = 0x370;      // sizeof(RVehicle)
const size_t kHandleBytes = 0x10000;    // the most of a handle (header and data) the state covers
const int kVtableSlots = 32;
const int kMaxRecords = 32;
const size_t kEventBytes = 0x60;

const uint32_t kInitTyreTracks = 0x00062120;
const uint32_t kInitializeCarVariables = 0x00062440;
const uint32_t kImproveLanding = 0x00062fe0;
const uint32_t kMenuSoundTrigger = 0x0011e010;     // AMenuSound::Trigger(bank number, patch name, mix name, view)
const size_t kNameBytes = 16;

typedef void (__fastcall *CarFn)(PBondCar *, int);
typedef void (__fastcall *FlagFn)(PBondCar *, int, int);
typedef void (__fastcall *TwoFlagFn)(PBondCar *, int, int, int);
typedef void *(__fastcall *PtrGetFn)(PBondCar *, int);
typedef uint8_t (__fastcall *ByteGetFn)(PBondCar *, int);
typedef float (__fastcall *FloatIndexFn)(PBondCar *, int, int);
typedef void *(__fastcall *PtrIndexFn)(PBondCar *, int, int);
typedef void (__fastcall *DamageFn)(PBondCar *, int, float, float, float, float);
typedef int (__fastcall *WheelForcesFn)(PBondCar *, int, BondCarWheelInput *, float *, const Coord4 *,
                                        const Coord4 *, const Coord4 *, const Coord4 *, int);
typedef int (__fastcall *SimpleWheelForcesFn)(PBondCar *, int, BondCarWheelInput *, const Coord4 *, const Coord4 *,
                                              const Coord4 *, float *);

#define ShadowSim ((void *)0x00233ff0)
#define ShadowSimRandom (*(SimRandom **)0x00233ff0)          // the Simulation's first word
#define ShadowRandomSeed U32_AT(0x001c45c4)
#define ShadowActiveSystems ((void **)0x001eb878)          // [0x100]
#define ShadowActiveSystemCount U32_AT(0x001ebc78)
#define ShadowCreationPoint (*(uint8_t **)0x001e47d8)
#define ShadowDeletionPoint (*(uint8_t **)0x001e47dc)

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0, g_skipped = 0, g_soundsDeleted = 0;
unsigned int g_x87 = 0, g_sse = 0;

void Differ(const char *what, int index, const char *detail) {
    g_differ++;
    if (g_details++ < 10) {
        printf("[bondcarC]   %s #%d: %s\n", what, index, detail);
        fflush(stdout);
    }
}

// Per function: its cases, those with a difference, and its faulted runs (original, port)
struct Kind {
    const char *name;
    int cases, differing, faults[2], sounds;
};
Kind g_kinds[32];
int g_kindCount = 0;
Kind *g_openKind = NULL;
int g_openDiffer = 0;
int g_runFaults[2] = { 0, 0 };

void EndCase() {
    if (g_openKind != NULL && g_differ != g_openDiffer)
        g_openKind->differing++;
    g_openKind = NULL;
}

void BeginCase(const char *what) {
    EndCase();
    Kind *kind = NULL;
    for (int i = 0; i < g_kindCount; i++)
        if (strcmp(g_kinds[i].name, what) == 0)
            kind = &g_kinds[i];
    if (kind == NULL && g_kindCount < int(sizeof(g_kinds) / sizeof(g_kinds[0]))) {
        kind = &g_kinds[g_kindCount++];
        *kind = { what, 0, 0, { 0, 0 }, 0 };
    }
    if (kind == NULL)
        return;
    kind->cases++;
    kind->faults[0] += g_runFaults[0];
    kind->faults[1] += g_runFaults[1];
    g_openKind = kind;
    g_openDiffer = g_differ;
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    char detail[96];
    snprintf(detail, sizeof(detail), "byte 0x%x of 0x%x: original %02x, port %02x", unsigned(at), unsigned(bytes),
             x[at], y[at]);
    Differ(what, index, detail);
}

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

typedef void (*CaseFn)(void *context, bool original);

bool Guarded(CaseFn run, void *context, bool original) {
#ifdef _MSC_VER
    __try {
        run(context, original);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        return false;
    }
#else
    run(context, original);
    return true;
#endif
}

void RestoreOriginals(bool original) {
    for (uint32_t at : kPorted)
        XbeOriginal_Restore(at, original);
}

// ---- recorders: the scene object's two virtual methods and the other packages' calls

struct Record {
    uint32_t kind;          // 1 UpdatePosition, 2 TriggerFX, 3 InitTyreTracks, 4 InitializeCarVariables,
                            // 5 ImproveLanding, 6 AMenuSound::Trigger
    uint32_t words[3];
    float point[4];
    float velocity[4];
    float intensity;
    char names[2][kNameBytes];
};

struct Log {
    int count;
    Record records[kMaxRecords];
};
Log g_log;

Record *NewRecord(uint32_t kind) {
    if (g_log.count >= kMaxRecords)
        return NULL;
    Record *record = &g_log.records[g_log.count++];
    memset(record, 0, sizeof(*record));
    record->kind = kind;
    return record;
}

void __fastcall RecordUpdatePosition(void *, int, int update) {
    if (Record *record = NewRecord(1))
        record->words[0] = uint32_t(update & 0xff);
}

void __fastcall RecordTriggerFX(void *, int, int type, uint32_t which, uint32_t unused, const float *point,
                                const float *velocity, float intensity) {
    if (Record *record = NewRecord(2)) {
        record->words[0] = uint32_t(type);
        record->words[1] = which;
        record->words[2] = unused;
        if (point != NULL)
            memcpy(record->point, point, sizeof(record->point));
        if (velocity != NULL)
            memcpy(record->velocity, velocity, sizeof(record->velocity));
        record->intensity = intensity;
    }
}

void __fastcall RecordInitTyreTracks(PBondCar *, int) {
    NewRecord(3);
}

void __fastcall RecordInitializeCarVariables(PBondCar *, int, int initializeControls) {
    if (Record *record = NewRecord(4))
        record->words[0] = uint32_t(initializeControls & 0xff);
}

void __fastcall RecordImproveLanding(PBondCar *, int) {
    NewRecord(5);
}

void RecordMenuSound(int bank, const char *patch, const char *mix, int view) {
    if (Record *record = NewRecord(6)) {
        record->words[0] = uint32_t(bank);
        record->words[1] = uint32_t(view);
        if (patch != NULL)
            strncpy(record->names[0], patch, kNameBytes - 1);
        if (mix != NULL)
            strncpy(record->names[1], mix, kNameBytes - 1);
    }
}

// The jump our build wrote at a patched address, and its redirections. Our code calls our function directly, so
// its entry is patched to the recorder too (its first five bytes kept).
struct Redirection {
    uint32_t at;
    const void *jumpTarget;
    bool redirected;
    uint8_t saved[5];
    bool entryPatched;
};
Redirection g_redirections[4] = { { kInitTyreTracks, NULL, false, {}, false },
                                  { kInitializeCarVariables, NULL, false, {}, false },
                                  { kImproveLanding, NULL, false, {}, false },
                                  { kMenuSoundTrigger, NULL, false, {}, false } };

const void *JumpTarget(uint32_t at) {
    const uint8_t *code = reinterpret_cast<const uint8_t *>(uintptr_t(at));
    if (code[0] != 0xe9)
        return NULL;
    int32_t offset;
    memcpy(&offset, code + 1, sizeof(offset));
    return reinterpret_cast<const void *>(uintptr_t(at + 5 + offset));
}

bool PatchBytes(void *at, const void *bytes, size_t count) {
    DWORD old;
    if (!VirtualProtect(at, count, PAGE_EXECUTE_READWRITE, &old))
        return false;
    memcpy(at, bytes, count);
    VirtualProtect(at, count, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, count);
    return true;
}

void Redirect(Redirection *r, const void *to) {
    r->jumpTarget = JumpTarget(r->at);
    r->redirected = r->jumpTarget != NULL && XbeOriginal_Redirect(r->at, to);
    r->entryPatched = false;
    if (r->redirected) {
        uint8_t *entry = static_cast<uint8_t *>(const_cast<void *>(r->jumpTarget));
        uint8_t jump[5] = { 0xe9 };
        int32_t offset = int32_t(uintptr_t(to) - (uintptr_t(entry) + 5));
        memcpy(jump + 1, &offset, sizeof(offset));
        memcpy(r->saved, entry, sizeof(r->saved));
        r->entryPatched = PatchBytes(entry, jump, sizeof(jump));
    }
}

void Unredirect(Redirection *r) {
    if (r->entryPatched)
        PatchBytes(const_cast<void *>(r->jumpTarget), r->saved, sizeof(r->saved));
    r->entryPatched = false;
    if (r->redirected)
        XbeOriginal_Redirect(r->at, r->jumpTarget);
    r->redirected = false;
}

// ---- the state a call runs on

alignas(16) BondCarScratch g_pad;

struct LiveCar {
    PBondCar *car;
    RigidBody *body;
    RigidBodyInfo *info;
    CarPhysics *physics;
    void *audio;
    RTyreTrack *tracks[kCarWheels];
    void *ai;
    WRoadNav *nav;
    ActionQueue *queue;
    Handle *handle;
    size_t handleBytes;
};
LiveCar g_live;

struct CarState {
    alignas(16) uint8_t car[sizeof(PBondCar)];
    alignas(16) RigidBody body;
    alignas(16) RigidBodyInfo info;
    CarPhysics physics;
    uint8_t audio[kAudioBytes];
    uint8_t tracks[kCarWheels][kTyreTrackBytes];
    alignas(16) BondCarScratch pad;
    uint8_t ai[kAIBytes];
    uint8_t nav[sizeof(WRoadNav)];
    uint8_t camera[sizeof(RPlayerCamera)];
    uint8_t queue[sizeof(ActionQueue)];
    uint8_t handle[kHandleBytes];
    void *activeSystems[0x100];
    uint32_t activeSystemCount;
    uint32_t randomSeed;
    SimRandom simRandom;
    uint32_t barrierMask;
    uint8_t *creationPoint;
    uint8_t *deletionPoint;
    uint8_t events[kEventBytes];        // what a run added to the event queue
    uint32_t sounds;                    // the sounds a run added to fgSoundList (deleted)
    Log log;
};

RPlayerCamera *Camera() {
    return fgRenderHigh != NULL ? fgRenderHigh->views[0].camera : NULL;
}

RigidBody *BodyOf(PBondCar *car) {
    return Simulation_GetRigidBody(ShadowSim, 0, car->rigidBodySlot);
}

struct AIView {
    uint8_t unknown00[0x64];
    WRoadNav *driveToNav;
    uint8_t active;
    uint8_t unknown69[0x17];
    int32_t type;
};

void Bind(PBondCar *car) {
    g_live.car = car;
    g_live.body = BodyOf(car);
    g_live.info = g_live.body->info;
    g_live.physics = car->physics;
    g_live.audio = car->audio;
    for (int i = 0; i < kCarWheels; i++)
        g_live.tracks[i] = car->tyreTracks[i];
    g_live.ai = car->aiGroundVehicle;
    g_live.nav = g_live.ai != NULL ? static_cast<AIView *>(g_live.ai)->driveToNav : NULL;
    g_live.queue = car->actionQueue;
    g_live.handle = car->renderObject != NULL ? car->renderObject->animHandle : NULL;
    g_live.handleBytes = 0;
    if (g_live.handle != NULL && sizeof(Handle) + g_live.handle->dataSize <= kHandleBytes)
        g_live.handleBytes = sizeof(Handle) + g_live.handle->dataSize;
}

void TakeState(CarState *state) {
    memcpy(state->car, g_live.car, sizeof(PBondCar));
    state->body = *g_live.body;
    state->info = *g_live.info;
    state->physics = *g_live.physics;
    if (g_live.audio != NULL)
        memcpy(state->audio, g_live.audio, kAudioBytes);
    for (int i = 0; i < kCarWheels; i++)
        if (g_live.tracks[i] != NULL)
            memcpy(state->tracks[i], g_live.tracks[i], kTyreTrackBytes);
    state->pad = g_pad;
    if (g_live.ai != NULL)
        memcpy(state->ai, g_live.ai, kAIBytes);
    if (g_live.nav != NULL)
        memcpy(state->nav, g_live.nav, sizeof(WRoadNav));
    if (Camera() != NULL)
        memcpy(state->camera, Camera(), sizeof(RPlayerCamera));
    if (g_live.queue != NULL)
        memcpy(state->queue, g_live.queue, sizeof(ActionQueue));
    if (g_live.handleBytes != 0)
        memcpy(state->handle, g_live.handle, g_live.handleBytes);
    memcpy(state->activeSystems, ShadowActiveSystems, sizeof(state->activeSystems));
    state->activeSystemCount = ShadowActiveSystemCount;
    state->randomSeed = ShadowRandomSeed;
    state->simRandom = *ShadowSimRandom;
    state->barrierMask = fgCollisionMgr->barrierMask;
    state->creationPoint = ShadowCreationPoint;
    state->deletionPoint = ShadowDeletionPoint;
    state->log = g_log;
}

void PutState(const CarState *state) {
    memcpy(g_live.car, state->car, sizeof(PBondCar));
    *g_live.body = state->body;
    *g_live.info = state->info;
    *g_live.physics = state->physics;
    if (g_live.audio != NULL)
        memcpy(g_live.audio, state->audio, kAudioBytes);
    for (int i = 0; i < kCarWheels; i++)
        if (g_live.tracks[i] != NULL)
            memcpy(g_live.tracks[i], state->tracks[i], kTyreTrackBytes);
    g_pad = state->pad;
    if (g_live.ai != NULL)
        memcpy(g_live.ai, state->ai, kAIBytes);
    if (g_live.nav != NULL)
        memcpy(g_live.nav, state->nav, sizeof(WRoadNav));
    if (Camera() != NULL)
        memcpy(Camera(), state->camera, sizeof(RPlayerCamera));
    if (g_live.queue != NULL)
        memcpy(g_live.queue, state->queue, sizeof(ActionQueue));
    if (g_live.handleBytes != 0)
        memcpy(g_live.handle, state->handle, g_live.handleBytes);
    memcpy(ShadowActiveSystems, state->activeSystems, sizeof(state->activeSystems));
    ShadowActiveSystemCount = state->activeSystemCount;
    ShadowRandomSeed = state->randomSeed;
    *ShadowSimRandom = state->simRandom;
    fgCollisionMgr->barrierMask = state->barrierMask;
    ShadowCreationPoint = state->creationPoint;
    ShadowDeletionPoint = state->deletionPoint;
    g_log = state->log;
}

CarState g_saved, g_start, g_result[2];

// The sounds added to the end of fgSoundList after `last`, deleted as the game deletes a sound; their number
uint32_t DeleteNewSounds(PointerListNode *last) {
    ASoundList *list = fgSoundList;
    if (list == NULL || last == NULL)
        return 0;
    uint32_t count = 0;
    while (list->head->prev != last) {
        PointerListNode *node = list->head->prev;
        static_cast<ABaseSound *>(node->value)->CallDelete(1);
        count++;
        if (list->head->prev == node)
            break;          // not taken out by its delete: leave the rest
    }
    return count;
}

// The fake scene object: a copy of the car's with a vtable that records slots 14 and 15
alignas(16) uint8_t g_fakeRender[kRenderBytes];
void *g_fakeVtable[kVtableSlots];

void UseFakeRender(PBondCar *car, RSceneObj *real) {
    if (real == NULL)
        return;
    memcpy(g_fakeRender, real, kRenderBytes);
    memcpy(g_fakeVtable, *reinterpret_cast<void ***>(real), sizeof(g_fakeVtable));
    g_fakeVtable[14] = reinterpret_cast<void *>(&RecordUpdatePosition);
    g_fakeVtable[15] = reinterpret_cast<void *>(&RecordTriggerFX);
    void **vtable = g_fakeVtable;
    memcpy(g_fakeRender, &vtable, sizeof(vtable));
    car->renderObject = reinterpret_cast<RSceneObj *>(g_fakeRender);
}

// The original (all of the package's entry bytes swapped in), then the port, each from the car's current state;
// the car is left as it was
template <class F>
void Both(F &&call) {
    typedef typename std::remove_reference<F>::type Call;
    CaseFn thunk = [](void *context, bool original) { (*static_cast<Call *>(context))(original); };
    g_log.count = 0;
    TakeState(&g_start);
    for (int run = 0; run < 2; run++) {
        const bool original = run == 0;
        PutState(&g_start);
        PointerListNode *lastSound = fgSoundList != NULL ? fgSoundList->head->prev : NULL;
        if (original)
            RestoreOriginals(true);
        g_runFaults[run] = Guarded(thunk, &call, original) ? 0 : 1;
        if (original)
            RestoreOriginals(false);
        TakeState(&g_result[run]);
        g_result[run].sounds = DeleteNewSounds(lastSound);
        g_soundsDeleted += int(g_result[run].sounds);
        memset(g_result[run].events, 0, kEventBytes);
        if (ShadowCreationPoint != g_start.creationPoint && g_start.creationPoint != NULL) {
            memcpy(g_result[run].events, g_start.creationPoint, kEventBytes);
            // the impact's +0x38..+0x3f (after the event's vtable), which its constructor leaves as the stack had it
            memset(g_result[run].events + 4 + 0x38, 0, 8);
        }
    }
    PutState(&g_start);
}

void CompareRuns(const char *what, int index) {
    g_cases++;
    BeginCase(what);
    char name[64];
    const CarState &a = g_result[0], &b = g_result[1];
    snprintf(name, sizeof(name), "%s car", what);
    CheckBytes(name, index, a.car, b.car, sizeof(PBondCar));
    snprintf(name, sizeof(name), "%s body", what);
    CheckBytes(name, index, &a.body, &b.body, sizeof(RigidBody));
    snprintf(name, sizeof(name), "%s info", what);
    CheckBytes(name, index, &a.info, &b.info, sizeof(RigidBodyInfo));
    snprintf(name, sizeof(name), "%s physics", what);
    CheckBytes(name, index, &a.physics, &b.physics, sizeof(CarPhysics));
    snprintf(name, sizeof(name), "%s audio", what);
    CheckBytes(name, index, a.audio, b.audio, kAudioBytes);
    snprintf(name, sizeof(name), "%s tracks", what);
    CheckBytes(name, index, a.tracks, b.tracks, sizeof(a.tracks));
    snprintf(name, sizeof(name), "%s scratch pad", what);
    CheckBytes(name, index, &a.pad, &b.pad, sizeof(BondCarScratch));
    snprintf(name, sizeof(name), "%s AI", what);
    CheckBytes(name, index, a.ai, b.ai, kAIBytes);
    snprintf(name, sizeof(name), "%s navigator", what);
    CheckBytes(name, index, a.nav, b.nav, sizeof(WRoadNav));
    snprintf(name, sizeof(name), "%s camera", what);
    CheckBytes(name, index, a.camera, b.camera, sizeof(RPlayerCamera));
    snprintf(name, sizeof(name), "%s queue", what);
    CheckBytes(name, index, a.queue, b.queue, sizeof(ActionQueue));
    if (g_live.handleBytes != 0) {
        snprintf(name, sizeof(name), "%s handle", what);
        CheckBytes(name, index, a.handle, b.handle, g_live.handleBytes);
    }
    snprintf(name, sizeof(name), "%s active systems", what);
    CheckBytes(name, index, a.activeSystems, b.activeSystems, sizeof(a.activeSystems));
    if (g_openKind != NULL)
        g_openKind->sounds += int(a.sounds + b.sounds);
    uint32_t words[2][5] = { { a.activeSystemCount, a.randomSeed, a.barrierMask, uint32_t(uintptr_t(a.creationPoint)),
                               a.sounds },
                             { b.activeSystemCount, b.randomSeed, b.barrierMask, uint32_t(uintptr_t(b.creationPoint)),
                               b.sounds } };
    snprintf(name, sizeof(name), "%s globals", what);
    CheckBytes(name, index, words[0], words[1], sizeof(words[0]));
    snprintf(name, sizeof(name), "%s random", what);
    CheckBytes(name, index, &a.simRandom, &b.simRandom, sizeof(SimRandom));
    snprintf(name, sizeof(name), "%s events", what);
    CheckBytes(name, index, a.events, b.events, kEventBytes);
    snprintf(name, sizeof(name), "%s calls", what);
    CheckBytes(name, index, &a.log, &b.log, sizeof(Log));
}

// ---- inputs

uint32_t g_random = 0x2c9e51b7;

uint32_t Random() {
    g_random ^= g_random << 13;
    g_random ^= g_random >> 17;
    g_random ^= g_random << 5;
    return g_random;
}

int RandomInt(int n) { return n <= 0 ? 0 : int(Random() % uint32_t(n)); }
float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Random() >> 8) * (1.0f / 16777216.0f); }
bool Chance(int n) { return RandomInt(n) == 0; }

const float kEdges[] = { 0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 0.2f, 0.9f };
float Edgy(float lo, float hi) {
    return Chance(5) ? kEdges[RandomInt(int(sizeof(kEdges) / sizeof(kEdges[0])))] : Uniform(lo, hi);
}

void RandomVector(Coord3 *v, float size) {
    *v = { Uniform(-size, size), Uniform(-size, size), Uniform(-size, size) };
}

void RandomRotation(MATRIX4 *m, bool nearUpright) {
    for (;;) {
        Coord4 q = { Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f), Uniform(-1.0f, 1.0f) };
        if (nearUpright) {
            q.x *= 0.15f;
            q.z *= 0.15f;
        }
        float length = sqrtf(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
        if (length > 0.1f) {
            q = { q.x / length, q.y / length, q.z / length, q.w / length };
            VU0_quattom4(m, &q);
            return;
        }
    }
}

void FillPad() {
    float *words = reinterpret_cast<float *>(&g_pad);
    for (size_t i = 0; i < sizeof(g_pad) / sizeof(float); i++)
        words[i] = Uniform(-2.0f, 2.0f);
}

std::vector<PBondCar *> g_cars;

// The car's driving state shaken: controls, speeds, timers, flags
void PerturbDriving(PBondCar *car, bool withAudio) {
    RigidBody *body = g_live.body;
    BondCarControl &control = car->control;
    control.steering = Edgy(-1.0f, 1.0f);
    control.steeringVertical = Edgy(-1.0f, 1.0f);
    control.gas = Chance(3) ? 0.0f : Edgy(0.0f, 1.0f);
    control.brake = Chance(2) ? 0.0f : Edgy(0.0f, 1.0f);
    control.handBrake = uint8_t(Chance(3));
    car->reverseTimer = int8_t(Chance(2) ? 30 : RandomInt(32));
    car->reversing = uint8_t(Chance(4));
    car->unknown29B = uint8_t(Chance(3));
    car->unknown29F = uint8_t(Chance(2));
    car->againstWall = uint8_t(Chance(5));
    car->inShock = int8_t(Chance(4) ? RandomInt(10) : 0);
    car->oilSlick = uint8_t(Chance(6));
    car->numWheelsOnGround = int8_t(RandomInt(5));
    car->gear = int8_t(RandomInt(6) - (Chance(4) ? 1 : 0));
    car->previousGear = Chance(2) ? car->gear : int8_t(RandomInt(6));
    car->gearChangeTimer = uint8_t(RandomInt(16));
    car->unknown2DA = uint8_t(Chance(2));
    car->twoWheelMode = uint8_t(Chance(5));
    car->twoWheelStuntTimer = Chance(2) ? 1.0f : Uniform(0.0f, 1.0f);
    car->unknown2B3 = uint8_t(Chance(4));
    car->unknown3FD = uint8_t(Chance(3) ? RandomInt(20) : 0);
    car->landingFlag = uint8_t(Chance(2) ? RandomInt(40) : 255);
    car->carWheelSpeed = Uniform(-0.05f, 0.05f);
    car->wheelSpinAngle[0] = Uniform(-1.5f, 1.5f);
    car->wheelSpinAngle[1] = Chance(3) ? 0.9999f : Uniform(-1.5f, 1.5f);
    car->unknown2EC = Chance(3) ? Uniform(0.0f, 2.0f) : 0.0f;
    car->unknown3F8 = Chance(4) ? Uniform(-1.0f, 1.0f) : 0.0f;
    car->bob = Uniform(-0.2f, 0.2f);
    car->bobAngle = Chance(3) ? 0.999f : Uniform(0.0f, 1.0f);
    car->unknown3E8 = Chance(3) ? 0.998f : Uniform(0.0f, 1.0f);
    for (int i = 0; i < kCarWheels; i++) {
        car->suspensionCompression[i] = Edgy(-0.2f, 0.5f);
        if (Chance(5))
            car->tyreDamagePoints[i] = 0;
    }
    car->rocketBoost = 0;
    if (Chance(3)) {
        const int boostEnd = car->attributes.LookupInt("BOOST_TIME", NULL) * 3;
        car->rocketBoost = withAudio ? boostEnd + RandomInt(3) : boostEnd + 2 + RandomInt(5);
    }
    RandomVector(&body->velocity, Chance(2) ? 3.0f : 40.0f);
    RandomVector(&body->angularVelocity, Chance(2) ? 0.5f : 3.0f);
    RandomVector(&body->angularMomentum, 2000.0f);
    if (Chance(2))
        RandomRotation(&body->info->orientation, !Chance(3));
    body->info->unknown4fd = uint8_t(Chance(5));
    body->info->unknown4fe = uint8_t(Chance(5));
    body->info->unknown4de = uint16_t(Chance(2) ? 0xffff : RandomInt(100));
    body->groundContacts = uint8_t(Chance(2) ? 0 : RandomInt(4));
    if (Chance(3))
        body->flags ^= RigidBody::kFlag0;
}

// ---- the accessors

void ScrambleCar(PBondCar *car) {
    uint8_t *bytes = reinterpret_cast<uint8_t *>(car);
    for (size_t at = offsetof(PBondCar, tyreTracks); at < sizeof(PBondCar); at++)
        bytes[at] = uint8_t(Random());
}

uint64_t g_value[2];

template <class F>
void Accessor(PBondCar *car, const char *what, int index, F &&call) {
    TakeState(&g_saved);
    ScrambleCar(car);
    g_value[0] = g_value[1] = 0xdeadbeefdeadbeefull;
    Both([&](bool original) { g_value[original ? 0 : 1] = call(original); });
    CompareRuns(what, index);
    char name[64];
    snprintf(name, sizeof(name), "%s result", what);
    CheckBytes(name, index, &g_value[0], &g_value[1], sizeof(uint64_t));
    PutState(&g_saved);
}

uint64_t Bits(float f) { return std::bit_cast<uint32_t>(f); }
uint64_t Bits(const void *p) { return uintptr_t(p); }

void TestAccessors() {
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        for (int repeat = 0; repeat < 8; repeat++, index++) {
            const int wheel = RandomInt(kCarWheels);
            const int byte = int(Random() & 0xff);
            const int byte2 = int(Random() & 0xff);
            Accessor(car, "GetSuspensionCompression", index, [&](bool o) {
                return o ? Bits(((PtrGetFn)0x00066060)(car, 0)) : Bits(car->GetSuspensionCompression()); });
            Accessor(car, "GetCarWheelSlip", index, [&](bool o) {
                return o ? Bits(((FloatIndexFn)0x00066070)(car, 0, wheel)) : Bits(car->GetCarWheelSlip(wheel)); });
            Accessor(car, "DisableTyreBlowOuts", index, [&](bool o) {
                if (o) ((CarFn)0x00066080)(car, 0); else car->DisableTyreBlowOuts(); return uint64_t(0); });
            Accessor(car, "GetIsInTwoWheelMode", index, [&](bool o) {
                return uint64_t(o ? ((ByteGetFn)0x00066090)(car, 0) : uint8_t(car->GetIsInTwoWheelMode())); });
            Accessor(car, "GetWasInAir", index, [&](bool o) {
                return uint64_t(o ? ((ByteGetFn)0x000660a0)(car, 0) : car->GetWasInAir()); });
            Accessor(car, "SetWasInAir", index, [&](bool o) {
                if (o) ((FlagFn)0x000660b0)(car, 0, byte & 1); else car->SetWasInAir((byte & 1) != 0);
                return uint64_t(0); });
            Accessor(car, "SetImmunity", index, [&](bool o) {
                if (o) ((FlagFn)0x000660c0)(car, 0, byte & 1); else car->SetImmunity((byte & 1) != 0);
                return uint64_t(0); });
            Accessor(car, "GetWheelPos", index, [&](bool o) {
                return o ? Bits(((PtrIndexFn)0x000660d0)(car, 0, wheel)) : Bits(car->GetWheelPos(wheel)); });
            Accessor(car, "GetForceStop", index, [&](bool o) {
                return uint64_t(o ? ((ByteGetFn)0x000660e0)(car, 0) : car->GetForceStop()); });
            Accessor(car, "ForceStopOn", index, [&](bool o) {
                if (o) ((FlagFn)0x000660f0)(car, 0, byte); else car->ForceStopOn(uint8_t(byte)); return uint64_t(0); });
            Accessor(car, "ForceStopOff", index, [&](bool o) {
                if (o) ((FlagFn)0x00066110)(car, 0, byte); else car->ForceStopOff(uint8_t(byte)); return uint64_t(0); });
            Accessor(car, "ForceRollDirection", index, [&](bool o) {
                if (o) ((FlagFn)0x00066130)(car, 0, byte & 1); else car->ForceRollDirection((byte & 1) != 0);
                return uint64_t(0); });
            Accessor(car, "RollSub", index, [&](bool o) {
                if (o) ((TwoFlagFn)0x00066140)(car, 0, byte & 1, byte2 & 1);
                else car->RollSub((byte & 1) != 0, (byte2 & 1) != 0);
                return uint64_t(0); });
            Accessor(car, "GetSecondaryType", index, [&](bool o) {
                return o ? Bits(((PtrGetFn)0x00066160)(car, 0)) : Bits(car->GetSecondaryType()); });
            Accessor(car, "GetTargetBeacon", index, [&](bool o) {
                return o ? Bits(((PtrGetFn)0x00066170)(car, 0)) : Bits(car->GetTargetBeacon()); });
        }
    }
}

// ---- SetVisualDamage

void TestVisualDamage() {
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        if (car->renderObject == NULL || g_live.handleBytes == 0)
            continue;
        for (int repeat = 0; repeat < 24; repeat++, index++) {
            TakeState(&g_saved);
            UseFakeRender(car, car->renderObject);
            ShadowRandomSeed = Random() & 0xffff;
            float fromA = Uniform(-0.1f, 1.0f), toA = Chance(6) ? fromA - 0.1f : Uniform(fromA, 1.05f);
            float fromB = Uniform(-0.1f, 1.0f), toB = Chance(6) ? fromB - 0.1f : Uniform(fromB, 1.05f);
            if (Chance(4)) {
                fromA = 0.0f;
                toA = 1.0f;
            }
            Both([&](bool original) {
                if (original)
                    ((DamageFn)0x00066180)(car, 0, fromA, toA, fromB, toB);
                else
                    car->SetVisualDamage(fromA, toA, fromB, toB);
            });
            CompareRuns("SetVisualDamage", index);
            PutState(&g_saved);
        }
    }
}

// ---- ResetCar

// The navigator is placed on a segment of the loaded network, with some length to it: only then can ResetCar's
// IncNavPosition move it (unplaced, it reads a missing segment table, or loops on a curve of no length)
bool NavOnRoad(const WRoadNav *nav) {
    if (nav == NULL || !fgRoadNetworkData.loaded || fgRoadNetworkData.segments == NULL)
        return false;
    if (nav->segment < 0 || nav->segment >= fgRoadNetworkData.segmentCount ||
        fgRoadNetworkData.segments[nav->segment] == NULL)
        return false;
    return vec3distance(&nav->boundStart, &nav->boundEnd) > 0.01f;
}

void TestResetCar() {
    if (!g_redirections[0].redirected || !g_redirections[1].redirected) {
        g_skipped++;
        return;
    }
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        if (car->renderObject == NULL)
            continue;
        for (int repeat = 0; repeat < 12; repeat++, index++) {
            TakeState(&g_saved);
            UseFakeRender(car, car->renderObject);
            static const int kClasses[] = { 0, 1, 2, 3 };
            if (repeat > 0)
                car->carClass = kClasses[RandomInt(4)];
            if (car->carClass == 0 && Camera() == NULL)
                car->carClass = 1;
            if (Chance(2))
                RandomRotation(&g_live.body->info->orientation, Chance(2));
            RandomVector(&car->resetDirection, 1.0f);
            if (g_live.ai != NULL && NavOnRoad(g_live.nav) && Chance(2)) {
                AIView *ai = static_cast<AIView *>(g_live.ai);
                ai->active = 1;
                ai->type = 2;
            }
            bool withTrack[kCarWheels];
            for (int i = 0; i < kCarWheels; i++)
                withTrack[i] = !Chance(4);
            Both([&](bool original) {
                // Fresh blocks for the run to free; afterwards the car's (freed) pointers are written as tokens, so
                // the two runs' blocks compare equal
                RTyreTrack *blocks[kCarWheels];
                for (int i = 0; i < kCarWheels; i++) {
                    blocks[i] = withTrack[i] ? static_cast<RTyreTrack *>(UMemory::FastAlloc(kTyreTrackBytes, "#shadow"))
                                             : NULL;
                    car->tyreTracks[i] = blocks[i];
                }
                if (original)
                    ((FlagFn)0x00066390)(car, 0, 1);
                else
                    car->ResetCar(true);
                for (int i = 0; i < kCarWheels; i++)
                    if (blocks[i] != NULL && car->tyreTracks[i] == blocks[i])
                        car->tyreTracks[i] = reinterpret_cast<RTyreTrack *>(uintptr_t(i + 1));
            });
            CompareRuns("ResetCar", index);
            PutState(&g_saved);
        }
    }
}

// ---- GetControllerInput

void TestControllerInput() {
    const bool hud = GHud::TheApp() != NULL;
    int index = 0;
    for (PBondCar *car : g_cars) {
        if (car->actionQueue == NULL)
            continue;
        Bind(car);
        for (int repeat = 0; repeat < 64; repeat++, index++) {
            TakeState(&g_saved);
            PerturbDriving(car, g_live.audio != NULL);
            static const uint8_t kStops[] = { 0, 0, 0, 0, 1, 2, 4, 8, 0x0a, 0xf0 };
            car->forceStop = kStops[RandomInt(10)];
            car->unknown278 = Edgy(0.0f, 1.0f);
            car->unknown27C = Edgy(0.0f, 1.0f);
            car->unknown280 = Edgy(0.0f, 1.0f);
            car->unknown284 = Edgy(0.0f, 1.0f);
            car->unknown2C4 = Edgy(-1.0f, 1.0f);
            car->unknown400 = Edgy(-1.0f, 1.0f);
            car->unknown404 = Chance(4) ? Uniform(-1.0f, 1.0f) : 0.0f;
            car->carSpeed = Uniform(0.0f, 60.0f);
            car->physics->wsSpeed = Uniform(0.0f, 60.0f);
            car->physics->isSub = Chance(4);
            car->actionQueue->Flush();
            for (int n = RandomInt(9); n > 0; n--) {
                ActionData action;
                action.action = RandomInt(36);
                if (!hud && action.action >= 29 && action.action <= 33)
                    action.action = 22;
                if (g_live.audio == NULL && action.action >= 12 && action.action <= 17)
                    action.action = 1;
                action.source = 0;
                action.value = Edgy(-1.0f, 1.0f);
                if (Chance(6))
                    action.value = BondCar_PAD_DEAD_ZONE * (Chance(2) ? 1.0f : -1.0f);
                car->actionQueue->ReceiveAction(&action);
            }
            Both([&](bool original) {
                if (original)
                    ((CarFn)0x00066640)(car, 0);
                else
                    car->GetControllerInput();
            });
            CompareRuns("GetControllerInput", index);
            PutState(&g_saved);
        }
    }
}

// ---- TwoWheelStunt

void TestTwoWheelStunt() {
    const float saved[5] = { BondCar_TWO_WHEEL_ANGLE, BondCar_TWO_WHEEL_SCALE, BondCar_TWO_WHEEL_LIMIT,
                             BondCar_TWO_WHEEL_OPPOSITE_SCALE, BondCar_TWO_WHEEL_TENSOR_SCALE };
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        for (int repeat = 0; repeat < 32; repeat++, index++) {
            TakeState(&g_saved);
            PerturbDriving(car, g_live.audio != NULL);
            car->numWheelsOnGround = int8_t(Chance(5) ? 0 : 1 + RandomInt(4));
            car->unknown2B1 = uint8_t(RandomInt(6));
            car->carSpeed = Chance(6) ? 20.0f : Uniform(10.0f, 60.0f);
            if (Chance(2)) {
                BondCar_TWO_WHEEL_ANGLE = Uniform(-0.2f, 0.2f);
                BondCar_TWO_WHEEL_SCALE = Uniform(0.0f, 3000.0f);
                BondCar_TWO_WHEEL_LIMIT = Uniform(0.0f, 3000.0f);
                BondCar_TWO_WHEEL_OPPOSITE_SCALE = Uniform(0.0f, 1.0f);
                BondCar_TWO_WHEEL_TENSOR_SCALE = Uniform(0.0f, 30.0f);
            }
            Both([&](bool original) {
                if (original)
                    ((CarFn)0x00066c90)(car, 0);
                else
                    car->TwoWheelStunt();
            });
            CompareRuns("TwoWheelStunt", index);
            PutState(&g_saved);
            BondCar_TWO_WHEEL_ANGLE = saved[0];
            BondCar_TWO_WHEEL_SCALE = saved[1];
            BondCar_TWO_WHEEL_LIMIT = saved[2];
            BondCar_TWO_WHEEL_OPPOSITE_SCALE = saved[3];
            BondCar_TWO_WHEEL_TENSOR_SCALE = saved[4];
        }
    }
}

// ---- ControlTyreTracks

void TestTyreTracks() {
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        for (int repeat = 0; repeat < 24; repeat++, index++) {
            TakeState(&g_saved);
            for (int i = 0; i < kCarWheels; i++) {
                if (Chance(3))
                    car->wheels[i].face.corner[2].tag.type = uint8_t(Chance(3) ? kSurface11 : RandomInt(14));
                car->suspensionCompression[i] = Edgy(-0.1f, 0.4f);
                car->wheelSlip[i] = Chance(3) ? 0.0f : Uniform(0.0f, 1.0f);
                car->tyreDamagePoints[i] = Chance(5) ? 0 : 50;
            }
            car->unknown3FE = uint8_t(Chance(3));
            g_live.physics->isSnowmobile = Chance(5);
            RandomVector(&g_live.body->angularVelocity, 2.0f);
            RandomVector(&g_live.body->velocity, 30.0f);
            Both([&](bool original) {
                if (original)
                    ((CarFn)0x00066ee0)(car, 0);
                else
                    car->ControlTyreTracks();
            });
            CompareRuns("ControlTyreTracks", index);
            PutState(&g_saved);
        }
    }
}

// ---- the wheel forces

void RandomWheelInputs(PBondCar *car) {
    for (int i = 0; i < kCarWheels; i++) {
        BondCarWheelInput &in = g_pad.wheelInput[i];
        in.grip = Edgy(0.0f, 10.0f);
        in.drive = Chance(3) ? 0.0f : Uniform(-20.0f, 20.0f);
        in.frictionLimit = Chance(8) ? 0.0f : Uniform(0.1f, 40.0f);
        in.handbrake = Chance(2) ? 0.0f : Uniform(0.0f, 10.0f);
        g_pad.wheelSlip[i] = Uniform(0.0f, 1.0f);
        Coord4 &normal = car->wheelRoadNormal[i];
        normal = { Uniform(-0.4f, 0.4f), 1.0f, Uniform(-0.4f, 0.4f), Uniform(-0.6f, 0.3f) };
        VU0_v4unitxyz(&normal, &normal);
        car->wheelPos[i] = { g_live.body->position.x + Uniform(-2.0f, 2.0f), g_live.body->position.y - 0.5f,
                             g_live.body->position.z + Uniform(-2.0f, 2.0f), 0.0f };
        if (Chance(3))
            car->wheels[i].face.corner[2].tag.type = uint8_t(Chance(3) ? kICE : RandomInt(14));
    }
    for (int i = 0; i < 2; i++) {
        Coord4 &heading = g_pad.wheelHeading[i];
        heading = { Uniform(-1.0f, 1.0f), Uniform(-0.1f, 0.1f), Uniform(-1.0f, 1.0f), 0.0f };
        VU0_v4unitxyz(&heading, &heading);
    }
    g_pad.rollingResistance4 = { Uniform(-0.2f, 0.2f), 0.0f, Uniform(-0.2f, 0.2f), 0.0f };
    car->unknown2E8 = Edgy(0.0f, 1.0f);
    car->carSpeed = Uniform(0.0f, 60.0f);
    CarPhysics *physics = g_live.physics;
    physics->isRally = Chance(3);
    if (Chance(2))
        physics->yawStabilityFactor = Chance(2) ? 0.0f : Uniform(0.0f, 2.0f);
    if (Chance(3))
        physics->springRestLength = Uniform(0.0f, 0.6f);
    if (Chance(3))
        physics->springCompressionLimit = Uniform(0.0f, 0.5f);
    car->carClass = RandomInt(4);
    if (Chance(4) && car->hitPointLoc == &car->hitPoints)
        car->hitPoints = Chance(2) ? 0.0f : -5.0f;
    if (Chance(4))
        physics->isBoat = 1;
}

void TestWheelForces() {
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        for (int repeat = 0; repeat < 32; repeat++, index++) {
            TakeState(&g_saved);
            PerturbDriving(car, g_live.audio != NULL);
            RandomWheelInputs(car);
            const bool wheelSpin = Chance(3);
            uint32_t answer[2] = { 0, 0 };
            Both([&](bool original) {
                if (original)
                    answer[0] = uint8_t(((WheelForcesFn)0x000670e0)(car, 0, g_pad.wheelInput, g_pad.wheelSlip,
                                                                    g_pad.wheelHeading, car->wheelRoadNormal,
                                                                    car->wheelPos, &g_pad.rollingResistance4,
                                                                    wheelSpin));
                else
                    answer[1] = uint8_t(car->AddWheelForces(g_pad.wheelInput, g_pad.wheelSlip, g_pad.wheelHeading,
                                                            car->wheelRoadNormal, car->wheelPos,
                                                            &g_pad.rollingResistance4, wheelSpin));
            });
            CompareRuns("AddWheelForces", index);
            CheckBytes("AddWheelForces result", index, &answer[0], &answer[1], sizeof(uint32_t));
            PutState(&g_saved);

            TakeState(&g_saved);
            PerturbDriving(car, g_live.audio != NULL);
            RandomWheelInputs(car);
            Both([&](bool original) {
                if (original)
                    answer[0] = uint8_t(((SimpleWheelForcesFn)0x000690a0)(car, 0, g_pad.wheelInput,
                                                                          g_pad.wheelHeading, car->wheelRoadNormal,
                                                                          car->wheelPos, g_pad.wheelSlip));
                else
                    answer[1] = uint8_t(car->AddSimpleWheelForces(g_pad.wheelInput, g_pad.wheelHeading,
                                                                  car->wheelRoadNormal, car->wheelPos,
                                                                  g_pad.wheelSlip));
            });
            CompareRuns("AddSimpleWheelForces", index);
            CheckBytes("AddSimpleWheelForces result", index, &answer[0], &answer[1], sizeof(uint32_t));
            PutState(&g_saved);
        }
    }
}

// ---- the physics steps

void TestPhysics() {
    int index = 0;
    for (PBondCar *car : g_cars) {
        Bind(car);
        for (int repeat = 0; repeat < 48; repeat++, index++) {
            for (int model = 0; model < 2; model++) {
                TakeState(&g_saved);
                if (car->renderObject != NULL)
                    UseFakeRender(car, car->renderObject);
                if (repeat > 0) {
                    PerturbDriving(car, g_live.audio != NULL);
                    car->carClass = RandomInt(4);
                    if (Chance(4))
                        g_live.physics->counterSteerScale = Chance(2) ? 0.0f : Uniform(0.0f, 3.0f);
                    if (Chance(4))
                        g_live.physics->isRally = !g_live.physics->isRally;
                    if (Chance(6))
                        g_live.physics->isBoat = 1;
                    if (Chance(4) && car->hitPointLoc == &car->hitPoints)
                        car->hitPoints = Chance(2) ? 0.0f : 50.0f;
                }
                if (model == 0) {
                    Both([&](bool original) {
                        if (original)
                            ((CarFn)0x00067bd0)(car, 0);
                        else
                            car->ProcessPhysics();
                    });
                    CompareRuns("ProcessPhysics", index);
                } else {
                    Both([&](bool original) {
                        if (original)
                            ((CarFn)0x00069710)(car, 0);
                        else
                            car->ProcessSimplePhysics();
                    });
                    CompareRuns("ProcessSimplePhysics", index);
                }
                PutState(&g_saved);
            }
        }
    }
}

}  // namespace

void BondCarShadowC_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_BONDCARSHADOWC", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    g_cars.clear();
    for (int i = 0; i < kOwners; i++) {
        PhysicsObject *owner = PhysicsObjects[i];
        if (owner == NULL || uint32_t(uintptr_t(owner->vtable)) != kBondCarVtable)
            continue;
        PBondCar *car = static_cast<PBondCar *>(static_cast<PVehicle *>(owner));
        RigidBody *body = BodyOf(car);
        if (body != NULL && body->info != NULL && car->physics != NULL)
            g_cars.push_back(car);
    }
    if (g_cars.empty() || fgCollisionMgr == NULL) {
        printf("[bondcarC] no live cars - nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);
    BondCarScratch *livePad = BondCarScratchPad;
    FillPad();
    BondCarScratchPad = &g_pad;
    g_log.count = 0;
    Redirect(&g_redirections[0], reinterpret_cast<const void *>(&RecordInitTyreTracks));
    Redirect(&g_redirections[1], reinterpret_cast<const void *>(&RecordInitializeCarVariables));
    Redirect(&g_redirections[2], reinterpret_cast<const void *>(&RecordImproveLanding));
    Redirect(&g_redirections[3], reinterpret_cast<const void *>(&RecordMenuSound));

    TestAccessors();
    TestVisualDamage();
    TestResetCar();
    TestControllerInput();
    TestTwoWheelStunt();
    TestTyreTracks();
    TestWheelForces();
    TestPhysics();

    for (Redirection &r : g_redirections)
        Unredirect(&r);
    BondCarScratchPad = livePad;
    ResetFpu();
    EndCase();
    printf("[bondcarC]   by function (cases/differing/faulted original+port):");
    for (int i = 0; i < g_kindCount; i++) {
        const Kind &kind = g_kinds[i];
        if (kind.differing != 0 || kind.faults[0] != 0 || kind.faults[1] != 0)
            printf(" %s %d/%d/%d+%d", kind.name, kind.cases, kind.differing, kind.faults[0], kind.faults[1]);
    }
    printf("\n");
    if (g_soundsDeleted != 0) {
        printf("[bondcarC]   sounds the runs left in the sound list, deleted:");
        for (int i = 0; i < g_kindCount; i++)
            if (g_kinds[i].sounds != 0)
                printf(" %s %d", g_kinds[i].name, g_kinds[i].sounds);
        printf("%s\n", g_redirections[3].redirected ? "" : " (AMenuSound::Trigger not redirected)");
    }
    printf("[bondcarC] PBondCar part 3 vs originals (%d cars): %d cases, %d checks, %d differ%s%s\n",
           int(g_cars.size()), g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "",
           g_skipped != 0 ? " (ResetCar skipped: InitTyreTracks/InitializeCarVariables not ported)" : "");
    fflush(stdout);
}
