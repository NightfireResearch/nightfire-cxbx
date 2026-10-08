// RPlayerCamera, 0x00086bb0..0x00089b70: collisions with the world and with objects, explosion shaking, the aim
// matrix, the per-frame UpdateCamera, the momentum heli, tumble and ellipse cameras, the Set*Camera functions,
// construction and restart, the old and last selectable modes, and the animation triggers.

#pragma fp_contract(off)

#include "PlayerCameraC.h"

#include "PlayerCameraA.h"            // PointInTriangle, CameraClamp
#include "PlayerCamState.h"

#include <stddef.h>
#include <stdint.h>
#include <bit>

#include "../../common/xbeOverload.h"
#include "../../helpers.h"
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/SimRandom.h"        // Noise
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsMath.h"     // Abs
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"      // MEM_fill
#include "../platform/X87.h"
#include "../data/Carp.h"
#include "../world/Collider.h"
#include "../world/RoadNav.h"
#include "../world/WorldPos.h"

// ---- the game's code not ported yet
#define WorldCollisionInfo_Construct ((WorldCollisionInfo *(__fastcall *)(WorldCollisionInfo *, int))0x0001d9f0)
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define Simulation_GetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int slot))0x000b2730)
#define RotateOffsetToHeading ((void (*)(const Coord4 *offset, const Coord4 *heading, Coord4 *out))0x000808f0)
#define BlendVectors ((void (*)(Coord4 *out, const Coord4 *from, const Coord4 *to, float t))0x00022870)
#define Instance_SetMatrix ((void (__fastcall *)(CARP::Instance *, int, const MATRIX4 *))0x00035610)     // FUN_00035610: the axes and position
#define RColorize_SetEnabled ((void (__fastcall *)(void *, int, int))0x0009a500)

namespace {

// The explosions the shake measures (an object with its size after PhysicsObject's fields)
struct ShakeSource : PhysicsObject {
    uint32_t unknown6C;
    float size;                         // +0x70
};

}  // namespace

// ---- globals
#define Sim ((void *)0x00233ff0)                                    // the Simulation
#define SimState I32_AT(0x00234e24)
#define SimTimeStep FLOAT_AT(0x00234e30)                            // the simulation's step, in seconds
#define SimStepCount I32_AT(0x00234e34)
#define SimStepsPerSecond I32_AT(0x00234e2c)
#define PlayerObjects (*(GameVector<PhysicsObject *> *)0x00234e3c)  // the players' objects; the first is ours
#define Explosions (*(GameVector<ShakeSource *> *)0x00234ebc)
#define Colorize PTR_AT(0x001f6898)                                 // the RColorize
#define SimpleBodyOwners ((PhysicsObject **)0x002343a0)             // [96] the simple bodies' owners, by slot

#define HeliGroundCount I32_AT(0x001ec3e8)          // the heli camera's ground-contact count, 0-80
#define HeliGroundPoint (*(Coord4 *)0x001ec3f0)     // where its last ground probe hit
#define AimMatrix (*(MATRIX4 *)0x001ec400)          // GetAimMatrix4's answer

constexpr int32_t kSimState3 = 3;
constexpr unsigned kVirtualUpdateCamera = 3;
constexpr float kAnimationFieldOfView = 33.0f;
constexpr int kMaxHeliGroundCount = 80;
constexpr unsigned kColliderMask = WCollider::kCollideBarriers | WCollider::kCollideInstances;

static const MATRIX4 kIdentity = { {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
} };

// cos_fractionalangle leaves FCOS's result in ST0, which the ellipse camera multiplies unrounded: read as a double.
static double CosineTurns(float turns) {
    return reinterpret_cast<double (*)(float)>(&cos_fractionalangle)(turns);
}

// The inlined VU0 lerp: out = from + t * (to - from), on x, y and z
static void LerpXYZ(const Coord4 *from, const Coord4 *to, float t, Coord4 *out) {
    float x = (to->x - from->x) * t + from->x;
    float y = (to->y - from->y) * t + from->y;
    float z = (to->z - from->z) * t + from->z;
    out->x = x;
    out->y = y;
    out->z = z;
}

// ... and on all four
static void Lerp(const Coord4 *from, const Coord4 *to, float t, Coord4 *out) {
    LerpXYZ(from, to, t, out);
    out->w = (to->w - from->w) * t + from->w;
}

// std::list::clear on the director's queue, as the camera inlines it
static void ClearDirectorQueue(RDirectorQueue *queue) {
    DirectorQueueNode *result;
    queue->queue.Erase(&result, queue->queue.Begin(), queue->queue.head);
}

static bool IsHeliWithNoise(const CameraModeInfo *mode) {
    return mode->type == kCameraHeli && fgCameraTables.helis[mode->index].noiseAmount > 0.0f;
}

// One of ResolveAllCollisions' probes: where the segment hits the world, as a fraction of the way along it past
// `limit`, kept in `fraction` when it is the nearest; a hit closer than `limit` + 1.5 clears `clear`.
static void ProbeCollision(WCollider *collider, const Coord4 *segment, WorldCollisionInfo *info, float limit,
                           float *fraction, bool *clear) {
    if (!collider->GetWorldNormal(segment, info))
        return;
    Coord4 along, toHit;
    VU0_v4sub(&segment[1], &segment[0], &along);
    float length = VU0_v3length(&along);
    VU0_v4sub(&info->point, &segment[0], &toHit);
    double distance = VU0_v3length(&toHit);
    if (distance > 1.1f) {
        if (distance < limit)
            distance = limit;
        double ratio = distance / length;
        if (*fraction > ratio)
            *fraction = (float)ratio;
        if (distance - limit < 1.5f)
            *clear = false;
    }
}

// FUNC_AT(0x00086bb0)
bool RPlayerCamera::CheckObjectCollisions(Coord4 *position) {
    bool adjusted = false;
    for (PhysicsObject **it = PlayerObjects.first; it != PlayerObjects.last; ++it) {
        PhysicsObject *object = *it;
        if (object == anchor)
            continue;
        RigidBody *body = Simulation_GetRigidBody(Sim, 0, object->rigidBodySlot);
        float radius = VU0_v3lengthsquare(&body->info->halfExtents) * fgCameraConstants.kCameraObjectSphereRad;
        if (VU0_v3distancesquare(&body->position, position) <= radius)
            adjusted = AdjustCamAroundObjectEllipse(position, body, NULL, 0);
    }
    for (int slot = 0; slot < 96; slot++) {
        if (SimpleBodyOwners[slot] == NULL)
            continue;
        SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, slot);
        if (body->bodyType != kSimpleHelicopter)
            continue;
        float radius = body->radius * fgCameraConstants.kCameraObjectSphereRad;
        if (VU0_v3distancesquare(&body->position, position) <= radius)
            adjusted = AdjustCamAroundObjectEllipse(position, NULL, body, 0);
    }
    return adjusted;
}

// FUNC_AT(0x00086d00)
int RPlayerCamera::ResolveAllCollisions(Coord4 *position, const Coord4 *target, bool objects) {
    collisionState = 0;
    WorldCollisionInfo info;
    WorldCollisionInfo_Construct(&info, 0);
    Coord4 segment[2];
    segment[0].w = 1.0f;
    segment[1].w = 1.0f;

    bool heli = IsHeliWithNoise(&fgCameraTables.modes[cameraMode]);
    float reach = 2.0f;
    if (objects && !heli)
        reach = 2.5f;
    float drop = 0.5f;
    if (heli)
        drop = 0.1f;

    Coord4 offset;
    VU0_v4sub(position, target, &offset);
    Coord4 middle;
    LerpXYZ(target, position, 0.5375f, &middle);
    float length = VU0_v3length(&offset);

    if (collider == NULL) {
        void *block = UMemory::FastAlloc(sizeof(WCollider), "WCollider");
        collider = block != NULL ? static_cast<WCollider *>(block)->Construct(
                                       reinterpret_cast<const Coord3 *>(&middle), length * 0.9125f, false,
                                       kColliderMask)
                                 : NULL;
    } else if (!collider->InRegion(reinterpret_cast<const Coord3 *>(&middle),
                                   (float)(((double)length * 1.075f + 0.5f) * 0.5f), kColliderMask)) {
        if (collider != NULL) {
            collider->Destruct();
            UMemory::FastFree(collider, sizeof(WCollider));
        }
        void *block = UMemory::FastAlloc(sizeof(WCollider), "WCollider");
        collider = block != NULL ? static_cast<WCollider *>(block)->Construct(
                                       reinterpret_cast<const Coord3 *>(&middle), length * 0.9125f, false,
                                       kColliderMask)
                                 : NULL;
    }

    float limit = reach;
    if (!objects) {
        float alignment = Abs(v3dotprod(MatrixRow(&matrix, 2), MatrixRow(GetAnchorMatrix4(), 2)));
        limit = (float)(((double)reach - 1.1f) * alignment + 1.1f);
    }

    // Three probes back from the target past the camera: lowered, then raised and to one side, then the other
    VU0_v4copy(target, &segment[0]);
    float fraction = 1.0f;
    bool clear = true;
    VU0_v4scaleadd(&offset, 1.075f, &segment[0], &segment[1]);
    VU0_v4scaleadd(MatrixRow(&matrix, 1), -0.5f, &segment[1], &segment[1]);
    ProbeCollision(collider, segment, &info, limit, &fraction, &clear);
    VU0_v4scaleadd(MatrixRow(&matrix, 1), 1.0f, &segment[1], &segment[1]);
    VU0_v4scaleadd(MatrixRow(&matrix, 0), -0.4f, &segment[1], &segment[1]);
    ProbeCollision(collider, segment, &info, limit, &fraction, &clear);
    VU0_v4scaleadd(MatrixRow(&matrix, 0), 0.8f, &segment[1], &segment[1]);
    ProbeCollision(collider, segment, &info, limit, &fraction, &clear);

    if (fraction < 1.0f) {
        VU0_v4scaleadd(&offset, fraction, target, position);
        if (objects)
            VU0_v4scaleadd(MatrixRow(&matrix, 1), (float)((1.0 - fraction) * drop), position, position);
    }

    // Heli modes: a probe from the camera along the anchor's forward axis; the more it hits, the lower the camera
    if (fgCameraTables.modes[cameraMode].type == kCameraHeli && clear) {
        MATRIX4 *anchorMatrix = GetAnchorMatrix4();
        VU0_v4sub(position, target, &offset);
        float distance = VU0_v3length(&offset);
        VU0_v4scaleadd(MatrixRow(anchorMatrix, 2), distance * 0.125f, position, &segment[0]);
        VU0_v4scaleadd(MatrixRow(anchorMatrix, 2), distance * 0.975f, position, &segment[1]);
        if (collider->GetWorldNormal(segment, &info)) {
            VU0_v4copy(&info.point, &HeliGroundPoint);
            if (HeliGroundCount < kMaxHeliGroundCount)
                HeliGroundCount++;
        }
        if (HeliGroundCount > 0) {
            if (PointInTriangle(&HeliGroundPoint, position, &segment[1], target)) {
                if (HeliGroundCount < kMaxHeliGroundCount)
                    HeliGroundCount++;
            } else {
                HeliGroundCount--;
            }
            VU0_v4scaleadd(MatrixRow(&matrix, 1), (float)HeliGroundCount * -0.0125f, position, position);
        }
    }

    CheckObjectCollisions(position);
    return collisionState;
}

// FUNC_AT(0x000873e0)
void RPlayerCamera::CheckForCameraShaking() {
    if (shakeStepsLeft <= 0) {
        shakeAmount = 0.0f;
        shaking = false;
    }
    float scale = fgCameraTables.modes[cameraMode].explosionShakeScale;
    float target = 0.0f;
    for (ShakeSource **it = Explosions.first; it != Explosions.last; ++it) {
        ShakeSource *explosion = *it;
        float size = explosion->size * 5.0f;
        if (size < 50.0f)
            continue;
        SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, explosion->rigidBodySlot);
        double distanceSquared = VU0_v3distancesquare(&eye, &body->position);
        if (distanceSquared < size) {
            double amount = (size - distanceSquared) * fgCameraConstants.kExplosionScale * scale;
            double cap = (double)fgCameraConstants.kExplosionMaxShake * scale;
            target = (float)cap;
            if (cap < amount)
                amount = target;
            target = (float)amount;
            if (amount > shakeAmount)
                shakeAmount = (float)amount;
            int steps = Truncate((float)((double)SimStepsPerSecond * fgCameraConstants.kExplosionTimeScale * size * scale * 0.06f));
            shakeStepsLeft = steps > shakeStepsLeft ? steps : shakeStepsLeft;
        }
    }

    if (--shakeStepsLeft <= 0)
        return;
    shakeAmount = (float)(((double)target - shakeAmount) * fgCameraConstants.kExplosionAfterShock * scale + shakeAmount);
    float time = (float)((double)SimStepCount / kExplosionShakePeriod);
    float across = (float)(Noise::Noise1(time) * shakeAmount);
    float up = (float)(Noise::Noise1(time * 3.618f) * shakeAmount);
    Coord4 *right = MatrixRow(&matrix, 0);
    Coord4 *upward = MatrixRow(&matrix, 1);
    Coord4 *forward = MatrixRow(&matrix, 2);
    if (shaking) {
        VU0_v4copy(&ShakeOrigin, &eye);
        VU0_v4scaleadd(upward, across, &eye, &eye);
        VU0_v4scaleadd(right, up, &eye, &eye);
        VU0_v4copy(&eye, MatrixRow(&matrix, 3));
    } else {
        VU0_v4scaleadd(forward, across, upward, upward);
        VU0_v4unitxyz(upward, upward);
        VU0_v4scaleadd(upward, -across, forward, forward);
        VU0_v4unitxyz(forward, forward);
        VU0_v4scaleadd(right, up, upward, upward);
        VU0_v4unitxyz(upward, upward);
        VU0_v4scaleadd(upward, -up, right, right);
        VU0_v4unitxyz(right, right);
    }
    matrixChanged = true;
}

// FUNC_AT(0x000876b0)
MATRIX4* RPlayerCamera::GetAimMatrix4(float yawScale, float pitchScale) {
    if (yawScale == 1.0f && pitchScale == 1.0f)
        return &aimMatrix;
    if (yawScale == 0.0f && pitchScale == 1.0f)
        return &matrix;
    RigidBody *body = Simulation_GetRigidBody(Sim, 0, PlayerObjects.first[0]->rigidBodySlot);
    Coord4 yawAxis = *MatrixRow(&body->info->orientation, 1);
    Coord4 pitchAxis = *MatrixRow(&body->info->orientation, 0);
    MATRIX4 pitch, yaw;
    RCameraMath::BuildRotationMat4(&pitch, pitchScale * aimPitch, &pitchAxis);
    RCameraMath::BuildRotationMat4(&yaw, yawScale * aimYaw, &yawAxis);
    VU0_MATRIX4_mult(&AimMatrix, &pitch, &matrix);
    VU0_MATRIX4_mult(&AimMatrix, &yaw, &AimMatrix);
    return &AimMatrix;
}

// FUNC_AT(0x000877e0)
void RPlayerCamera::UpdateCamera() {
    if (fgCameraTables.modes == NULL || !active)
        return;
    if (SimStepCount == lastUpdateStep && SimState != kSimState3)
        return;
    lastUpdateStep = SimStepCount;
    if (SimState != kSimState3)
        directorQueue->ProcessDirectorLogic();
    (this->*XbeOriginal<decltype(&RPlayerCamera::UpdateCamera)>((size_t)fgCameraTables.modes[cameraMode].update))();
    if ((fgCameraTables.modes[cameraMode].shake || shaking) && SimState != kSimState3)
        CheckForCameraShaking();
    modeChangeFlags = 0;
}

// FUNC_AT(0x00087880)
void RPlayerCamera::UpdateMomentumHeliCam() {
    const CameraModeInfo *mode = &fgCameraTables.modes[cameraMode];
    if (mode->index < 0 || mode->index >= fgCameraTables.heliCount)
        return;
    float previousDistance = unknown228;
    bool replay = SimState == kSimState3;
    const HeliCamInfo *arm = &fgCameraTables.helis[mode->index];
    const HeliArmInfo *armPosition = &arm->arms[currentArm];
    bool zoom = true;
    MATRIX4 *anchorMatrix = GetAnchorMatrix4();
    const Coord4 *anchorPosition = AsVector4(GetAnchorPosition());

    // The offset from the anchor, pulled in with speed and stirred by noise
    Coord4 offset;
    VU0_v4copy(&armPosition->sideways, &offset);
    if (state->lookingBack == 1 && mode->lookBack) {
        offset.z = -offset.z;
    } else {
        double pull = Abs(GetAnchorSpeed()) * (double)arm->fallbackFactor;
        if (!(pull < arm->maxFallback))
            pull = arm->maxFallback;
        offset.z = (float)(offset.z - pull);
    }
    if (arm->noiseAmount > 0.0f) {
        float calm = (float)((20.0f - (double)GetAnchorSpeed()) * 0.05f);
        const Coord3 *position = GetAnchorPosition();
        Coord4 noise;
        noise.x = (float)(Noise::Noise1((float)(((double)SimStepCount * 0.5f + position->x) * arm->noiseFrequency)) *
                          calm * arm->noiseAmount);
        noise.y = (float)(Noise::Noise1((float)(((double)SimStepCount * 0.3f + position->y) * arm->noiseFrequency)) *
                          calm * arm->noiseAmount);
        noise.z = 0.0f;
        noise.w = 0.0f;
        Lerp(&unknown230, &noise, arm->noisePace, &unknown230);
        VU0_v3add(&offset, &unknown230, &offset);
    }

    // ... turned with the anchor (its w, which the original leaves as stack garbage, ends up in cameraOffset.w)
    Coord4 turned = {};
    if (!arm->rigidArm) {
        if (anchor->type == 1) {
            float forwardY = anchorMatrix->mtx[2][1];
            if (!replay) {
                float scale = forwardY < 0.0f ? arm->maxVertigoDownhill : arm->maxVertigoUphill;
                unknown258 = (float)(((double)forwardY * scale - unknown258) * arm->vertigoLerp + unknown258);
            }
            float sine = unknown258;
            double cosine = VU0_sqrt((float)(1.0 - (double)sine * sine));
            double y = offset.y * cosine + (double)sine * offset.z;
            double z = cosine * offset.z - (double)offset.y * sine;
            offset.z = (float)z;
            offset.y = (float)y;
        }
        RotateOffsetToHeading(&offset, MatrixRow(anchorMatrix, 2), &turned);
    } else {
        MATRIX4 frame;
        if (cameraMode == fgCameraModeIndices.missile) {
            MatrixCopy(anchorMatrix, &frame);
        } else if (arm->upRate > 0.0f) {
            Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
            float level = Abs(anchorMatrix->mtx[2][1]);
            float upright = -anchorMatrix->mtx[1][1];
            if (upright < 0.0f)
                upright = 0.0f;
            double blend = (1.0 - Abs(anchorMatrix->mtx[0][1])) * (1.0 - level) * upright + level;
            float roll = (float)blend;
            if (state->unknown30 > 0 && level < 0.7f) {
                blend *= 0.1f;
                roll = (float)blend;
            }
            if (blend > 1.0)
                roll = 1.0f;
            BlendVectors(&up, &up, MatrixRow(anchorMatrix, 1), roll);
            VU0_v4unitxyz(&up, &up);
            Coord4 forward = *MatrixRow(anchorMatrix, 2);
            Coord4 right = { 1.0f, 0.0f, 0.0f, 0.0f };
            VU0_v4unitcrossprodxyz(&up, &forward, &right);
            VU0_v4crossprodxyz(&forward, &right, &up);
            *MatrixRow(&frame, 0) = right;
            *MatrixRow(&frame, 1) = up;
            *MatrixRow(&frame, 2) = forward;
        } else {
            MatrixCopy(anchorMatrix, &frame);
        }
        VU0_MATRIX4_vect3rotate(&offset, &frame, &turned);
    }

    // ... and eased into place
    if (modeChangeFlags == 0) {
        AnchorCamera(true, &armPosition->anchor.offset);
        unknown228 = VU0_v3length(&turned);
        bool smooth;
        if (unknown144 == 1) {
            unknown25C = 0.1f;
            smooth = !replay;
        } else {
            float rate = CameraClamp(arm->maxRate,
                                     (float)(Abs(GetAnchorSpeed()) * ((double)SimTimeStep * 60.0f) * arm->speedRateDiff),
                                     arm->minRate);
            if (unknown13C > 0 && fgCameraTables.modes[lastSelectableCameraMode].type != kCameraDashboard) {
                if (transitionActive == 1) {
                    if (cameraMode == fgCameraModeIndices.cinematic)
                        directorQueue->flags |= RDirectorQueue::kHeld;
                    if (!replay) {
                        if (transitionStep)
                            transitionFactor = (float)(((double)unknown228 - previousDistance) /
                                                       (double)(unsigned)armPosition->armTransition);
                        unknown228 = (float)(unknown228 - (double)unknown13C * transitionFactor);
                        transitionStep = 0;
                        unknown13C--;
                        unknown25C = (float)(((double)rate - unknown25C) * fgCameraConstants.kTransRateLerpRate + unknown25C);
                    }
                }
                smooth = !replay;
            } else {
                if (!replay) {
                    unknown25C = rate;
                    unknown228 = VU0_v3length(&turned);
                }
                if (cameraMode == fgCameraModeIndices.cinematic)
                    directorQueue->flags &= ~RDirectorQueue::kHeld;
                smooth = !replay;
            }
        }
        if (smooth) {
            VU0_v4unitxyz(&cameraOffset, &cameraOffset);
            VU0_v4scale(&cameraOffset, unknown228, &cameraOffset);
            Coord4 step;
            VU0_v4sub(&turned, &cameraOffset, &step);
            VU0_v4scale(&step, unknown25C, &step);
            VU0_v3add(&cameraOffset, &step, &cameraOffset);
        }
    } else {
        if (DoSmoothModeChange() == 1 && !(modeChangeFlags & kLookBackChanged) &&
            fgCameraTables.modes[lastSelectableCameraMode].type != kCameraDashboard) {
            if (fgCameraModeIndices.cinematic == cameraMode)
                directorQueue->flags |= RDirectorQueue::kHeld;
            AnchorCamera(true, &armPosition->anchor.offset);
            int steps = unknown26C;
            if (steps == 0)
                steps = armPosition->armTransition;
            transitionStep = 1;
            unknown13C = steps;
            transitionActive = 1;
            unknown25C = fgCameraConstants.kTransRate;
        } else {
            AnchorCamera(false, &armPosition->anchor.offset);
            cameraOffset = turned;
            zoom = false;
        }
        unknown228 = VU0_v3length(&cameraOffset);
    }

    VU0_v3add(&lookAt, &cameraOffset, &eye);
    if (arm->checkCollisions)
        ResolveAllCollisions(&eye, anchorPosition, true);

    if (cameraMode == fgCameraModeIndices.missile) {
        VU0_v4copy(MatrixRow(anchorMatrix, 1), &upVector);
    } else if (arm->upRate > 0.0f && !replay) {
        Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
        float level = Abs(anchorMatrix->mtx[2][1]);
        float upright = -anchorMatrix->mtx[1][1];
        if (upright < 0.0f)
            upright = 0.0f;
        double blend = (1.0 - Abs(anchorMatrix->mtx[0][1])) * (1.0 - level) * upright + level;
        float roll = (float)blend;
        RPlayerCamState *camState = state;
        if (camState->unknown30 > 0) {
            if (!camState->unknown2C)
                camState->unknown30--;
            if (level < 0.7f) {
                blend *= 0.1f;
                roll = (float)blend;
            }
        }
        if (blend > 1.0)
            roll = 1.0f;
        BlendVectors(&up, &up, MatrixRow(anchorMatrix, 1), roll);
        VU0_v4unitxyz(&up, &up);
        BlendVectors(&upVector, &upVector, &up, arm->upRate);
        VU0_v4unitxyz(&upVector, &upVector);
    }

    Coord4 lift = { 0.0f, arm->lookUp, 0.0f, 0.0f };
    SetViewingTransform(&upVector, &lift.x, GetAnchorLinearVelocity(), 0);
    float modeZoom = mode->defaultFov;
    if (zoom)
        RWorldCamera::SetCameraZoom(modeZoom, fgCameraConstants.kZoomIncSpeed * zoomSlope);
    else if (modeZoom > 2.0f)
        fieldOfView = modeZoom;
}

// FUNC_AT(0x00088380)
void RPlayerCamera::UpdateTumbleCam() {
    if (previousCameraMode >= fgCameraTables.modeCount)
        return;
    const CameraModeInfo *mode = &fgCameraTables.modes[previousCameraMode];
    Coord4 offset;
    if (mode->type == kCameraHeli) {
        if (mode->index < 0 || mode->index >= fgCameraTables.heliCount)
            return;
        const HeliCamInfo *arm = &fgCameraTables.helis[mode->index];
        const HeliArmInfo *armPosition = &arm->arms[currentArm];
        VU0_v4scale(&armPosition->sideways, arm->tumbleArmScale, &offset);
        AnchorCamera(true, &armPosition->anchor.offset);
    } else {
        offset.x = 0.0f;
        offset.y = 3.0f;
        offset.z = -6.0f;
        offset.w = 0.0f;
        AnchorCamera(true, &fgCameraTables.tumbleAnchor.offset);
    }
    unknown228 = VU0_v3length(&offset);

    Coord4 heading;
    if (GetAnchorSpeed() != 0.0f)
        heading = *AsVector4(GetAnchorLinearVelocity());
    else
        VU0_v4sub(&lookAt, &eye, &heading);
    Coord4 toTarget;
    VU0_v4sub(&lookAt, &eye, &toTarget);
    LerpXYZ(&toTarget, &heading, fgCameraTables.tumbleVectorLerp, &toTarget);
    Coord4 turned = {};
    RotateOffsetToHeading(&offset, &toTarget, &turned);
    VU0_v4unitxyz(&turned, &turned);
    VU0_v4scale(&turned, unknown228, &turned);

    if (modeChangeFlags & kAnchorChanged) {
        if (!DoSmoothModeChange()) {
            cameraOffset.x = eye.x;
            cameraOffset.y = eye.y + 2.0f;
            cameraOffset.z = eye.z;
            VU0_v4sub(&cameraOffset, &lookAt, &cameraOffset);
        } else {
            VU0_v4sub(&eye, &lookAt, &cameraOffset);
        }
    }
    LerpXYZ(&cameraOffset, &turned, fgCameraTables.tumbleRelPosLerp, &cameraOffset);
    VU0_v3add(&lookAt, &cameraOffset, &eye);
    ResolveAllCollisions(&eye, AsVector4(GetAnchorPosition()), false);
    tumbleCamIndex--;

    Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
    SetViewingTransform(&up, GetAnchorLinearVelocity(), 0);
    RWorldCamera::SetCameraZoom(fieldOfView, fgCameraConstants.kZoomIncSpeed * zoomSlope);
}

// FUNC_AT(0x000886f0)
void RPlayerCamera::UpdateEllipseCam() {
    const CameraModeInfo *mode = &fgCameraTables.modes[cameraMode];
    if (mode->index < 0 || mode->index >= fgCameraTables.ellipseCount)
        return;
    const EllipseCamInfo *ellipse = &fgCameraTables.ellipses[mode->index];
    // The counts are used unsigned
    unsigned facets = ellipse->facets;
    unsigned heightCount = ellipse->heightCount;
    const int8_t *heights = ellipse->heights;
    unsigned quarter = facets / 4;
    if (modeChangeFlags != 0) {
        unknown264 = Truncate((float)((double)facets * 0.0f));
        unknown268 = 0;
    }
    unknown264 = (unsigned)(unknown264 + 1) % facets;

    // Round the ellipse in steps; the height changes each half lap, eased over the second and fourth quarters
    float lap = (float)((double)(unsigned)unknown264 / (double)facets);
    float turns = lap;
    if (lap < 0.0f)
        turns = lap + 1.0f;
    float sine = sin_fractionalangle(turns);
    turns = lap;
    if (lap < 0.0f)
        turns = lap + 1.0f;
    Coord4 offset;
    offset.x = (float)(CosineTurns(turns) * ellipse->xRad);
    offset.z = sine * ellipse->zRad;
    offset.w = 0.0f;    // stack the original leaves unset
    if ((unsigned)unknown264 % (facets / 2) == 0)
        unknown268 = (unsigned)(unknown268 + 1) % heightCount;
    unsigned step = unknown264;
    if (step < quarter * 3 && (step < quarter || step >= quarter * 2)) {
        offset.y = (float)((double)heights[(unsigned)unknown268 % heightCount] * 0.5f);
    } else {
        double from = (double)heights[(unsigned)unknown268 % heightCount] * 0.5f;
        double to = (double)heights[(unsigned)(unknown268 + 1) % heightCount] * 0.5f;
        offset.y = (float)((to - from) * (double)(step % quarter) / (double)quarter + from);
    }
    RotateOffsetToHeading(&offset, MatrixRow(GetAnchorMatrix4(), 2), &offset);
    offset.y = Abs(offset.y) + 1.0f;

    if (modeChangeFlags == 0) {
        AnchorCamera(true, &ellipse->anchor.offset);
        if (unknown13C > 0 && transitionActive == 1)
            UpdateTransition(&cameraOffset, &offset);
        else
            cameraOffset = offset;
    } else if (DoSmoothModeChange() == 1) {
        AnchorCamera(true, &ellipse->anchor.offset);
        InitTransition(1, &cameraOffset, &offset, mode->smoothTrans);
    } else {
        AnchorCamera(false, &ellipse->anchor.offset);
        cameraOffset = offset;
    }
    VU0_v3add(&lookAt, &cameraOffset, &eye);
    ResolveAllCollisions(&eye, AsVector4(GetAnchorPosition()), false);

    Coord4 up = { 0.0f, 1.0f, 0.0f, 0.0f };
    SetViewingTransform(&up, GetAnchorLinearVelocity(), 0);
    RWorldCamera::SetCameraZoom(mode->defaultFov, fgCameraConstants.kZoomIncSpeed * zoomSlope);
}

// FUNC_AT(0x00088b10)
void RPlayerCamera::SetAutoDriveCamera(float x, float y, int w) {
    Coord4 rotation;
    VU0_v4Init(&rotation);
    rotation.y = -y;
    rotation.x = x;
    rotation.w = 1.0f;
    if (w == 0)
        rotation.w = 0.0f;
    ClearDirectorQueue(directorQueue);
    SetCameraModeByIndex(fgCameraModeIndices.autoDrive, (uint16_t)kAutoDriveLatency, 4, 0, 0, NULL, &rotation);
    lastUpdateStep++;
    (this->*XbeVirtual<decltype(&RPlayerCamera::UpdateCamera)>(this, kVirtualUpdateCamera))();
    SetCameraModeByIndex(fgCameraModeIndices.autoDrive, 0, 4, 0, 0, NULL, &rotation);
    lastUpdateStep++;
    (this->*XbeVirtual<decltype(&RPlayerCamera::UpdateCamera)>(this, kVirtualUpdateCamera))();
}

// FUNC_AT(0x00088be0)
void RPlayerCamera::SetMissileCamera(PhysicsObject *missile) {
    if (state->unknown05)
        return;
    ClearDirectorQueue(directorQueue);
    void *block = OperatorNew(sizeof(RDirectorQueueData));
    RDirectorQueueData *data = NULL;
    int8_t mode = fgCameraModeIndices.missile;
    if (block != NULL)
        data = static_cast<RDirectorQueueData *>(block)->Construct(fgCameraConstants.kMissileCamLatency, mode,
                                                                   fgCameraTables.modes[mode].smoothTrans,
                                                                   RDirectorQueueData::kKeep, NULL, missile, 0);
    directorQueue->AppendData(data);
    if (data != NULL) {
        data->Destruct();
        OperatorDelete(data);
    }
}

// FUNC_AT(0x00088cc0)
void RPlayerCamera::SetPauseCamera() {
    ClearDirectorQueue(directorQueue);
    SetCameraModeByIndex(fgCameraModeIndices.pause, 0, 4, 0, 0, NULL, NULL);
}

// FUNC_AT(0x00088d10)
void RPlayerCamera::SetTumbleCam(unsigned index) {
    tumbleCamIndex = index;
    ClearDirectorQueue(directorQueue);
    if (cameraMode != fgCameraModeIndices.tumble)
        SetCameraModeByIndex(fgCameraModeIndices.tumble, 0, 0, 0, 0, NULL, NULL);
}

// FUNC_AT(0x00088d70)
void RPlayerCamera::RestartCamera() {
    RWorldCamera::RestartCamera();
    unknown1D0 = 0.0f;
    unknown1D4 = 0.0f;
    unknown228 = 0.0f;
    VU0_v4Init(&cameraOffset);
    VU0_v4Init(&fixedCamPosition);
    VU0_v4Init(&upVector);
    upVector.y = 1.0f;
    VU0_v4Init(&unknown230);
    unknown230.w = 0.0f;
    VU0_v4Init(&unknown240);
    unknown240.w = 0.0f;
    unknown250 = 0.0f;
    unknown254 = 0.0f;
    state->ResetState();
    state->lookingBack = false;
    state->lookBackOff = true;
    lockOnPoint = NULL;
    VU0_v4Init(&lockOnTarget);
    lockOnParam2 = 0;
    lockOnParam1 = 0;
    tumbleCamIndex = 0;
    unknown220 = false;
    unknown221 = false;
    unknown258 = 0.0f;
    shakeStepsLeft = 0;
    shakeAmount = 0.0f;
    shaking = false;
    unknown290 = 0.0f;
    unknown294 = 0.0f;
    autoDriveRotationX = 0.0f;
    autoDriveRotationY = 0.0f;
    autoDriveRotating = false;
    autoDriveZoom = 0.0f;
    zoomSlope = 1.0f;
    spinState = 0;
    unknown1D8 = 0.0f;
    unknown1DC = 0.0f;
    aimPitch = 0.0f;
    aimYaw = 0.0f;
    targetAngleX = 0.0f;
    targetAngleY = 0.0f;
    unknown1D0 = 0.0f;
    unknown1D4 = 0.0f;
    adWeaponFlag = false;
    adWeaponAnimState = 0;
    VU0_v4Init(&spinVec);
    unknown200 = 0;
    currentArm = 0;
    autoDriveArm = -1;
    unknown26C = 0;
    transitionFactor = 0.0f;
    VU0_v4Init(&transitionVec);
    unknown25C = 0.0f;
    modeChangeFlags = kAnchorChanged;
    unknown27D = 0;
    unknown260 = 0.0f;
    transitionStep = 0;
    unknown13C = 0;
    transitionActive = 0;
    lastUpdateStep = 0;
    weaponFired = 0;
    unknown1F0 = 0.0f;
    unknown1F4 = 0.0f;
    autoDriveForwardLock = false;
    relativeAnimMatrix = kIdentity;
    matrix = kIdentity;
    matrixChanged = true;
    collisionState = 0;
    MatrixCopy(&matrix, &aimMatrix);
    VU0_v4copy(MatrixRow(&matrix, 2), &forwardAimVec);
    ClearDirectorQueue(directorQueue);
    directorQueue->RestartDirectorQueue();
    unknown264 = 0;
    unknown268 = 0;
    VU0_v4Init(&unknown2D0);
    if (collider != NULL) {
        collider->Destruct();
        UMemory::FastFree(collider, sizeof(WCollider));
    }
    collider = NULL;
}

// FUNC_AT(0x00089010)
void RPlayerCamera::AbortCinematic() {
    unknown13C = 0;
    ClearDirectorQueue(directorQueue);
    state->unknown05 = false;
    state->unknown06 = false;
}

// FUNC_AT(0x00089060)
RPlayerCamera* RPlayerCamera::Construct() {
    RWorldCamera::Construct();
    vtable = reinterpret_cast<void **>(0x001918ac);
    unknown144 = false;
    unknown145 = false;

    void *block = UMemory::FastAlloc(0x40, "WWorldPos");
    worldPos = block != NULL ? static_cast<WWorldPos *>(block)->Construct() : NULL;
    block = UMemory::FastAlloc(0xc0, "WRoadNav");
    roadNav = block != NULL ? static_cast<WRoadNav *>(block)->Construct() : NULL;
    roadNav->mode = kNavDirection;
    block = UMemory::FastAlloc(0x14, "RDirectorQueue");
    directorQueue = NULL;
    if (block != NULL) {
        directorQueue = static_cast<RDirectorQueue *>(block);
        directorQueue->Construct(this);
    }
    block = UMemory::FastAlloc(0x70, "RCameraSpline");
    spline = NULL;
    if (block != NULL) {
        spline = static_cast<RCameraSpline *>(block);
        spline->Construct();
    }
    block = UMemory::FastAlloc(0x34, "RPlayerCamState");
    state = NULL;
    if (block != NULL) {
        state = static_cast<RPlayerCamState *>(block);
        state->Construct(this);
    }
    block = OperatorNew(1);
    iniLoader = NULL;
    if (block != NULL) {
        iniLoader = static_cast<RCameraIniLoader *>(block);
        iniLoader->Construct(this);
    }
    collider = NULL;
    RestartCamera();
    StartCameraInputReceiver();
    return this;
}

// Changes to `mode`. Unless it is a noisy heli mode or `b` is 0 or 1, the change is animated: a scratch camera
// runs the mode once to find where it would put the camera, and an animation camera moves there first.
// FUNC_AT(0x00089200)
void RPlayerCamera::SetOldCameraMode(int mode, int delay, unsigned b, bool relative) {
    if (mode >= fgCameraTables.modeCount)
        return;
    const CameraModeInfo *entry = &fgCameraTables.modes[mode];
    if (b <= 1 || IsHeliWithNoise(entry)) {
        modeChangeFlags |= kNoSmoothChange;
        SetCameraModeByIndex(mode, delay, 4, 0, 0, NULL, NULL);
        return;
    }

    uint32_t update = entry->update;
    alignas(16) RPlayerCamera scratch;
    scratch.Construct();
    scratch.SetAnchor(anchor);
    scratch.previousCameraMode = mode;
    scratch.modeChangeFlags |= kNoSmoothChange;
    VU0_v4sub(&scratch.eye, &scratch.lookAt, &scratch.cameraOffset);
    (scratch.*XbeOriginal<decltype(&RPlayerCamera::UpdateCamera)>((size_t)update))();
    if (relative) {
        scratch.AnchorRelativeCamera(false, &fgCameraTables.animationAnchor.offset);
        VU0_v4sub(&scratch.eye, &scratch.lookAt, &scratch.cameraOffset);
        CARP::Instance *point = static_cast<CARP::Instance *>(OperatorNew(sizeof(CARP::Instance)));
        MEM_fill(point, 0, sizeof(CARP::Instance));
        MATRIX4 inverse;
        VU0_MATRIX4_transpose(&inverse, GetAnchorMatrix4());
        Coord4 offset, forward;
        VU0_MATRIX4_vect3rotate(&scratch.cameraOffset, &inverse, &offset);
        VU0_MATRIX4_vect3rotate(MatrixRow(&scratch.matrix, 2), &inverse, &forward);
        point->axisZ[0] = forward.x;
        point->axisZ[1] = forward.y;
        point->axisZ[2] = forward.z;
        point->position[0] = offset.x;
        point->position[1] = offset.y;
        point->position[2] = offset.z;
        SetCameraModeByIndex(fgCameraModeIndices.relativeAnimation, delay, 0x1c, b, 0, point, NULL);
    } else {
        CARP::Instance *point = static_cast<CARP::Instance *>(OperatorNew(sizeof(CARP::Instance)));
        MEM_fill(point, 0, sizeof(CARP::Instance));
        Instance_SetMatrix(point, 0, &scratch.matrix);
        point->position[0] = scratch.cameraOffset.x;
        point->position[1] = scratch.cameraOffset.y;
        point->position[2] = scratch.cameraOffset.z;
        point->flags = 1;
        lookAtOffset = scratch.lookAtOffset;
        SetCameraModeByIndex(fgCameraModeIndices.worldAnimation, delay, 0x1c, b, 0, point, NULL);
    }
    SetCameraModeByIndex(mode, 0, 4, 0, 0, NULL, NULL);
    scratch.Destruct();
}

// FUNC_AT(0x000894d0)
void RPlayerCamera::SetLastSelectableCameraMode(int delay, unsigned b, bool relative, bool notAutoDrive) {
    if (notAutoDrive && (cameraMode == fgCameraModeIndices.autoDrive || previousCameraMode == fgCameraModeIndices.autoDrive))
        return;
    if (lastSelectableCameraMode >= fgCameraTables.modeCount)
        lastSelectableCameraMode = 0;
    if (!fgCameraTables.modes[lastSelectableCameraMode].selectable && fgCameraTables.modes[cameraMode].selectable)
        lastSelectableCameraMode = cameraMode;
    if (!fgCameraTables.modes[lastSelectableCameraMode].selectable) {
        for (lastSelectableCameraMode = 0; (unsigned)lastSelectableCameraMode < (unsigned)fgCameraTables.modeCount;
             lastSelectableCameraMode++) {
            if (fgCameraTables.modes[lastSelectableCameraMode].selectable)
                break;
        }
    }
    if (fgCameraTables.modes[lastSelectableCameraMode].selectable)
        SetOldCameraMode(lastSelectableCameraMode, delay, b, relative);
}

// FUNC_AT(0x000895e0)
void RPlayerCamera::EndMissileCamera() {
    if (!state->unknown20)
        return;
    state->unknown20 = false;
    SetAnchor(PlayerObjects.first[0]);
    SetLastSelectableCameraMode(0, 0, false, true);
    modeChangeFlags |= kNoSmoothChange;
    RColorize_SetEnabled(Colorize, 0, 0);
}

// Puts the camera on an animation path. With `timePath`, the path's steps are shared out again over its points
// in proportion to the distances between them.
// FUNC_AT(0x00089630)
void RPlayerCamera::TriggerAnimationCamera(CARP::Instance *animation, int b, unsigned short delay, bool timePath,
                                           int fov) {
    if (animation == NULL)
        return;
    fieldOfView = kAnimationFieldOfView;
    zoomFov = kAnimationFieldOfView;
    zoomFovTarget = kAnimationFieldOfView;
    if ((double)fov > 2.0f)
        zoomFovTarget = (float)fov;
    spinState = 0;
    state->ResetStateForAnimation();
    autoDriveZoom = 0.0f;
    zoomSlope = 1.0f;
    unknown290 = 0.0f;
    unknown294 = 0.0f;
    autoDriveRotationX = 0.0f;
    autoDriveRotationY = 0.0f;
    autoDriveRotating = false;
    fgCameraTables.weaponArmTurn = 0.0f;
    EndMissileCamera();
    SetCameraModeByIndex(fgCameraModeIndices.worldAnimation, delay, 4, b, 0, animation, NULL);
    if (animation->articleDesc.value != 0)
        return;

    spline->AddToSplinePtList(AsVector4(animation->position));
    if (timePath && int32_t(spline->GetPointListSize()) > 1) {
        // Each queued animation without an article keeps its distance from the one before in its unknown2c
        RDirectorQueue::Queue *queue = &directorQueue->queue;
        float pathLength = 0.0f;
        unsigned pathSteps = 0;
        const void *previous = &eye;
        DirectorQueueNode *node = queue->Begin();
        for (int i = 0; i < (int)queue->size; i++, node = node->next) {
            if (fgCameraTables.modes[node->value.cameraMode].type != kCameraAnimation)
                continue;
            CARP::Instance *point = node->value.data18;
            if (point->articleDesc.value != 0)
                continue;
            float distance = vec3distance(point->position, previous);
            point->unknown2c = std::bit_cast<uint32_t>(distance);
            pathLength = distance + pathLength;
            pathSteps += node->value.unknown14;
            previous = point->position;
        }
        if (pathSteps != 0 && pathLength != 0.0f) {
            float perLength = (float)(1.0 / pathLength);
            node = queue->Begin();
            for (int i = 0; i < (int)queue->size; i++, node = node->next) {
                if (fgCameraTables.modes[node->value.cameraMode].type != kCameraAnimation)
                    continue;
                CARP::Instance *point = node->value.data18;
                if (point->articleDesc.value != 0)
                    continue;
                float distance = std::bit_cast<float>(point->unknown2c);
                node->value.unknown14 = (uint16_t)Truncate((float)((double)pathSteps * distance * perLength));
            }
        }
    }
    lastUpdateStep++;
    (this->*XbeVirtual<decltype(&RPlayerCamera::UpdateCamera)>(this, kVirtualUpdateCamera))();
}

// FUNC_AT(0x000898d0)
void RPlayerCamera::TriggerAIPathAnimationCamera(void *path, int b, unsigned short delay, int fov) {
    if (fov <= 0)
        fieldOfView = kAnimationFieldOfView;
    else if ((double)fov > 2.0f)
        fieldOfView = (float)fov;
    zoomFov = kAnimationFieldOfView;
    zoomFovTarget = kAnimationFieldOfView;
    spinState = 0;
    state->ResetStateForAnimation();
    autoDriveZoom = 0.0f;
    zoomSlope = 1.0f;
    unknown290 = 0.0f;
    unknown294 = 0.0f;
    autoDriveRotationX = 0.0f;
    autoDriveRotationY = 0.0f;
    autoDriveRotating = false;
    fgCameraTables.weaponArmTurn = 0.0f;
    EndMissileCamera();

    void *block = OperatorNew(sizeof(RDirectorQueueData));
    RDirectorQueueData *data = NULL;
    if (block != NULL)
        data = static_cast<RDirectorQueueData *>(block)->Construct(delay, fgCameraModeIndices.aiPathAnimation, b,
                                                                   RDirectorQueueData::kKeep, NULL, NULL, 0);
    data->data1C = path;
    directorQueue->AppendData(data);
    data->Destruct();
    OperatorDelete(data);
    lastUpdateStep++;
    (this->*XbeVirtual<decltype(&RPlayerCamera::UpdateCamera)>(this, kVirtualUpdateCamera))();
}

// The mode whose id is `id`, or the last selectable one for id 0
// FUNC_AT(0x00089a40)
void RPlayerCamera::ForceCameraChange(unsigned id, int delay, int b, int unused) {
    if (id == 0) {
        SetLastSelectableCameraMode(delay, b, false, true);
        return;
    }
    for (int mode = 0; mode < fgCameraTables.modeCount; mode++) {
        if ((unsigned)fgCameraTables.modes[mode].camID == id) {
            SetCameraModeByIndex(mode, delay, 4, b, 0, NULL, NULL);
            return;
        }
    }
}

// FUNC_AT(0x00089ac0)
void RPlayerCamera::EndCameraAnim(unsigned short delay, int b, bool c, unsigned id) {
    RPlayerCamState *camState = state;
    if (!camState->unknown05 && !camState->unknown06)
        return;
    SetAnchor(PlayerObjects.first[0]);
    int found = -1;
    if (id != 0) {
        for (int mode = 0; mode < fgCameraTables.modeCount; mode++) {
            if ((unsigned)fgCameraTables.modes[mode].camID == id) {
                found = mode;
                break;
            }
        }
    }
    if (found != -1)
        SetCameraModeByIndex(found, delay, 4, b, 0, NULL, NULL);
    else
        SetLastSelectableCameraMode(delay, b, c, true);
    state->unknown05 = false;
    state->unknown06 = false;
}
