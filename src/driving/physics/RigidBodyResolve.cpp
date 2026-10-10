#include "RigidBodyResolve.h"

#include <bit>

#include "PhysicsMath.h"
#include "PhysicsObject.h"
#include "RigidBodyBasics.h"             // ClampedForceRatio
#include "../engine/MissionManager.h"
#include "../engine/PhysicsUtil.h"
#include "../platform/RealMath.h"

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// RigidBody's collision response. Bodies of kind below 4 belong to vehicles: their owners' PVehicle methods are
// called (through the owner's vtable).
// ---------------------------------------------------------------------------------------------------------------

namespace {

PhysicsObject *Owner(const RigidBody *body) {
    return PhysicsObjects[body->ownerIndex];
}

PVehicle *Vehicle(const RigidBody *body) {
    return RigidVehicles[body->ownerIndex];
}

bool OwnerHasHitPoints(const RigidBody *body) {
    return Owner(body)->GetHitPoints() > 0.0;
}

constexpr float kShareLimit = 0.66f;
static_assert(std::bit_cast<uint32_t>(kShareLimit) == 0x3f28f5c3, "0.66");
constexpr float kFlattenTurn = 0.707f;
static_assert(std::bit_cast<uint32_t>(kFlattenTurn) == 0x3f34fdf4, "0.707");

}  // namespace

// ---- two bodies

// `this` is only consulted for its own car class; the caller passes a.
// FUNC_AT(0x000aec10)
bool RigidBody::ResolveCollision(RigidBody *a, RigidBody *b, Coord4 *normal, const Coord4 *point, float depth,
                                 CollisionImpact *impact) {
    bool wakeA = false;
    bool wakeB = false;
    Coord4 leverA = {}, leverB = {};
    VU0_v4sub(point, &a->position, &leverA);
    VU0_v4sub(point, &b->position, &leverB);
    const bool bothVehicles = a->kind < 4 && b->kind < 4;
    const bool neitherVehicle = a->kind >= 4 && b->kind >= 4;
    ModifyLevers(a, b, &leverA, &leverB, point, normal, bothVehicles);

    Coord4 velocityA = {}, velocityB = {}, relative = {};
    VU0_v4crossprodxyz(&a->angularVelocity, &leverA, &velocityA);
    VU0_v3add(&velocityA, &a->velocity, &velocityA);
    VU0_v4crossprodxyz(&b->angularVelocity, &leverB, &velocityB);
    VU0_v3add(&velocityB, &b->velocity, &velocityB);
    VU0_v4sub(&velocityB, &velocityA, &relative);
    const float closingSpeed = v3dotprod(normal, &relative);
    if (closingSpeed > 10.0f)
        return false;
    if (a->info->unknown4fc || b->info->unknown4fc)
        return true;

    impact->damageScaleB = 1.0f;
    impact->damageScaleA = 1.0f;
    VU0_v4sub(&b->velocity, &a->velocity, &relative);
    const float speed = bothVehicles ? Abs(v3dotprod(normal, &relative)) : VU0_v3length(&relative);
    const float scaledSpeed = Rigid_SPEED_SCALE * speed;

    // The force: the speed's or the depth's, at least 1, squared, under a cap from the faster body's speed
    double depthForce = Rigid_DEPTH_FORCE_SCALE;
    if (neitherVehicle)
        depthForce -= 1.0;
    depthForce *= depth;
    const double force = scaledSpeed > depthForce ? scaledSpeed : depthForce;
    float clampedForce = float(force);
    if (!(force > 1.0))
        clampedForce = 1.0f;
    const float speedSquaredA = VU0_v3lengthsquare(&a->velocity);
    const float speedSquaredB = VU0_v3lengthsquare(&b->velocity);
    double cap = speedSquaredA > speedSquaredB ? speedSquaredA : speedSquaredB;
    cap = cap * Rigid_TOTAL_FORCE_LIMIT / Rigid_ForceCapDivisor;
    if (!(cap > Rigid_ForceCapFloor))
        cap = Rigid_ForceCapFloor;
    if (!(cap < Rigid_TOTAL_FORCE_LIMIT))
        cap = Rigid_TOTAL_FORCE_LIMIT;
    if (neitherVehicle)
        cap *= 0.4f;
    const double forceSquared = double(clampedForce) * clampedForce;
    const float magnitude = forceSquared < cap ? float(forceSquared) : float(cap);

    Coord4 impulse = {};
    VU0_v4scale(normal, magnitude, &impulse);
    const float totalMass = b->mass + a->mass;
    Coord4 impulseA = {}, impulseB = {};    // taken from a, given to b

    if (!bothVehicles) {
        float scaleA, scaleB;
        if (a->kind < 4) {
            impact->damageScaleA = 0.0f;
            scaleA = 0.1f;
            leverA.x = leverA.y = leverA.z = 0.0f;
        } else {
            impact->damageScaleA = 1.0f;
            scaleA = 1.0f;
        }
        if (b->kind < 4) {
            impact->damageScaleB = 0.0f;
            scaleB = 0.1f;
            leverB.x = leverB.y = leverB.z = 0.0f;
        } else {
            impact->damageScaleB = 1.0f;
            scaleB = 1.0f;
        }
        float effectiveMass;
        if (a->mass > 1500.0f && b->mass > 1500.0f)
            effectiveMass = b->mass + a->mass;
        else
            effectiveMass = Min(a->mass, b->mass) * 0.75f;

        bool share = true;
        if (((a->kind == 2 || a->kind == 3) && OwnerHasHitPoints(a)) ||
            ((b->kind == 2 || b->kind == 3) && OwnerHasHitPoints(b)) ||
            ((kind == 2 || kind == 3) && Vehicle(this)->GetCarClass() == 1)) {
            a->flags &= ~kFlag1;
            b->flags &= ~kFlag1;
        } else {
            // A body flagged kFlag1, at rest and struck slowly by one no heavier, does not move: the other takes
            // the whole impulse
            if ((a->flags & kFlag1) && speedSquaredA == 0.0f && speedSquaredB < 49.0f && b->mass >= a->mass) {
                VU0_v4scale(&impulse, 0.0f, &impulseA);
                VU0_v4scale(&impulse, Max(scaledSpeed, 1.0f) * b->mass, &impulseB);
                impact->damageScaleA = 0.0f;
                share = false;
                wakeB = true;
            } else {
                a->flags &= ~kFlag1;
                Owner(a)->PlayAnimation(12);
            }
            if ((b->flags & kFlag1) && speedSquaredB == 0.0f && speedSquaredA < 49.0f && a->mass >= b->mass) {
                VU0_v4scale(&impulse, Max(scaledSpeed, 1.0f) * a->mass, &impulseA);
                VU0_v4scale(&impulse, 0.0f, &impulseB);
                impact->damageScaleB = 0.0f;
                share = false;
                wakeA = true;
            } else {
                b->flags &= ~kFlag1;
                Owner(b)->PlayAnimation(12);
            }
        }
        if (share) {
            // Each body's share: the other's part of the total mass, scaled and capped
            if (neitherVehicle)
                effectiveMass *= 0.5f;
            const double inverseTotal = 1.0 / totalMass;
            const float roundedInverseTotal = float(inverseTotal);
            const float shareB = Min(float(inverseTotal * a->mass * scaleB), kShareLimit) * effectiveMass;
            const float shareA = Min(float(double(roundedInverseTotal) * b->mass * scaleA), kShareLimit) *
                                 effectiveMass;
            VU0_v4scale(&impulse, shareA, &impulseA);
            VU0_v4scale(&impulse, shareB, &impulseB);
            wakeB = true;
            wakeA = true;
        }

        // A vehicle's impulse has no vertical part; an object slower than 15 takes one bounded by ten times its
        // mass, its vertical part made positive, a faster one none
        if (a->kind < 4) {
            impulseA.y = 0.0f;
        } else if (VU0_v3length(&a->velocity) < 15.0f) {
            const float upper = a->mass * 10.0f;
            const float lower = a->mass * -10.0f;
            impulseA.x = Min(Max(impulseA.x, lower), upper);
            impulseA.y = Min(Abs(impulseA.y), upper);
            impulseA.z = Min(Max(impulseA.z, lower), upper);
        } else {
            impulseA.x = impulseA.y = impulseA.z = 0.0f;
        }
        if (b->kind < 4) {
            impulseB.y = 0.0f;
        } else if (VU0_v3length(&b->velocity) < 15.0f) {
            const float upper = b->mass * 10.0f;
            const float lower = b->mass * -10.0f;
            impulseB.x = Min(Max(impulseB.x, lower), upper);
            impulseB.y = Min(Abs(impulseB.y), upper);
            impulseB.z = Min(Max(impulseB.z, lower), upper);
        } else {
            impulseB.x = impulseB.y = impulseB.z = 0.0f;
        }

        Coord4 &angular = relative;
        VU0_v4crossprodxyz(&leverA, &impulseA, &angular);
        if (a->kind >= 4)
            VU0_v4scale(&angular, 0.25f, &angular);
        VU0_v4sub(&a->angularMomentum, &angular, &a->angularMomentum);
        VU0_v4crossprodxyz(&leverB, &impulseB, &angular);
        if (b->kind >= 4)
            VU0_v4scale(&angular, 0.25f, &angular);
        VU0_v3add(&b->angularMomentum, &angular, &b->angularMomentum);
    } else {
        const float effectiveMass = totalMass < 3000.0f ? totalMass : 3000.0f;
        a->info->unknown4de = 0;
        b->info->unknown4de = 0;
        wakeB = true;
        wakeA = true;

        // With the mission manager's flag, a car of class 1 or 2 with hit points against one that is not: weights
        // of a quarter and four
        float weightA = 1.0f;
        float weightB = 1.0f;
        if (glbMissionManager->unknown478) {
            bool classedA = Vehicle(a)->GetCarClass() == 1 || Vehicle(a)->GetCarClass() == 2;
            bool classedB = Vehicle(b)->GetCarClass() == 1 || Vehicle(b)->GetCarClass() == 2;
            if (Owner(a)->GetHitPoints() <= 0.0)
                classedA = false;
            if (Owner(b)->GetHitPoints() <= 0.0)
                classedB = false;
            if (classedA && !classedB) {
                weightA = 0.25f;
                weightB = 4.0f;
            } else if (!classedA && classedB) {
                weightA = 4.0f;
                weightB = 0.25f;
            }
        }

        // A car on the ground against one off it: the one off the ground takes the larger share, and if its
        // momentum is downwards, that is halved and the other's impulse keeps a tenth of its vertical part
        // (groundContacts is tested signed)
        const bool groundedA = Vehicle(a)->GetNumWheelsOnGround() != 0 || int8_t(a->groundContacts) > 0;
        const bool groundedB = Vehicle(b)->GetNumWheelsOnGround() != 0 || int8_t(b->groundContacts) > 0;
        bool fallingA = false;
        bool fallingB = false;
        float shareWeightA = weightA;
        if (groundedA && !groundedB) {
            shareWeightA = 0.2f;
            weightB = 1.0f;
            if (b->momentum.y < 0.0f) {
                fallingB = true;
                b->momentum.y *= 0.5f;
            }
        } else if (!groundedA && groundedB) {
            shareWeightA = 1.0f;
            weightB = 0.2f;
            if (a->momentum.y < 0.0f) {
                fallingA = true;
                a->momentum.y *= 0.5f;
            }
        }
        const float inverseTotal = 1.0f / totalMass;
        VU0_v4scale(&impulse, float(double(inverseTotal) * b->mass * shareWeightA * effectiveMass), &impulseA);
        VU0_v4scale(&impulse, float(double(inverseTotal) * a->mass * weightB * effectiveMass), &impulseB);
        if (fallingA)
            impulseB.y *= 0.1f;
        if (fallingB)
            impulseA.y *= 0.1f;
        ScaleObjObjForces(a, b, &impulseA, &impulseB, 225.0f, 1.05f, depth);

        if (fallingA) {
            if (a->velocity.y > 10.0f)
                impulseA.x = impulseA.y = impulseA.z = 0.0f;
            const double upper = double(a->mass) * 10.0f;
            if (impulseA.y > upper)
                impulseA.y = float(upper);
        }
        if (fallingB) {
            if (b->velocity.y > 10.0f)
                impulseB.x = impulseB.y = impulseB.z = 0.0f;
            const double upper = double(b->mass) * 10.0f;
            if (impulseB.y > upper)
                impulseB.y = float(upper);
        }

        Coord4 &angular = relative;
        VU0_v4crossprodxyz(&leverA, &impulseA, &angular);
        angular.y *= a->kind == 1 ? 0.16f : 0.5f;
        if (!fallingA)
            VU0_v4sub(&a->angularMomentum, &angular, &a->angularMomentum);
        VU0_v4crossprodxyz(&leverB, &impulseB, &angular);
        angular.y *= b->kind == 1 ? 0.16f : 0.5f;
        if (!fallingB)
            VU0_v3add(&b->angularMomentum, &angular, &b->angularMomentum);
        impulseA.y = 0.0f;
        impulseB.y = 0.0f;
    }

    VU0_v4sub(&a->momentum, &impulseA, &a->momentum);
    VU0_v3add(&b->momentum, &impulseB, &b->momentum);
    VU0_v4scale(&a->momentum, 1.0f / a->mass, &a->velocity);
    VU0_MATRIX4_vect3rotate(&a->angularMomentum, &a->info->worldInverseInertia, &a->angularVelocity);
    VU0_v4scale(&b->momentum, 1.0f / b->mass, &b->velocity);
    VU0_MATRIX4_vect3rotate(&b->angularMomentum, &b->info->worldInverseInertia, &b->angularVelocity);

    impact->normal = *normal;
    impact->point = *point;
    impact->closingSpeed = closingSpeed;
    impact->strength = float(ClampedForceRatio(magnitude, Rigid_TOTAL_FORCE_LIMIT, 1.0f));
    if (wakeA) {
        a->sleepState = kAwake;
        a->info->unknown4fe = 1;
        a->flags |= kFlag0;
    }
    if (wakeB) {
        b->sleepState = kAwake;
        b->info->unknown4fe = 1;
        b->flags |= kFlag0;
    }
    return true;
}

// ---- one body against the world

// CollideWithWorld passes fromWorld true; the events that push a body (EKickObject, EBashCarAlongObjectAxis, the
// nuclear sequence) false.
// FUNC_AT(0x000afdb0)
void RigidBody::GenerateImpulse(CollisionImpact *impact, const Coord4 *normal, const Coord4 *point, float push,
                                bool fromWorld, uint16_t tag, float speedScale) {
    const MATRIX4 *inverseInertia = &info->worldInverseInertia;
    info->unknown4fd = 1;
    Coord4 lever = {}, pointVelocity = {};
    VU0_v4sub(point, &position, &lever);
    VU0_v4crossprodxyz(&angularVelocity, &lever, &pointVelocity);
    VU0_v3add(&pointVelocity, &velocity, &pointVelocity);
    const float normalSpeed = v3dotprod(&pointVelocity, normal) * speedScale;
    if (fromWorld && normalSpeed > 0.0f) {
        // Already moving away
        impact->closingSpeed = normalSpeed;
        impact->normal = *normal;
        impact->relativeVelocity = pointVelocity;
        impact->strength = 0.0f;
        impact->kindA = 1;
        impact->unknown4a = 0x10;
        impact->ownerA = ownerIndex;
        impact->kindB = 6;
        impact->tag = tag;
        VU0_v4copy(point, impact);
        return;
    }

    // The impulse along the normal: restitution growing with the speed up to BUILDING_MICRO_THRESHOLD, against
    // the push, over the effective inverse mass at the point
    Coord4 axis = {}, turn = {};
    VU0_v4crossprodxyz(&lever, normal, &axis);
    VU0_MATRIX4_vect3rotate(&axis, inverseInertia, &axis);
    VU0_v4crossprodxyz(&axis, &lever, &turn);
    double across = double(lever.z) * normal->z + double(lever.x) * normal->x;
    if (across < 0.0)
        across = -across;
    const float lateral = float(double(lever.z) * lever.z + double(lever.x) * lever.x - across);
    float leverTerm = 0.0f;
    if (lateral > 0.0f)
        leverTerm = VU0_sqrt(lateral) / info->halfExtents.x;
    const double denominator = double(v3dotprod(&turn, normal)) + leverTerm;
    const double pushSpeed = double(Rigid_PF_SCALE) * push;
    float impulse = 0.0f;
    if (denominator != 0.0) {
        const float speed = Abs(normalSpeed);
        const double restitution = RigidBuildingRestitutionRate *
                                   double(speed > Rigid_BUILDING_MICRO_THRESHOLD ? Rigid_BUILDING_MICRO_THRESHOLD
                                                                                 : speed) + 1.0;
        impulse = float(-(restitution * (normalSpeed - pushSpeed)) / denominator);
    }
    float strength = impulse;
    if (!(impulse < Rigid_BUILDING_FORCE_LIMIT))
        impulse = Rigid_BUILDING_FORCE_LIMIT;
    if (!(impulse > -Rigid_BUILDING_FORCE_LIMIT))
        impulse = -Rigid_BUILDING_FORCE_LIMIT;

    // Against the world, less the friction along the velocity at the point
    Coord4 friction = {};
    VU0_v4scale(&pointVelocity, Rigid_BUILDING_FRICTION_FACTOR, &friction);
    friction.x = Util_Bound(friction.x, Rigid_BUILDING_FRICTION_LIMIT, Rigid_BUILDING_FRICTION_LIMIT);
    friction.y = Util_Bound(friction.y, Rigid_BUILDING_FRICTION_LIMIT, Rigid_BUILDING_FRICTION_LIMIT);
    friction.z = Util_Bound(friction.z, Rigid_BUILDING_FRICTION_LIMIT, Rigid_BUILDING_FRICTION_LIMIT);
    Coord4 linear = {};
    VU0_v4scale(normal, impulse, &linear);
    if (fromWorld)
        VU0_v4sub(&linear, &friction, &linear);
    VU0_v4scale(&linear, mass, &linear);

    // A kind 1 car reversing, its steering beyond 0.75, the point on the negative side of its z axis
    // and the normal well off it: no vertical impulse, and a small turn
    PVehicle *owner = Vehicle(this);
    bool flatten = false;
    if (fromWorld && kind == 1 && owner->GetPhysics()->subPhysics == 0 && owner->GetCarClass() != 1 &&
        owner->IsReversing()) {
        BondCarControl control;
        if (Abs(owner->GetCarControl(&control)->steering) > 0.75f &&
            v3dotprod(&lever, info->orientation.mtx[2]) < 0.0f &&
            Abs(v3dotprod(normal, info->orientation.mtx[2])) < kFlattenTurn) {
            linear.y = 0.0f;
            lever.y = 0.0f;
            flatten = true;
        }
    }

    // An object hitting the world never comes away faster than it went in
    if (kind >= 4 && fromWorld) {
        const float speedSquared = VU0_v3lengthsquare(&velocity);
        if (speedSquared > 0.0f) {
            Coord4 after = {};
            VU0_v3add(&momentum, &linear, &after);
            VU0_v4scale(&after, 1.0f / mass, &after);
            const float afterSquared = VU0_v3lengthsquare(&after);
            if (afterSquared > speedSquared)
                VU0_v4scale(&linear, speedSquared / afterSquared, &linear);
        }
    }
    VU0_v3add(&momentum, &linear, &momentum);

    Coord4 angular = {};
    VU0_v4crossprodxyz(&lever, &linear, &angular);
    if (fromWorld && kind < 4 && owner->GetPhysics()->subPhysics == 0) {
        ConvertWorldToLocal(&angular);
        angular.z *= 0.2f;
    }
    if (flatten) {
        angular.y *= 0.425f;
        strength *= 0.001f;
    }
    if (kind >= 4) {
        VU0_v4scale(&angular, 0.1f, &angular);
    } else {
        if (fromWorld && owner->GetPhysics()->subPhysics == 0) {
            angular.y *= 0.2f;
            VU0_MATRIX4_vect3mult(&angular, &info->orientation, &angular);
        }
        if (owner->GetPhysics()->subPhysics == 1) {
            MATRIX4 worldToBody;
            VU0_MATRIX4_transpose(&worldToBody, &info->orientation);
            VU0_MATRIX4_vect3mult(&angular, &worldToBody, &angular);
            angular.z *= 0.15f;
            VU0_MATRIX4_vect3mult(&angular, &info->orientation, &angular);
        }
    }
    VU0_v3add(&angularMomentum, &angular, &angularMomentum);
    VU0_v4scale(&momentum, 1.0f / mass, &velocity);
    VU0_MATRIX4_vect3rotate(&angularMomentum, inverseInertia, &angularVelocity);

    impact->closingSpeed = normalSpeed;
    impact->normal = *normal;
    impact->relativeVelocity = pointVelocity;
    // ClampedForceRatio (0x000ad650), inlined
    const double scaledStrength = Abs(strength) * 0.5 / Rigid_BUILDING_FORCE_LIMIT;
    impact->strength = scaledStrength < 1.0 ? float(scaledStrength) : 1.0f;
    impact->kindA = 1;
    impact->unknown4a = 0x10;
    impact->ownerA = ownerIndex;
    impact->kindB = 6;
    impact->tag = tag;
    VU0_v4copy(point, impact);
}
