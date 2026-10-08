#pragma fp_contract(off)

#include "PlayerCameraB.h"

#include "PlayerCameraA.h"            // CameraAsinTurns, CameraAcosTurns, CameraClamp
#include "PlayerCamState.h"

#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../engine/MissionManager.h"
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../world/RoadNav.h"
#include "../world/RoadNetwork.h"

#include <bit>
#include <math.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// RPlayerCamera, 0x00083190..0x00086bb0, ported from the listing. See PlayerCameraB.h.
//
// The x87 code is ported bit for bit: what the original keeps on the x87 stack is computed in double in its order,
// rounded to float where it stores a float; comparisons keep their NaN behaviour.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// SetCameraZoom's kinds (the names are ours)
enum ZoomKind : int {
    kZoomToFov = 1,                     // to the field of view given
    kZoomBetween = 2,                   // between the mode's zoom (0) and the auto-drive arm's minFov (1)
    kZoomByDistance = 3,                // the field of view SetupCameraZoom's slope gives for a distance
};

constexpr int kYawBands = kAutoDriveLimits;     // an auto-drive arm's bands of pitch and yaw limits
constexpr float kMinArmTurn = 0.001f;   // turns: less is no turn
constexpr float kLaneOffset = 2.5f;     // the spline camera's offset across a road of several lanes
constexpr float kSplineStartDistance = 6.0f;
constexpr float kDegreesToTurns = 0.00277777808f;
constexpr float kOneSixtieth = 0.0166666675f;
constexpr float kOneThirtyThird = 0.0303030312f;
constexpr float kSpinStep = 0.013888889f;       // 1/72 of a turn a step
constexpr float kAimFollow = 0.8f;              // the lock-on's easing towards the target's angles
constexpr float kYawScaleSlope = 0.571428597f;  // the yaw cursor's speed from its travel: 4/7 of it
constexpr float kYawScaleBase = 0.16f;
constexpr float kPitchScaleSlope = 0.8f;
constexpr float kPitchScaleBase = 0.08f;
constexpr float kMaxCursorScale = 0.5f;
constexpr float kScreenScaleX = 0.245f;         // the aim's offset on screen, as a share of the width
constexpr float kScreenScaleY = 0.31f;          // ... and of the height
constexpr float kFovToHalfTurns = 0.00138888904f;   // degrees to turns, halved: 1/720
constexpr float kRecentreInput = 0.0025f;       // the steering that stops the auto-drive camera recentring
constexpr float kRecentred = 0.0001f;           // turns: close enough to the rest position
static_assert(std::bit_cast<uint32_t>(kMinArmTurn) == 0x3a83126f, "0x001918cc");
static_assert(std::bit_cast<uint32_t>(kDegreesToTurns) == 0x3b360b62, "0x001918c8");
static_assert(std::bit_cast<uint32_t>(kOneSixtieth) == 0x3c888889, "0x00189fd4");
static_assert(std::bit_cast<uint32_t>(kOneThirtyThird) == 0x3cf83e10, "0x001918e8");
static_assert(std::bit_cast<uint32_t>(kSpinStep) == 0x3c638e39, "0x001918d0");
static_assert(std::bit_cast<uint32_t>(kAimFollow) == 0x3f4ccccd, "0x0018b8ec");
static_assert(std::bit_cast<uint32_t>(kPitchScaleSlope) == 0x3f4ccccd, "0x0018b8ec");
static_assert(std::bit_cast<uint32_t>(kYawScaleSlope) == 0x3f124925, "0x001918e4");
static_assert(std::bit_cast<uint32_t>(kYawScaleBase) == 0x3e23d70a, "0x001918e0");
static_assert(std::bit_cast<uint32_t>(kPitchScaleBase) == 0x3da3d70a, "0x0018a49c");
static_assert(std::bit_cast<uint32_t>(kScreenScaleX) == 0x3e7ae148, "0x001918dc");
static_assert(std::bit_cast<uint32_t>(kScreenScaleY) == 0x3e9eb852, "0x001918d4");
static_assert(std::bit_cast<uint32_t>(kFovToHalfTurns) == 0x3ab60b62, "0x001918d8");
static_assert(std::bit_cast<uint32_t>(kRecentreInput) == 0x3b23d70a, "0x0018a4c4");
static_assert(std::bit_cast<uint32_t>(kRecentred) == 0x38d1b717, "0x0018cb6c");

}  // namespace

// ---- the game's globals

// The auto-drive arms' change of arm (the names are ours), with fgCameraTables.weaponArmTurn and
// previousAutoDriveArm
#define WeaponArmFrom (*(Coord4 *)0x001c4180)               // the rotation the change starts from
#define WeaponArmBlend FLOAT_AT(0x001c4190)                 // how far it has gone, 0 to 1
// The auto-drive camera's steering the step before (the names are ours)
#define PreviousRotationX FLOAT_AT(0x001ec3e0)
#define PreviousRotationY FLOAT_AT(0x001ec3e4)
#define DiagonalBoost FLOAT_AT(0x001ec3c4)                  // 0.005 * 0.2 (a static initialiser)

#define SimTimeStep FLOAT_AT(0x00234e30)                    // the simulation's step, in seconds
#define SimStepCount I32_AT(0x00234e34)
#define SimState I32_AT(0x00234e24)
#define playerPhysicsObject (*(RigidVehicle ***)0x00234e40)
#define fgRenderer (*(CameraRendererFields **)0x001ebff4)
#define Colorize (*(void **)0x001f6898)                     // the RColorize post-process

// ---- calls to originals not ported

// RCamera's field of view, if above 2 degrees (an inline the compiler sometimes kept)
#define RCamera_SetFieldOfView ((void (__fastcall *)(RPlayerCamera *, int, float fov))0x00011000)
// The helpers before them (unnamed)
#define WrapTurns ((double (*)(float turns))0x00080820)             // into (-1, 1), negatives then into [0, 1)
#define HeadingTurns ((double (*)(float x, float z))0x000808c0)     // atan_turns(x, z) in [0, 1)
#define SinTurnsWrapped ((double (*)(float turns))0x00080860)       // FSIN's result, unrounded
#define TanTurnsWrapped ((double (*)(float turns))0x00080890)       // FPTAN's result, unrounded
#define WRoadNav_LaneCount ((int8_t (__fastcall *)(WRoadNav *, int))0x00080a30)
#define RColorize_SetEnabled ((void (__fastcall *)(void *, int, int mode))0x0009a500)

namespace {

double (*const SinTurnsUnrounded)(float) = SinTurnsWrapped;
double (*const TanTurnsUnrounded)(float) = TanTurnsWrapped;

// FSIN, FCOS and FPTAN give 64-bit results that the original multiplies or divides by straight away, rounding only
// then; these keep that operation on the x87.
__declspec(naked) double SinTurnsTimes(float turns, float factor) {
    __asm {
        push dword ptr [esp + 4]
        call dword ptr [SinTurnsUnrounded]
        add esp, 4
        fmul dword ptr [esp + 8]
        ret
    }
}

__declspec(naked) double CosTurnsTimes(float turns, float factor) {
    __asm {
        push dword ptr [esp + 4]
        call cos_fractionalangle
        add esp, 4
        fmul dword ptr [esp + 8]
        ret
    }
}

__declspec(naked) double TanTurnsTimes(float turns, int factor) {
    __asm {
        push dword ptr [esp + 4]
        call dword ptr [TanTurnsUnrounded]
        add esp, 4
        fimul dword ptr [esp + 8]
        ret
    }
}

// value / tan(turns)
__declspec(naked) double DivideByTanTurns(float value, float turns) {
    __asm {
        push dword ptr [esp + 8]
        call dword ptr [TanTurnsUnrounded]
        add esp, 4
        fdivr dword ptr [esp + 4]
        ret
    }
}

// FPATAN's result times 1/2pi, unrounded: atan_turns is assembly that leaves it in ST0.
typedef double (*UnroundedTurnsFn)(float, float);
double AtanTurnsUnrounded(float y, float x) {
    return reinterpret_cast<UnroundedTurnsFn>(&atan_turns)(y, x);
}

Coord4 Widened(const Coord3 &v) {
    Coord4 out = {v.x, v.y, v.z, 0.0f};
    return out;
}

// out = from + (to - from) * t in x, y and z, as the original's inlined SSE does it; w is kept
void EaseXYZ(const Coord4 *from, const Coord4 *to, float t, Coord4 *out) {
    float x = t * (to->x - from->x) + from->x;
    float y = t * (to->y - from->y) + from->y;
    float z = t * (to->z - from->z) + from->z;
    out->x = x;
    out->y = y;
    out->z = z;
}

RigidBody *PlayerBody() {
    return (*playerPhysicsObject)->GetRigidBody();
}

CarAudioFlags *PlayerCarAudio() {
    typedef CarAudioFlags *(RigidVehicle::*GetAudioMethod)();
    RigidVehicle *car = *playerPhysicsObject;
    return (car->*XbeVirtual<GetAudioMethod>(car, 7))();
}

// The missile mode's colouring off, back to the mode before it (inlined twice in DirectorChangeCameraMode)
void LeaveMissileView(RPlayerCamera *camera) {
    RPlayerCamState *state = camera->state;
    if (state->unknown20 || camera->cameraMode == fgCameraModeIndices.missile) {
        state->unknown20 = false;
        RColorize_SetEnabled(Colorize, 0, 0);
        camera->cameraMode = camera->previousCameraMode;
        fgCameraTables.weaponArmTurn = 0.0f;
    }
}

// The frame turned by `rotation` from the player's car's orientation and position, at the eye (inlined twice in
// UpdateADWeaponAnims)
void SetFrameFromCar(RPlayerCamera *camera, const Coord4 *rotation, const MATRIX4 *orientation) {
    MATRIX4 turn;
    VU0_quattom4(&turn, rotation);
    MATRIX4 car = *orientation;
    const RigidBody *body = PlayerBody();
    car.mtx[3][0] = body->position.x;
    car.mtx[3][1] = body->position.y;
    car.mtx[3][2] = body->position.z;
    car.mtx[3][3] = 1.0f;
    MATRIX4_mult(&turn, &car, &camera->matrix);
    *MatrixRow(&camera->matrix, 3) = camera->eye;
}

}  // namespace

// ---- the pitch and yaw limits

// FUNC_AT(0x00083190)
bool RPlayerCamera::LimitPitchYaw(const AutoDriveArmInfo *arm, float *pitch, float *yaw) {
    if (arm->maxPitch[0] == 0.0f && arm->minPitch[0] == 0.0f)
        return false;
    bool limited = false;
    bool yawLimited = !(arm->maxYaw[0] == 0.0f && arm->minYaw[0] == 0.0f);

    // The band the yaw is in. The bands end at the first without a maximum; a band whose limits are equal runs
    // from the last band's minimum. `middle` is the last middle worked out, whichever band it was.
    float middle = 0.0f;
    int band;
    for (band = 0; band < kYawBands; band++) {
        if (arm->maxYaw[band] == 0.0f) {
            if (band > 0)
                band--;
            break;
        }
        if (yawLimited && arm->minYaw[band] == arm->maxYaw[band]) {
            if (band == 0) {
                if (*yaw < arm->maxYaw[0])
                    break;
            } else if (!(*yaw < arm->minYaw[band - 1]) && *yaw < arm->maxYaw[band]) {
                break;
            }
            continue;
        }
        middle = float((double(arm->maxYaw[band]) + arm->minYaw[band]) * 0.5);
        if (*yaw < middle) {
            if (*yaw > arm->minYaw[band])
                break;
        } else if (*yaw < arm->maxYaw[band]) {
            break;
        }
    }

    // The yaw's limits, and the pitch's there: the band's, blended with the band before by where the yaw is
    float upper, lower;
    double maxPitchLimit;
    float minPitchLimit;
    if (band >= kYawBands) {
        upper = arm->maxYaw[kYawBands - 1];
        lower = arm->minYaw[kYawBands - 1];
        minPitchLimit = float(-CameraAsinTurns(arm->maxPitch[kYawBands - 1]));
        maxPitchLimit = -CameraAsinTurns(arm->minPitch[kYawBands - 1]);
    } else {
        float maxHere = float(-CameraAsinTurns(arm->minPitch[band]));
        float maxBelow = 0.0f, minBelow = 0.0f;
        if (band > 0)
            maxBelow = float(-CameraAsinTurns(arm->minPitch[band - 1]));
        float minHere = float(-CameraAsinTurns(arm->maxPitch[band]));
        if (band > 0)
            minBelow = float(-CameraAsinTurns(arm->maxPitch[band - 1]));

        if (yawLimited && arm->minYaw[band] == arm->maxYaw[band]) {
            upper = arm->maxYaw[band];
            if (band > 0) {
                const float below = arm->minYaw[band - 1];
                lower = below;
                float span = float(double(arm->maxYaw[band]) - below);
                float t = span;
                if (span != 0.0f) {
                    double share = (double(*yaw) - below) / span;
                    t = float(share);
                    if (share > 1.0)
                        t = 1.0f;
                    else if (t < 0.0f)
                        t = 0.0f;
                }
                double maxChange = double(maxHere) - maxBelow;
                float maxChangeF = float(maxChange);
                maxPitchLimit = maxChange * t + maxBelow;
                double minChange = double(minHere) - minBelow;
                float minChangeF = float(minChange);
                minPitchLimit = float(minChange * t + minBelow);

                // A pitch past a limit that changes faster than the yaw moves the yaw instead
                if (maxPitchLimit < *pitch && Abs(maxChangeF) > Abs(span)) {
                    double s = maxChangeF;
                    if (maxChangeF != 0.0f) {
                        s = (double(*pitch) - maxBelow) / maxChangeF;
                        if (s > 1.0)
                            s = 1.0;
                        else if (s < 0.0)
                            s = 0.0;
                    }
                    *yaw = float(span * s + below);
                    maxPitchLimit = s * maxChangeF + maxBelow;
                }
                if (*pitch < minPitchLimit && Abs(minChangeF) > Abs(span)) {
                    double s = minChangeF;
                    if (minChangeF != 0.0f) {
                        s = (double(*pitch) - minBelow) / minChangeF;
                        if (s > 1.0)
                            s = 1.0;
                        else if (s < 0.0)
                            s = 0.0;
                    }
                    *yaw = float((double(arm->maxYaw[band]) - below) * s + below);
                    minPitchLimit = float(minChangeF * s + minBelow);
                }
            } else {
                lower = arm->minYaw[0];
                minPitchLimit = minHere;
                maxPitchLimit = maxHere;
            }
        } else {
            upper = arm->maxYaw[band];
            lower = arm->minYaw[band];
            if (band > 0) {
                double s;
                if (*yaw < middle) {
                    s = double(arm->minYaw[band]) - arm->minYaw[band - 1];
                    if (s != 0.0) {
                        s = (double(*yaw) - arm->minYaw[band - 1]) / s;
                        if (s > 1.0)
                            s = 1.0;
                        else if (s < 0.0)
                            s = 0.0;
                    }
                } else {
                    s = double(arm->maxYaw[band]) - arm->maxYaw[band - 1];
                    if (s != 0.0) {
                        s = (double(*yaw) - arm->maxYaw[band - 1]) / s;
                        if (s > 1.0)
                            s = 1.0;
                        else if (s < 0.0)
                            s = 0.0;
                    }
                }
                maxPitchLimit = float((double(maxHere) - maxBelow) * s + maxBelow);
                minPitchLimit = float((double(minHere) - minBelow) * s + minBelow);
            } else {
                minPitchLimit = minHere;
                maxPitchLimit = maxHere;
            }
        }
    }

    if (yawLimited) {
        // A limit past half a turn takes the yaw a turn round to meet it
        if (upper >= 0.5f && *yaw > upper)
            *yaw -= 1.0f;
        if (lower <= -0.5f && *yaw < lower)
            *yaw += 1.0f;
        if (*yaw >= upper) {
            *yaw = upper;
            limited = true;
        }
        if (*yaw <= lower) {
            *yaw = lower;
            limited = true;
        }
    } else if (*yaw > 1.0f) {
        *yaw -= 1.0f;
    } else if (*yaw < -1.0f) {
        *yaw += 1.0f;
    }
    if (maxPitchLimit <= *pitch) {
        *pitch = float(maxPitchLimit);
        limited = true;
    }
    if (*pitch <= minPitchLimit) {
        *pitch = minPitchLimit;
        limited = true;
    }
    return limited;
}

// ---- zoom

// FUNC_AT(0x00083940)
void RPlayerCamera::SetCameraZoom(int kind, float value, int steps) {
    switch (kind) {
    case kZoomToFov:
        if (steps != 0)
            RWorldCamera::SetCameraZoom(value, fgCameraConstants.kZoomIncSpeed * zoomSlope);
        else
            RCamera_SetFieldOfView(this, 0, value);
        return;

    case kZoomBetween: {
        const CameraModeInfo &mode = fgCameraTables.modes[cameraMode];
        float armMinFov = fgCameraTables.autoDriveArms[autoDriveArm].minFov;
        float fov = mode.defaultFov;
        if (mode.defaultFov > armMinFov) {
            fov = float(mode.defaultFov - (double(mode.defaultFov) - armMinFov) * value);
            if (fov <= 0.0f)
                return;
            zoom298 = fov;
        }
        if (steps != 0) {
            double slope = 1.0 / steps;
            zoomSlope = float(slope);
            RWorldCamera::SetCameraZoom(fov, float(slope * fgCameraConstants.kZoomIncSpeed));
        } else {
            RCamera_SetFieldOfView(this, 0, fov);
        }
        return;
    }

    case kZoomByDistance: {
        float fov = float(double(value) * autoDriveZoom + zoom2B0);
        if (fov <= 0.0f)
            return;
        if (steps == 0) {
            RCamera_SetFieldOfView(this, 0, fov);
            return;
        }
        // at most kZoomIncSpeed degrees a step, and not past the zoom's limits
        float widest = fgCameraConstants.kZoomIncSpeed + fieldOfView;
        if (fov > widest) {
            RCamera_SetFieldOfView(this, 0, Min(widest, zoom298));
            return;
        }
        float narrowest = fieldOfView - fgCameraConstants.kZoomIncSpeed;
        if (fov < narrowest) {
            RCamera_SetFieldOfView(this, 0, Max(narrowest, zoom29C));
            return;
        }
        RCamera_SetFieldOfView(this, 0, fov);
        return;
    }
    }
}

// ---- keeping the camera outside the objects round it

// The point moved out to the surface of the ellipsoid round a rigid or simple body (its box, or its radius, scaled
// by kCameraObjectRadiusEx), straight up or down in the body's frame, when it is inside; whether it was.
// FUNC_AT(0x00083b40)
bool RPlayerCamera::AdjustCamAroundObjectEllipse(Coord4 *position, RigidBody *body, SimpleRigidBody *simpleBody,
                                                 int unknown) {
    Coord4 extents;
    const MATRIX4 *orientation;
    const Coord3 *centre;
    MATRIX4 simpleOrientation;
    if (body != NULL) {
        const RigidBodyInfo *info = body->info;
        extents.x = fgCameraConstants.kCameraObjectRadiusEx.x * info->halfExtents.x;
        extents.y = fgCameraConstants.kCameraObjectRadiusEx.y * info->halfExtents.y;
        extents.z = fgCameraConstants.kCameraObjectRadiusEx.z * info->halfExtents.z;
        extents.w = fgCameraConstants.kCameraObjectRadiusEx.w * info->halfExtents.w;
        orientation = &info->orientation;
        centre = &body->position;
    } else {
        VU0_quattom4(&simpleOrientation, &simpleBody->orientation);
        VU0_v4scale(&fgCameraConstants.kCameraObjectRadiusEx, simpleBody->radius, &extents);
        orientation = &simpleOrientation;
        centre = &simpleBody->position;
    }
    Coord4 offset;
    VU0_v4sub(position, centre, &offset);
    MATRIX4 inverse;
    VU0_MATRIX4_transpose(&inverse, orientation);
    Coord4 local = {};
    VU0_MATRIX4_vect3rotate(&offset, &inverse, &local);
    if (!(Abs(local.x) < extents.x && Abs(local.y) < extents.y && Abs(local.z) < extents.z))
        return false;

    double xx = double(local.x) * local.x;
    double yy = double(local.y) * local.y;
    double zz = double(local.z) * local.z;
    extents.x = extents.x * extents.x;
    extents.y = extents.y * extents.y;
    extents.z = extents.z * extents.z;
    float xShare = float(xx / extents.x);
    float zShare = float(zz / extents.z);
    if (!(yy / extents.y + zShare + xShare < 1.0))
        return false;
    local.y = VU0_sqrt(float((1.0 - xShare - zShare) * extents.y));
    VU0_MATRIX4_vect3rotate(&local, orientation, position);
    position->y = Abs(position->y);
    VU0_v3add(position, centre, position);
    return true;
}

// ---- transitions: the camera carried from where it was to a new place over a number of steps

// FUNC_AT(0x00083d70)
void RPlayerCamera::InitTransition(char active, Coord4 *from, Coord4 *to, int steps) {
    VU0_v4sub(from, to, &transitionVec);
    transitionVec.w = VU0_v3length(&transitionVec);     // the distance left
    if (!(transitionVec.w > 0.0f)) {
        transitionStep = 0;
        unknown13C = 0;
        transitionActive = 0;
        return;
    }
    if (steps == 0)
        steps = fgCameraConstants.kDefualtTransition;
    VU0_v4unitxyz(&transitionVec, &transitionVec);
    unknown13C = steps;
    transitionStep = 1;
    transitionActive = active;
    transitionFactor = float(transitionVec.w / steps);
}

// FUNC_AT(0x00083e10)
void RPlayerCamera::UpdateTransition(Coord4 *position, Coord4 *target) {
    unknown13C--;
    float left = transitionVec.w - transitionFactor;
    transitionVec.w = left;
    transitionStep = 0;
    VU0_v4scaleadd(&transitionVec, left, target, position);
}

// ---- shaking

// A shake for an explosion `range` from `origin` lasting `duration` seconds, if the camera is in range: as strong
// as the mode allows, and as long as any shake under way.
// FUNC_AT(0x00083e60)
void RPlayerCamera::ShakeCamera(int duration, float range, Coord4 *origin, int keepOrigin) {
    if (!(VU0_v3distancesquare(&eye, origin) < double(range) * range))
        return;
    float shakeScale = fgCameraTables.modes[cameraMode].explosionShakeScale;
    float byRange = float(double(fgCameraConstants.kExplosionScale) * shakeScale * range);
    double amount = double(shakeScale) * fgCameraConstants.kExplosionMaxShake;
    if (!(amount < byRange))
        amount = byRange;
    if (amount > shakeAmount)
        shakeAmount = float(amount);
    int steps = Truncate(float(double(SimStepsPerSecond * duration) * kOneSixtieth));
    if (steps <= shakeStepsLeft)
        steps = shakeStepsLeft;
    shakeStepsLeft = steps;
    shaking = false;
    if (keepOrigin) {
        VU0_v4copy(&eye, &ShakeOrigin);
        shaking = true;
    }
}

// ---- mode changes: queued for the director, and made when it hands them back

// FUNC_AT(0x00083f50)
void RPlayerCamera::SetCameraModeByIndex(int mode, uint16_t delay, uint16_t flags, uint16_t unknown14,
                                         uint32_t unknown24, CARP::Instance *data18, const Coord4 *position) {
    if (mode >= fgCameraTables.modeCount)
        return;
    RDirectorQueueData *data = static_cast<RDirectorQueueData *>(OperatorNew(sizeof(RDirectorQueueData)));
    if (data != NULL)
        data = data->Construct(delay, mode, unknown14, flags, data18, NULL, unknown24);
    if (position != NULL)
        data->position = *position;
    else
        VU0_v4Init(&data->position);
    directorQueue->AppendData(data);
    if (data != NULL) {
        data->Destruct();
        OperatorDelete(data);
    }
}

// The next selectable mode after the current one (from a selectable or the collision mode, never the cinematic)
// FUNC_AT(0x00084030)
void RPlayerCamera::NextCameraMode(uint16_t delay) {
    const CameraModeInfo &current = fgCameraTables.modes[cameraMode];
    if (!current.selectable && current.type != kCameraCollision)
        return;
    if (cameraMode == fgCameraModeIndices.cinematic)
        return;
    unsigned count = fgCameraTables.modeCount;
    unsigned next = unsigned(cameraMode + 1) % count;
    for (int tried = 0; tried < int(count) && !fgCameraTables.modes[next].selectable; tried++)
        next = (next + 1) % count;
    SetCameraModeByIndex(next, delay, 0, 0, 0, NULL, NULL);
}

// FUNC_AT(0x000840b0)
void RPlayerCamera::PrevCameraMode(uint16_t delay) {
    const CameraModeInfo &current = fgCameraTables.modes[cameraMode];
    if (!current.selectable && current.type != kCameraCollision)
        return;
    if (cameraMode == fgCameraModeIndices.cinematic)
        return;
    unsigned count = fgCameraTables.modeCount;
    unsigned previous = unsigned(cameraMode - 1) % count;
    for (int tried = 0; tried < int(count) && !fgCameraTables.modes[previous].selectable; tried++)
        previous = (previous - 1) % count;
    SetCameraModeByIndex(previous, delay, 0, 0, 0, NULL, NULL);
}

// FUNC_AT(0x00084130)
void RPlayerCamera::DirectorChangeCameraMode(RDirectorQueueData *data) {
    if (data->cameraMode >= fgCameraTables.modeCount)
        return;
    switch (fgCameraTables.modes[data->cameraMode].type) {
    case kCameraSpline: {
        bool roads = fgRoadNetworkData.loaded;
        WRoadNetwork::Get();
        if (!roads)
            return;
        break;
    }

    case kCameraHeli:
        if (data->cameraMode == fgCameraModeIndices.cinematic) {
            fgCameraTables.modes[fgCameraModeIndices.cinematic].smoothTrans = int8_t(data->unknown14);
            // the cinematic heli's first arm placed by the change's position: Heli_Sideways, Heli_Height,
            // Heli_Distance and the word after them
            HeliArmInfo *arm = fgCameraTables.helis[fgCameraModeIndices.cinematicHeli - 1].arms;
            memcpy(&arm->sideways, &data->position, sizeof(Coord4));
        } else if (cameraMode == data->cameraMode) {
            int8_t previous = currentArm;
            currentArm = FindHeliArmInd(cameraMode);
            if (currentArm == previous)
                return;
        } else {
            currentArm = FindHeliArmInd(data->cameraMode);
        }
        break;

    case kCameraAnimation: {
        LeaveMissileView(this);
        bool loaded;
        if (data->flags & RDirectorQueueData::kFromList)
            loaded = LoadSingleAnimationFromList(data->unknown24);
        else
            loaded = LoadSingleAnimation(data->data18);
        if (!loaded)
            return;
        break;
    }

    case kCameraAIPathAnimation:
        LeaveMissileView(this);
        if (!LoadAISplinePathAnimation(data->data1C))
            return;
        break;

    case kCameraAutoDrive: {
        if (!fgCameraTables.autoDriveArms[0].lockArmToCar)
            adWeaponFlag = true;
        if (data->position.x == -1.0f)
            break;
        // The arms turned to the yaw and pitch asked for, and the camera with them (w 1: as the anchor's plus
        // them; else as given)
        MATRIX4 *anchor = GetAnchorMatrix4();
        float anchorYaw = float(HeadingTurns(anchor->mtx[2][0], anchor->mtx[2][2]));
        float anchorPitch = float(CameraAsinTurns(anchor->mtx[2][1]));
        float yaw, pitch;
        if (data->position.w == 1.0f) {
            yaw = data->position.x;
            pitch = data->position.y;
        } else {
            yaw = data->position.x - anchorYaw;
            pitch = data->position.y - anchorPitch;
        }
        MATRIX4 tilt, turn;
        VU0_MATRIX4setxrot(&tilt, float(WrapTurns(pitch)));
        VU0_MATRIX4setyrot(&turn, float(WrapTurns(yaw)));
        VU0_MATRIX4_mult(&turn, &tilt, &turn);
        Coord4 rotation;
        VU0_m4toquat(&rotation, &turn);
        for (int i = 0; i < fgCameraTables.autoDriveArmCount; i++) {
            AutoDriveArmInfo &arm = fgCameraTables.autoDriveArms[i];
            double restPitch = CameraAsinTurns(arm.minPitch[0]);
            arm.restPitch = float(restPitch);
            if (arm.hasRestPos) {
                arm.pitch = float(restPitch);
                MATRIX4 rest;
                RCameraMath::BuildRotationMat4(&rest, float(-restPitch), MatrixRow(&turn, 0));
                VU0_MATRIX4_mult(&turn, &rest, &turn);
                VU0_m4toquat(&arm.rotation, &turn);
            } else {
                arm.rotation = rotation;
            }
        }
        if (data->position.w == 1.0f) {
            yaw = anchorYaw + data->position.x;
            pitch = anchorPitch + data->position.y;
        } else {
            yaw = data->position.x;
            pitch = data->position.y;
        }
        VU0_MATRIX4setxrot(&tilt, float(WrapTurns(pitch)));
        VU0_MATRIX4setyrot(&matrix, float(WrapTurns(yaw)));
        VU0_MATRIX4_mult(&matrix, &tilt, &matrix);
        matrixChanged = 1;
        VU0_v4copy(MatrixRow(&matrix, 2), &forwardAimVec);
        targetAngleX = float(-CameraAsinTurns(matrix.mtx[2][1]));
        double heading = AtanTurnsUnrounded(matrix.mtx[2][0], matrix.mtx[2][2]);
        if (heading < 0.0)
            heading += 1.0;
        heading -= anchorYaw;
        targetAngleY = float(heading);
        if (heading >= 1.0)
            targetAngleY = float(heading - 1.0);
        if (targetAngleY <= -1.0f)
            targetAngleY += 1.0f;
        break;
    }
    }

    if (cameraMode < fgCameraTables.modeCount && (fgCameraTables.modes[cameraMode].type & ~(kCameraTumble | kCameraCollision)) != 0) {
        previousCameraMode = cameraMode;
        if (fgCameraTables.modes[cameraMode].selectable)
            lastSelectableCameraMode = cameraMode;
    }
    cameraMode = data->cameraMode;
    unknown258 = 0.0f;
    shakeStepsLeft = 0;
    modeChangeFlags |= RWorldCamera::kAnchorChanged;
    unknown26C = data->unknown14;
    SetupCameraZoom();
    transitionStep = 0;
    unknown13C = 0;
    transitionActive = 0;
    if (fgCameraTables.modes[cameraMode].type != kCameraAnimation || (data->flags & RDirectorQueueData::kKeepPoints))
        spline->ClearSplinePtList();
    if (cameraMode == fgCameraModeIndices.missile) {
        RColorize_SetEnabled(Colorize, 0, 2);
        state->unknown20 = true;
    }
    if (!(data->flags & RDirectorQueueData::kKeep260))
        unknown260 = 0.0f;
}

// A heli mode whose arm the weapons now pick differently is set again
// FUNC_AT(0x00084680)
void RPlayerCamera::UpdateCurrentArm() {
    int mode = cameraMode;
    if (fgCameraTables.modes[mode].type == kCameraHeli && FindHeliArmInd(mode) != currentArm)
        SetCameraModeByIndex(mode, fgCameraConstants.kWeaponArmChangeLatency, 0, 0, 0, NULL, NULL);
}

// FUNC_AT(0x000846d0)
void __fastcall BoostDiagonalRotation(float *x, float *y) {
    float larger = Abs(Abs(*x) > Abs(*y) ? *x : *y);
    if (!(larger > 0.0f))
        return;
    double difference = double(fabsf(*x)) - Abs(*y);
    if (difference < 0.0)
        difference = -difference;
    double boost = (larger - difference) / larger * DiagonalBoost;
    *x = float(*x > 0.0f ? boost + *x : *x - boost);
    *y = float(*y > 0.0f ? boost + *y : *y - boost);
    if (*x > 1.0f)
        *x = 1.0f;
    else if (*x < -1.0f)
        *x = -1.0f;
    if (*y > 1.0f)
        *y = 1.0f;
    else if (*y < -1.0f)
        *y = -1.0f;
}

// Each answers whether the auto-drive camera is at a limit
// FUNC_AT(0x000847f0)
bool RPlayerCamera::SetAutoDriveRotationX(float rotation) {
    autoDriveRotationX = float(SetAutoDriveRotation(rotation));
    BoostDiagonalRotation(&autoDriveRotationX, &autoDriveRotationY);
    return autoDriveRotating;
}

// FUNC_AT(0x00084820)
bool RPlayerCamera::SetAutoDriveRotationY(float rotation) {
    autoDriveRotationY = float(SetAutoDriveRotation(rotation));
    BoostDiagonalRotation(&autoDriveRotationX, &autoDriveRotationY);
    return autoDriveRotating;
}

// The index'th fixed mode, its camera at `position`
// FUNC_AT(0x00084850)
void RPlayerCamera::TriggerFixedCamera(Coord4 *position, int index, uint16_t delay) {
    if (index < 0 || index >= fgCameraTables.fixedCount)
        return;
    int mode = 0;
    for (int i = 0; i < fgCameraTables.modeCount && index >= 0; i++) {
        if (fgCameraTables.modes[i].type == kCameraFixed) {
            mode = i;
            index--;
        }
    }
    fixedCamPosition = *position;
    SetCameraModeByIndex(mode, delay, 0, 0, 0, NULL, NULL);
}

// The cinematic mode at a place given as a distance, a yaw and a pitch in degrees (x, y, z)
// FUNC_AT(0x000848d0)
void RPlayerCamera::SetCinematicCamera(Coord4 *place, uint16_t delay, uint16_t unknown14) {
    float yaw = float(double(place->y) * kDegreesToTurns - 0.5);
    if (yaw < 0.0f)
        yaw += 1.0f;
    float sinYaw = sin_fractionalangle(yaw);
    float cosYaw = cos_fractionalangle(yaw);
    float pitch = place->z * kDegreesToTurns;
    if (pitch < 0.0f)
        pitch += 1.0f;
    float sinPitch = sin_fractionalangle(pitch);
    double across = CosTurnsTimes(pitch, place->x);
    Coord4 position;
    position.x = float(sinYaw * across);
    state->unknown06 = true;
    position.y = sinPitch * place->x;
    position.z = float(across * cosYaw);
    position.w = 1.0f;
    SetCameraModeByIndex(fgCameraModeIndices.cinematic, delay, RDirectorQueueData::kKeep, unknown14, 0, NULL,
                         &position);
}

// FUNC_AT(0x00084a30)
RPlayerCamera* RPlayerCamera::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RPlayerCamera));
    return this;
}

// ---- the spline camera: along the road network beside the anchor

// FUNC_AT(0x00084a60)
void RPlayerCamera::UpdateSplineCam() {
    const CameraModeInfo &mode = fgCameraTables.modes[cameraMode];
    if (mode.index < 0 || mode.index >= fgCameraTables.splineCount)
        return;
    const SplineCamInfo &cam = fgCameraTables.splines[mode.index];
    MATRIX4 *anchorMatrix = GetAnchorMatrix4();
    Coord3 *anchorPosition = GetAnchorPosition();
    Coord4 *anchorForward = MatrixRow(anchorMatrix, 2);

    if (!(modeChangeFlags & RWorldCamera::kAnchorChanged)) {
        AnchorCamera(true, &cam.anchor.offset);
        // The navigator turned round if the anchor is going the other way
        Coord4 along = Widened(roadNav->direction);
        VU0_v4unitxyz(&along, &along);
        float speed = v3dotprod(GetAnchorLinearVelocity(), &along);
        if (speed < 0.0f) {
            speed = -speed;
            roadNav->ReverseNavDirection();
            along = Widened(roadNav->direction);
        }
        float roadSpeed = speed * cam.splineSpeed;
        float step = SimTimeStep * roadSpeed;

        // On a road of several lanes, 2.5 to the side of the line along the road nearest the anchor. (The
        // original leaves the offset uninitialised on a road of one lane.)
        Coord4 offset = {};
        if (WRoadNav_LaneCount(roadNav, 0) > 1) {
            Coord4 direction = {along.x, 0.0f, along.z, 0.0f};
            Coord4 point = {roadNav->position.x, 0.0f, roadNav->position.z, 0.0f};
            Coord4 side = {0.0f, 1.0f, 0.0f, 0.0f};
            VU0_v4sub(anchorPosition, &point, &offset);
            VU0_v4unitxyz(&direction, &direction);
            offset.y = 0.0f;
            float distance = v3dotprod(&offset, &direction);
            VU0_v4scaleadd(&direction, distance, &point, &offset);
            VU0_v4unitcrossprodxyz(&direction, &side, &side);
            VU0_v4sub(anchorPosition, &offset, &offset);
            // (the original tests which side of the road the anchor is on, and scales by 2.5 on either)
            VU0_v4scale(&side, kLaneOffset, &offset);
        }
        roadNav->IncNavPosition(step, AsCoord3(anchorForward), -1);
        // (the original also scales `along` by the road speed, and does not use it)
        Coord4 target;
        GetSafeWRoadNavPosition(&target, &anchorPosition->y);
        target.y = target.y + cam.heightOffset;
        EaseXYZ(&unknown2D0, &offset, fgCameraConstants.kSplineOffsetLerp, &unknown2D0);
        VU0_v3add(&target, &unknown2D0, &target);
        unknown228 = vec3distance(&target, &lookAt);
        if (unknown228 > cam.maxSplineCamDist) {
            // Too far behind: placed again ahead of the anchor
            double ahead = GetAnchorSpeed() * (1.0 - cam.splineSpeed) * cam.expLifeTime;
            float distance = cam.maxSplineCamDist < ahead ? cam.maxSplineCamDist : float(ahead);
            roadNav->InitAtPoint(anchorPosition, AsCoord3(anchorForward), false);
            roadNav->IncNavPosition(distance, AsCoord3(anchorForward), -1);
            GetSafeWRoadNavPosition(&eye, &anchorPosition->y);
            eye.y = cam.heightOffset + eye.y;
            VU0_v4Init(&unknown2D0);
            transitionStep = 0;
            unknown13C = 0;
            transitionActive = 0;
        } else if (unknown13C > 0 && transitionActive == 1) {
            UpdateTransition(&eye, &target);
        } else {
            eye = target;
        }
        Coord4 up = {0.0f, 1.0f, 0.0f, 0.0f};
        SetViewingTransform(&up, GetAnchorLinearVelocity(), 0);
    } else {
        if (DoSmoothModeChange()) {
            AnchorCamera(true, &cam.anchor.offset);
            roadNav->InitAtPoint(AsCoord3(&eye), AsCoord3(anchorForward), false);
            roadNav->IncNavPosition(0.0f, AsCoord3(anchorForward), -1);
            Coord4 target;
            GetSafeWRoadNavPosition(&target, &anchorPosition->y);
            target.y = target.y + cam.heightOffset;
            InitTransition(1, &eye, &target, mode.smoothTrans);
        } else {
            AnchorCamera(false, &cam.anchor.offset);
            eye = *anchorForward;
            VU0_v4scale(&eye, kSplineStartDistance, &eye);
            VU0_v3add(anchorPosition, &eye, &eye);
            roadNav->InitAtPoint(AsCoord3(&eye), AsCoord3(anchorForward), false);
            roadNav->IncNavPosition(0.0f, AsCoord3(anchorForward), -1);
            GetSafeWRoadNavPosition(&eye, &anchorPosition->y);
            eye.y = cam.heightOffset + eye.y;
            transitionStep = 0;
            unknown13C = 0;
            transitionActive = 0;
        }
        VU0_v4Init(&unknown2D0);
        Coord4 up = {0.0f, 1.0f, 0.0f, 0.0f};
        SetViewingTransform(&up, GetAnchorLinearVelocity(), 0);
    }
    VU0_v4sub(&eye, &lookAt, &cameraOffset);
    unknown228 = vec3distance(&eye, &lookAt);
    SetCameraZoom(kZoomByDistance, unknown228, 1);
}

// ---- the fixed camera: in one place, turning to follow the anchor

// FUNC_AT(0x00085160)
void RPlayerCamera::UpdateFixedCam() {
    const CameraModeInfo &mode = fgCameraTables.modes[cameraMode];
    if (mode.index < 0 || mode.index >= fgCameraTables.fixedCount)
        return;
    const FixedCamInfo &cam = fgCameraTables.fixeds[mode.index];
    if (modeChangeFlags == 0) {
        Coord4 previous = lookAt;
        AnchorCamera(true, &cam.anchor.offset);
        EaseXYZ(&previous, &lookAt, cam.rotSpeed, &lookAt);
        if (unknown13C > 0 && transitionActive == 1)
            UpdateTransition(&eye, &fixedCamPosition);
        else
            eye = fixedCamPosition;
    } else {
        fixedCamPosition = eye;
        if (DoSmoothModeChange()) {
            InitTransition(1, &eye, &fixedCamPosition, mode.smoothTrans);
            AnchorCamera(true, &cam.anchor.offset);
        } else {
            AnchorCamera(false, &cam.anchor.offset);
        }
    }
    VU0_v4sub(&eye, &lookAt, &cameraOffset);
    unknown228 = vec3distance(&eye, &lookAt);
    if (modeChangeFlags == 0) {
        // the camera's velocity, from where its frame was
        Coord4 previous = *MatrixRow(&matrix, 3);
        VU0_v4sub(&eye, &previous, &unknown50);
        VU0_v4scale(&unknown50, float(SimStepsPerSecond), &unknown50);
    }
    Coord4 up = {0.0f, 1.0f, 0.0f, 0.0f};
    SetViewingTransform(&up, &unknown50, 0);
    SetCameraZoom(kZoomByDistance, unknown228, 1);
}

// ---- the auto-drive camera

// The arms' blends back to their rest positions, and the blend from the old arm to the new after a change
// FUNC_AT(0x000853e0)
void RPlayerCamera::UpdateADWeaponAnims() {
    MATRIX4 carOrientation = PlayerBody()->info->orientation;
    int now = SimStepCount;
    if (autoDriveArm == -1) {
        int8_t arm = FindAutoDriveArmInd(cameraMode);
        autoDriveArm = arm;
        fgCameraTables.previousAutoDriveArm = arm;
        adWeaponFlag = false;
        SetFrameFromCar(this, &fgCameraTables.autoDriveArms[autoDriveArm].rotation, &carOrientation);
        matrixChanged = 1;
    }
    for (int i = 0; i < fgCameraTables.autoDriveArmCount; i++) {
        AutoDriveArmInfo &arm = fgCameraTables.autoDriveArms[i];
        if (arm.hasRestPos && arm.pitch != arm.restPitch) {
            double progress = (now - arm.restStartStep) / (double(arm.restInterpolFallScale) * arm.restPitchChange);
            if (progress >= 1.0) {
                arm.pitch = arm.restPitch;
                arm.rotation = arm.rotationTo;
            } else {
                VU0_fastqslerp(&arm.rotationFrom, &arm.rotationTo, &arm.rotation, float(progress));
            }
        }
    }
    if (!adWeaponFlag)
        return;

    AutoDriveArmInfo &arm = fgCameraTables.autoDriveArms[autoDriveArm];
    bool blending = false;
    if (Abs(fgCameraTables.weaponArmTurn) > kMinArmTurn) {
        double progress =
            (now - adWeaponAnimState) / (double(fgCameraTables.maxWeapTransTime) * fgCameraTables.weaponArmTurn);
        WeaponArmBlend = float(progress);
        blending = !(progress >= 1.0);
    }
    if (blending) {
        Coord4 rotation;
        VU0_fastqslerp(&WeaponArmFrom, &arm.rotation, &rotation, WeaponArmBlend);
        SetFrameFromCar(this, &rotation, &carOrientation);
    } else {
        WeaponArmBlend = 1.0f;
        adWeaponFlag = false;
        SetFrameFromCar(this, &arm.rotation, &carOrientation);
    }
    matrixChanged = 1;
    targetAngleX = float(-CameraAsinTurns(matrix.mtx[2][1]));
    double heading = AtanTurnsUnrounded(matrix.mtx[2][0], matrix.mtx[2][2]);
    if (heading < 0.0)
        heading += 1.0;
    targetAngleY = float(heading);
}

// FUNC_AT(0x000857d0)
void RPlayerCamera::UpdateAutoDriveCam() {
    int now = SimStepCount;
    if (glbMissionManager->unknown47c != 0 || SimState == 3)
        return;
    MATRIX4 *anchorMatrix = GetAnchorMatrix4();
    Coord3 *anchorPosition = GetAnchorPosition();

    if (autoDriveForwardLock) {
        // fixed on the arm, facing the anchor's way
        Coord4 offset;
        VU0_MATRIX4_vect3rotate(&fgCameraTables.autoDriveArms[autoDriveArm].relPos, anchorMatrix, &offset);
        VU0_v3add(&offset, anchorPosition, &eye);
        MatrixCopy(anchorMatrix, &matrix);
        *MatrixRow(&matrix, 3) = eye;
        matrixChanged = 1;
        return;
    }

    int8_t armIndex = FindAutoDriveArmInd(cameraMode);
    if (armIndex != autoDriveArm) {
        modeChangeFlags |= RWorldCamera::kAnchorChanged;
        fgCameraTables.previousAutoDriveArm = autoDriveArm;
        autoDriveArm = armIndex;
    }
    const CameraModeInfo &mode = fgCameraTables.modes[cameraMode];
    AutoDriveArmInfo *arm = &fgCameraTables.autoDriveArms[autoDriveArm];
    RPlayerCamState *state = this->state;

    // zoom
    if (state->aiming) {
        if (zoom298 < arm->minFov)
            zoom298 = arm->minFov;
    } else {
        zoom298 = mode.defaultFov;
    }
    if (autoDriveZoom != 0.0f) {
        RCamera_SetFieldOfView(this, 0, CameraClamp(mode.defaultFov, fieldOfView + autoDriveZoom, arm->minFov));
        zoom298 = fieldOfView;
    }
    RWorldCamera::SetCameraZoom(zoom298, fgCameraConstants.kZoomIncSpeed * zoomSlope);

    // a change of mode or arm
    if (modeChangeFlags & RWorldCamera::kAnchorChanged) {
        Coord4 lookFrom;
        VU0_v4sub(&lookAt, &lookAtOffset, &lookFrom);
        Coord4 offset;      // (its w is the original's uninitialised stack, copied into cameraOffset.w)
        VU0_MATRIX4_vect3rotate(&arm->relPos, GetAnchorMatrix4(), &offset);
        if (DoSmoothModeChange()) {
            AnchorCamera(true, &arm->anchor.offset);
            InitTransition(1, &cameraOffset, &offset, mode.smoothTrans);
        } else if (autoDriveArm != -1) {
            AnchorCamera(false, &arm->anchor.offset);
            cameraOffset = offset;
        }
        VU0_v3add(&lookAt, &cameraOffset, &eye);
        unknown290 = 0.0f;
        autoDriveRotationX = 0.0f;
        unknown294 = 0.0f;
        autoDriveRotationY = 0.0f;
        autoDriveRotating = false;
        unknown27D = 0;
        if (autoDriveArm != -1 && fgCameraTables.previousAutoDriveArm != -1) {
            // The camera's frame relative to the anchor's, where the blend to the new arm starts
            MATRIX4 anchorFrame = *anchorMatrix;
            anchorFrame.mtx[3][0] = anchorPosition->x;
            anchorFrame.mtx[3][1] = anchorPosition->y;
            anchorFrame.mtx[3][2] = anchorPosition->z;
            anchorFrame.mtx[3][3] = 1.0f;
            OrthoInverse(&anchorFrame);
            MATRIX4 relative;
            MATRIX4_mult(&matrix, &anchorFrame, &relative);
            VU0_m4toquat(&WeaponArmFrom, &relative);
            if (!adWeaponFlag && fgCameraTables.previousAutoDriveArm != -1) {
                AutoDriveArmInfo *previous = &fgCameraTables.autoDriveArms[fgCameraTables.previousAutoDriveArm];
                if (previous->preserveTransform) {
                    previous->rotation = WeaponArmFrom;
                    if (previous->hasRestPos) {
                        // the old arm blends back from the camera's pitch to its rest position
                        double pitch = CameraAsinTurns(matrix.mtx[2][1]);
                        previous->pitch = float(pitch);
                        double change = pitch - previous->restPitch;
                        float changeF = float(change);
                        previous->restPitchChange = changeF;
                        if (change < kMinArmTurn) {
                            previous->pitch = previous->restPitch;
                        } else {
                            previous->rotationFrom = WeaponArmFrom;
                            previous->restStartStep = now;
                            Coord4 xAxis = {1.0f, 0.0f, 0.0f, 1.0f};
                            MATRIX4 tilt;
                            RCameraMath::BuildRotationMat4(&tilt, changeF, &xAxis);
                            MATRIX4 tilted;
                            VU0_MATRIX4_mult(&tilted, &tilt, &relative);
                            VU0_m4toquat(&previous->rotationTo, &tilted);
                            previous->restPitchChange = previous->restPitchChange + previous->restPitchChange;
                        }
                    }
                }
            }
            if (arm->preserveTransform) {
                fgCameraTables.weaponArmTurn = float(CameraAcosTurns(v4dotprod(&WeaponArmFrom, &arm->rotation)) * 2.0);
                adWeaponAnimState = now;
                adWeaponFlag = true;
                WeaponArmBlend = 0.0f;
            }
        }
        if (previousCameraMode == fgCameraModeIndices.missile) {
            previousCameraMode = cameraMode;
            fgCameraTables.weaponArmTurn = 0.0f;
            VU0_v4sub(&lookFrom, &eye, MatrixRow(&matrix, 2));
            RCameraMath::MatrixFromDirection(MatrixRow(&matrix, 2), &matrix);
            matrixChanged = 1;
        }
    }

    UpdateADWeaponAnims();
    if (weaponFired != 0) {
        // the kick of a weapon fired
        double age = (now - weaponFired) / double(fgCameraConstants.kfWeaponAnimationLength);
        if (age < 1.0)
            unknown1D4 = float(SinTurnsTimes(float(age * 0.5), fgCameraConstants.kfWeaponAnimationAmplitude) + unknown1D4);
        else
            weaponFired = 0;
    }

    // the steering, eased
    unknown290 = float((double(autoDriveRotationX) - unknown290) * fgCameraTables.autoDriveInertia + unknown290);
    unknown294 = float((double(autoDriveRotationY) - unknown294) * fgCameraTables.autoDriveInertia + unknown294);
    float fovScale = fieldOfView * kOneThirtyThird;

    if (arm->allowDeadzone && !state->unknown0D) {
        // The aim moves inside a dead zone before the camera turns, or follows a target
        float aimPitchTarget = 0.0f, aimYawTarget = 0.0f;
        bool onTarget = false;
        float stepX, stepY;
        if (state->aiming) {
            stepX = float(double(unknown290) * arm->targetCursorMoveSpeed * fovScale);
            stepY = float(double(unknown294) * arm->targetCursorMoveSpeed * fovScale);
        } else {
            stepX = float(double(unknown290) * arm->normCursorMoveSpeed * fovScale);
            stepY = float(double(unknown294) * arm->normCursorMoveSpeed * fovScale);
            onTarget = UpdateADTargetAngles(&aimPitchTarget, &aimYawTarget);
        }
        // the cursor's travel while the steering keeps its sign, and its speed from that
        if (!(double(PreviousRotationY) * unknown294 < 0.0) && !(Abs(unknown294) <= kMinArmTurn))
            unknown1F0 = stepY + unknown1F0;
        else
            unknown1F0 = 0.0f;
        double yScaleD = Abs(unknown1F0) * double(kYawScaleSlope) + kYawScaleBase;
        float yScale = float(yScaleD);
        if (yScaleD > kMaxCursorScale)
            yScale = kMaxCursorScale;
        if (!(double(PreviousRotationX) * unknown290 < 0.0) && !(Abs(unknown290) <= kMinArmTurn))
            unknown1F4 = stepX + unknown1F4;
        else
            unknown1F4 = 0.0f;
        double xScaleD = Abs(unknown1F4) * double(kPitchScaleSlope) + kPitchScaleBase;
        float xScale = float(xScaleD);
        if (xScaleD > kMaxCursorScale)
            xScale = kMaxCursorScale;
        PreviousRotationX = unknown290;
        PreviousRotationY = unknown294;

        float moveX, moveY;
        if (onTarget && !state->unknown0B)
            state->unknown0B = true;
        if (state->unknown0B) {
            aimPitch = float((double(aimPitchTarget) - aimPitch) * arm->autoaimInterpolSpeed + aimPitch);
            aimYaw = float((double(aimYawTarget) - aimYaw) * arm->autoaimInterpolSpeed + aimYaw);
            moveX = float(double(unknown290) * arm->normCursorEndMoveSpeed * xScale);
            moveY = float(double(unknown294) * arm->normCursorEndMoveSpeed * yScale);
        } else if (state->aiming) {
            aimPitch = stepX + aimPitch;
            float limit = fovScale * arm->maxDeadzonePitch;
            if (Abs(aimPitch) > limit) {
                moveX = float(double(unknown290) * arm->targetCursorEndMoveSpeed * xScale * fovScale);
                aimPitch = aimPitch < 0.0f ? -limit : limit;
            } else {
                unknown1F4 = 0.0f;
                moveX = 0.0f;
            }
            aimYaw = stepY + aimYaw;
            limit = fovScale * arm->maxDeadzoneYaw;
            if (Abs(aimYaw) > limit) {
                moveY = float(double(unknown294) * arm->targetCursorEndMoveSpeed * yScale * fovScale);
                aimYaw = aimYaw < 0.0f ? -limit : limit;
            } else {
                unknown1F0 = 0.0f;
                moveY = 0.0f;
            }
        } else {
            moveX = float(double(xScale) * arm->normCursorEndMoveSpeed * fovScale * unknown290);
            aimPitch = 0.0f;
            aimYaw = 0.0f;
            moveY = float(double(unknown294) * arm->normCursorEndMoveSpeed * yScale * fovScale);
        }
        // the aim's place on screen
        unknown1D0 = float(DivideByTanTurns(float(TanTurnsTimes(aimYaw, fgRenderer->screenWidth) * kScreenScaleX),
                                            fieldOfView * kFovToHalfTurns));
        unknown1D4 = float(DivideByTanTurns(float(TanTurnsTimes(aimPitch, fgRenderer->screenHeight) * kScreenScaleY),
                                            fieldOfView * kFovToHalfTurns));
        targetAngleX = moveX + targetAngleX;
        targetAngleY = moveY + targetAngleY;
    } else {
        aimPitch = 0.0f;
        aimYaw = 0.0f;
        unknown1D0 = 0.0f;
        unknown1D4 = 0.0f;
        targetAngleX = float(double(unknown290) * arm->normCursorEndMoveSpeed * fovScale + targetAngleX);
        targetAngleY = float(double(unknown294) * arm->normCursorEndMoveSpeed * fovScale + targetAngleY);
    }

    if (spinState != 0) {
        // a spin round: past the last band's maximum, on from its minimum
        targetAngleY = targetAngleY + kSpinStep;
        int band;
        for (band = kYawBands - 1; band >= 0; band--) {
            if (arm->maxYaw[band] > 0.0f)
                break;
        }
        if (band >= 0 && targetAngleY > arm->maxYaw[band])
            targetAngleY = arm->minYaw[band];
        spinState--;
        if (spinState < 0)
            spinState = 0;
    }

    Coord4 worldUp = {0.0f, 1.0f, 0.0f, 0.0f};
    Coord4 worldRight = {1.0f, 0.0f, 0.0f, 0.0f};
    const Coord4 *up = &worldUp;
    const Coord4 *right = &worldRight;
    if (arm->lockArmToCar) {
        up = MatrixRow(anchorMatrix, 1);
        right = MatrixRow(anchorMatrix, 0);
    }
    if (lockOnPoint != NULL) {
        // Turned towards the locked-on point, eased in over the lock-on's first steps
        Coord4 toTarget = {};
        VU0_v4sub(lockOnPoint, &eye, &toTarget);
        toTarget.y = toTarget.y + lockOnTarget.y;
        VU0_v4scaleadd4(MatrixRow(anchorMatrix, 0), lockOnTarget.x, &toTarget, &toTarget);
        VU0_v4scaleadd4(MatrixRow(anchorMatrix, 2), lockOnTarget.z, &toTarget, &toTarget);
        VU0_v4unitxyz(&toTarget, &toTarget);
        if (lockOnParam1 > 0) {
            Coord4 forward = *MatrixRow(&matrix, 2);
            float t = float(double(lockOnParam2 - lockOnParam1) / lockOnParam2);
            EaseXYZ(&forward, &toTarget, t, &toTarget);
            VU0_v4unitxyz(&toTarget, &toTarget);
            lockOnParam1--;
        }
        Coord4 side;
        VU0_v4unitcrossprodxyz(up, &toTarget, &side);
        targetAngleY = float((-CameraAcosTurns(v3dotprod(right, &side)) - targetAngleY) * kAimFollow + targetAngleY);
        Coord4 unused;
        VU0_v4crossprodxyz(&toTarget, &side, &unused);
        Coord4 upNow;
        VU0_v4crossprodxyz(&side, up, &upNow);
        double pitch = CameraAcosTurns(v3dotprod(&upNow, &toTarget));
        if (upNow.y < toTarget.y)
            pitch = -pitch;
        targetAngleX = float((pitch - targetAngleX) * kAimFollow + targetAngleX);
        aimPitch = 0.0f;
        aimYaw = 0.0f;
        unknown1D0 = 0.0f;
        unknown1D4 = 0.0f;
        if (LimitPitchYaw(arm, &targetAngleX, &targetAngleY))
            SetCameraZoom(kZoomBetween, 0.0f, 30);
    }
    autoDriveRotating = LimitPitchYaw(arm, &targetAngleX, &targetAngleY);
    if (autoDriveRotating) {
        PlayerCarAudio()->flagC2 = 0;
        PlayerCarAudio()->flagC3 = 0;
    }

    // the frame: yawed about up, pitched about its own row 0
    MATRIX4 turn;
    RCameraMath::BuildRotationMat4(&turn, targetAngleY, up);
    VU0_MATRIX4_vect3rotate(right, &turn, MatrixRow(&matrix, 0));
    RCameraMath::BuildRotationMat4(&turn, targetAngleX, MatrixRow(&matrix, 0));
    VU0_MATRIX4_vect3rotate(up, &turn, MatrixRow(&matrix, 1));
    VU0_v4crossprodxyz(MatrixRow(&matrix, 0), MatrixRow(&matrix, 1), MatrixRow(&matrix, 2));
    matrix.mtx[0][3] = 0.0f;
    matrix.mtx[1][3] = 0.0f;
    matrix.mtx[2][3] = 0.0f;
    if (arm->allowDeadzone && !state->unknown0D) {
        MATRIX4 aimTurn;
        RCameraMath::BuildRotationMat4(&aimTurn, aimPitch, MatrixRow(&matrix, 0));
        VU0_MATRIX4_vect3rotate(MatrixRow(&matrix, 2), &aimTurn, &forwardAimVec);
        RCameraMath::BuildRotationMat4(&aimTurn, aimYaw, MatrixRow(&matrix, 1));
        VU0_MATRIX4_vect3rotate(&forwardAimVec, &aimTurn, &forwardAimVec);
    } else {
        MatrixCopy(&matrix, &aimMatrix);
        VU0_v4copy(MatrixRow(&matrix, 2), &forwardAimVec);
    }

    // the eye: the arm's offset turned with the frame, from the anchor's point
    if (!(modeChangeFlags & RWorldCamera::kAnchorChanged)) {
        Coord4 offset;      // (its w is the original's uninitialised stack, as above)
        VU0_MATRIX4_vect3rotate(&arm->relPos, anchorMatrix, &offset);
        if (unknown13C > 0 && transitionActive == 1)
            UpdateTransition(&cameraOffset, &offset);
        else
            cameraOffset = offset;
        VU0_v3add(&cameraOffset, anchorPosition, &eye);
    }
    Coord4 armOffset;
    VU0_MATRIX4_vect3rotate(&arm->relPos, &matrix, &armOffset);
    Coord4 pivot = *AsVector4(anchorPosition);
    VU0_v4scaleadd4(MatrixRow(anchorMatrix, 0), arm->anchor.offset.x, &pivot, &pivot);
    VU0_v4scaleadd4(MatrixRow(anchorMatrix, 1), arm->anchor.offset.y, &pivot, &pivot);
    VU0_v4scaleadd4(MatrixRow(anchorMatrix, 2), arm->anchor.offset.z, &pivot, &pivot);
    VU0_v3add(&pivot, &armOffset, &eye);
    eye.w = 1.0f;
    *MatrixRow(&matrix, 3) = eye;
    matrixChanged = 1;

    // recentring: given up when steered, done when close
    if (Abs(unknown290) > kRecentreInput || Abs(unknown294) > kRecentreInput)
        state->unknown0D = false;
    if (state->unknown0D) {
        targetAngleX = float((1.0 - fgCameraConstants.kCenteringSpeed) * targetAngleX);
        targetAngleY = float((1.0 - fgCameraConstants.kCenteringSpeed) * targetAngleY);
        if (Abs(targetAngleX) < kRecentred || Abs(targetAngleY) < kRecentred)
            state->unknown0D = false;
    }

    // the camera's heading from the anchor's, round y
    Coord4 anchorHeading = {anchorMatrix->mtx[2][0], 0.0f, anchorMatrix->mtx[2][2], 0.0f};
    Coord4 heading = {matrix.mtx[2][0], 0.0f, matrix.mtx[2][2], 0.0f};
    unknown260 = float(RCameraMath::AngleTurns(&anchorHeading, &heading));
    VU0_v4copy(GetAnchorLinearVelocity(), &unknown50);
    VU0_v4sub(&eye, &lookAt, &cameraOffset);
    unknown228 = vec3distance(&eye, &lookAt);
}
