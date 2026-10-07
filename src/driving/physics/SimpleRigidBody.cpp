#pragma fp_contract(off)

#include "SimpleRigidBody.h"

#include "PhysicsObject.h"
#include "RigidBody.h"
#include "../../helpers.h"
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/OBB.h"
#include "../platform/RealMath.h"
#include "../platform/RealPrint.h"        // MEM_fill

// ---------------------------------------------------------------------------------------------------------------
// SimpleRigidBody (0x000b1e10-0x000b25e0), ported from the listing. The x87 arithmetic is the original's: chains in
// double in its order, rounded where it stores to a float; the comparisons keep its sense for NaNs.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define Simulation_GetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int slot))0x000b2730)

// ---- globals
#define Sim ((void *)0x00233ff0)                    // the Simulation
#define SimTimeStep FLOAT_AT(0x00234e30)            // the simulation's step, in seconds

namespace {

constexpr int kRigidBodies = 64;
constexpr int kSimpleBodies = 96;

// The checking body's owner when the other body is a helicopter's (a class derived from PhysicsObject; only the
// byte read here)
struct HelicopterTestOwner : PhysicsObject {
    uint8_t unknown6c[0xcd];
    uint8_t unknown139;         // +0x139 zero: the helicopter's body is tested 3 higher
};
static_assert(offsetof(HelicopterTestOwner, unknown139) == 0x139, "owner layout");

void SetHit(uint64_t *hits, int bit) {
    hits[bit / 64] |= uint64_t(1) << (bit % 64);
}

} // namespace

// FUNC_AT(0x000b1e10)
SimpleRigidBody* SimpleRigidBody::Construct(int slot, uint8_t bodyType, const Coord3 *position,
                                            const Coord3 *velocity, const Coord3 *acceleration,
                                            const MATRIX4 *orientation, float radius, float mass) {
    this->position = *position;
    this->bodyType = bodyType;
    this->slot = slot;
    flags = 0;
    this->velocity = *velocity;
    this->radius = radius;
    this->acceleration = *acceleration;
    this->mass = mass;
    VU0_m4toquat(&this->orientation, orientation);
    return this;
}

// FUNC_AT(0x000b1e90)
void SimpleRigidBody::Destruct() {
    MEM_fill(this, 0, sizeof(SimpleRigidBody));
}

// FUNC_AT(0x000b1ea0)
PhysicsObject* SimpleRigidBody::GetOwner() {
    return SimpleBodyOwners[slot];
}

// FUNC_AT(0x000b1eb0)
void SimpleRigidBody::RecalcOrientMat(MATRIX4 *matrix) {
    VU0_quattom4(matrix, &orientation);
}

// FUNC_AT(0x000b1ec0)
void SimpleRigidBody::GetForwardVector(Coord3 *forward) {
    VU0_ExtractZAxis3FromQuat(&orientation, forward);
}

// FUNC_AT(0x000b1ee0)
void SimpleRigidBody::GetRightVector(Coord3 *right) {
    VU0_ExtractXAxis3FromQuat(&orientation, right);
}

// FUNC_AT(0x000b1f00)
void SimpleRigidBody::SetOrientMat(const MATRIX4 *matrix) {
    VU0_m4toquat(&orientation, matrix);
}

// FUNC_AT(0x000b1f10)
void SimpleRigidBody::SetOrientation(const Coord4 *quaternion) {
    orientation = *quaternion;
}

// FUNC_AT(0x000b1f30)
float SimpleRigidBody::GetScalarVelocity() {
    return VU0_v3length(&velocity);
}

// FUNC_AT(0x000b1f40)
void SimpleRigidBody::Accelerate(const Coord3 *acceleration) {
    if (flags & kMoves) {
        double step = SimTimeStep;
        velocity.x = (float)(step * acceleration->x + velocity.x);
        velocity.y = (float)(step * acceleration->y + velocity.y);
        velocity.z = (float)(step * acceleration->z + velocity.z);
    }
}

// FUNC_AT(0x000b1f80)
void SimpleRigidBody::UpdatePosition() {
    if ((flags & kFlag04) && (flags & kMoves)) {
        VU0_v4scale(&velocity, 0.99f, &velocity);
        VU0_v4scale(&acceleration, 0.98f, &acceleration);
    }
    if (flags & kMoves) {
        double step = SimTimeStep;
        position.x = (float)(step * velocity.x + position.x);
        position.y = (float)(step * velocity.y + position.y);
        position.z = (float)(step * velocity.z + position.z);
    }
}

// FUNC_AT(0x000b1fe0)
bool SimpleRigidBody::NeedsCollisionCheck() {
    return (flags & kCollisionChecks) != 0;
}

// A body is near when the distance between the centres is less than the two radii (this body's at least 0.25);
// a box test then puts a box round this body two radii wide and long along the velocity - (2r, 2r, 2r x speed)
// against a rigid body's own box, (r, r, r x speed) against a simple body's collision bounds.
// FUNC_AT(0x000b1ff0)
void SimpleRigidBody::CheckCollisions(uint64_t *hits) {
    float reach = radius < 0.25f ? 0.25f : radius;
    float speed = VU0_v3length(&velocity);

    if (flags & (kNearRigidHits | kBoxRigidHits)) {
        for (int i = 0; i < kRigidBodies; i++) {
            if (PhysicsObjects[i] == NULL)
                continue;
            RigidBody *body = Simulation_GetRigidBody(Sim, 0, i);
            if (body->sleepState == RigidBody::kFrozen)
                continue;
            alignas(16) Coord4 offset;
            VU0_v4sub(&position, &body->position, &offset);
            double reachBoth = (double)body->radius + reach;
            if (!(VU0_v3lengthsquare(&offset) < reachBoth * reachBoth))
                continue;
            if (flags & kNearRigidHits) {
                SetHit(hits, i);
                continue;
            }

            RigidBodyInfo *info = body->info;
            alignas(16) OBB bodyBox;
            bodyBox.Construct();
            alignas(16) OBB ownBox;
            ownBox.Construct();
            alignas(16) MATRIX4 bodyMatrix = info->orientation;
            alignas(16) MATRIX4 ownMatrix;
            VU0_quattom4(&ownMatrix, &orientation);
            alignas(16) Coord4 bodyPosition;
            Float_COORD3toCOORD4(&bodyPosition, &body->position);
            alignas(16) Coord4 ownPosition;
            Float_COORD3toCOORD4(&ownPosition, &position);
            alignas(16) Coord4 bodyExtents = info->halfExtents;
            alignas(16) Coord4 ownExtents;
            ownExtents.x = reach + reach;
            ownExtents.y = reach + reach;
            ownExtents.z = (float)((double)speed * reach * 2);
            ownExtents.w = 1.0f;
            bodyBox.Reset(&bodyMatrix, &bodyPosition, &bodyExtents);
            ownBox.Reset(&ownMatrix, &ownPosition, &ownExtents);
            if (ownBox.CheckOBBOverlap(&bodyBox))
                SetHit(hits, i);
            NullFunction();   // ~OBB
            NullFunction();   // ~OBB
        }
    }

    if (flags & (kNearSimpleHits | kBoxSimpleHits)) {
        for (int i = 0; i < kSimpleBodies; i++) {
            if (i == slot || SimpleBodyOwners[i] == NULL)
                continue;
            SimpleRigidBody *body = Simulation_GetSimpleRigidBody(Sim, 0, i);
            if (body->flags & kFlag01)
                continue;
            Coord3 centre = body->position;
            float bodyRadius = body->radius;
            if (body->bodyType == kSimpleHuman)
                centre.y = centre.y + 0.5f;
            if (body->bodyType == kSimpleHelicopter &&
                static_cast<HelicopterTestOwner *>(SimpleBodyOwners[slot])->unknown139 == 0)
                centre.y = centre.y + 3.0f;
            alignas(16) Coord4 offset;
            VU0_v4sub(&position, &centre, &offset);
            double reachBoth = (double)bodyRadius + reach;
            if (!(VU0_v3lengthsquare(&offset) < reachBoth * reachBoth))
                continue;
            if (flags & kNearSimpleHits) {
                SetHit(hits, kRigidBodies + i);
                continue;
            }

            alignas(16) OBB ownBox;
            ownBox.Construct();
            alignas(16) OBB bodyBox;
            bodyBox.Construct();
            alignas(16) MATRIX4 ownMatrix;
            VU0_quattom4(&ownMatrix, &orientation);
            alignas(16) MATRIX4 bodyMatrix;
            VU0_quattom4(&bodyMatrix, &body->orientation);
            alignas(16) Coord4 ownPosition;
            Float_COORD3toCOORD4(&ownPosition, &position);
            alignas(16) Coord4 bodyPosition;
            Float_COORD3toCOORD4(&bodyPosition, &centre);
            alignas(16) Coord4 ownExtents;
            ownExtents.x = reach;
            ownExtents.y = reach;
            ownExtents.z = speed * reach;
            ownExtents.w = 1.0f;
            alignas(16) Coord4 bodyExtents;
            if (!SimpleBodyOwners[i]->GetCollisionBounds(&bodyExtents)) {
                bodyExtents.x = bodyRadius;
                bodyExtents.y = bodyRadius;
                bodyExtents.z = bodyRadius;
            }
            bodyExtents.w = 1.0f;
            ownBox.Reset(&ownMatrix, &ownPosition, &ownExtents);
            bodyBox.Reset(&bodyMatrix, &bodyPosition, &bodyExtents);
            if (ownBox.CheckOBBOverlap(&bodyBox))
                SetHit(hits, kRigidBodies + i);
            NullFunction();   // ~OBB
            NullFunction();   // ~OBB
        }
    }
}

// FUNC_AT(0x000b25c0)
void SimpleRigidBody::SetCanHitTrigger(bool canHit) {
    if (canHit)
        flags |= kTouchesTriggers;
    else
        flags &= ~kTouchesTriggers;
}
