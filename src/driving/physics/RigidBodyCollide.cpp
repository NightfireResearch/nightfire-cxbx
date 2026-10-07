#pragma fp_contract(off)

#include "RigidBodyCollide.h"

#include <bit>

#include "PhysicsMath.h"
#include "PhysicsObject.h"
#include "../../helpers.h"
#include "../EventManager.hpp"            // Event::operator new
#include "../data/Carp.h"                 // CARP::Instance
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/MissionManager.h"
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"
#include "../world/Collider.h"
#include "../world/CollisionManager.h"
#include "../world/World.h"

// ---------------------------------------------------------------------------------------------------------------
// RigidBody's collision detection: CollideWithWorld (0x000b1420), CollideWithObject (0x000b0510) and
// ResolveWorldOBBCollision (0x000af960), ported from the listing. See RigidBodyCollide.h.
//
// The x87 code is ported bit for bit: chains in double in the original's order, rounded where it stores a float;
// comparisons keep their NaN behaviour. Vector words the original leaves as its stack had them (a vector's w that
// only the xyz functions touch, WorldCollisionInfo's segment words, which its inlined constructor copies from
// uninitialised stack) are zero here.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define CollisionImpact_Construct ((CollisionImpact *(__fastcall *)(CollisionImpact *, int))0x0003dc20)
#define ECollision_Construct ((void *(__fastcall *)(void *, int, CollisionImpact impact))0x0003f370)
#define Handle_ProcessStimuli ((void (__fastcall *)(Handle *, int, uint32_t stimulus, uint32_t step, int unknown))0x00077e00)
#define SMissionManager_ProgrammerDefinedEvent ((void (__fastcall *)(void *, int, int event, const char *text))0x000b72f0)

// ---- globals
#define Sim ((void *)0x00233ff0)                    // the Simulation
#define SimTimeStep FLOAT_AT(0x00234e30)            // the simulation's step, in seconds
#define SimStepCount U32_AT(0x00234e34)             // the Simulation's steps so far
#define DefaultVector (*(const Coord4 *)0x001d4c00) // (0, 0, 0, 1)

namespace {

constexpr unsigned kMaxPhysicsObjects = 64;      // PhysicsObjects' size
constexpr int8_t kPlayerKind = 1;           // the damage its body does to a vehicle goes to AddDamageByPlayer
constexpr int8_t kFirstObjectKind = 4;      // below: the owner is a vehicle (RigidVehicle)

// WCollisionMgr::barrierMask while a body's points are tested, and otherwise
constexpr uint32_t kBodyBarrierMask = 0x20;
constexpr uint32_t kDefaultBarrierMask = 0x10;

constexpr uint8_t kMissionEventFace = 0x0e;     // a face that raises programmer-defined mission event 0x15
constexpr int kMissionEventFaceHit = 0x15;

constexpr uint32_t kCollisionEventSize = 0x58;  // sizeof(ECollision)
constexpr float kWorldBoxForceScale = 6000.0f;  // CollideWithWorld's to ResolveWorldOBBCollision

// The stimuli a hit sends the animation of the world instance it hits: always the first, then by strength
enum HitStimulus : uint32_t {
    kStimulusHit = 0x04,
    kStimulusHit13 = 0x13,      // strength at least 0.1
    kStimulusHit14 = 0x14,      // 0.5
    kStimulusHit15 = 0x15,      // 2.5
    kStimulusHit16 = 0x16,      // 5
};

constexpr float kTenth = 0.1f;
static_assert(std::bit_cast<uint32_t>(kTenth) == 0x3dcccccd, "the original's 0.1");
constexpr float kForceCapRate = 0.0004f;
static_assert(std::bit_cast<uint32_t>(kForceCapRate) == 0x39d1b717, "the original's 0.0004");
constexpr float kMinimumBoxPushScale = 0.15f;
static_assert(std::bit_cast<uint32_t>(kMinimumBoxPushScale) == 0x3e19999a, "the original's 0.15");
constexpr float kTwentieth = 0.05f;
static_assert(std::bit_cast<uint32_t>(kTwentieth) == 0x3d4ccccd, "the original's 0.05");
constexpr float kWheelieHalfWidth = 0.2f;
static_assert(std::bit_cast<uint32_t>(kWheelieHalfWidth) == 0x3e4ccccd, "the original's 0.2");
constexpr float kSteepFace = 0.8191f;
static_assert(std::bit_cast<uint32_t>(kSteepFace) == 0x3f51b08a, "the original's 0.8191");

// The position and the word after it, as the original hands them on (a Coord4)
const Coord4 *Position4(const RigidBody *body) {
    return reinterpret_cast<const Coord4 *>(&body->position);
}

void RaiseCollisionEvent(const CollisionImpact &impact) {
    void *event = Event::operator new(kCollisionEventSize);
    if (event != NULL)
        ECollision_Construct(event, 0, impact);
}

template <class T> void DestroyVector(GameVector<T> *vector) {
    if (vector->first != NULL)
        UMemory::FastFree(vector->first, unsigned((vector->end - vector->first) * sizeof(T)));
}

} // namespace

// The response to the body's box meeting a collision object's (moving at objectVelocity, per step): an impulse
// along the normal against the body's momentum and angular momentum, unless the meeting is too fast (above 25
// along the normal, or 5 against a still object) or the body's info says not to. The impact records it, its
// strength out of TOTAL_FORCE_LIMIT.
// FUNC_AT(0x000af960)
bool RigidBody::ResolveWorldOBBCollision(const Coord4 *normal, const Coord4 *point, float depth,
                                         CollisionImpact *impact, const Coord3 *objectVelocity, float forceScale) {
    bool stillObject = VU0_v3lengthsquare(objectVelocity) == 0.0f;

    Coord4 lever = {};
    VU0_v4sub(point, &position, &lever);
    if (kind < kFirstObjectKind) {
        // a vehicle's lever: a tenth of the point's height, made negative, at most 0.5 down
        double height = double(lever.y) * kTenth;
        lever.y = float(height);
        if (height > 0.0)
            lever.y = float(-(height > 0.5 ? 0.5 : height));
        else if (height < -0.5)
            lever.y = -0.5f;
    }

    Coord4 pointVelocity = {}, relative = {};
    VU0_v4crossprodxyz(&angularVelocity, &lever, &pointVelocity);
    VU0_v3add(&pointVelocity, &velocity, &pointVelocity);
    VU0_v4sub(objectVelocity, &pointVelocity, &relative);
    float closingSpeed = v3dotprod(normal, &relative);

    impact->kindA = 1;
    impact->unknown4a = 0x10;
    impact->tag = 0x10;
    impact->ownerA = ownerIndex;
    impact->kindB = 6;
    impact->normal = *normal;
    impact->point = *point;
    impact->closingSpeed = closingSpeed;
    impact->strength = 0.0f;
    if (stillObject && closingSpeed > 5.0f)
        return false;
    if (closingSpeed > 25.0f)
        return false;
    if (info->unknown4fc)
        return true;
    info->unknown4fd = 1;

    VU0_v4sub(objectVelocity, &velocity, &relative);
    float approach = v3dotprod(normal, &relative);
    if (approach < 0.0f)
        approach = -approach;
    double push = double(approach) * Rigid_BoxSpeedScale;
    float depthPush = float(Rigid_BoxDepthScale * depth);
    if (!(push > depthPush))
        push = depthPush;
    float scale = float(push);
    if (stillObject) {
        push *= 0.25;
        scale = float(push);
    }
    if (push < 1.0)
        scale = 1.0f;

    float speedSquared = VU0_v3lengthsquare(&velocity);
    float objectSpeedSquared = float(VU0_v3lengthsquare(objectVelocity) *
                                     double(SimStepsPerSecond * SimStepsPerSecond));
    float fastest = speedSquared > objectSpeedSquared ? speedSquared : objectSpeedSquared;
    double forceCap = double(fastest) * Rigid_BoxForceCapScale * kForceCapRate;
    if (!(forceCap > Rigid_ForceCapFloor))
        forceCap = Rigid_ForceCapFloor;
    double scaleSquared = double(scale) * scale;
    scale = float(scaleSquared);
    if (scaleSquared > forceCap)
        scale = float(forceCap);

    Coord4 impulse = {};
    VU0_v4scale(normal, scale, &impulse);
    VU0_v4scale(&impulse, forceScale, &impulse);
    double speedsSum = double(objectSpeedSquared) + speedSquared;
    float speedsSquared = float(speedsSum);
    if (speedsSum > 0.0) {
        // no faster afterwards than 1.25 times the speeds before
        Coord4 after = {};
        VU0_v4sub(&momentum, &impulse, &after);
        VU0_v4scale(&after, float(1.0 / mass), &after);
        double afterSum = double(VU0_v3lengthsquare(&after)) + objectSpeedSquared;
        float afterSquared = float(afterSum);
        if (afterSum > 10.0) {
            double limit = speedsSquared * 1.25;
            if (afterSquared > limit) {
                double ratio = limit / afterSquared;
                float shrink = float(ratio);
                if (ratio < kMinimumBoxPushScale)
                    shrink = kMinimumBoxPushScale;
                VU0_v4scale(&impulse, shrink, &impulse);
            }
        }
    }

    Coord4 turn = {};
    VU0_v4crossprodxyz(&lever, &impulse, &turn);
    turn.y = float(turn.y * kTwentieth);
    VU0_v4sub(&angularMomentum, &turn, &angularMomentum);
    VU0_v4sub(&momentum, &impulse, &momentum);
    VU0_v4scale(&momentum, float(1.0 / mass), &velocity);
    VU0_MATRIX4_vect3rotate(&angularMomentum, &info->worldInverseInertia, &angularVelocity);

    double strength = Abs(scale);
    strength /= Rigid_TOTAL_FORCE_LIMIT;      // (the original first multiplies by 1)
    impact->strength = strength < 1.0 ? float(strength) : 1.0f;
    return true;
}

// The body's box against the box of every other body after `index` in the Simulation's table that is awake or
// asleep (not frozen) and within reach: ResolveCollision's response, then damage to both owners and an ECollision
// event. A body whose info says so is woken instead.
// FUNC_AT(0x000b0510)
void RigidBody::CollideWithObject(int index, int unknown) {
    (void)unknown;
    RigidScratchPadFields *scratch = RigidScratchPad;
    scratch->bodyBoxValid = false;
    for (unsigned i = index + 1; i < kMaxPhysicsObjects; i++) {
        if (PhysicsObjects[i] == NULL)
            continue;
        RigidBody *other = Simulation_GetRigidBody(Sim, 0, i);
        if (other->sleepState == kFrozen || other->sleepState == 0)
            continue;
        double reach = double(other->radius) + radius;
        double reachSquared = reach * reach;
        float limit = 1.0 > reachSquared ? 1.0f : float(reachSquared);
        if (!(VU0_v3distancesquare(&position, &other->position) < limit))
            continue;

        if (!scratch->bodyBoxValid) {
            RigidBodyInfo *bodyInfo = info;
            if (kind == kPlayerKind && RigidVehicles[ownerIndex]->GetIsInTwoWheelMode()) {
                Coord4 halfExtents = bodyInfo->halfExtents;
                halfExtents.x *= kWheelieHalfWidth;
                halfExtents.y *= 0.25f;
                scratch->body.Reset(&bodyInfo->orientation, Position4(this), &halfExtents);
            } else {
                scratch->body.Reset(&bodyInfo->orientation, Position4(this), &bodyInfo->halfExtents);
            }
            scratch->bodyBoxValid = true;
        }
        scratch->other.Reset(&other->info->orientation, Position4(other), &other->info->halfExtents);
        if (!scratch->body.CheckOBBOverlapAndFindIntersection(&scratch->other))
            continue;

        float depth = -scratch->body.penetration;
        const Coord4 *point = &scratch->body.contactPoint;
        CollisionImpact impact;
        CollisionImpact_Construct(&impact, 0);
        if (!ResolveCollision(this, other, &scratch->body.normal, point, depth, &impact))
            continue;
        if (info->unknown4fc || other->info->unknown4fc) {
            if (info->unknown4fc)
                sleepState = kAwake;
            if (other->info->unknown4fc)
                other->sleepState = kAwake;
            continue;
        }

        float strength = impact.strength;
        float damageA = strength;
        float damageB = strength;
        PhysicsObject *ownerA = PhysicsObjects[ownerIndex];
        if (strength > 0.0f)
            ownerA->SetInShock(strength);
        PhysicsObject *ownerB = PhysicsObjects[other->ownerIndex];
        impact.kindA = kind;
        impact.ownerA = ownerIndex;
        if (strength > 0.0f)
            ownerB->SetInShock(strength);
        impact.ownerB = other->ownerIndex;
        impact.kindB = other->kind;
        if (kind < kFirstObjectKind)
            damageA = 2.0f * strength;
        if (other->kind < kFirstObjectKind)
            damageB = 2.0f * strength;
        if (!(strength > kTwentieth))
            continue;

        Coord3 contact = { point->x, point->y, point->z };
        float damage = float(double(impact.damageScaleA) * Rigid_DamageScale * damageA);
        if (kind < kFirstObjectKind) {
            damage *= 3.0f;
            if (other->kind == kPlayerKind)
                RigidVehicles[ownerIndex]->AddDamageByPlayer(damage);
        }
        if (damage > 0.0f)
            impact.unknown4a = uint16_t(ownerA->ApplyDamageVirtual(&contact, &position, damage,
                                                                   Rigid_ObjectDamageFactor, 1, DamageSourceSig));
        damage = float(double(impact.damageScaleB) * Rigid_DamageScale * damageB);
        if (other->kind < kFirstObjectKind) {
            damage *= 3.0f;
            if (kind == kPlayerKind)
                RigidVehicles[other->ownerIndex]->AddDamageByPlayer(damage);
        }
        if (damage > 0.0f)
            impact.tag = uint16_t(ownerB->ApplyDamageVirtual(&contact, &other->position, damage,
                                                             Rigid_ObjectDamageFactor, 1, DamageSourceSig));
        RaiseCollisionEvent(impact);
    }
}

// The body against the world, once a step for a player's body, otherwise every step it is fast, every other step
// at walking pace, every fourth slower: its box's eight corners (and for a vehicle three points round it - ahead,
// and either side) swept along its velocity for a step from its centre, against the world and its barriers
// (through the owner's collider) and, for the player's body, the collision objects' cylinders; each hit gives an
// impulse (GenerateImpulse), world damage, stimuli to the hit instance's animation and an ECollision event. Then,
// for the player's body, its box against each collision object's box near it.
// FUNC_AT(0x000b1420)
void RigidBody::CollideWithWorld() {
    info->unknown4fd = 0;
    if (!(flags & kFlag0))
        return;

    float speedSquared = VU0_v3lengthsquare(&velocity);
    int8_t stepMask = speedSquared < 4.0f ? 3 : speedSquared < 225.0f ? 1 : 0;

    ObjectList cylinders, boxes;
    cylinders.first = NULL;
    cylinders.last = NULL;
    cylinders.end = NULL;
    boxes.first = NULL;
    boxes.last = NULL;
    boxes.end = NULL;
    if (kind == kPlayerKind)
        fgCollisionMgr->GetObjectLists(&cylinders, &boxes, &position, radius);

    bool wantInstances = kind < kFirstObjectKind && RigidVehicles[ownerIndex]->GetPhysics()->unknownC0 == 1;
    fgCollisionMgr->barrierMask = kBodyBarrierMask;
    PhysicsObject *owner = PhysicsObjects[ownerIndex];
    uint32_t mask = WCollider::kCollideBarriers | (wantInstances ? WCollider::kCollideInstances : 0);
    if (owner->collider == NULL) {
        void *memory = UMemory::FastAlloc(sizeof(WCollider), "WCollider");
        owner->collider = memory != NULL ? static_cast<WCollider *>(memory)->Construct(&position, radius, false, mask)
                                         : NULL;
    }
    owner->collider->Refresh(&position, radius);
    fgCollisionMgr->barrierMask = kDefaultBarrierMask;

    // The last lever arm worked out against the world: the box hits below hand it to CalculateAndApplyWorldDamage
    // too. Where nothing set it (no world hit before, or not a vehicle) the original passes what its stack held.
    Coord4 arm = {};
    CollisionImpact impact;
    bool wallChecked = false;
    if (kind == kPlayerKind || (SimStepCount & stepMask) == 0) {
        const Coord4 *corner = info->corners[info->cornerBank];
        if (kind < kFirstObjectKind)
            RigidVehicles[ownerIndex]->SetAgainstWallFlag(false);
        Coord4 origin = *Position4(this);
        int pointCount = kind < kFirstObjectKind ? kBoxCorners + 3 : kBoxCorners;
        for (int i = 0; i < pointCount; i++, corner++) {
            Coord4 point = {};
            if (i < kBoxCorners)
                VU0_v3add(corner, &origin, &point);
            else if (i == kBoxCorners)
                VU0_v4scaleadd(MatrixRow(&info->orientation, 2), info->halfExtents.z + kTwentieth, &position, &point);
            else if (i == kBoxCorners + 1)
                VU0_v4scaleadd(MatrixRow(&info->orientation, 0), info->halfExtents.x, &position, &point);
            else
                VU0_v4scaleadd(MatrixRow(&info->orientation, 0), -info->halfExtents.x, &position, &point);
            VU0_v4scaleadd(&velocity, SimTimeStep, &point, &point);

            Coord4 segment[2] = { origin, point };
            WorldCollisionInfo hit = {};
            hit.point = DefaultVector;
            fgCollisionMgr->barrierMask = kBodyBarrierMask;
            bool collided = owner->collider->GetWorldNormal(segment, &hit) ||
                            fgCollisionMgr->GetCylObjectCollision(segment, &cylinders, &hit);
            fgCollisionMgr->barrierMask = kDefaultBarrierMask;
            if (!collided)
                continue;
            if (hit.faceType == kMissionEventFace) {
                SMissionManager_ProgrammerDefinedEvent(glbMissionManager, 0, kMissionEventFaceHit, "");
                continue;
            }

            // the player's car is against a wall when a front corner or the point ahead hits a face it is not
            // driving away from, unless it is going faster than 7.5
            if (!wallChecked && kind == kPlayerKind && ((i > 1 && i < 6) || i == kBoxCorners)) {
                if (VU0_v3length(&velocity) > 7.5f)
                    wallChecked = true;
                else if (v3dotprod(MatrixRow(&info->orientation, 2), &hit.normal) <= 0.0f)
                    RigidVehicles[ownerIndex]->SetAgainstWallFlag(true);
            }

            hit.normal.w = 1.0f;
            Coord4 penetration = {};
            VU0_v4sub(&segment[1], &hit.point, &penetration);
            if (!(kind < kFirstObjectKind && RigidVehicles[ownerIndex]->GetPhysics()->unknownC0 == 1))
                penetration.y = 0.0f;
            float push = VU0_v3length(&penetration);
            float speedScale;
            if (i > kBoxCorners) {
                speedScale = 0.25f;
                push *= 0.25f;
            } else {
                speedScale = 1.0f;
            }

            if (kind < kFirstObjectKind) {
                // the point's height above the centre is taken off, and a lift put on, at the point the impulse
                // acts at
                VU0_v4sub(&point, &origin, &arm);
                float height = v3dotprod(&arm, MatrixRow(&info->orientation, 1));
                float lift;
                if (VU0_v3length(&velocity) < 5.0f) {
                    lift = Rigid_CornerLift;
                } else {
                    lift = float((double(VU0_v3length(&velocity)) - 5.0f) * kTenth);
                    if (lift < 0.5f)
                        lift = 0.5f;
                    float facing = v3dotprod(&hit.normal, MatrixRow(&info->orientation, 2));
                    if (facing < 0.0f)
                        facing = -facing;
                    lift = facing > kSteepFace ? lift * Rigid_CornerLift : 0.0f;
                }
                if (RigidVehicles[ownerIndex]->GetPhysics()->unknownC0 == 0)
                    point.y = float((double(point.y) - height) + lift);
            }

            CollisionImpact_Construct(&impact, 0);
            impact.ownerA = ownerIndex;
            GenerateImpulse(&impact, &hit.normal, &point, push, true, hit.faceType, speedScale);
            impact.kindA = kind;
            if (i < kBoxCorners + 1)
                CalculateAndApplyWorldDamage(&point, impact.strength);
            if (kind < kFirstObjectKind && i < kBoxCorners)
                impact.unknown50 = i;

            if (hit.faceInstance != NULL && kind < kFirstObjectKind) {
                MATRIX4 *renderInstance = hit.faceInstance->GetRenderInstance();
                // (a render instance's matrix is its first member)
                RSceneObj *sceneObject = renderInstance == NULL ? NULL :
                    fgWorld->GetSceneObjFromInstance(reinterpret_cast<const CARP::Instance *>(renderInstance));
                if (sceneObject != NULL) {
                    Handle *animation = sceneObject->animHandle;
                    uint32_t step = SimStepCount;
                    Handle_ProcessStimuli(animation, 0, kStimulusHit, step, 0);
                    if (impact.strength >= 5.0f)
                        Handle_ProcessStimuli(animation, 0, kStimulusHit16, step, 0);
                    if (impact.strength >= 2.5f)
                        Handle_ProcessStimuli(animation, 0, kStimulusHit15, step, 0);
                    if (impact.strength >= 0.5f)
                        Handle_ProcessStimuli(animation, 0, kStimulusHit14, step, 0);
                    if (impact.strength >= kTenth)
                        Handle_ProcessStimuli(animation, 0, kStimulusHit13, step, 0);
                }
            }
            RaiseCollisionEvent(impact);
        }
    }

    RigidScratchPadFields *scratch = RigidScratchPad;
    scratch->bodyBoxValid = false;
    while (boxes.first != NULL && boxes.last - boxes.first != 0) {
        Coord3 objectVelocity;
        uint16_t faceTag = 0;
        OBB *box = fgCollisionMgr->PopObjectOBB(&boxes, &objectVelocity, &faceTag);
        if (!scratch->bodyBoxValid) {
            scratch->body.Reset(&info->orientation, Position4(this), &info->halfExtents);
            scratch->bodyBoxValid = true;
        }
        if (scratch->body.CheckOBBOverlapAndFindIntersection(box)) {
            Coord4 normal = scratch->body.normal;
            float depth = -scratch->body.penetration;
            CollisionImpact_Construct(&impact, 0);
            impact.ownerA = ownerIndex;
            ResolveWorldOBBCollision(&normal, &scratch->body.contactPoint, depth, &impact, &objectVelocity,
                                     kWorldBoxForceScale);
            CalculateAndApplyWorldDamage(&arm, impact.strength);
            RaiseCollisionEvent(impact);
        }
        if (box != NULL) {
            NullFunction();                 // ~OBB
            UMemory::FastFree(box, sizeof(OBB));
        }
    }
    DestroyVector(&boxes);
    DestroyVector(&cylinders);
}
