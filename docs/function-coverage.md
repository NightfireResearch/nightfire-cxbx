# Function coverage by subsystem

How much of each engine's code is ours, where the rest sits, and how much of it never needs reimplementing. The
action engine's numbers are from 1 October 2026, the [driving engine's](#the-driving-engine) from 2 October 2026
(after the platform files); `tools/function_coverage.py` regenerates them.

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

**Done** is replaced plus dead: what no longer needs reimplementing. The plain replaced count, 717 of 3,999 (18%), is
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
677 KB still original.**

## By subsystem

| Subsystem | Functions | Replaced | Dead | Live | Done | KB | Done (bytes) |
|---|--:|--:|--:|--:|--:|--:|--:|
| **ai** | 837 | 28 | 4 | 805 | **4%** | 250 | 1% |
| ai.bots | 75 | 5 | 1 | 69 | 8% | 21 | 2% |
| ai.drones | 694 | 20 | 3 | 671 | 3% | 212 | 1% |
| ai.nav | 68 | 3 | 0 | 65 | 4% | 16 | 1% |
| **player** | 174 | 37 | 2 | 135 | **22%** | 75 | 13% |
| **objects** | 204 | 30 | 2 | 172 | **16%** | 61 | 13% |
| **effects** | 56 | 0 | 0 | 56 | **0%** | 19 | 0% |
| **mp** | 77 | 17 | 1 | 59 | **23%** | 27 | 13% |
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
| **loader** | 58 | 23 | 3 | 32 | **45%** | 20 | 51% |
| **ui** | 345 | 75 | 4 | 266 | **23%** | 185 | 36% |
| ui.frontend | 300 | 59 | 1 | 240 | 20% | 164 | 35% |
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
| **game code (above)** | 2500 | 443 | 24 | 2033 | **19%** | 858 | 21% |
| **platform + libraries** | 1499 | 274 | 1068 | 157 | **90%** | 470 | 95% |
| **  without the C runtime** | 1258 | 271 | 987 | 0 | **100%** | 436 | 100% |
| **everything** | 3999 | 717 | 1092 | 2190 | **45%** | 1328 | 47% |

## Where the remaining game code is

Of the 677 KB of game code still original:

| Subsystem | Live KB | Share |
|---|--:|--:|
| ai.drones | 209 | 31% |
| ui.frontend | 107 | 16% |
| player | 65 | 10% |
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

### By subsystem (2 October 2026)

| Subsystem | Functions | Replaced | Dead | Live | Done | KB | Done (bytes) |
|---|--:|--:|--:|--:|--:|--:|--:|
| **game** | 2852 | 1 | 5 | 2846 | **0%** | 474 | 0% |
| game.ai | 522 | 0 | 0 | 522 | 0% | 109 | 0% |
| game.audio | 366 | 0 | 0 | 366 | 0% | 51 | 0% |
| game.effects | 195 | 0 | 0 | 195 | 0% | 36 | 0% |
| game.events | 793 | 0 | 0 | 793 | 0% | 72 | 0% |
| game.frontend | 332 | 1 | 5 | 326 | 2% | 69 | 1% |
| game.missions | 273 | 0 | 0 | 273 | 0% | 33 | 0% |
| game.vehicles | 193 | 0 | 0 | 193 | 0% | 62 | 0% |
| game.weapons | 178 | 0 | 0 | 178 | 0% | 43 | 0% |
| **engine** | 4032 | 62 | 12 | 3958 | **2%** | 527 | 2% |
| engine.anim | 415 | 0 | 2 | 413 | 0% | 54 | 0% |
| engine.audio | 270 | 0 | 0 | 270 | 0% | 35 | 0% |
| engine.camera | 273 | 0 | 0 | 273 | 0% | 60 | 0% |
| engine.core | 254 | 10 | 3 | 241 | 5% | 26 | 6% |
| engine.data | 555 | 3 | 1 | 551 | 1% | 79 | 0% |
| engine.input | 107 | 42 | 5 | 60 | 44% | 12 | 46% |
| engine.physics | 156 | 0 | 0 | 156 | 0% | 33 | 0% |
| engine.render | 760 | 7 | 1 | 752 | 1% | 114 | 3% |
| engine.static | 798 | 0 | 0 | 798 | 0% | 26 | 0% |
| engine.world | 444 | 0 | 0 | 444 | 0% | 89 | 0% |
| **platform** | 1458 | 204 | 159 | 1095 | **25%** | 261 | 21% |
| platform.eagl | 805 | 0 | 6 | 799 | 1% | 145 | 0% |
| platform.files | 77 | 44 | 33 | 0 | 100% | 12 | 100% |
| platform.input | 6 | 6 | 0 | 0 | 100% | 1 | 100% |
| platform.math | 104 | 94 | 10 | 0 | 100% | 16 | 100% |
| platform.movie | 72 | 0 | 72 | 0 | 100% | 15 | 100% |
| platform.sound | 306 | 0 | 10 | 296 | 3% | 64 | 1% |
| platform.system | 88 | 60 | 28 | 0 | 100% | 9 | 100% |
| **sys** | 1393 | 273 | 792 | 328 | **76%** | 243 | 85% |
| sys.crt | 373 | 4 | 41 | 328 | 12% | 43 | 15% |
| sys.d3d | 427 | 151 | 276 | 0 | 100% | 120 | 100% |
| sys.dsound | 314 | 64 | 250 | 0 | 100% | 36 | 100% |
| sys.xapi | 107 | 45 | 62 | 0 | 100% | 21 | 100% |
| sys.xpp | 172 | 9 | 163 | 0 | 100% | 24 | 100% |
| **game + engine** | 6884 | 63 | 17 | 6804 | **1%** | 1002 | 1% |
| **platform + system** | 2851 | 477 | 951 | 1423 | **50%** | 504 | 52% |
| **  without the C runtime** | 2478 | 473 | 910 | 1095 | **56%** | 462 | 55% |
| **everything** | 9735 | 540 | 968 | 8227 | **15%** | 1506 | 18% |

- **Almost nothing above the platform is ours yet: 1% of game and engine code.** What is replaced is the input
  layer (`engine.input`, 44%: `IOModule`, `XBoxPadDevice`, `ActionQueue`, the pad), the event and scheduler core,
  `RGlareManager`'s drawing, file loading and `PlayMPC`.
- **Every system library but the C runtime is done: D3D (with D3DX and XGRPH), DSOUND, XPP and XAPI at 100%.**
  The C runtime (328 live, 12%) is left to go by itself, as in the action engine: game code calls it everywhere, and
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
- **The platform tier is 25% done (21% by bytes): its system library, files, input, maths and movie player are
  done.** What is left is EAGL (145 KB) and the sound library with its streamer (64 KB).
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
- **Of the 1,001 KB of game and engine code, 988 KB is still original.** By size: rendering 110 KB, AI 109 KB, world
  and collision 88 KB, data and tuning 79 KB, mission events 71 KB, front end and HUD 68 KB, vehicles 62 KB,
  cameras 60 KB, animation 51 KB, gameplay audio 50 KB, then the rest at under 45 KB each.
- **The static initialisers (798, 26 KB) all count as live**, because our driving startup still calls the C runtime's
  own `_cinit` (0x00110663, `src/driving/platform/XboxStartup.cpp`), which walks the C++ initialiser table. The
  action engine's startup calls its initialisers by name instead.

**How sure the split is.** Gameplay is nearly all named (under 5% of bytes unnamed in AI, events, vehicles,
weapons, front end), so its numbers are firm. The data, core, EAGL, files, maths and movie subsystems are 35-70%
unnamed by bytes and rest on the neighbour rule; a misattributed unnamed function moves bytes between neighbouring
subsystems, not between done and not done. `python tools/function_coverage.py --driving --unclassified` lists named
functions no rule matches (now almost only exception funclets named after a method, which the neighbour rule
places correctly). Sizes are bytes to the next function, so a function followed by code Ghidra never disassembled
looks larger than it is (`EFireWeapon::~EFireWeapon` at 6.8 KB is one).

The largest live functions above the platform: `GHud::DrawPauseMenu`, `RCameraIniLoader::LoadFile`, `GHud::GHud`,
`PVehicle::InitializeGlobals`, `PBondCar::ApplyDamage`, `GHud::DrawCrossHairs`, `PBondCar::ProcessPhysics`,
`AIHelicopter::DoAgentLogic` and `RPlayerCamera::UpdateAutoDriveCam`, each 5-7 KB.
