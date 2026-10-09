#ifndef DRIVING_PHYSICS_SIMULATION_H_
#define DRIVING_PHYSICS_SIMULATION_H_

// ---------------------------------------------------------------------------------------------------------------
// The Simulation (one, at 0x00233ff0) and PVehicle's car list are not ported yet: the originals the ported code
// calls, each at its address, defined once here.
// ---------------------------------------------------------------------------------------------------------------

#include <stdint.h>

struct PhysicsObject;
struct RigidBody;
struct SimpleRigidBody;

// The Simulation's methods (thiscall: the Simulation, then a dummy EDX)
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define Simulation_GetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int slot))0x000b2730)
#define Simulation_FindPhysicsObjectSignature ((PhysicsObject *(__fastcall *)(void *, int, uint32_t signature))0x000b27d0)
#define Simulation_GetPlayerObject ((PhysicsObject *(__fastcall *)(void *, int))0x000b2d30)

// PVehicle's car names
#define PVehicle_GetNameCount ((uint32_t (*)())0x00071880)
#define PVehicle_NameToIndex ((int (*)(const char *name))0x000718a0)
#define PVehicle_GetCarNames ((const char *const *(*)())0x000718e0)

#endif // DRIVING_PHYSICS_SIMULATION_H_
