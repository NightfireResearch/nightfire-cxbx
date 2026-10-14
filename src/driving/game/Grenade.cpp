#pragma fp_contract(off)

#include "Grenade.h"
#include "../Vtables.h"   // our copies of the game's vtables
#include "Explosion.h"                  // ExplosionType
#include "Simulation.h"
#include "WeaponManager.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../helpers.h"
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"       // DamageSourceSig
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"      // RAutonomousObj
#include "../world/Collider.h"
#include "../world/CollisionTypes.h"    // WorldCollisionInfo
#include "../world/WorldPos.h"

// ---------------------------------------------------------------------------------------------------------------
// Grenade (0x0005d4f0-0x0005dc60), ported from the listing. See Grenade.h.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's, not ported yet


// ---- the game's globals

extern const MATRIX4 *IdentityMatrix;   // anim/Weapon.cpp

namespace {

void **const kGrenadeVtable = (void **)Vtable_0018ed98;
RSceneObj_vtbl *const kRAutonomousObjVtable = (RSceneObj_vtbl *)Vtable_0018a398;
constexpr float kThrowScale = 0.5f;             // of the speed
constexpr float kUnknown6c4 = 0.5f;
constexpr int kFuseSeconds = 3;
constexpr int kArmingSteps = 5;
constexpr float kRadius = 0.5f;
constexpr float kMass = 0.25f;
constexpr float kBounceKeep = 0.25f;            // of the velocity
constexpr float kBounceSpinKeep = 0.95f;        // of the acceleration
constexpr int kBouncesToExplode = 2;
const Coord3 kGravity = {0.0f, -40.0f, 0.0f};  // 0x0018ede0
const Coord3 kZero3 = {0.0f, 0.0f, 0.0f};

// The explosions' size: the grenade weapon's damage
float ExplosionSize() {
    return float(WeaponManager->weapons[kWeaponGrenade].damage);
}

}  // namespace

// FUNC_AT(0x0005d4f0)
void Grenade::Destruct() {
    vtable = kGrenadeVtable;
    PhysicsObject::Destruct();
}

// FUNC_AT(0x0005d7a0)
Grenade* Grenade::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(Grenade));
    return this;
}

// FUNC_AT(0x0005d550)
void Grenade::Render(bool firstView) {
    if (firstView) {
        SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
        Coord4 at = {body->position.x, body->position.y, body->position.z, 1.0f};
        streak.Update(&at);
    }
    streak.Draw();
}

// FUNC_AT(0x0005d5c0)
Grenade* Grenade::Construct(int weaponType, const Coord3 *direction, const Coord3 *position, float speed,
                            uint32_t ownerSig) {
    PhysicsObject::Construct("weapon", "Grenade", kObjectGrenade, ownerSig, false, false);
    vtable = kGrenadeVtable;
    // RMissileStreak's constructor (the linker folded it with EAGL::TARProperty's)
    streak.count = 0;
    streak.tick = 0;
    streak.type = 0;
    this->weaponType = weaponType;
    unknown6c4 = kUnknown6c4;
    fuse = fgSimulation.stepsPerSecond * kFuseSeconds;
    bounces = 0;
    armedStep = fgSimulation.stepCount + kArmingSteps;
    explode = 0;
    Coord4 velocity;
    VU0_v4scale(direction, speed * kThrowScale, &velocity);
    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleGrenade, position, Xyz(&velocity), &kZero3, &orientation, kRadius,
                        kMass);
    fgSimulation.GetSimpleRigidBody(rigidBodySlot)->flags |=
        SimpleRigidBody::kFlag01 | SimpleRigidBody::kFlag04 | SimpleRigidBody::kMoves |
        SimpleRigidBody::kNearRigidHits | SimpleRigidBody::kNearSimpleHits;

    RAutonomousObj *render = static_cast<RAutonomousObj *>(UMemory::FastAlloc(sizeof(RAutonomousObj), "RAutonomousObj"));
    if (render != NULL) {
        memcpy(&render->instance, IdentityMatrix, sizeof render->instance);
        render->Construct(&render->instance);
        render->vtable = kRAutonomousObjVtable;
    }
    SetRenderObject(render);
    renderObject->Load("data\\car\\model\\", "grenade.crp", "grenade", 0);
    return this;
}

// FUNC_AT(0x0005d7d0)
void Grenade::Simulate() {
    PhysicsObject::Simulate();
    explode = --fuse <= 0;

    // the step just taken, against the world
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    Coord4 segment[2];
    segment[1] = Coord4{body->position.x, body->position.y, body->position.z, 1.0f};
    Coord4 step = {body->velocity.x, body->velocity.y, body->velocity.z, 1.0f};
    VU0_v4scale4(&step, fgSimulation.timeStep, &step);
    VU0_v4sub4(&segment[1], &step, &segment[0]);
    WorldCollisionInfo info;
    info.Construct();
    WCollider collider;
    collider.Construct(segment, WCollider::kCollideBarriers | WCollider::kCollideInstances);

    bool bounce = true;
    if (!collider.GetWorldNormal(segment, &info)) {
        // through the ground under it?
        worldPos.FindClosestFace(Xyz(&segment[1]), true);
        bool below = false;
        if (worldPos.valid && worldPos.HeightAtPoint(Xyz(&segment[1]), false) > segment[1].y)
            below = true;
        info.point = segment[0];
        if (!below) {
            info.normal = Coord4{0.0f, 1.0f, 0.0f, 0.0f};
            bounce = false;
        } else if (worldPos.valid) {
            worldPos.face.GetNormal(Xyz(&info.normal));
        } else {
            info.normal.x = 0.0f;
            info.normal.y = 1.0f;
            info.normal.z = 0.0f;
        }
    }

    if (bounce) {
        // reflected off the normal, keeping a quarter of the speed
        Coord4 velocity = {body->velocity.x, body->velocity.y, body->velocity.z, 0.0f};
        Coord4 spin = {body->acceleration.x, body->acceleration.y, body->acceleration.z, 0.0f};
        VU0_v4scaleadd(&info.normal, -2.0f * v3dotprod(&info.normal, &velocity), &velocity, &velocity);
        VU0_v4scale(&velocity, kBounceKeep, &velocity);
        VU0_v4scale(&spin, kBounceSpinKeep, &spin);
        body->velocity = *Xyz(&velocity);
        body->acceleration = *Xyz(&spin);
        if (++bounces >= kBouncesToExplode) {
            explode = 1;
        } else {
            Coord3 forward;
            body->GetForwardVector(&forward);
            fgSimulation.SpawnExplosion(kMineExplosion, weaponType, &forward, &body->position, ExplosionSize(), 0, NULL,
                                        DamageSourceSig);
        }
    }

    // gravity, and facing the way it moves
    body->Accelerate(&kGravity);
    Coord4 heading;
    VU0_v4unitxyz(&body->velocity, &heading);
    MATRIX4 generated;
    MATRIX4 frame = *Util_GenerateMatrix(&generated, Xyz(&heading));
    Coord4 orientation;
    VU0_m4toquat(&orientation, &frame);
    body->SetOrientation(&orientation);

    if (fgSimulation.stepCount < armedStep)
        explode = 0;
    if (explode) {
        SimpleRigidBody *exploding = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
        Coord3 forward;
        exploding->GetForwardVector(&forward);
        fgSimulation.SpawnExplosion(kMissileExplosion, weaponType, &forward, &exploding->position, ExplosionSize(), 0,
                                    NULL, DamageSourceSig);
        fgSimulation.DeletePhysicsObjectThunk(this);
    }
    collider.Destruct();
}
