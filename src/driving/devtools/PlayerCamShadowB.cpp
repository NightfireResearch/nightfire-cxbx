#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "PlayerCamShadowB.h"

#include "../camera/CameraSpline.h"
#include "../camera/DirectorQueue.h"
#include "../camera/PlayerCamState.h"
#include "../camera/PlayerCamera.h"
#include "../camera/PlayerCameraB.h"
#include "../engine/SimRandom.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../render/Colorize.h"
#include "../render/RenderHigh.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"
#include "../../common/xbeOriginal.h"
#include "../../common/xbeOverload.h"

#include <windows.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_PLAYERCAMSHADOWB=1, once on the first simulation tick: camera/PlayerCameraB.cpp against the originals.
//
// Each case starts from a copy of the live player camera (fgRenderHigh->views[0]), perturbed, with its
// RPlayerCamState and its road navigator (and the navigator's spline) swapped for copies of their own. The case runs
// twice from the same bytes - first with 0x00083190-0x00086bb0's originals swapped back in, then with our jumps -
// calling the original address both times. Everything a run can write is put back before each run and compared
// after: the camera, its state and navigator, the auto-drive arms, the mode table, the cinematic heli arm, the
// camera globals, the simulation's random generator, the road network's query stamps, GetSegmentCurveStep's spline,
// the player car audio's two flags, the scratch arguments and the result.
//
// The calls that reach beyond the camera are replaced for both runs by fakes that record their arguments, and the
// two records compared: RDirectorQueue::AppendData (the record queued), RColorize::SetEnabled, RCameraSpline::
// ClearSplinePtList, the three animation loads (answering as the case says) and GetSafeWRoadNavPosition (the
// navigator's position at the height given plus 1.25).
//
// The original UpdateSplineCam eases towards an uninitialised stack vector on a road of one lane (the port uses
// zero), so the spline cases start on segments of several lanes only, and only where the navigator finds a segment
// at each point the camera may place it (see NavFindsSegment); a navigator never placed is placed at the eye for
// the cases without a mode change (see NavCurveHasLength). UpdateAutoDriveCam's cameraOffset.w is the
// original's uninitialised stack too, and is not compared.
//
// A mutation this catches: BoostDiagonalRotation (FUN_000846d0) without the absolute value of the difference of
// the two rotations' sizes boosts by more whenever |y| > |x| - about half its 400 cases.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr unsigned kRangeLo = 0x00083190, kRangeHi = 0x00086bb0;

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

#define ShadowSimStepCount I32_AT(0x00234e34)
#define ShadowPlayerCar (**(RigidVehicle ***)0x00234e40)
#define ShadowGetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int slot))0x000b2730)
#define ShadowSim ((void *)0x00233ff0)

constexpr unsigned kNavSplineSize = 0x70;           // RCameraSpline
uint8_t *const kCurveState = reinterpret_cast<uint8_t *>(0x0023e100);   // GetSegmentCurveStep's spline
constexpr size_t kCurveStateSize = 0x74;
constexpr int kMaxModes = 128;

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

// The original's entry and our port's, both to the fake
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
    uint32_t words[10];
};
constexpr int kMaxLog = 32;
LogEntry g_log[kMaxLog];
int g_logCount;
bool g_loadAnswer;

void Log(uint32_t id, const void *data, size_t size) {
    if (g_logCount >= kMaxLog)
        return;
    LogEntry &e = g_log[g_logCount++];
    memset(&e, 0, sizeof(e));
    e.id = id;
    memcpy(e.words, data, size < sizeof(e.words) ? size : sizeof(e.words));
}

void __fastcall FakeAppendData(void *, int, const RDirectorQueueData *data) {
    Log(1, data, sizeof(RDirectorQueueData));
}

void __fastcall FakeColorize(void *, int, int mode) {
    Log(2, &mode, 4);
}

void __fastcall FakeClearPoints(void *spline, int) {
    Log(3, &spline, 4);
}

bool __fastcall FakeLoadAnimation(void *, int, void *instance) {
    Log(4, &instance, 4);
    return g_loadAnswer;
}

bool __fastcall FakeLoadFromList(void *, int, uint32_t id) {
    Log(5, &id, 4);
    return g_loadAnswer;
}

bool __fastcall FakeLoadAIPath(void *, int, void *path) {
    Log(6, &path, 4);
    return g_loadAnswer;
}

void __fastcall FakeGetSafePosition(RPlayerCamera *camera, int, Coord4 *position, float *height) {
    const WRoadNav *nav = camera->roadNav;
    position->x = nav->position.x;
    position->y = *height + 1.25f;
    position->z = nav->position.z;
    position->w = 1.0f;
    Log(7, position, sizeof(Coord4));
}

void InstallFakes() {
    HookBoth(0x0007c6a0, XbeAddress(&RDirectorQueue::AppendData), (const void *)&FakeAppendData);
    HookBoth(0x0009a500, XbeAddress(&RColorize::SetEnabled), (const void *)&FakeColorize);
    HookBoth(0x0007a9f0, XbeAddress(&RCameraSpline::ClearSplinePtList), (const void *)&FakeClearPoints);
    HookBoth(0x00097770, XbeAddress(&RWorldCamera::LoadSingleAnimation), (const void *)&FakeLoadAnimation);
    HookBoth(0x00097870, XbeAddress(&RWorldCamera::LoadSingleAnimationFromList), (const void *)&FakeLoadFromList);
    HookBoth(0x000977d0, XbeAddress(&RWorldCamera::LoadAISplinePathAnimation), (const void *)&FakeLoadAIPath);
    HookBoth(0x00081110, XbeAddress(&RPlayerCamera::GetSafeWRoadNavPosition), (const void *)&FakeGetSafePosition);
}

// ---- the world outside the camera that a call can write

SimRandom *Random() { return *reinterpret_cast<SimRandom **>(0x00233ff0); }

struct CameraGlobals {
    int32_t previousArm;        // 0x001ec378
    float armTurn;              // 0x001ec37c
    Coord4 armFrom;             // 0x001c4180
    float armBlend;             // 0x001c4190
    float previousRotation[2];  // 0x001ec3e0
    Coord4 shakeOrigin;         // 0x001ec3a0
};

void ReadGlobals(CameraGlobals *g) {
    memcpy(&g->previousArm, (void *)0x001ec378, 4);
    memcpy(&g->armTurn, (void *)0x001ec37c, 4);
    memcpy(&g->armFrom, (void *)0x001c4180, 16);
    memcpy(&g->armBlend, (void *)0x001c4190, 4);
    memcpy(g->previousRotation, (void *)0x001ec3e0, 8);
    memcpy(&g->shakeOrigin, (void *)0x001ec3a0, 16);
}

void WriteGlobals(const CameraGlobals *g) {
    memcpy((void *)0x001ec378, &g->previousArm, 4);
    memcpy((void *)0x001ec37c, &g->armTurn, 4);
    memcpy((void *)0x001c4180, &g->armFrom, 16);
    memcpy((void *)0x001c4190, &g->armBlend, 4);
    memcpy((void *)0x001ec3e0, g->previousRotation, 8);
    memcpy((void *)0x001ec3a0, &g->shakeOrigin, 16);
}

uint8_t *g_carAudio;            // the player car's GetAudio(): its flags at +0xc2 and +0xc3
HeliArmInfo *g_cinematicArm;    // the cinematic heli's first arm, NULL without one

struct World {
    std::vector<uint8_t> arms;
    uint8_t modes[kMaxModes * sizeof(CameraModeInfo)];
    uint8_t heliArm[sizeof(HeliArmInfo)];
    CameraGlobals globals;
    SimRandom random;
    uint32_t queryStamp;
    std::vector<uint32_t> stamps;
    uint8_t curve[kCurveStateSize];
    uint8_t audio[2];
};

size_t ArmsBytes() { return size_t(fgCameraTables.autoDriveArmCount) * sizeof(AutoDriveArmInfo); }
size_t ModesBytes() {
    int count = fgCameraTables.modeCount + 1;
    if (count > kMaxModes)
        count = kMaxModes;
    return size_t(count) * sizeof(CameraModeInfo);
}

void Take(World *w) {
    w->arms.resize(ArmsBytes());
    if (!w->arms.empty())
        memcpy(w->arms.data(), fgCameraTables.autoDriveArms, w->arms.size());
    memset(w->modes, 0, sizeof(w->modes));
    memcpy(w->modes, fgCameraTables.modes, ModesBytes());
    memset(w->heliArm, 0, sizeof(w->heliArm));
    if (g_cinematicArm != NULL)
        memcpy(w->heliArm, g_cinematicArm, sizeof(w->heliArm));
    ReadGlobals(&w->globals);
    w->random = *Random();
    w->queryStamp = fgRoadNetworkData.queryStamp;
    w->stamps.resize(fgRoadNetworkData.loaded ? size_t(fgRoadNetworkData.segmentCount) : 0);
    for (size_t i = 0; i < w->stamps.size(); i++)
        w->stamps[i] = fgRoadNetworkData.segments[i]->queryStamp;
    memcpy(w->curve, kCurveState, sizeof(w->curve));
    memset(w->audio, 0, sizeof(w->audio));
    if (g_carAudio != NULL)
        memcpy(w->audio, g_carAudio + 0xc2, 2);
}

void Put(const World &w) {
    if (!w.arms.empty())
        memcpy(fgCameraTables.autoDriveArms, w.arms.data(), w.arms.size());
    memcpy(fgCameraTables.modes, w.modes, ModesBytes());
    if (g_cinematicArm != NULL)
        memcpy(g_cinematicArm, w.heliArm, sizeof(w.heliArm));
    WriteGlobals(&w.globals);
    *Random() = w.random;
    fgRoadNetworkData.queryStamp = w.queryStamp;
    for (size_t i = 0; i < w.stamps.size(); i++)
        fgRoadNetworkData.segments[i]->queryStamp = w.stamps[i];
    memcpy(kCurveState, w.curve, sizeof(w.curve));
    if (g_carAudio != NULL)
        memcpy(g_carAudio + 0xc2, w.audio, 2);
}

// ---- the cases

enum Op {
    kLimit, kZoom, kEllipse, kInitTransition, kUpdateTransition, kShake, kSetMode, kNextMode, kPrevMode, kDirector,
    kCurrentArm, kBoost, kRotationX, kRotationY, kFixedTrigger, kCinematic, kSpline, kFixed, kWeaponAnims,
    kAutoDrive, kOpCount,
};
const char *const kOpNames[kOpCount] = {
    "LimitPitchYaw", "SetCameraZoom", "AdjustCamAroundObjectEllipse", "InitTransition", "UpdateTransition",
    "ShakeCamera", "SetCameraModeByIndex", "NextCameraMode", "PrevCameraMode", "DirectorChangeCameraMode",
    "UpdateCurrentArm", "FUN_000846d0", "SetAutoDriveRotationX", "SetAutoDriveRotationY", "TriggerFixedCamera",
    "SetCinematicCamera", "UpdateSplineCam", "UpdateFixedCam", "UpdateADWeaponAnims", "UpdateAutoDriveCam",
};

// The scratch objects a run works on
alignas(16) uint8_t g_camera[sizeof(RPlayerCamera)];
uint8_t g_state[sizeof(RPlayerCamState)];
alignas(16) uint8_t g_nav[sizeof(WRoadNav)];
alignas(16) uint8_t g_navSpline[kNavSplineSize];
struct Scratch {
    AutoDriveArmInfo arm;
    Coord4 a, b;
    Coord4 lockOn;
    float x, y;
    uint8_t data[sizeof(RDirectorQueueData)];
};
alignas(16) Scratch g_scratch;

struct Case {
    Op op;
    alignas(16) uint8_t camera[sizeof(RPlayerCamera)];
    uint8_t state[sizeof(RPlayerCamState)];
    alignas(16) uint8_t nav[sizeof(WRoadNav)];
    alignas(16) uint8_t navSpline[kNavSplineSize];
    Scratch scratch;
    World world;
    int i0, i1, i2, i3;
    float f0;
    RigidBody *body;
    SimpleRigidBody *simpleBody;
    bool loadAnswer;
    bool lockOn;
};

struct Result {
    uint8_t camera[sizeof(RPlayerCamera)];
    uint8_t state[sizeof(RPlayerCamState)];
    uint8_t nav[sizeof(WRoadNav)];
    uint8_t navSpline[kNavSplineSize];
    Scratch scratch;
    World world;
    LogEntry log[kMaxLog];
    int logCount;
    int32_t value;
    bool faulted;
};

RPlayerCamera *Camera() { return reinterpret_cast<RPlayerCamera *>(g_camera); }

typedef bool (__fastcall *LimitFn)(RPlayerCamera *, int, const AutoDriveArmInfo *, float *, float *);
typedef void (__fastcall *ZoomFn)(RPlayerCamera *, int, int, float, int);
typedef bool (__fastcall *EllipseFn)(RPlayerCamera *, int, Coord4 *, RigidBody *, SimpleRigidBody *, int);
typedef void (__fastcall *InitTransitionFn)(RPlayerCamera *, int, char, Coord4 *, Coord4 *, int);
typedef void (__fastcall *UpdateTransitionFn)(RPlayerCamera *, int, Coord4 *, Coord4 *);
typedef void (__fastcall *ShakeFn)(RPlayerCamera *, int, int, float, Coord4 *, int);
typedef void (__fastcall *SetModeFn)(RPlayerCamera *, int, int, int, int, int, int, void *, const Coord4 *);
typedef void (__fastcall *ModeStepFn)(RPlayerCamera *, int, int);
typedef void (__fastcall *DirectorFn)(RPlayerCamera *, int, void *);
typedef void (__fastcall *PlainFn)(RPlayerCamera *, int);
typedef void (__fastcall *BoostFn)(float *, float *);
typedef bool (__fastcall *RotationFn)(RPlayerCamera *, int, float);
typedef void (__fastcall *TriggerFn)(RPlayerCamera *, int, Coord4 *, int, int);

int32_t Call(const Case &c) {
    RPlayerCamera *camera = Camera();
    Scratch &s = g_scratch;
    switch (c.op) {
    case kLimit:
        return reinterpret_cast<LimitFn>(0x00083190)(camera, 0, &s.arm, &s.x, &s.y);
    case kZoom:
        reinterpret_cast<ZoomFn>(0x00083940)(camera, 0, c.i0, c.f0, c.i1);
        return 0;
    case kEllipse:
        return reinterpret_cast<EllipseFn>(0x00083b40)(camera, 0, &s.a, c.body, c.simpleBody, 0);
    case kInitTransition:
        reinterpret_cast<InitTransitionFn>(0x00083d70)(camera, 0, char(c.i0), &s.a, &s.b, c.i1);
        return 0;
    case kUpdateTransition:
        reinterpret_cast<UpdateTransitionFn>(0x00083e10)(camera, 0, &s.a, &s.b);
        return 0;
    case kShake:
        reinterpret_cast<ShakeFn>(0x00083e60)(camera, 0, c.i0, c.f0, &s.a, c.i1);
        return 0;
    case kSetMode:
        reinterpret_cast<SetModeFn>(0x00083f50)(camera, 0, c.i0, c.i1, c.i2, c.i3, c.i1 * 3, (void *)0x00012340,
                                                c.lockOn ? &s.a : NULL);
        return 0;
    case kNextMode:
        reinterpret_cast<ModeStepFn>(0x00084030)(camera, 0, c.i0);
        return 0;
    case kPrevMode:
        reinterpret_cast<ModeStepFn>(0x000840b0)(camera, 0, c.i0);
        return 0;
    case kDirector:
        reinterpret_cast<DirectorFn>(0x00084130)(camera, 0, s.data);
        return 0;
    case kCurrentArm:
        reinterpret_cast<PlainFn>(0x00084680)(camera, 0);
        return 0;
    case kBoost:
        reinterpret_cast<BoostFn>(0x000846d0)(&s.x, &s.y);
        return 0;
    case kRotationX:
        return reinterpret_cast<RotationFn>(0x000847f0)(camera, 0, c.f0);
    case kRotationY:
        return reinterpret_cast<RotationFn>(0x00084820)(camera, 0, c.f0);
    case kFixedTrigger:
        reinterpret_cast<TriggerFn>(0x00084850)(camera, 0, &s.a, c.i0, c.i1);
        return 0;
    case kCinematic:
        reinterpret_cast<TriggerFn>(0x000848d0)(camera, 0, &s.a, c.i0, c.i1);
        return 0;
    case kSpline:
        reinterpret_cast<PlainFn>(0x00084a60)(camera, 0);
        return 0;
    case kFixed:
        reinterpret_cast<PlainFn>(0x00085160)(camera, 0);
        return 0;
    case kWeaponAnims:
        reinterpret_cast<PlainFn>(0x000853e0)(camera, 0);
        return 0;
    case kAutoDrive:
        reinterpret_cast<PlainFn>(0x000857d0)(camera, 0);
        return 0;
    default:
        return 0;
    }
}

bool SafeCall(const Case &c, int32_t *value) {
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
    memcpy(g_camera, c.camera, sizeof(g_camera));
    memcpy(g_state, c.state, sizeof(g_state));
    memcpy(g_nav, c.nav, sizeof(g_nav));
    memcpy(g_navSpline, c.navSpline, sizeof(g_navSpline));
    g_scratch = c.scratch;
    RPlayerCamera *camera = Camera();
    camera->state = reinterpret_cast<RPlayerCamState *>(g_state);
    camera->roadNav = reinterpret_cast<WRoadNav *>(g_nav);
    reinterpret_cast<WRoadNav *>(g_nav)->spline = reinterpret_cast<RCameraSpline *>(g_navSpline);
    camera->lockOnPoint = c.lockOn ? &g_scratch.lockOn : NULL;
    Put(c.world);
    g_logCount = 0;
    g_loadAnswer = c.loadAnswer;

    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, true);
    r->faulted = !SafeCall(c, &r->value);
    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, false);

    memcpy(r->camera, g_camera, sizeof(r->camera));
    memcpy(r->state, g_state, sizeof(r->state));
    memcpy(r->nav, g_nav, sizeof(r->nav));
    memcpy(r->navSpline, g_navSpline, sizeof(r->navSpline));
    r->scratch = g_scratch;
    Take(&r->world);
    memcpy(r->log, g_log, sizeof(r->log));
    r->logCount = g_logCount;
}

int g_cases, g_checks, g_differ, g_faults, g_details, g_skipped;
bool g_verbose;     // NIGHTFIRE_PLAYERCAMSHADOWB=2: each case named before it runs
int g_opCases[kOpCount];

void Detail(const Case &c, int index, const char *what, size_t offset, uint32_t original, uint32_t ours) {
    if (g_details++ >= 10)
        return;
    printf("[playercamB]   case %d %s: %s +0x%03x original %08x ours %08x\n", index, kOpNames[c.op], what,
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
        printf("[playercamB] case %d %s\n", index, kOpNames[c.op]);
        fflush(stdout);
    }
    RunOnce(c, &original, true);
    if (g_verbose) {
        printf("[playercamB]   original returned\n");
        fflush(stdout);
    }
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
    // UpdateAutoDriveCam leaves cameraOffset.w as the original's uninitialised stack has it (the port's has it 0)
    size_t offsetW = offsetof(RPlayerCamera, cameraOffset) + 12;
    if (c.op == kAutoDrive)
        memcpy(ours.camera + offsetW, original.camera + offsetW, 4);
    bool differ = false;
    CompareBlock(c, index, "camera", original.camera, ours.camera, sizeof(original.camera), &differ);
    CompareBlock(c, index, "state", original.state, ours.state, sizeof(original.state), &differ);
    CompareBlock(c, index, "navigator", original.nav, ours.nav, sizeof(original.nav), &differ);
    CompareBlock(c, index, "navigator's spline", original.navSpline, ours.navSpline, sizeof(original.navSpline),
                 &differ);
    CompareBlock(c, index, "arguments", &original.scratch, &ours.scratch, sizeof(original.scratch), &differ);
    CompareBlock(c, index, "result", &original.value, &ours.value, sizeof(original.value), &differ);
    const World &a = original.world, &b = ours.world;
    CompareBlock(c, index, "auto-drive arms", a.arms.data(), b.arms.data(), a.arms.size(), &differ);
    CompareBlock(c, index, "modes", a.modes, b.modes, sizeof(a.modes), &differ);
    CompareBlock(c, index, "cinematic heli arm", a.heliArm, b.heliArm, sizeof(a.heliArm), &differ);
    CompareBlock(c, index, "globals", &a.globals, &b.globals, sizeof(a.globals), &differ);
    CompareBlock(c, index, "random", &a.random, &b.random, sizeof(a.random), &differ);
    CompareBlock(c, index, "query stamp", &a.queryStamp, &b.queryStamp, sizeof(a.queryStamp), &differ);
    CompareBlock(c, index, "segment stamps", a.stamps.data(), b.stamps.data(), a.stamps.size() * 4, &differ);
    CompareBlock(c, index, "curve spline", a.curve, b.curve, sizeof(a.curve), &differ);
    CompareBlock(c, index, "car audio", a.audio, b.audio, sizeof(a.audio), &differ);
    CompareBlock(c, index, "calls made", &original.logCount, &ours.logCount, sizeof(original.logCount), &differ);
    CompareBlock(c, index, "calls", original.log, ours.log, sizeof(LogEntry) * size_t(original.logCount), &differ);
    if (differ)
        g_differ++;
}

// ---- building the cases

Case g_case;
const RPlayerCamera *g_liveCamera;

void Start(Op op) {
    g_case.op = op;
    memcpy(g_case.camera, g_liveCamera, sizeof(g_case.camera));
    memcpy(g_case.state, g_liveCamera->state, sizeof(g_case.state));
    memset(g_case.nav, 0, sizeof(g_case.nav));
    memset(g_case.navSpline, 0, sizeof(g_case.navSpline));
    if (g_liveCamera->roadNav != NULL) {
        memcpy(g_case.nav, g_liveCamera->roadNav, sizeof(g_case.nav));
        if (g_liveCamera->roadNav->spline != NULL)
            memcpy(g_case.navSpline, g_liveCamera->roadNav->spline, sizeof(g_case.navSpline));
    }
    memset(&g_case.scratch, 0, sizeof(g_case.scratch));
    g_case.world = g_live;
    g_case.i0 = g_case.i1 = g_case.i2 = g_case.i3 = 0;
    g_case.f0 = 0.0f;
    g_case.body = NULL;
    g_case.simpleBody = NULL;
    g_case.loadAnswer = false;
    g_case.lockOn = false;
}

RPlayerCamera *Edit() { return reinterpret_cast<RPlayerCamera *>(g_case.camera); }
RPlayerCamState *EditState() { return reinterpret_cast<RPlayerCamState *>(g_case.state); }
WRoadNav *EditNav() { return reinterpret_cast<WRoadNav *>(g_case.nav); }
AutoDriveArmInfo *EditArms() { return reinterpret_cast<AutoDriveArmInfo *>(g_case.world.arms.data()); }
CameraGlobals &EditGlobals() { return g_case.world.globals; }

void Jiggle(Coord4 *v, Rng *rng, float amount) {
    v->x += rng->Uniform(-amount, amount);
    v->y += rng->Uniform(-amount, amount);
    v->z += rng->Uniform(-amount, amount);
}

Coord4 RandomVector(Rng *rng, float size) {
    Coord4 v = {rng->Uniform(-size, size), rng->Uniform(-size, size), rng->Uniform(-size, size), 1.0f};
    return v;
}

float Interesting(Rng *rng, float lo, float hi) {
    switch (rng->Range(0, 9)) {
    case 0:
        return 0.0f;
    case 1:
        return -0.0f;
    case 2:
        return lo;
    case 3:
        return hi;
    default:
        return rng->Uniform(lo, hi);
    }
}

int ModeOfType(uint16_t type, int nth) {
    for (int i = 0; i < fgCameraTables.modeCount; i++)
        if (fgCameraTables.modes[i].type == type && nth-- == 0)
            return i;
    return -1;
}

// An arm of random bands: rising yaw limits (some bands of equal limits), pitch limits as sines
void RandomArm(AutoDriveArmInfo *arm, Rng *rng) {
    memset(arm, 0, sizeof(*arm));
    int bands = rng->Range(0, kAutoDriveLimits);
    float yaw = rng->Uniform(-0.5f, -0.1f);
    for (int i = 0; i < bands; i++) {
        arm->minYaw[i] = yaw;
        yaw += rng->Chance(20) ? 0.0f : rng->Uniform(0.01f, 0.25f);
        arm->maxYaw[i] = rng->Chance(15) ? arm->minYaw[i] : yaw;
        if (arm->maxYaw[i] == 0.0f)
            arm->maxYaw[i] = 0.001f;
        arm->minPitch[i] = rng->Uniform(-0.99f, 0.3f);
        arm->maxPitch[i] = rng->Uniform(-0.3f, 0.99f);
    }
    if (rng->Chance(10)) {
        arm->minYaw[0] = 0.0f;
        arm->maxYaw[0] = 0.0f;
    }
    if (rng->Chance(5)) {
        arm->minPitch[0] = 0.0f;
        arm->maxPitch[0] = 0.0f;
    }
    if (bands == 0) {
        arm->minPitch[0] = rng->Uniform(-0.9f, 0.0f);
        arm->maxPitch[0] = rng->Uniform(0.0f, 0.9f);
    }
}

void PerturbAutoDrive(Rng *rng, int armCount) {
    RPlayerCamera *camera = Edit();
    RPlayerCamState *state = EditState();
    camera->autoDriveArm = int8_t(rng->Chance(10) ? -1 : rng->Range(0, armCount - 1));
    camera->modeChangeFlags = rng->Chance(30) ? 1u : rng->Chance(10) ? 5u : 0u;
    camera->autoDriveForwardLock = rng->Chance(8);
    camera->autoDriveRotationX = Interesting(rng, -1.0f, 1.0f);
    camera->autoDriveRotationY = Interesting(rng, -1.0f, 1.0f);
    camera->unknown290 = Interesting(rng, -1.0f, 1.0f);
    camera->unknown294 = Interesting(rng, -1.0f, 1.0f);
    camera->unknown1F0 = rng->Uniform(-0.5f, 0.5f);
    camera->unknown1F4 = rng->Uniform(-0.5f, 0.5f);
    camera->aimPitch = rng->Uniform(-0.3f, 0.3f);
    camera->aimYaw = rng->Uniform(-0.3f, 0.3f);
    camera->targetAngleX = rng->Uniform(-0.4f, 0.4f);
    camera->targetAngleY = rng->Uniform(-1.2f, 1.2f);
    camera->autoDriveZoom = rng->Chance(50) ? 0.0f : rng->Uniform(-3.0f, 3.0f);
    camera->zoom298 = rng->Uniform(10.0f, 90.0f);
    camera->zoomSlope = rng->Uniform(0.1f, 1.0f);
    camera->fieldOfView = rng->Uniform(10.0f, 90.0f);
    camera->weaponFired = rng->Chance(40) ? ShadowSimStepCount - rng->Range(0, 90) : 0;
    camera->spinState = rng->Chance(25) ? rng->Range(-1, 30) : 0;
    camera->unknown13C = rng->Chance(40) ? rng->Range(1, 30) : 0;
    camera->transitionActive = uint8_t(rng->Range(0, 1));
    camera->adWeaponFlag = rng->Chance(40);
    camera->adWeaponAnimState = ShadowSimStepCount - rng->Range(0, 60);
    camera->previousCameraMode = rng->Chance(15) && fgCameraModeIndices.missile >= 0 ? fgCameraModeIndices.missile
                                                                                     : camera->previousCameraMode;
    Jiggle(&camera->cameraOffset, rng, 2.0f);
    Jiggle(&camera->eye, rng, 2.0f);
    g_case.lockOn = rng->Chance(30);
    if (g_case.lockOn) {
        g_case.scratch.lockOn = camera->lookAt;
        Jiggle(&g_case.scratch.lockOn, rng, 40.0f);
        camera->lockOnTarget = RandomVector(rng, 3.0f);
        camera->lockOnParam2 = rng->Range(1, 40);
        camera->lockOnParam1 = rng->Range(0, camera->lockOnParam2);
    }
    state->aiming = rng->Chance(35);
    state->unknown0B = rng->Chance(20);
    state->unknown0D = rng->Chance(20);
    CameraGlobals &g = EditGlobals();
    g.previousArm = rng->Chance(20) ? -1 : rng->Range(0, armCount - 1);
    g.armTurn = rng->Chance(30) ? 0.0f : rng->Uniform(-1.0f, 1.0f);
    g.armBlend = rng->Uniform(0.0f, 1.0f);
    g.previousRotation[0] = rng->Uniform(-1.0f, 1.0f);
    g.previousRotation[1] = rng->Uniform(-1.0f, 1.0f);
    AutoDriveArmInfo *arms = EditArms();
    for (int i = 0; i < armCount; i++) {
        AutoDriveArmInfo &arm = arms[i];
        if (rng->Chance(30)) {
            arm.hasRestPos = true;
            arm.pitch = arm.restPitch + rng->Uniform(-0.1f, 0.1f);
            arm.restStartStep = ShadowSimStepCount - rng->Range(0, 120);
            arm.restPitchChange = rng->Uniform(-0.2f, 0.2f);
        }
        if (rng->Chance(30))
            arm.preserveTransform = !arm.preserveTransform;
        if (rng->Chance(20))
            arm.allowDeadzone = !arm.allowDeadzone;
        if (rng->Chance(15))
            arm.lockArmToCar = !arm.lockArmToCar;
    }
}

// Whether a copy of the live navigator finds a segment at the point. UpdateSplineCam places its navigator at the
// eye, the anchor or ahead of it; where none is found the original's IncNavPosition reads a segment at index -1
// (faulting, or never returning), as ours does. The world the probe writes is put back.
bool NavFindsSegment(const Coord4 *point, Coord4 *heading) {
    alignas(16) uint8_t navBytes[sizeof(WRoadNav)];
    alignas(16) uint8_t splineBytes[kNavSplineSize];
    memcpy(navBytes, g_case.nav, sizeof(navBytes));
    memcpy(splineBytes, g_case.navSpline, sizeof(splineBytes));
    WRoadNav *nav = reinterpret_cast<WRoadNav *>(navBytes);
    nav->spline = reinterpret_cast<RCameraSpline *>(splineBytes);
    nav->InitAtPoint(reinterpret_cast<const Coord3 *>(point), reinterpret_cast<const Coord3 *>(heading), false);
    bool found = nav->segment >= 0 && nav->segment < fgRoadNetworkData.segmentCount;
    Put(g_live);
    return found;
}

// The points the spline camera may place its navigator at this case
bool SplinePointsOnRoads() {
    alignas(16) uint8_t cameraBytes[sizeof(RPlayerCamera)];
    memcpy(cameraBytes, g_case.camera, sizeof(cameraBytes));
    RPlayerCamera *camera = reinterpret_cast<RPlayerCamera *>(cameraBytes);
    MATRIX4 *anchorMatrix = camera->GetAnchorMatrix4();
    const Coord3 *anchorPosition = camera->GetAnchorPosition();
    if (anchorMatrix == NULL || anchorPosition == NULL)
        return false;
    Coord4 heading = *MatrixRow(anchorMatrix, 2);
    Coord4 anchor = {anchorPosition->x, anchorPosition->y, anchorPosition->z, 1.0f};
    Coord4 ahead = {anchor.x + 6.0f * heading.x, anchor.y + 6.0f * heading.y, anchor.z + 6.0f * heading.z, 1.0f};
    return NavFindsSegment(&camera->eye, &heading) && NavFindsSegment(&anchor, &heading) &&
           NavFindsSegment(&ahead, &heading);
}

// Without a mode change UpdateSplineCam moves its navigator on from where it is. On a curve of no length (a
// navigator never placed: Reset leaves both ends zero) the original's IncNavPosition divides the step by zero and
// loops forever on the NaN, as ours does.
bool NavCurveHasLength(const WRoadNav *nav) {
    return nav->boundStart.x != nav->boundEnd.x || nav->boundStart.y != nav->boundEnd.y ||
           nav->boundStart.z != nav->boundEnd.z;
}

// The case's navigator placed at the eye, facing the anchor's way, as a mode change places it
void PlaceNavAtEye() {
    alignas(16) uint8_t cameraBytes[sizeof(RPlayerCamera)];
    memcpy(cameraBytes, g_case.camera, sizeof(cameraBytes));
    RPlayerCamera *camera = reinterpret_cast<RPlayerCamera *>(cameraBytes);
    MATRIX4 *anchorMatrix = camera->GetAnchorMatrix4();
    if (anchorMatrix == NULL)
        return;
    WRoadNav *nav = EditNav();
    nav->spline = reinterpret_cast<RCameraSpline *>(g_case.navSpline);
    nav->InitAtPoint(reinterpret_cast<const Coord3 *>(&camera->eye),
                     reinterpret_cast<const Coord3 *>(MatrixRow(anchorMatrix, 2)), false);
    Put(g_live);
}

// A table of three arms for the auto-drive cases when the track's camera file has none
alignas(16) AutoDriveArmInfo g_syntheticArms[3];

void MakeSyntheticArms(Rng *rng) {
    for (int i = 0; i < 3; i++) {
        AutoDriveArmInfo &arm = g_syntheticArms[i];
        RandomArm(&arm, rng);
        Coord4 identity = {0.0f, 0.0f, 0.0f, 1.0f};
        arm.rotation = arm.rotationFrom = arm.rotationTo = identity;
        arm.relPos.x = rng->Uniform(-1.0f, 1.0f);
        arm.relPos.y = rng->Uniform(1.0f, 3.0f);
        arm.relPos.z = rng->Uniform(-6.0f, -3.0f);
        arm.maxDeadzonePitch = 0.1f;
        arm.maxDeadzoneYaw = 0.1f;
        arm.maxAutoaimPitch = 0.1f;
        arm.maxAutoaimYaw = 0.1f;
        arm.autoaimInterpolSpeed = 0.3f;
        arm.allowDeadzone = i != 1;
        arm.aimFov = 30.0f;
        arm.minFov = 20.0f + 5.0f * i;
        arm.preserveTransform = i != 2;
        arm.hasRestPos = i == 0;
        arm.lockArmToCar = i == 2;
        arm.restInterpolFallScale = 50.0f;
        arm.normCursorMoveSpeed = 0.05f;
        arm.normCursorEndMoveSpeed = 0.03f;
        arm.targetCursorMoveSpeed = 0.04f;
        arm.targetCursorEndMoveSpeed = 0.02f;
        arm.anchor.offset.x = 0.0f;
        arm.anchor.offset.y = 1.0f;
        arm.anchor.offset.z = 0.5f;
    }
}

}  // namespace

void PlayerCamShadowB_Run(void) {
    const char *setting = getenv("NIGHTFIRE_PLAYERCAMSHADOWB");
    if (setting == NULL || atoi(setting) == 0)
        return;
    g_verbose = atoi(setting) >= 2;
    if (fgRenderHigh == NULL || fgRenderHigh->views[0].camera == NULL || fgCameraTables.modes == NULL) {
        printf("[playercamB] no player camera yet - skipped\n");
        fflush(stdout);
        return;
    }
    g_liveCamera = fgRenderHigh->views[0].camera;
    Rng rng = {0x5eedb0bu};
    int modes = fgCameraTables.modeCount;

    // The auto-drive arms: the track's, or three made up for the run
    AutoDriveArmInfo *liveArms = fgCameraTables.autoDriveArms;
    int liveArmCount = fgCameraTables.autoDriveArmCount;
    if (liveArms == NULL || liveArmCount == 0) {
        MakeSyntheticArms(&rng);
        fgCameraTables.autoDriveArms = reinterpret_cast<AutoDriveArmInfo *>(g_syntheticArms);
        fgCameraTables.autoDriveArmCount = 3;
    }
    int armCount = fgCameraTables.autoDriveArmCount;

    g_carAudio = NULL;
    if (ShadowPlayerCar != NULL) {
        typedef uint8_t *(RigidVehicle::*GetAudioMethod)();
        RigidVehicle *car = ShadowPlayerCar;
        g_carAudio = (car->*XbeVirtual<GetAudioMethod>(car, 7))();
    }
    g_cinematicArm = NULL;
    if (fgCameraModeIndices.cinematicHeli >= 1 && fgCameraTables.helis != NULL)
        g_cinematicArm = fgCameraTables.helis[fgCameraModeIndices.cinematicHeli - 1].arms;

    // GetSegmentCurveStep's first call constructs its spline (an allocation): made here, once, for both runs
    if (fgRoadNetworkData.loaded && fgRoadNetworkData.segmentCount > 0 &&
        !(*reinterpret_cast<uint32_t *>(kCurveState + 0x70) & 1)) {
        WRoadSegment *segment = fgRoadNetworkData.segments[0];
        Coord3 point;
        WRoadNetwork::Get()->GetSegmentCurveStep(&fgRoadNetworkData.nodes[segment->node[0]]->position,
                                                 &fgRoadNetworkData.nodes[segment->node[1]]->position, segment,
                                                 0.5f, &point);
    }

    Take(&g_live);
    InstallFakes();

    // LimitPitchYaw: the track's arms and random ones, pitches and yaws round their limits
    for (int i = 0; i < 1500; i++) {
        Start(kLimit);
        if (i < armCount * 40)
            memcpy(&g_case.scratch.arm, &EditArms()[i % armCount], sizeof(g_case.scratch.arm));
        else
            RandomArm(&g_case.scratch.arm, &rng);
        const AutoDriveArmInfo &arm = g_case.scratch.arm;
        int band = rng.Range(0, kAutoDriveLimits - 1);
        g_case.scratch.y = rng.Chance(20) ? arm.minYaw[band] : rng.Chance(20) ? arm.maxYaw[band]
                                                                              : Interesting(&rng, -1.6f, 1.6f);
        g_case.scratch.x = Interesting(&rng, -0.4f, 0.4f);
        if (rng.Chance(2))
            g_case.scratch.y = NAN;
        RunCase(g_case);
    }

    // SetCameraZoom: each kind, with and without steps
    for (int i = 0; i < 300; i++) {
        Start(kZoom);
        RPlayerCamera *camera = Edit();
        camera->cameraMode = rng.Range(0, modes - 1);
        camera->autoDriveArm = int8_t(rng.Range(0, armCount - 1));
        camera->fieldOfView = Interesting(&rng, 1.0f, 100.0f);
        camera->zoom298 = rng.Uniform(5.0f, 90.0f);
        camera->zoom29C = rng.Uniform(5.0f, 90.0f);
        camera->autoDriveZoom = Interesting(&rng, -2.0f, 2.0f);
        camera->zoom2B0 = rng.Uniform(-20.0f, 80.0f);
        camera->zoomSlope = rng.Uniform(0.0f, 1.0f);
        g_case.i0 = rng.Range(0, 4);
        g_case.f0 = g_case.i0 == 2 ? Interesting(&rng, -0.5f, 1.5f) : Interesting(&rng, -5.0f, 80.0f);
        g_case.i1 = rng.Chance(50) ? 0 : rng.Range(-3, 60);
        RunCase(g_case);
    }

    // AdjustCamAroundObjectEllipse: points in and round every body
    for (int slot = 0; slot < 64; slot++) {
        PhysicsObject *owner = PhysicsObjects[slot];
        if (owner == NULL || (owner->flags & PhysicsObject::kSimpleBody))
            continue;
        RigidBody *body = owner->GetRigidBody();
        for (int i = 0; i < 12; i++) {
            Start(kEllipse);
            g_case.body = body;
            Coord4 at = {body->position.x, body->position.y, body->position.z, 1.0f};
            Jiggle(&at, &rng, i < 6 ? 2.0f : 6.0f);
            g_case.scratch.a = at;
            RunCase(g_case);
        }
    }
    for (int slot = 0; slot < 96; slot++) {
        if (SimpleBodyOwners[slot] == NULL)
            continue;
        SimpleRigidBody *body = ShadowGetSimpleRigidBody(ShadowSim, 0, slot);
        for (int i = 0; i < 6; i++) {
            Start(kEllipse);
            g_case.simpleBody = body;
            Coord4 at = {body->position.x, body->position.y, body->position.z, 1.0f};
            Jiggle(&at, &rng, body->radius * 3.0f + 0.5f);
            g_case.scratch.a = at;
            RunCase(g_case);
        }
    }

    // InitTransition and UpdateTransition
    for (int i = 0; i < 200; i++) {
        Start(kInitTransition);
        g_case.scratch.a = RandomVector(&rng, 20.0f);
        g_case.scratch.b = rng.Chance(10) ? g_case.scratch.a : RandomVector(&rng, 20.0f);
        g_case.i0 = rng.Range(0, 2);
        g_case.i1 = rng.Chance(30) ? 0 : rng.Range(-2, 90);
        RunCase(g_case);
    }
    for (int i = 0; i < 200; i++) {
        Start(kUpdateTransition);
        RPlayerCamera *camera = Edit();
        camera->transitionVec = RandomVector(&rng, 1.0f);
        camera->transitionVec.w = rng.Uniform(0.0f, 30.0f);
        camera->transitionFactor = rng.Uniform(0.0f, 2.0f);
        camera->unknown13C = rng.Range(-1, 60);
        g_case.scratch.a = RandomVector(&rng, 20.0f);
        g_case.scratch.b = RandomVector(&rng, 20.0f);
        RunCase(g_case);
    }

    // ShakeCamera
    for (int i = 0; i < 200; i++) {
        Start(kShake);
        RPlayerCamera *camera = Edit();
        camera->cameraMode = rng.Range(0, modes - 1);
        camera->shakeStepsLeft = rng.Range(-2, 120);
        camera->shakeAmount = rng.Uniform(0.0f, 1.0f);
        g_case.scratch.a = camera->eye;
        Jiggle(&g_case.scratch.a, &rng, 60.0f);
        g_case.i0 = rng.Range(-1, 5);
        g_case.f0 = Interesting(&rng, 0.0f, 80.0f);
        g_case.i1 = rng.Range(0, 1);
        RunCase(g_case);
    }

    // The mode changes queued: SetCameraModeByIndex, NextCameraMode, PrevCameraMode, UpdateCurrentArm,
    // TriggerFixedCamera, SetCinematicCamera
    for (int i = 0; i < 120; i++) {
        Start(kSetMode);
        g_case.i0 = rng.Range(-2, modes + 1);
        g_case.i1 = rng.Range(0, 0x1ffff);
        g_case.i2 = rng.Range(0, 0xff);
        g_case.i3 = rng.Range(0, 0x1ffff);
        g_case.lockOn = rng.Chance(50);
        g_case.scratch.a = RandomVector(&rng, 5.0f);
        RunCase(g_case);
    }
    for (int i = 0; i < modes * 6 + 12; i++) {
        Start(i % 2 ? kNextMode : kPrevMode);
        Edit()->cameraMode = (i / 2) % (modes + 1);
        g_case.i0 = rng.Range(0, 0x1ffff);
        RunCase(g_case);
    }
    for (int i = 0; i < 60; i++) {
        Start(kCurrentArm);
        Edit()->cameraMode = rng.Range(0, modes - 1);
        Edit()->currentArm = int8_t(rng.Range(-1, 3));
        RunCase(g_case);
    }
    for (int i = 0; i < 60; i++) {
        Start(kFixedTrigger);
        g_case.scratch.a = RandomVector(&rng, 50.0f);
        g_case.i0 = rng.Range(-1, fgCameraTables.fixedCount + 1);
        g_case.i1 = rng.Range(0, 0x1ffff);
        RunCase(g_case);
    }
    for (int i = 0; i < 120; i++) {
        Start(kCinematic);
        Coord4 place = {rng.Uniform(1.0f, 40.0f), Interesting(&rng, -720.0f, 720.0f),
                        Interesting(&rng, -180.0f, 180.0f), 1.0f};
        g_case.scratch.a = place;
        g_case.i0 = rng.Range(0, 0x1ffff);
        g_case.i1 = rng.Range(0, 0x1ffff);
        RunCase(g_case);
    }

    // DirectorChangeCameraMode: every mode, each with the flags, positions and answers that steer it
    const uint16_t flagSets[] = {0, 0x10, 0x40, 0x80, 0xd0, 0x04};
    for (int mode = -1; mode <= modes; mode++) {
        for (int i = 0; i < 18; i++) {
            Start(kDirector);
            RPlayerCamera *camera = Edit();
            camera->cameraMode = rng.Chance(15) && fgCameraModeIndices.missile >= 0 ? fgCameraModeIndices.missile
                                                                                    : rng.Range(0, modes - 1);
            if (rng.Chance(25))
                camera->cameraMode = mode < 0 ? 0 : mode;
            camera->previousCameraMode = rng.Range(0, modes - 1);
            camera->currentArm = int8_t(rng.Range(-1, 2));
            EditState()->unknown20 = rng.Chance(20);
            RDirectorQueueData *data = reinterpret_cast<RDirectorQueueData *>(g_case.scratch.data);
            data->position = RandomVector(&rng, 0.6f);
            if (rng.Chance(20))
                data->position.x = -1.0f;
            data->position.w = rng.Chance(50) ? 1.0f : 0.0f;
            data->delay = uint16_t(rng.Range(0, 30));
            data->cameraMode = uint16_t(mode < 0 ? 0xffff : mode);
            data->unknown14 = uint16_t(rng.Range(0, 0x1ff));
            data->flags = flagSets[i % 6];
            data->data18 = reinterpret_cast<CARP::Instance *>(uintptr_t(0x00012340));
            data->data1C = (void *)0x00056780;
            data->unknown24 = uint32_t(rng.Range(0, 1000));
            g_case.loadAnswer = (i % 3) != 0;
            RunCase(g_case);
        }
    }

    // FUN_000846d0 and the auto-drive rotations
    for (int i = 0; i < 400; i++) {
        Start(kBoost);
        g_case.scratch.x = Interesting(&rng, -1.2f, 1.2f);
        g_case.scratch.y = Interesting(&rng, -1.2f, 1.2f);
        RunCase(g_case);
    }
    for (int i = 0; i < 200; i++) {
        Start(i % 2 ? kRotationX : kRotationY);
        RPlayerCamera *camera = Edit();
        camera->cameraMode = rng.Range(0, modes - 1);
        camera->autoDriveRotationX = Interesting(&rng, -1.0f, 1.0f);
        camera->autoDriveRotationY = Interesting(&rng, -1.0f, 1.0f);
        camera->autoDriveRotating = rng.Chance(50);
        g_case.f0 = Interesting(&rng, -1.0f, 1.0f);
        RunCase(g_case);
    }

    // The spline cameras, starting on roads of several lanes
    if (fgRoadNetworkData.loaded && g_liveCamera->roadNav != NULL) {
        for (int n = 0;; n++) {
            int mode = ModeOfType(kCameraSpline, n);
            if (mode < 0)
                break;
            for (int i = 0; i < 40; i++) {
                Start(kSpline);
                RPlayerCamera *camera = Edit();
                camera->cameraMode = mode;
                camera->modeChangeFlags = rng.Chance(25) ? 1u : rng.Chance(10) ? 5u : 0u;
                camera->unknown13C = rng.Chance(40) ? rng.Range(1, 30) : 0;
                camera->transitionActive = uint8_t(rng.Range(0, 1));
                Jiggle(&camera->eye, &rng, 10.0f);
                Jiggle(&camera->unknown2D0, &rng, 2.0f);
                Jiggle(&camera->lookAt, &rng, rng.Chance(20) ? 80.0f : 3.0f);
                WRoadNav *nav = EditNav();
                bool steady = !(camera->modeChangeFlags & RWorldCamera::kAnchorChanged);
                if (steady && !NavCurveHasLength(nav))
                    PlaceNavAtEye();
                if (steady && !NavCurveHasLength(nav)) {
                    g_skipped++;
                    continue;
                }
                if (nav->segment < 0 || nav->segment >= fgRoadNetworkData.segmentCount) {
                    g_skipped++;
                    continue;
                }
                WRoadSegment *segment = fgRoadNetworkData.segments[nav->segment];
                if (int8_t(segment->leftLanes + segment->rightLanes) <= 1 && !(camera->modeChangeFlags & 1)) {
                    g_skipped++;
                    continue;
                }
                if (!SplinePointsOnRoads()) {
                    g_skipped++;
                    continue;
                }
                RunCase(g_case);
            }
        }
    }

    // The fixed cameras
    for (int n = 0;; n++) {
        int mode = ModeOfType(kCameraFixed, n);
        if (mode < 0)
            break;
        for (int i = 0; i < 30; i++) {
            Start(kFixed);
            RPlayerCamera *camera = Edit();
            camera->cameraMode = mode;
            camera->modeChangeFlags = rng.Chance(30) ? 1u : rng.Chance(10) ? 4u : 0u;
            camera->unknown13C = rng.Chance(40) ? rng.Range(1, 30) : 0;
            camera->transitionActive = uint8_t(rng.Range(0, 1));
            camera->fixedCamPosition = RandomVector(&rng, 30.0f);
            Jiggle(&camera->eye, &rng, 10.0f);
            Jiggle(MatrixRow(&camera->matrix, 3), &rng, 2.0f);
            RunCase(g_case);
        }
    }

    // UpdateADWeaponAnims and UpdateAutoDriveCam
    int autoDriveMode = fgCameraModeIndices.autoDrive >= 0 ? fgCameraModeIndices.autoDrive : 0;
    for (int i = 0; i < 300; i++) {
        Start(i % 3 == 0 ? kWeaponAnims : kAutoDrive);
        PerturbAutoDrive(&rng, armCount);
        Edit()->cameraMode = rng.Chance(80) ? autoDriveMode : rng.Range(0, modes - 1);
        RunCase(g_case);
    }

    HooksRemove();
    if (liveArms == NULL || liveArmCount == 0) {
        fgCameraTables.autoDriveArms = liveArms;
        fgCameraTables.autoDriveArmCount = liveArmCount;
    }

    printf("[playercamB] RPlayerCamera (0x00083190-0x00086bb0): %d cases, %d checks, %d differ (%d faulted, %d "
           "skipped)\n", g_cases, g_checks, g_differ, g_faults, g_skipped);
    printf("[playercamB]   by function:");
    for (int op = 0; op < kOpCount; op++)
        printf(" %s %d%s", kOpNames[op], g_opCases[op], op + 1 < kOpCount ? "," : "\n");
    fflush(stdout);
}
