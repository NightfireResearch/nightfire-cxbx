# The driving engine's mission events

The game tier's mission events: the 223 small event classes the mission scripts, triggers, animations and the
scheduler's tasks raise - the AI's orders, the cameras, the player's controls and auto-drive, objectives, counters
and messages, path and sound events, spawns, power-ups, timers, streams and the per-tick update events. They sit on
the event framework (EventManager, the scheduler, RegisterEvent: docs/driving/core-data.md) and drive the
missions, weapons, vehicles, audio and effects (docs/driving/game-missions.md, game-weapons.md, vehicles.md,
game-audio.md, game-effects.md), all ours; the AI and the front end are still the game's. Ported on 12 October 2026
by four packages, then merged (`src/driving/game/`). Every address is Driving.xbe's.

**Status: ported, untested (12 October 2026).** 667 entries, all `FUNC_AT`: 167 of package A (E007Logo to EEndMission)
and the six functions of other classes placed among them, 129 of package B (EExit to EPathSFX, with EFireWeapon, and
two Ghidra missed: EHideInstance's destructor 0x00040300 and its scalar deleting destructor 0x00047b50), 221 of
package C (EPathSFXUpdate to ESetRecSpeedOffset, with ECameraLockOn's make callback and the path helper) and 144 of
package D (ESetRecordedGameTime to the end, with Event's own destructor). `game.events` is 99% ours by bytes: 667
replaced, 96 dead, 30 live - all exception funclets (0x0015xxxx) of replaced functions. Four shadow tests compare
the ports with the originals; the lockstep runs and the shadows are still to be run.

## The event model

- **An event is an Event with its data after the vtable** (src/driving/EventManager.hpp), allocated from the
  event buffer by Event's operator new. The event manager runs each event by destroying it: `RunEvents` calls the
  vtable's one slot, the scalar deleting destructor, with flags 1.
- **Every event vtable has one slot**, the scalar deleting destructor (`Delete` here). Event's own vtable is
  0x0018c3ac, its slot Event::Delete (0x0003db30); the events' vtables are single words from 0x0018c3b0 to
  0x0018c83c. The PS2 build's vtables have a second slot, GetEventName; the Xbox's do not.
- **The work is in the destructor.** Each class's destructor (`Destruct`, the entry the vtable's slot calls) sets
  the class's vtable, does the event's work and puts Event's vtable back - Event's destructor (Event::Destruct,
  0x00043640, Ghidra's SomeGenericEventProcess), inlined. The work is written as the class's `Process()`, which the
  compiler inlined into its one caller; "Process" is our name - neither the PS2 symbols nor the GameCube build have
  a Process or Execute, the work being in the destructor there too. Ghidra's names for these functions are
  unreliable (its "EFireWeapon::~EFireWeapon" at 0x0004ac40 is the 7 KB main body): each method is named by its
  slot. `Event::DeleteEvent` and `Event::DeleteBaseEvent` are the scalar deleting destructors' shape (the class's
  destructor, or Event's, then the bytes given back).
- **The make and resolve callbacks** are named as the GameCube (AUF) build names them:
  `<Class>_MakeEvent_Callback` builds the event from its record of the CARP data (and, for some, from what raised
  it, `gEventDynamicData`) - RegisterEvent's table at 0x0018bf38 (282 entries) holds them, and Ghidra calls them
  "GetEventName"; `<Class>_ResolveEvent_Callback` turns the record's references into addresses when the track
  loads (CARP::EventListResolver) - the table at 0x001b6660, unnamed in Ghidra. The linker shares code across
  classes: one resolver serves 37 entries (0x0003f500), another 24 (0x00041fe0), another 11 (0x000464b0), several
  make callbacks serve two event numbers, and 0x00048680 is the one scalar deleting destructor of eight vtables.
- **Work in the constructor, or none.** EChangeMaxTraffic, EChangeStage, EForceNearPedRespawn, EStartTimedAction,
  EStartTimer and EStopTimer do their work in their constructors and have Event's destructor; EAddObjective,
  EAddScoreObjective, EBeginDownloadCode, EDisplayObjectiveText, EFailMissionObjective, EPassMissionObjective,
  EPassObjective, EInsertObjective, EPowerUpBraking, EPowerUpTopSpeed and EPowerUpInvulnerable do nothing at all
  (their destructor is Event's, through the shared deleting destructors).

## Who reaches it

RegisterEvent's tables (the trigger events, the animation frames' events, the scheduler's tasks: EScheduleEvent's
and the per-tick update events EAIUpdate, EAnimUpdate, EAudioUpdate, ECameraUpdate, ERenderFrame, ESimFrameUpdate,
ESimEndFrame), and directly: the physics' collisions (ECollision's constructor from BondCarPhysics and
RigidBodyCollide), the broken windows (EHitWindow's from WCollisionMgr::CheckHitWindow), Simulation::SpawnExplosion
(ESpawnExplosion's), the mission manager's restart (ERestart's) and the HUD's (EExit's, from GOrthoHudView, still
original).

## Layout

| File | What |
|---|---|
| `game/EventsA.h/.cpp` | E007Logo to EEndMission (57 classes): the per-tick updates, the AI orders (EAICommand, the element events), the player's car and auto-drive, the collision event (ECollision, with its zone record), the cameras (ECameraLockOn, whose make callback is in C), power-ups and triggers, the mission's counters, messages and end |
| `game/EventsB.h/.cpp` | EExit to EPathSFX (43 classes): the HUD and the mission's end, objectives, the trigger events, the AI's kills and activations, objects (kick, kill, inflict damage, hide), muzzle flashes, the nuclear blast, the path throttle events and the path sound; `PathSFXUpdateData` |
| `game/FireWeaponEvent.h/.cpp` | EFireWeapon: a shot from a weapon the CARP data or a holder fires - aim, spawn (the weapon's shell, missile, mine, grenade or ray shell, by `DrivingWeapon`), sound and force feedback |
| `game/EventsC.h/.cpp` | EPathSFXUpdate to ESetRecSpeedOffset (73 classes): the path sound's update, the path setters, pause and resume, actor and camera animations, effects and sounds, the player's win, loss and immunity, power-ups, the restart, the mission's settings, the render frame; `EPathUtil_GetFromInstanceOrAI` |
| `game/EventsD.h/.cpp` | ESetRecordedGameTime to the end (46 classes): the mission's settings, the simulation's frame events, spawns (explosions, force effects, sentries, smackables, newtons, traffic), the effect events (`EEffectEvent`), streams and subtitles, timers, the stinger channel, target beacons, smackables woken |
| `EventManager.hpp/.cpp` | Event's own destructor and scalar deleting destructor, `kEventVtable`, and the deleting templates the events use |
| elsewhere | the other classes' functions the linker placed among the events, each a member of its class now: CollisionImpact::Construct (physics/RigidBodyResolve, 0x0003dc20), AVoice::Destruct and PlayOnce (audio/Voice, 0x0003f400, 0x0003e6b0), AOneShotSound::Construct and ConstructAt (audio/Sound, 0x000476f0, 0x000477d0), RandomWord (render/RSceneObj, 0x0003e6f0), CARP::Instance::GetMatrix4 (data/Carp, 0x0003db00) and SimRandom::GenerateBits (engine/SimRandom, 0x00040070) |

Merged at integration: the packages' provisional views (EventsACalls.h to EventsDCalls.h) are gone. The mission
manager, Simulation, weapon manager, missiles, sentries, ray shells, timers and rules are the real classes, and
every call to them is direct (as are the calls into the events from BondCarPhysics, RigidBodyCollide,
WCollisionMgr, SimulationSpawn, the mission manager and the camera); the events' PVehicle, RSceneObj, AVehicle and
ASubmersible virtual calls are those classes' members; the cinematic masks are MissionManager.h's
`CinematicSource`, EFireWeapon's weapon types the weapon table's `DrivingWeapon`, its weapon definitions
`WeaponRecord` (with `pickupText` and `ammoText`), ActWeapon's accuracy pointer `WeaponAccuracy`, the environment
description world/World.h's `EnviroDesc`. The AI the events use was in one header, game/AIViews.h (since folded
into the AI's own classes, docs/driving/game-ai.md, with `WeaponAccuracy` now the AI element's character record; it
also took in game/AICharacter.h, game/AIVehicle.h and Helicopter.h's HelicopterAI, now `AIHelicopter`, and the by-address AI
calls of GameLoop, World, BondCarSimulate, Helicopter, MissionManager, MissionRules, WeaponManager, Targeting,
Shell and WorldCamera); the AI elements are data/Carp.h's `CARP::AIElement` (type 0 a character element, 1 a
vehicle element). The front end is in one header, engine/GameInterfaces.hpp: GHud (with the HUD calls of the
cameras, BondCarSimulate, DebugAudio, the mission manager and scores, the weapon manager, RenderHigh and Targeting),
GFX, GLoadingScreen, GLocale and GSubtitles (with GameLoop's and World's); the front end's port folded them into its
classes (docs/driving/game-frontend.md). Package B's static MakeEvent members
and per-class vtable words follow the others' shape now.

Schedule::AddTask (0x0005c490) returns the task number, as the original leaves it in EAX: EPathSFX stores it and
EPathSFXUpdate removes that task when the sound ends. Before, the port returned nothing and the path sound's task
was never removed (a behaviour change, the original's).

The globals only our code touches are ours: ECollision's incidental steps (0x001e23a0, 0x001e23a4), the glass
sounds' counters (0x001e2398, 0x001e239c), EFireWeapon's gun sound counters (0x001e23a8-0x001e23b0), the render
frame's clock (0x001e2394), the explosions' sound slots (0x001e23b4-0x001e23c8) and the stinger channel
(0x001d8058, "Speech" at first). Still at the game's address: the AI's and the HUD's (the AI and front end use
them), the launch page, the game's zero vector and (0, 0, 0, 1), whose addresses go to GFX, and the stream,
subtitle and feedback switches the front end and the rest of the engine share.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GAMEEVENTSSHADOWA` | first tick | every scalar deleting destructor of package A on events built from random records in a stand-in world (AI elements, characters and vehicles, cars with fake vtables, bodies, a trigger, the mission manager, camera, renderer, world, sounds, a CARP instance and scene object), the make callbacks, resolvers and constructors, and the other classes' functions; 40 rounds |
| `NIGHTFIRE_GAMEEVENTSSHADOWB` | first tick | each class made through its make callback at its RegisterEvent address on random records and event data, then run through its vtable; about 1,400 cases, 640 of them EFireWeapon over every weapon type and kind of shooter; SimRandom::GenerateBits and the resolver directly |
| `NIGHTFIRE_GAMEEVENTSSHADOWC` | first tick | the 74 classes (make, then the scalar deleting destructor), the resolvers, the three constructors, the path helper, CARP::Instance::GetMatrix4 and Event::Delete; 40 rounds, about 3,600 cases |
| `NIGHTFIRE_GAMEEVENTSSHADOWD` | first tick | all 45 classes (make, then the deleting destructor), the six resolvers and Event's destructor; 40 rounds of about 52 cases |

Each shadow swaps its package's range back for the original's run and replaces every call out of the package -
the AI, the mission manager, the Simulation, the weapons, the HUD, GFX, the sounds, the world, the vehicles' slots,
Event's operator new and delete - with recording fakes, hooked at the original's entry and at our port behind the
jump; it compares the events' bytes, the stand-in world, the globals the events write and the call logs (pointers
as offsets into the world). Where a port now has a callee inlined, the case lifts that fake and runs the real one on
both sides: A's AVoice::Destruct (View::Destruct, on views out of the active list and without a voice) and C's
Event::Delete (Event's operator delete, the deletion point put back after each side). The mutations each was
checked against (by reading): A, EActivateAIElement clearing the trigger without kRigidType1 and ECollision's
second side testing `tag`; B, EFireWeapon's noise phase (& 0x7f) and EKickObject's +0.25; C, EPlayerLose's `< 0`
for `<= 0` and SetRunning given a raw byte; D, ESetSimRate's clamp. (D's EStreamEvent testing EAX for AL is no longer
caught: its fakes answer 0 or 1 in all of EAX, as the mission manager's functions do.)

In game: lockstep runs of missions 1-8, idle and accelerating, to tick 9000, frames and sound traces against the
baseline. They reach EAIUpdate, EAnimUpdate, EAudioUpdate, ECameraUpdate, ESimFrameUpdate and ESimEndFrame every
tick, ERenderFrame every frame, ECollision on every collision, EStartMission at the start, EFireWeapon whenever
the AI shoots, EPathSFXUpdate while a path sound plays, and whatever the missions' scripts raise in their first
9000 ticks (cameras, AI orders, spawns, streams, objectives). Not on an idle run: EPlayerWin, EPlayerLose,
ERestart, EPause and EResume. To test specially in game: a mission won and one lost (EPlayerWin, EPlayerLose and
the restart), a pause and resume, a path sound that ends (the AddTask fix: its task now removed), the stinger after
a score action, a sentry spawned and a broken window.

## What the port taught

(To come.)

## What the testing taught

The first round (gev1) had all 16 lockstep runs exact, but every shadow run ended after GameWeaponsShadowB's summary,
killed at drive_game.ps1's 20 minutes. Nothing hung or crashed for good:

- **Faults cost time.** The loader opened and closed crash.log for each of a fault report's seventy-odd lines; with
  eight runs at once that made a caught fault cost up to two seconds (30 ms in the missions round), and the new
  shadows added about 900 faults on both sides. GameWeaponsShadowC started at minute 19. The loader now gathers a
  fault's report and appends it once (and prints the DLL's load address, for its new map file: drivinginject.map);
  round.sh gives a shadow run 30 minutes.
- **A log line past its buffer.** The shadows' Vec helper formatted into 160 bytes and went on past the end
  (snprintf's return added up); IFeedback::AddCollisionInfo's 21-word impact smashed the stack of ECollision's
  cases, 256 faults that ran into ASCII. It appends to a string now (A, C, D).
- **The hook table filled up again.** Shadow B had room for 160 hooks; IFeedback::SetRumble's was among those
  dropped, so every EFireWeapon case ran the real one on a NULL feedback object (420 faults, 944 differences). It
  has 256, and says when it is full.
- **An owned global needs both copies set** (as in the missions round): StingerChannel, written by A and D into the
  game's copy only, so EAddScoreAction's port took the other channel.
- **A fault unwinds the original's destructor.** The originals' exception funclets put Event's vtable back as a
  structured exception passes through them; the port's code has none. When both sides fault, the vtable word is
  not compared.
- **Harness, each in one shadow:** C's real clock (RealClock_Div2Ticks) ticked during a case, one in 30 (it stands
  still while C runs now); C's and D's resolver cases ran the port on the record the original had already
  resolved; C's ERandomExplosion faked RandomShort, which the original inlines; C's GFX::TriggerByName fake logged
  the name's address (the port's literal is its own); C's RollSub fake read whole words where bytes are passed; D
  faked UGroup::GetArray, which the port inlines (the real one runs on the stand-in group now); signalling NaNs in
  A's AI commands, which the original copies through the x87; the skipping-cinematic fakes answered more than 0 or 1.
- **Three port bugs.** EMuzzleFlash dropped three zero stores as dead: they clear the instance frame's translation
  (0x00040d48-0x00040d5b), so the flash's direction came out moved by the instance's position. EBashCarAlongObjectAxis
  kept the w of the instance's y row where the original zeroes it in its copy (0x00049bee). ESpawnSmackable asked a
  scene object for its velocity twice where the original asks once.

With these, the second round (gev2) had all 16 lockstep runs exact against the baselines and every shadow run
finished on missions 1-8 (a frame dumped, in 14 to 25 minutes), the four events shadows with 0 differences and 80
faults on both sides between them (62 of them A's, on made-up AI elements); only the two known differences remain
(sndsysshadow case 0, sndstreamfileshadow case 52).

## Random calls

| Function | Draws |
|---|---|
| EKickObject | SimRandom::Generate, then SimRandom::GenerateBits (two Generate) |
| EKickAIElement | SimRandom::GenerateBits |
| EMaybeActivateAIElement | SimRandom::Generate |
| EFireWeapon | SimRandom::Generate for each AI roll; Noise::Noise1 twice for each AI aim |
| EFireRandomTrigger | RandomScaled (the renderer's sequence) |
| ERandomExplosion | RandomShort three times (inlined in the original) |
| ESpawnSmackable | RandomWord |

## Known deviations

Places where the original reads stack it never wrote; the port uses zero: ECinematicCamera's placement w
(0x00049e27), ECollision's zone record beyond the position (0x00049ee3), EBashCarAlongObjectAxis's impulse w
(0x00049c8b), EHitWindow's, EKickAIElement's and EFireWeapon's vectors' w, EResetPlayerCarPos's momentum w
(0x0004d56e), the GFX positions of EPlayActorEffect, EPlayEffect and EPowerUp (Coord3 locals; GFX::Trigger copies x,
y and z only), ESpawnExplosionStatic's position with neither a position nor an instance (0x00045199), EWakeupSmackable's
ray and body w, ESpawnTraffic's and EStartMission's momentum w.

Other differences, none observable: the make callbacks copy floats as bits (the original's fld/fstp would quiet a
signalling NaN); AVoice::Destruct calls View::Destruct directly where the original goes through the vector
destructor iterator; EFireWeapon passes "" where it passes an unwritten mix-name buffer (0x0004c544-0x0004c550);
setne arguments are clean bools (EForceCarStop 0x0004c7a4, ELavaHaze, ELetterBoxOn/Off, EObjectiveDisplayText,
ESpawnSentry 0x000458dd) and EStreamEvent's "nis" case passes QueuePrimary a bool (0x0004f0b2); ERenderFrame,
ESetSubtitle and EStreamEvent test all of IsSkippingCinematic's and GoingToSkipCinematic's answer where the original
tests AL (both answer 0 or 1 in all of EAX); ESetCullDistanceFactor and ESetJumpGravity write the vtable store the
original's compiler dropped (0x00043fa0, 0x00044040).

## Odd things in the original, kept

- NULL not checked: EAudioUpdate's new listener (0x000498c5, allocated 0x80 bytes for an AListener of 0x78), the
  new one-shot sounds and temporary targets of EHitWindow, EPathSFX and EFireWeapon, EPathSFXUpdate's one-shot
  (read before it is tested), EPlaySoundPos's, ESpawnExplosion's sounds, ESpawnSmackable's UseArticle after a failed
  FastAlloc, and the lookups of a dozen events.
- Arrays unchecked: ECollision's corner table, the element events' member loops (to memberCount over three
  members), EInflictDamage's index (< 64 only), EKickObject's owner index, EPowerUpTimer's and EPowerUpAmmo's
  indexes, EPlayCarCameraAnim's body slot.
- Branches: EAudioUpdate's filter ((band width + level + y) x 0.1); EAICommand disables a trigger lacking
  kRigidType1 and carries on; ECollision's second side tests unknown4a for three tags and `tag` for the fourth, with
  no corner rotation or rumble, and a non-player collision's incidental timer is the later of the two; two
  activation events print "The Helicopter hit the Trigger."; EFireWeapon gives the snowmobile and FOD guns the
  ultralight gun's damage and replaces the muzzle by the camera for the player's aim, but not for ray shells;
  EFireEventList compares its count unsigned; EMuzzleFlash's "which" is position.x < 0; ERandomExplosion adds 1.5
  before the height lookup and 1 after; four events write their results back into themselves; EPlayerLose makes an
  ERestart inside its destructor; EStreamEvent compares the name with "nis" before its NULL test; the explosions'
  "near up" test is a four-word dot; ESpawnForceEffect leaves its effect field pointing at its stack;
  ESetReflectiveObject uses the CARP element unredirected; the effect events shift by zone + 16 mod 32; the traffic
  spawn counter goes back to 0 past 32767.
- Dead work: answers asked for and dropped (GetCarpElementTracking, a second rigid body, a second AIRoadSpawn::Get,
  GetPlayerHealth and GetAudio twice, GetVelocity twice, a scalar velocity and a length), dead loads of an element's
  +0x77, MEM_totalunused twice, ESetCreakLevel's copy of the whole CarPhysics, ESpawnSmackable's unused lookups,
  ESetRenderVariation's double dereference, record fields never read. gLimitHenchSpawns is always 1 and the
  ray-shell bullets byte always 1 (the shell branch is dead).

All of these are in docs/driving/original-quirks.md with their addresses.
