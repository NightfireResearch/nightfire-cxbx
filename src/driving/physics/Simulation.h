#ifndef DRIVING_PHYSICS_SIMULATION_H_
#define DRIVING_PHYSICS_SIMULATION_H_

// ---------------------------------------------------------------------------------------------------------------
// The Simulation (one, at 0x00233ff0) is not ported yet: the originals the ported code calls, each at its address,
// defined once here.
// ---------------------------------------------------------------------------------------------------------------

#include <stdint.h>

#include "../data/CoordConvert.h"       // Coord3
#include "../engine/CoreContainers.h"   // GameVector
#include "../engine/SimRandom.h"        // BitSet192

class Grenade;
class Missile;
class Shell;
struct PhysicsObject;
struct RigidBody;
struct SimpleRigidBody;

// The Simulation's methods (thiscall: the Simulation, then a dummy EDX)
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define Simulation_GetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int slot))0x000b2730)
#define Simulation_FindPhysicsObjectSignature ((PhysicsObject *(__fastcall *)(void *, int, uint32_t signature))0x000b27d0)
#define Simulation_GetPlayerObject ((PhysicsObject *(__fastcall *)(void *, int))0x000b2d30)
// The body by its place in the Simulation's order: the rigid bodies (0-63), then the simple ones (64-159)
#define Simulation_GetOrderedBody ((PhysicsObject *(__fastcall *)(void *, int, int index))0x000b28d0)
#define Simulation_DetonateRemoteMines ((void (__fastcall *)(void *, int))0x000b2c10)
// A jump to the body at 0x000b4250, which a few callers (Smackable) call directly
#define Simulation_DeletePhysicsObject ((void (__fastcall *)(void *, int, PhysicsObject *))0x000b4740)
#define Simulation_DeletePhysicsObjectDirect ((void (__fastcall *)(void *, int, PhysicsObject *))0x000b4250)
// An explosion of the type (game/Explosion.h's ExplosionType) by the weapon (or shell type) at `position`, set off
// on `directHit` (or none) by the owner with the signature
#define Simulation_SpawnExplosion ((void (__fastcall *)(void *, int, int type, int weapon, const Coord3 *direction, const Coord3 *position, float size, int faceType, PhysicsObject *directHit, const uint32_t *ownerSig))0x000b4cd0)

// ---- its fields

// The bodies each simple body's collision check found (SimpleRigidBody::CheckCollisions' bits), by the simple body's
// slot
#define SimpleBodyHits ((BitSet192 *)0x00234520)                        // [96]
// The live weapon objects
#define SimMissiles (*(GameVector<Missile *> *)0x00234e7c)
#define SimGrenades (*(GameVector<Grenade *> *)0x00234e9c)
#define SimShells (*(GameVector<Shell *> *)0x00234eac)

#endif // DRIVING_PHYSICS_SIMULATION_H_
