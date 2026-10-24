#ifndef DRIVING_PHYSICS_RIGIDBODYBASICS_H_
#define DRIVING_PHYSICS_RIGIDBODYBASICS_H_

#include <stddef.h>
#include <stdint.h>

#include "PhysicsObject.h"
#include "RigidBody.h"

// ---------------------------------------------------------------------------------------------------------------
// RigidBody's small methods (0x000acde0-0x000adc80): the system's tuning, the accessors and frame conversions,
// SetOrientation, resolving forces and torques, the zone, world damage, sleeping, the ground under a point and the
// levers. Declared in RigidBody.h; see RigidBodyBasics.cpp. Also here: two helpers in the same range that are not
// RigidBody's.
// ---------------------------------------------------------------------------------------------------------------

// std::allocator<T *>::deallocate, every pointer vector's (the linker folded the copies; FUN_000ad630): `first`
// is the vector's T ** block. A member of the vector that does not read `this`: the stdcall form pops the same two
// arguments.
void __stdcall PointerVectorDeallocate(void *first, uint32_t count);                           // 0x000ad630

// |value| * scale / limit, at most 1 (FUN_000ad650, ResolveCollision's; the name is ours). Unrounded.
double ClampedForceRatio(float value, float limit, float scale);                               // 0x000ad650

#endif // DRIVING_PHYSICS_RIGIDBODYBASICS_H_
