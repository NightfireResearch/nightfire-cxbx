#pragma fp_contract(off)

#include "RigidBody.h"

#include <stdio.h>
#include <string.h>

#include "PhysicsMath.h"
#include "PhysicsObject.h"
#include "../../helpers.h"
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/PhysicsUtil.h"        // Util_Bound
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../world/WorldPos.h"

// ---------------------------------------------------------------------------------------------------------------
// RigidBody's core: construction and reset, each step's initial forces (gravity, drag), the levers against the
// ground, the impulse scaling between two bodies, sleeping, and the integration of momentum, position and
// orientation. Ported from the listing; the x87 arithmetic is the original's - chains in double in its order,
// rounded where it stores to a float, comparisons keeping its sense for NaNs.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define Simulation_GetRigidBodyInfo ((RigidBodyInfo *(__fastcall *)(void *, int, int ownerIndex))0x000b2760)
#define Crt_stricmp ((int (*)(const char *, const char *))0x00134537)
#define String_Assign ((void (__fastcall *)(void *, int, const char *text, uint32_t length))0x00013630)
#define LogicError_Construct ((void (__fastcall *)(void *, int, const void *message))0x00013700)
#define Crt_CxxThrowException ((void (__stdcall *)(void *object, const void *throwInfo))0x001325ad)

// ---- globals
#define Sim ((void *)0x00233ff0)                    // the Simulation
#define SimTimeStep FLOAT_AT(0x00234e30)            // the simulation's step, in seconds
#define ZeroVector (*(const Coord3 *)0x00243030)    // the game's zero vector, never written
#define LengthErrorVtable ((void *)0x00189eec)      // std::length_error's
#define LengthErrorThrowInfo ((const void *)0x001a89bc)

namespace {

// The warning beside the provisional port (code no shipped data reaches): said once, the first time it runs
void RigidCoreUntested(const char *what) {
    printf("[physics] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define RIGIDCORE_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            RigidCoreUntested(what); \
        } \
    } while (0)

constexpr float kMinHalfExtent = 0.001f;
constexpr float kUnderwaterGravityScale = 0.1f;
constexpr float kUnderwaterDrag = -0.1f;
constexpr float kDragGravityScale = 0.2f;
constexpr float kDrag = -1.0f;
constexpr float kJungleTruckGravity = -34.0f;
constexpr float kGroundFrictionScale = 4.0f;
constexpr float kFallingSpeed = -32.0f;             // CollideWithGround: falling faster tests the four lower levers
constexpr float kFastSquared = 25.0f;
constexpr float kUpright = 0.8f;
constexpr int kUprightLeverEnd = 12;
constexpr float kAligned = 0.866f;
constexpr float kAlignedCogShift = 0.12f;
constexpr float kCogShift = -0.35f;
constexpr float kBoostBase = 0.5f;
constexpr float kBoostScale = 1.5f;
constexpr float kBoostedShare = 0.5f;
constexpr float kTiltedUp = 0.7f;
constexpr float kTiltedSleepScale = 0.25f;
constexpr float kVehicleHalfWidening = 0.18f;
constexpr uint16_t kStepCountMax = 0xffff;

// The orientation matrix, the inverse inertia tensor in the world's frame and the levers in the world's frame,
// from the quaternion (the constructor's and ResetObject's)
void SetFrame(RigidBody *body) {
    RigidBodyInfo *info = body->info;
    MATRIX4 rotation, transposed, scale, scaled;
    // VU0_MATRIX3x4_mult leaves row 3 as it was: the original copies its stack's leftovers there; zero here
    MATRIX4 worldInverseInertia = {};
    VU0_quattom4(&rotation, &body->orientation);
    VU0_MATRIX4_transpose(&transposed, &rotation);
    BuildScale(&scale, body->inverseInertiaX, body->inverseInertiaY, body->inverseInertiaZ);
    VU0_MATRIX3x4_mult(&scale, &transposed, &scaled);
    VU0_MATRIX3x4_mult(&rotation, &scaled, &worldInverseInertia);
    info->orientation = rotation;
    info->worldInverseInertia = worldInverseInertia;
    VU0_MATRIX4_vect4multarray(info->levers, &rotation, info->worldLevers, info->leverCount);
}

// The game's std::string (MSVC 7.x): its own 16 bytes, or a pointer to the heap past them
struct GameString {
    uint32_t allocator;
    char buffer[16];
    uint32_t size;
    uint32_t capacity;
};
static_assert(sizeof(GameString) == 0x1c, "the game's std::string is 28 bytes");

// The game's std::length_error: std::logic_error's vtable, std::exception's message and its string
struct GameLengthError {
    void *vtable;
    uint8_t unknown04[0x24];
};

} // namespace

// FUNC_AT(0x000b0b20)
RigidBody* RigidBody::Construct(int8_t ownerIndex, int8_t kind, const Coord3 *position, const Coord3 *velocity,
                                const Coord3 *angularVelocity, const MATRIX4 *orientation, float mass,
                                const Coord3 *inverseInertia, const Coord4 *halfExtents, PhysicsObject *owner,
                                bool moving, bool flag1) {
    this->position = *position;
    this->velocity = *velocity;
    this->angularVelocity = *angularVelocity;
    this->mass = mass;
    this->kind = kind;
    groundContacts = 0;
    this->ownerIndex = ownerIndex;
    torque = ZeroVector;
    if (moving) {
        this->velocity = *velocity;
        this->angularVelocity = *angularVelocity;
    } else {
        this->velocity = Coord3{0.0f, 0.0f, 0.0f};
        this->angularVelocity = Coord3{0.0f, 0.0f, 0.0f};
    }
    VU0_m4toquat(&this->orientation, orientation);
    momentum.x = mass * this->velocity.x;
    inverseInertiaX = inverseInertia->x;
    inverseInertiaY = inverseInertia->y;
    inverseInertiaZ = inverseInertia->z;
    momentum.y = mass * this->velocity.y;
    force = Coord3{0.0f, 0.0f, 0.0f};
    momentum.z = mass * this->velocity.z;
    torque = Coord3{0.0f, 0.0f, 0.0f};
    angularMomentum.x = this->angularVelocity.x / inverseInertiaX;
    angularMomentum.y = this->angularVelocity.y / inverseInertiaY;
    angularMomentum.z = this->angularVelocity.z / inverseInertiaZ;

    info = Simulation_GetRigidBodyInfo(Sim, 0, this->ownerIndex);
    info->unknown4ff = 0;
    memset(info, 0, sizeof(RigidBodyInfo));
    info->halfExtents.x = halfExtents->x < kMinHalfExtent ? kMinHalfExtent : halfExtents->x;
    info->halfExtents.y = halfExtents->y < kMinHalfExtent ? kMinHalfExtent : halfExtents->y;
    info->halfExtents.z = halfExtents->z < kMinHalfExtent ? kMinHalfExtent : halfExtents->z;
    info->halfExtents.w = 0.0f;
    for (int i = 0; i < kMaxLevers; i++) {
        WWorldPos *leverPosition = (WWorldPos *)UMemory::FastAlloc(sizeof(WWorldPos), "WWorldPos");
        info->leverPositions[i] = leverPosition ? leverPosition->Construct() : NULL;
    }
    info->worldPosition = &owner->worldPos;
    radius = VU0_v3length(halfExtents);
    InitLevers(owner, halfExtents);
    sleepState = moving ? kAwake : kAsleep;
    info->cornerBank = 0;
    SetFrame(this);
    UpdateZonalInfo();
    if (this->kind == 1 || this->kind == 2)
        flags |= kFlag0;
    else
        flags &= ~kFlag0;
    if (flag1)
        flags |= kFlag1;
    else
        flags &= ~kFlag1;
    info->unknown4de = kStepCountMax;
    return this;
}

// FUNC_AT(0x000adc80)
void RigidBody::Destruct() {
    kind = 0;
    for (int i = 0; i < kMaxLevers; i++) {
        WWorldPos *leverPosition = info->leverPositions[i];
        if (leverPosition) {
            NullFunction();     // WWorldPos's destructor
            UMemory::FastFree(leverPosition, sizeof(WWorldPos));
        }
    }
    info = NULL;
}

// FUNC_AT(0x000adcd0)
void RigidBody::ResetObject(const MATRIX4 *orientation, const Coord3 *position) {
    sleepState = kAwake;
    VU0_m4toquat(&this->orientation, orientation);
    velocity = Coord3{0.0f, 0.0f, 0.0f};
    angularVelocity = Coord3{0.0f, 0.0f, 0.0f};
    momentum = Coord3{0.0f, 0.0f, 0.0f};
    angularMomentum = Coord3{0.0f, 0.0f, 0.0f};
    flags &= ~kFlag1;
    this->position = *position;
    groundContacts = 0;
    info->cornerBank = 0;
    info->unknown4ff = 0;
    SetFrame(this);
    UpdateZonalInfo();
    if (kind == 1 || kind == 2)
        flags |= kFlag0;
    else
        flags &= ~kFlag0;
    info->unknown4de = kStepCountMax;
}

// FUNC_AT(0x000ade40)
void RigidBody::ApplyInitialForcesAndTorques() {
    Coord4 gravity = {0.0f, Rigid_GRAVITY, 0.0f, 0.0f};
    Coord4 playerGravity = {0.0f, RigidPlayerGravity, 0.0f, 0.0f};
    force = Coord3{0.0f, 0.0f, 0.0f};
    torque = Coord3{0.0f, 0.0f, 0.0f};
    if (info->unknown4de < kStepCountMax)
        info->unknown4de++;
    info->unknown4ff = info->unknown4fe;
    info->unknown4fe = 0;

    if (RigidUnderwater && kind != 1) {
        gravity.y = gravity.y * kUnderwaterGravityScale;
        Coord4 drag = {velocity.x, velocity.y, velocity.z, 0.0f};
        VU0_v4scale(&drag, kUnderwaterDrag, &drag);
        drag.y = 0.0f;
        ResolveMassScaledForce4(&drag);
    }
    if (kind < 4 && RigidVehicles[ownerIndex]->GetPhysics()->unknownCC != 0) {
        gravity.y = gravity.y * kDragGravityScale;
        Coord4 drag = {velocity.x, velocity.y, velocity.z, 0.0f};
        VU0_v4scale(&drag, kDrag, &drag);
        drag.y = 0.0f;
        ResolveMassScaledForce4(&drag);
    }
    if (kind < 4 && (kind == 1 || Crt_stricmp(RigidVehicles[ownerIndex]->GetCarType(), "paradis_car") == 0)) {
        if (RigidVehicles[ownerIndex]->GetNumWheelsOnGround() == 0) {
            RigidVehicles[ownerIndex]->SetWasInAir(true);
            gravity.x = playerGravity.x;
            gravity.y = playerGravity.y;
            gravity.z = playerGravity.z;
        } else if (RigidVehicles[ownerIndex]->GetWasInAir()) {
            RigidVehicles[ownerIndex]->SetWasInAir(false);
            if (Crt_stricmp(RigidVehicles[ownerIndex]->GetCarType(), "jungle_truck") == 0)
                RigidPlayerGravity = kJungleTruckGravity;
            else
                RigidPlayerGravity = Rigid_BODGE_PLAYER_GRAVITY;
        }
    }
    if (!(kind < 4 && RigidVehicles[ownerIndex]->GetPhysics()->unknownC0 == 1))
        ResolveMassScaledForce4(&gravity);

    if ((int8_t)groundContacts > 2 && kind >= 4 && info->unknown4ff == 0 && info->unknown4fd == 0) {
        Coord4 friction;
        VU0_v4scale(&velocity, Rigid_GROUND_FRICTION_COEFF * kGroundFrictionScale, &friction);
        VU0_v4sub(&velocity, &friction, &velocity);
        VU0_v4scale(&velocity, mass, &momentum);
    }
}

// FUNC_AT(0x000ae150)
void RigidBody::ResolveLeverForces(const LeverContacts *contacts, const Coord4 *levers, const Coord4 *grounds,
                                   const MATRIX4 *worldInverseInertia) {
    float inverseMass = 1.0f / mass;
    for (int i = 0; i < contacts->count; i++) {
        Coord4 lever = {levers[i].x, levers[i].y, levers[i].z, 0.0f};
        Coord4 normal = {grounds[i].x, grounds[i].y, grounds[i].z, 0.0f};

        // the lever's velocity along the ground's normal, and the effective inverse mass there
        Coord4 leverVelocity, arm, turn;
        VU0_v4crossprodxyz(&angularVelocity, &lever, &leverVelocity);
        VU0_v3add(&leverVelocity, &velocity, &leverVelocity);
        float approach = v3dotprod(&leverVelocity, &normal);
        VU0_v4crossprodxyz(&lever, &normal, &arm);
        VU0_MATRIX4_vect3rotate(&arm, worldInverseInertia, &arm);
        VU0_v4crossprodxyz(&arm, &lever, &turn);
        float effectiveInverseMass = v3dotprod(&turn, &normal) + inverseMass;

        float speed = Abs(approach);
        if (speed > Rigid_MICRO_THRESHOLD)
            speed = Rigid_MICRO_THRESHOLD;
        double rate = kind < 4 && RigidVehicles[ownerIndex]->GetPhysics()->unknownC8 != 0
                          ? RigidRestitutionRateC8 : RigidGroundRestitutionRate;
        Coord4 impulse, forcePerMass, normalPart, sliding;
        VU0_v4scale(&normal, (float)(-((rate * speed + 1.0) * approach) / effectiveInverseMass), &impulse);

        // friction against the sliding, in proportion to the force along the normal
        VU0_v4scale(&force, 1.0f / mass, &forcePerMass);
        float pressure = v3dotprod(&normal, &forcePerMass);
        float normalSpeed = v3dotprod(&normal, &leverVelocity);
        VU0_v4scale(&normal, normalSpeed, &normalPart);
        VU0_v4sub(&leverVelocity, &normalPart, &sliding);
        VU0_v4unitxyz(&sliding, &sliding);
        VU0_v4scale(&sliding, pressure * contacts->friction, &sliding);
        VU0_v3add(&impulse, &sliding, &impulse);

        if (!(impulse.y < 0.0f)) {
            Coord4 angularImpulse;
            VU0_v3add(&momentum, &impulse, &momentum);
            VU0_v4crossprodxyz(&lever, &impulse, &angularImpulse);
            VU0_v3add(&angularMomentum, &angularImpulse, &angularMomentum);
            velocity.x = inverseMass * momentum.x;
            velocity.y = inverseMass * momentum.y;
            velocity.z = inverseMass * momentum.z;
            VU0_MATRIX4_vect3rotate(&angularMomentum, worldInverseInertia, &angularVelocity);
        }
    }
}

// FUNC_AT(0x000ae4c0)
void RigidBody::CollideWithGround() {
    if (RigidUnderwater)
        return;
    if (kind < 4 && RigidVehicles[ownerIndex]->GetPhysics()->unknownC4 != 0)
        return;
    if (kind < 4 && RigidVehicles[ownerIndex]->GetSplinePath() != NULL) {
        info->ground.x = 0.0f;
        info->ground.y = 1.0f;
        info->ground.z = 0.0f;
        return;
    }

    // the levers on the ground, and their ground, gathered in the scratch pad
    Coord4 *contactLevers = RigidScratchPad->contactLevers;
    Coord4 *contactGrounds = RigidScratchPad->contactGrounds;
    int contacts = 0;
    groundContacts = 0;
    float deepest = 0.0f;
    int deepestLever = 0;
    int first, end;
    if (kind < 4) {
        if (!(velocity.y > kFallingSpeed) && kind != 1 && (flags & kFlag0)) {
            first = 4;
            double speedSquared = ((double)velocity.x * velocity.x + (double)velocity.z * velocity.z) +
                                  (double)velocity.y * velocity.y;
            if (!(speedSquared > kFastSquared))
                first = 8;
        } else {
            first = 8;
        }
        if (GetOrientToGround() > kUpright) {
            end = kUprightLeverEnd;
        } else {
            first = 4;
            end = info->leverCount;
        }
    } else {
        first = 0;
        end = info->leverCount;
    }

    TempGetHeightInformation(false, &position, &info->ground, info->worldPosition);
    for (int i = first; i < end; i++) {
        const Coord4 *lever = &info->worldLevers[i];
        if (!(info->ground.w <= 0.0f) || !(lever->y > 0.0f)) {
            Coord3 point;
            point.x = position.x + lever->x;
            point.y = lever->y + position.y;
            point.z = lever->z + position.z;
            TempGetHeightInformation(false, &point, &info->leverGround[i], info->leverPositions[i]);
            float depth = info->leverGround[i].w;
            if (depth > 0.0f) {
                if (depth > deepest) {
                    deepest = depth;
                    deepestLever = i;
                }
                *contactLevers++ = *lever;
                *contactGrounds++ = info->leverGround[i];
                contacts++;
            }
        }
    }

    if (contacts != 0) {
        groundContacts = contacts;
        LeverContacts leverContacts;
        leverContacts.unknown00 = 0.0f;     // not read (the original leaves it unset)
        leverContacts.friction = info->groundFriction;
        leverContacts.unknown08 = info->unknown4d0;
        leverContacts.count = contacts;
        RigidScratchPadFields *scratch = RigidScratchPad;
        ResolveLeverForces(&leverContacts, scratch->contactLevers, scratch->contactGrounds,
                           &info->worldInverseInertia);
        if (deepest > 0.0f) {
            const Coord4 &ground = info->leverGround[deepestLever];
            position.x = (float)((double)deepest * ground.x + position.x);
            position.y = (float)((double)deepest * ground.y + position.y);
            position.z = (float)((double)deepest * ground.z + position.z);
        }
    }
    info->ground.w = position.y + info->ground.w;
}

// FUNC_AT(0x000ae830)
void RigidBody::ModifyLevers(RigidBody *a, RigidBody *b, Coord4 *leverA, Coord4 *leverB, const Coord4 *point,
                             Coord4 *direction, bool modify) {
    if (!modify) {
        bool aIsVehicle = false;
        if (a->kind < 4) {
            leverA->y = 0.0f;
            aIsVehicle = true;
        }
        if (b->kind < 4)
            leverB->y = 0.0f;
        else if (!aIsVehicle)
            return;
        VU0_v4sub(point, &a->position, direction);
        VU0_v4unitxyz(direction, direction);
        return;
    }

    // the levers into each body's frame
    MATRIX4 transposed;
    VU0_MATRIX4_transpose(&transposed, &a->info->orientation);
    VU0_MATRIX4_vect3mult(leverA, &transposed, leverA);
    VU0_MATRIX4_transpose(&transposed, &b->info->orientation);
    VU0_MATRIX4_vect3mult(leverB, &transposed, leverB);

    float alignment = v3dotprod(&a->info->orientation.mtx[2], &b->info->orientation.mtx[2]);
    if (alignment < 0.0f)
        alignment = -alignment;
    Coord4 offset;
    VU0_v4sub(&a->position, &b->position, &offset);
    float along = v3dotprod(&offset, &a->info->orientation.mtx[2]);
    if (along < 0.0f)
        along = -along;
    float shift = kCogShift;
    if (alignment > kAligned && along > kAligned) {
        shift = kAlignedCogShift;
        if (leverA->y > 0.0f)
            leverA->y = -leverA->y;
        if (leverB->y > 0.0f)
            leverB->y = -leverB->y;
    }
    double scaleA = (double)(a->kind == 1 ? Rigid_CAR_COLLIDE_COG_SCALE : Rigid_UnknownCogScale) + shift;
    if (scaleA < 0.0)
        scaleA = 0.0;
    double scaleB = (double)(b->kind == 1 ? Rigid_CAR_COLLIDE_COG_SCALE : Rigid_UnknownCogScale) + shift;
    if (scaleB < 0.0)
        scaleB = 0.0;
    leverA->y = (float)(scaleA * leverA->y);
    leverB->y = (float)(scaleB * leverB->y);

    // and back
    VU0_MATRIX4_vect3mult(leverA, &a->info->orientation, leverA);
    VU0_MATRIX4_vect3mult(leverB, &b->info->orientation, leverB);
}

// FUNC_AT(0x000aea40)
void RigidBody::ScaleObjObjForces(RigidBody *a, RigidBody *b, Coord4 *forceA, Coord4 *forceB, float limit,
                                  float scale, float boost) {
    float speedSquaredA = VU0_v3lengthsquare(&a->velocity);
    float speedSquaredB = VU0_v3lengthsquare(&b->velocity);
    float speedSquared = speedSquaredA > speedSquaredB ? speedSquaredA : speedSquaredB;
    if (!(speedSquared > limit))
        return;

    // the velocities the forces would leave, limited to `scale` times the faster one's before
    Coord4 velocityA, velocityB;
    VU0_v4sub(&a->momentum, forceA, &velocityA);
    VU0_v3add(&b->momentum, forceB, &velocityB);
    VU0_v4scale(&velocityA, 1.0f / a->mass, &velocityA);
    VU0_v4scale(&velocityB, 1.0f / b->mass, &velocityB);
    float afterA = VU0_v3lengthsquare(&velocityA);
    float afterB = VU0_v3lengthsquare(&velocityB);
    if (afterA > limit) {
        double allowed = (double)speedSquared * scale;
        if (afterA > allowed)
            VU0_v4scale(forceA, (float)(allowed / afterA), forceA);
    }
    if (afterB > limit) {
        double allowed = (double)speedSquared * scale;
        if (afterB > allowed)
            VU0_v4scale(forceB, (float)(allowed / afterB), forceB);
    }

    if (boost > kBoostBase) {
        double excess = (double)boost - kBoostBase;
        float boosted = (float)((excess + excess) * kBoostScale + kBoostScale);
        if (a->info->unknown4fd) {
            VU0_v4scale(forceB, boosted, forceB);
            VU0_v4scale(forceA, kBoostedShare, forceA);
        }
        if (b->info->unknown4fd) {
            VU0_v4scale(forceA, boosted, forceA);
            VU0_v4scale(forceB, kBoostedShare, forceB);
        }
    }
}

// FUNC_AT(0x000b09d0)
void RigidBody::ControlSleep() {
    Coord3 linear = velocity;
    Coord3 angular = angularVelocity;
    double linearX = linear.x < 0.0f ? -linear.x : linear.x;
    double linearY = linear.y < 0.0f ? -linear.y : linear.y;
    double linearZ = linear.z < 0.0f ? -linear.z : linear.z;
    double linearSpeed = linearX + (linearZ + linearY);
    double angularX = angular.x < 0.0f ? -angular.x : angular.x;
    double angularY = angular.y < 0.0f ? -angular.y : angular.y;
    double angularZ = angular.z < 0.0f ? -angular.z : angular.z;
    float angularSpeed = (float)((angularZ + angularY) + angularX);

    float sleepSpeed;
    if (kind < 4) {
        if (info->orientation.mtx[1][1] > kTiltedUp)
            return;
        sleepSpeed = Rigid_SLEEP_VEL * kTiltedSleepScale;
    } else {
        sleepSpeed = Rigid_SLEEP_VEL;
    }
    if (linearSpeed < sleepSpeed && angularSpeed < sleepSpeed && (int8_t)groundContacts > 2)
        ForceToSleep();
}

// FUNC_AT(0x000b0ec0)
void RigidBody::UpdatePositionAndOrientation(float unused) {
    (void)unused;
    momentum.x = Util_Bound(momentum.x, Rigid_M_LIMIT * mass, Rigid_M_LIMIT * mass);
    momentum.y = Util_Bound(momentum.y, Rigid_M_LIMIT * mass, Rigid_M_LIMIT * mass);
    momentum.z = Util_Bound(momentum.z, Rigid_M_LIMIT * mass, Rigid_M_LIMIT * mass);
    float step = SimTimeStep;
    float inverseMass = 1.0f / mass;
    VU0_v4scaleadd(&force, step, &momentum, &momentum);
    VU0_v4scaleadd(&torque, step, &angularMomentum, &angularMomentum);
    VU0_v4scale(&momentum, inverseMass, &velocity);
    VU0_v4scaleadd(&velocity, step, &position, &position);

    // the angular velocity, bounded, and the angular momentum back from it
    MATRIX4 transposed;
    VU0_MATRIX4_transpose(&transposed, &info->orientation);
    VU0_MATRIX4_vect3rotate(&angularMomentum, &info->worldInverseInertia, &angularVelocity);
    angularVelocity.x = Util_Bound(angularVelocity.x, Rigid_AM_LIMIT, Rigid_AM_LIMIT);
    angularVelocity.y = Util_Bound(angularVelocity.y, Rigid_AM_LIMIT, Rigid_AM_LIMIT);
    angularVelocity.z = Util_Bound(angularVelocity.z, Rigid_AM_LIMIT, Rigid_AM_LIMIT);
    VU0_MATRIX4_vect3rotate(&angularVelocity, &transposed, &angularMomentum);
    angularMomentum.x = angularMomentum.x / inverseInertiaX;
    angularMomentum.y = angularMomentum.y / inverseInertiaY;
    angularMomentum.z = angularMomentum.z / inverseInertiaZ;
    VU0_MATRIX4_vect3rotate(&angularMomentum, &info->orientation, &angularMomentum);

    // the quaternion's derivative, (0, w) q / 2, over the step; then normalised
    Coord3 w = angularVelocity;
    Coord4 q = orientation;
    double a = (double)w.x * step;
    double b = (double)w.y * step;
    double c = (double)w.z * step;
    Coord4 change;
    change.x = (float)(((q.z * b - q.y * c) + q.w * a) * 0.5);
    change.y = (float)(((q.x * c - q.z * a) + q.w * b) * 0.5);
    change.z = (float)(((q.y * a - q.x * b) + q.w * c) * 0.5);
    change.w = (float)(((-(q.x * a) - q.y * b) - q.z * c) * 0.5);
    VU0_v4add4(&orientation, &change, &orientation);
    double lengthSquared = (((double)orientation.x * orientation.x + (double)orientation.y * orientation.y) +
                            (double)orientation.z * orientation.z) + (double)orientation.w * orientation.w;
    VU0_v4scale4(&orientation, 1.0f / VU0_sqrt((float)lengthSquared), &orientation);

    MATRIX4 scale, rotation, scaled;
    BuildScale(&scale, inverseInertiaX, inverseInertiaY, inverseInertiaZ);
    VU0_quattom4(&rotation, &orientation);
    VU0_MATRIX4_transpose(&transposed, &rotation);
    VU0_MATRIX4_mult(&scaled, &scale, &rotation);
    VU0_MATRIX4_mult(&info->worldInverseInertia, &transposed, &scaled);
    info->orientation = rotation;
    for (int i = 0; i < info->leverCount; i++)
        VU0_MATRIX4_vect3rotate(&info->levers[i], &rotation, &info->worldLevers[i]);

    // the box's corners, into the other half of the corner list
    info->cornerBank ^= 1;
    Coord4 *corner = info->corners[(int8_t)info->cornerBank];
    float x = info->halfExtents.x;
    float y = info->halfExtents.y;
    float z = info->halfExtents.z;
    if (kind < 4)
        x = x + kVehicleHalfWidening;
    Coord4 local = {-x, -y, -z, 1.0f};
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[0]);
    local.x = x;
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[1]);
    local.z = z;
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[2]);
    local.x = -x;
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[3]);
    local.y = y;
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[4]);
    local.x = x;
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[5]);
    local.z = -z;
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[6]);
    local.x = -x;
    VU0_MATRIX4_vect3rotate(&local, &info->orientation, &corner[7]);

    ControlSleep();
    UpdateZonalInfo();
}

// FUNC_AT(0x000b2810)
void RigidBody::ResetRigidBodySPThunk() {
    ResetRigidBodySP();
}

// FUNC_AT(0x000b13a0)
void VectorXlen() {
    RIGIDCORE_UNTESTED("VectorXlen");
    GameString message;
    message.capacity = 15;
    message.size = 0;
    message.buffer[0] = '\0';
    const char *text = "vector<T> too long";
    String_Assign(&message, 0, text, (uint32_t)strlen(text));
    GameLengthError error;
    LogicError_Construct(&error, 0, &message);
    error.vtable = LengthErrorVtable;
    Crt_CxxThrowException(&error, LengthErrorThrowInfo);
}
