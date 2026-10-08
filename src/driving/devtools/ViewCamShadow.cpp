#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#include "ViewCamShadow.h"
#include "FpControl.h"

#include "../camera/Camera.h"
#include "../camera/PlayerCamera.h"       // CameraViews
#include "../camera/WorldCamera.h"
#include "../eagl/View.h"
#include "../engine/CoreContainers.h"
#include "../physics/PhysicsObject.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

#include <windows.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_VIEWCAMSHADOW=1, from the first simulation tick: the camera classes against the originals (every range
// of Camera.cpp and WorldCamera.cpp swapped back in for each original run, common/xbeOriginal.h), on identical
// inputs, compared byte for byte. Each case runs on a scratch state - an RCamera, an RWorldCamera and an RViewCamera
// copied from the live player camera and view, the view pointing at the scratch camera and at a copy of its viewport
// (made inactive so nothing reaches D3D) - with the globals the functions write (GetAnchorMatrix4's static matrix,
// the RRenderWorldCulling, the renderer's word ConfigureView clears) and the ones the cases vary (widescreen, the
// field of view scale, the aspect scale, the perspective sway) put back before each side.
//   - RCamera: the constructors, CreateMatrix4Inv (changed or not), ConvertToRHCS, SetMatrix4, SetActive, on
//     random frames.
//   - RViewCamera: RefreshLODMultiplier, UpdateForResolution, SetZBufferRange, both SetExtents, SetGuardBandSize,
//     SetFillColour, AspectRatio, SetDeviceTransformMode, SetWorldTransformMode (with ApplyPerspectiveFunction),
//     SetViewPortToUnitTransformMode, with random extents, fields of view and renderer settings, the sway on and
//     off; ApplyPerspectiveFunction alone; RPlayerViewCamera::ConfigureView.
//   - RWorldCamera: both SetViewingTransforms, the anchor queries, SetAnchor, SetCameraZoom, AnchorCamera and
//     AnchorRelativeCamera, with every car, shell, missile and grenade as the anchor, none, and copies of the cars
//     with their type, simple-body flag and slot changed (out of range among them).
//   - RRenderWorldCamera::CullModule over random spheres round the eye, with a scripted module (the items it is
//     told about recorded).
//   - FloorToInt (whole numbers, fractions, both signs, huge values, NaN) and TransformPointXZ.
//
// Drawing (DoRender and its parts), the animations, the constructors that allocate and the input queue are
// tested by lockstep runs.
//
// One mutation this catches: FloorToInt without its "- 1" for a negative fraction differs on every negative
// non-whole input.
// ---------------------------------------------------------------------------------------------------------------

namespace {

void OriginalWindow(bool original) {
    XbeOriginal_RestoreRange(0x00013020, 0x00013060, original);
    XbeOriginal_RestoreRange(0x00078430, 0x00078550, original);
    XbeOriginal_RestoreRange(0x0008a3f0, 0x0008a4f0, original);
    XbeOriginal_RestoreRange(0x0008bf20, 0x0008bf30, original);
    XbeOriginal_RestoreRange(0x0008c8e0, 0x0008d180, original);
    XbeOriginal_RestoreRange(0x00096820, 0x000980e0, original);
}

// ---- the originals

typedef RCamera *(__fastcall *CameraConstructFn)(RCamera *, int);
typedef RCamera *(__fastcall *CameraCopyFn)(RCamera *, int, const RCamera *);
typedef void (__fastcall *CameraFn)(RCamera *, int);
typedef void (__fastcall *CameraMatrixFn)(RCamera *, int, const MATRIX4 *);
typedef void (__fastcall *CameraBoolFn)(RCamera *, int, bool);
typedef void (__fastcall *ViewFn)(RViewCamera *, int);
typedef void (__fastcall *ViewRangeFn)(RViewCamera *, int, uint32_t, uint32_t);
typedef double (__fastcall *ViewDoubleFn)(RViewCamera *, int);
typedef void (__fastcall *ViewWordFn)(RViewCamera *, int, uint32_t);
typedef void (__fastcall *ViewFloatFn)(RViewCamera *, int, float);
typedef void (__fastcall *ViewExtentsFn)(RViewCamera *, int, float, float, float, float);
typedef void (__fastcall *ViewOtherFn)(RViewCamera *, int, const RViewCamera *);
typedef void (*UnitModeFn)(EAGL::ViewPort *, float, float);
typedef void (*PerspectiveFn)(float *, float *);
typedef int (*FloorFn)(float);
typedef void (*TransformXZFn)(const Coord3 *, const MATRIX4 *, Coord4 *);
typedef void (__fastcall *ViewingFn)(RWorldCamera *, int, const Coord4 *, const Coord4 *, int);
typedef void (__fastcall *ViewingOffsetFn)(RWorldCamera *, int, const Coord4 *, const float *, const Coord4 *, int);
typedef float (__fastcall *WorldFloatFn)(RWorldCamera *, int);
typedef double (__fastcall *WorldDoubleFn)(RWorldCamera *, int);
typedef void (__fastcall *WorldVectorFn)(RWorldCamera *, int, Coord4 *);
typedef void *(__fastcall *WorldPointerFn)(RWorldCamera *, int);
typedef int (__fastcall *WorldIntFn)(RWorldCamera *, int);
typedef void (__fastcall *WorldAnchorFn)(RWorldCamera *, int, PhysicsObject *);
typedef void (__fastcall *WorldZoomFn)(RWorldCamera *, int, float, float);
typedef void (__fastcall *WorldAnchorCameraFn)(RWorldCamera *, int, bool, const Coord3 *);
typedef void (__fastcall *CullModuleFn)(RRenderWorldCamera *, int, CullNextItem, CullSetItem, bool);

#define Orig_RCamera_Construct ((CameraConstructFn)0x00078430)
#define Orig_RCamera_ConstructCopy ((CameraCopyFn)0x00096980)
#define Orig_RCamera_CreateMatrix4Inv ((CameraFn)0x000784a0)
#define Orig_RCamera_ConvertToRHCS ((CameraFn)0x00078500)
#define Orig_RCamera_SetMatrix4 ((CameraMatrixFn)0x00078520)
#define Orig_RCamera_SetActive ((CameraBoolFn)0x00078540)
#define Orig_RViewCamera_SetGuardBandSize ((ViewFloatFn)0x00096850)
#define Orig_RViewCamera_UpdateForResolution ((ViewFn)0x00096860)
#define Orig_RViewCamera_RefreshLODMultiplier ((ViewFn)0x000968b0)
#define Orig_RViewCamera_SetZBufferRange ((ViewRangeFn)0x00096900)
#define Orig_RViewCamera_AspectRatio ((ViewDoubleFn)0x00096960)
#define Orig_RViewCamera_SetViewPortToUnitTransformMode ((UnitModeFn)0x000969f0)
#define Orig_RViewCamera_SetDeviceTransformMode ((ViewFn)0x00096a70)
#define Orig_RViewCamera_SetFillColour ((ViewWordFn)0x00096ca0)
#define Orig_RViewCamera_SetExtents ((ViewExtentsFn)0x00096cc0)
#define Orig_RViewCamera_SetExtentsOther ((ViewOtherFn)0x00096d70)
#define Orig_ApplyPerspectiveFunction ((PerspectiveFn)0x00096e20)
#define Orig_RViewCamera_SetWorldTransformMode ((ViewFn)0x00096eb0)
#define Orig_FloorToInt ((FloorFn)0x000970a0)
#define Orig_TransformPointXZ ((TransformXZFn)0x0008d120)
#define Orig_RWorldCamera_SetViewingTransform ((ViewingFn)0x00097100)
#define Orig_RWorldCamera_SetViewingTransformOffset ((ViewingOffsetFn)0x00097190)
#define Orig_RWorldCamera_GetAnchorSpeed ((WorldFloatFn)0x00097240)
#define Orig_RWorldCamera_GetAnchorAcceleration ((WorldVectorFn)0x000972c0)
#define Orig_RWorldCamera_GetAnchorMatrix4 ((WorldPointerFn)0x000973c0)
#define Orig_RWorldCamera_GetAnchorPosition ((WorldPointerFn)0x00097470)
#define Orig_RWorldCamera_GetAnchorResetAvailable ((WorldIntFn)0x000974e0)
#define Orig_RWorldCamera_SetAnchor ((WorldAnchorFn)0x00097530)
#define Orig_RWorldCamera_GetAnchorRenderOffset ((WorldDoubleFn)0x000975d0)
#define Orig_RWorldCamera_SetCameraZoom ((WorldZoomFn)0x00097710)
#define Orig_RWorldCamera_AnchorCamera ((WorldAnchorCameraFn)0x00097c60)
#define Orig_RWorldCamera_AnchorRelativeCamera ((WorldAnchorCameraFn)0x00097d70)
#define Orig_RWorldCamera_GetAnchorLinearVelocity ((WorldPointerFn)0x00097fc0)
#define Orig_RPlayerViewCamera_ConfigureView ((ViewFn)0x0008a420)
#define Orig_RRenderWorldCamera_CullModule ((CullModuleFn)0x0008c9b0)

// ---- the game's state

struct ShadowRenderer {
    uint8_t unknown00[0x20];
    uint32_t unknown20;         // +0x20
    uint8_t unknown24[0x28];
    uint8_t widescreen;         // +0x4c
    uint8_t unknown4D[0xb];
    float fieldOfViewScale;     // +0x58
};

#define ShadowRendererFields (*(ShadowRenderer **)0x001ebff4)
#define ShadowAnchorMatrix ((uint8_t *)0x001f2d20)          // the static matrix and its guard
const uint32_t kAnchorMatrixBytes = 0x44;
#define ShadowWorldCulling ((uint8_t *)0x001f2c80)
const uint32_t kWorldCullingBytes = 0xa0;
#define ShadowAspectScale (*(float *)0x001c47a0)
#define ShadowSway (*(uint8_t *)0x001c47b8)
#define ShadowCars (*(GameVector<PhysicsObject *> *)0x00234e3c)
#define ShadowMissiles (*(GameVector<PhysicsObject *> *)0x00234e7c)
#define ShadowGrenades (*(GameVector<PhysicsObject *> *)0x00234e9c)
#define ShadowShells (*(GameVector<PhysicsObject *> *)0x00234eac)
#define ShadowCullingEye (*(const Coord4 *)0x001f2cd0)

const uint32_t kViewPortActive = 0x188;
const uint32_t kFakeBytes = 0x420;      // a car (PBondCar) copied whole, its virtual methods reading their fields

// ---- one case's state

struct Globals {
    uint8_t anchorMatrix[kAnchorMatrixBytes];
    uint8_t culling[kWorldCullingBytes];
    uint32_t rendererUnknown20;
    uint8_t widescreen;
    uint8_t sway;
    float fieldOfViewScale;
    float aspectScale;
};

struct CullRecord {
    int item;
    int culled;
    float distance;
};

const int kCullItems = 48;

struct Results {
    double number;
    uint32_t word;
    float values[2];
    Coord4 vector;
    CullRecord records[kCullItems];
    int recordCount;
};

struct State {
    alignas(16) RCamera camera;
    alignas(16) RWorldCamera world;
    alignas(16) RViewCamera view;
    alignas(16) uint8_t viewPort[sizeof(EAGL::ViewPort)];
    alignas(16) Results results;
    Globals globals;
};

void TakeGlobals(Globals *g) {
    memcpy(g->anchorMatrix, ShadowAnchorMatrix, sizeof(g->anchorMatrix));
    memcpy(g->culling, ShadowWorldCulling, sizeof(g->culling));
    g->rendererUnknown20 = ShadowRendererFields->unknown20;
    g->widescreen = ShadowRendererFields->widescreen;
    g->fieldOfViewScale = ShadowRendererFields->fieldOfViewScale;
    g->aspectScale = ShadowAspectScale;
    g->sway = ShadowSway;
}

void PutGlobals(const Globals *g) {
    memcpy(ShadowAnchorMatrix, g->anchorMatrix, sizeof(g->anchorMatrix));
    memcpy(ShadowWorldCulling, g->culling, sizeof(g->culling));
    ShadowRendererFields->unknown20 = g->rendererUnknown20;
    ShadowRendererFields->widescreen = g->widescreen;
    ShadowRendererFields->fieldOfViewScale = g->fieldOfViewScale;
    ShadowAspectScale = g->aspectScale;
    ShadowSway = g->sway;
}

State g_state, g_pre, g_afterOriginal, g_afterPort;
Globals g_live;

// ---- the case

enum Op {
    kOpCameraConstruct, kOpCameraCopy, kOpCreateInverse, kOpConvertToRHCS, kOpSetMatrix, kOpSetActive,
    kOpGuardBand, kOpUpdateForResolution, kOpRefreshLod, kOpZBufferRange, kOpAspectRatio, kOpUnitMode,
    kOpDeviceMode, kOpFillColour, kOpExtents, kOpExtentsOther, kOpPerspective, kOpWorldMode, kOpConfigureView,
    kOpFloor, kOpTransformXZ, kOpViewing, kOpViewingOffset, kOpAnchorSpeed, kOpAnchorAcceleration,
    kOpAnchorMatrix, kOpAnchorPosition, kOpAnchorReset, kOpSetAnchor, kOpAnchorRenderOffset,
    kOpAnchorVelocity, kOpZoom, kOpAnchorCamera, kOpAnchorRelative, kOpCullModule,
    kOpCount
};

const char *const kOpNames[kOpCount] = {
    "RCamera::RCamera", "RCamera copy", "CreateMatrix4Inv", "ConvertToRHCS", "SetMatrix4", "SetActive",
    "SetGuardBandSize", "UpdateForResolution", "RefreshLODMultiplier", "SetZBufferRange", "AspectRatio",
    "SetViewPortToUnitTransformMode", "SetDeviceTransformMode", "SetFillColour", "SetExtents", "SetExtents(view)",
    "ApplyPerspectiveFunction", "SetWorldTransformMode", "ConfigureView", "FloorToInt", "TransformPointXZ",
    "SetViewingTransform", "SetViewingTransform(offset)", "GetAnchorSpeed", "GetAnchorAcceleration",
    "GetAnchorMatrix4", "GetAnchorPosition", "GetAnchorResetAvailable", "SetAnchor", "GetAnchorRenderOffset",
    "GetAnchorLinearVelocity", "SetCameraZoom", "AnchorCamera", "AnchorRelativeCamera", "CullModule",
};

struct Inputs {
    Op op;
    float f[6];
    uint32_t words[2];
    bool flag;
    Coord4 a, b;
    MATRIX4 matrix;
    RViewCamera other;
    PhysicsObject *anchor;
};

Inputs g_in;

// The scripted culling module
struct CullItem {
    Coord4 sphere;
    float height;
    bool checkFar;
    float farScale;
};

CullItem g_cullItems[kCullItems];
int g_cullCount, g_cullCursor;

int ScriptedNext(Coord4 *sphere, float *height, bool *checkFar, float *farScale) {
    if (g_cullCursor >= g_cullCount)
        return -1;
    const CullItem &item = g_cullItems[g_cullCursor];
    *sphere = item.sphere;
    *height = item.height;
    *checkFar = item.checkFar;
    *farScale = item.farScale;
    return 100 + g_cullCursor++;
}

void ScriptedSet(int item, bool culled, float distance) {
    Results &r = g_state.results;
    if (r.recordCount >= kCullItems)
        return;
    CullRecord &record = r.records[r.recordCount++];
    record.item = item;
    record.culled = culled;
    record.distance = distance;
}

void Run(bool original) {
    State &s = g_state;
    Results &r = s.results;
    const Inputs &in = g_in;
    RWorldCamera *w = &s.world;
    RViewCamera *v = &s.view;
    EAGL::ViewPort *port = reinterpret_cast<EAGL::ViewPort *>(s.viewPort);
    switch (in.op) {
    case kOpCameraConstruct:
        original ? (void)Orig_RCamera_Construct(&s.camera, 0) : (void)s.camera.Construct();
        break;
    case kOpCameraCopy:
        original ? (void)Orig_RCamera_ConstructCopy(&s.camera, 0, w) : (void)s.camera.ConstructCopy(w);
        break;
    case kOpCreateInverse:
        original ? Orig_RCamera_CreateMatrix4Inv(&s.camera, 0) : s.camera.CreateMatrix4Inv();
        break;
    case kOpConvertToRHCS:
        original ? Orig_RCamera_ConvertToRHCS(&s.camera, 0) : s.camera.ConvertToRHCS();
        break;
    case kOpSetMatrix:
        original ? Orig_RCamera_SetMatrix4(&s.camera, 0, &in.matrix) : s.camera.SetMatrix4(&in.matrix);
        break;
    case kOpSetActive:
        original ? Orig_RCamera_SetActive(&s.camera, 0, in.flag) : s.camera.SetActive(in.flag);
        break;
    case kOpGuardBand:
        original ? Orig_RViewCamera_SetGuardBandSize(v, 0, in.f[0]) : v->SetGuardBandSize(in.f[0]);
        break;
    case kOpUpdateForResolution:
        original ? Orig_RViewCamera_UpdateForResolution(v, 0) : v->UpdateForResolution();
        break;
    case kOpRefreshLod:
        original ? Orig_RViewCamera_RefreshLODMultiplier(v, 0) : v->RefreshLODMultiplier();
        break;
    case kOpZBufferRange:
        original ? Orig_RViewCamera_SetZBufferRange(v, 0, in.words[0], in.words[1])
                 : v->SetZBufferRange(in.words[0], in.words[1]);
        break;
    case kOpAspectRatio:
        r.number = original ? Orig_RViewCamera_AspectRatio(v, 0) : v->AspectRatio();
        break;
    case kOpUnitMode:
        original ? Orig_RViewCamera_SetViewPortToUnitTransformMode(port, in.f[0], in.f[1])
                 : RViewCamera::SetViewPortToUnitTransformMode(port, in.f[0], in.f[1]);
        break;
    case kOpDeviceMode:
        original ? Orig_RViewCamera_SetDeviceTransformMode(v, 0) : v->SetDeviceTransformMode();
        break;
    case kOpFillColour:
        original ? Orig_RViewCamera_SetFillColour(v, 0, in.words[0]) : v->SetFillColour(in.words[0]);
        break;
    case kOpExtents:
        original ? Orig_RViewCamera_SetExtents(v, 0, in.f[0], in.f[1], in.f[2], in.f[3])
                 : v->SetExtents(in.f[0], in.f[1], in.f[2], in.f[3]);
        break;
    case kOpExtentsOther:
        original ? Orig_RViewCamera_SetExtentsOther(v, 0, &in.other) : v->SetExtents(&in.other);
        break;
    case kOpPerspective:
        r.values[0] = in.f[0];
        r.values[1] = in.f[1];
        original ? Orig_ApplyPerspectiveFunction(&r.values[0], &r.values[1])
                 : ApplyPerspectiveFunction(&r.values[0], &r.values[1]);
        break;
    case kOpWorldMode:
        original ? Orig_RViewCamera_SetWorldTransformMode(v, 0) : v->SetWorldTransformMode();
        break;
    case kOpConfigureView:
        original ? Orig_RPlayerViewCamera_ConfigureView(v, 0) : static_cast<RPlayerViewCamera *>(v)->ConfigureView();
        break;
    case kOpFloor:
        r.word = original ? Orig_FloorToInt(in.f[0]) : FloorToInt(in.f[0]);
        break;
    case kOpTransformXZ: {
        Coord3 point = { in.a.x, in.a.y, in.a.z };
        original ? Orig_TransformPointXZ(&point, &in.matrix, &r.vector) : TransformPointXZ(&point, &in.matrix, &r.vector);
        break;
    }
    case kOpViewing:
        original ? Orig_RWorldCamera_SetViewingTransform(w, 0, &in.a, in.flag ? &in.b : NULL, 0)
                 : w->SetViewingTransform(&in.a, in.flag ? &in.b : NULL, 0);
        break;
    case kOpViewingOffset:
        original ? Orig_RWorldCamera_SetViewingTransformOffset(w, 0, &in.a, in.f, in.flag ? &in.b : NULL, 0)
                 : w->SetViewingTransform(&in.a, in.f, in.flag ? &in.b : NULL, 0);
        break;
    case kOpAnchorSpeed: {
        float speed = original ? Orig_RWorldCamera_GetAnchorSpeed(w, 0) : w->GetAnchorSpeed();
        memcpy(&r.word, &speed, 4);
        break;
    }
    case kOpAnchorAcceleration:
        original ? Orig_RWorldCamera_GetAnchorAcceleration(w, 0, &r.vector) : w->GetAnchorAcceleration(&r.vector);
        break;
    case kOpAnchorMatrix:
        r.word = uint32_t(uintptr_t(original ? Orig_RWorldCamera_GetAnchorMatrix4(w, 0) : (void *)w->GetAnchorMatrix4()));
        break;
    case kOpAnchorPosition:
        r.word = uint32_t(uintptr_t(original ? Orig_RWorldCamera_GetAnchorPosition(w, 0) : (void *)w->GetAnchorPosition()));
        break;
    case kOpAnchorReset:
        r.word = original ? Orig_RWorldCamera_GetAnchorResetAvailable(w, 0) : w->GetAnchorResetAvailable();
        break;
    case kOpSetAnchor:
        original ? Orig_RWorldCamera_SetAnchor(w, 0, in.anchor) : w->SetAnchor(in.anchor);
        break;
    case kOpAnchorRenderOffset:
        r.number = original ? Orig_RWorldCamera_GetAnchorRenderOffset(w, 0) : w->GetAnchorRenderOffset();
        break;
    case kOpAnchorVelocity:
        r.word = uint32_t(uintptr_t(original ? Orig_RWorldCamera_GetAnchorLinearVelocity(w, 0)
                                             : (void *)w->GetAnchorLinearVelocity()));
        break;
    case kOpZoom:
        original ? Orig_RWorldCamera_SetCameraZoom(w, 0, in.f[0], in.f[1]) : w->SetCameraZoom(in.f[0], in.f[1]);
        break;
    case kOpAnchorCamera: {
        Coord3 offset = { in.a.x, in.a.y, in.a.z };
        original ? Orig_RWorldCamera_AnchorCamera(w, 0, in.flag, &offset) : w->AnchorCamera(in.flag, &offset);
        break;
    }
    case kOpAnchorRelative: {
        Coord3 offset = { in.a.x, in.a.y, in.a.z };
        original ? Orig_RWorldCamera_AnchorRelativeCamera(w, 0, in.flag, &offset)
                 : w->AnchorRelativeCamera(in.flag, &offset);
        break;
    }
    case kOpCullModule:
        g_cullCursor = 0;
        original ? Orig_RRenderWorldCamera_CullModule(static_cast<RRenderWorldCamera *>(v), 0, ScriptedNext,
                                                      ScriptedSet, in.flag)
                 : static_cast<RRenderWorldCamera *>(v)->CullModule(ScriptedNext, ScriptedSet, in.flag);
        break;
    default:
        break;
    }
}

// ---- results

int g_cases = 0, g_checks = 0, g_differ = 0, g_details = 0, g_faults = 0;
unsigned int g_x87 = 0, g_sse = 0;

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

bool Guarded(bool original) {
    if (original)
        OriginalWindow(true);
    bool ok = true;
#ifdef _MSC_VER
    __try {
        Run(original);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        g_faults++;
        ok = false;
    }
#else
    Run(original);
#endif
    if (original)
        OriginalWindow(false);
    return ok;
}

void CheckBytes(const char *what, int index, const void *a, const void *b, size_t bytes) {
    g_checks++;
    if (memcmp(a, b, bytes) == 0)
        return;
    const uint8_t *x = static_cast<const uint8_t *>(a), *y = static_cast<const uint8_t *>(b);
    size_t at = 0;
    while (x[at] == y[at])
        at++;
    g_differ++;
    if (g_details++ < 10)
        printf("[viewcam]   %s #%d: byte %u of %u: original %02x, port %02x\n", what, index, unsigned(at),
               unsigned(bytes), x[at], y[at]);
}

// Both sides from g_state as it is now; the state after each compared
void SideBySide(int index) {
    g_cases++;
    PutGlobals(&g_state.globals);
    g_pre = g_state;
    bool originalOk = Guarded(true);
    TakeGlobals(&g_state.globals);
    g_afterOriginal = g_state;
    g_state = g_pre;
    PutGlobals(&g_state.globals);
    bool portOk = Guarded(false);
    TakeGlobals(&g_state.globals);
    g_afterPort = g_state;
    g_state = g_pre;
    const char *name = kOpNames[g_in.op];
    if (!originalOk || !portOk) {
        // a fault on one side only is a difference; on both, the case is not compared
        const char *side = originalOk ? "the port faulted" : portOk ? "the original faulted" : "both faulted";
        if (originalOk != portOk)
            g_differ++;
        if (g_details++ < 10)
            printf("[viewcam]   %s #%d: %s\n", name, index, side);
        return;
    }
    CheckBytes(name, index, &g_afterOriginal.camera, &g_afterPort.camera, sizeof(RCamera));
    CheckBytes(name, index, &g_afterOriginal.world, &g_afterPort.world, sizeof(RWorldCamera));
    CheckBytes(name, index, &g_afterOriginal.view, &g_afterPort.view, sizeof(RViewCamera));
    CheckBytes(name, index, g_afterOriginal.viewPort, g_afterPort.viewPort, sizeof(g_afterPort.viewPort));
    CheckBytes(name, index, &g_afterOriginal.results, &g_afterPort.results, sizeof(Results));
    CheckBytes(name, index, &g_afterOriginal.globals, &g_afterPort.globals, sizeof(Globals));
}

// ---- random inputs (a generator of our own: the game's is not touched)

uint32_t g_seed = 0x5eedca3e;

uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(Next() & 0xffff) / 65535.0f;
}

Coord4 RandomVector(float range) {
    Coord4 v = { Uniform(-range, range), Uniform(-range, range), Uniform(-range, range), Uniform(-2.0f, 2.0f) };
    return v;
}

void RandomFrame(MATRIX4 *m) {
    for (int row = 0; row < 3; row++) {
        Coord4 v = RandomVector(1.0f);
        memcpy(m->mtx[row], &v, sizeof(v));
    }
    Coord4 p = RandomVector(800.0f);
    memcpy(m->mtx[3], &p, sizeof(p));
}

float RandomFieldOfView() {
    switch (Next() % 8) {
    case 0: return 32.0f;
    case 1: return 33.0f;
    case 2: return Uniform(-10.0f, 0.0f);
    default: return Uniform(1.0f, 120.0f);
    }
}

// ---- the base state, from the live cameras

RViewCamera *g_liveView;
RWorldCamera *g_liveCamera;

void MakeBase() {
    State &s = g_state;
    memset(&s, 0, sizeof(s));
    memcpy(&s.camera, g_liveCamera, sizeof(RCamera));
    memcpy(&s.world, g_liveCamera, sizeof(RWorldCamera));
    memcpy(&s.view, g_liveView, sizeof(RViewCamera));
    memcpy(s.viewPort, g_liveView->viewPort, sizeof(EAGL::ViewPort));
    s.viewPort[kViewPortActive] = 0;
    s.view.camera = &s.camera;
    s.view.viewPort = reinterpret_cast<EAGL::ViewPort *>(s.viewPort);
    s.view.active = 0;
    s.globals = g_live;
}

// Randomises what the view functions read
void PerturbView() {
    State &s = g_state;
    RandomFrame(&s.camera.matrix);
    s.camera.matrixChanged = uint8_t(Next() % 2);
    s.camera.fieldOfView = RandomFieldOfView();
    float x = Uniform(-0.2f, 1.0f), y = Uniform(-0.2f, 1.0f);
    s.view.xMin = x;
    s.view.yMin = y;
    s.view.xMax = x + Uniform(0.0f, 1.2f);
    s.view.yMax = y + Uniform(0.0f, 1.2f);
    s.view.nearZ = Uniform(0.01f, 2.0f);
    s.view.farZ = Uniform(100.0f, 5000.0f);
    s.globals.widescreen = uint8_t(Next() % 2);
    s.globals.fieldOfViewScale = Next() % 2 ? g_live.fieldOfViewScale : Uniform(0.2f, 2.0f);
    s.globals.aspectScale = Next() % 2 ? g_live.aspectScale : Uniform(0.5f, 2.0f);
    s.globals.sway = uint8_t(Next() % 2);
}

// ---- the anchors

const int kMaxAnchors = 64;
PhysicsObject *g_anchors[kMaxAnchors];
int g_anchorCount;
bool g_anchorIsCar[kMaxAnchors];
alignas(16) uint8_t g_fakes[12][kFakeBytes];

void AddAnchor(PhysicsObject *object, bool car) {
    if (g_anchorCount < kMaxAnchors) {
        g_anchorIsCar[g_anchorCount] = car;
        g_anchors[g_anchorCount++] = object;
    }
}

void AddList(const GameVector<PhysicsObject *> &list, bool cars, int most) {
    if (list.first == NULL)
        return;
    for (PhysicsObject **it = list.first; it != list.last && most-- > 0; it++)
        AddAnchor(*it, cars);
}

void CollectAnchors() {
    g_anchorCount = 0;
    AddAnchor(NULL, false);
    AddList(ShadowCars, true, 16);
    AddList(ShadowShells, false, 6);
    AddList(ShadowMissiles, false, 4);
    AddList(ShadowGrenades, false, 4);
    // copies of cars with their type, simple-body flag and slot changed
    int cars = 0;
    for (int i = 0; i < g_anchorCount; i++)
        if (g_anchorIsCar[i])
            cars++;
    if (cars == 0)
        return;
    static const int16_t kSlots[] = { -1, 0, 63, 64, 100, -32768 };
    for (int k = 0; k < 12; k++) {
        PhysicsObject *source = ShadowCars.first[k % cars];
        bool copied = true;
#ifdef _MSC_VER
        __try {
            memcpy(g_fakes[k], source, kFakeBytes);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            copied = false;
        }
#else
        memcpy(g_fakes[k], source, kFakeBytes);
#endif
        if (!copied)
            continue;
        PhysicsObject *fake = reinterpret_cast<PhysicsObject *>(g_fakes[k]);
        if (k % 3 == 0)
            fake->type = int32_t(k % 4);
        if (k % 3 == 1)
            fake->flags ^= PhysicsObject::kSimpleBody;
        if (k % 2 == 0)
            fake->rigidBodySlot = kSlots[k / 2];
        AddAnchor(fake, true);
    }
}

// A copy whose slot leads to a rigid body without its info (an unused slot) faults on both sides where the
// info's matrix is read; those cases are left out.
#define ShadowGetRigidBody ((RigidBodyFields *(__fastcall *)(void *, int, int slot))0x000b2700)

struct RigidBodyFields {
    uint8_t unknown00[0x5c];
    void *info;                 // +0x5c
};

bool ReadsMissingInfo(PhysicsObject *anchor, Op op) {
    if (op != kOpAnchorAcceleration && op != kOpAnchorMatrix && op != kOpAnchorCamera && op != kOpAnchorRelative)
        return false;
    if (anchor == NULL || (anchor->flags & PhysicsObject::kSimpleBody) || anchor->rigidBodySlot < 0 ||
        anchor->rigidBodySlot >= 0x40)
        return false;
    if (op == kOpAnchorAcceleration && anchor->type != 1)
        return false;
    RigidBodyFields *body = ShadowGetRigidBody((void *)0x00233ff0, 0, anchor->rigidBodySlot);
    return body == NULL || body->info == NULL;
}

// ---- the tests

void TestCameraAndView() {
    for (int round = 0; round < 60; round++) {
        for (int op = kOpCameraConstruct; op <= kOpFloor; op++) {
            MakeBase();
            PerturbView();
            memset(&g_in, 0, sizeof(g_in));
            g_in.op = Op(op);
            for (int i = 0; i < 6; i++)
                g_in.f[i] = Uniform(-2.0f, 2.0f);
            g_in.words[0] = Next();
            g_in.words[1] = Next();
            g_in.flag = Next() % 2 != 0;
            RandomFrame(&g_in.matrix);
            if (op == kOpExtents) {
                g_in.f[0] = Uniform(-0.2f, 1.0f);
                g_in.f[1] = Uniform(-0.2f, 1.0f);
                g_in.f[2] = Uniform(0.0f, 1.0f);
                g_in.f[3] = Uniform(0.0f, 1.0f);
            }
            if (op == kOpUnitMode) {
                g_in.f[0] = Uniform(0.0f, 1.0f);
                g_in.f[1] = Uniform(1.0f, 100.0f);
            }
            if (op == kOpPerspective) {
                g_in.f[0] = Uniform(0.5f, 2.5f);
                g_in.f[1] = RandomFieldOfView();
            }
            if (op == kOpExtentsOther) {
                g_in.other = g_state.view;
                g_in.other.xMin = Uniform(0.0f, 0.5f);
                g_in.other.yMin = Uniform(0.0f, 0.5f);
                g_in.other.xMax = Uniform(0.5f, 1.0f);
                g_in.other.yMax = Uniform(0.5f, 1.0f);
                g_in.other.nearZ = Uniform(0.01f, 1.0f);
                g_in.other.farZ = Uniform(100.0f, 4000.0f);
            }
            if (op == kOpFloor) {
                switch (round % 6) {
                case 0: g_in.f[0] = float(int(Next() % 2001) - 1000); break;
                case 1: g_in.f[0] = Uniform(-1000.0f, 1000.0f); break;
                case 2: g_in.f[0] = Uniform(-1.0f, 1.0f); break;
                case 3: g_in.f[0] = Uniform(-3.0e9f, 3.0e9f); break;
                case 4: g_in.f[0] = round % 12 == 4 ? NAN : -0.0f; break;
                default: g_in.f[0] = -Uniform(0.0f, 1.0e-3f); break;
                }
            }
            SideBySide(round);
        }
        // TransformPointXZ
        memset(&g_in, 0, sizeof(g_in));
        g_in.op = kOpTransformXZ;
        g_in.a = RandomVector(1000.0f);
        RandomFrame(&g_in.matrix);
        SideBySide(round);
    }
}

void TestWorldCamera() {
    CollectAnchors();
    for (int round = 0; round < 8; round++) {
        for (int a = 0; a < g_anchorCount; a++) {
            for (int op = kOpViewing; op <= kOpAnchorRelative; op++) {
                // a shell, missile or grenade may have no render object, which these read
                if ((op == kOpSetAnchor || op == kOpAnchorRenderOffset || op == kOpAnchorRelative) &&
                    g_anchors[a] != NULL && !g_anchorIsCar[a])
                    continue;
                if (ReadsMissingInfo(g_anchors[a], Op(op)))
                    continue;
                MakeBase();
                RWorldCamera &w = g_state.world;
                w.anchor = g_anchors[a];
                Coord4 v = RandomVector(400.0f);
                w.eye = v;
                v = RandomVector(400.0f);
                w.lookAt = v;
                v = RandomVector(10.0f);
                w.lookAtOffset = v;
                RandomFrame(&w.matrix);
                w.fieldOfView = RandomFieldOfView();
                if (round % 4 == 1)
                    g_state.globals.anchorMatrix[0x40] &= ~1;     // the static not made yet
                memset(&g_in, 0, sizeof(g_in));
                g_in.op = Op(op);
                g_in.anchor = g_anchors[a];
                g_in.flag = Next() % 2 != 0;
                g_in.a = RandomVector(round % 3 == 0 ? 1.0f : 20.0f);
                g_in.b = RandomVector(50.0f);
                g_in.f[0] = Uniform(-3.0f, 3.0f);
                g_in.f[1] = Uniform(-3.0f, 3.0f);
                if (op == kOpZoom) {
                    g_in.f[0] = round % 5 == 0 ? w.fieldOfView : RandomFieldOfView();
                    g_in.f[1] = round % 5 == 1 ? 0.0f : Uniform(0.0f, 8.0f);
                }
                SideBySide(round * 1000 + a);
            }
        }
    }
}

void TestCulling() {
    for (int round = 0; round < 40; round++) {
        MakeBase();
        memset(&g_in, 0, sizeof(g_in));
        g_in.op = kOpCullModule;
        g_in.flag = Next() % 2 != 0;
        g_cullCount = 1 + int(Next() % kCullItems);
        Coord4 eye = ShadowCullingEye;
        for (int i = 0; i < g_cullCount; i++) {
            CullItem &item = g_cullItems[i];
            float range = round % 2 == 0 ? 100.0f : 800.0f;
            item.sphere.x = eye.x + Uniform(-range, range);
            item.sphere.y = eye.y + Uniform(-20.0f, 20.0f);
            item.sphere.z = eye.z + Uniform(-range, range);
            item.sphere.w = Uniform(0.0f, 30.0f);
            item.height = Uniform(0.0f, 20.0f);
            item.checkFar = Next() % 2 != 0;
            item.farScale = Next() % 3 == 0 ? 0.3f : Next() % 2 ? 1.0f : 2.0f;
        }
        SideBySide(round);
    }
}

}  // namespace

void ViewCamShadow_Run(void) {
    char value[16] = "";
    DWORD length = GetEnvironmentVariableA("NIGHTFIRE_VIEWCAMSHADOW", value, sizeof(value));
    if (length == 0 || length >= sizeof(value) || atoi(value) == 0)
        return;
    if (CameraViews == NULL || CameraViews[0].view == NULL || CameraViews[0].camera == NULL ||
        ShadowRendererFields == NULL || CameraViews[0].view->viewPort == NULL) {
        printf("[viewcam] no camera view yet - skipped\n");
        fflush(stdout);
        return;
    }
    g_liveView = CameraViews[0].view;
    g_liveCamera = CameraViews[0].camera;
    FpControlGet(&g_x87, &g_sse);
    TakeGlobals(&g_live);
    TestCameraAndView();
    TestWorldCamera();
    TestCulling();
    PutGlobals(&g_live);
    ResetFpu();
    printf("[viewcam] RCamera, RViewCamera, RWorldCamera, CullModule, ConfigureView vs originals: %d cases, %d checks, "
           "%d differ%s\n", g_cases, g_checks, g_differ, g_faults != 0 ? " (with faults)" : "");
    if (g_faults != 0)
        printf("[viewcam]   %d calls faulted\n", g_faults);
    fflush(stdout);
}
