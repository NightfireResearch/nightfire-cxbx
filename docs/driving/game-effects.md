# The driving engine's gameplay effects

The game tier's render effects: RVehicle, the render object every car and helicopter draws through, with its tyre
tracks, debris and particles (RTyreTrack, RDebris, RDebrisType, RVehicleParticle); the EMP effects (REmp,
REmpBolts); and the weapon effects and cameras (RBulletStreak, RMissileStreak, RMuzzleFlash, RScorchWorld,
RMissileCam, RSniperZoom). They sit on the renderer and EAGL (docs/driving/render.md, docs/driving/eagl.md) and the
vehicles (docs/driving/vehicles.md), all ours. Ported on 11 October 2026 by four packages, then merged
(`src/driving/game/`). Every address is Driving.xbe's.

**Status: tested (12 October 2026).** 125 functions ported (with the 4 replaced before, 129): 122 `FUNC_AT` and three register
adaptors (`AUTOLTCG`) under Ghidra's names - FUN_00095f90 (the switch effects' fade, EAX/ECX in, ST0 out),
FUN_000a68f0 (a sniper ring point, outputs in ESI/EDI) and FUN_000a6cd0 (a particle type's sprite rectangle, EAX
texture, ECX rectangle) - over plain C++ cores. `game.effects` is 100% ours by bytes: 129 replaced, 56 dead
(exception funclets), 10 live - all exception funclets at 0x00153680..0x00153c40 with no caller the tool can see
(their owners are replaced). Four shadow tests compare the ports with the originals on copies of the live objects
and synthetic ones, all with no differences on missions 1-8; lockstep runs of missions 1-8 to tick 9000, idle and
accelerating, match the baseline frame for frame and in their sound traces (every car on screen is drawn through
RVehicle with its tyre tracks and particles).

## Layout

| File | What |
|---|---|
| `game/VehicleRender.h/.cpp` | RVehicle (0x370, vtable 0x001922d0, an RSkeletalObj): construction (its particles), PostLoad (wheel and hub bones, the shadow and render-type-4 models), the wheels and dashboard needles (UpdatePosition, ComputeWheels, GetNeedlePos), the switch effects and damage zones in the shared buffers (`VehicleBuffers`, 0x001f2a50, which data/RCARPFile.cpp hands the render methods as "GAME::SwitchData" and "EAGL::DamageZones"), muzzle flashes, the colour dissolve, the EMP state and bolts, Render, RenderShadow, RenderFX |
| `game/TyreTrack.h/.cpp` | RTyreTrack (0x920, one per wheel of a PBondCar): a ring of up to 32 or 64 points, Add, Break, Draw through four volatile materials, Init |
| `game/Debris.h/.cpp` | RDebris (0x90, one, `Debris`) and its 23 RDebrisType kinds (0x1ad0 each, up to 128 pieces): Add, Draw, Reset, and DrawShellCasings (FUN_000a9680) for the player's weapon |
| `game/VehicleParticle.h/.cpp` | RVehicleParticle (0x4c, vtable 0x001930b0): a car's 14 particle systems and their emitters (smoke and dirt at each wheel, sparks), UpdateCar, the set of cars to update (std::set, `VehicleParticleSet`, with its compiled tree code), UpdateAll, ResetAll, Init, Kill |
| `game/EmpEffect.h/.cpp` | REmp (the shooting effect's ring, drawn twice, and a sphere never drawn), REmpBolts (0x22c, RVehicle +0x368: bolts between a car's "emp" locators or to the world) and the two std::maps between them (the anchors by group, `EmpAnchorMap`; the counts per group, `EmpGroupCountMap`), with SharedTreeMin (0x0009c310), the minimum every map with 0x18-byte nodes shares |
| `game/WeaponEffects.h/.cpp` | RBulletStreak (four batches of tracer quads), RMissileStreak (0x610, inside each Missile and Grenade), RMuzzleFlash (the model and the player's view of it), RScorchWorld (scorch marks through the decal manager) |
| `game/WeaponCameras.h/.cpp` | RMissileCam (0x1c, vtable 0x00192d38, `MissileCam`), RSniperZoom (0xc70, vtable 0x00193008, `SniperZoom`), SniperRingPoint, and ParticleTextureRect (the vehicle particles' sprite rectangles, read from their system types' texture names) |

Merged into their owners at integration: RVehicle's partial views (the vehicles' construction calls, BondCar's EMP
and boost calls, the renderer's shared-buffer calls, the shadow map's draw, the switch-effect FX types
`kFXSwitchOn`/`kFXSwitchOff` and the landing's `kFXSparks` with `RVehicleParticle::kSparkLine`); RTyreTrack's (its
size, the constructor's third flag is `snow`); RParticleEmitter (render/Particles.h) is the vehicle particles'
emitter: its position a Coord4 and its body a whole word; the PVehicle slot calls the effects make are PVehicle's
members (game/Vehicle.h); RParticleSystemData +0x24 is the type's `texture` name; CARP::BaseDesc +0x38 is a float
`height`; EmpGeoPrim's parameters are named (`EmpGeoPrimParam`, render/Decals.h); RColorize writes
RMissileCam's `enabled` and `alpha`; the managers are made by `NewPooledManager<T>` in render/RenderHigh.cpp.

The globals the static initialisers construct or clear are ours, and `tools/static_init_driving.py` constructs our
variables (`NAMED`, and `NAMED_OBJECT_ARRAYS` for arrays of objects): the bullet streaks' colours and materials, the
missile streaks' state and colours, the muzzle flashes' colours, materials and POV material, REmp's colours and
states, the debris colours, and the tyre tracks' colours and materials; also RMissileCam::Draw's state
(`MissileCamState`, its destructor `DestroyStatic_00200f60` in engine/StaticInit.cpp). Still at the game's address:
the vehicle particles' system types (0x001c50d8...: read by `ParticleType*`, initial data from the XBE, to own in a
sweep), the bullet streak kinds (0x001f2e20: Shell, RayShell and EFireWeapon read them), the debris kinds
(0x001c8c80) and the shared buffers.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GAMEEFFECTSSHADOWA` | first tick | RVehicle on copies of every live car and helicopter (controls, wheels, suspension, damage perturbed): ComputeDamage, ComputeWheels, UpdatePosition, the shared buffers, TriggerIlluminate, the EMP and boost triggers, TriggerFX's types, StartMuzzleFlash, StartColourDissolve, RenderShadowGeometry, RenderFX, RenderDeferredEffects; FUN_00095f90 and GetNeedlePos on random inputs; tyre tracks (Add, Break, Draw, the constructor, Init) on copies and made-up rings; debris on private copies of the kinds. The draws and the other packages' functions are recording fakes |
| `NIGHTFIRE_GAMEEFFECTSSHADOWB` | first tick | TriggerTireSmoke, TriggerSparks and UpdateCar on copies of a live RVehicleParticle with a stand-in vehicle (surfaces, slip, speed perturbed); UpdateAll, ResetAll and Kill on a private set; the constructor and destructors; the set's tree code |
| `NIGHTFIRE_GAMEEFFECTSSHADOWC` | first tick | REmp's Init and Kill, DrawShootingEffect (200 cases, NaN and out-of-range progress), LoadAttributes; both maps (600 random operations each, compared node by node); REmpBolts on four synthetic cars (about 150 cases a method) |
| `NIGHTFIRE_GAMEEFFECTSSHADOWD` | first tick | the bullet streaks, missile streaks, muzzle flashes and scorch marks on synthetic inputs (scorch marks on the live world round the first camera), RMissileCam and RSniperZoom on copies of the live ones, the two register adaptors |

Each shadow swaps its package's entries back for the original's run, and the fakes are hooked at the original's
address and at our entry (the ports call each other directly). The globals we own are lent to the game's copies
before the original runs and compared ours against the game's after; pointers into them are compared as the
game's. The mutations each was checked against (by reading): A, RTyreTrack::Draw's break test, ComputeWheels'
compression limit and Add's spacing masks; B, road dirt's rate clamped to 1 and the steep-direction case dropped;
C, CreateWorldBolt's sign from the wrong bit and Init's clear range; D, the UV switch's cases 1 and 2 swapped and
the missile streak's half-width rounded to float. C expects "calls faulted" on its one-anchor car: the original
faults there too (CreateVehicleBolt(from, NULL)).

In game: lockstep runs of missions 1-8, idle and accelerating, to tick 9000, frames against the baseline. They
reach the constructors and destructors, PostLoad, the shared buffers, Render and RenderShadow (every car's draw),
the tyre tracks and debris, the particles' UpdateCar, UpdateAll, Init and smoke and dirt, REmp's Init and Kill and
the bolts' LoadAttributes, and the weapon effects' Inits, Kills and per-frame early-outs. They do not reach what
needs input: muzzle flashes, the EMP shot (DrawShootingEffect) and its bolts (REmpBolts), the turbo smoke and
sparks (TriggerTireSmoke, TriggerSparks), the colour dissolve, the dashboard needles (the inside view), submarine
fins, shell casings, bullet and missile streaks, scorch marks, the missile camera and the sniper scope when in use
- for those the shadows are the test.

## Random calls

Every draw is from the renderer's 16-bit generator (`RandomShort`, seed and multiplier 0x001c45c4/0x001c45c8),
inlined in the original, except where noted:

| Function | Draws |
|---|---|
| RVehicle::StartMuzzleFlash | 1: RandomShort for type 45, else RandomScaled(0.2) |
| RDebrisType::Add | 4 (the spin axis and the turn rate) |
| REmpBolts::CreateVehicleBolt | 2 |
| REmpBolts::CreateWorldBolt | 1, the generator stepped twice (the multiplier squared), the sign from bit 8 of the first product |
| REmpBolts::GetRandomAnchor | 1 |
| REmpBolts::TrySpawnBolt | 2, and the C runtime's rand through Util_PerturbVector |
| REmpBolts::Draw | 1, and rand through Util_GaussRandom 3 times |
| RMissileStreak::Update(position) | 3 |
| RMuzzleFlash::DrawPOV | 2, only when the simulation is not paused |

RLightning's AddBolt and AddRoundedBolt and Shell::ApplyWorldEffects, which REmpBolts calls, draw on their own.
RVehicleParticle, RTyreTrack, RBulletStreak, RScorchWorld and the weapon cameras draw nothing.

## Known deviations

Two places where the original reads stack it never wrote; the port uses zero:

- REmpBolts::CreateVehicleBolt subtracts a vector it never sets from the spark direction it hands
  Shell::ApplyWorldEffects (only the sparks' direction; shadow C masks it).
- The anchor map's allocator byte, copied from an unset byte (nothing reads it; RVehicleParticle::Init's set copies
  one too).

Words the original leaves uninitialised in records nothing reads are written as zero: RTyreTrack::Add's
perpendicular w, RScorchWorld's centre and cross w.

## What the port taught

- **A register-argument helper's stack.** FUN_000a6cd0 pops ESI between its first FILD [ESP+4] and the next two, so
  those read the second and third locals, not the first again. The first port read the listing as written and gave
  every smoke, dirt, snow and water sprite the wrong rectangle: the lockstep frames showed it at once (wheel smoke
  and spray only), and restoring the vehicle particles with NIGHTFIRE_RESTORE_DIRECT pinned it to the helper.
- **FSIN/FCOS results stay unrounded.** SniperRingPoint multiplies the 64-bit sine and cosine by the radius; going
  through a double-returning helper first is one ulp off on some inputs (docs/driving/maths.md 3.2): naked helpers.
- **Shadows log by value.** A transform built on the caller's stack, a material pointer logged raw, the game's copy
  of an owned global read on the port's side: each gave hundreds of false differences, which hid the real one (only
  the first ten print).
- **NIGHTFIRE_RESTORE_DIRECT=1** (docs/driving/core-data.md) makes NIGHTFIRE_RESTORE_RANGES restore ports our own
  code calls directly, which a bisect over this round needed.

## Odd things in the original, kept

Every area's entries, by kind and with addresses: docs/driving/original-quirks.md.

- RVehicle: PostLoad's wheel-instance search can read past the array; RenderFX clears empVictim (and
  `ClearEmpVictim`, which nothing sets); the projected shadow is drawn only when the view distance is below 0.125;
  the destructor does not clear `particles`; StartColourDissolve and TriggerIlluminate ignore arguments.
- Debris: the colours are always DebrisColours[0]; a piece fades by (SimStep - lifetime) / its throw tick;
  DrawShellCasings computes an inverse it never uses; RDebris's constructor calls Init(NULL) when an allocation
  fails. RTyreTrack's constructor writes 1.0f into the last point's joined word.
- RVehicleParticle: UpdateAll skips when SimStepCount equals the last tick but stores the game tick; TriggerSparks
  never reads its float; snowmobiles throw no snow spray; the constructor writes active = 0 even when the
  allocation failed; UpdateCar's clamp compares the double and keeps the float; the record at 0x001c54f8 is never
  referenced.
- REmp: Init's clear overruns into the sphere's first position; the sphere is never drawn (no position or texture
  coordinate pointers); a segment's end has w 2. REmpBolts: numBolts never rises, so Update tries four spawns every
  step; a car with one anchor faults (CreateVehicleBolt(from, NULL)); a long locator name overflows into the next
  anchor.
- RMuzzleFlash::Init turns its static model a further quarter round each run. The bullet streaks' vertices 2 and 3
  get v = 0.1 (RBulletStreak::Init's switch on v % 6, from the jump table at 0x0009a1ac). RMissileStreak::Update
  with count 0 at an odd tick writes points[-2] and, as points[-1], its own header (shadow D leaves that case
  out). RScorchWorld's level test compares with a double holding 0.85f.
- RMissileCam::Draw points its GeoPrim at its own stack. RSniperZoom::Draw sets the shared material's state, not
  its own; the outer ring's outer grey is never written, so always 0.
