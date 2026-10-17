#ifndef DRIVING_PHYSICS_RIGIDBODYCOLLIDE_H_
#define DRIVING_PHYSICS_RIGIDBODYCOLLIDE_H_

// ---------------------------------------------------------------------------------------------------------------
// RigidBody's collision detection (RigidBodyCollide.cpp): CollideWithWorld, the body's box corners and three points
// round it swept along its velocity against the world, its barriers and the collision objects' cylinders, then its
// box against the collision objects' boxes; CollideWithObject, its box against every other body's after it in the
// Simulation's table; and ResolveWorldOBBCollision, the response to a collision object's box. Declared in
// RigidBody.h.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "RigidBody.h"
#include "RigidBodyResolve.h"           // CollisionImpact

#endif // DRIVING_PHYSICS_RIGIDBODYCOLLIDE_H_
