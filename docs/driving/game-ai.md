# The driving engine's AI

The game tier's AI: the characters (the pedestrians, the passenger, Bond at the wheel and the player's hands in
auto-drive, the enemies on foot and in cars), the vehicles' AI (every car's AIGroundVehicle, the helicopters'
AIHelicopter, the spline paths, the vehicle controller and the road spawner) and the mission's AI elements with their
tracking and the AI zones. It drives the vehicles, weapons, missions and events (docs/driving/vehicles.md,
game-weapons.md, game-missions.md, game-events.md), all ours; the front end is still the game's. Ported on 12 October
2026 by six packages, then merged (`src/driving/game/`). Every address is Driving.xbe's.

**Status: ported and tested (12 October 2026).** 361 entries, all `FUNC_AT` but one: 67 of package C (AICharacter,
the passenger, the pedestrians), 72 of package D (AIVehicle, AIHelicopter, AIVehicleController, AIRoadSpawn,
AISplinePath), 30 of package A and 19 of package B (AIGroundVehicle, with its vector deleting destructor 0x00035d90,
which Ghidra put inside AIVehicleController::Init), 102 of package E (the enemies, with Sunroof::DoAiming 0x000215b0,
which Ghidra merged into DoArmed) and 71 of package F (the elements, tracking and zones, the hands and Bond, with
AICharacterBond::GetRideRotPos 0x0001ccc0, CompareRanges 0x00028a90 and AIZone::Construct 0x00036a00, which Ghidra
missed). AIElementController::WakeUp is tagged `AUTOLTCG` (docs/driving/original-quirks.md 1.3). `game.ai` is ours by
bytes: 356 replaced (the five other entries are members of other classes, counted there), 113 dead, 53 live - all
exception funclets (0x00150xxx-0x00151xxx) of replaced functions. Six shadow tests compare the ports with the
originals; the lockstep runs and the shadows pass (below, "What the testing taught").

## The classes

Our classes declare no `virtual`: the vtable is the game's, the object's first word, and a call through it is a
small named helper (`XbeVirtual`). Where a subclass overrides a slot, or a caller dispatches through the vtable, the
plain name dispatches and the class's own version is `...Base` (`AICharacter::DeleteBase`, `IsAliveBase`,
`NotifyZoneDamageBase`, `GetZoneHitPointScaleBase`; AICharacterEnemy's `TargetIsInRangeBase`, `NeedsToReloadBase`,
`TargetIsAcquiredBase`).

| Class | Size | Vtable | Header | What |
|---|---|---|---|---|
| AICharacter | 0x140 | 0x0018a540, 33 slots | AICharacter.h | a person the AI drives: an actor (ActActorDatabase) on a Human, run as a state machine - Execute takes the actor's matrix, handles the interrupts (slot 30), then the state's handler (slot 7 + state, 23 states; the jump table at 0x0001c544); a thrown character flies and bounces |
| AICharacterPassenger | 0x150 | 0x0018ae68 | AICharacter.h | the mission set's passenger (1-5) in the player's car |
| AICharacterPedestrian | 0x290 | 0x0018b470, 34 slots (33 TargetIsInRange) | AIPedestrian.h | the sixteen pedestrians on the pavements: spawned and recycled along the roads, they wander, avoid, dodge and react to threats, talk and lean |
| AICharacterHands | 0x160 | 0x0018ad28 | AIHands.h | the player's hands holding the auto-drive weapons (fgHandObj) |
| AICharacterBond | 0x150 | 0x0018a688 | AIHands.h | Bond at the wheel, one per side, his hands on the car's steering effects |
| AICharacterEnemy | 0x140 | (none) | AIEnemy.h | the enemies' base: slots 33 TargetIsInRange, 34 NeedsToReload, 35 TargetIsAcquired |
| AICharacterEnemyRider | - | (none) | AIEnemy.h | an enemy in a car (`vehicle` +0x140; the name is ours), with the methods the linker folded for the riders |
| AICharacterEnemyDriver | 0x150 | 0x0018a788, 33 slots | AIEnemy.h | the driver's hands kept on the wheel by IK |
| AICharacterEnemyGround | 0x1c0 | 0x0018a840, 36 slots | AIEnemyGround.h | an enemy on foot: arms, aims and fires, dodges and runs from cars, falls when the ground drops |
| AICharacterEnemySnow, SSnow, Sunroof, Window | 0x170, 0x150, 0x170, 0x160 | 0x0018a948, 0x0018aa18, 0x0018aac0, 0x0018ab68 | AIEnemyRiders.h | the riders by seat |
| AIVehicle | 0x80 | 0x0018bb10, 5 slots | AIVehicle.h | the vehicles' AI base: Delete, Reset, Update, EngageSplinePath, DisengageSplinePath |
| AIGroundVehicle | 0x270 | 0x0018b8d4 | AIGroundVehicle.h | every car's AI: Update by type (player, auto-drive, spline, traffic, agent; the jump table at 0x00031804), the navigate, target and attack modes, the lanes, the collision checks, the weapons |
| AIHelicopter | 0x140 | 0x0018b950 | AIHelicopter.h | a helicopter's AI: DoAgentLogic by attack mode (the jump table at 0x000345c4), ControlMoveState, ManeuverToPos, the guns and rockets |
| AICommand | 0xa0 | - | AIVehicle.h | the CARP 'AICo' record EAICommand hands a vehicle |
| AISplinePath | 0xb0 | - | AISplinePath.h | an AI spline played as a path (the AI's and RWorldCamera's) |
| AIVehicleController, AIRoadSpawn, AIZoneController | 1 | - | AIVehicleController.h, AIRoadSpawn.h, AIElements.h | singletons: the vehicles and traffic lights; the traffic and pedestrian spawn places; thirty short-lived zones the AI keeps out of |
| CARP::AIElement | 0x100 | - | data/Carp.h | the level's 'AIEl' records (type 0 a character element, 1 a vehicle element - a car, or a helicopter with `car.helicopter` set), copied by the controller |
| AIElementTracking, AIElementController | 0x50, 0x58 | - | AIElements.h | each element's distance and wake state; the elements, woken within the level's slots (fgAIElementController) |

## Who reaches it

GameLoop and World (the controllers' Init, Shutdown and Update, the traffic cars, AICharacter::Init/Shutdown/Manage,
the element controller's Create/Destroy), the static initialisers (the player and traffic AI and the zones), the
events (EAICommand and the element events, auto-drive, spline control, traffic, zones), the missions (objectives on
elements, the rules' tracking and stats, kills), the weapons (the hands, zones), the vehicles and Human (each car's AI,
the characters' Execute and hits), the cameras (spline paths) and Targeting. All call the ports directly.

## Layout

| File | What |
|---|---|
| `game/AICharacter.h/.cpp` | AICharacter and the passenger (C) |
| `game/AIPedestrian.h/.cpp` | the pedestrians and their statics (C) |
| `game/AIHands.h/.cpp` | the hands and Bond (F) |
| `game/AIEnemy.h/.cpp`, `AIEnemyGround.h/.cpp`, `AIEnemyRiders.h/.cpp` | the enemies (E) |
| `game/AIElements.h/.cpp` | the elements, tracking, controller and zones, ceil and floor of a float (F) |
| `game/AIVehicle.h/.cpp`, `AISplinePath.h/.cpp`, `AIHelicopter.h/.cpp`, `AIVehicleController.h/.cpp`, `AIRoadSpawn.h/.cpp` | the vehicles' base, spline paths, helicopters, controller and road spawn (D) |
| `game/AIGroundVehicle.h/.cpp`, `AIGroundVehicleDriving.h/.cpp` | AIGroundVehicle: the modes, navigators and lanes (A); the driving, collision checks, traffic spawning, weapons and Update (B) |
| elsewhere | the other classes' functions the linker placed among the AI, each a member of its class: RCamera::SetFieldOfView (camera/Camera, 0x00011000), SimRandom::Fraction (engine/SimRandom, 0x0001d9d0), WorldCollisionInfo::Construct (world/CollisionManager, 0x0001d9f0), VectorConstructorIterator (engine/CoreFoundation, 0x00022770), BlendVectors (platform/VU0Math, 0x00022870), BarrierList::Count (world/Collider, 0x00023f20), WRoadNetwork's road counts AddRoadCar/RemoveRoadCar/AddRoadPedestrian/RemoveRoadPedestrian (world/RoadNetwork, 0x00029c90, 0x00029cb0, 0x00023990, 0x000239b0), WRoadNav::SegmentSpeed, LeftDrivableDist and RightDrivableDist (world/RoadNav, 0x00029cd0, 0x00029cf0, 0x00031820), WGridCellList::Xlen (world/Grid, 0x00034e50) and CARP::Instance::SetMatrix (data/Carp, 0x00035610) |

Merged at integration: the events' AI views (game/AIViews.h) are gone, with the packages' own provisional views (the
mission set, the AI element, the zones, the car AI the enemies read, the element controller's counts). The mission set
has Bond's character set (`bond`, +0x24) and the passenger (+0x28); the AI element is data/Carp.h's `CARP::AIElement`
with the controller's full layout (ActWeapon's accuracy pointer is the element's `AIElementCharacterInfo`, the
events' "unknown77" the helicopter flag, their "unknown48" the element's state); `AICommand` is defined once (the
data layer's resolver uses its two references); AIGroundVehicle's fields have the names the packages found (the
height clamp is +0x10c the ceiling, +0x110 the floor; `reversing`, `shocked`, `oiled`, `stopping`, `atRedLight`,
`heldStill`, `minSimUpdate`, `spawnPosition`, `maySpawn`); the mode setters take an int; AICharacter's +0xac and
+0xb0 are the animation and cross-fade controllers; the hands' +0x150 is `shown`; SMissionManager's +0x500 is
`enemyCount`; the road segment's +0x4c is its `speed`. Every call between the packages, from the AI into the
missions, events and weapons, and from the rest of the tree into the AI, is direct; the C runtime's vector iterators
take our constructors and destructors (`XbeAddress`), the actors our callbacks.

The globals only our code touches are ours: the ground vehicles' height phase and parking offset (0x001ddf00,
0x001ddf04), the helicopters' lane offsets (0x001ddf10), the road spawn's statics (0x001de738-0x001de810, 0x001de8e0,
0x001b6614, 0x001b6618), the controller's (0x001de8f0-0x001de910, 0x001de920, 0x001e1ee0), the enemies' (0x001b54c8,
0x001b57f0, 0x001b57f4, the driver's IK 0x001b52c0) and, from the static initialisers, the elements' default sleep
range (0x001ddefc) and the traffic's navigator step (0x001ddf0c). Still at the game's address: the hands' statics
(0x001dda70, which the camera's lock-on flag shares, still read by original code), Bond's IK (0x001b5240), the player
and traffic AI arrays (0x001de940, 0x001dee20), and globals global_coverage lists ready to own, left for a sweep
because the shadow tests set and compare them at the game's address: the pedestrians' statics (0x001ddb40, 0x001b6388),
fgAIElementController (0x001ddee4), the zones (0x001e1ee8, which the static initialisers construct), the road spawn's
navigator (0x001de820, destroyed at exit), MaxTraffic and the traffic spawn count (0x001de914, 0x001de918),
ForceNearRespawn (0x001de814), gLimitHenchSpawns (0x001b64ec) and the Bond weapon buttons (0x001dda2c).

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GAMEAISHADOWA` | first tick | all 30 of package A on synthetic vehicles, bodies and navigators copied from the live player's, the statics mirrored to the game's addresses; about 4,500 cases |
| `NIGHTFIRE_GAMEAISHADOWB` | first tick | the 19 of package B on four synthetic vehicles with fake car vtables, bodies from the live player car and real road segments; about 7,000 cases |
| `NIGHTFIRE_GAMEAISHADOWC` | first tick | the characters' pure functions, AICharacter on a character with an all-fake vtable, the passenger, and every pedestrian method but Init/Destroy/Manage/LoadPedTextures and the destructors on a copy of a live one |
| `NIGHTFIRE_GAMEAISHADOWD` | first tick | every function of package D but the length error, on synthetic helicopters and agent arrays round the live player car, road network, grid and camera, the statics mirrored; logging vtables on the live AI records |
| `NIGHTFIRE_GAMEAISHADOWE` | first tick (needs live cars) | Min/Max (NaNs), SimRandom::Fraction, WorldCollisionInfo::Construct, the vector iterator, IsBumping, every kind's constructors, destructors, Delete(0), the four GetRotPos and every virtual, on synthetic enemies round a live car with fake vtables |
| `NIGHTFIRE_GAMEAISHADOWF` | first tick | all 71 of package F: pure functions at edge values, zones, tracking at the wake range exactly, the controller on synthetic elements (every difficulty and seat, with and without the actor database, Update with too few slots and enough, the constructor on a synthetic 'AIEl' set), Create/Destroy, the hands and Bond with the game's vtables |

Each shadow swaps its package's range back for the original's run and replaces the calls out of it with recording
fakes, hooked at the original's entry and, where the entry jumps to a port, at the port (so the ports' direct calls
reach the fakes too); random draws are faked or run from copies of the generator's state. The mutations each was
checked against (by reading): A, GetBestWeightedLane's `<=` for `<` (ties generated on purpose); B, GetLookAhead
answering float(ahead); C, GetDegFromAng's `<= 22.5` for `< 22.5`; D, GetTrafficSpawnPointOnSegment's left lane
without the -1; E, Sunroof::UpdateLeanDirection's row 0 for row 2; F, GoToSleep's `hitPoints > 0` for `>= 0` and
Tracking::Update's `<` for `<=`.

In game: lockstep runs of missions 1-8 run the AI every tick - every AI car's Update (traffic: DoTrafficStateUpdate,
Spawn, CheckTrafficCollision, DriveToPointTraffic; agents: DriveToPoint, the collision checks, the weapons), the
player cars' DoPlayerLogic, the controllers' and road spawn's per-step updates, the element controller's Update,
tracking and WakeUp/GoToSleep, the zones, the pedestrians' Manage, Init and Destroy, the hands' and Bond's Init and
Bond's per-step methods. Only the shadows reach: auto-drive and spline cars, subs and turrets, the EMP and dead stops,
helicopters (unless the mission declares one), the crash, destruct and spline modes, the forced RefreshSpawnData, the
array paths of the deleting destructors, the passenger (mission sets 1-5 in a car), threat reactions, aiming and
special pedestrians, a thrown character, the enemies' deaths, falls, hits and dodges, auto-drive aiming, the hands'
Fire/Reload/ChangeWeapon/DoArmed/DoAiming/DoFiring, Bond's arming and firing, EveryoneChill, Destroy, CompareRanges,
GetProperty and GetCarpElementTracking. To test specially in game: auto-drive with the hands (switch, fire, reload,
aim), a mission with helicopters, enemies on foot and riders (a death of each kind, a dodge, a fall), a passenger
mission, pedestrians reacting to the car and recycled, the traffic lights, and a long run of traffic spawns and
unspawns (the parking offset).

## What the port taught

(To come.)

## What the testing taught

The first round (gai1) had 6 of the 16 lockstep runs differing (missions 1, 5 and 7, idle and accelerating, in frames
and sound traces), GameWeaponsShadowA showing about 315 differences on every mission, and every shadow run killed at
the old 20-minute limit before the AI's shadows had run. Bisected with NIGHTFIRE_RESTORE_RANGES and
NIGHTFIRE_RESTORE_DIRECT (by file, then by function: every lockstep difference was in AIGroundVehicle.cpp), then a live
per-call shadow of the function found, and the six shadows:

- **Six port bugs.**
  - AIGroundVehicle::DoSplinePathUpdate (0x0002a8b0) and AIHelicopter::ControlMoveState's spline case (0x00032b4b):
    the original copies the path's position into the translation row of the orientation it took from GetOrientMat
    (whose row is 0, 0, 0, 1), multiplies by the placement, and hands the body that row as its position - the
    position goes through the placement too. The ports kept the position apart and untransformed (mission 7 from
    tick 303: a spline car elsewhere; shadow D's simple body).
  - AIGroundVehicle::DoAttackMode (0x0002bf40): auto-drive's follow speed is 0.85 (0x0018b8e8), not 0.9 (mission 1 from
    tick 212, idle and accelerating); chase calls a charge off when the target is within the cars' half lengths
    (the value compared at 0x0002c16b is the reach, not the range made from it) - mission 5 from tick 5140.
  - AICharacterPedestrian::FaceTarget (0x00024c30): its turn limits are 0x3d47ae15 and 0x3da66667, a bit above
    0.04875 and 0.08125, which the port's literals rounded below (mission 5 accelerating).
  - AIElementController::CalcMightAsWellBeDead (0x00028730) tests the element's type (+0x44) for a vehicle, not its
    state (shadow F).
  A scan of every port's float constants against the original's (.rdata loads and immediates, both builds) found no
  other mismatch.
- **Harness, in the shadows.**
  - A case's own draws (Range, Chance inside a case) ran on from the original's side to the port's: every shadow now
    puts the input generator back before the port's side (A's deleting destructor cookie, E's constructors).
  - Shadow C ran several cases from one preparation but restored the test's starting state after each, so the
    second and later cases ran on a zeroed pedestrian (thousands of faults on both sides, which ran the shadow runs
    out of time); they restore the preparation now. Shadow F forced snow2a_mis4 with NULL render names, which both
    sides fault on and only the original's unwinding frees from.
  - Code pointers the ports hand the C runtime's vector iterators are ours: shadows A and D log them as the
    originals (AIGroundVehicle::Construct and Destruct). Shadow B's BarriersInPath fake logs `reversing` as a flag
    (the original passes the made-up 2 raw, ours as a bool; the game only has 0 and 1). Shadow E no longer logs
    WorldCollisionInfo's words neither constructor writes, and keeps TargetableStagger and TargetableCount (ours,
    run by both sides' constructors) in its state.
  - The fakes' "too short to hook" test read a 0xc3 inside an address (our RPlayerCamera::UpdateCamera begins
    `cmp [0x001ec368], 0`) as a return, so shadow F ran nothing; GameWeaponsShadowB applied it to the entries' own
    jumps too, whose offsets change with the DLL's load address, and so lost its PhysicsObject fakes (0x0006f520,
    0x0006f5b0) in this round: the real GetCollisionBounds ran on its made-up objects and faulted about 150 times,
    two seconds a fault with eight games running. Shadows C, E and F, GameWeaponsShadowB and D,
    GameEffectsShadowD and GameMissionsShadowB now take a return followed by padding, and B passes an entry's jump.
  - Each shadow prints its first three differences of each kind and the call log round a difference.
- **GameWeaponsShadowA** faked AICharacterHands::GetWeapon at its entry, which now jumps to our port, too short to
  hook: the original's calls reached the fake and the weapon manager's direct calls the real one. An entry whose
  port cannot be hooked is left unhooked too, so both sides run the real one, as before the AI was ported.
- **Time.** With the six AI shadows a shadow run takes 19 to 34 minutes on eight slots (the AI shadows about ten of
  them, more on the missions with traffic); round.sh gives a shadow run 45 minutes now.

With these, the final round (gai4) had all 16 lockstep runs exact against the baselines and every shadow run finished
on missions 1-8 (a frame dumped, in 19 to 34 minutes), the six AI shadows and GameWeaponsShadowA with 0 differences
(shadow B runs nothing on mission 7, which has no road network); only the two known differences remain (sndsysshadow
case 0, sndstreamfileshadow case 52).

## Random calls

Every draw is the Simulation's SimRandom (`fgSimulation.random`): Generate, or Fraction (Generate / 65536, left
unrounded).

| Function | Draws |
|---|---|
| AIGroundVehicle::DoAgentStateUpdate | one Generate per charge roll (r x 2^-16 < chargeChance), every 5 s for a non-sub with its goal in sight |
| AIHelicopter::ManeuverToPos | one |
| AIHelicopter::FireRockets | Fraction three times per salvo |
| AIRoadSpawn::GetTrafficSpawnPointOnSegment, GetPedSpawnPointOnSegment | one; two |
| AIRoadSpawn::GetRandomTrafficSpawn, GetRandomPedSpawn | one; one per try |
| AIVehicleController::CreateSimTrafficCars | one per car type (while fewer than seven types are left to choose) and one per car for its colour |
| AICharacterPedestrian::GetRandomConvAnim | r x 6 >> 16 or r x 11 >> 16 |
| AICharacterPedestrian::DoDying, DoLeaning, DoAvoiding, DoDodging (the voice delay) | r x stepsPerSecond >> 16 + 30 |
| AICharacterPedestrian::DoAiming | the aim time, r x 180 >> 16 |
| AICharacterEnemyGround::AvoidThreat | a 1% chance; the side r x 2 >> 16; high, Fraction < 0.5 |
| AICharacterEnemyGround::DoArmed | Fraction < 0.5; dodges r x 2; fire-above r x 2; fire r x 6 |
| AICharacterEnemyGround::PlayZoneInjuryAnim | scream r x n; claustrophobic death r x 4; zone r x 4 + 2; head Fraction > 0.25 then r x 4; death r x n; hit r x n; claustrophobic hit r x 3 |
| AICharacterEnemyGround::DoWandering | r x 3 |
| AICharacterEnemySnow | DoArmed r x 5; DoAiming r x 180 (two sites); DoDying the spread r x 5 / 65536 - 2.5 or r x 2 / 65536 - 1, then r x 2; a hit r x 5 |
| AICharacterEnemySSnow, Sunroof, Window | SSnow's hit r x 2; Sunroof's fire r x 6, injury r x n; Window's fire r x 5, injury r x 2 |
| AICharacterHands::UpdateRotPos | two per call while firing weapons 1, 4 and 6 (x then z jitter, r x 0.001 / 65536 - 0.0005); one while the kick eases (r x 0.005 / 65536, capped at 0) |

## Known deviations

Places where the original reads stack it never wrote; the port uses zero: GetDirectLaneWeights' collision record
(0x0002b7a0), WorldCollisionInfo::Construct's segment words (0x0001d9f0), the local vectors' w of the characters,
enemies, helicopters, controller, road spawn, element controller and hands. AIElementController::WakeUp gives a
helicopter's seated riders the helicopter where the original gives them an uninitialised stack word (0x00028bb0); no
mission seen has one.

Other differences, none observable: pure calls whose answers nothing reads are left out (GetPhysicsObject at the top
of the ground vehicles' three Fire functions, repeated WRoadNetwork::Get calls, a discarded floor in
CheckAgentRoadNetworkCollision); inlined copies are calls (InCollisionRange, RegisterSimCar, KillSelf); DoDying checks
its index before the read; the element constructor's placement-new NULL check is dropped; ProgrammerDefinedEvent's
empty text is our own ""; the road spawn's cell vector's allocator byte is written 0. The packages' own string and
animation tables are our constants. AIElementController::WakeUp is placed by Ghidra's name (`AUTOLTCG`): the binary's
measured facts read ESI on entry through the seat jump table, which the code does not do.

## Odd things in the original, kept

- NULL not checked: targets read unchecked by UpdateNavPos, DoAttackMode (mode 14), DoAgentStateUpdate (chase) and a
  sub's DriveToPoint; the controller's slots; FastAlloc before SetActor in WakeUp; the ground enemy's target
  registration after a failed allocation; the hands' Init.
- Leaks: AIVehicleController::Shutdown destroys the agent and helicopter arrays without freeing them (and the static
  player and traffic AI again at exit); the pedestrians' Destroy frees the navigators and grounds only; the hands'
  Shutdown forgets the hands and their Human; GoToSleep deletes only the Humans.
- Arrays and indexes: the element lookups index by the global controller's records, not `this`'s; DoTurning would
  read peds[-1]; the enemies' lean tables are indexed [direction - 1]; zone 4's empty death list plays animation 0;
  InZone takes the type's radius by its low 16 bits.
- Branches: the Approve functions answer the count as a pointer with no free slot, and Register makes a car its own AI
  vehicle; UpdateAgentGroundVehicles divides by zero with no agent cars; IntersectionGreenLight's fabs applies to the
  comparison; GetCollNav compares a signed lane unsigned; DoAgentStateUpdate's zone loop has no break; CheckHitWorld
  <= 1 counts as clear; the traffic's stop points accumulate; the side toggles as (side + 1) & 1; a helicopter's guns
  mode with no shots gets 255; GetRandomPedTexture is round-robin; UnSpawn's parking offset saturates at 5000, so
  after about a hundred unspawns every car parks at z = 45000; the zone types 0 and 1 never register; EMPTY is true for
  a vehicle with no riders and NaN hit points count as alive for BLOWOUT; a helicopter always counts as intact;
  WakeUp's ranges are x30 for ground characters and x9 for riders, and an empty slot ends the riders; Update hands
  out slots from the totals when there is not room; the element constructor turns "paris_hench" cars into
  "junglesnow" with no riders on snow2a_mis4 at difficulty 2; the hands' unarm branch switches at once and AimRelease
  runs while firing; Bond ignores select while arming; DoAttackMode's chase calls a charge off within the cars' half
  lengths, not its range; CalcMightAsWellBeDead counts vehicle elements awake or not. The events' EAIElementSetNonScoreable and EKillAIElement test
  an element's helicopter flag the wrong way round.
- Dead work: unused bodies, WWorldPos records, forward vectors and GetWeaponPosition answers; GetRigidBody called two
  to five times; arguments not read; a dead load and a dead re-test; the spline path cleared twice; the hand brake
  set false twice; the passenger's description loaded for the helicopter.

All of these are in docs/driving/original-quirks.md with their addresses.
