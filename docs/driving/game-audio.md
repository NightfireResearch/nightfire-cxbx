# The driving engine's gameplay audio

The game tier's sound objects: AVehicle, the engine sound every car and helicopter carries, with its engine models
(AEngine, ARaceEngine), horn and wind, and the kinds the vehicles make (APlayerVehicle, ATrafficVehicle,
ASnowMobile, ASubmersible, AUltraLite, AHelicopter, APlayerHeli, APlayerTank, AFingoDeath); the weapon and world
sounds (AGun, ATurret, ASentry, ASmackable, ATargeting, ASceneObj, AWorldSound); the characters' speech
(ACharacter); and DAudio, the debug HUD's audio pages, with the small library code the linker put beside it (the
locale helpers, the debug menu items' virtual methods). They sit on the audio framework (ASoundManager, the sounds
and voices, docs/driving/audio.md) and the sound library, both ours. Ported on 11 October 2026 by four packages,
then merged (`src/driving/game/`, `src/driving/data/`). Every address is Driving.xbe's.

**Status: tested (11 October 2026).** 154 functions ported, including five entry points no function list had (ASmackable's GetName, Play,
PlayLanding, PlayCollision and IsStillActive, slots 2-6 of its vtable 0x001a2f38, which Ghidra had not made
functions). `game.audio` is 100% ours by bytes: 150 replaced, 205 dead (exception funclets), 11 live - all
exception funclets at 0x00155a10..0x001569e0 with no caller the tool can see (their owners are replaced). Four
shadow tests compare the ports with the originals on copies of the live sound objects (about 41,000 cases on every
mission, 12 live vehicles), all with no differences; lockstep runs of missions 1-8 to tick 9000, idle and
accelerating, match the baseline in their sound call traces and frame for frame (every engine, horn, wind, weapon
and speech sound goes through this code).

## Layout

| File | What |
|---|---|
| `game/VehicleSound.h/.cpp` | AVehicle (0x1f0, vtable 0x001a2680, on ASceneObj): construction, the activations (gun, turret, zoom, wind), the horn choice, speed and rpm as heard, Play (scrapes, skids, rumbles, bumps, then wind, horn, gun, turret, zoom and car alarm), PlayLanding, PlayCollision; AHorn; ATrafficVehicle and AHelicopter's layouts (the vehicles' code reads them) |
| `game/VehicleSoundEngine.h/.cpp` | AVehicleWind, ASoundPlayParams::Compute (volume, Doppler pitch, azimuth and distance for a listener) and ListenerView::Azimuth, AEngine and ARaceEngine, URefCounter<AEngine>'s compiled tree code (EngineRefTree) and the one RefCounterPair constructor every URefCounter shares |
| `game/VehicleSoundKinds.h/.cpp` | APlayerVehicle, APlayerHeli, AUltraLite, ASnowMobile, ASubmersible, APlayerTank, and ATrafficVehicle's and AHelicopter's methods |
| `game/WeaponSound.h/.cpp` | ASceneObj (0xd0, vtable 0x001a2f80; the weapon flags), AGun, ATurret, ASentry, ASmackable, ATargeting, AWorldSound's destructor and Play, AVoice::Rewind |
| `game/CharacterSound.h/.cpp` | ACharacter (speech lines picked by the first letters asked for), MakeCoord3 |
| `game/FingoDeathSound.h/.cpp` | AFingoDeath (an AVehicle with a gun, a turret and a reload loop; slot 9 SetRechargePercent) |
| `game/DebugAudio.h/.cpp` | DAudio's pages (InfoDraw, MixerDraw, the targets) and DAudioTargets, its std::list |
| `data/StdLocale.h/.cpp` | locale::facet's reference count and destructors, ~locale, ios_base::getloc and its deleting destructor (members of StdStreams.h's Facet, Locale, IosBase) |
| `data/DebugItem.h/.cpp` | DebugItem's and DebugData's small virtual methods (DebugVariables.h's classes; vtable 0x0018bc78) |

The classes merged into their owners: AWorldSound's fields after +0x118 (Sound.h: the delays, repeats, mode, wait
and pass voice), ASoundPlayParams +0x0c (`facing`) and +0x14 (`distance`), AListener's `view`, `level` and
`bandWidth` (Mix.h), DebugItem as DebugData's base with its counted name and flags. The tables 0x0018bc6c..
0x0018bd4c are vtables: the facet's, ios_base's, DebugItem's (20 slots), basic_ios and istream's, ctype<char>'s and
DebugVariable<unsigned int>'s (21 slots).

All the sound objects' virtual methods keep their slots. Slot 5, PlayCollision, is `(float strength, const Coord3
*position, const Coord3 *velocity, int surface)` in every class: its callers in the collision code (0x0004a067,
0x0004a3d5, still original) pass two stack vectors by address and a surface code (0, 1, 4, 5, 8, 9, 10, 13 or 15),
and AVehicle's override compares the fourth argument with surfaces 1, 5 and 13 (as the PS2 build's does). Slot 6,
IsStillActive, returns `bool`: its caller (Smackable::Simulate, 0x000754cc) tests only AL.

## Tests

| Variable | When | What |
|---|---|---|
| `NIGHTFIRE_GAMEAUDIOSHADOWA` | first tick | AVehicle, AHorn, AVehicleWind, AEngine, ARaceEngine and the play parameters on copies of up to 12 live vehicle sounds (with their horn and wind) and every live engine, perturbed; the object, its allocations, PlayCollision's tick table and gate counter and every call out compared |
| `NIGHTFIRE_GAMEAUDIOSHADOWB` | first tick | Play of the eight kinds on live copies found by vtable and on synthetic sounds, with the kinds' constructors, destructors, IsTracked, GetName, SetCreakLevel, PlayLanding and AddSiren; AVehicle's and the framework's calls replaced by recording fakes |
| `NIGHTFIRE_GAMEAUDIOSHADOWC` | first tick | ASceneObj, AGun, ATurret, ASentry, ASmackable, ACharacter, AWorldSound, ATargeting and AFingoDeath on copies of the live sentries, smackables, characters, world sounds and vehicles with a gun or turret (about 6,700 cases) |
| `NIGHTFIRE_GAMEAUDIOSHADOWD` | first tick | the locale helpers, the list operations, DebugItem with a recording vtable, UpdateTargets over the live sound list (perturbed), the draw functions with recording fakes; the original side works on the game's copy of the target list, which the shadow constructs for the test |

The fakes are hooked at the original's address and at our entry (the ports call each other directly). Not
covered by a shadow, tested in game: EngineRefTree's seven entries (the same code as URefCounter<AMix>'s copies but
for the addresses they call).

In game: the sound call trace (NIGHTFIRE_SNDTRACE with NIGHTFIRE_SNDLOCKSTEP) of lockstep runs of missions 1-8, idle
and accelerating, to tick 9000, against the pre-port baseline; the frames should not change at all. The pages DAudio
draws are provisional: nothing in a shipped run opens them, and each says so once (an untested warning) if it runs.

## What the port taught

- **The first test passed.** Four packages ported in parallel, merged by one integration pass that checked the
  generated code against the packages' own, came through the shadows and the lockstep sound traces with no
  differences - the sound call trace in lockstep is as exact a check for audio as the frame dumps are for drawing.
- **Find a class's base before laying it out.** AVehicle derives from ASceneObj (vtable 0x001a2f80), which sat in
  another package; the field names its users had (weaponFlags, flagC2...) were renamed once its own methods showed
  what the bytes are.
- **Linker-folded code** shows up as one function serving unrelated classes (DAudio's target list erase is WGrid's
  mover list erase; one GetName serves three vtables): port it once and call it from both.

## Odd things in the original, kept

Every area's entries, by kind and with addresses: docs/driving/original-quirks.md.

- AVehicle::CalculateSpeed: with a horn, a speed between 5 and 14 becomes 14. CalculateRpm takes the log through
  FLDLN2/FYL2X as the x87 makes it. The base Play (0x00017550) is empty and its call is left out.
- PlayCollision on paved ground plays the tyre sound when `s0 || (s1 && s2) || s3`; its search of the impact
  sounds steps past busy entries without a bound, so the tick table (0x00243988) stays at the game's address with
  the globals after it. ARaceEngine::Play asks for the mix's volume twice.
- APlayerVehicle::Play picks the road voice by surface through a jump table (paved, gravel, cobble, ice, snow, two
  off-road surfaces; any other has none). The snowmobile's landing one-shots are built on the stack and destroyed
  at once, so never heard; its rev voice leaks; PlayCarving reads one word past the surfaces. The submersible's
  creaks index the level without an upper bound, and its jet fades by the double 0.2 but plays at the float 0.2.
  AUltraLite keeps the old pitch target for a NaN climb. ATrafficVehicle's destructor reads two counted flags its
  constructor never sets.
- Two of ACharacter's speech lists lack a comma between two names, which join, so the lists end in NULL entries
  and AIndex::Lookup(NULL) is called; the "F" lines' wrapped flag is never set. The gun fires only while the firing
  byte is exactly 1; AGun +0x18 is never written; a new one-shot and the voice loops go unchecked for NULL.
  AWorldSound's destructor does not free its pass voice. A smackable's roll volume rises by two steps and falls by
  one. ATurret's destructor uses the out-of-line AVoice destructor.
- DAudio: MixerDraw's editing mode pauses the game every frame; a mix group above 6 reads its colour past the
  table; mix and patch names are passed as format strings; InfoDraw prints a stack byte for a StreamHold outside
  0..2 (ours prints 0); UpdateTargets passes the camera's unknown50 as the listener's velocity. The target list's
  erase is one copy for it and WGrid's list of moving elements, whose nodes are the same size.
