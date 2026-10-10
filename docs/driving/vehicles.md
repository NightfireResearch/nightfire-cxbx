# The driving engine's vehicles

The game tier's vehicle classes: PBondCar, every car the game drives (the player's and the AI's, the snowmobiles,
the submarine, cars carried along a spline), its base PVehicle with the car attributes and names, and PHelicopter.
The first game-tier subsystem ported, on top of the whole engine tier (physics, collision, cameras, audio,
animation, rendering, input). Ported on 10 October 2026 by five packages, then merged (`src/driving/game/`). Every
address is Driving.xbe's.

**Status:** 135 functions ported, including InitCarPhysics (0x0006f8e0, CarPhysics' attribute initialiser, in no
function list) and FUN_00061a60 (AVehicle::SetOrientation, the name ours). `game.vehicles` is 100% ours by bytes:
134 replaced, 59 dead (exception funclets, IsWheelOnGround's own `return 0` tail at 0x00066051, the second and third
compiled copies of GetCarColourVariation), 0 live. Five shadow tests compare the ports with the originals on copies
of the live cars and helicopters, and lockstep runs of missions 1-8 to tick 9000, idle and with the accelerator
held, match the pre-port baseline frame for frame and in their sound call traces (every car on screen runs on
this code). The shadows of packages A and B also have a live mode (`=3`) that compares the original and the port
on every real call of the snowmobile, submarine and spline physics as the game makes it.

## Layout

| File | What |
|---|---|
| `game/Vehicle.h/.cpp` | PVehicle (0x74: a PhysicsObject with the mission editor's switch masks; its 83-slot vtable as `PVehicle::Slot`, and inline calls through it for the slots the engine uses), CarPhysics (the 0x100 "CarPhysics" structure, every field named after its attribute), BondCarControl, DamageZone and the shared damage levels, the car name list and its map (CarNameMap, the data layer's tree), PhysicsData::Construct |
| `game/Helicopter.h/.cpp` | PHelicopter (0x140, a PhysicsObject of type 10 on a simple body, not a PVehicle): construction, the controls from its action queue, Simulate, ApplyDamage |
| `game/BondCar.h` | PBondCar (0x420, vtable 0x0018f580) with every method, its car classes, the wheel inputs, and Simulate's scratch pad (BondCarScratch) |
| `game/BondCarSimulate.cpp` | the constructor, InitAudioObject (the engine sound for the class and type), Simulate (the per-step driver) and ApplyDamage |
| `game/BondCarState.h/.cpp` | the cars' tuning globals (`BondCar_*`, loaded by InitializeBondCarGlobals) and the ground kinds; tyre tracks, controls and variables, damage, shock, ResetCar, the RPM, laser, EMP and rocket boost, the target beacon, glares and turn signals, the two-wheel stunt |
| `game/BondCarSnowmobile.h/.cpp` | ProcessSnowmobilePhysics, AddSnowmobileForces, ImproveLanding |
| `game/BondCarModes.h/.cpp` | ProcessSubmarinePhysics (thrust, levers, roll and bob, the soft zones' push), ProcessSplinePhysics |
| `game/BondCarPhysics.h/.cpp` | GetControllerInput, the wheel forces, ProcessPhysics (the player's) and ProcessSimplePhysics (the AI's), the two-wheel stunt, tyre tracks, ResetCar(bool), ChangeCarType, SetVisualDamage, the deleting destructor, RaiseLandingImpact |
| `game/BondCarBasics.h/.cpp` | DebugObject, the destructor, GetCarColourVariation and the accessors |
| `game/SoftZone.h/.cpp` | the 19 soft zones and the point-in-quad test |
| `game/VehicleSound.h`, `AIVehicle.h`, `Missile.h` | provisional views of classes not ported: AVehicle (the engine sound, with ATrafficVehicle and AHelicopter), AIGroundVehicle, Missile |

The engine uses these types directly: physics calls a body's owner through `PVehicle` and reads `CarPhysics`; the cameras read `AVehicle`'s flags; the action ids are one enum,
`GameAction` in engine/ActionQueue.hpp, named after the game's table of action names.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_BONDCARSHADOWA` | first tick | BondCarState, BondCarSnowmobile and SoftZone on copies of the live cars: set-up, controls, damage, shock, ResetCar, the RPM, weapons, beacon, glares and turn signals, the two-wheel stunt, ImproveLanding, the snowmobile physics; the soft zones on points in and around them |
| `NIGHTFIRE_BONDCARSHADOWB` | first tick | BondCarModes and BondCarBasics: the submarine and spline physics on perturbed controls, timers and rolls, the accessors, GetCarColourVariation on every car name |
| `NIGHTFIRE_BONDCARSHADOWC` | first tick | BondCarPhysics on every live car in place: the accessors, SetVisualDamage, ResetCar, GetControllerInput with random queued actions, the two-wheel stunt, tyre tracks, both wheel-force methods, ProcessPhysics and ProcessSimplePhysics |
| `NIGHTFIRE_BONDCARSHADOWD` | first tick | Simulate and ApplyDamage on copies, every call beyond the car replaced by a recording fake (at the original's address and at our entry) |
| `NIGHTFIRE_VEHICLESHADOW` | first tick, then every tick | PVehicle's attribute lookups on every car name, the name list and map, PhysicsData's constructor, PHelicopter; the helicopter tests again the first tick a live helicopter exists |

Not covered by a shadow, tested in game: PBondCar's constructor, InitAudioObject, ChangeCarType and the destructor
(they make and free render, audio, AI and feedback objects).

## Simulate's scratch pad

Simulate leaves the address of a 0x450-byte block of its own stack frame in `BondCarScratchPad` for the methods it
calls, and writes none of it itself: the block holds whatever the stack held. Tracing every reader's accesses
against every writer's, along each Process* method with its callees, finds three reads of bytes nothing in the step
wrote:

- ProcessSimplePhysics and ProcessSnowmobilePhysics copy `rollingResistance` (+0x1d0, written only by
  ProcessPhysics) into `rollingResistance4` (+0x160), which neither they nor their wheel-force methods read.
- ImproveLanding copies `up` whole into the landing matrix's second row, so the row's w is the pad's +0x3cc; the
  matrix then only goes to VU0_m4toquat, which reads the 3x3 part.

None of them reaches a result, so the port does not depend on the stack's leftovers.

## What the port taught

The first test round diverged on six of the eight missions; nine port bugs later every one matched. Each was a
misreading of the listing, found by a shadow or by the live mode, and only the lockstep runs showed that they
mattered:

- **The wrong vector.** ProcessPhysics scaled the rolling resistance by the steered heading; the original scales the
  body's level velocity, a stack slot three pushes down. ProcessSnowmobilePhysics dotted the forward vector with the
  body's velocity instead of the pad's levelled copy, which differs only on slopes. ProcessSplinePhysics added the
  path position to the wheels where the original adds the placed position: it copies the path position into the
  stack slot that is also its frame matrix's translation row, so the following multiply carries it through the
  placement.
- **Swapped or misplaced operands.** The submarine's yaw and pitch speed scales (2.5 and 3.0) were swapped, and a
  push was zeroed by the slow-turn flag where the original tests the interior view, both slots two pushes apart.
- **A misread constant.** ResetDamage and the constructor set 175 hit points; the original stores 700, so every car
  died four times sooner.
- **A wrong bit.** Simulate passed the mission editor's effect switches to TriggerFX as `1 << effect`; the original
  passes `1 << (effect + 16)`.
- **Rounding.** The boat's and the submarine's bob and the submarine's roll test round an FSIN result to a float that
  the original keeps on the x87 stack (platform/RealMath.h's SineTurns and CosineTurns now read them as doubles, for
  every port that needs them).

And about testing: the first-tick shadows run before any car is on a slope, on a spline path or in the air, so they
passed while the game diverged; the live mode found the last three bugs within one run. NIGHTFIRE_RESTORE_RANGES
cannot isolate a package whose functions other ports call directly. Lockstep comparisons need
NIGHTFIRE_SNDLOCKSTEP: without it sound timing varies and mission 5 did not repeat itself.

## Odd things in the original, kept

- ProcessSnowmobilePhysics, ProcessPhysics and ProcessSimplePhysics each raise a hard landing's collision event
  with the same inlined code; it is one function here (RaiseLandingImpact).
- InitAudioObject builds "SFX_<engine>LdEn" in a local buffer and never uses it; Simulate asks for the car's class
  twice and drops the first answer; the mission editor's switches test only bit 0 of each mask, shifted.
- The gear bytes are signed: `previousGear` starts at -1, and CalculateRPM keeps the previous gear while
  `gearChangeTimer` (15 steps from a change) runs. The reverse timer, the submarine's roll and the forced roll
  direction are compared signed too.
- RVehicle::ComputeWheels asks for the wheel spin angles of 0 and 1 only: one for the front pair, one for the rear.
- Two of the tuning block's values, the steps in the air before ImproveLanding (50) and before it damps the spin
  (10), are never written, so they are constants here; the squared distance beyond which class 3 cars simulate
  every other step (2500) is read as the global it is.
- SetVisualDamage compares against the shared damage levels (0x001c3db4) that ApplyDamage uses, of which
  InitializeCarVariables clears `spread` - the threshold above which damage spreads to the zones beside the hit.
- RigidBody::InitLevers divides a body's mass by BASE_FRICTION_MASS, the cars' tuning global, not a constant 8.
- PHelicopter's ApplyDamage gathers the zones at damage stage 4 and does not send them; its Simulate computes a dot
  product it does not use.
- AttributeSystem::ConfigEditParameters is empty in the retail game; InitializeGlobals still registers each
  CarPhysics field's editor range.
