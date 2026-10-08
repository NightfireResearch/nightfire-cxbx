#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// RPlayerCamera, 0x00080a60..0x00083190 (PlayerCamera.h has the class): the tuning tables' teardown, the bumper
// and dashboard cameras, the three animation cameras (along an AI spline path, at a 'Cams' instance in the world,
// and relative to the anchor's heading), zoom set-up, the arms chosen by the current weapon, the director's anchor
// change, look-back, the auto-drive rotation response, lock-on, aim zoom, the destructor and the auto-drive
// target angles; and the free helpers among them (PointDir, PointInTriangle, the arc cosine and sine in turns,
// the clamp).
//
// The tuning file's tables are RCameraIniLoader::LoadFile's (camera/CameraIniLoader.cpp).
// ---------------------------------------------------------------------------------------------------------------

#include "PlayerCameraA.h"

#include <stddef.h>
#include <stdint.h>
#include <bit>

#include "../../helpers.h"
#include "../data/Carp.h"
#include "../engine/CoreFoundation.h"   // NullFunction
#include "../engine/GameLoop.h"         // LaunchPage
#include "../engine/MissionManager.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"     // Abs
#include "../physics/PhysicsObject.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RPathHandle.hpp"
#include "../world/Collider.h"
#include "../world/RoadNav.h"
#include "../world/Targeting.h"
#include "../world/WorldMath.h"
#include "../world/WorldPos.h"
#include "PlayerCamState.h"

namespace {

// ---- views (the names are ours) of what other systems' objects hold

// The 'Cams' instance's flags as the animation cameras read them
enum CameraAnimFlags : uint8_t {
    kCamAnimFollowsAnchor = 0x01,       // the path is kept relative to the camera's lookAt
};

// RWorldCamera::animHandle (RAnimEngine::Handle): its 'Cams' instance
struct CameraAnimHandle {
    uint8_t unknown00[0x40];
    CARP::Instance instance;            // +0x40
};

// The weapon manager: the current slot and the slots (the first word of each is its weapon)
struct WeaponSlot {
    int32_t weapon;
    uint8_t unknown04[0x50];
};
static_assert(sizeof(WeaponSlot) == 0x54, "a weapon slot is 84 bytes");

struct WeaponManagerFields {
    uint32_t unknown00;
    int32_t current;                    // +0x04 -1: none
    uint8_t unknown08[8];
    WeaponSlot *slots;                  // +0x10
};

// The input configuration (InputConfigManager +0x04)
struct InputConfig {
    uint32_t unknown00;
    int32_t scheme;                     // +0x04 InputScheme
};

enum InputScheme : int32_t {
    kInputAutoDrive = 1,                // the auto-drive camera's input handler
};

constexpr int32_t kSimState3 = 3;       // Sim.simState: the cameras stand still
constexpr int32_t kAnchorRigidBody = 1; // PhysicsObject::type: a rigid body (the dashboard camera's vertigo)
constexpr int32_t kWeapon1C = 0x1c;     // the weapon WeaponFired notes
constexpr uint16_t kCarAnimationFlags = 0x44;   // TriggerCarAnimationCamera's additions to the caller's flags

}  // namespace

// ---- the game's globals

#define Launch (*(LaunchPage *)0x00243b90)
#define RadiansToTurns FLOAT_AT(0x001ec3b8)     // 1 / (2 pi), set by a static initialiser (this unit's copy)
#define SimState I32_AT(0x00234e24)
#define SimStepsPerSecond I32_AT(0x00234e2c)
#define SimTimeStep FLOAT_AT(0x00234e30)
#define SimStepCount I32_AT(0x00234e34)
#define WeaponManager (*(WeaponManagerFields **)0x0023923c)

#define PlayerCameraVtable ((void **)0x001918ac)

// ---- calls to originals not ported

#define RCamera_SetFieldOfView ((void (__fastcall *)(RCamera *, int, float))0x00011000)    // FUN_00011000: above 2 only
// FUN_0003db00: the instance's matrix, its fourth column (0, 0, 0, 1)
#define CARPInstance_GetMatrix4 ((void (__fastcall *)(const CARP::Instance *, int, MATRIX4 *))0x0003db00)
#define InputConfigManager_Get ((InputConfig *(*)(void))0x00050270)
#define WWorldPos_FaceNormal ((void (__fastcall *)(const WWorldPos *, int, Coord3 *))0x0005d3f0)
#define RPathHandle_GetPosition ((const Coord3 *(__fastcall *)(RPathHandle *, int))0x0007ff50)
#define RPathHandle_GetOrientMat ((void (__fastcall *)(RPathHandle *, int, MATRIX4 *))0x0007ff60)
#define FUN_00080820 ((double (*)(float))0x00080820)        // the turns brought into 0..1
#define FUN_00080980 ((void (*)(MATRIX4 *))0x00080980)      // row 1 = row 2, row 2 = -row 1
#define FUN_000809e0 ((void (*)(MATRIX4 *))0x000809e0)      // rows 0-2 = -row 0, -row 2, -row 1

namespace {

// ---- constants

constexpr float kMinFieldOfView = 2.0f;         // fields of view at or below are not taken
constexpr float kVertigoLimit = 0.05f;          // the dashboard camera's vertigo, either way
static_assert(std::bit_cast<uint32_t>(kVertigoLimit) == 0x3d4ccccd, "the vertigo limit");
constexpr float kStepsPerSecond = 60.0f;        // the dashboard camera's inertia is per sixtieth of a second

// The auto-drive rotation's response: the stick's travel in 96ths, to a speed in 255ths of 0.005 turns
constexpr float kRotationSteps = 96.0f;
constexpr float kRotationUnit = 0.005f;
constexpr float kOneOver255 = 1.0f / 255.0f;
static_assert(std::bit_cast<uint32_t>(kRotationUnit) == 0x3ba3d70a, "the rotation response's unit");
static_assert(std::bit_cast<uint32_t>(kOneOver255) == 0x3b808081, "1 / 255");
constexpr uint8_t kRotationResponse[97] = {
    0,   0,   0,   0,   0,   10,  10,  10,  10,  11,  12,  13,  14,  15,  16,  17,  18,  19,  20,  21,
    22,  23,  24,  25,  26,  27,  28,  29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,
    42,  43,  44,  45,  46,  47,  48,  49,  50,  52,  54,  56,  58,  60,  62,  64,  66,  68,  70,  72,
    74,  76,  78,  80,  82,  84,  86,  88,  90,  94,  98,  102, 106, 110, 114, 118, 122, 126, 130, 134,
    138, 142, 146, 150, 160, 170, 180, 190, 200, 210, 220, 230, 240, 250, 255, 255, 255,
};

// InitSpin's turn per step about the anchor's up axis: the sine and cosine of 2.5 degrees, for 36 steps
constexpr float kSpinSine = 0.0436193906f;
constexpr float kSpinCosine = 0.999048173f;
static_assert(std::bit_cast<uint32_t>(kSpinSine) == 0x3d32aa3f && std::bit_cast<uint32_t>(kSpinCosine) == 0x3f7fc19f,
              "the spin's quaternion");
constexpr int kSpinSteps = 36;

constexpr float kOneThird = 1.0f / 3.0f;        // the auto-aim limits, per step of unknown4dc
static_assert(std::bit_cast<uint32_t>(kOneThird) == 0x3eaaaaab, "a third");

// The animation cameras' blend into their path: the spline's tension
constexpr float kWorldAnimTension = 0.33f;
constexpr float kRelativeAnimTension = 0.5f;
static_assert(std::bit_cast<uint32_t>(kWorldAnimTension) == 0x3ea8f5c3, "the world animation's tension");

// ---- helpers

CARP::Instance *AnimInstanceOf(RPlayerCamera *camera) {
    return &reinterpret_cast<CameraAnimHandle *>(camera->animHandle)->instance;
}

// The instance's animation, NULL if it has none
const CameraAnimData *AnimDescOf(const CARP::Instance *instance) {
    return reinterpret_cast<const CameraAnimData *>(instance->articleDesc.value);
}

// a + (b - a) * t in x, y and z, out's w kept: the SSE lerp the compiler inlined (MOVSS and MOVHPS put x in
// lane 0 and y, z in lanes 2 and 3)
void LerpXYZ(const Coord4 *a, const Coord4 *b, float t, Coord4 *out) {
    __m128 from = _mm_set_ps(a->z, a->y, 0.0f, a->x);
    __m128 to = _mm_set_ps(b->z, b->y, 0.0f, b->x);
    __m128 r = _mm_add_ps(_mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(to, from)), from);
    float lanes[4];
    _mm_storeu_ps(lanes, r);
    out->x = lanes[0];
    out->y = lanes[2];
    out->z = lanes[3];
}

// v + (to - v) * t in all four lanes, in place
void Lerp4(Coord4 *v, const Coord4 *to, float t) {
    __m128 from = _mm_loadu_ps(&v->x);
    _mm_storeu_ps(&v->x, _mm_add_ps(_mm_mul_ps(_mm_set1_ps(t), _mm_sub_ps(_mm_loadu_ps(&to->x), from)), from));
}

// atan_turns answers in ST0 unrounded; read as the double it is rather than through its float declaration
double AtanTurnsUnrounded(float y, float x) {
    typedef double (*UnroundedTurnsFn)(float, float);
    return reinterpret_cast<UnroundedTurnsFn>(&atan_turns)(y, x);
}

}  // namespace

// ---- the free functions

// FUNC_AT(0x00080f50)
double PointDir(int axis, const Coord4 *a, const Coord4 *b, const Coord4 *origin) {
    if (axis == 2)
        return (double(a->x) - origin->x) * (double(b->y) - origin->y) -
               (double(a->y) - origin->y) * (double(b->x) - origin->x);
    if (axis == 1)
        return (double(a->z) - origin->z) * (double(b->x) - origin->x) -
               (double(b->z) - origin->z) * (double(a->x) - origin->x);
    return (double(a->y) - origin->y) * (double(b->z) - origin->z) -
           (double(a->z) - origin->z) * (double(b->y) - origin->y);
}

// FUNC_AT(0x00080fe0)
bool PointInTriangle(const Coord4 *point, const Coord4 *a, const Coord4 *b, const Coord4 *c) {
    Coord4 ab, cb, normal;
    VU0_v4sub(a, b, &ab);
    VU0_v4sub(c, b, &cb);
    VU0_v4crossprodxyz(&ab, &cb, &normal);
    int axis;
    if (normal.y > normal.x)
        axis = normal.z > normal.y ? 2 : 1;
    else
        axis = normal.z > normal.x ? 2 : 0;

    if (!(PointDir(axis, point, a, b) <= 0.0))     // positive, or unordered
        return PointDir(axis, point, b, c) >= 0.0 && PointDir(axis, point, c, a) >= 0.0;
    return PointDir(axis, point, b, c) <= 0.0 && PointDir(axis, point, c, a) <= 0.0;
}

// FUNC_AT(0x00081b00)
double CameraAcosTurns(float value) {
    if (value > 1.0f)
        value = 1.0f;
    else if (value < -1.0f)
        value = -1.0f;
    return CrtAcosTimes(value, &RadiansToTurns);
}

// FUNC_AT(0x00081b60)
double CameraAsinTurns(float value) {
    if (value > 1.0f)
        value = 1.0f;
    else if (value < -1.0f)
        value = -1.0f;
    return CrtAsinTimes(value, &RadiansToTurns);
}

// FUNC_AT(0x00081bc0)
float CameraClamp(float high, float value, float low) {
    float result = value > low ? value : low;
    return high < result ? high : result;
}

// ---- RPlayerCamera

// Frees the tuning file's tables.
// FUNC_AT(0x00080a60)
void RPlayerCamera::Shutdown() {
    for (int i = 0; i < fgCameraTables.ellipseCount; i++) {
        if (fgCameraTables.ellipses[i].heights != NULL)
            OperatorDelete(fgCameraTables.ellipses[i].heights);
    }
    for (int i = 0; i < fgCameraTables.heliCount; i++) {
        if (fgCameraTables.helis[i].arms != NULL)
            OperatorDelete(fgCameraTables.helis[i].arms);
    }
    if (fgCameraTables.fixeds != NULL)
        OperatorDelete(fgCameraTables.fixeds);
    if (fgCameraTables.ellipses != NULL)
        OperatorDelete(fgCameraTables.ellipses);
    if (fgCameraTables.splines != NULL)
        OperatorDelete(fgCameraTables.splines);
    if (fgCameraTables.helis != NULL)
        OperatorDelete(fgCameraTables.helis);
    if (fgCameraTables.bumpers != NULL)
        OperatorDelete(fgCameraTables.bumpers);
    if (fgCameraTables.dashboards != NULL)
        OperatorDelete(fgCameraTables.dashboards);
    if (fgCameraTables.modes != NULL)
        OperatorDelete(fgCameraTables.modes);
    if (fgCameraTables.autoDriveArms != NULL) {
        OperatorDelete(fgCameraTables.autoDriveArms);
        fgCameraTables.autoDriveArms = NULL;
    }
    fgCameraTables.fixeds = NULL;
    fgCameraTables.ellipses = NULL;
    fgCameraTables.splines = NULL;
    fgCameraTables.helis = NULL;
    fgCameraTables.bumpers = NULL;
    fgCameraTables.dashboards = NULL;
    fgCameraTables.modes = NULL;
}

// The bumper camera: the arm (forwards, or back while looking back) turned with the anchor, raised by its pan,
// the height eased.
// FUNC_AT(0x00080b90)
void RPlayerCamera::UpdateBumperCam() {
    const CameraModeInfo *mode = &fgCameraTables.modes[cameraMode];
    if (mode->index < 0 || mode->index >= fgCameraTables.bumperCount)
        return;
    const BumperCamInfo *bumper = &fgCameraTables.bumpers[mode->index];
    const Coord4 *arm = &bumper->forwardArm;      // w: its pan up
    if (state->lookingBack == 1 && mode->lookBack)
        arm = &bumper->backwardsArm;

    MATRIX4 *anchorMatrix = GetAnchorMatrix4();
    const Coord3 *anchorPosition = GetAnchorPosition();
    Coord4 offset = {};
    Coord4 position = {};       // (w: the original's stack, copied into eye.w below)
    VU0_MATRIX4_vect3rotate(arm, anchorMatrix, &offset);
    VU0_v3add(anchorPosition, &offset, &position);
    position.y = position.y + arm->w;
    if (modeChangeFlags != 0) {
        eye = position;
    } else {
        eye.x = position.x;
        eye.y = float((double(position.y) - eye.y) * fgCameraConstants.kBumperYLerpRate + eye.y);
        eye.z = position.z;
    }
    VU0_v3add(&eye, &offset, &lookAt);
    SetViewingTransform(MatrixRow(anchorMatrix, 1), GetAnchorLinearVelocity(), 0);
    if (mode->defaultFov > kMinFieldOfView)
        fieldOfView = mode->defaultFov;
}

// The camera along the AI spline path: the path's frame at its position, the spline's placement applied.
// FUNC_AT(0x00080cf0)
void RPlayerCamera::UpdateAIPathAnimationCam() {
    if (SimState == kSimState3 || aiSplinePath == NULL)
        return;
    RPathHandle *path = aiSplinePath->path;
    MATRIX4 frame;
    RPathHandle_GetOrientMat(path, 0, &frame);
    Coord4 *position = MatrixRow(&frame, 3);    // (0, 0, 0, 1) from GetOrientMat
    position->x = RPathHandle_GetPosition(path, 0)->x;
    position->y = RPathHandle_GetPosition(path, 0)->y;
    position->z = RPathHandle_GetPosition(path, 0)->z;
    VU0_MATRIX4_mult(&frame, &frame, &aiSplinePath->placement);
    GetAnchorMatrix4();
    AnchorCamera(false, &fgCameraTables.animationAnchor.offset);
    cameraOffset.x = 0.0f;
    cameraOffset.y = 0.0f;
    cameraOffset.z = 0.0f;
    cameraOffset.w = 1.0f;
    VU0_v4copy(position, &eye);
    VU0_v4copy(&eye, &lookAt);
    MatrixCopy(&frame, &matrix);
    Coord4 *row3 = MatrixRow(&matrix, 3);
    row3->x = eye.x;
    row3->y = eye.y;
    row3->z = eye.z;
    row3->w = 1.0f;
    matrix.mtx[0][3] = 0.0f;
    matrix.mtx[1][3] = 0.0f;
    matrix.mtx[2][3] = 0.0f;
    matrixChanged = 1;
    unknown50.x = path->velocity.x;
    unknown50.y = path->velocity.y;
    unknown50.z = path->velocity.z;
}

// How far the auto-drive camera's field of view has come from the mode's towards the arm's narrowest, 0-1; 0
// in other modes. Unrounded.
// FUNC_AT(0x00080e20)
double RPlayerCamera::GetZoomPercent() {
    const CameraModeInfo *mode = &fgCameraTables.modes[cameraMode];
    if (mode->type == kCameraAutoDrive) {
        float widest = mode->defaultFov;
        float narrowest = fgCameraTables.autoDriveArms[autoDriveArm].minFov;
        if (widest > narrowest)
            return (double(widest) - fieldOfView) / (double(widest) - narrowest);
    }
    return 0.0;
}

// The zoom's line for the spline and fixed cameras: from the mode's field of view to minFov over the zoom
// distances.
// FUNC_AT(0x00080e80)
void RPlayerCamera::SetupCameraZoom() {
    const CameraModeInfo *mode = &fgCameraTables.modes[cameraMode];
    double slope;
    float farthest;
    switch (mode->type) {
    case kCameraSpline: {
        const SplineCamInfo *spline = &fgCameraTables.splines[mode->index];
        zoom298 = mode->defaultFov;
        zoom29C = spline->minFov;
        slope = (double(zoom29C) - zoom298) / (double(spline->maxSplineCamDist) - spline->minZoomDist);
        farthest = spline->maxSplineCamDist;
        break;
    }
    case kCameraFixed: {
        const FixedCamInfo *fixed = &fgCameraTables.fixeds[mode->index];
        zoom298 = mode->defaultFov;
        zoom29C = fixed->minFov;
        slope = (double(zoom29C) - zoom298) / (double(fixed->maxZoomDist) - fixed->minZoomDist);
        farthest = fixed->maxZoomDist;
        break;
    }
    default:
        zoom29C = mode->defaultFov;
        zoom2B0 = 0.0f;
        zoom298 = mode->defaultFov;
        autoDriveZoom = 0.0f;
        zoomSlope = 1.0f;
        return;
    }
    autoDriveZoom = float(slope);
    zoom2B0 = float(zoom29C - slope * farthest);   // from the unrounded slope
}

// The road navigator's point at the given height, dropped onto the world's surface there if it has a face; the
// navigator's position itself if not.
// FUNC_AT(0x00081110)
void RPlayerCamera::GetSafeWRoadNavPosition(Coord4 *position, float *height) {
    position->x = roadNav->position.x;
    position->y = *height;
    position->z = roadNav->position.z;
    position->w = 1.0f;
    worldPos->FindClosestFace(AsCoord3(position), true);
    if (worldPos->valid) {
        Coord3 normal;
        if (worldPos->valid) {
            WWorldPos_FaceNormal(worldPos, 0, &normal);
        } else {
            normal.x = 0.0f;
            normal.y = 1.0f;
            normal.z = 0.0f;
        }
        position->y = float(WWorldMath::GetPlaneY(&normal, &worldPos->face.corner[0].Position(), AsCoord3(position)));
    } else {
        VU0_v4copy(&roadNav->position, position);
    }
}

// The heli mode's arm position for the current weapon: the first whose weapons include it, 0 if none (or not a
// heli mode, or no weapon).
// FUNC_AT(0x000811d0)
int8_t RPlayerCamera::FindHeliArmInd(int mode) {
    const CameraModeInfo *entry = &fgCameraTables.modes[mode];
    if (entry->type != kCameraHeli)
        return 0;
    if (WeaponManager->current == -1)
        return 0;
    const HeliCamInfo *heli = &fgCameraTables.helis[entry->index];
    const HeliArmInfo *arms = heli->arms;
    int weapon = WeaponManager->slots[WeaponManager->current].weapon;
    uint32_t weapons[2] = {0, 0};
    weapons[weapon / 32] = 1u << (weapon % 32);
    for (int i = 0; i < heli->armCount; i++) {
        for (int word = 0; word < 2; word++) {
            if (arms[i].weapons[word] & weapons[word])
                return int8_t(i);
        }
    }
    return 0;
}

// The same among the auto-drive arms.
// FUNC_AT(0x00081290)
int8_t RPlayerCamera::FindAutoDriveArmInd(int mode) {
    if (fgCameraTables.modes[mode].type != kCameraAutoDrive)
        return 0;
    if (WeaponManager->current == -1)
        return 0;
    int weapon = WeaponManager->slots[WeaponManager->current].weapon;
    uint32_t weapons[2] = {0, 0};
    weapons[weapon / 32] = 1u << (weapon % 32);
    for (int i = 0; i < fgCameraTables.autoDriveArmCount; i++) {
        for (int word = 0; word < 2; word++) {
            if (fgCameraTables.autoDriveArms[i].weapons[word] & weapons[word])
                return int8_t(i);
        }
    }
    return 0;
}

// The director's anchor change. With unknown14 set (and not in the missile mode) the camera stays where it was:
// the offsets are made again from the new anchor. False without an anchor.
// FUNC_AT(0x00081340)
bool RPlayerCamera::DirectorSetAnchor(RDirectorQueueData *data) {
    PhysicsObject *newAnchor = data->anchor;
    if (newAnchor == NULL)
        return false;
    if (newAnchor == anchor)
        return true;
    if (cameraMode == fgCameraModeIndices.missile || data->unknown14 == 0) {
        SetAnchor(newAnchor);
        return true;
    }
    VU0_v3add(&lookAtOffset, GetAnchorPosition(), &lookAt);
    VU0_v3add(&lookAt, &cameraOffset, &eye);
    SetAnchor(newAnchor);
    unknown228 = vec3distance(&eye, GetAnchorPosition());
    VU0_v4scaleadd(MatrixRow(&matrix, 2), unknown228, MatrixRow(&matrix, 3), &lookAt);
    VU0_v4sub(&lookAt, GetAnchorPosition(), &lookAtOffset);
    VU0_v4sub(&eye, &lookAt, &cameraOffset);
    return true;
}

// FUNC_AT(0x00081430)
void RPlayerCamera::ResetCamera() {
    modeChangeFlags |= kAnchorChanged;
    transitionStep = 0;
    unknown13C = 0;
    transitionActive = 0;
    tumbleCamIndex = 0;
}

// Whether a mode change eases from the old mode to the new: both modes' smoothTrans set, unless the flags say.
// FUNC_AT(0x00081460)
bool RPlayerCamera::DoSmoothModeChange() {
    if (modeChangeFlags & kNoSmoothChange)
        return false;
    if (modeChangeFlags & kForceSmoothChange)
        return true;
    return previousCameraMode < fgCameraTables.modeCount && cameraMode < fgCameraTables.modeCount &&
           fgCameraTables.modes[cameraMode].smoothTrans > 0 && fgCameraTables.modes[previousCameraMode].smoothTrans > 0;
}

// Modes with lookBack look back on request; in the missile, animation and tumble modes the request to stop is
// taken anyway.
// FUNC_AT(0x000814c0)
void RPlayerCamera::SetCameraLookBack(bool lookBack) {
    if (fgCameraTables.modes[cameraMode].lookBack) {
        state->lookingBack = lookBack;
        modeChangeFlags |= kLookBackChanged;
        return;
    }
    if (!lookBack &&
        (cameraMode == fgCameraModeIndices.missile || cameraMode == fgCameraModeIndices.worldAnimation ||
         cameraMode == fgCameraModeIndices.relativeAnimation || cameraMode == fgCameraModeIndices.aiPathAnimation ||
         cameraMode == fgCameraModeIndices.tumble))
        state->lookingBack = 0;
}

// The auto-drive camera's turn speed for the stick's travel (-1..1), through the response table; 0 in other
// modes. Unrounded. (No bounds check: travel beyond 1 reads past the table, as in the original.)
// FUNC_AT(0x00081540)
double RPlayerCamera::SetAutoDriveRotation(float rotation) {
    if (fgCameraTables.modes[cameraMode].type != kCameraAutoDrive)
        return 0.0;
    if (rotation > 0.0f)
        return double(kRotationResponse[Truncate(rotation * kRotationSteps)]) * kRotationUnit * kOneOver255;
    if (rotation < 0.0f)
        return double(kRotationResponse[-Truncate(rotation * kRotationSteps)]) * kRotationUnit * -kOneOver255;
    return 0.0;
}

// Locks on to a point (non-NULL), or lets go; either way the auto-drive rotation, zoom and spin stop.
// Unless `quiet`, a lock-on sets the camera state's flag (and CameraLockOnFlag when not aiming).
// FUNC_AT(0x00081610)
void RPlayerCamera::CameraLockOn(const Coord4 *point, const Coord4 *target, int param, int quiet) {
    unknown290 = 0.0f;
    unknown294 = 0.0f;
    autoDriveRotationX = 0.0f;
    autoDriveRotationY = 0.0f;
    autoDriveRotating = false;
    zoomSlope = 1.0f;
    spinState = 0;
    if (point != NULL) {
        lockOnPoint = point;
        VU0_v4copy(target, &lockOnTarget);
        lockOnParam2 = param;
        lockOnParam1 = param;
        if (quiet == 0) {
            if (!state->aiming)
                CameraLockOnFlag = 1;
            state->lockedOn = 1;
        }
    } else if (lockOnPoint != NULL) {
        lockOnPoint = NULL;
        VU0_v4Init(&lockOnTarget);
        lockOnParam2 = 0;
        lockOnParam1 = 0;
        CameraLockOnFlag = 0;
        state->lockedOn = 0;
    }
}

// FUNC_AT(0x000816e0)
void RPlayerCamera::SetAutoDriveZoom(float zoom) {
    zoomSlope = 1.0f;
    autoDriveZoom = fgCameraTables.autoDriveZoomFactor * zoom;
}

// The aim field of view of the current arm, or the mode's.
// FUNC_AT(0x00081700)
void RPlayerCamera::ToggleAutoDriveZoom(bool aim) {
    if (aim)
        zoom298 = fgCameraTables.autoDriveArms[autoDriveArm].aimFov;
    else
        zoom298 = fgCameraTables.modes[cameraMode].defaultFov;
}

// Queues the relative animation mode, its animation `animationId` from the anchor's list, after `delay` updates,
// blending in over `steps`.
// FUNC_AT(0x00081740)
void RPlayerCamera::TriggerCarAnimationCamera(uint32_t animationId, int steps, uint16_t delay,
                                              PhysicsObject *animAnchor, int flags) {
    state->ResetStateForAnimation();
    RDirectorQueueData *data = static_cast<RDirectorQueueData *>(OperatorNew(sizeof(RDirectorQueueData)));
    if (data != NULL)
        data = data->Construct(delay, uint16_t(fgCameraModeIndices.relativeAnimation), uint16_t(steps),
                               uint16_t(flags | kCarAnimationFlags), NULL, animAnchor, animationId);
    directorQueue->AppendData(data);
    if (data != NULL) {
        data->Destruct();
        OperatorDelete(data);
    }
}

// Looks back on resuming if the mode looks back and the state is not paused; stops looking back if it is.
// FUNC_AT(0x000817f0)
void RPlayerCamera::PauseOff() {
    if (!fgCameraTables.modes[cameraMode].lookBack)
        return;
    RPlayerCamState *camState = state;
    if (camState->lookBackOff) {
        if (camState->lookingBack)
            SetCameraLookBack(false);
        return;
    }
    if (!camState->lookingBack) {
        camState->lookingBack = 1;
        modeChangeFlags |= kLookBackChanged;
    }
}

// vtable slot 6: an action from the camera's input queue, to the camera state's handler for the input scheme.
// FUNC_AT(0x00081840)
void RPlayerCamera::CameraInputCallback(int action, float value) {
    if (InputConfigManager_Get()->scheme == kInputAutoDrive)
        state->AutoDriveCamInputHandler(action, value);
    else
        state->DriveCamInputHandler(action, value);
}

// The aim's direction: the camera's forward axis at 0, forwardAimVec at 1, between them (x, y and z) otherwise.
// FUNC_AT(0x00081890)
const Coord4* RPlayerCamera::GetForwardAimVec4(float blend) {
    // XBE_GLOBAL(0x001ec3d0, 16)
    static Coord4 blended;
    if (blend == 1.0f)
        return &forwardAimVec;
    if (blend == 0.0f)
        return MatrixRow(&matrix, 2);
    LerpXYZ(MatrixRow(&matrix, 2), &forwardAimVec, blend, &blended);
    return &blended;
}

// FUNC_AT(0x00081930)
void RPlayerCamera::SetAutoDriveForwardLock(bool lock) {
    autoDriveForwardLock = lock;
}

// FUNC_AT(0x00081940)
void RPlayerCamera::SetControlToCPU(bool cpu) {
    state->unknown07 = cpu;
}

// FUNC_AT(0x00081950)
void RPlayerCamera::WeaponFired(int weapon) {
    if (glbMissionManager->unknown478 && weapon == kWeapon1C)
        weaponFired = SimStepCount;
}

// FUNC_AT(0x00081980)
uint8_t RPlayerCamera::CameraAiming() {
    if (state == NULL)
        return 0;
    return state->aiming;
}

// A half turn about the anchor's up axis, in 36 steps of the quaternion spinVec.
// FUNC_AT(0x000819a0)
void RPlayerCamera::InitSpin() {
    Coord4 up = *MatrixRow(GetAnchorMatrix4(), 1);
    spinVec.x = up.x * kSpinSine;
    spinState = kSpinSteps;
    spinVec.w = kSpinCosine;
    spinVec.y = up.y * kSpinSine;
    spinVec.z = up.z * kSpinSine;
}

// After a weapon change, the auto-drive arm for the new weapon.
// FUNC_AT(0x00081a20)
void RPlayerCamera::InitWeaponChange() {
    if (fgCameraTables.modes[cameraMode].type != kCameraAutoDrive)
        return;
    int8_t arm = FindAutoDriveArmInd(cameraMode);
    if (arm != autoDriveArm) {
        modeChangeFlags |= kAnchorChanged;
        fgCameraTables.previousAutoDriveArm = autoDriveArm;
        autoDriveArm = arm;
    }
}

// FUNC_AT(0x00081a70)
void RPlayerCamera::InitAimZoom() {
    aimPitch = 0.0f;
    aimYaw = 0.0f;
    const float *aimFov = &fgCameraTables.autoDriveArms[autoDriveArm].aimFov;
    if (*aimFov == 0.0f)
        return;
    if (state->aiming == 1)
        zoom298 = *aimFov;
    else
        zoom298 = fgCameraTables.modes[cameraMode].defaultFov;
}

// FUNC_AT(0x00081ad0)
void RPlayerCamera::ResetZoomSlope() {
    autoDriveZoom = 0.0f;
    zoomSlope = 1.0f;
}

// FUNC_AT(0x00081af0)
int RPlayerCamera::GetMaxTumble() {
    return fgCameraConstants.kMaxTumble;
}

// The destructor: the navigator, world position, director's queue, spline, state, tuning loader and collider,
// then RWorldCamera's. (The world position's, state's and loader's destructors are the empty function.)
// FUNC_AT(0x00081bf0)
void RPlayerCamera::Destruct() {
    vtable = PlayerCameraVtable;
    if (roadNav != NULL) {
        roadNav->Destruct();
        UMemory::FastFree(roadNav, sizeof(WRoadNav));
    }
    if (worldPos != NULL) {
        NullFunction();
        UMemory::FastFree(worldPos, sizeof(WWorldPos));
    }
    if (directorQueue != NULL) {
        directorQueue->Destruct();
        UMemory::FastFree(directorQueue, sizeof(RDirectorQueue));
    }
    if (spline != NULL) {
        spline->Destruct();
        UMemory::FastFree(spline, sizeof(RCameraSpline));
    }
    if (state != NULL) {
        NullFunction();
        UMemory::FastFree(state, sizeof(RPlayerCamState));
    }
    if (iniLoader != NULL) {
        NullFunction();
        OperatorDelete(iniLoader);
    }
    if (collider != NULL) {
        collider->Destruct();
        UMemory::FastFree(collider, sizeof(WCollider));
    }
    RWorldCamera::Destruct();
}

// The dashboard camera: the arm (eased in its turn, the inertia scaled by speed), pitched with the slope, swayed
// by the anchor's acceleration and the steering, and turned by the glance.
// FUNC_AT(0x00081d10)
void RPlayerCamera::UpdateDashboardCam() {
    const CameraModeInfo *mode = &fgCameraTables.modes[cameraMode];
    bool stopped = SimState == kSimState3;
    if (mode->index < 0 || mode->index >= fgCameraTables.dashboardCount)
        return;
    const DashboardCamInfo *dashboard = &fgCameraTables.dashboards[mode->index];
    RPlayerCamState *camState = state;

    if (camState->lookingBack == 1 && mode->lookBack) {
        MATRIX4 *anchorMatrix = GetAnchorMatrix4();
        const Coord3 *anchorPosition = GetAnchorPosition();
        Coord4 arm = {};
        Coord4 position = {};   // (w: the original's stack, copied into eye.w below)
        VU0_MATRIX4_vect3rotate(&dashboard->backwardsArm, anchorMatrix, &arm);
        VU0_v3add(anchorPosition, &arm, &position);
        if (modeChangeFlags != 0) {
            eye = position;
        } else {
            eye.x = position.x;
            eye.y = float((double(position.y) - eye.y) * fgCameraConstants.kBumperYLerpRate + eye.y);
            eye.z = position.z;
        }
        VU0_v3add(&eye, &arm, &lookAt);
        SetViewingTransform(MatrixRow(anchorMatrix, 1), GetAnchorLinearVelocity(), 0);
        RCamera_SetFieldOfView(this, 0, mode->defaultFov);
        return;
    }

    Coord4 arm = dashboard->forwardArm;
    MATRIX4 *anchorMatrix = GetAnchorMatrix4();
    const Coord3 *anchorPosition = GetAnchorPosition();
    if (modeChangeFlags == 0 && anchor->type == kAnchorRigidBody) {
        if (!stopped) {
            float slope = anchorMatrix->mtx[2][1];
            double vertigo;
            if (slope < 0.0f)
                vertigo = double(arm.z) * dashboard->maxVertigoDownhill * slope;
            else
                vertigo = -(double(arm.z) * dashboard->maxVertigoUphill * slope);
            if (vertigo < -kVertigoLimit)
                vertigo = -kVertigoLimit;
            else if (vertigo > kVertigoLimit)
                vertigo = kVertigoLimit;
            unknown258 = float((vertigo - unknown258) * dashboard->vertigoLerp + unknown258);
        }
        arm.y = arm.y + unknown258;
    }

    Coord4 offset = {};         // (w: the original's stack, copied into cameraOffset.w below)
    VU0_MATRIX4_vect3rotate(&arm, anchorMatrix, &offset);
    if (modeChangeFlags != 0) {
        cameraOffset = offset;
        VU0_v3add(anchorPosition, &offset, &eye);
    } else {
        if (!stopped) {
            float step = SimTimeStep;
            float speed = Abs(GetAnchorSpeed());
            float inertia = float(double(speed) * (double(step) * kStepsPerSecond) * dashboard->intertiaScale);
            unknown25C = CameraClamp(dashboard->intertiaMax, inertia, dashboard->intertiaMin);
        }
        unknown228 = VU0_v3length(&offset);
        Coord4 ease;
        VU0_v4sub(&offset, &cameraOffset, &ease);
        VU0_v4scale(&ease, unknown25C, &ease);
        VU0_v3add(&cameraOffset, &ease, &cameraOffset);
        VU0_v3add(anchorPosition, &cameraOffset, &eye);
    }
    eye.w = 1.0f;
    if (camState->lookingBack == 1 && mode->lookBack)
        VU0_v4sub(&eye, MatrixRow(anchorMatrix, 2), &lookAt);
    else
        VU0_v3add(&eye, MatrixRow(anchorMatrix, 2), &lookAt);
    lookAt.w = 1.0f;

    Coord4 up = *MatrixRow(anchorMatrix, 1);
    float sway[4] = {0.0f, 0.0f, 0.0f, 0.0f};       // SetViewingTransform's offset: along row 0, along up
    if (!stopped) {
        Coord4 acceleration = {};   // (the original's stack when the anchor answers nothing)
        GetAnchorAcceleration(&acceleration);
        VU0_v4scale(&acceleration, -SimTimeStep, &acceleration);
        VU0_v4multxyz(&acceleration, &dashboard->torqueScale, &acceleration);
        acceleration.x = CameraClamp(dashboard->torqueMax.x, acceleration.x, -dashboard->torqueMax.x);
        acceleration.z = CameraClamp(dashboard->torqueMax.z, acceleration.z, -dashboard->torqueMax.z);
        Lerp4(&unknown240, &acceleration, dashboard->torquePace);
    }
    up.x = up.x - unknown240.x;
    sway[1] = sway[1] - unknown240.z;
    if (dashboard->steerScale != 0.0f) {
        if (!stopped) {
            float steer = CameraClamp(dashboard->steerMax, camState->unknown24 * dashboard->steerScale,
                                      -dashboard->steerMax);
            unknown250 = float((double(steer) - unknown250) * dashboard->steerPace + unknown250);
        }
        sway[0] = unknown250;
    }
    SetViewingTransform(&up, sway, GetAnchorLinearVelocity(), 0);
    if (mode->defaultFov > kMinFieldOfView)
        fieldOfView = mode->defaultFov;

    if (dashboard->glanceScale == 0.0f)
        return;
    if (!stopped) {
        float glance = CameraClamp(dashboard->glanceMax, camState->unknown28 * dashboard->glanceScale,
                                   -dashboard->glanceMax);
        unknown254 = float((double(glance) - unknown254) * dashboard->glancePace + unknown254);
    }
    Coord4 anchorUp = *MatrixRow(anchorMatrix, 1);
    Coord4 anchorRight = *MatrixRow(anchorMatrix, 0);
    MATRIX4 turn;
    RCameraMath::BuildRotationMat4(&turn, unknown254 + dashboard->forwardYaw, &anchorUp);
    VU0_MATRIX4_mult(&matrix, &turn, &matrix);
    RCameraMath::BuildRotationMat4(&turn, dashboard->forwardPitch, &anchorRight);
    VU0_MATRIX4_mult(&matrix, &turn, &matrix);
    matrixChanged = 1;
}

// The camera at a 'Cams' instance in the world: on a mode change, a spline from where the camera is to the
// animation's first key (or the instance) over unknown26C steps, then the animation.
// FUNC_AT(0x00082400)
void RPlayerCamera::UpdateWorldAnimationCam() {
    if (SimState == kSimState3 || animHandle == NULL)
        return;
    Coord4 oldEye = eye;
    VU0_v3add(GetAnchorPosition(), &lookAtOffset, &lookAt);
    CARP::Instance *instance = AnimInstanceOf(this);

    if (modeChangeFlags & kAnchorChanged) {
        if (unknown26C != 0) {
            const Coord4 *start = &eye;
            if (instance->flags & kCamAnimFollowsAnchor) {
                VU0_v4sub(&eye, &lookAt, &cameraOffset);
                start = &cameraOffset;
            }
            const Coord4 *end;
            Coord4 endDirection = {};
            MATRIX4 endFrame;
            const CameraAnimData *desc = AnimDescOf(instance);
            if (desc != NULL) {
                const CameraAnimTrack *track = &desc->tracks[instance->procAnimType];
                const CameraAnimKey *keys = track->keys;
                end = &keys[0].position;
                const Coord4 *next = track->keyCount > 1 ? &keys[1].position : end;
                VU0_v4sub(next, end, &endDirection);
                VU0_quattom4(&endFrame, &keys[0].rotation);
                FUN_00080980(&endFrame);
            } else {
                CARPInstance_GetMatrix4(instance, 0, &endFrame);
                end = MatrixRow(&endFrame, 3);
            }
            Coord4 startDirection;
            VU0_v4copy(&unknown50, &startDirection);
            Coord4 toEnd;
            VU0_v4sub(end, start, &toEnd);
            if (v3dotprod(&startDirection, &toEnd) < 0.0f)
                VU0_v4scale(&startDirection, -1.0f, &startDirection);
            startDirection.y = Abs(startDirection.y);
            Coord4 startRotation, endRotation;
            VU0_m4toquat(&startRotation, &matrix);
            VU0_m4toquat(&endRotation, &endFrame);
            spline->BuildSpline(start, &startDirection, end, &endDirection, &startRotation,
                                      &endRotation, kWorldAnimTension);
            unknown13C = unknown26C;
            transitionVec.w = 0.0f;
            transitionFactor = float(1.0 / double(uint32_t(unknown26C)));
        } else if (instance->articleDesc.value != 0) {
            PlayCurrentAnimation();
        }
    }

    bool playing;
    if (unknown13C > 0) {
        transitionVec.w = transitionFactor + transitionVec.w;
        spline->EvaluateSpline(transitionVec.w, &matrix, true);
        matrixChanged = 1;
        double zoom = (1.0 - transitionVec.w) * zoomFov + double(zoomFovTarget) * transitionVec.w;
        if (zoom > kMinFieldOfView)
            fieldOfView = float(zoom);
        unknown13C--;
        if (unknown13C == 0 && instance->articleDesc.value != 0)
            playing = PlayCurrentAnimation() == 1;
        else if (unknown13C > 0)
            playing = true;
        else
            playing = int32_t(spline->GetPointListSize()) < 1;
    } else if (instance->articleDesc.value != 0) {
        uint8_t animating = UpdateAnimationCam(&matrix, 0);
        matrixChanged = 1;
        RCamera_SetFieldOfView(this, 0, zoomFovTarget);
        playing = animating == 1;
    } else {
        playing = false;
    }

    Coord4 *position = MatrixRow(&matrix, 3);
    if (playing) {
        directorQueue->flags |= RDirectorQueue::kHeld;
        if (instance->flags & kCamAnimFollowsAnchor) {
            cameraOffset = *position;
            VU0_v3add(&lookAt, &cameraOffset, position);
            matrixChanged = 1;
        } else {
            VU0_v4sub(position, &lookAt, &cameraOffset);
        }
    } else {
        directorQueue->flags &= ~RDirectorQueue::kHeld;
        if (instance->flags & kCamAnimFollowsAnchor) {
            VU0_v3add(&cameraOffset, &lookAt, position);
            matrixChanged = 1;
            unknown228 = VU0_v3length(&cameraOffset);
            VU0_v4scaleadd(MatrixRow(&matrix, 2), unknown228, position, &lookAt);
            VU0_v4sub(&lookAt, GetAnchorPosition(), &lookAtOffset);
        } else {
            VU0_v4sub(position, &lookAt, &cameraOffset);
        }
    }
    VU0_v4sub(&eye, &oldEye, &unknown50);
    VU0_v4scale(&unknown50, float(SimStepsPerSecond), &unknown50);
    eye = *position;
}

// The camera relative to the anchor's heading: on a mode change (if it eases), a spline from where the camera is
// to the animation's first key (or the instance), in the heading's frame, then the animation; the result turned
// back into the world with the heading. unknown260 turns the path about y.
// FUNC_AT(0x00082840)
void RPlayerCamera::UpdateRelativeAnimationCam() {
    if (SimState == kSimState3 || animHandle == NULL)
        return;
    CARP::Instance *instance = AnimInstanceOf(this);
    const MATRIX4 *anchorMatrix = GetAnchorMatrix4();
    Coord4 heading;
    heading.x = anchorMatrix->mtx[2][0];
    heading.y = 0.0f;
    heading.z = anchorMatrix->mtx[2][2];
    heading.w = 0.0f;           // (the original's stack; not read)
    MATRIX4 headingFrame;
    RCameraMath::MatrixFromDirection(&heading, &headingFrame);
    Coord4 oldEye = eye;
    MATRIX4 *path = &relativeAnimMatrix;

    if (modeChangeFlags & kAnchorChanged) {
        if (DoSmoothModeChange() && unknown26C != 0) {
            Coord4 end, endDirection = {};
            const CameraAnimData *desc = AnimDescOf(instance);
            if (desc != NULL) {
                const CameraAnimTrack *track = &desc->tracks[instance->procAnimType];
                const CameraAnimKey *keys = track->keys;
                end = keys[0].position;
                Coord4 next = track->keyCount > 1 ? keys[1].position : end;
                VU0_quattom4(path, &keys[0].rotation);
                FUN_000809e0(path);
                if (unknown260 != 0.0f) {
                    MATRIX4 turn;
                    VU0_MATRIX4setyrot(&turn, float(FUN_00080820(unknown260)));
                    VU0_MATRIX4_vect3rotate(&next, &turn, &next);
                    VU0_MATRIX4_vect3rotate(&end, &turn, &end);
                    VU0_MATRIX4_mult(path, path, &turn);
                }
                VU0_v4sub(&next, &end, &endDirection);
            } else {
                CARPInstance_GetMatrix4(instance, 0, path);
                end = *MatrixRow(path, 3);
            }
            MATRIX4 toHeading;
            VU0_MATRIX4_transpose(&toHeading, &headingFrame);
            Coord4 start;
            VU0_v3add(&cameraOffset, &lookAtOffset, &start);
            float renderOffset = float(GetAnchorRenderOffset());
            VU0_v4scaleadd(MatrixRow(&headingFrame, 1), renderOffset, &start, &start);
            VU0_MATRIX4_vect3rotate(&start, &toHeading, &start);
            Coord4 startDirection;
            VU0_v4copy(&unknown50, &startDirection);
            Coord4 toEnd;
            VU0_v4sub(&end, &start, &toEnd);
            if (v3dotprod(&startDirection, &toEnd) < 0.0f)
                VU0_v4scale(&startDirection, -1.0f, &startDirection);
            startDirection.y = Abs(startDirection.y);
            MATRIX4 startFrame;
            VU0_MATRIX3x4_mult(&matrix, &toHeading, &startFrame);
            Coord4 startRotation, endRotation;
            VU0_m4toquat(&startRotation, &startFrame);
            VU0_m4toquat(&endRotation, path);
            spline->ClearSplinePtList();
            spline->BuildSpline(&start, &startDirection, &end, &endDirection, &startRotation,
                                      &endRotation, kRelativeAnimTension);
            unknown13C = unknown26C;
            transitionVec.w = 0.0f;
            transitionFactor = float(1.0 / double(uint32_t(unknown26C)));
        } else {
            PlayCurrentAnimation();
        }
    }

    uint8_t playing = 0;        // (the original leaves its stack's byte here when there is no animation to play)
    if (unknown13C > 0) {
        transitionVec.w = transitionFactor + transitionVec.w;
        spline->EvaluateSpline(transitionVec.w, path, true);
        unknown13C--;
        if (unknown13C == 0 && instance->articleDesc.value != 0)
            playing = PlayCurrentAnimation();
        else
            playing = 1;
    } else if (instance->articleDesc.value != 0) {
        playing = UpdateAnimationCam(path, 2);
        if (playing == 1 && unknown260 != 0.0f) {
            MATRIX4 turn;
            VU0_MATRIX4setyrot(&turn, float(FUN_00080820(unknown260)));
            VU0_MATRIX4_vect3rotate(MatrixRow(path, 3), &turn, MatrixRow(path, 3));
            VU0_MATRIX4_mult(path, path, &turn);
        }
    }

    AnchorRelativeCamera(false, &fgCameraTables.animationAnchor.offset);
    VU0_MATRIX4_vect3rotate(MatrixRow(path, 3), &headingFrame, &cameraOffset);
    cameraOffset.w = 1.0f;
    VU0_v3add(&cameraOffset, &lookAt, &eye);
    VU0_MATRIX3x4_mult(path, &headingFrame, &matrix);
    Coord4 *position = MatrixRow(&matrix, 3);
    position->z = eye.z;
    matrix.mtx[0][3] = 0.0f;
    matrix.mtx[1][3] = 0.0f;
    matrix.mtx[2][3] = 0.0f;
    position->x = eye.x;
    position->y = eye.y;
    position->w = 1.0f;
    matrixChanged = 1;
    if (playing == 1) {
        directorQueue->flags |= RDirectorQueue::kHeld;
    } else {
        directorQueue->flags &= ~RDirectorQueue::kHeld;
        unknown228 = VU0_v3length(&cameraOffset);
        VU0_v4scaleadd(MatrixRow(&matrix, 2), unknown228, position, &lookAt);
        VU0_v4sub(&lookAt, GetAnchorPosition(), &lookAtOffset);
        VU0_v4sub(&eye, &lookAt, &cameraOffset);
    }
    VU0_v4sub(&eye, &oldEye, &unknown50);
    VU0_v4scale(&unknown50, float(SimStepsPerSecond), &unknown50);
}

// The auto-drive camera's angles to the selected target: the pitch and yaw from the camera's forward axis, each
// written when inside the arm's auto-aim limits (narrowed with distance and by unknown4dc). While the state holds
// a lock the angles are written until either leaves its limit, which drops the lock. True while locked, or when
// both angles were found.
// FUNC_AT(0x00082e00)
bool RPlayerCamera::UpdateADTargetAngles(float *pitchOut, float *yawOut) {
    if (Launch.autoAim == 0)
        return false;
    WTargetable *target = TargetPicker.GetSelectedTarget();
    const AutoDriveArmInfo *arm = &fgCameraTables.autoDriveArms[autoDriveArm];
    bool haveTarget = target != NULL;
    double share = double(3 - glbMissionManager->unknown4dc) * kOneThird;
    bool pitchFound = false;
    bool yawFound = false;
    float pitchLimit = float(share * arm->maxAutoaimPitch);
    float yawLimit = float(share * arm->maxAutoaimYaw);
    RPlayerCamState *camState = state;
    const Coord4 *forward = MatrixRow(&matrix, 2);
    Coord4 toTarget;

    if (haveTarget) {
        VU0_v4sub(&target->position, &eye, &toTarget);
        float distance = VU0_v3length(&toTarget);
        if (distance < fgCameraConstants.kfMaxAutoaimDistance) {
            float nearness = VU0_sqrt(float(1.0 - distance / double(fgCameraConstants.kfMaxAutoaimDistance)));
            pitchLimit = pitchLimit * nearness;
            yawLimit = nearness * yawLimit;
            VU0_v4scale(&toTarget, distance, &toTarget);
            VU0_v4sub(&target->position, &eye, &toTarget);
            VU0_v4unitxyz(&toTarget, &toTarget);
            Coord4 level;
            level.x = toTarget.x;
            level.y = forward->y;
            level.z = toTarget.z;
            level.w = 0.0f;
            float cosine = v3dotprod(&level, &toTarget);
            float pitch = float(CameraAcosTurns(cosine / VU0_v3length(&level)));
            if (camState->unknown0B) {
                if (pitch > pitchLimit)
                    camState->unknown0B = 0;
                else if (toTarget.y > forward->y)
                    *pitchOut = -pitch;
                else
                    *pitchOut = pitch;
            } else if (pitch < pitchLimit) {
                if (toTarget.y > forward->y)
                    *pitchOut = -pitch;
                else
                    *pitchOut = pitch;
                pitchFound = true;
            }
        } else {
            haveTarget = false;
        }
    }
    if (!haveTarget)
        camState->unknown0B = 0;

    if (haveTarget && (camState->unknown0B || pitchFound)) {
        double cameraTurns = AtanTurnsUnrounded(forward->z, forward->x);
        float cameraYaw = float(cameraTurns);
        if (cameraTurns < 0.0)
            cameraYaw = cameraYaw + 1.0f;
        double targetYaw = AtanTurnsUnrounded(toTarget.z, toTarget.x);
        if (targetYaw < 0.0)
            targetYaw = targetYaw + 1.0;
        float yaw = float(cameraYaw - targetYaw);
        if (camState->unknown0B) {
            if (Abs(yaw) > yawLimit)
                camState->unknown0B = 0;
            else
                *yawOut = yaw;
        } else if (Abs(yaw) < yawLimit) {
            *yawOut = yaw;
            yawFound = true;
        }
    } else {
        camState->unknown0B = 0;
    }
    return camState->unknown0B || (yawFound && pitchFound);
}
