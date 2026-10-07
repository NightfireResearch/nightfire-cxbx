#ifndef DRIVING_PHYSICS_RIGIDBODYRESOLVE_H_
#define DRIVING_PHYSICS_RIGIDBODYRESOLVE_H_

// ---------------------------------------------------------------------------------------------------------------
// RigidBody's collision response (RigidBodyResolve.cpp): ResolveCollision, the exchange of momentum between two
// bodies that touch, and GenerateImpulse, one body's impulse against the world (or an event's push). Both describe
// what happened in a CollisionImpact, which the caller then uses for damage and effects.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "RigidBody.h"

// What a collision did (0x54 bytes; the name is ours). FUN_0003dc20 constructs it: the three vectors (0, 0, 0, 1),
// the rest zero except +0x38..+0x3f, which it leaves, and unknown50, -1. CollideWithObject fills the two sides with
// the bodies' kinds and owners; GenerateImpulse makes the body side 1 and the other 6.
struct CollisionImpact {
    Coord4 point;               // +0x00
    Coord4 normal;              // +0x10
    Coord4 relativeVelocity;    // +0x20 GenerateImpulse's: the body's velocity at the point
    float damageScaleA;         // +0x30 ResolveCollision's, for each body (CollideWithObject scales the damage it
    float damageScaleB;         // +0x34 applies to the body's owner by it)
    uint8_t unknown38[8];
    float closingSpeed;         // +0x40 along the normal
    float strength;             // +0x44 0 to 1
    uint8_t kindA;              // +0x48
    int8_t ownerA;              // +0x49
    uint16_t unknown4a;         // +0x4a 0x10 from GenerateImpulse; in CollideWithObject, owner A's ApplyDamage answer
    uint8_t kindB;              // +0x4c
    int8_t ownerB;              // +0x4d
    uint16_t tag;               // +0x4e GenerateImpulse's argument; in CollideWithObject, owner B's ApplyDamage answer
    int32_t unknown50;          // +0x50 CollideWithWorld: the corner of the box that hit (a vehicle's)
};
static_assert(sizeof(CollisionImpact) == 0x54, "a collision impact is 84 bytes");
static_assert(offsetof(CollisionImpact, damageScaleA) == 0x30 && offsetof(CollisionImpact, closingSpeed) == 0x40 &&
              offsetof(CollisionImpact, kindA) == 0x48 && offsetof(CollisionImpact, tag) == 0x4e,
              "collision impact layout");

#endif // DRIVING_PHYSICS_RIGIDBODYRESOLVE_H_
