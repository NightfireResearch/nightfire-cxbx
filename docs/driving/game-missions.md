# The driving engine's missions

The game tier's mission layer: Simulation (the world of game objects, their containers, the step that drives them),
SMissionManager (the mission's state, scores and counters, success and failure), and the mission rules (SMissionRule,
SMissionTimer, SMissionRuleTarget and the SRule* tests). They sit on the engine (physics, world, scheduler, events)
and the game tier's vehicles, audio, effects and weapons (docs/driving/vehicles.md, game-audio.md, game-effects.md,
game-weapons.md), all ours. Ported on 12 October 2026 by four packages, then merged (`src/driving/game/`). Every
address is Driving.xbe's.

**Status: ported, untested (12 October 2026).** 202 entries, all `FUNC_AT`: 44 of Simulation's smaller methods and
the containers' compiled code, 25 of Simulation's constructor, destructor, step and spawns (with
DeletePhysicsObjectThunk, the 5-byte jump at 0x000b4740), 80 of SMissionManager, 53 of the rules (50 listed, plus
three Ghidra missed: SMissionTimer::Construct 0x000b95d0, SMissionRule::Delete 0x000b9cb0 and SRuleProperty::CheckRule
0x000ba560). `game.missions` is 100% ours by bytes: 199 replaced, 73 dead (exception funclets), 1 live -
FUN_00154250, an exception funclet (8 bytes) with no caller the tool can see. Four shadow tests compare the ports
with the originals; the lockstep runs and the shadows are still to be run.

## Who reaches it

The engine and the game tier call Simulation and the mission manager directly (GameLoop's reset, clean-up and
player car, the static initialisers' construction of the one Simulation, the world's level load and its tracked
instances, the weapons, vehicles, cameras and effects), and since 12 October the mission events too
(docs/driving/game-events.md). Still original: the AI (about 100 calls) and the front end (about 20) call them by
their original addresses, which jump to ours.

## Layout

| File | What |
|---|---|
| `game/Simulation.h` | Simulation (0xf24, one, `fgSimulation` at 0x00233ff0): the body pool (64 rigid and 96 simple bodies, their signatures and owners, `simpleBodyHits`), the pause state, the step count and rate, a vector of each kind of object (typed: `cars`, `missiles`, `shells`...), the tracked instances, the object multimap and the dead set; `SimulationState`, `TrackedInstance`, the signature constants. `PhysicsObjectType` is physics/PhysicsObject.h's |
| `game/SimulationObjects.h/.cpp` | the pause state, the slots and signatures, DetectCollisionsSRB, the lookups, CanSpawnRigidBody, the reflective object, the tracked instances, DeleteDeadObjects |
| `game/SimulationContainers.h/.cpp` | the compiled std::multimap/set/vector code: PhysicsObjMap, DeadObjectSet, SimPointerVector and the pointer helpers - shared by every container of the same shape in the game (the linker folded them), so the scheduler, the action queues, the singletons, the collision lists, the symbol table, the particles, the vehicle particles and the audio streams call them too |
| `game/SimulationSpawn.h/.cpp` | Reserve and InsertUnique, DeletePhysicsObject, CleanUpObjects, CanSpawnSimpleRigidBody, TrackInstance, Reset, SimulateGame, SpawnExplosion, SpawnPhysicsObject, the constructor and destructor, and the eleven Spawn*Object (each calls its class's constructor) |
| `game/MissionManager.h/.cpp` | SMissionManager (0x1c70, one, `glbMissionManager`): lifetime, Load, Reset, the rule, objective and message lists (`MissionList`, whose `BuyHead` every list of pointers in the game calls), state, cinematics, the drive missile, the rules, Update, the objectives; `MissionSet`, `MissionState`, `MissionScore`, `MissionEvent` |
| `game/MissionScores.cpp` | health and shield, the shot, hit and kill counts, the magic counters, timed actions, CalcEndMissionScores, TallyScores, ClearScoreBuffer |
| `game/MissionRules.h/.cpp` | SMissionRule and the fifteen SRule* classes (vtables 0x001935c0-0x00193654), Instantiate, `MissionRuleRecord` (the level's 'Rule' data, which the trigger manager reads too), SMissionRuleTarget, SMissionTimer (the manager holds ten), CompareWithinDegrees, StubError |

Merged at integration: physics/Simulation.h and engine/MissionManager.h, the provisional views, are gone; every
`Sim`, `SimState`, `SimStepCount`, `SimTimeStep`, `SimStepsPerSecond`, `SimulationRandom` and player-car define
across anim/, camera/, engine/, game/, physics/, render/ and world/ is a field of `fgSimulation`, as are RigidBody.h's
and PhysicsObject.cpp's body tables, the weapons' object lists, PlayerCameraC's explosions and RenderHigh's car list;
`SimpleBodyHits` is engine/SimRandom.h's `BitSet192` (now with a three-word view); one `PhysicsObjectType`. The
mission manager's views fold into SMissionManager (`missionState` with `kMissionWon`/`kMissionLost`, `difficulty`,
`superEasy`), MissionLaunchPage's score fields into engine/GameLoop.h's `LaunchPage` (with one `Launch` define and
one `LaunchFlags` enum, the weapons' flags among them), the timers into `SMissionTimer`, the rules' weapon slots
into the weapons' `WeaponRecord` ("NOBOOST" and "ONEEMP" read `shotsFired`), the AI element views (the manager's,
the rules' and data/Carp.cpp's record) into `CARP::AIElement` (data/Carp.h), the rules' mine view into `Mine`, the
hands calls into game/AIViews.h (now the AI's own classes, docs/driving/game-ai.md), and `gPlayerCanNeverLose` (0x00239218, still read by EPlayerLose) into
MissionManager.h. Every call between the packages, from the missions into the weapons (the constructors, Smackable's
GoToSleep, SWeaponManager's Reset/LoadWeapon/SetSecondary, Missile's steering) and from the rest of the tree into
the missions is direct; `tools/static_init_driving.py` constructs `fgSimulation` and calls `MissionList::BuyHead`
(static now: it never read `this`).

The globals only our code touches are ours: `NextSignature` (0x001c8fbc, 1 at start), `SimDeleteInProgress`
(0x00233fd0), `MissionCinematicMode` (0x00243a64), `DefaultMissionSet`, `MissionHealthTimes100` and the car-override
flags (0x00239219-0x0023921e, never set), `CharInfoBeforeHud`, and the maths header's constants the static
initialisers made for these units and nothing reads (0x00233fd4-0x00233fe4, 0x00239224-0x00239238; the generator
leaves those stores out). Still at the game's address: `fgSimulation` and `glbMissionManager` (the mission events, AI
and front end use them), `gPlayerCanNeverLose`, `DamageSourceSig` (0x00233fe8) and the scratch pad after the
Simulation (0x00234f20).

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GAMEMISSIONSSHADOWA` | first tick | Simulation's smaller methods on a perturbed copy of the live one, the trees node by node, the vectors' insert, erase and helpers, synthetic state for ReleaseRigidBodySlot, SetStartConditions, DetectCollisionsSRB and the untracking; fakes for GoToSleep and EndMissileCamera |
| `NIGHTFIRE_GAMEMISSIONSSHADOWB` | first tick | about 900 cases, each on its own deep copy of the live Simulation (scalars, map and set shape and colour, the 13 vectors, object identity), plus the body pool, the collision manager's flag and the launch page: Reserve, InsertUnique, DeletePhysicsObject and the thunk, SpawnPhysicsObject for every type, CanSpawnSimpleRigidBody, the tracked instances, SimulateGame, Reset and CleanUpObjects, SpawnExplosion, the eleven spawns (the constructors faked), the constructor and destructor |
| `NIGHTFIRE_GAMEMISSIONSSHADOWC` | first tick | all 80 SMissionManager functions on one perturbed copy of the live manager with synthetic lists per side; the launch page, the cinematic global, the scheduler's time scale and a call log compared; every outgoing call faked |
| `NIGHTFIRE_GAMEMISSIONSSHADOWD` | first tick | every rule constructor, Instantiate (types -3..20 and a failed allocation), every deleting destructor, every CheckRule over every target type (with NaNs), DoRule, RunEvents, the target methods, the timers, Crossed and CompareWithinDegrees, on copies of the live player body, manager and weapons |

Each shadow swaps its package's range back for the original's run; the fakes are hooked at the original's entry and
at our port (the ports call each other directly; BondCarShadowA and D now hook the mission manager's ports too). The
mutations each was checked against (by reading): A, the signature counter's wrap (0 for 0x100) and CanSpawnRigidBody's
`>=` for `>`; B, the humans' and mines' cases swapped and the cars' end test dropped; C, IncShotsHit dividing by 5;
D, Crossed's `>` for `>=` and ProgCounter's any/no bits swapped.

In game: lockstep runs of missions 1-8, idle and accelerating, to tick 9000, frames and sound traces against the
baseline. They reach Simulation's constructor, Reset, SimulateGame, AdvanceStep, DetectCollisionsSRB,
DeleteDeadObjects, the slots and getters, CleanUpObjects, TrackInstance and the car and human spawns every run; the
mission manager's constructor, Load, ChangeStage, Start and Update every tick (UpdateCharInfo, UpdateObjectiveSpecial,
UpdateTimedActions, ProcessRules), the event counters; and the rules' Instantiate, constructors, DoRule, CheckRule and
the timers' GetValue. What only the shadows reach (or a played mission): Pause/UnPause, SetStartConditions,
DetonateRemoteMines, UntrackInstance, EndMissileCamera, the weapon, newton, sentry and smackable spawns,
DeletePhysicsObject and its newton path, the destructor; Win, Lose, CalcEndMissionScores and TallyScores, SkipCinematic
and the fades, the drive missile, SetAutoDrive, the cheats and power-up boosts, the restart path, Destroy; RunEvents
and the once flag, the timers' Start/Stop/Increment/SetTicking and beeps, the Ammo, Property and PlayerDir rules and
the other difficulties. To test specially in game: a mission won and one lost (the end-of-mission scores and the
launch page's totals), a cinematic skipped, the drive missile, an auto-drive section, and a remote mine detonated.

## What the testing taught

The first round (gm1) had every lockstep run exact, but every shadow run stopped inside shadow A, and
GameAudioShadowB and GameAudioShadowD, clean before, differed. All of it was harness:

- **A global made ours needs both copies set.** NextSignature (0x001c8fbc) and MissionCinematicMode (0x00243a64)
  became ours in this integration. Shadow A, GameAudioShadowB and shadow C still set only the game's copy, so the
  original read the shadow's value and the port its own: AssignRigidBodySlot's counter one apart and the new
  signature's low byte wrong; ASnowMobile::Play with no volume on the original's side in the one case in five that
  is cinematic (154-204 differences). The shadows now set both copies, compare each side's own and put both back.
  NextSignature is declared in SimulationObjects.h, so tools/owned_globals.py lends it.
- **One memset per element, each the whole array's size.** Shadow A's DetectCollisionsSRB cases filled
  simpleBodyHits that way, up to 1.8 KB past their heap copy of the Simulation: the heap corruption (0xc0000374)
  that ended the runs, and the faults at address 0 before it on some missions.
- **A set ordered by address.** DeleteDeadObjects walks the dead set, which is ordered by map node address. Each side
  builds its own map, so the two deleted in different orders. The deletions are compared by their place in the set
  and which objects went, and the missile camera's end by the object deleted after it.
- **A hook table that fills up quietly.** GameAudioShadowD had room for 16 hooks. With PauseSimState and Stop ported,
  each of its nine fakes takes two (the entry and our port), so IFeedback::Pause's port went unhooked, and so did
  ASoundManager::Pause: the port's side paused the feedback for real and logged nothing (8-15 MixerDraw cases). It
  has room for 32 now, and a full table prints a line.
- **A fake refused on an entry that became ours.** GameWeaponsShadowC would only fake an original whose entry had no
  jump. With the Simulation's getters, slots and SpawnExplosion ported it gave up ("not run: fakes missing", which
  still says 0 differ, so the summary missed it). It now hooks our port behind the jump, where the originals' calls
  and our ports' direct calls both arrive. A port that is a five-byte tail jump counts as hookable.
- **A heap address in a call log.** Shadow C's Create+Destroy logged calls that name the new manager, which operator
  new places differently on each side (mission 3). They name it by offset now. round.sh now lists a shadow's
  "- not run" line among the differences.

With these, the second round (gm2) had all 16 lockstep runs exact against the baselines and every shadow run
finished on missions 1-8 (a frame dumped), the four missions shadows, GameAudioShadowB and D and GameWeaponsShadowC
with 0 differences; only the two known ones remain (sndsysshadow case 0, sndstreamfileshadow case 52; both harness problems, fixed since: sound.md 9.6). No game behaviour
changed (only an extern added to SimulationObjects.h), so no quirk or deviation was added.

## Random calls

| Function | Draws |
|---|---|
| Simulation::Construct | SimRandom::Construct for the Simulation's generator |
| Simulation::Reset | SimRandom::Reset |

The mission manager and the rules draw nothing.

## Known deviations

Places where the original reads stack it never wrote; the port uses zero:

- SMissionManager::UpdateObjectiveSpecial (0x000b81a0): the w of three Coord4s (only x, y and z feed the dot
  product).
- SRulePlayerDir::CheckRule (0x000ba690): the w of three stack vectors, which nothing reads.
- Simulation::Construct (0x000b4fe0): the object map's and the dead set's allocator bytes; nothing reads them.

Other differences, none observable:

- MissionList::BuyHead (0x000b8490): when FastAlloc fails, the original stores NULL at address 4; the port skips it.
- SMissionManager::Destruct (0x000b8e70): the ten empty timer-destructor (NullFunction) calls are dropped;
  SMissionManager::Construct (0x000b8c30) builds the 64 stage lists inline instead of calling 0x0011cc10.
- SimPointerVector::InsertN (0x000b3b80) has no catch block, the containers' throws go through TreeThrow, and Reserve
  (0x000b4080) returns after Xlen.
- Simulation::SpawnExplosion (0x000b4cd0): a clean bool for the simple flag where the original's dword carries
  left-over upper bytes (only the low byte is read).
- SRuleRange::CheckRule (0x000ba570) zeroes four words of its position, not three; SRulePlayerDir writes `-angle`
  for `angle * -1.0`; SRuleTimer's threshold goes through QuietNaN; the rules' FastAlloc name is our own literal.
- The callers of the shared vector helpers no longer push the two words uninitialized_fill_n never reads (the
  scheduler, action queues, singletons, collision lists).

## Odd things in the original, kept

- Simulation: CanSpawnRigidBody reads the global Simulation's bodies, only puts a smackable that is not awake to sleep,
  and at exactly the limit neither sleeps nor spawns. DeletePhysicsObject searches only the first `count` nodes of the
  type and may put the node after them (perhaps the end) in the dead set; it erases from the vectors other than the
  cars' without checking. CanSpawnSimpleRigidBody deletes every newton (through the global Simulation) once past 72.
  DeleteDeadObjects reads an object's type before its NULL check. UntrackInstance erases one element. The
  constructor leaves several fields unset and gives the grenades and tracked instances no reserve. A failed
  allocation in a spawn hands NULL to SpawnPhysicsObject, which faults. SpawnExplosion's last argument,
  SpawnExplosionObject's second and CanSpawnSimpleRigidBody's position are never read.
- SMissionManager: PassObjective adds to glbMissionManager's scores, not this one's. IncShotsHit's window is a tenth
  of the step count. Load writes 1.0 into the level's mission-set data when its first glue factor or text value is 0.
  Score 4 is always 0; score 3 is the set's unknown2e0 over the shots; score 9 the mean of the accuracy and score-3
  shares. AddObjective and InsertObjective test the size after inserting. The constructor leaves missionSets unset.
  UpdateCharInfo clears a flag nothing reads; two dead reads in the objective code.
- Rules: SRuleDeath passes for target types other than the player and an element; SRuleDamage with the AI figures
  never updates lastHealth; SRuleRange keeps the distance but tests no crossing; unknown target types answer 0 or
  false or leave the output alone; SRuleProperty's eight-character name is unterminated; CompareWithinDegrees answers
  1.0 for a NaN; RunEvents re-reads the count each pass after a dead store; the constructors write the base vtable,
  then their own.

All of these are in docs/driving/original-quirks.md with their addresses.
