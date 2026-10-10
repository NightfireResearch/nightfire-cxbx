# Function coverage by subsystem

How much of each engine's code is ours, where the rest sits, and how much of it never needs reimplementing. The
action engine's numbers are from 7 October 2026, the [driving engine's](#the-driving-engine) further down from 10 October;
`tools/function_coverage.py` regenerates them.

```
python tools/function_coverage.py                 # the summary below
python tools/function_coverage.py ai.drones       # one subsystem's live functions by module, largest first
python tools/function_coverage.py --markdown      # the summary as a markdown table
python tools/function_coverage.py --driving       # the same for the driving engine (with any of the above)
```

## How it counts

Every function Ghidra knows (`tools/functions_action.json`, 3,999 of them) is

- **replaced**: its entry jumps to ours;
- **dead**: not replaced, but nothing that can still run reaches it any more. Mostly code that only replaced code
  called, and library code the seams have cut off;
- **live**: original code that still runs.

**Done** is replaced plus dead: what no longer needs reimplementing. The plain replaced count, 725 of 3,999 (18%), is
the figure quoted so far. Replaced includes the startup layer's own patches of XAPI entry points (`WriteJump` in
`engine/XboxStartup.cpp`).

Liveness is `tools/global_coverage.py`'s call graph (its `CallGraph` class) with one difference. A pointer to an XDK
library function counts only when it sits in the game's own data. Pointers inside a library's own code and tables
(D3D's render-state jump table, DSOUND's COM vtables, XPP's callbacks) only matter once the library is entered from
outside, and the call edges already cover that. Counted naively, most of D3D and DSOUND would look live.
`global_coverage.py` keeps the naive count, which is the safe one for owning globals.

Two kinds of reference are traced to the functions that own them rather than counted as roots (both in
`CallGraph`, so the globals report sees them too):
- **a vtable slot**: a method is reached through the constructors and destructors that install its vtable (a
  `MOV [reg+disp], vtable` store is every reference to the vtable's start), so once they are dead or replaced, so
  is a method only that vtable holds. A table referenced any other way, or whose address our source names, stays
  a root.
- **code outside any function**: an instruction Ghidra left out of every function (a body it cut short, a method
  it never made a function) belongs to the function before it, as for direct calls; a table of addresses among
  the code belongs to the functions that index it. A function's reference to itself is not a caller.

Subsystems are address ranges in `tools/subsystems_action.txt`. The XBE lays Eurocom's objects out roughly
alphabetically by source file, so a module is one run of names (Anim..., BOT..., Break..., Bullet...).

Sizes are bytes to the next function, padding included. Ghidra leaves about 80 KB of DSOUND and 130 KB of XMV
undefined, so both are under-counted in functions and bytes alike.

## The surface

| Layer | XBE range | Functions | What it is |
|---|---|--:|---|
| Game code | `.text` 0x11000-0xdbcd0, plus static initialisers at 0xf5460 | 2,500 | Eurocom's portable engine and game |
| Eurocom's Xbox layer | `.text` 0xdbcd0-0xe93f0 | 272 | `psi*`, `xbox*`, `d3d*`, `dsnd*`, FS, saves, movies: what the seams replace |
| XAPI and kernel thunks | `.text` 0xe93f0-0xee24c, 0xf53d0-0xf5460 | 102 | XDK runtime: process startup, files, threads, the heap |
| C runtime | `.text` 0xee24c-0xf53d0 | 241 | `sprintf`, `memcpy`, `malloc`, stdio, floating point (with the XAPI heap under `malloc`) |
| XDK library sections | `D3D`, `D3DX`, `XGRPH`, `DSOUND`, `XMV`, `XPP` | 884 | Microsoft libraries, each in its own XBE section |

**1,499 of the 3,999 functions (37%) are below the surface, and everything there except the C runtime is done:
100% of the Xbox layer and of every library, by functions and by bytes.** Nothing below the surface that is not
the C runtime can still run.

- **Eurocom's Xbox layer (272): done.** Graphics (`d3dSeam.cpp`, `psiDraw.cpp`, `psiSprite.cpp`, `psiLight.cpp`,
  `xboxMatrix.cpp`), sound (`dsndSeam.cpp`, `psiSound.cpp`), input (`psiInput.cpp`), movies (`Fmv.cpp`), files
  and saves (`FS.cpp`, `XboxFile.cpp`, `psiSave.cpp`, `psiFile.cpp`), the error screens (`XboxError.cpp`) and
  the system services - language, moving between engines, memory (`XboxSystem.cpp`).
- **XAPI (102): done.** Process startup is ours (`XboxStartup.cpp`: the entry point, the C runtime's initialisers
  run as an explicit list), the file and save calls end in our file layer, and the save signatures are gone.
- **D3D, D3DX, XGRPH (246), XPP (177), DSOUND (401), XMV (60): done.** None is reached. DSOUND's last four were
  static constructors in the initialiser table; our startup leaves them out.
- **The C runtime (241: 157 live) is left to go by itself.** Game code calls `__ftol2`, `sprintf`, `memcpy` and
  `malloc` everywhere; as those callers become ours, our compiler's runtime serves them instead. XAPI code that
  only the C runtime reaches (the heap allocator under `malloc`) is counted with it (`tools/function_coverage.py`
  works that out: "XAPI under the C runtime").

`python tools/function_coverage.py --why <subsystem>` lists each live function there and what keeps it live (its
live callers, our code calling it, data holding its address, the entry point).

So the work that matters is the game code above the surface: **2,500 functions, 19% done (21% by bytes), about
675 KB still original.**

## By subsystem

| Subsystem | Functions | Replaced | Dead | Live | Done | KB | Done (bytes) |
|---|--:|--:|--:|--:|--:|--:|--:|
| **ai** | 837 | 28 | 4 | 805 | **4%** | 250 | 1% |
| ai.bots | 75 | 5 | 1 | 69 | 8% | 21 | 2% |
| ai.drones | 694 | 20 | 3 | 671 | 3% | 212 | 1% |
| ai.nav | 68 | 3 | 0 | 65 | 4% | 16 | 1% |
| **player** | 174 | 39 | 2 | 133 | **24%** | 75 | 19% |
| **objects** | 204 | 32 | 2 | 170 | **17%** | 61 | 14% |
| **effects** | 56 | 0 | 0 | 56 | **0%** | 19 | 0% |
| **mp** | 77 | 19 | 1 | 57 | **26%** | 27 | 17% |
| **engine** | 555 | 200 | 6 | 349 | **37%** | 178 | 43% |
| engine.anim | 80 | 3 | 1 | 76 | 5% | 34 | 5% |
| engine.camera | 37 | 7 | 0 | 30 | 19% | 8 | 21% |
| engine.collision | 42 | 5 | 0 | 37 | 12% | 18 | 3% |
| engine.flow | 15 | 8 | 0 | 7 | 53% | 4 | 43% |
| engine.input | 14 | 8 | 0 | 6 | 57% | 2 | 50% |
| engine.lighting | 11 | 7 | 1 | 3 | 73% | 4 | 77% |
| engine.math | 113 | 61 | 0 | 52 | 54% | 17 | 44% |
| engine.mission | 11 | 11 | 0 | 0 | 100% | 3 | 100% |
| engine.objects | 25 | 11 | 0 | 14 | 44% | 5 | 19% |
| engine.physics | 19 | 0 | 0 | 19 | 0% | 4 | 0% |
| engine.savestate | 20 | 0 | 0 | 20 | 0% | 4 | 0% |
| engine.script | 55 | 29 | 1 | 25 | 55% | 14 | 40% |
| engine.static | 11 | 4 | 0 | 7 | 36% | 44 | 100% |
| engine.text | 17 | 8 | 0 | 9 | 47% | 3 | 28% |
| engine.util | 71 | 36 | 3 | 32 | 55% | 11 | 51% |
| engine.world | 14 | 2 | 0 | 12 | 14% | 3 | 6% |
| **render** | 59 | 19 | 1 | 39 | **34%** | 16 | 12% |
| **loader** | 58 | 24 | 3 | 31 | **47%** | 20 | 66% |
| **ui** | 345 | 76 | 4 | 265 | **23%** | 185 | 36% |
| ui.frontend | 300 | 60 | 1 | 239 | 20% | 164 | 35% |
| ui.hud | 45 | 16 | 3 | 26 | 42% | 20 | 43% |
| **audio** | 135 | 14 | 1 | 120 | **11%** | 29 | 5% |
| **platform** | 272 | 240 | 32 | 0 | **100%** | 51 | 100% |
| platform.files | 43 | 24 | 19 | 0 | 100% | 6 | 100% |
| platform.gfx | 132 | 125 | 7 | 0 | 100% | 33 | 100% |
| platform.input | 14 | 14 | 0 | 0 | 100% | 3 | 100% |
| platform.movie | 5 | 5 | 0 | 0 | 100% | 1 | 100% |
| platform.sound | 57 | 55 | 2 | 0 | 100% | 5 | 100% |
| platform.system | 21 | 17 | 4 | 0 | 100% | 2 | 100% |
| **lib** | 1227 | 34 | 1036 | 157 | **87%** | 419 | 95% |
| lib.crt | 241 | 3 | 81 | 157 | 35% | 34 | 34% |
| lib.d3d | 246 | 0 | 246 | 0 | 100% | 72 | 100% |
| lib.dsound | 401 | 0 | 401 | 0 | 100% | 118 | 100% |
| lib.xapi | 102 | 31 | 71 | 0 | 100% | 14 | 100% |
| lib.xmv | 60 | 0 | 60 | 0 | 100% | 157 | 100% |
| lib.xpp | 177 | 0 | 177 | 0 | 100% | 23 | 100% |
| **game code (above)** | 2500 | 451 | 24 | 2025 | **19%** | 858 | 22% |
| **platform + libraries** | 1499 | 274 | 1068 | 157 | **90%** | 470 | 95% |
| **  without the C runtime** | 1258 | 271 | 987 | 0 | **100%** | 436 | 100% |
| **everything** | 3999 | 725 | 1092 | 2182 | **45%** | 1328 | 48% |

## Where the remaining game code is

Of the 675 KB of game code still original:

| Subsystem | Live KB | Share |
|---|--:|--:|
| ai.drones | 209 | 31% |
| ui.frontend | 107 | 16% |
| player | 62 | 9% |
| objects | 53 | 8% |
| engine.anim | 32 | 5% |
| audio | 28 | 4% |
| mp | 23 | 3% |
| ai.bots | 21 | 3% |
| effects | 19 | 3% |
| engine.collision | 17 | 3% |
| everything else | 104 | 15% |

- **The drones are the single largest block**, nearly a third of what is left. 192 of their 694 functions are the
  state functions (`NDrone2_DSTATE_*`), dispatched through the state table; `docs/drone/` maps the system.
- **`engine.static` is done by bytes.** Its one big function, `WeaponDataTableInit` (0xf5530), a 44 KB static
  initialiser that built the weapon table one store at a time, is now ours: `tools/weapon_table.py` generates the
  table from the XBE and `docs/weapons.md` names every field. The seven small initialisers left are trivial.
- **The front end** is large but shallow: 300 functions, many of them page and control handlers. `docs/ui/` covers it.

The largest single live functions are `C_GCPAUSE_Handler` (5.0 KB), `NDrone2_DefaultInit`
(4.8 KB), `NDrone2_DSTATE_BotGlobal` (4.1 KB), `List_SendMessage` (4.0 KB), `Player_WeaponFiring` (3.8 KB),
`Car_Update` (3.4 KB), `Manager_SendMessage` (3.4 KB), `Player_SetWeaponAnimObj` (3.2 KB) and `P_TWEAKS_Handler` (3.2 KB).

## What each subsystem is

The architecture notes in [architecture/](architecture/README.md) describe how each subsystem works, what is
understood and what is not.

## The driving engine

`python tools/function_coverage.py --driving` counts `Driving.xbe` the same way: replaced, dead and live, from the
same call graph (`tools/functions_driving.json`, `tools/xrefs_driving.json`). Replaced means an entry patched by
`src/driving/autogenerated_injections.inc` (the `WriteJmpTo` table), by a `WriteJump` in our startup or input code,
or listed in a seam's generated entry table (`src/driving/gfx/d3d8Entries.inc`, `src/driving/sound/dsoundEntries.inc`).
The seams patch every entry point those tables list, so a library entry point counts as replaced whether our
backend implements it or stubs it. The originals our code still calls come from `src/driving/autogenerated_functions.inc`,
by address.

### Four tiers

The driving engine is EA's, not Eurocom's, and is shaped differently: a C++ game on a portable C engine library.
`tools/subsystems_driving.txt` sorts every function into four tiers:

| Tier | What it is |
|---|---|
| **game** | Gameplay: AI (`AI*`), mission script events (`E*`), missions and rules (`Simulation`, `SMission*`, `SRule*`), vehicles (`PBondCar`, `PVehicle`, `PHelicopter`), weapons and objects, the HUD and front end (`G*`), gameplay audio (`AVehicle`, `AEngine`...) and effects (`RVehicleParticle`, `RTyreTrack`...) |
| **engine** | The game engine: animation (`Act*`), rendering (`R*`), cameras, physics (`RigidBody`, `PhysicsObject`), world and collision (`W*`), the audio framework (`AStream`, `AMix`...), input (`IOModule`, `ActionQueue`), data and tuning (`CARP`, `Attribute*`, `DTuning*`), the game loop, scheduler and event framework, utilities (`U*`), and the static initialisers |
| **platform** | EA's platform layer: EAGL (graphics, including EAGLAnim, FONT, SHAPE), the sound library (`SND*`, `MIX`, `SFILTER`), files, memory, threads and timers, VU0 maths, the RCMP movie player |
| **sys** | System APIs: Microsoft's D3D (with D3DX and XGRPH), DSOUND and XPP sections, XAPI and the kernel thunks, the C/C++ runtime |

The game's source files are named after their classes and the binary is laid out by file, so classification is by
name. Address ranges give the libraries (and each area's default); class rules sort named functions; an unnamed
function takes the subsystem of the nearest named one before it in its range. Classes whose inline copies are
scattered through the binary (EAGL, VU0, URefCounter, std) never count as that neighbour. The exception-handling
funclets at 0x150000 are named after the function they belong to and go with it. The letter prefixes are EA's
(A audio, D data, E events, G graphics/HUD, I input, P vehicle physics, R rendering, S simulation, U utilities,
W world); where a class could sit in either of two tiers the choice is a judgement, and the rules in the file are
where to change it.

### By subsystem (10 October 2026)

| Subsystem | Functions | Replaced | Dead | Live | Done | KB | Done (bytes) |
|---|--:|--:|--:|--:|--:|--:|--:|
| **game** | 2852 | 140 | 146 | 2566 | **10%** | 474 | 14% |
| game.ai | 522 | 0 | 29 | 493 | 6% | 109 | 1% |
| game.audio | 366 | 1 | 0 | 365 | 0% | 51 | 0% |
| game.effects | 195 | 4 | 8 | 183 | 6% | 36 | 2% |
| game.events | 793 | 0 | 5 | 788 | 1% | 72 | 0% |
| game.frontend | 332 | 1 | 39 | 292 | 12% | 69 | 2% |
| game.missions | 273 | 0 | 0 | 273 | 0% | 33 | 0% |
| game.vehicles | 193 | 134 | 59 | 0 | 100% | 62 | 100% |
| game.weapons | 178 | 0 | 6 | 172 | 3% | 43 | 0% |
| **engine** | 3990 | 2479 | 1399 | 112 | **97%** | 526 | 100% |
| engine.anim | 415 | 332 | 72 | 11 | 97% | 54 | 100% |
| engine.audio | 270 | 203 | 54 | 13 | 95% | 35 | 100% |
| engine.camera | 273 | 224 | 41 | 8 | 97% | 60 | 99% |
| engine.core | 213 | 155 | 56 | 2 | 99% | 25 | 100% |
| engine.data | 554 | 454 | 80 | 20 | 96% | 79 | 100% |
| engine.input | 107 | 79 | 23 | 5 | 95% | 12 | 100% |
| engine.physics | 156 | 117 | 31 | 8 | 95% | 33 | 100% |
| engine.render | 760 | 555 | 174 | 31 | 96% | 114 | 100% |
| engine.static | 798 | 22 | 776 | 0 | 100% | 26 | 100% |
| engine.world | 444 | 338 | 92 | 14 | 97% | 89 | 100% |
| **platform** | 1453 | 1254 | 199 | 0 | **100%** | 262 | 100% |
| platform.eagl | 807 | 751 | 56 | 0 | 100% | 146 | 100% |
| platform.files | 77 | 44 | 33 | 0 | 100% | 12 | 100% |
| platform.input | 6 | 6 | 0 | 0 | 100% | 1 | 100% |
| platform.math | 104 | 94 | 10 | 0 | 100% | 16 | 100% |
| platform.movie | 72 | 0 | 72 | 0 | 100% | 15 | 100% |
| platform.sound | 299 | 299 | 0 | 0 | 100% | 64 | 100% |
| platform.system | 88 | 60 | 28 | 0 | 100% | 9 | 100% |
| **sys** | 1440 | 273 | 822 | 345 | **76%** | 244 | 85% |
| sys.crt | 420 | 4 | 71 | 345 | 18% | 44 | 18% |
| sys.d3d | 427 | 151 | 276 | 0 | 100% | 120 | 100% |
| sys.dsound | 314 | 64 | 250 | 0 | 100% | 36 | 100% |
| sys.xapi | 107 | 45 | 62 | 0 | 100% | 21 | 100% |
| sys.xpp | 172 | 9 | 163 | 0 | 100% | 24 | 100% |
| **game + engine** | 6842 | 2619 | 1545 | 2678 | **61%** | 1000 | 59% |
| **platform + system** | 2893 | 1527 | 1021 | 345 | **88%** | 506 | 93% |
| **  without the C runtime** | 2473 | 1523 | 950 | 0 | **100%** | 462 | 100% |
| **everything** | 9735 | 4146 | 2566 | 3023 | **69%** | 1506 | 70% |

- **The engine's core and data layers are done (4 October 2026): `engine.core` and `engine.data`, 100% by bytes**
  (docs/driving/core-data.md): memory (UMemory, new/delete), reference counters, data groups, singletons, the
  scheduler, random numbers and noise, the game loop and `main`; tuning files (with the C++ stream library they
  parse through), the attribute system, CARP level data and its resolvers, symbol tables, DAFI and file loading.
  Five shadow tests compare them with the originals (0 differences), and lockstep runs of missions 1-8 match the
  baseline frame for frame. The functions still counted live there are exception funclets of functions in other
  subsystems, filed by address. With the static initialisers and the input layer, game and engine together are 25%
  done by functions, 14% by bytes.
- **The collision system is done (5 October 2026, docs/driving/collision.md):** WCollisionMgr, colliders,
  collision instances, the spatial grid and scene tree, the geometry maths, the physics Util_* helpers and OBB - 196
  functions, half of `engine.world` by bytes. Four shadow tests match the originals on the loaded track, and
  lockstep missions 1-8 match the baseline. Game and engine together are now 28% done by functions, 19% by bytes.
- **The rest of the world is done (7 October 2026, docs/driving/world.md):** WWorld (open, reset, close), the
  visibility curtains, triggers, the road network and road navigation, targeting and sound groups - 144 functions;
  `engine.world` is 94% ours by bytes, all but WRender (the world's drawing, ported the same day: now 100%). Five shadow tests match the originals
  on the loaded track, and lockstep missions 1-8 match the baseline. Game and engine together are now 31% done by
  functions, 23% by bytes.
- **Physics is done (7 October 2026, docs/driving/physics.md):** RigidBody (integration, levers, ground, world and
  object collision, impulses, damage), SimpleRigidBody, PhysicsObject, PhysicsNamespace and Newton - 95 functions;
  `engine.physics` is 100% ours by bytes. Five shadow tests match the originals on copies of the live bodies, and
  lockstep missions 1-8 match the baseline. Game and engine together are now 33% done by functions, 26% by bytes.
- **The cameras are done (7 October 2026, docs/driving/camera.md):** the camera classes, the frame's rendering
  (RRenderWorldCamera), RPlayerCamera in all its modes, the camera tuning file, splines, the director queue and the
  camera input - 226 functions; `engine.camera` is 99% ours by bytes. Five shadow tests match the originals on copies
  of the live camera, and lockstep missions 1-8 match the baseline. Game and engine together are now 37% done by
  functions, 32% by bytes.
- **The audio framework is done (8 October 2026, docs/driving/audio.md):** ASoundManager, the sounds, voices,
  mixes, faders, effects, the listener, banks and their indexes, and streams - 207 functions; `engine.audio` is
  100% ours by bytes. Five shadow tests match the originals, and a trace of every call into the sound library over
  lockstep runs of missions 1-8 (with the sound side in lockstep too) matches the baseline line for line. Game and
  engine together are now 41% done by functions, 36% by bytes.
- **Animation is done (8 October 2026, docs/driving/anim.md):** the actors and their controllers, characters,
  animation banks and groups, events, IK and pose overrides, the manager, models, skeletons and textures, posers,
  weapons, and RAnimEngine with the proc-anim functions - 341 functions; `engine.anim` is 100% ours by bytes. Six
  shadow tests match the originals, and lockstep missions 1-8 match the baseline in frames and sound traces. Game and
  engine together are now 47% done by functions, 41% by bytes.
- **Rendering is done (10 October 2026, docs/driving/render.md):** the renderer core and its instance draws, the
  materials, fog, lights, the path engine, render states and texture contexts, scene objects and culling, the
  high-level renderer, and the effects - reflections, colouring, decals, gain, lens flares, glare, lightning, the
  glow, shadow maps, sky, water, broken windows and particles - about 560 functions; `engine.render` is 100% ours by
  bytes. Seven shadow tests match the originals on missions 1, 4 and 6, and lockstep missions 1-8 match the baseline
  pixel for pixel. The engine tier is now 98% ours by bytes, all but input. Game and engine together are now 57%
  done by functions, 52% by bytes.
- **The input layer is done (10 October 2026, docs/driving/input.md):** the control configurations
  (InputConfigManager, Master.def and the .def files with their front-end labels), the devices' mappings
  (InputToAction, InputTable), force feedback (IFeedback and its thread), the rest of ActionQueueManager's vector and
  the text and path helpers - 40 functions on top of IOModule, the devices and the queues; `engine.input` is 100%
  ours by bytes, and the engine tier with it. A shadow test compares them with the originals on the disc's control
  files and mutated copies. Game and engine together are now 58% done by functions, 53% by bytes.
- **The vehicles are done (10 October 2026, docs/driving/vehicles.md):** PBondCar - every car, the player's and
  the AI's, with the snowmobile, submarine and spline modes, its construction, controls, physics steps, wheel
  forces, damage, tyre tracks, weapons and engine sound inputs - PVehicle with its car names and attributes, and
  PHelicopter: 134 functions, `game.vehicles` 100% by bytes, the first finished game-tier subsystem. Five shadow
  tests compare them with the originals on copies of the live cars and helicopters (to be run in game). Game and
  engine together are now 61% done by functions, 59% by bytes.
- **Every system library but the C runtime is done: D3D (with D3DX and XGRPH), DSOUND, XPP and XAPI at 100%.**
  The C runtime (347 live, 17%) is left to go by itself, as in the action engine: game code calls it everywhere, and
  it goes as that code becomes ours. How the rest got there (2 October 2026):
  - *D3D*: the seam's table gained 25 entry points EAGL calls directly (render-state setters, `SetIndices`,
    `SetGammaRamp`, `CreateIndexBuffer2`...), named in Ghidra after the table was last generated, and a hand-kept
    `src/driving/gfx/d3d8EntriesUnnamed.inc` covers four Ghidra has no name for (`D3DXLoadSurfaceFromMemory`,
    `XGBytesPerPixelFromFormat`, `XGWriteSurfaceToFile` and a constant).
  - *DSOUND*: the startup runs the C runtime's initialiser tables itself (`src/driving/platform/XboxStartup.cpp`)
    and leaves out DSOUND's four static constructors; `CRefCount::AddRef`, reached through a vtable, is ported.
  - *XPP*: the import thunk for `XInitDevices` is patched along with the function.
  - *XAPI*: `src/driving/platform/XboxXapi.cpp` replaces 31 functions - files, events, waits, sleeping, threads,
    the heap under `malloc`, contiguous memory, time, `IsBadReadPtr` - with their Win32 namesakes (the loader's
    kernel shims hand out Win32 handles, so the two mix freely), and the startup's own entry point is ported.
    The process heap is a Win32 heap now: XAPI's five heap entry points go to `HeapCreate`/`HeapAlloc`/`HeapFree`/
    `HeapReAlloc`/`HeapSize`, so `malloc` and `free` reach Win32's heap through them.
- **The platform tier is done: all 1,453 functions, 100% by functions and by bytes.** With the system libraries
  but the C runtime, everything below the game is ours. The last piece was the sound library.
  - *Sound* (`src/driving/sound/snd/`, docs/driving/sound.md): all 305 functions - banks and voices, the EA-XA,
    MicroTalk and PCM decoders, the SFILTER graph, the mixer and reverb, the system and its server thread, the
    streams and the STREAM file side, and the platform driver over DirectSound (`src/driving/sound/dsndSeam.cpp`).
    Shadow tests in `src/driving/devtools/Snd*Shadow.cpp` drive each part beside the original (tags, decoders,
    filters, mixer, system, streams, platform, stream files), all with no differences, and lockstep runs of the
    eight missions match the baseline.
  - *EAGL* (docs/driving/eagl.md): all 807 functions. realgraph FONT/SHAPE/LOCALE, `EAGL::Transform` and the loader
    are shadow-tested at injection time; EAGLAnim's live types (`eagl/anim/`) by `AnimShadow.cpp` over every anim
    on the disc and its Skeleton by `SkelShadow.cpp` live; the render core (GeoPrimState, TAR, render methods,
    viewports and contexts, the font driver, models, the profiler) by lockstep runs of all eight missions against a
    baseline, every frame identical. The RUNTIME_ALLOC parsers and the animation types no shipped data builds are
    provisional, with a one-time untested warning.
  - *System* (`src/driving/platform/RealSystem.cpp`, `RealPrint.cpp`, `RealMemory.cpp`): EA's portable library -
    TIMER, THREAD, SIGNAL, SYNCTASK, MUTEX, PRINT, the exit and abort handlers, the MEM_ copy and fill helpers,
    and the MEM block allocator itself (classes, first and largest fit, top allocation, resize, validation), which
    `src/driving/devtools/MemShadow.cpp` drives side by side with the original (`NIGHTFIRE_MEMSHADOW=1`).
  - *Files* (`src/driving/platform/FileSys.cpp`, docs/driving/filesys.md): the whole FILESYS layer in one piece -
    the op queue and its worker thread, the sync wrappers, the FILE_ helpers, the open-file table and the .viv
    archive directories, and what is left of ASYNCFILE. Its records stay where the original kept them, so the
    game allocates exactly what it did. `src/driving/devtools/FileSysShadow.cpp` checks the archive lookup against
    the original on every archive (`NIGHTFIRE_FSSHADOW=1`); `FileSysTrace.cpp` prints the ops (`NIGHTFIRE_FSTRACE=1`).
  - *Maths* (`src/driving/platform/RealMath.cpp`, `VU0Math.cpp`, `D3DXMath.cpp`, docs/driving/maths.md): EA's x87
    library, the SSE VU0 layer and the D3DX routines linked beside them, ported bit for bit - double in the
    original's order where it used the x87, intrinsics lane for lane where it used SSE, the original instructions
    where FSIN and its kin keep 64 bits. `src/driving/devtools/MathShadow.cpp` checks all of it against the
    originals (`NIGHTFIRE_MATHSHADOW=1`). The 36 EAGLAnim helpers that were filed as maths are counted with EAGL
    now, the D3DX routines with D3D, and a dead software-transform library behind unreferenced tables as dead.
  - *Movies*: playback is FFmpeg (`src/driving/engine/PlayMPC.cpp`, now including `PlayMPC`'s constructor and
    destructor), so EA's RCMP player is all dead. STREAM, which shares its library but is the sound streamer's file
    side, is counted with the sound library; EA's packer (`src/driving/platform/RefPack.cpp`, checked against the
    original on every packed file in the archives) with the files. `XGetAVPack`/`XGetVideoFlags`, which sat among
    the file code, are counted with XAPI (ported in `XboxXapi.cpp`).
- **Of the 1,000 KB of game and engine code, about 410 KB is still original:** the game tier but its vehicles;
  what else is left is exception funclets filed in finished subsystems. The game tier by size: AI 109 KB, mission
  events 72 KB, front end and HUD 68 KB, gameplay audio 51 KB, weapons 43 KB, effects 36 KB, missions 33 KB
  (vehicles, 62 KB, done).
- **The static initialisers are done: `engine.static`, 798 functions, 100%.** The C++ initialiser table
  (0x001b3db0) is no longer walked: `RunStaticInitialisers` (`src/driving/engine/StaticInitTable.cpp`, generated
  from the XBE by `tools/static_init_driving.py`) does all 720 entries' work in table order - 585 float and double
  constants computed at startup (`1/FLT_MIN`, `pi+pi`, `180/pi`...: one exact rounding each, so plain C++ gives the
  same bits), fills and copies, 52 colours through `ColourConvertXBoxToPS2`, the global objects' constructors (ours
  called directly) with their destructors registered with the runtime's `atexit` as before, and the driving
  weapon table, which a 4.4 KB initialiser built a store at a time, as a typed table (`WeaponDefinition`,
  `src/driving/engine/StaticInit.h`). The 22 destructor thunks game code registers for its function-local statics
  are ported in `StaticInit.cpp`. `src/driving/devtools/StaticInitDump.cpp` checked it: `.data`, `.bss` and the
  atexit list after our list and after the XBE's own table differ only in the weapon names (our string literals),
  heap pointers (one base offset apart, so the same allocations) and the profiler timer's start time.

**How sure the split is.** Gameplay is nearly all named (under 5% of bytes unnamed in AI, events, vehicles,
weapons, front end), so its numbers are firm. The data, core, EAGL, files, maths and movie subsystems are 35-70%
unnamed by bytes and rest on the neighbour rule; a misattributed unnamed function moves bytes between neighbouring
subsystems, not between done and not done. `python tools/function_coverage.py --driving --unclassified` lists named
functions no rule matches (now almost only exception funclets named after a method, which the neighbour rule
places correctly). Sizes are bytes to the next function, so a function followed by code Ghidra never disassembled
looks larger than it is (`EFireWeapon::~EFireWeapon` at 6.8 KB is one).

The largest live functions above the platform, all in the game tier: `GHud::DrawPauseMenu`, `GHud::GHud`,
`PVehicle::InitializeGlobals`, `PBondCar::ApplyDamage`, `GHud::DrawCrossHairs`, `PBondCar::ProcessPhysics` and
`AIHelicopter::DoAgentLogic`, each 5-7 KB.
