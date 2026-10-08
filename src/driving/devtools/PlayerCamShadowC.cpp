#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "PlayerCamShadowC.h"

#include "../camera/PlayerCamera.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../engine/UMemory.hpp"
#include "../world/Collider.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_PLAYERCAMSHADOWC=1, once on the first simulation tick: camera/PlayerCameraC.cpp against the originals.
//
// Each case starts from a copy of the live player camera (CameraViews[0]), perturbed, with its RPlayerCamState
// and director queue swapped for copies of their own and no collider. The case runs twice from the same bytes -
// first with 0x00086bb0-0x00089b70's originals swapped back in, then with our jumps - calling the original
// address both times, and the two runs' camera, state, queue, the camera globals they write (the shake position,
// the heli camera's ground probe, the aim matrix), the in/out vector and the result are compared byte for byte.
// A collider the run made is compared by its region (its lists hold heap pointers) and freed.
//
//   - GetAimMatrix4 with the early-out scales and random ones, random aim angles;
//   - CheckForCameraShaking with shake counts and amounts round zero, both shake styles, every mode's scale;
//   - CheckObjectCollisions at points round the player's car and the camera;
//   - ResolveAllCollisions from random camera points to random targets near the anchor, with and without the
//     objects, in every mode (heli modes take the ground probe);
//   - UpdateMomentumHeliCam, UpdateTumbleCam and UpdateEllipseCam in each mode that uses them, with mode-change
//     flags, transitions, the smoothing state and the camera's place perturbed.
// The rest (construction, restart, the mode and animation triggers) changes the director's queue and is tested
// in game by the lockstep runs.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr unsigned kRangeLo = 0x00086bb0, kRangeHi = 0x00089b70;

struct Rng {
    uint32_t state;
    uint32_t Next() {
        state = state * 1664525u + 1013904223u;
        return state >> 8;
    }
    float Uniform(float lo, float hi) { return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f; }
    int Range(int lo, int hi) { return lo + int(Next() % uint32_t(hi - lo + 1)); }
};

#define ShadowPlayerObject (**(PhysicsObject ***)0x00234e40)

// The camera globals the functions write
struct CameraGlobals {
    Coord4 shakePosition;       // 0x001ec3a0
    int32_t groundCount;        // 0x001ec3e8
    Coord4 groundPoint;         // 0x001ec3f0
    MATRIX4 aimMatrix;          // 0x001ec400
};

void ReadGlobals(CameraGlobals *g) {
    memcpy(&g->shakePosition, (void *)0x001ec3a0, 16);
    memcpy(&g->groundCount, (void *)0x001ec3e8, 4);
    memcpy(&g->groundPoint, (void *)0x001ec3f0, 16);
    memcpy(&g->aimMatrix, (void *)0x001ec400, 64);
}

void WriteGlobals(const CameraGlobals *g) {
    memcpy((void *)0x001ec3a0, &g->shakePosition, 16);
    memcpy((void *)0x001ec3e8, &g->groundCount, 4);
    memcpy((void *)0x001ec3f0, &g->groundPoint, 16);
    memcpy((void *)0x001ec400, &g->aimMatrix, 64);
}

enum Op { kAim, kShake, kObjects, kResolve, kHeli, kTumble, kEllipse };
const char *const kOpNames[] = {"GetAimMatrix4", "CheckForCameraShaking", "CheckObjectCollisions",
                                "ResolveAllCollisions", "UpdateMomentumHeliCam", "UpdateTumbleCam",
                                "UpdateEllipseCam"};

struct Case {
    Op op;
    alignas(16) uint8_t camera[sizeof(RPlayerCamera)];
    uint8_t state[0x34];
    uint8_t queue[0x14];
    CameraGlobals globals;
    Coord4 a, b;
    float x, y;
    bool flag;
};

struct Result {
    uint8_t camera[sizeof(RPlayerCamera)];
    uint8_t state[0x34];
    uint8_t queue[0x14];
    CameraGlobals globals;
    Coord4 a;
    int32_t value;
    uint8_t collider[0x30];
    bool hadCollider;
    bool faulted;
};

alignas(16) uint8_t g_camera[sizeof(RPlayerCamera)];
uint8_t g_state[0x34];
uint8_t g_queue[0x14];
Coord4 g_a, g_b;

RPlayerCamera *Scratch() { return reinterpret_cast<RPlayerCamera *>(g_camera); }

typedef MATRIX4 *(__fastcall *AimFn)(RPlayerCamera *, int, float, float);
typedef void (__fastcall *MethodFn)(RPlayerCamera *, int);
typedef bool (__fastcall *ObjectsFn)(RPlayerCamera *, int, Coord4 *);
typedef int (__fastcall *ResolveFn)(RPlayerCamera *, int, Coord4 *, const Coord4 *, bool);

int32_t Call(const Case &c) {
    RPlayerCamera *camera = Scratch();
    switch (c.op) {
    case kAim: {
        MATRIX4 *m = reinterpret_cast<AimFn>(0x000876b0)(camera, 0, c.x, c.y);
        return m == &camera->aimMatrix ? 1 : m == &camera->matrix ? 2 : m == (MATRIX4 *)0x001ec400 ? 3 : 4;
    }
    case kShake:
        reinterpret_cast<MethodFn>(0x000873e0)(camera, 0);
        return 0;
    case kObjects:
        return reinterpret_cast<ObjectsFn>(0x00086bb0)(camera, 0, &g_a) ? 1 : 0;
    case kResolve:
        return reinterpret_cast<ResolveFn>(0x00086d00)(camera, 0, &g_a, &g_b, c.flag);
    case kHeli:
        reinterpret_cast<MethodFn>(0x00087880)(camera, 0);
        return 0;
    case kTumble:
        reinterpret_cast<MethodFn>(0x00088380)(camera, 0);
        return 0;
    case kEllipse:
        reinterpret_cast<MethodFn>(0x000886f0)(camera, 0);
        return 0;
    }
    return 0;
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
    memcpy(g_queue, c.queue, sizeof(g_queue));
    RPlayerCamera *camera = Scratch();
    camera->state = reinterpret_cast<RPlayerCamState *>(g_state);
    camera->directorQueue = reinterpret_cast<RDirectorQueue *>(g_queue);
    camera->collider = NULL;
    WriteGlobals(&c.globals);
    g_a = c.a;
    g_b = c.b;

    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, true);
    r->faulted = !SafeCall(c, &r->value);
    if (original)
        XbeOriginal_RestoreRange(kRangeLo, kRangeHi, false);

    memset(r->collider, 0, sizeof(r->collider));
    r->hadCollider = camera->collider != NULL;
    if (camera->collider != NULL) {
        memcpy(r->collider, camera->collider, sizeof(r->collider));
        camera->collider->Destruct();
        UMemory::FastFree(camera->collider, sizeof(WCollider));
        camera->collider = NULL;
    }
    memcpy(r->camera, g_camera, sizeof(r->camera));
    memcpy(r->state, g_state, sizeof(r->state));
    memcpy(r->queue, g_queue, sizeof(r->queue));
    ReadGlobals(&r->globals);
    r->a = g_a;
}

int g_cases, g_checks, g_differ, g_faults, g_details;

void Detail(const Case &c, int index, const char *what, size_t offset, uint32_t original, uint32_t ours) {
    if (g_details++ >= 10)
        return;
    printf("[playercamC]   case %d %s: %s +0x%03x original %08x ours %08x\n", index, kOpNames[c.op], what,
           unsigned(offset), original, ours);
}

void CompareBlock(const Case &c, int index, const char *what, const void *a, const void *b, size_t size,
                  bool *differ) {
    g_checks++;
    if (memcmp(a, b, size) == 0)
        return;
    *differ = true;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        uint32_t x, y;
        memcpy(&x, static_cast<const uint8_t *>(a) + i, 4);
        memcpy(&y, static_cast<const uint8_t *>(b) + i, 4);
        if (x != y) {
            Detail(c, index, what, i, x, y);
            break;
        }
    }
}

void RunCase(const Case &c, const CameraGlobals &live) {
    static Result original, ours;
    int index = g_cases++;
    RunOnce(c, &original, true);
    RunOnce(c, &ours, false);
    WriteGlobals(&live);
    if (original.faulted || ours.faulted) {
        g_faults++;
        if (original.faulted != ours.faulted) {
            g_differ++;
            Detail(c, index, "fault", 0, original.faulted, ours.faulted);
        }
        return;
    }
    // The heli, tumble and ellipse cameras set cameraOffset from locals whose w the original never writes: its w is
    // that stack's garbage (the port's is 0)
    size_t offsetW = offsetof(RPlayerCamera, cameraOffset) + 12;
    memcpy(ours.camera + offsetW, original.camera + offsetW, 4);
    bool differ = false;
    CompareBlock(c, index, "camera", original.camera, ours.camera, sizeof(original.camera), &differ);
    CompareBlock(c, index, "state", original.state, ours.state, sizeof(original.state), &differ);
    CompareBlock(c, index, "queue", original.queue, ours.queue, sizeof(original.queue), &differ);
    CompareBlock(c, index, "globals", &original.globals, &ours.globals, sizeof(original.globals), &differ);
    CompareBlock(c, index, "vector", &original.a, &ours.a, sizeof(original.a), &differ);
    CompareBlock(c, index, "result", &original.value, &ours.value, sizeof(original.value), &differ);
    CompareBlock(c, index, "has collider", &original.hadCollider, &ours.hadCollider, 1, &differ);
    CompareBlock(c, index, "collider", original.collider, ours.collider, sizeof(original.collider), &differ);
    if (differ)
        g_differ++;
}

// ---- building the cases

Case g_case;

void Start(Op op, const RPlayerCamera *live, const CameraGlobals &globals) {
    memset(&g_case, 0, sizeof(g_case));
    g_case.op = op;
    memcpy(g_case.camera, live, sizeof(g_case.camera));
    memcpy(g_case.state, live->state, sizeof(g_case.state));
    memcpy(g_case.queue, live->directorQueue, sizeof(g_case.queue));
    g_case.globals = globals;
}

RPlayerCamera *Edit() { return reinterpret_cast<RPlayerCamera *>(g_case.camera); }

void Jiggle(Coord4 *v, Rng *rng, float amount) {
    v->x += rng->Uniform(-amount, amount);
    v->y += rng->Uniform(-amount, amount);
    v->z += rng->Uniform(-amount, amount);
}

Coord4 Near(const Coord4 &centre, Rng *rng, float across, float up) {
    Coord4 v = centre;
    v.x += rng->Uniform(-across, across);
    v.y += rng->Uniform(-1.0f, up);
    v.z += rng->Uniform(-across, across);
    return v;
}

bool UsesUpdate(int mode, unsigned address) {
    return fgCameraTables.modes[mode].update == address;
}

}  // namespace

void PlayerCamShadowC_Run(void) {
    const char *setting = getenv("NIGHTFIRE_PLAYERCAMSHADOWC");
    if (setting == NULL || atoi(setting) == 0)
        return;
    if (CameraViews == NULL || CameraViews[0].camera == NULL || fgCameraTables.modes == NULL) {
        printf("[playercamC] no player camera yet - skipped\n");
        fflush(stdout);
        return;
    }
    const RPlayerCamera *live = CameraViews[0].camera;
    CameraGlobals globals;
    ReadGlobals(&globals);
    Rng rng = {0x5eed0c3u};
    int modes = fgCameraTables.modeCount;
    Coord4 anchorAt = live->lookAt;

    // GetAimMatrix4
    const float scales[][2] = {{1, 1}, {0, 1}, {1, 0}, {0, 0}, {0.5f, 1}, {1, 0.5f}};
    for (int i = 0; i < 60; i++) {
        Start(kAim, live, globals);
        Edit()->aimPitch = rng.Uniform(-0.4f, 0.4f);
        Edit()->aimYaw = rng.Uniform(-0.4f, 0.4f);
        if (i < 12) {
            g_case.x = scales[i % 6][0];
            g_case.y = scales[i % 6][1];
        } else {
            g_case.x = rng.Uniform(-1.5f, 1.5f);
            g_case.y = rng.Uniform(-1.5f, 1.5f);
        }
        RunCase(g_case, globals);
    }

    // CheckForCameraShaking
    const int shakes[] = {-1, 0, 1, 2, 5, 40};
    for (int i = 0; i < 48; i++) {
        Start(kShake, live, globals);
        Edit()->shakeStepsLeft = shakes[i % 6];
        Edit()->shakeAmount = i % 3 == 0 ? 0.0f : rng.Uniform(0.0f, 2.0f);
        Edit()->shaking = (i / 6) % 2 != 0;
        Edit()->cameraMode = rng.Range(0, modes - 1);
        Jiggle(&g_case.globals.shakePosition, &rng, 3.0f);
        RunCase(g_case, globals);
    }

    // CheckObjectCollisions
    Coord4 car = anchorAt;
    if (ShadowPlayerObject != NULL) {
        const Coord3 *position = ShadowPlayerObject->GetPosition();
        car.x = position->x;
        car.y = position->y;
        car.z = position->z;
    }
    for (int i = 0; i < 40; i++) {
        Start(kObjects, live, globals);
        g_case.a = Near(i % 2 ? car : live->eye, &rng, i < 20 ? 3.0f : 12.0f, 4.0f);
        g_case.a.w = 1.0f;
        RunCase(g_case, globals);
    }

    // ResolveAllCollisions
    for (int i = 0; i < 120; i++) {
        Start(kResolve, live, globals);
        Edit()->cameraMode = i < 20 ? live->cameraMode : rng.Range(0, modes - 1);
        g_case.b = Near(anchorAt, &rng, 1.0f, 1.5f);
        g_case.b.w = 1.0f;
        g_case.a = Near(g_case.b, &rng, i % 4 == 0 ? 25.0f : 8.0f, 6.0f);
        g_case.a.w = 1.0f;
        g_case.flag = (i & 1) != 0;
        g_case.globals.groundCount = rng.Range(0, 80);
        RunCase(g_case, globals);
    }

    // The heli, tumble and ellipse cameras, in every mode that uses one
    const unsigned updates[] = {0x00087880, 0x00088380, 0x000886f0};
    const Op ops[] = {kHeli, kTumble, kEllipse};
    const uint32_t flags[] = {0, 1, 2, 8, 9};
    for (int u = 0; u < 3; u++) {
        for (int mode = 0; mode < modes; mode++) {
            if (!UsesUpdate(mode, updates[u]))
                continue;
            for (int i = 0; i < 25; i++) {
                Start(ops[u], live, globals);
                RPlayerCamera *camera = Edit();
                camera->cameraMode = mode;
                camera->currentArm = 0;
                if (ops[u] == kTumble)
                    camera->previousCameraMode = rng.Range(0, modes);
                camera->modeChangeFlags = flags[i % 5];
                camera->transitionActive = uint8_t(rng.Range(0, 1));
                camera->transitionStep = uint8_t(rng.Range(0, 1));
                camera->unknown13C = rng.Range(0, 1) ? rng.Range(1, 12) : 0;
                camera->unknown144 = (i % 7) == 3;
                camera->unknown26C = rng.Range(0, 1) ? rng.Range(1, 20) : 0;
                camera->unknown228 = rng.Uniform(0.0f, 15.0f);
                camera->unknown25C = rng.Uniform(0.0f, 1.0f);
                camera->unknown258 = rng.Uniform(-0.5f, 0.5f);
                camera->unknown264 = rng.Range(0, 400);
                camera->unknown268 = rng.Range(0, 8);
                camera->lastSelectableCameraMode = rng.Range(0, modes - 1);
                camera->zoomSlope = rng.Uniform(0.5f, 2.0f);
                Jiggle(&camera->eye, &rng, 4.0f);
                Jiggle(&camera->cameraOffset, &rng, 4.0f);
                Jiggle(&camera->upVector, &rng, 0.3f);
                Jiggle(&camera->unknown230, &rng, 0.5f);
                reinterpret_cast<int32_t *>(g_case.state)[0x30 / 4] = rng.Range(0, 3);
                RunCase(g_case, globals);
            }
        }
    }

    printf("[playercamC] RPlayerCamera (0x00086bb0-0x00089b70): %d cases, %d checks, %d differ (%d faulted)\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
