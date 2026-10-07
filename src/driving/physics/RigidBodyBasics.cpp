#include "RigidBodyBasics.h"

#include "../../helpers.h"
#include "../data/DebugVariables.h"   // dbattrib_float
#include "../data/Tuning.h"           // TuningDBMgr
#include "../engine/GameLoop.h"       // LaunchPage
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../world/CollisionManager.h"
#include "../world/WorldPos.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// RigidBody's small methods (0x000acde0-0x000adc80), ported from the listing. The x87 chains are computed in
// double in the original's order and rounded where it stores a float; comparisons fail on NaN where the original's
// jumps do.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's, not ported yet

#define Simulation_GetScratchPadFreeZone ((RigidScratchPadFields *(*)(void))0x000b2820)
#define WWorldPos_FaceNormal ((void (__fastcall *)(const WWorldPos *, int, Coord4 *normal))0x0005d3f0)

// ---- globals

#define Launch (*(LaunchPage *)0x00243b90)

namespace {

constexpr float kZoneScale = 0.2f;                  // UpdateZonalInfo: a zone is 5 units
constexpr float kVehicleDamageThreshold = 0.2f;     // 0x00193280: kind below 4
constexpr float kDamageThreshold = 0.05f;           // 0x00193278
constexpr uint32_t kHeightSearchMask = 0x20;        // TempGetHeightInformation's barrier mask while it searches,
constexpr uint32_t kHeightRestoreMask = 0x10;       // and the mask it leaves
constexpr float kProbeStep = 0.1f;                  // its probes' step along x
constexpr float kVehicleHeightLift = 10.0f;
constexpr float kNoGeometryUnknown60 = 1.5f;
constexpr float kGroundFrictionMassScale = 8.0f;    // 0x001c36d0

constexpr int8_t kVehicleKinds = 4;                 // a body of kind below 4 belongs to a vehicle
constexpr int8_t kKind1 = 1;
constexpr int32_t kOwnerTypeCar = 1;                // PhysicsObject::type with wheels (CAR_WHEEL_* offsets)
constexpr int kWheels = 4;

}  // namespace

// ---- the system

// FUNC_AT(0x000acde0)
void RigidBody::InitRigidBodySystem() {
    RigidScratchPad = Simulation_GetScratchPadFreeZone();
    TuningDBMgr->LoadDatabase("Physics:Rigid", Launch.missionName, 0, false);
    dbattrib_float("GRAVITY", &Rigid_GRAVITY, -100.0f, -0.001f, 4, 1.0f, NULL);
    dbattrib_float("BODGE_PLAYER_GRAVITY", &Rigid_BODGE_PLAYER_GRAVITY, -100.0f, -0.001f, 4, 1.0f, NULL);
    dbattrib_float("SLEEP_VEL", &Rigid_SLEEP_VEL, 0.0f, 5.0f, 4, 1.0f, NULL);
    dbattrib_float("GROUND_FRICTION_COEFF", &Rigid_GROUND_FRICTION_COEFF, 0.0f, 0.05f, 4, 1.0f, NULL);
    dbattrib_float("MICRO_THRESHOLD", &Rigid_MICRO_THRESHOLD, 0.0f, 50.0f, 4, 1.0f, NULL);
    dbattrib_float("GROUND_RESTITUTION", &Rigid_GROUND_RESTITUTION, 0.0f, 5.0f, 4, 1.0f, NULL);
    dbattrib_float("NATURAL_ANGULAR_DAMPING", &Rigid_NATURAL_ANGULAR_DAMPING, 0.98f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("M_LIMIT", &Rigid_M_LIMIT, 0.0f, 200.0f, 4, 1.0f, NULL);
    dbattrib_float("AM_LIMIT", &Rigid_AM_LIMIT, 0.0f, 200.0f, 4, 1.0f, NULL);
    dbattrib_float("CAR_COLLIDE_COG_SCALE", &Rigid_CAR_COLLIDE_COG_SCALE, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("DEPTH_FORCE_SCALE", &Rigid_DEPTH_FORCE_SCALE, 0.0f, 20.0f, 4, 1.0f, NULL);
    dbattrib_float("SPEED_SCALE", &Rigid_SPEED_SCALE, 0.0f, 8.0f, 4, 1.0f, NULL);
    dbattrib_float("SPEED_FORCE_LIMIT", &Rigid_SPEED_FORCE_LIMIT, 0.0f, 40.0f, 4, 1.0f, NULL);
    dbattrib_float("TOTAL_FORCE_LIMIT", &Rigid_TOTAL_FORCE_LIMIT, 0.0f, 50.0f, 4, 1.0f, NULL);
    dbattrib_float("BUILDING_MICRO_THRESHOLD", &Rigid_BUILDING_MICRO_THRESHOLD, 0.0f, 50.0f, 4, 1.0f, NULL);
    dbattrib_float("BUILDING_FRICTION_FACTOR", &Rigid_BUILDING_FRICTION_FACTOR, 0.0f, 0.5f, 4, 1.0f, NULL);
    dbattrib_float("BUILDING_COEFF_RESTITUTION", &Rigid_BUILDING_COEFF_RESTITUTION, 0.0f, 1.0f, 4, 1.0f, NULL);
    dbattrib_float("BUILDING_FRICTION_LIMIT", &Rigid_BUILDING_FRICTION_LIMIT, 0.0f, 10.0f, 4, 1.0f, NULL);
    dbattrib_float("BUILDING_FORCE_LIMIT", &Rigid_BUILDING_FORCE_LIMIT, 0.0f, 100.0f, 4, 1.0f, NULL);
    dbattrib_float("PF_SCALE", &Rigid_PF_SCALE, 0.0f, 8.0f, 4, 1.0f, NULL);
    dbattrib_float("BUILDING_COLLIDE_COG_SCALE", &Rigid_BUILDING_COLLIDE_COG_SCALE, 0.0f, 1.0f, 4, 1.0f, NULL);
    TuningDBMgr->CloseCurrent();
    RigidUnderwater = strstr(Launch.missionName, "uw_") != NULL;
}

// FUNC_AT(0x000ad0e0)
void RigidBody::ResetRigidBodySP() {
    RigidScratchPadFields *scratch = RigidScratchPad;
    scratch->unknown390 = 0;
    scratch->unknown399 = 0;
}

// ---- accessors

// FUNC_AT(0x000ad100)
PhysicsObject* RigidBody::GetOwner() {
    return PhysicsObjects[ownerIndex];
}

// The body's up axis against the world's: row 1's y, with x and z weighted by zero (a NaN or infinity there makes
// the answer NaN). Unrounded.
// FUNC_AT(0x000ad110)
double RigidBody::GetOrientToGround() {
    const float *up = info->orientation.mtx[1];
    return (double)up[2] * 0.0 + (double)up[0] * 0.0 + up[1];
}

// FUNC_AT(0x000ad130)
void RigidBody::ApplyAngularDamping() {
    if (groundContacts == 0 && kind >= kVehicleKinds) {
        angularMomentum.x = Rigid_NATURAL_ANGULAR_DAMPING * angularMomentum.x;
        angularMomentum.y = Rigid_NATURAL_ANGULAR_DAMPING * angularMomentum.y;
        angularMomentum.z = Rigid_NATURAL_ANGULAR_DAMPING * angularMomentum.z;
    }
}

// FUNC_AT(0x000ad170)
void RigidBody::ApplyHeavyFriction() {
    v3scale(1, &momentum, 0.0f, &momentum);
    v3scale(1, &angularMomentum, 0.0f, &angularMomentum);
}

// The delta is carried into the world's frame in place, then added.
// FUNC_AT(0x000ad1a0)
void RigidBody::ModifyAngularMomentum(Coord3 *localDelta) {
    VU0_MATRIX4_vect3mult(localDelta, &info->orientation, localDelta);
    VU0_v3add(&angularMomentum, localDelta, &angularMomentum);
}

// FUNC_AT(0x000ad1d0)
Coord3* RigidBody::GetLocalAngularMomentum(Coord3 *result) {
    MATRIX4 toLocal;
    Coord4 local;
    VU0_MATRIX4_transpose(&toLocal, &info->orientation);
    VU0_MATRIX4_vect3mult(&angularMomentum, &toLocal, &local);
    result->x = local.x;
    result->y = local.y;
    result->z = local.z;
    return result;
}

// FUNC_AT(0x000ad220)
Coord3* RigidBody::GetLocalVelocity(Coord3 *result) {
    MATRIX4 toLocal;
    Coord4 local;
    VU0_MATRIX4_transpose(&toLocal, &info->orientation);
    VU0_MATRIX4_vect3mult(&velocity, &toLocal, &local);
    result->x = local.x;
    result->y = local.y;
    result->z = local.z;
    return result;
}

// FUNC_AT(0x000ad270)
Coord3* RigidBody::GetLocalAngularVelocity(Coord3 *result) {
    MATRIX4 toLocal;
    Coord4 local;
    VU0_MATRIX4_transpose(&toLocal, &info->orientation);
    VU0_MATRIX4_vect3mult(&angularVelocity, &toLocal, &local);
    result->x = local.x;
    result->y = local.y;
    result->z = local.z;
    return result;
}

// FUNC_AT(0x000ad2c0)
void RigidBody::ConvertWorldToLocal(Coord4 *v) {
    MATRIX4 toLocal;
    VU0_MATRIX4_transpose(&toLocal, &info->orientation);
    VU0_MATRIX4_vect3mult(v, &toLocal, v);
}

// FUNC_AT(0x000ad2f0)
void RigidBody::ConvertLocalToWorld(Coord3 *v) {
    VU0_MATRIX4_vect3mult(v, &info->orientation, v);
}

// FUNC_AT(0x000ad310)
void RigidBody::SetAngularMomentum(const Coord3 *local) {
    VU0_MATRIX4_vect3mult(local, &info->orientation, &angularMomentum);
}

// The orientation, its matrix, and the inverse inertia tensor carried into the world's frame: R^T S R.
// FUNC_AT(0x000ad330)
void RigidBody::SetOrientation(const Coord4 *quaternion) {
    MATRIX4 rotation, transposed, inertia, scaled, worldInertia;
    orientation = *quaternion;
    VU0_quattom4(&rotation, &orientation);
    VU0_MATRIX4_transpose(&transposed, &rotation);
    BuildScale(&inertia, inverseInertiaX, inverseInertiaY, inverseInertiaZ);
    VU0_MATRIX4_mult(&scaled, &inertia, &rotation);
    VU0_MATRIX4_mult(&worldInertia, &transposed, &scaled);
    info->orientation = rotation;
    info->worldInverseInertia = worldInertia;
}

// ---- forces and torques: each wakes the body unless it is frozen

// FUNC_AT(0x000ad3f0)
void RigidBody::ResolveForce(const Coord3 *f) {
    if (sleepState == kFrozen)
        return;
    sleepState = kAwake;
    flags &= ~kFlag1;
    VU0_v3add(f, &force, &force);
}

// FUNC_AT(0x000ad420)
void RigidBody::ResolveTorque(const Coord3 *t) {
    if (sleepState == kFrozen)
        return;
    sleepState = kAwake;
    VU0_v3add(t, &torque, &torque);
}

// FUNC_AT(0x000ad440)
void RigidBody::ResolveMassScaledForce4(const Coord4 *acceleration) {
    if (sleepState == kFrozen)
        return;
    flags &= ~kFlag1;
    sleepState = kAwake;
    VU0_v4scaleadd(acceleration, mass, &force, &force);
}

// FUNC_AT(0x000ad470)
void RigidBody::ResolveMassScaledTorque4(const Coord4 *acceleration) {
    if (sleepState == kFrozen)
        return;
    sleepState = kAwake;
    VU0_v4scaleadd(acceleration, mass, &torque, &torque);
}

// FUNC_AT(0x000ad4a0)
void RigidBody::UpdateZonalInfo() {
    info->zone[0] = RoundToInt(position.x * kZoneScale);
    info->zone[1] = RoundToInt(position.y * kZoneScale);
    info->zone[2] = RoundToInt(position.z * kZoneScale);
}

// A hit on the world: past the threshold the force is scaled into damage, which a vehicle also counts as the
// player's while its timer runs, and which goes to the owner unless it is a kind 1 body on a spline path.
// FUNC_AT(0x000ad520)
void RigidBody::CalculateAndApplyWorldDamage(const Coord4 *direction, float force) {
    float threshold = kind < kVehicleKinds ? kVehicleDamageThreshold : kDamageThreshold;
    if (!(force > threshold))
        return;
    force = (float)((double)Rigid_DamageScale * 2.0 * force);
    if (kind < kVehicleKinds) {
        force = Rigid_VehicleWorldDamageScale * force;
        if (RigidVehicles[ownerIndex]->GetDamageByPlayerTimer())
            RigidVehicles[ownerIndex]->AddDamageByPlayer(force);
    }
    if (kind == kKind1 && RigidVehicles[ownerIndex]->GetSplinePath() != NULL)
        return;
    if (force > 0.0f) {
        PhysicsObject *owner = GetOwner();
        owner->ApplyDamageVirtual(direction, &position, force, Rigid_WorldDamageFactor, 1, DamageSourceSig);
    }
}

// FUNC_AT(0x000ad600)
void RigidBody::ForceToSleep() {
    sleepState = kAsleep;
    angularMomentum = Coord3{0.0f, 0.0f, 0.0f};
    momentum = Coord3{0.0f, 0.0f, 0.0f};
    velocity = Coord3{0.0f, 0.0f, 0.0f};
    angularVelocity = Coord3{0.0f, 0.0f, 0.0f};
}

// FUNC_AT(0x000ad630)
void __stdcall PointerVectorDeallocate(void *first, uint32_t count) {
    if (first != NULL)
        UMemory::FastFree(first, count * sizeof(void *));
}

// FUNC_AT(0x000ad650)
double ClampedForceRatio(float value, float limit, float scale) {
    if (value < 0.0f)
        value = -value;
    double ratio = (double)value * scale / limit;
    if (!(ratio < 1.0))
        ratio = 1.0;
    return ratio;
}

// The ground under a point: the face under it (kept from the last call if still current), its normal in `ground`
// and the point's height above it in ground->w (with the surface's bumps; 10 more for some vehicles). Without a
// face, two probes 0.1 and 0.2 along x; a face whose search found an instance but no face is then taken as found,
// and the answer is false. With no instance there at all, a flat face is made at the point.
// FUNC_AT(0x000ad690)
bool RigidBody::TempGetHeightInformation(bool, const Coord3 *point, Coord4 *ground, WWorldPos *worldPosition) {
    bool found = true;
    fgCollisionMgr->barrierMask = kHeightSearchMask;
    bool searched = worldPosition->FindClosestFace(point, true);
    if (!worldPosition->valid) {
        if (worldPosition->face.corner[2].tag.type != 0) {
            Coord3 probe = *point;
            for (int i = 0; i < 2; i++) {
                probe.x += kProbeStep;
                worldPosition->FindClosestFace(&probe, true);
                if (worldPosition->valid)
                    break;
            }
            if (!worldPosition->valid && worldPosition->face.corner[2].tag.type != 0) {
                found = false;
                worldPosition->valid = 1;
            }
        } else {
            worldPosition->MakeFaceAtPoint(point);
        }
        searched = true;
    }
    if (searched) {
        if (worldPosition->valid) {
            WWorldPos_FaceNormal(worldPosition, 0, ground);
        } else {
            ground->x = 0.0f;
            ground->y = 1.0f;
            ground->z = 0.0f;
        }
        ground->w = 0.0f;
    }
    if (worldPosition->valid) {
        double bump = fgCollisionMgr->SurfaceBumpHeight(point, &worldPosition->face.corner[2].tag.type);
        const StripVertex &corner = worldPosition->face.corner[0];
        ground->w = (float)(bump + ((double)corner.z - point->z) * ground->z + ((double)corner.y - point->y) * ground->y +
                            ((double)corner.x - point->x) * ground->x);
        if (kind < kVehicleKinds && RigidVehicles[ownerIndex]->GetPhysics()->unknownCC != 0)
            ground->w += kVehicleHeightLift;
    } else {
        ground->w = 0.0f;
    }
    fgCollisionMgr->barrierMask = kHeightRestoreMask;
    return found;
}

// The levers: a car's four wheels from its CAR_WHEEL_* offsets, then the render object's collision points (raised
// by its offset), at most 16 in all; without collision points the box's eight corners.
// FUNC_AT(0x000ad840)
void RigidBody::InitLevers(PhysicsObject *owner, const Coord4 *halfExtents) {
    uint32_t count;
    float offset = 0.0f;
    const Coord4 *points = static_cast<const Coord4 *>(owner->GetCollisionGeometry(&count, &offset));
    Coord4 *levers = info->levers;
    if (count != 0) {
        int first = 0;
        if (owner->type == kOwnerTypeCar) {
            static_cast<RigidVehicle *>(owner)->GetPhysics();     // the answer is not used
            float wheelX = owner->attributes.LookupFloat("CAR_WHEEL_X_OFFSET", NULL);
            float frontZ = owner->attributes.LookupFloat("CAR_WHEEL_ZF_OFFSET", NULL);
            float rearZ = owner->attributes.LookupFloat("CAR_WHEEL_ZR_OFFSET", NULL);
            levers[0] = Coord4{wheelX - halfExtents->x, -halfExtents->y, frontZ + halfExtents->z, 0.0f};
            levers[1] = Coord4{halfExtents->x - wheelX, -halfExtents->y, frontZ + halfExtents->z, 0.0f};
            if (RigidVehicles[ownerIndex]->GetPhysics()->unknownC8 != 0) {
                levers[2] = Coord4{0.0f, -halfExtents->y, rearZ - halfExtents->z, 0.0f};
                levers[3] = Coord4{0.0f, -halfExtents->y, rearZ - halfExtents->z, 0.0f};
            } else {
                levers[2] = Coord4{wheelX - halfExtents->x, -halfExtents->y, rearZ - halfExtents->z, 0.0f};
                levers[3] = Coord4{halfExtents->x - wheelX, -halfExtents->y, rearZ - halfExtents->z, 0.0f};
            }
            first = kWheels;
        }
        info->leverCount = int8_t(count + first);
        if (info->leverCount > kMaxLevers) {
            count += kMaxLevers - info->leverCount;
            info->leverCount = kMaxLevers;
        }
        for (uint32_t i = 0; i < count; i++) {
            levers[first + i] = points[i];
            levers[first + i].y = offset + levers[first + i].y;
        }
    } else {
        info->leverCount = kBoxCorners;
        levers[0] = Coord4{-halfExtents->x, -halfExtents->y, halfExtents->z, 0.0f};
        levers[1] = Coord4{halfExtents->x, -halfExtents->y, halfExtents->z, 0.0f};
        levers[2] = Coord4{-halfExtents->x, -halfExtents->y, -halfExtents->z, 0.0f};
        levers[3] = Coord4{halfExtents->x, -halfExtents->y, -halfExtents->z, 0.0f};
        levers[4] = Coord4{halfExtents->x, halfExtents->y, halfExtents->z, 0.0f};
        levers[5] = Coord4{-halfExtents->x, halfExtents->y, halfExtents->z, 0.0f};
        levers[6] = Coord4{halfExtents->x, halfExtents->y, -halfExtents->z, 0.0f};
        levers[7] = Coord4{-halfExtents->x, halfExtents->y, -halfExtents->z, 0.0f};
        if (kind < kVehicleKinds)
            static_cast<RigidVehicle *>(owner)->GetPhysics()->unknown60 = kNoGeometryUnknown60;
    }
    info->groundFriction = (float)((double)mass / kGroundFrictionMassScale * Rigid_GROUND_FRICTION_COEFF);
}
