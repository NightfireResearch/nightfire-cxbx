# The driving engine's physics

Rigid-body physics for every car, helicopter, boat and loose object, on top of the collision system
([collision.md](collision.md)): RigidBody (forces and torques, levers, integration, sleeping, collision with the
ground, the world's boxes and other bodies, impulses, damage), SimpleRigidBody (a light point body for explosions,
people, missiles, shells, grenades and the like), PhysicsObject (the game object that owns a body), PhysicsNamespace
and Newton (objects spawned from events and simulated loose). Ported on 7 October 2026 by five packages, then merged
(`src/driving/physics/`). Every address is Driving.xbe's.

**Status:** 95 functions ported (with the OBB and Util_* helpers the collision port brought, the whole subsystem). `engine.physics` is 100% ours by bytes; the ten functions still counted live are
exception funclets at 0x15xxxx filed by address. Checked by five shadow tests against the originals on copies of the
live bodies (missions 1 and 4) and by lockstep runs of missions 1-8, every dumped frame identical to the baseline.

## Layout

| File | What |
|---|---|
| `physics/RigidBody.h` | the shared layouts: RigidBody (0x80: orientation quaternion, position, velocities, momenta, mass, the inverse inertia's diagonal in the vectors' w words, sleep state, kind), RigidBodyInfo (0x500: orientation and world inverse inertia matrices, 16 levers, corners, the levers' WWorldPos and ground), the vehicle view of the owner's virtual methods (RigidVehicle, named after PBondCar's overrides), the simulation's scratch pad, the tuning floats named after their attributes |
| `physics/RigidBody.cpp` | construction, reset, initial forces, levers and ground collision, sleep control, integration |
| `physics/RigidBodyBasics.cpp` | the small methods: InitRigidBodySystem, local/world conversions, damping, friction, force and torque resolution, zones, world damage, ground height, InitLevers |
| `physics/RigidBodyResolve.h/.cpp` | collision response: ResolveCollision, GenerateImpulse, and the CollisionImpact record (0x54) they fill |
| `physics/RigidBodyCollide.cpp` | collision detection: CollideWithWorld, CollideWithObject, ResolveWorldOBBCollision |
| `physics/PhysicsObject.h/.cpp` | PhysicsObject (0x6c): hit points, damage zones, render/audio/feedback links, ownership |
| `physics/SimpleRigidBody.h/.cpp` | SimpleRigidBody (0x40) and its collision checks |
| `physics/PhysicsNamespace.cpp`, `Newton.cpp` | name lookup and the physics data record; spawned objects |
| `physics/PhysicsMath.h` | the game's fabs, min and max helpers (0x0001b160, 0x0001c600, 0x0001c620), written inline |
| `engine/MissionManager.h` | the fields of SMissionManager the physics, targeting and game loop read |

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_PHYSOBJSHADOW` | first tick | every SimpleRigidBody method, CheckCollisions around the live bodies, every PhysicsObject query and (on copies, side effects recorded) its writes, constructors and destructors, PhysicsNamespace |
| `NIGHTFIRE_RIGIDBASICSSHADOW` | first tick | the small methods on copies of every live body, as found and perturbed (kind, sleep state, NaNs); ground height on and off the track; InitLevers with 0-20 collision points; Newton::Simulate on a stand-in |
| `NIGHTFIRE_RIGIDCORESHADOW` | first tick | the ten core functions on copies of the live bodies with perturbed contacts, sleep state, velocities, orientation, gravity and the vehicle's fields |
| `NIGHTFIRE_RIGIDRESOLVESHADOW` | first tick | ResolveCollision on every ordered pair of live bodies, GenerateImpulse 60 times per body, PlayAnimation recorded |
| `NIGHTFIRE_RIGIDCOLLIDESHADOW` | first tick | the three detection functions on perturbed copies, the other packages' functions replaced by recording fakes, every body, the scratch pad and the collision manager compared |

Construction at spawn, Newton's spawning and deletion, and whole-frame behaviour are covered by the lockstep runs:
every car's motion goes through RigidBody every tick.

## What the port taught

- **MSVC turns a branch into fabs.** The game's fabs (0x0001b160) compares and negates, so -0.0 stays -0.0. The
  same C++, `x < 0 ? -x : x`, compiles under /O2 to an ANDPS that clears the sign: GenerateImpulse's impact
  strength came out +0.0 where the original stored -0.0. `PhysicsMath.h`'s Abs flips the sign bit explicitly; the
  other sites written that way were checked against their listings (where only a comparison follows, the sign of
  zero cannot escape and they stay).
- **Stack leftovers, two more.** VU0_MATRIX3x4_mult writes three rows, so Construct and ResetObject copy stack
  leftovers into row 3 of the world inverse inertia (nothing reads it; the port writes zeros, the shadow masks
  them). WWorldPos::FindClosestFace copies a candidate face whose first corner's count word is never written: the
  port now zeroes the candidate, as collision.md always said it did.
- **A parameter the original writes through.** ResolveCollision's normal is not const: ModifyLevers overwrites it
  when only one body is a vehicle. A by-address call had hidden that behind a cast.

## Odd things in the original, kept

- UpdatePositionAndOrientation ignores its time-step argument and uses the global step. It builds the inertia
  matrix as transpose x scale x rotation with D3DX multiplies; the constructor and ResetObject build it the other
  way round with 3x4 multiplies.
- ControlSleep never puts an upright vehicle to sleep. ApplyForces ORs 1 into the word that also holds the body's
  radius; the flag tests read that bit.
- Newton::Simulate computes a midpoint, its distance and a clamped height and uses none of them, normalises the
  quaternion by its squared length, and finishes its step on a body it has just deleted.
- In CheckCollisions a helicopter is tested higher depending on a byte of the checking body's owner, and a body's
  own box is twice as large against rigid bodies as against simple ones.
- CollideWithWorld hands its box hits the lever arm left by the last vehicle world hit (stack garbage when there
  was none; the port passes zero).
