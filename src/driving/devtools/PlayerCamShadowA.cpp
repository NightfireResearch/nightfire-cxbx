#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "PlayerCamShadowA.h"

#include "../camera/CameraIniLoader.h"
#include "../camera/CameraSpline.h"
#include "../camera/DirectorQueue.h"
#include "../camera/PlayerCamState.h"
#include "../camera/PlayerCamera.h"
#include "../camera/PlayerCameraA.h"
#include "../engine/MissionManager.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../world/Targeting.h"
#include "../world/WorldPos.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_PLAYERCAMSHADOWA=1, once on the first simulation tick: camera/PlayerCameraA.cpp against the originals.
//
// Two passes run the same cases, calling every function at its original address: the first with the originals
// swapped back in (0x00080a60-0x00083190), the second with our jumps. Each case starts from a fresh copy of the
// live player camera (CameraViews[0]) with its own copies of the camera state, the director's queue and the world
// position, a new spline, and perturbations drawn from the case's seed (mode, mode-change flags, positions,
// eased values, inputs, look-back, zoom arm, the simulation state); the globals a case changes are put back after
// it. Each case logs what the function answered, the whole camera (64 bytes a line), the state, the queue's flags,
// the world position and the spline; the two logs must match line for line.
//
//   - the free functions on random vectors and values (PointDir, PointInTriangle, the arc cosine and sine, the
//     clamp), with NaNs and the limits;
//   - every small method, over every mode of the tuning file;
//   - the bumper and dashboard cameras in every mode (their own and others'), looking back or not, stopped or not;
//   - the AI path camera on a made-up path; the world and relative animation cameras on a made-up 'Cams' instance
//     with and without a made-up animation, through the spline blend (the animation engine itself is not reached);
//   - UpdateADTargetAngles with a made-up selected target round the camera, held or not, the auto-aim option and
//     the mission manager's scale varied.
// Shutdown, the destructor, TriggerCarAnimationCamera (heap and queue) and CameraInputCallback (the state's
// handlers act on the live camera) are left to the game.
//
// Where the original copies its own stack into the camera (eye.w after the bumper camera and the dashboard's
// look-back with a mode change, cameraOffset.w after the dashboard's; the port writes zero), those words are
// masked. The AI spline path's address (on this function's stack) is masked in the camera's dump.
// ---------------------------------------------------------------------------------------------------------------

namespace {

int g_cases, g_faults;
bool g_counting;
std::string *g_log;

void Logf(const char *format, ...) {
    if (g_log == NULL)
        return;
    char line[1024];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    g_log->append(line);
    g_log->push_back('\n');
}

void LogBytes(const char *what, const void *p, size_t n) {
    const uint8_t *bytes = static_cast<const uint8_t *>(p);
    for (size_t offset = 0; offset < n; offset += 64) {
        std::string s = what;
        char h[16];
        snprintf(h, sizeof(h), " +%03x ", unsigned(offset));
        s += h;
        for (size_t i = offset; i < n && i < offset + 64; i++) {
            snprintf(h, sizeof(h), "%02x", bytes[i]);
            s += h;
        }
        Logf("%s", s.c_str());
    }
}

void Case() {
    if (g_counting)
        g_cases++;
}

uint32_t Bits(float f) {
    uint32_t u;
    memcpy(&u, &f, sizeof(u));
    return u;
}

unsigned long long Bits(double d) {
    unsigned long long u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f; }
    int Range(int lo, int hi) { return lo + int(Next() % uint32_t(hi - lo + 1)); }
    bool Chance(int percent) { return int(Next() % 100u) < percent; }
};

float QuietNaN() {
    uint32_t bits = 0x7fc00000u;
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

// ---- what the tests read and write of the game

struct ShadowWeaponManager {
    uint32_t unknown00;
    int32_t current;                    // +0x04
};

// RPlayerCamState's bytes the port reads beyond PlayerCamState.h's
struct ShadowStateBytes {
    uint8_t bytes[0x34];
};

#define ShadowSimState (*(int32_t *)0x00234e24)
#define ShadowAutoAimOption (*(int32_t *)0x002445d0)
#define ShadowWeaponManagerPtr (*(ShadowWeaponManager **)0x0023923c)
#define ShadowPlayerCameraVtable 0x001918ac

// ---- the functions, at their original addresses

typedef void (__fastcall *MethodFn)(RPlayerCamera *, int);
typedef double (__fastcall *DoubleMethodFn)(RPlayerCamera *, int);
typedef uint8_t (__fastcall *ByteMethodFn)(RPlayerCamera *, int);
typedef void (__fastcall *SafePositionFn)(RPlayerCamera *, int, Coord4 *, float *);
typedef int8_t (__fastcall *FindArmFn)(RPlayerCamera *, int, int);
typedef uint8_t (__fastcall *SetAnchorFn)(RPlayerCamera *, int, RDirectorQueueData *);
typedef void (__fastcall *BoolMethodFn)(RPlayerCamera *, int, bool);
typedef double (__fastcall *RotationFn)(RPlayerCamera *, int, float);
typedef void (__fastcall *LockOnFn)(RPlayerCamera *, int, int, const Coord4 *, int, int);
typedef void (__fastcall *FloatMethodFn)(RPlayerCamera *, int, float);
typedef const Coord4 *(__fastcall *AimVecFn)(RPlayerCamera *, int, float);
typedef void (__fastcall *IntMethodFn)(RPlayerCamera *, int, int);
typedef int (*MaxTumbleFn)();
typedef uint8_t (__fastcall *TargetAnglesFn)(RPlayerCamera *, int, float *, float *);
typedef double (*PointDirFn)(int, const Coord4 *, const Coord4 *, const Coord4 *);
typedef uint8_t (*PointInTriangleFn)(const Coord4 *, const Coord4 *, const Coord4 *, const Coord4 *);
typedef double (*TurnsFn)(float);
typedef double (*ClampFn)(float, float, float);
typedef RCameraSpline *(__fastcall *SplineConstructFn)(RCameraSpline *, int);
typedef void (__fastcall *SplineDestructFn)(RCameraSpline *, int);

#define AT(type, address) ((type)(address))

// ---- the camera copy

alignas(16) uint8_t g_cameraBytes[sizeof(RPlayerCamera)];
alignas(16) uint8_t g_stateBytes[sizeof(RPlayerCamState)];
alignas(16) uint8_t g_queueBytes[sizeof(RDirectorQueue)];
alignas(16) uint8_t g_worldPosBytes[sizeof(WWorldPos)];
RCameraSpline *g_spline;
RPlayerCamera *g_live;

RPlayerCamera *Camera() {
    return reinterpret_cast<RPlayerCamera *>(g_cameraBytes);
}

ShadowStateBytes *State() {
    return reinterpret_cast<ShadowStateBytes *>(g_stateBytes);
}

// The globals a case may change, put back after it
struct SavedGlobals {
    int32_t simState;
    int32_t autoAim;
    uint8_t handsLock;
    int32_t previousArm;
    int32_t weapon;
    uint8_t missionFlag;
    int32_t missionScale;
    WTargetable *selected;
    Coord4 aimStatic;

    void Save() {
        simState = ShadowSimState;
        autoAim = ShadowAutoAimOption;
        handsLock = CameraLockOnFlag;
        previousArm = fgCameraTables.previousAutoDriveArm;
        weapon = ShadowWeaponManagerPtr != NULL ? ShadowWeaponManagerPtr->current : 0;
        missionFlag = glbMissionManager->unknown478;
        memcpy(&missionScale, reinterpret_cast<uint8_t *>(glbMissionManager) + 0x4dc, 4);
        selected = TargetPicker.selected;
        memcpy(&aimStatic, reinterpret_cast<void *>(0x001ec3d0), sizeof(aimStatic));
    }
    void Restore() const {
        ShadowSimState = simState;
        ShadowAutoAimOption = autoAim;
        CameraLockOnFlag = handsLock;
        fgCameraTables.previousAutoDriveArm = previousArm;
        if (ShadowWeaponManagerPtr != NULL)
            ShadowWeaponManagerPtr->current = weapon;
        glbMissionManager->unknown478 = missionFlag;
        memcpy(reinterpret_cast<uint8_t *>(glbMissionManager) + 0x4dc, &missionScale, 4);
        TargetPicker.selected = selected;
        memcpy(reinterpret_cast<void *>(0x001ec3d0), &aimStatic, sizeof(aimStatic));
    }
};
SavedGlobals g_saved;

void MakeSpline() {
    g_spline = static_cast<RCameraSpline *>(UMemory::FastAlloc(sizeof(RCameraSpline), "RCameraSpline"));
    AT(SplineConstructFn, 0x0007acd0)(g_spline, 0);
}

void FreeSpline() {
    AT(SplineDestructFn, 0x0007a9b0)(g_spline, 0);
    UMemory::FastFree(g_spline, sizeof(RCameraSpline));
    g_spline = NULL;
}

float Jitter(Rng *rng, float value, float amount) {
    return value + rng->Uniform(-amount, amount);
}

void JitterVector(Rng *rng, Coord4 *v, float amount) {
    v->x = Jitter(rng, v->x, amount);
    v->y = Jitter(rng, v->y, amount);
    v->z = Jitter(rng, v->z, amount);
}

// A fresh copy of the live camera, perturbed from the seed. `mode` < 0: a random mode.
void Prepare(Rng *rng, int mode) {
    memcpy(g_cameraBytes, g_live, sizeof(g_cameraBytes));
    memcpy(g_stateBytes, g_live->state, sizeof(g_stateBytes));
    memcpy(g_queueBytes, g_live->directorQueue, sizeof(g_queueBytes));
    memcpy(g_worldPosBytes, g_live->worldPos, sizeof(g_worldPosBytes));
    RPlayerCamera *camera = Camera();
    camera->state = reinterpret_cast<RPlayerCamState *>(g_stateBytes);
    camera->directorQueue = reinterpret_cast<RDirectorQueue *>(g_queueBytes);
    camera->worldPos = reinterpret_cast<WWorldPos *>(g_worldPosBytes);
    MakeSpline();
    camera->spline = g_spline;

    int modes = fgCameraTables.modeCount;
    camera->cameraMode = mode >= 0 ? mode : rng->Range(0, modes - 1);
    camera->previousCameraMode = rng->Range(0, modes);         // (one past the table at times)
    static const uint32_t kFlagSets[] = {0, 1, 2, 4, 8, 1 | 4, 1 | 8, 2 | 8};
    camera->modeChangeFlags = kFlagSets[rng->Range(0, 7)];
    JitterVector(rng, &camera->eye, 2.0f);
    JitterVector(rng, &camera->lookAt, 2.0f);
    JitterVector(rng, &camera->lookAtOffset, 1.0f);
    JitterVector(rng, &camera->cameraOffset, 1.0f);
    JitterVector(rng, &camera->unknown50, 3.0f);
    JitterVector(rng, &camera->unknown240, 0.2f);
    camera->unknown240.w = rng->Uniform(-0.1f, 0.1f);
    camera->unknown250 = rng->Uniform(-0.3f, 0.3f);
    camera->unknown254 = rng->Uniform(-0.3f, 0.3f);
    camera->unknown258 = rng->Uniform(-0.06f, 0.06f);
    camera->unknown25C = rng->Uniform(0.0f, 1.0f);
    camera->fieldOfView = rng->Uniform(10.0f, 80.0f);
    camera->zoomFov = rng->Uniform(20.0f, 60.0f);
    camera->zoomFovTarget = rng->Uniform(20.0f, 60.0f);
    if (fgCameraTables.autoDriveArmCount > 0)
        camera->autoDriveArm = int8_t(rng->Range(0, fgCameraTables.autoDriveArmCount - 1));
    ShadowStateBytes *state = State();
    state->bytes[0x0a] = uint8_t(rng->Chance(30));          // lookBackOff
    state->bytes[0x0b] = uint8_t(rng->Chance(40));          // held target
    state->bytes[0x0c] = uint8_t(rng->Chance(40));          // aiming
    state->bytes[0x21] = uint8_t(rng->Chance(40));          // looking back
    float steer = rng->Uniform(-1.0f, 1.0f), glance = rng->Uniform(-1.0f, 1.0f);
    memcpy(&state->bytes[0x24], &steer, 4);
    memcpy(&state->bytes[0x28], &glance, 4);
    if (rng->Chance(20))
        ShadowSimState = 3;
}

// Logs the camera, state, queue flags, world position and spline, then frees the spline and puts the globals
// back. `mask` zeroes the words the original fills from its own stack.
enum MaskWords {
    kMaskNone = 0,
    kMaskEyeW = 1,
    kMaskOffsetW = 2,
    kMaskLookAtW = 4,
};

void Finish(int mask) {
    RPlayerCamera *camera = Camera();
    if (mask & kMaskEyeW)
        camera->eye.w = 0.0f;
    if (mask & kMaskOffsetW)
        camera->cameraOffset.w = 0.0f;
    if (mask & kMaskLookAtW)
        camera->lookAt.w = 0.0f;
    RCameraSpline *spline = camera->spline;
    camera->spline = NULL;
    // the made-up path and animation are on the test's stack, which may sit elsewhere in the other pass
    Logf("made-up %d %d", camera->aiSplinePath != NULL, camera->animHandle != NULL);
    camera->aiSplinePath = NULL;
    camera->animHandle = NULL;
    LogBytes("cam", g_cameraBytes, sizeof(g_cameraBytes));
    LogBytes("state", g_stateBytes, sizeof(g_stateBytes));
    Logf("queue %02x", reinterpret_cast<RDirectorQueue *>(g_queueBytes)->flags);
    LogBytes("worldpos", g_worldPosBytes, sizeof(g_worldPosBytes));
    if (spline != NULL) {
        LogBytes("spline", spline, offsetof(RCameraSpline, pointList));
        Logf("spline points %u", spline->pointList.size);
    }
    Logf("globals %02x %d %d", CameraLockOnFlag, fgCameraTables.previousAutoDriveArm,
         ShadowWeaponManagerPtr != NULL ? ShadowWeaponManagerPtr->current : 0);
    FreeSpline();
    g_saved.Restore();
}

// One call, a fault counted rather than fatal
template <class F>
void Guard(const char *what, F call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        Logf("fault in %s", what);
    }
#else
    call();
#endif
}

// ---- the free functions

void RandomVector(Rng *rng, Coord4 *v, float range) {
    v->x = rng->Uniform(-range, range);
    v->y = rng->Uniform(-range, range);
    v->z = rng->Uniform(-range, range);
    v->w = 1.0f;
}

void TestFree(Rng *rng) {
    for (int i = 0; i < 600; i++) {
        Case();
        Coord4 p, a, b, c;
        RandomVector(rng, &p, 10.0f);
        RandomVector(rng, &a, 10.0f);
        RandomVector(rng, &b, 10.0f);
        RandomVector(rng, &c, 10.0f);
        if (i % 9 == 0)
            p.x = QuietNaN();
        if (i % 13 == 0)
            b = a;                      // a degenerate triangle
        if (i % 3 == 0) {               // near the plane, inside often
            p.x = (a.x + b.x + c.x) / 3.0f + rng->Uniform(-1.0f, 1.0f);
            p.y = (a.y + b.y + c.y) / 3.0f;
            p.z = (a.z + b.z + c.z) / 3.0f + rng->Uniform(-1.0f, 1.0f);
        }
        double d[3];
        uint8_t inside = 0;
        Guard("PointDir", [&] {
            for (int axis = 0; axis < 3; axis++)
                d[axis] = AT(PointDirFn, 0x00080f50)(axis, &p, &a, &b);
            inside = AT(PointInTriangleFn, 0x00080fe0)(&p, &a, &b, &c);
        });
        Logf("pointdir %016llx %016llx %016llx in %u", Bits(d[0]), Bits(d[1]), Bits(d[2]), inside);
    }
    static const float kEdges[] = {1.0f, -1.0f, 0.0f, -0.0f, 1.0000001f, -1.0000001f, 2.0f, -3.0f, 0.5f};
    for (int i = 0; i < 400; i++) {
        Case();
        float x = i < 9 ? kEdges[i] : rng->Uniform(-1.2f, 1.2f);
        if (i == 9)
            x = QuietNaN();
        float hi = rng->Uniform(-2.0f, 2.0f), lo = rng->Uniform(-2.0f, 2.0f);
        if (i % 7 == 0)
            lo = QuietNaN();
        double ac = 0.0, as = 0.0, cl = 0.0;
        Guard("turns", [&] {
            ac = AT(TurnsFn, 0x00081b00)(x);
            as = AT(TurnsFn, 0x00081b60)(x);
            cl = AT(ClampFn, 0x00081bc0)(hi, x, lo);
        });
        Logf("turns %08x: %016llx %016llx clamp %016llx", Bits(x), Bits(ac), Bits(as), Bits(cl));
    }
}

// ---- the small methods

void TestSmall(Rng *rng) {
    int modes = fgCameraTables.modeCount;
    for (int mode = 0; mode < modes; mode++) {
        for (int k = 0; k < 6; k++) {
            g_saved.Save();
            Case();
            Prepare(rng, mode);
            RPlayerCamera *camera = Camera();
            double zoom = 0.0, rotation = 0.0;
            int8_t heli = 0, autoDrive = 0;
            uint8_t smooth = 0, aiming = 0;
            float travel = k == 0 ? 1.0f : k == 1 ? -1.0f : k == 2 ? 0.0f : k == 3 ? QuietNaN() : rng->Uniform(-1.0f, 1.0f);
            if (ShadowWeaponManagerPtr != NULL && k == 5)
                ShadowWeaponManagerPtr->current = -1;
            Guard("small", [&] {
                zoom = AT(DoubleMethodFn, 0x00080e20)(camera, 0);
                AT(MethodFn, 0x00080e80)(camera, 0);
                heli = AT(FindArmFn, 0x000811d0)(camera, 0, mode);
                autoDrive = AT(FindArmFn, 0x00081290)(camera, 0, mode);
                smooth = AT(ByteMethodFn, 0x00081460)(camera, 0);
                rotation = AT(RotationFn, 0x00081540)(camera, 0, travel);
                aiming = AT(ByteMethodFn, 0x00081980)(camera, 0);
            });
            Logf("small %d/%d zoom %016llx arms %d %d smooth %u rotation %016llx aiming %u", mode, k, Bits(zoom),
                 heli, autoDrive, smooth, Bits(rotation), aiming);
            Finish(kMaskNone);

            g_saved.Save();
            Case();
            Prepare(rng, mode);
            bool lookBack = (k & 1) != 0;
            float value = rng->Uniform(-2.0f, 2.0f);
            Guard("setters", [&] {
                AT(BoolMethodFn, 0x000814c0)(camera, 0, lookBack);
                AT(FloatMethodFn, 0x000816e0)(camera, 0, value);
                AT(BoolMethodFn, 0x00081700)(camera, 0, lookBack);
                AT(BoolMethodFn, 0x00081930)(camera, 0, !lookBack);
                AT(BoolMethodFn, 0x00081940)(camera, 0, lookBack);
                AT(MethodFn, 0x000817f0)(camera, 0);
            });
            Logf("setters %d/%d", mode, k);
            Finish(kMaskNone);

            g_saved.Save();
            Case();
            Prepare(rng, mode);
            Guard("inits", [&] {
                AT(MethodFn, 0x00081430)(camera, 0);
                AT(MethodFn, 0x000819a0)(camera, 0);
                AT(MethodFn, 0x00081a20)(camera, 0);
                AT(MethodFn, 0x00081a70)(camera, 0);
                if (k & 2)
                    AT(MethodFn, 0x00081ad0)(camera, 0);
            });
            Logf("inits %d/%d", mode, k);
            Finish(kMaskNone);
        }
    }

    for (int i = 0; i < 160; i++) {
        g_saved.Save();
        Case();
        Prepare(rng, -1);
        RPlayerCamera *camera = Camera();
        int lockState = rng->Range(0, 2);
        int quiet = rng->Range(0, 1);
        camera->lockOnPoint = reinterpret_cast<const Coord4 *>(uintptr_t(rng->Range(0, 1)));
        Coord4 target;
        RandomVector(rng, &target, 50.0f);
        int param = rng->Range(-3, 300);
        float blend = i % 4 == 0 ? 0.0f : i % 4 == 1 ? 1.0f : i % 8 == 2 ? QuietNaN() : rng->Uniform(-0.5f, 1.5f);
        int weapon = i % 3 == 0 ? 0x1c : rng->Range(0, 40);
        glbMissionManager->unknown478 = uint8_t(rng->Range(0, 1));
        if (i % 10 == 0)
            camera->state = NULL;
        Coord4 aim = {};
        int tumble = 0;
        uint8_t aiming = 0;
        Guard("lock", [&] {
            aiming = AT(ByteMethodFn, 0x00081980)(camera, 0);
            if (camera->state == NULL)
                return;
            AT(LockOnFn, 0x00081610)(camera, 0, lockState, &target, param, quiet);
            aim = *AT(AimVecFn, 0x00081890)(camera, 0, blend);
            AT(IntMethodFn, 0x00081950)(camera, 0, weapon);
            tumble = AT(MaxTumbleFn, 0x00081af0)();
        });
        camera->state = reinterpret_cast<RPlayerCamState *>(g_stateBytes);
        Logf("lock %d: aim %08x %08x %08x %08x tumble %d aiming %u", i, Bits(aim.x), Bits(aim.y), Bits(aim.z),
             Bits(aim.w), tumble, aiming);
        Finish(kMaskNone);
    }
}

// ---- the road navigator's safe position, the director's anchor

void TestPositionAnchor(Rng *rng) {
    for (int i = 0; i < 120; i++) {
        g_saved.Save();
        Case();
        Prepare(rng, -1);
        RPlayerCamera *camera = Camera();
        Coord4 position = {};
        float height = g_live->eye.y + rng->Uniform(-20.0f, 20.0f);
        Guard("safe", [&] { AT(SafePositionFn, 0x00081110)(camera, 0, &position, &height); });
        Logf("safe %d: %08x %08x %08x %08x", i, Bits(position.x), Bits(position.y), Bits(position.z), Bits(position.w));
        Finish(kMaskNone);
    }
    for (int i = 0; i < 80; i++) {
        g_saved.Save();
        Case();
        Prepare(rng, -1);
        RPlayerCamera *camera = Camera();
        alignas(16) RDirectorQueueData data;
        memset(&data, 0, sizeof(data));
        data.anchor = i % 5 == 0 ? NULL : g_live->anchor;
        data.unknown14 = uint16_t(rng->Chance(60) ? rng->Range(1, 60) : 0);
        if (rng->Chance(70))
            camera->anchor = NULL;
        if (i % 7 == 0)
            camera->cameraMode = fgCameraModeIndices.missile;
        uint8_t done = 0;
        Guard("anchor", [&] { done = AT(SetAnchorFn, 0x00081340)(camera, 0, &data); });
        Logf("anchor %d: %u", i, done);
        Finish(kMaskNone);
    }
}

// ---- the bumper and dashboard cameras

void TestBumperDashboard(Rng *rng) {
    int modes = fgCameraTables.modeCount;
    for (int mode = 0; mode < modes; mode++) {
        for (int k = 0; k < 8; k++) {
            g_saved.Save();
            Case();
            Prepare(rng, mode);
            RPlayerCamera *camera = Camera();
            int mask = camera->modeChangeFlags != 0 ? kMaskEyeW : kMaskNone;
            Guard("bumper", [&] { AT(MethodFn, 0x00080b90)(camera, 0); });
            Logf("bumper %d/%d", mode, k);
            Finish(mask);

            g_saved.Save();
            Case();
            Prepare(rng, mode);
            mask = camera->modeChangeFlags != 0 ? kMaskEyeW | kMaskOffsetW : kMaskNone;
            Guard("dashboard", [&] { AT(MethodFn, 0x00081d10)(camera, 0); });
            Logf("dashboard %d/%d", mode, k);
            Finish(mask);
        }
    }
}

// ---- the animation cameras, on made-up paths and instances

struct FakePathHandle {
    const float *instance;              // +0x00 a matrix: the frame, row 3 the position
    uint8_t unknown04[0x84];
    Coord3 velocity;                    // +0x88
};

struct FakeAnimHandle {
    uint8_t unknown00[0x40];
    float instance[16];                 // +0x40 a CARP::Instance: the matrix, flags in +0x0c, the article at +0x1c
};

struct FakeAnimKey {
    Coord4 rotation;
    Coord4 position;
};

struct FakeAnimTrack {
    const FakeAnimKey *keys;
    uint32_t unknown04;
    uint16_t keyCount;
    uint8_t unknown0A[6];
};

struct FakeAnimDesc {
    uint8_t unknown00[8];
    const FakeAnimTrack *tracks;
};

void RandomFrame(Rng *rng, float *matrix, const Coord4 *around) {
    Coord4 q;
    RandomVector(rng, &q, 1.0f);
    q.w = rng->Uniform(-1.0f, 1.0f);
    VU0_v4unit(&q, &q);
    VU0_quattom4(matrix, &q);
    matrix[12] = around->x + rng->Uniform(-15.0f, 15.0f);
    matrix[13] = around->y + rng->Uniform(-3.0f, 6.0f);
    matrix[14] = around->z + rng->Uniform(-15.0f, 15.0f);
    matrix[15] = 1.0f;
}

void TestAnimation(Rng *rng) {
    alignas(16) float pathFrame[16];
    alignas(16) FakePathHandle path;
    alignas(16) uint8_t splinePath[0xb0];
    alignas(16) FakeAnimHandle handle;
    alignas(16) FakeAnimKey keys[2];
    FakeAnimTrack track;
    FakeAnimDesc desc;

    for (int i = 0; i < 60; i++) {
        g_saved.Save();
        Case();
        Prepare(rng, -1);
        RPlayerCamera *camera = Camera();
        memset(&path, 0, sizeof(path));
        memset(splinePath, 0, sizeof(splinePath));
        RandomFrame(rng, pathFrame, &g_live->eye);
        path.instance = pathFrame;
        path.velocity.x = rng->Uniform(-30.0f, 30.0f);
        path.velocity.y = rng->Uniform(-3.0f, 3.0f);
        path.velocity.z = rng->Uniform(-30.0f, 30.0f);
        FakePathHandle *pathPointer = &path;
        memcpy(splinePath + 0x60, &pathPointer, sizeof(pathPointer));
        RandomFrame(rng, reinterpret_cast<float *>(splinePath + 0x70), &g_live->eye);
        camera->aiSplinePath = i % 9 == 0 ? NULL : reinterpret_cast<AISplinePath *>(splinePath);
        Guard("aipath", [&] { AT(MethodFn, 0x00080cf0)(camera, 0); });
        Logf("aipath %d", i);
        Finish(kMaskNone);
    }

    for (int i = 0; i < 160; i++) {
        bool relative = i % 2 != 0;
        bool withAnimation = (i / 2) % 3 == 0;
        g_saved.Save();
        Case();
        Prepare(rng, -1);
        RPlayerCamera *camera = Camera();
        memset(&handle, 0, sizeof(handle));
        RandomFrame(rng, handle.instance, &g_live->eye);
        uint8_t *instanceBytes = reinterpret_cast<uint8_t *>(handle.instance);
        instanceBytes[0x0c] = uint8_t(rng->Range(0, 1));            // flags: follows the anchor
        instanceBytes[0x0d] = 0;                                    // procAnimType
        uint32_t article = 0;
        if (withAnimation) {
            for (int key = 0; key < 2; key++) {
                RandomVector(rng, &keys[key].rotation, 1.0f);
                keys[key].rotation.w = rng->Uniform(-1.0f, 1.0f);
                VU0_v4unit(&keys[key].rotation, &keys[key].rotation);
                RandomVector(rng, &keys[key].position, 12.0f);
            }
            track.keys = keys;
            track.unknown04 = 0;
            track.keyCount = uint16_t(rng->Range(1, 3));
            desc.tracks = &track;
            article = uint32_t(reinterpret_cast<uintptr_t>(&desc));
        }
        memcpy(instanceBytes + 0x1c, &article, sizeof(article));
        camera->animHandle = reinterpret_cast<Handle *>(&handle);
        // The animation engine is not reached: the blend runs (unknown13C stays above 0 after its step), or a mode
        // change starts one; without an animation, a finished blend is safe too.
        camera->unknown26C = rng->Range(withAnimation ? 2 : 1, 40);
        camera->transitionFactor = rng->Uniform(0.01f, 0.2f);
        camera->transitionVec.w = rng->Uniform(0.0f, 0.9f);
        camera->unknown260 = rng->Chance(50) ? rng->Uniform(-1.5f, 1.5f) : 0.0f;
        if (rng->Chance(50)) {
            camera->modeChangeFlags = 1 | 4;                        // a mode change, eased
        } else {
            camera->modeChangeFlags = 0;
            camera->unknown13C = rng->Range(withAnimation || relative ? 2 : 0, 30);
        }
        if (i % 11 == 0)
            camera->animHandle = NULL;
        Guard("animation", [&] {
            AT(MethodFn, relative ? 0x00082840 : 0x00082400)(camera, 0);
        });
        Logf("animation %d %s %d", i, relative ? "relative" : "world", withAnimation);
        Finish(kMaskNone);
    }
}

// ---- the auto-drive target angles

void TestTargetAngles(Rng *rng) {
    alignas(16) WTargetable target;
    for (int i = 0; i < 240; i++) {
        g_saved.Save();
        Case();
        Prepare(rng, -1);
        RPlayerCamera *camera = Camera();
        memset(&target, 0, sizeof(target));
        const Coord4 *forward = MatrixRow(&camera->matrix, 2);
        float distance = rng->Uniform(1.0f, 340.0f);
        target.position.x = camera->eye.x + forward->x * distance + rng->Uniform(-0.3f, 0.3f) * distance;
        target.position.y = camera->eye.y + forward->y * distance + rng->Uniform(-0.2f, 0.2f) * distance;
        target.position.z = camera->eye.z + forward->z * distance + rng->Uniform(-0.3f, 0.3f) * distance;
        target.onScreen = uint8_t(rng->Chance(85));
        TargetPicker.selected = i % 13 == 0 ? NULL : &target;
        ShadowAutoAimOption = i % 17 == 0 ? 0 : 1;
        int32_t scale = rng->Range(0, 3);
        memcpy(reinterpret_cast<uint8_t *>(glbMissionManager) + 0x4dc, &scale, 4);
        float pitch = -99.0f, yaw = -99.0f;
        uint8_t found = 0;
        Guard("angles", [&] { found = AT(TargetAnglesFn, 0x00082e00)(camera, 0, &pitch, &yaw); });
        Logf("angles %d: %u %08x %08x", i, found, Bits(pitch), Bits(yaw));
        Finish(kMaskNone);
    }
}

void RunPass(std::string *log, bool original) {
    g_log = log;
    if (original)
        XbeOriginal_RestoreRange(0x00080a60, 0x00083190, true);
    void (*const tests[])(Rng *) = {TestFree, TestSmall, TestPositionAnchor, TestBumperDashboard, TestAnimation,
                                    TestTargetAngles};
    uint32_t seed = 0x3ca7e5u;
    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        Rng rng = {seed + uint32_t(i) * 7919u};
        tests[i](&rng);
    }
    if (original)
        XbeOriginal_RestoreRange(0x00080a60, 0x00083190, false);
    g_log = NULL;
}

void SplitLines(const std::string &text, std::vector<std::string> *lines) {
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos)
            end = text.size();
        lines->push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

}  // namespace

void PlayerCamShadowA_Run(void) {
    const char *setting = getenv("NIGHTFIRE_PLAYERCAMSHADOWA");
    if (setting == NULL || atoi(setting) == 0)
        return;
    if (CameraViews == NULL || CameraViews[0].camera == NULL || fgCameraTables.modes == NULL ||
        *reinterpret_cast<uint32_t *>(CameraViews[0].camera) != ShadowPlayerCameraVtable) {
        printf("[playercamA] no player camera yet - skipped\n");
        fflush(stdout);
        return;
    }
    g_live = CameraViews[0].camera;
    if (g_live->state == NULL || g_live->directorQueue == NULL || g_live->worldPos == NULL ||
        g_live->roadNav == NULL || g_live->anchor == NULL) {
        printf("[playercamA] the player camera is not set up - skipped\n");
        fflush(stdout);
        return;
    }

    std::string original, ours;
    g_counting = true;
    RunPass(&original, true);
    g_counting = false;
    RunPass(&ours, false);

    std::vector<std::string> a, b;
    SplitLines(original, &a);
    SplitLines(ours, &b);
    size_t lines = a.size() > b.size() ? a.size() : b.size();
    int differ = 0;
    for (size_t i = 0; i < lines; i++) {
        const std::string *x = i < a.size() ? &a[i] : NULL;
        const std::string *y = i < b.size() ? &b[i] : NULL;
        if (x != NULL && y != NULL && *x == *y)
            continue;
        if (differ < 10) {
            // the case's name: the nearest line above that is not a dump
            size_t at = i < a.size() ? i : a.size() - 1;
            while (at > 0 && (a[at].compare(0, 4, "cam ") == 0 || a[at].compare(0, 6, "state ") == 0 ||
                              a[at].compare(0, 7, "spline ") == 0 || a[at].compare(0, 9, "worldpos ") == 0 ||
                              a[at].compare(0, 6, "queue ") == 0 || a[at].compare(0, 8, "globals ") == 0 ||
                              a[at].compare(0, 8, "made-up ") == 0))
                at--;
            printf("[playercamA]   line %u (%.60s): original \"%.180s\" ours \"%.180s\"\n", unsigned(i),
                   a.empty() ? "" : a[at].c_str(), x != NULL ? x->c_str() : "(none)", y != NULL ? y->c_str() : "(none)");
        }
        differ++;
    }
    printf("[playercamA] RPlayerCamera 0x80a60-0x83190: %d cases, %u checks, %d differ (%d faults)\n", g_cases,
           unsigned(lines), differ, g_faults);
    fflush(stdout);
}
