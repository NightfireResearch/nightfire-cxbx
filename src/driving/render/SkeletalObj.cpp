#include "SkeletalObj.h"

#include "../anim/ProcAnim.h"            // SetProcAnimXFormList, SetProcAnimParamList
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/Simulation.h"
#include "../platform/RealMath.h"
#include "../world/World.h"               // ArticleEffect

#include <bit>
#include <string.h>

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// RSkeletalObj (0x00090b90..0x000914f0), ported from the listing. The spring step is the original's x87 chain: the
// products kept on the stack are doubles here, rounded where the original stores them.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals

#define TheSimulation ((void *)0x00233ff0)
#define SimTimeStep FLOAT_AT(0x00234e30)                    // the simulation's step, in seconds

static RSceneObj_vtbl *const kSkeletalObjVtable = (RSceneObj_vtbl *)0x00191c78;

constexpr uint8_t kProcAnimXFormTransform = 0xfe;

// The spring's constants
constexpr float kGravity = -9.821f;
constexpr float kDriveRate = 7000.0f;                       // the pull towards a positive limit
constexpr float kTwelfth = 1.0f / 12.0f;                    // a rod's moment of inertia: m * length^2 / 12
constexpr float kStartInertiaScale = 10.0f;                 // the first step's
constexpr float kDamping = 0.95f;
constexpr float kBounce = -0.9f;                            // the angular velocity's, at either end of the swing
static_assert(std::bit_cast<uint32_t>(kGravity) == 0xc11d22d1 && std::bit_cast<uint32_t>(kTwelfth) == 0x3daaaaab &&
              std::bit_cast<uint32_t>(kDamping) == 0x3f733333 && std::bit_cast<uint32_t>(kBounce) == 0xbf666666,
              "the original's constants");

// 1 << bit as __allshl computes it: none past bit 63
static uint64_t Bit64(uint32_t bit) {
    return bit < 64 ? uint64_t(1) << bit : 0;
}

// FUNC_AT(0x00090b90)
RSkeletalObj* RSkeletalObj::Construct(CARP::Instance *instance) {
    RSceneObj::Construct(instance);
    springPos = {0.0f, 0.0f, 0.0f, -1.0f};
    springPrevPos = {0.0f, 0.0f, 0.0f, -1.0f};
    springEffects = 0;
    numBones = 0;
    bones = NULL;
    vtable = kSkeletalObjVtable;
    memset(params, 0, sizeof(params));
    return this;
}

// FUNC_AT(0x00090c10)
void RSkeletalObj::Destruct() {
    vtable = kSkeletalObjVtable;
    if (bones != NULL)
        UMemory::FastFree(bones, numBones * sizeof(MATRIX4));
    RSceneObj::Destruct();
}

// FUNC_AT(0x000912d0)
RSkeletalObj* RSkeletalObj::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(RSkeletalObj));
    return this;
}

// FUNC_AT(0x00090c80)
void RSkeletalObj::AllocateSkeleton(uint32_t count) {
    if (bones != NULL)
        UMemory::FastFree(bones, numBones * sizeof(MATRIX4));
    numBones = count;
    if (count == 0) {
        bones = NULL;
        return;
    }
    bones = static_cast<MATRIX4 *>(UMemory::FastAlloc(count * sizeof(MATRIX4), "Bones"));
    for (uint32_t i = 0; i < count; i++)
        VU0_MATRIX4Init(&bones[i]);
}

// The lever runs from the hinge to the effect's point; its middle, turned by the angle, is where the forces act.
// The point's velocity in the body's frame (linear plus angular, as |w| |r| along w x r) against the last two steps'
// gives a drag; gravity along the body's up axis and, for a positive limit, a pull towards the limit add to it. The
// torque about the axis over the lever's moment of inertia (ten times smaller on the first step) is the angular
// acceleration. The angle is then kept between 0 and the limit, the angular velocity bouncing back at either end;
// a spring the torque pushes past 0 the wrong way, harder than its threshold, is released and comes to rest at 0.
// FUNC_AT(0x00090cf0)
bool RSkeletalObj::UpdateSpringMassSystem(RigidBody *body, CARP::Instance *, const Coord3 *point, const Coord3 *axis, float torqueThreshold, float mass, float limit, float *angle, float *angularVelocity) {
    float timeStep = SimTimeStep;
    Coord4 lever = {point->x, point->y, point->z, 1.0f};
    VU0_v4scale(&lever, 0.5f, &lever);
    Coord4 unitAxis = {axis->x, axis->y, axis->z, 1.0f};
    VU0_v4unitxyz(&unitAxis, &unitAxis);
    MATRIX4 rotation;
    MATRIX4_axisrotate(&unitAxis, *angle, &rotation);
    VU0_MATRIX4_vect4mult(&lever, &rotation, &lever);

    bool starting = false;
    if (springPrevPos.w == -1.0f) {
        springPos = {0.0f, 0.0f, 0.0f, 1.0f};
        springPrevPos = {0.0f, 0.0f, 0.0f, 1.0f};
        starting = true;
    }

    Coord4 drag = {0.0f, 0.0f, 0.0f, 1.0f};
    Coord3 local;
    body->GetLocalVelocity(&local);
    Coord4 velocity = {local.x, local.y, local.z, 1.0f};
    body->GetLocalAngularVelocity(&local);
    Coord4 spin = {local.x, local.y, local.z, 1.0f};
    Coord4 end = {point->x, point->y, point->z, 1.0f};
    Coord4 direction = {0.0f, 0.0f, 0.0f, 0.0f};
    VU0_v4unitcrossprodxyz(&spin, &end, &direction);
    float endDistance = VU0_v3length(&end);
    VU0_v4scale(&direction, (float)((double)VU0_v3length(&spin) * endDistance), &spin);
    VU0_v3add(&spin, &velocity, &velocity);

    Coord4 relative = {0.0f, 0.0f, 0.0f, 0.0f};
    Coord4 change = {0.0f, 0.0f, 0.0f, 0.0f};
    VU0_v4sub(&velocity, &springPos, &relative);
    VU0_v4sub(&springPos, &springPrevPos, &change);
    VU0_v4addscale(&change, &relative, 0.5f, &drag);
    VU0_v4scale(&drag, -mass, &drag);
    double gravity = (double)timeStep * mass * kGravity;
    springPrevPos = springPos;
    springPos = velocity;

    Coord4 weight = {0.0f, 0.0f, 0.0f, 0.0f};
    VU0_v4scale(body->info->orientation.mtx[1], (float)gravity, &weight);
    Coord4 drive = {0.0f, 0.0f, 0.0f, 1.0f};
    if (limit >= 0.0f)
        VU0_v4scale(&unitAxis, (float)(((double)limit - *angle) * timeStep * kDriveRate), &drive);
    Coord4 force = {0.0f, 0.0f, 0.0f, 0.0f};
    VU0_v3add(&weight, &drag, &force);
    Coord4 torque = {0.0f, 0.0f, 0.0f, 0.0f};
    VU0_v4crossprodxyz(&lever, &force, &torque);
    Coord4 totalTorque = {0.0f, 0.0f, 0.0f, 0.0f};
    VU0_v3add(&torque, &drive, &totalTorque);
    float axialTorque = v3dotprod(&totalTorque, &unitAxis);

    double length = (double)VU0_v3length(&lever) * 2.0;
    double inertia = length * length * mass * kTwelfth;
    double acceleration = (starting ? (double)kStartInertiaScale : 1.0) / inertia;
    double newVelocity = (acceleration * axialTorque * timeStep + *angularVelocity) * kDamping;
    *angularVelocity = (float)newVelocity;
    double newAngle = newVelocity * timeStep + *angle;
    *angle = (float)newAngle;

    bool moving = true;
    if (limit >= 0.0f) {
        if (newAngle < 0.0 && axialTorque > torqueThreshold) {
            *angularVelocity = 0.0f;
            *angle = 0.0f;
            return false;
        }
        if (newAngle > limit) {
            *angle = limit;
            *angularVelocity *= kBounce;
        } else if (newAngle < 0.0) {
            *angle = 0.0f;
            *angularVelocity *= kBounce;
        }
        return moving;
    }
    if (newAngle > 0.0 && axialTorque > torqueThreshold) {
        *angularVelocity = 0.0f;
        *angle = 0.0f;
        moving = false;
    }
    if (*angle < limit) {
        *angle = limit;
        *angularVelocity *= kBounce;
    } else if (*angle > 0.0f) {
        *angle = 0.0f;
        *angularVelocity *= kBounce;
    }
    return moving;
}

// FUNC_AT(0x000912b0)
void RSkeletalObj::SetProcAnimState() {
    SetProcAnimXFormList(bones, numBones);
    SetProcAnimParamList(params, kParams);
}

// FUNC_AT(0x00091300)
void RSkeletalObj::PostLoad() {
    AllocateSkeleton(GetNumBones());
    for (uint32_t i = 0; i < animHandle->effectCount; i++) {
        if (animHandle->effects[i].type == ArticleEffect::kTypeSpring)
            springEffects |= Bit64(i);
    }
}

// The handle's live springs (its effectsOn bits) are stepped; a released one's bit is cleared.
// FUNC_AT(0x00091360)
void RSkeletalObj::UpdatePosition(bool force) {
    RSceneObj::UpdatePosition(force);
    uint32_t i = 0;
    for (uint64_t springs = springEffects; springs != 0; springs >>= 1, i++) {
        if ((springs & 1) == 0)
            continue;
        Handle *handle = animHandle;
        if (((handle->effectsOn >> i) & 1) == 0)
            continue;
        ArticleEffect *effect = &handle->effects[i];
        if (effect->type != ArticleEffect::kTypeSpring)
            continue;
        CARP::Instance *instance = &handle->Instances()[effect->instance];
        if (instance->procAnimType != kProcAnimXFormTransform)
            continue;
        RigidBody *body = Simulation_GetRigidBody(TheSimulation, 0, physics->rigidBodySlot);
        ArticleSpringEffect *spring = &effect->spring;
        bool moving = UpdateSpringMassSystem(body, instance, &effect->position, &spring->axis, spring->torqueThreshold,
                                             spring->mass, spring->limit, &spring->angle, &spring->angularVelocity);
        // The bone: the quaternion of the angle about the axis
        float sine = sin_fractionalangle(spring->angle * 0.5f);
        Coord4 rotation;
        rotation.x = sine * spring->axis.x;
        rotation.y = sine * spring->axis.y;
        rotation.z = sine * spring->axis.z;
        rotation.w = cos_fractionalangle(spring->angle * 0.5f);
        VU0_quattom4(&bones[instance->procAnimIndex - 1], &rotation);
        if (!moving)
            animHandle->effectsOn &= ~Bit64(i);
    }
}
