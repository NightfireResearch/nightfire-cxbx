#include "Newton.h"

#include "PhysicsMath.h"
#include "RigidBody.h"
#include "SimpleRigidBody.h"
#include "Simulation.h"
#include "../../helpers.h"
#include "../anim/AnimEngine.h"         // Handle
#include "../engine/PhysicsUtil.h"       // Util_GenerateMatrix
#include "../engine/UGroup.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../platform/X87.h"
#include "../render/Renderer.h"
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"
#include "../world/World.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#pragma fp_contract(off)

// ---------------------------------------------------------------------------------------------------------------
// Newton (0x00060b60-0x00061a50), ported from the listing. The x87 chains are computed in double in the original's
// order and rounded where it stores a float; comparisons fail on NaN where the original's jumps do.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's, not ported yet

#define Simulation_DeletePhysicsObject ((void (__fastcall *)(void *, int, PhysicsObject *))0x000b4740)
#define Simulation_SpawnNewtonObject ((Newton *(__fastcall *)(void *, int, const Coord4 *direction, const Coord4 *position, const Coord4 *momentum, const Coord4 *spin, CARP::Instance *instances, int instanceCount, float mass, float lifetime))0x000b5e20)
#define WorldCollisionInfo_Construct ((WorldCollisionInfo *(__fastcall *)(WorldCollisionInfo *, int))0x0001d9f0)

// ---- globals

#define Sim ((void *)0x00233ff0)                            // the Simulation
#define SimTimeStep FLOAT_AT(0x00234e30)                    // the simulation's step, in seconds
#define ZeroVector (*(const Coord3 *)0x00243030)            // the game's zero vector
#define IdentityMatrix (*(const MATRIX4 **)0x001c4654)

namespace {

void **const kNewtonVtable = (void **)0x0018ef7c;
RSceneObj_vtbl *const kRAutonomousObjVtable = (RSceneObj_vtbl *)0x0018a398;

constexpr int kNewtonType = 9;
constexpr float kHitPointsPerMass = 10.0f;
constexpr float kBodyRadius = 0.25f;
constexpr float kInstanceSize = 0.25f;              // the render instance's dimensions
constexpr uint32_t kAnimStatesAll = 2;              // InitAllSystemStates' argument
constexpr uint8_t kNoProcAnim = 0xff;
constexpr uint32_t kInstanceDataTag = 0x696e0000;   // 'in\0\0': a model's instances

// Simulate
constexpr Coord3 kGravity = {0.0f, -9.8f, 0.0f};    // 0x0018ef70
constexpr float kBounce = -2.0f;                    // the reflection of a vector off the ground's plane
constexpr uint8_t kFaceType6 = 6;                   // a face of this type keeps less of the speed
constexpr float kBounceKeepType6 = 0.35f;
constexpr float kBounceKeep = 0.6f;
constexpr float kHalfVelocityMass = 3.14159f;       // 0x40490fd0: a body of this mass keeps half its y velocity
constexpr float kSpinBounce = -0.8f;

// SpawnFromEvent
constexpr float kProcAnimScale = 1.2f;
constexpr float kSpreadBase = 0.01f;
constexpr float kSpreadPerSpeed = 0.1f;
constexpr float kSpreadMin = 1.0f;
constexpr float kSpreadMax = 8.0f;
constexpr float kSpinSize = 4.0f;
constexpr float kKickSize = 3.0f;
constexpr float kSlowSquared = 0.1f;                // below this squared speed the kick is random
constexpr float kRandomLift = 2.0f;
constexpr float kAwayLift = 0.5f;
constexpr float kAwaySpeedScale = 1.25f;
constexpr float kAwayMinHeight = 4.0f;
constexpr float kTowardsMassScale = 10000.0f;
constexpr float kFlightTime = 1.0f;                 // 0x0018ef68 (the name is ours)

// Two draws packed for BytesToCoordXYZ: x from the second, inverted, y and z from the first
uint32_t RandomBytes() {
    uint32_t low = RandomShort();
    uint32_t high = RandomShort();
    return ~high << 16 | low;
}

}  // namespace

// FUNC_AT(0x00061060)
Newton* Newton::Construct(const Coord3 *direction, const Coord3 *position, const Coord3 *momentum, const Coord3 *spin,
                          CARP::Instance *instances, int instanceCount, float mass, float lifetime) {
    PhysicsObject::Construct("newton", "Newton", kNewtonType, 0, true, false);
    vtable = kNewtonVtable;
    stepsLeft = Ftol((double)SimStepsPerSecond * lifetime);
    unknown74 = 0;
    lastPosition = Coord4{position->x, position->y, position->z, 1.0f};

    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    double inverseMass = 1.0 / mass;
    CARP::Instance single = *instances;
    Coord3 velocity;
    velocity.x = (float)(momentum->x * inverseMass);
    velocity.y = (float)(momentum->y * inverseMass);
    velocity.z = (float)(momentum->z * inverseMass);
    if (instanceCount == 1) {
        single.flags &= ~kWorldInstanceSceneObj;
        single.procAnimType = kNoProcAnim;
        single.position[0] = 0.0f;
        single.position[1] = 0.0f;
        single.position[2] = 0.0f;
        instances = &single;
    }

    RAutonomousObj *render = static_cast<RAutonomousObj *>(UMemory::FastAlloc(sizeof(RAutonomousObj), "RAutonomousObj"));
    if (render != NULL) {
        memcpy(&render->instance, IdentityMatrix, sizeof render->instance);
        render->Construct(&render->instance);
        render->vtable = kRAutonomousObjVtable;
    }
    SetRenderObject(render);
    renderObject->UseInstanceList(instances, instanceCount, NULL, instanceCount == 1, "<<Newton>>");
    renderObject->animHandle->InitAllSystemStates(kAnimStatesAll);
    hitPoints = mass * kHitPointsPerMass;
    SetHitPointLoc(&hitPoints);
    static_cast<CARP::Instance *>(renderObject->sourceInstance)->SetDimensions(false, kInstanceSize, 0.0f, 0.0f);

    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    if (body != NULL)
        body->Construct(rigidBodySlot, kSimpleNewton, position, &velocity, spin, &orientation, kBodyRadius, mass);
    Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot)->flags |= SimpleRigidBody::kMoves;
    return this;
}

// FUNC_AT(0x00060b60)
void Newton::Destruct() {
    vtable = kNewtonVtable;
    PhysicsObject::Destruct();
}

// FUNC_AT(0x000612f0)
Newton* Newton::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::Free(this);
    return this;
}

// FUNC_AT(0x00061050)
int Newton::ApplyDamage(const void *, const void *, float, float, int, const uint32_t *) {
    return 0x20;
}

// The step: when the lifetime runs out the object is deleted (and the rest still runs on its body). Otherwise the
// segment from the last position to the body's is tested against the ground; on a hit the body is put back above
// it, mirrored through the ground's plane, and its velocity reflected and damped, its spin reversed and damped.
// The orientation then turns by the spin over the step - normalised by its squared length, not its length - and
// gravity accelerates the body.
// FUNC_AT(0x00060b70)
void Newton::Simulate() {
    SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, rigidBodySlot);
    if (--stepsLeft <= 0) {
        Simulation_DeletePhysicsObject(Sim, 0, this);
    } else {
        Coord4 segment[2];
        segment[0] = lastPosition;
        segment[1] = Coord4{body->position.x, body->position.y, body->position.z, 1.0f};
        Coord4 velocity = {body->velocity.x, body->velocity.y, body->velocity.z, 1.0f};
        Coord4 midpoint = {body->acceleration.x, body->acceleration.y, body->acceleration.z, 1.0f};
        Coord4 spin = midpoint;
        Coord4 q = body->orientation;
        Coord4 turn;
        VU0_v4scale(&spin, SimTimeStep, &turn);

        WorldCollisionInfo closest, none, ground;
        WorldCollisionInfo_Construct(&closest, 0);
        WorldCollisionInfo_Construct(&none, 0);
        WorldCollisionInfo_Construct(&ground, 0);

        // The midpoint of the step, its distance from the last position and a clamp of its height: none is used.
        VU0_v3add(&segment[0], &segment[1], &midpoint);
        VU0_v4scale(&midpoint, 0.5f, &midpoint);
        double dx = (double)midpoint.x - segment[0].x;
        double dy = (double)midpoint.y - segment[0].y;
        double dz = (double)midpoint.z - segment[0].z;
        VU0_sqrt((float)(dz * dz + dy * dy + dx * dx));
        midpoint.w = 1.0f < midpoint.z ? 1.0f : midpoint.z;

        fgCollisionMgr->GetGroundCollision(segment, &worldPos, &ground);
        fgCollisionMgr->ClosestCollisionInfo(reinterpret_cast<const Coord3 *>(&segment[0]), &none, &ground, &closest);
        if (closest.hitType != 0) {
            Coord4 depth, mirrored, bounced;
            VU0_v4sub(&segment[1], &closest.point, &depth);
            VU0_v4scaleadd(&closest.normal, v3dotprod(&closest.normal, &depth) * kBounce, &depth, &mirrored);
            VU0_v3add(&mirrored, &closest.point, &bounced);
            body->position = Coord3{bounced.x, bounced.y, bounced.z};
            VU0_v4scaleadd(&closest.normal, v3dotprod(&closest.normal, &velocity) * kBounce, &velocity, &velocity);
            VU0_v4scale(&velocity, closest.faceType == kFaceType6 ? kBounceKeepType6 : kBounceKeep, &velocity);
            if (GetMass() == kHalfVelocityMass)
                velocity.y = velocity.y * 0.5f;
            VU0_v4scale(&spin, kSpinBounce, &spin);
            body->velocity = Coord3{velocity.x, velocity.y, velocity.z};
            body->acceleration = Coord3{spin.x, spin.y, spin.z};
        }

        // q += (0, turn) q / 2
        Coord4 dq;
        dq.x = (float)(((double)q.z * turn.y - (double)q.y * turn.z + (double)q.w * turn.x) * 0.5);
        dq.y = (float)(((double)q.x * turn.z - (double)turn.x * q.z + (double)q.w * turn.y) * 0.5);
        dq.z = (float)(((double)turn.x * q.y - (double)q.x * turn.y + (double)q.w * turn.z) * 0.5);
        dq.w = (float)((-((double)q.x * turn.x) - (double)q.y * turn.y - (double)turn.z * q.z) * 0.5);
        VU0_v4add4(&q, &dq, &q);
        VU0_v4scale4(&q, 1.0f / VU0_v4lengthsquare(&q), &q);
        body->SetOrientation(&q);
        body->Accelerate(&kGravity);
    }
    lastPosition = Coord4{body->position.x, body->position.y, body->position.z, 1.0f};
    PhysicsObject::Simulate();
}

// The instance: a proc-anim instance's scene object is used instead of `sceneObj`. With a scene object, the
// instance is its own (then hidden, and all the instances of its animation handle go to the Newton) or one of its
// model's (then that one of the handle's instances, placed where the scene object draws it). The spin is the
// body's plus a random part that grows with its speed; the kick the body's velocity per unit mass, given a random
// rise, or random if slow; then either thrown at the renderer's point or pushed away from the scene object's centre.
// FUNC_AT(0x00061310)
void Newton::SpawnFromEvent(float mass, float lifetime, CARP::Instance *instance, bool simpleBody, int body,
                            RSceneObj *sceneObj, uint8_t flags) {
    if (instance == NULL) {
        if (sceneObj == NULL)
            return;
        instance = static_cast<CARP::Instance *>(sceneObj->sourceInstance);
        if (instance == NULL)
            return;
    }

    bool useCollisionInfo = true;
    bool randomKick = false;
    int instanceCount = 1;
    float kickScale = 1.0f;
    if (instance->flags & kWorldInstanceProcAnim) {
        RSceneObj *animated = fgWorld->procAnims[instance->procAnimIndex].sceneObj;
        if (animated != NULL)
            sceneObj = animated;
        useCollisionInfo = false;
        kickScale = kProcAnimScale;
        randomKick = true;
    }

    bool modelInstance = false;
    uint32_t modelIndex = 0;
    if (sceneObj != NULL) {
        Handle *handle = sceneObj->animHandle;
        if (sceneObj->sourceInstance == instance) {
            sceneObj->Hide();
            instanceCount = handle->instanceCount;
            instance = handle->Instances();
        } else {
            CARP::BaseDesc *desc = static_cast<CARP::BaseDesc *>(sceneObj->baseDesc);
            UGroup *model = reinterpret_cast<UGroup *>(uintptr_t(desc->model.value));
            UData *data = model->DataLocateTag(kInstanceDataTag);
            if (data == model->DataEnd())
                return;
            modelIndex = uint32_t(instance - reinterpret_cast<CARP::Instance *>(data->Data()));
            if (modelIndex >= data->count)
                return;
            if (modelIndex >= handle->instanceCount)
                return;
            instance = &handle->Instances()[modelIndex];
            modelInstance = true;
        }
    }

    Coord4 kick = {ZeroVector.x, ZeroVector.y, ZeroVector.z, 0.0f};
    Coord4 spin = {ZeroVector.x, ZeroVector.y, ZeroVector.z, 0.0f};
    float speed = 0.0f;
    if (body >= 0) {
        if (simpleBody) {
            SimpleRigidBody *simple = Simulation_GetSimpleRigidBody(Sim, 0, body);
            kick = Coord4{simple->velocity.x, simple->velocity.y, simple->velocity.z, 0.0f};
            spin = Coord4{simple->acceleration.x, simple->acceleration.y, simple->acceleration.z, 0.0f};
            speed = simple->GetScalarVelocity();
        } else {
            RigidBody *rigid = Simulation_GetRigidBody(Sim, 0, body);
            kick = Coord4{rigid->velocity.x, rigid->velocity.y, rigid->velocity.z, 0.0f};
            spin = Coord4{rigid->angularVelocity.x, rigid->angularVelocity.y, rigid->angularVelocity.z, 0.0f};
            speed = VU0_v3length(&rigid->velocity);
        }
        if (sceneObj == NULL) {
            double inverseMass = 1.0 / mass;
            kick.x = (float)(kick.x * inverseMass);
            kick.y = (float)(kick.y * inverseMass);
            kick.z = (float)(kick.z * inverseMass);
        }
    } else {
        randomKick = true;
    }

    // Where the Newton starts, and the point a kick pushes it away from (the original leaves `centre` as its stack
    // had it on the paths that set neither; zero here).
    MATRIX4 matrix;
    Coord4 centre = {0.0f, 0.0f, 0.0f, 0.0f};
    Coord4 extents;
    if (sceneObj == NULL) {
        matrix = instance->Matrix();
        matrix.mtx[0][3] = 0.0f;
        matrix.mtx[1][3] = 0.0f;
        matrix.mtx[2][3] = 0.0f;
        matrix.mtx[3][3] = 1.0f;
        VU0_v4Init(&centre);
        VU0_v4Init(&extents);
    } else {
        if (modelInstance)
            sceneObj->GetInstancePosition(modelIndex, &matrix, true);
        else
            sceneObj->GetTransform(&matrix);
        if (useCollisionInfo) {
            sceneObj->GetCollisionInfo(&centre, &extents);
        } else if (!modelInstance && !simpleBody && body != -1) {
            // behind the instance as seen from the body, by the body's speed in x/z, at least 4 high
            RigidBody *rigid = Simulation_GetRigidBody(Sim, 0, body);
            Coord4 away, direction;
            VU0_v4sub(MatrixRow(&matrix, 3), &rigid->position, &away);
            away.y = Max(0.0f, away.y) + kAwayLift;
            VU0_v4unitxyz(&away, &direction);
            VU0_v4scale(&direction, VU0_v3lengthxz(&rigid->velocity) * kAwaySpeedScale, &centre);
            centre.y = Max(centre.y, kAwayMinHeight);
            VU0_v4sub(MatrixRow(&matrix, 3), &centre, &centre);
        }
    }

    Coord4 spinSize = {kSpinSize, kSpinSize, kSpinSize, 0.0f};
    float spread = (float)(((double)speed + kSpreadBase) * kSpreadPerSpeed);
    spread = kSpreadMax < spread ? kSpreadMax : spread;
    spread = kSpreadMin < spread ? spread : kSpreadMin;
    Coord4 spinRange;
    VU0_v4scale(&spinSize, spread, &spinRange);
    Coord4 centred = {-0.5f, -0.5f, -0.5f, 0.0f};
    BytesToCoordXYZ(RandomBytes(), &centred, &spinRange, &spin, &spin);

    if (randomKick || VU0_v3lengthsquare(&kick) < kSlowSquared) {
        Coord4 kickRange = {kKickSize, kKickSize, kKickSize, 0.0f};
        Coord4 level = {-0.5f, 0.0f, -0.5f, 0.0f};
        BytesToCoordXYZ(RandomBytes(), &level, &kickRange, &kick, &kick);
    } else {
        kick.y = (float)(RandomScaled(kRandomLift) + kick.y);
    }

    if (flags & kNewtonSpawnTowardsView) {
        // from the instance to the renderer's point, led by the player's velocity, in kFlightTime under gravity
        mass = mass * kTowardsMassScale;
        Coord4 from = {matrix.mtx[3][0], matrix.mtx[3][1], matrix.mtx[3][2], 0.0f};
        const Coord4 &view = fgRenderer->cameraPosition;
        Coord4 target = {view.x, view.y, view.z, 0.0f};
        PhysicsObject *player = Simulation_GetPlayerObject(Sim, 0);
        const Coord3 &playerVelocity = Simulation_GetRigidBody(Sim, 0, player->rigidBodySlot)->velocity;
        Coord4 lead = {playerVelocity.x, playerVelocity.y, playerVelocity.z, 0.0f};
        VU0_v4scale(&lead, kFlightTime, &lead);
        VU0_v3add(&target, &lead, &target);
        Coord4 toward;
        VU0_v4sub(&target, &from, &toward);
        toward.y = toward.y + kAwayLift;
        VU0_v4scale(&toward, 1.0f / kFlightTime, &kick);
        kick.y = (float)(kick.y - (double)kGravity.y * kFlightTime * 0.5);
        VU0_v4scale(&kick, mass, &kick);
    } else {
        VU0_v4sub(MatrixRow(&matrix, 3), &centre, &centre);
        VU0_v3add(&kick, &centre, &kick);
        VU0_v4scale(&kick, kickScale * mass, &kick);
    }
    Simulation_SpawnNewtonObject(Sim, 0, MatrixRow(&matrix, 2), MatrixRow(&matrix, 3), &kick, &spin, instance,
                                 instanceCount, mass, lifetime);
    instance->flags |= kWorldInstanceSceneObj;
}
