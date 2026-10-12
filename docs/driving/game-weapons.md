# The driving engine's weapons

The game tier's weapons: SWeaponManager, which fires the player's weapons, and the weapon objects - Missile,
Explosion, RayShell, Shell, Grenade, Mine, Sentry, Smackable and Human. They sit on physics, collision and the world
(docs/driving/physics.md, docs/driving/collision.md, docs/driving/world.md) and drive the gameplay audio and effects
(docs/driving/game-audio.md, docs/driving/game-effects.md), all ours. Ported on 12 October 2026 by four packages,
then merged (`src/driving/game/`). Every address is Driving.xbe's.

**Status: ported and tested (12 October 2026).** 100 functions, all `FUNC_AT`: 20 of SWeaponManager, 15 of Missile
and Explosion (with FUN_0005ce80, Explosion's scalar deleting destructor, and FUN_0005d3f0, a face's normal, now
`StripTriangle::GetNormal`), 30 of RayShell, Shell and Grenade, 35 of Sentry, Mine, Smackable and Human.
`game.weapons` is 100% ours by bytes: 100 replaced, 77 dead (exception funclets), 1 live - FUN_001542f0, an
exception funclet of SWeaponManager's (8 bytes) with no caller the tool can see. Four shadow tests compare the
ports with the originals on synthetic objects and copies of the live ones. Round gw3 (after the fixes below): all
16 lockstep runs (missions 1-8 to tick 9000, idle and accelerating) match the baselines exactly, frames and sound
traces, and the four shadows run to completion on every mission with 0 differences (A 6780 cases, B 11860, C 2092,
D 12300).

## Layout

| File | What |
|---|---|
| `game/WeaponManager.h/.cpp` | SWeaponManager (0x17c, one, `WeaponManager`): the weapon records (`WeaponRecord`, 0x54, a working copy of the weapon table), the selections (primary, secondary, special), the sorted selection list, the fire, select and reload actions from its own queue, FireWeapon (each shot a stimulus to one of the weapon's animation systems on the player car), reloading, deploying and stowing the systems, the turret, the nuclear blast sequence; `DrivingWeapon` (the weapon ids, from the table's names), `WeaponFlags` |
| `game/Missile.h/.cpp` | Missile (0x730, vtable 0x0018eef8): the rockets, missiles, torpedoes and the Q-Pulse - construction by weapon (the jump table at 0x0005f2fc), Render (the trail and its light), the drive missile's steering, homing, the wobble, CheckForCollision, Simulate, and the missiles' tuning globals |
| `game/Explosion.h/.cpp` | Explosion (0x98, vtable 0x0018ed58): a growing sphere that pushes, spins and damages each body it reaches once; `ExplosionType` |
| `game/Shell.h/.cpp` | RayShell (0xe0, vtable 0x0018ffe4): the instant shot - objects along the ray, then the world, in up to `passes` passes - with the static tracers, muzzle flash lights and the trigger queue (WTriggerManager's); Shell (0x90, vtable 0x0019006c): the moving round; and ApplyWorldEffects and ApplyWorldDamage, which the missiles and the EMP bolts use too |
| `game/Grenade.h/.cpp` | Grenade (0x6e0, vtable 0x0018ed98): a thrown grenade that bounces and explodes, with its smoke trail |
| `game/Sentry.h/.cpp` | Sentry (0xc8, vtable 0x00190034): turns towards its target two ways (its body, its aim animation) and fires; the static muzzle flashes |
| `game/Mine.h/.cpp` | Mine (0x88, vtable 0x0018ee28): time, proximity, remote and floating mines |
| `game/Smackable.h/.cpp` | Smackable (0x88, vtable 0x00190248): a scene object given a rigid body while it is hit and moves, handed back to its trigger once at rest; the "Dummy" PhysicsData |
| `game/Human.h/.cpp` | Human (0x90, vtable 0x0018edf4): the simple body an AI character drives; its hits go to the character |
| `game/AICharacter.h` | AICharacter and AICharacterHands, not ported: the one provisional view of what the weapons and the targeting read of them |

All the weapon objects derive straight from PhysicsObject; there is no common weapon base. `PhysicsObjectType`
(physics/PhysicsObject.h) names every class's type number.

Merged into their owners at integration: the weapon manager's views (anim/Weapon.cpp's last weapon fired,
camera/CameraIniLoader.cpp's and PlayerCameraA.cpp's slots, world/Targeting.cpp's, game/BondCarSimulate.cpp's ammo
and hands flag, Grenade's and Mine's weapon damage; engine/GameLoop.cpp's construction, destruction and
SetPlayerCar are direct calls); the weapon table's record (engine/StaticInit.h's `WeaponDefinition` is
`WeaponRecord`; `tools/static_init_driving.py` writes the table as `kWeaponDefinitions`); the mines' kinds are
`DrivingWeapon`'s; SMissionManager's fields in one view (engine/MissionManager.h: `missionState`, `autoDrive`,
`driveMissile`, `cinematicMode`, the sentry count at +0x500, and the methods the weapons call); the Simulation's
methods and fields the weapons use in one view (physics/Simulation.h: GetOrderedBody, DetonateRemoteMines,
DeletePhysicsObject and the jump to it most callers use, SpawnExplosion, `SimpleBodyHits`, and the missile, grenade
and shell vectors); AICharacter's views (Human.h's, Shell.cpp's, Targeting.cpp's) and AICharacterHands' in
game/AICharacter.h; the HUD's `paused` byte (engine/GameInterfaces.hpp then, BondCarPhysics' too; game/Hud.h's GHud since the front end's
port); the launch page's
`cheats` and `autoSwitch` (engine/GameLoop.h); ActiveRayShell's `damage` and `radius` (world/Trigger.h);
ProcAnimState's `damage` byte (world/World.h); the missile light types (`MissileLightType`, render/Lights.h); one
`Xyz` (data/CoordConvert.h). FUN_0005d3f0 is `StripTriangle::GetNormal` (world/CollisionTypes.h, beside `LowestY`
in world/CollisionQueries.cpp), called directly by the cameras, the collision queries, RigidBody, WWorldPos and
Grenade. The world camera draws the tracers, flashes, missiles, grenades and sentry muzzle flashes, the trigger
manager reads the ray queue, GameLoop resets the ray shells and the EMP bolts' sparks go through
Shell::ApplyWorldEffects, all by direct calls.

The globals only our code touches are ours: the missiles' tuning (0x001c3680-0x001c36a0, 0x001c36a8, 0x001c36ac),
the explosions' inverse scale (`ExplosionInverseScale`, 0x001e7a60, which a static initialiser set to 0.5), the
ray shells' tracers, flashes, trigger queue, counts and ApplyWorldEffects' statics, the sentry muzzle flashes, the
"Dummy" PhysicsData and its guard (`DummyPhysicsData`, destroyed by `DestroyStatic_001e9294`), and the weapon
manager's constant tables (WeaponSystems, WeaponSelectionOrder, WeaponRefireSeconds) as constants.
`tools/static_init_driving.py` clears our ray shell flashes and trigger queue (`NAMED_ARRAYS` now takes a record's
field at any offset) and leaves ExplosionInverseScale to its definition. Still at the game's address: the weapon
table (`WeaponDefinitions`, 0x001c94d8: only our code touches it, but shadow A compares the game's copy), 0x001c36a4
(other code reads it), the bytes AICharacterBond reads (`BondSelectPressed`, `BondFirePressed`), and the bullet
streak kinds.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GAMEWEAPONSSHADOWA` | first tick | SWeaponManager on perturbed copies of the live manager and its weapons: Reset, SetPlayerCar, LoadWeapon, UnloadWeapon, the selections and their order, DoWeaponAnim, GetWeaponFromSystem, FireWeapon for every weapon, UserReloading, UpdateTurret, the nuclear sequence, Update with random action queues; Construct and Destruct on blocks of their own |
| `NIGHTFIRE_GAMEWEAPONSSHADOWB` | first tick | synthetic only (no missile exists at the first tick; about 12,800 cases): StripTriangle::GetNormal on random triangles; Explosion's constructor (every type), destructors and Simulate; Missile's constructor (every weapon, every kind of owner), Render, the steering inputs, SetTarget, CheckForCollision, SteerMissile and Simulate |
| `NIGHTFIRE_GAMEWEAPONSSHADOWC` | first tick | every RayShell, Shell and Grenade function on synthetic objects in a faked Simulation; the world queries (CollideWithWorld, ApplyWorldDamage) against the live world round the player's car |
| `NIGHTFIRE_GAMEWEAPONSSHADOWD` | first tick | Sentry's ComputeNewRotation and ComputeTargetAngle on random inputs, the muzzle flashes, and Sentry, Mine, Smackable and Human methods on synthetic objects on free body slots (a sentry's settings copied from a live one where the mission has one); not the constructors, destructors and deleting destructors |

Each shadow swaps its package's entries back for the original's run; the fakes are hooked at the original's entry
and at our port (the ports call each other directly). The globals we own are lent to the game's copies before the
original runs and compared after. The mutations each was checked against (by reading): A, FireWeapon without the
clip-of-4 decrement after the missile's shot; B, SteerMissile's homing slerp fraction rounded as float(ramp) *
float(rate); C, Fire's second victim damaged at its own impact point, and ApplyWorldEffects building its frame from
x at a dot of at most 0.9; D, ComputeNewRotation's limit and 1 - limit swapped, and Smackable::ApplyDamage testing
the rounded hit. C may show a one-sided difference if the linker inlines a small faked function
(AttributeSet::Construct and Destruct, AIndex::Lookup).

In game: lockstep runs of missions 1-8, idle and accelerating, to tick 9000, frames and sound traces against the
baseline. They reach the weapon manager's construction, Reset, SetPlayerCar, Update (without actions),
SortWeaponList, UpdateTurret and HandleNuclearSequence; the face normal (every frame); the sentries placed at level
load (construction, Simulate, TrackTarget, the rotations, Shoot, the muzzle flashes); the humans the AI makes; and
the missiles, shells, ray shells, grenades and explosions the AI and the missions fire. The mission events reach
SetSpecialWeapon, UnloadWeapon, StartNuclearSequence and GetWeaponFromSystem. What needs input is for the shadows:
FireWeapon, DoWeaponAnim, SelectSecondary, GetPreviousSecondaryType, UserReloading and Update's actions
(GetNextSecondaryType only through the auto-switch option), the drive missile's steering, the player's shots
(AddDamageByPlayer, humans as victims, tracers following the hands), the cheat paths (launch flag 0x1000), and most
likely the mines, the smackables, Sentry's and Human's ApplyDamage and Sentry::GetTarget.

## Random calls

| Function | Draws |
|---|---|
| Explosion::Simulate | 2 from the Simulation's SimRandom (Generate) for each body it pushes: the spin |
| Missile::Construct | for a sentry's missile, one 32-bit word from the Simulation's SimRandom (FUN_00040070): the homing delay, thrust, turn and heading |
| RayShell::Construct | in the passes after the first: Util_PerturbVector, through Util_GaussRandom, the C runtime's rand |
| Shell::Construct | 1 RandomScaled (the renderer's RandomShort, inlined): the speed |
| Shell::ApplyWorldEffects | 1 SimRandom Generate for the ricochet, and 1 more for a metal sound |
| Mine::Construct | a floating mine's throw: Util_PerturbVector, through Util_GaussRandom, rand |

SWeaponManager, Grenade, Sentry, Smackable and Human draw nothing.

## Known deviations

Places where the original reads stack it never wrote; the port uses zero:

- SWeaponManager::FireWeapon places the Q-Slick's scorch from a local it never sets (shadow A does not log the
  scorch's position).
- Sentry::ComputeTargetAngle leaves its flag unset for an axis other than x, y or z, and Sentry::RotateSRB its
  matrix; Sentry::DrawMuzzleFlashes passes a direction whose w it never sets.
- RayShell and Shell: a vector's w that only the xyz functions touch, and WorldCollisionInfo's segment words, which
  the inlined constructor copies from uninitialised stack (RayShell::CollideWithWorld, Shell::Simulate).
- Missile::CheckForCollision: the w of the world segment's start, which VU0_v4sub (0x0005fd92) leaves alone, so
  CheckHitWorld (0x0005ff75) gets the stack's word there and in WorldCollisionInfo's copy of the segment (the rep
  movsd at 0x0005fed0). Shadow B logs both starts' xyz only; it showed 1553 differences in the first round.

One dead computation in RayShell::CollideWithObjects is left out: the first offset from the origin to the object
(0x000722fe), which nothing reads before the simple-body branch overwrites it.

## What the port taught

- **A "dead" store that wasn't.** RayShell::CollideWithObjects works out the bottom of a rigid body's box after the
  overlap test (0x00072576: the frame's second row times minus the half height, plus the centre) and stores it,
  with w 1, into the translation row of its copy of the body's frame (0x0007257b-0x000725b1), which
  GetOBBObjectIntersection (0x0007264f) then takes. The first port read it as dead and kept the frame's own row, so
  a ray shell's impact on a car came out elsewhere: the AI's shots' sound positions (mission 1 from tick 251: the
  weapons bank's patch 0x6c with another volume, azimuth and pitch) and every frame after, in missions 1 and 3-7.
  Shadow C showed it as the victim's impact point (+0x80) differing in RayShell::CollideWithObjects.
- **A shadow that hangs looks like a crash.** The first round's shadow runs ended without shadows C and D, after
  250-390 loader faults: drive_game.ps1's 20-minute limit had killed them. The faults were caught ones, on both
  sides (RayShell's constructor calls IsOwnedBy on the shooter it looks up, NULL for a made-up signature); the
  hang was shadow C's synthetic world giving two objects each other as owner, a cycle IsOwnedBy (ours, both sides
  share it) walks for ever. Closing the render window on the runner (a WM_CLOSE posted from the desktop session)
  wrote the loader's thread snapshot, which named the original constructor's IsOwnedBy call. Owners are now only
  objects placed earlier, or the object itself.
- **Fakes that cannot be installed** are named now. Shadow A: 0x00022860 (AICharacterHands_GetWeapon) and
  0x00035840 (an AI helicopter's physics), originals of four bytes (`mov eax, [ecx+n]; ret`). Shadow B:
  0x0008db30 (RSceneObj::Hide) and 0x000a0e90 (RMissileStreak::Update with no point), ports shorter than a jump.
  Both sides run the same code on the same synthetic blocks, so they only go unlogged.
- **Someone else's uninitialised read can surface.** Missions 3, 4 and 5 then still differed in every frame and in
  nothing else: the road half as bright. The draw traces against the commit below differed in one draw, the player
  car's projected-shadow geometry, its fit 15-20 times too large. RShadowMap::SetCamera (render/ShadowMap.cpp, the
  render port's) transforms the car's box corners with a w that neither it nor the original (0x000a5e3a) writes;
  with the weapons ports the stack there held a NaN, every corner's distance became NaN and the fit fell to zero.
  The port now writes zero there (docs/driving/render.md). No weapons function was involved, which is why
  restoring the weapons ranges package by package left the frames as they were.
- **Shadow C's world hits** come from the live world round the player's car at the first tick: none on missions 1,
  5 and 7, where nothing is near enough, 48 on the others. Its scorch fake logs WWorldPos's bytes without the
  words WWorldPos::Construct leaves as the stack had them (the first two corners' fourth words and the padding
  after `valid`), which differed between the two runs of RayShell::CollideWithWorld's place (87-103 differences
  on missions 2-4, 6 and 8 before).

## Odd things in the original, kept

- SWeaponManager: Update's targeting test reads `weapons[special].id` with special -1 (and `weapons[secondary]`
  with -1 after UnloadWeapon): the heap word before the array. FireWeapon can write `fireSteps[4]`, which is
  `damage` (+0x34), when firing from system 2. The explosion sounds' four field writes happen even when the
  allocation failed. `primary` is -1 after Reset and nothing sets it. The secondary fire's event (0x16) is not sent
  when the selection is weapon 22.
- Missile: the transit sound's pitch steps by 0.01 and resets at 0.04 or more, so it is always 1.0. The constructor
  and the EMP detonation write through a sound that may be NULL. SteerMissile overwrites `detonate` when there is a
  target; a torpedo drops its target once it faces away. Render's light is scaled by the square of twice the steps
  left, compared unsigned. The OBBs' and RMissileStreak's empty destructors are still called.
- RayShell and Shell: ConsiderVictim's test for a non-human second victim can never pass. Fire's second case damages
  the second victim at the first victim's impact point. A missed shot's tracer runs from the muzzle to the muzzle
  plus the direction times the range. The trigger queue takes at most 63 rays a step. The "straight up" test is a
  four-component dot with (0, 1, 0, 1). ApplyWorldEffects writes to the sound even when the allocation failed.
  Shell::Simulate calls PhysicsObject::Simulate after DeletePhysicsObject on two paths and before it on the third.
  CollideWithWorld's segment w grows by one each step. AddFlash never reads `this`.
- Grenade: its first bounce sets off an explosion of type 3 (`kMineExplosion`).
- Sentry: RotateSRB and RotateAnim set the turning flags by comparing the new angle with the previous one (0.001
  either way), not with the target. ComputeNewRotation answers `limit` on one branch and `float(1 - limit)` on the
  other. The first impact-level test in Sentry's and Smackable's ApplyDamage, the flash fade test and the
  constructor's -1 clamp compare unrounded x87 values. GetTarget writes through NULL when an allocation fails. The
  constructor calls GetSimpleRigidBody once and drops the answer.
- Mine and Smackable: every mine uses the Prox Mine weapon's damage. Mine and Smackable Simulate carry on after
  deleting the object. Smackable::GoToSleep does not check the proc-anim state for NULL.
