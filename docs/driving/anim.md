# The driving engine's animation system

EA's actor animation system above EAGLAnim ([eagl.md](eagl.md)), and the render side's procedural animation: the
people on foot, drivers and passengers, the weapons they hold, and animated scenery. Ported on 8 October 2026 by six
packages, then merged (`src/driving/anim/`). Every address is Driving.xbe's.

**Status:** 341 functions ported, including entry points Ghidra had not made functions (FnDefaultAnimBank's eight
vtable methods, the four ActEvents handlers, four ActWeapon vtable slots, AnimationController's deleting
destructor, logic_error::what, length_error's copy constructor and others). `engine.anim` is 100% ours by bytes;
what the coverage tool still counts live are exception funclets. Checked by six shadow tests against the originals
(missions 1 and 4), and by lockstep runs of missions 1-8: frames identical to the baseline, and the sound call
traces too.

## Layout

| File | What |
|---|---|
| `anim/Actor.h/.cpp`, `Controllers.h/.cpp` | ActActor (a placed animated thing: its matrices, poser, character, controllers), ActActorDatabase (a PointerList of actors; culling, update and draw of all), the animation controllers (standard and cross-fade), VU0_quatstoangvel |
| `anim/AnimationDatabase.h/.cpp`, `Character.h/.cpp` | ActAnimationDatabase, its banks (FnDefaultAnimBank) and PrivateData (ALookup.bin), ActAnimGroup, ActCharacter and ActCharacterInfo (models, skins, LOD, shadows, muzzle-flash light), the light block and character draw options |
| `anim/Events.h/.cpp`, `IK.h/.cpp` | ActEvents (events keyed to animation time) and its handlers and resolver; ActIK, ActIKSolver(Array), ActGlobalPoseOverrideArray |
| `anim/Manager.h/.cpp`, `Model.h/.cpp`, `Skeleton.h/.cpp` | ActManager (start-up, shut-down, the infrared mode), ActModel and ActModelDatabase, ActTextureDatabase, ActSkeleton and ActSkeletonDatabase |
| `anim/Poser.h/.cpp`, `Weapon.h/.cpp` | ActPoser (pose evaluation, cross-fades, manual mode) with its matrices and cross-fade blend data; ActWeapon (held weapons, muzzle flashes, shell casings) and ActWeaponDatabase |
| `anim/AnimEngine.h/.cpp`, `ProcAnim.h/.cpp` | RAnimEngine: Handle and System (the animated scene objects' systems, stimuli, frames), Update, EvaluateInstance; GetInstanceMatrix and the 15 proc-anim functions of the table at 0x001c41c0 by `CARP::Instance::procAnimType` |

Shared code the system brought with it now lives where every user can reach it: the game's `std::string` and
`logic_error`/`length_error` in data/StdStreams.h (GameStd), `std::list<T*>` in world/Targeting.h (PointerList),
the URefCounter tree code in engine/URefCounter.h (RefCounterTree), and EAGL Transform's rotation and quaternion
helpers in eagl/Transform.h. `CARP::AnimInfo`/`AnimKey` are in data/Carp.h.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_ACTORSHADOW` | first tick | VU0_quatstoangvel (4000 cases), the controllers, fake and copied live actors' matrices with real ground lookups, culling, the list and string code |
| `NIGHTFIRE_CHARACTERSHADOW` | first tick | every bank's methods and lookups, anim groups, character infos (live and made up), the lights, draw options, the characters' queries on copies |
| `NIGHTFIRE_IKSHADOW` | first tick | the IK maths helpers, ActIK on random rigid frames and targets, the solver arrays, pose overrides in every mode |
| `NIGHTFIRE_ACTMODELSHADOW` | first tick | the model and texture trees, the live skeletons' bone queries and blends, ActSkeleton's construction on every skeleton file, the infrared mode |
| `NIGHTFIRE_POSERSHADOW` | first tick | every live poser at perturbed times and flags, cross-fades, manual mode, weapons, the weapon tree (the channels' cached keys cleared before each case) |
| `NIGHTFIRE_ANIMENGINESHADOW` | first tick | GetInstanceMatrix on every track instance and 30,000 perturbed ones across every proc-anim type, handle pairs driven through 160 random steps each |

## What the port taught

Five port bugs, each one the shadows or the lockstep frames showed:

- **A float product kept in float.** In Transform::BuildRotation, four elements multiply two stored floats and add;
  the original keeps the product unrounded on the x87 stack (`fld; fmul; fadd`), the port rounded it: 1-ulp
  differences in the IK's middle joint.
- **The wrong matrix's translation.** ActActor::CalculateMatrices looks the ground up at the local matrix's
  translation; the port used the world matrix's, so drivers and passengers got the wrong ground face (their shadows).
- **Another function's constant.** The swinging billboard (proc-anim 238) scales its angle by 0.1; the port took the
  sway's 0.33 from the constant next door. Mission 1's frames showed it.
- **A stack slot misread by two pushes.** ActIK::Solve builds its pole rotation from the quaternion product; the
  port read the argument two pushes off and took the bend axis's z instead.
- **A placeholder's bytes in the wrong order** in ActCharacterInfo's model path (overwritten before use).

And two lessons about testing: animation channels cache their last decoded key, so the number and order of
evaluations matters to the last bit - a shadow must clear the cache before each case; and synthetic inputs must stay
inside what the data can be (an animation index past an article's list faults both sides).

## Odd things in the original, kept

- SetGlobalPoseOverride in global mode swaps rows 0 and 2 for every override, then negates row 2 for index 0 and row
  0 for index 1.
- ActCharacterInfo leaves its type unset for a model letter other than h/p/c/a; ~ActEvents leaks its physics-off
  handler; GetLight returns the colour rotated, SetLight stores it straight.
- ActWeapon's constructor sets the manual-render flag, so its vtable Render never draws; ~ActWeaponDatabase frees every
  weapon it loaded whatever the reference count says.
- Handle::Stop and DeactivateSystem move the active list down by one entry more than it holds; ProcAnimSway reads the
  instance position after clearing it, so instances of one size sway in sync.
