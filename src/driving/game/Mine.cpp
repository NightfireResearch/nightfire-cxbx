#pragma fp_contract(off)

#include "Mine.h"

#include <stdlib.h>
#include <string.h>

#include "Explosion.h"                  // ExplosionType
#include "Simulation.h"
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix, Util_PerturbVector
#include "../engine/UMemory.hpp"
#include "../physics/RigidBody.h"       // DamageSourceSig
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"  // fgCollisionMgr
#include "../world/CollisionTypes.h"    // WorldCollisionInfo
#include "../world/Targeting.h"

// ---------------------------------------------------------------------------------------------------------------
// Mine (0x0005e180-0x0005e8c0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet

#define WorldCollisionInfo_Construct ((WorldCollisionInfo *(__fastcall *)(WorldCollisionInfo *, int))0x0001d9f0)

// ---- globals
#define IdentityMatrix (*(const MATRIX4 **)0x001c4654)

namespace {

const Coord3 kZero = {0.0f, 0.0f, 0.0f};
RSceneObj_vtbl *const kRAutonomousObjVtable = (RSceneObj_vtbl *)0x0018a398;

constexpr int kMaxRigidBodies = 64;
constexpr float kCellScale = 0.2f;          // world units to the rigid bodies' cells (RigidBodyInfo::zone)
constexpr float kBodyRadius = 0.25f;
constexpr float kBodyMass = 0.5f;
constexpr float kThrowSpread = 0.2f;        // a floating mine's throw, Gaussian turns of deviation
constexpr float kThrowAcceleration = 50.0f;
constexpr float kSetOffDamage = 5.0f;
constexpr int kTimeMineSeconds = 6;
constexpr int kMineSeconds = 30;            // the other mines' timers (never counted down)
constexpr float kProxMineRadius = 1.0f;
constexpr float kFloatingMineRadius = 2.5f;
constexpr float kMineRadius = 3.0f;

// The first Simulate's effects (TriggerFX's effect bits and types; names ours)
constexpr uint32_t kMineEffect = 0x2000000;
constexpr int kFXType11 = 11;               // a type RSceneObj::TriggerFX does nothing with
constexpr uint32_t kFXType11Which = 9;

// RSceneObj::TriggerFX through the render object's vtable (slot 15)
void TriggerFX(RSceneObj *render, int type, uint32_t which) {
    typedef void (RSceneObj::*TriggerFXMethod)(int type, uint32_t which, uint32_t unused, const Coord3 *position,
                                               const Coord3 *direction, float intensity);
    (render->*XbeVirtual<TriggerFXMethod>(render, 15))(type, which, 0, &kZero, &kZero, -1.0f);
}

} // namespace

// ---- construction

// FUNC_AT(0x0005e480)
Mine* Mine::Construct(int kind, const Coord3 *direction, const Coord3 *position, float speed, uint32_t ownerSig) {
    PhysicsObject::Construct("weapon", "Mine", kObjectMine, ownerSig, false, false);
    this->kind = kind;
    vtable = reinterpret_cast<void **>(kMineVtable);
    radius = 0.0f;
    armStep = fgSimulation.stepsPerSecond + fgSimulation.stepCount;
    detonate = 0;
    target = NULL;
    effectsStarted = 0;
    switch (kind) {
    case kWeaponTimeMine:
        timer = fgSimulation.stepsPerSecond * kTimeMineSeconds;
        break;
    case kWeaponProxMine:
        timer = 0;
        radius = kProxMineRadius;
        break;
    case kWeaponFloatingMine:
        timer = fgSimulation.stepsPerSecond * kMineSeconds;
        radius = kFloatingMineRadius;
        break;
    default:
        timer = fgSimulation.stepsPerSecond * kMineSeconds;
        radius = kMineRadius;
        break;
    }

    Coord3 velocity;
    VU0_v4scale(direction, speed * 0.5f, &velocity);
    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleMine, position, &velocity, &kZero, &orientation, kBodyRadius, kBodyMass);
    fgSimulation.GetSimpleRigidBody(rigidBodySlot)->flags |=
        SimpleRigidBody::kMoves | SimpleRigidBody::kNearRigidHits | SimpleRigidBody::kTouchesTriggers;

    RAutonomousObj *render =
        static_cast<RAutonomousObj *>(UMemory::FastAlloc(sizeof(RAutonomousObj), "RAutonomousObj"));
    if (render != NULL) {
        memcpy(&render->instance, IdentityMatrix, sizeof render->instance);
        render->Construct(&render->instance);
        render->vtable = kRAutonomousObjVtable;
    }
    SetRenderObject(render);

    if (kind == kWeaponFloatingMine) {
        // Util_PerturbVector reads the direction's x, y and z only
        Coord3 perturbed;
        Coord3 push = *Util_PerturbVector(&perturbed, reinterpret_cast<const Coord4 *>(direction), kThrowSpread);
        v3unit(&push, &push);
        v3scale(1, &push, kThrowAcceleration, &push);
        fgSimulation.GetSimpleRigidBody(rigidBodySlot)->Accelerate(&push);
    }
    return this;
}

// FUNC_AT(0x0005e180)
void Mine::Destruct() {
    vtable = reinterpret_cast<void **>(kMineVtable);
    if (target != NULL) {
        TargetPicker.UnregisterTarget(target);
        target->ownerType = kTargetPoint;
        target->RemoveReference();
    }
    PhysicsObject::Destruct();
}

// FUNC_AT(0x0005e6e0)
Mine* Mine::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(Mine));
    return this;
}

// ---- going off

// An explosion of the proximity mine's damage at the body, and the mine deleted
void Mine::Explode(SimpleRigidBody *body, const Coord3 *forward) {
    float damage = float(WeaponManager->weapons[kWeaponProxMine].damage);
    fgSimulation.SpawnExplosion(kMineExplosion, kind, forward, &body->position, damage, 0, NULL, DamageSourceSig);
    fgSimulation.DeletePhysicsObjectThunk(this);
}

// FUNC_AT(0x0005e200)
void Mine::CheckForProximity() {
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    const Coord3 *position = &body->position;
    int cell[3] = {
        RoundToInt(position->x * kCellScale),
        RoundToInt(position->y * kCellScale),
        RoundToInt(position->z * kCellScale),
    };
    float reachSq = radius * radius;
    for (int i = 0; i < kMaxRigidBodies; i++) {
        PhysicsObject *object = fgSimulation.rigidBodyOwners[i];
        if (object == NULL)
            continue;
        RigidBody *other = fgSimulation.GetRigidBody(i);
        if (fgSimulation.stepCount < armStep)
            continue;
        const int32_t *zone = other->info->zone;
        if (abs(cell[0] - zone[0]) > 1 || abs(cell[1] - zone[1]) > 1 || abs(cell[2] - zone[2]) > 1)
            continue;
        Coord4 here = {position->x, position->y, position->z, 0.0f};
        Coord4 offset;
        VU0_v4sub(&other->position, &here, &offset);
        if (VU0_v3lengthsquare(&offset) < reachSq && !IsOwnedBy(object)) {
            Coord3 forward;
            VU0_ExtractZAxis3FromQuat(&body->orientation, &forward);
            Explode(body, &forward);
            return;
        }
    }
}

// FUNC_AT(0x0005e400)
void Mine::CheckTimer() {
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    if (--timer > 0)
        return;
    Coord3 forward;
    body->GetForwardVector(&forward);
    Explode(body, &forward);
}

// FUNC_AT(0x0005e3e0)
int Mine::ApplyDamage(const Coord3 *, const Coord3 *, float amount, float, int, const uint32_t *) {
    if (amount > kSetOffDamage)
        detonate = 1;
    return 0;
}

// FUNC_AT(0x0005e710)
void Mine::Simulate() {
    SimpleRigidBody *body = fgSimulation.GetSimpleRigidBody(rigidBodySlot);
    if (!effectsStarted) {
        TriggerFX(renderObject, RSceneObj::kFXOn, kMineEffect);
        TriggerFX(renderObject, kFXType11, kFXType11Which);
        effectsStarted = 1;
    }
    if (detonate) {
        Coord3 forward;
        body->GetForwardVector(&forward);
        Explode(body, &forward);
    } else {
        switch (kind) {
        case kWeaponTimeMine:
            CheckTimer();
            break;
        case kWeaponProxMine:
        case kWeaponFloatingMine:
            CheckForProximity();
            break;
        }
    }
    PhysicsObject::Simulate();

    if (kind == kWeaponFloatingMine) {
        // Its last step's path against the world
        Coord4 path[2];
        path[1] = {body->position.x, body->position.y, body->position.z, 1.0f};
        Coord4 step = {body->velocity.x, body->velocity.y, body->velocity.z, 1.0f};
        VU0_v4scale4(&step, fgSimulation.timeStep, &step);
        VU0_v4sub4(&path[1], &step, &path[0]);
        WorldCollisionInfo info;
        WorldCollisionInfo_Construct(&info, 0);
        if (fgCollisionMgr->CheckHitWorld(path, &info) > 0)
            detonate = 1;
    }
}
