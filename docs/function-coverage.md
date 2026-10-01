# Function coverage by subsystem

How much of the action engine's code is ours, where the rest sits, and how much of it never needs reimplementing.
The numbers below are from 1 October 2026; `tools/function_coverage.py` regenerates them.

```
python tools/function_coverage.py                 # the summary below
python tools/function_coverage.py ai.drones       # one subsystem's live functions by module, largest first
python tools/function_coverage.py --markdown      # the summary as a markdown table
```

## How it counts

Every function Ghidra knows (`tools/functions_action.json`, 3,999 of them) is

- **replaced**: its entry jumps to ours;
- **dead**: not replaced, but nothing that can still run reaches it any more. Mostly code that only replaced code
  called, and library code the seams have cut off;
- **live**: original code that still runs.

**Done** is replaced plus dead: what no longer needs reimplementing. The plain replaced count, 613 of 3,999 (15%), is
the figure quoted so far. Replaced includes the startup layer's own patches of XAPI entry points (`WriteJump` in
`engine/XboxStartup.cpp`).

Liveness is `tools/global_coverage.py`'s call graph (its `CallGraph` class) with one difference. A pointer to an XDK
library function counts only when it sits in the game's own data. Pointers inside a library's own code and tables
(D3D's render-state jump table, DSOUND's COM vtables, XPP's callbacks) only matter once the library is entered from
outside, and the call edges already cover that. Counted naively, most of D3D and DSOUND would look live.
`global_coverage.py` keeps the naive count, which is the safe one for owning globals.

Subsystems are address ranges in `tools/subsystems_action.txt`. The XBE lays Eurocom's objects out roughly
alphabetically by source file, so a module is one run of names (Anim..., BOT..., Break..., Bullet...).

Sizes are bytes to the next function, padding included. Ghidra leaves about 80 KB of DSOUND and 130 KB of XMV
undefined, so both are under-counted in functions and bytes alike.

## The surface

| Layer | XBE range | Functions | What it is |
|---|---|--:|---|
| Game code | `.text` 0x11000-0xdbcd0, plus static initialisers at 0xf5460 | 2,500 | Eurocom's portable engine and game |
| Eurocom's Xbox layer | `.text` 0xdbcd0-0xe93f0 | 272 | `psi*`, `xbox*`, `d3d*`, `dsnd*`, FS, saves, movies: what the seams replace |
| XAPI and C runtime | `.text` 0xe93f0-0xf5460 | 343 | XDK runtime, CRT, kernel thunks |
| XDK library sections | `D3D`, `D3DX`, `XGRPH`, `DSOUND`, `XMV`, `XPP` | 884 | Microsoft libraries, each in its own XBE section |

**1,499 of the 3,999 functions (37%) are below the surface.** That is far more than their share of the work, and
most of it is already settled (75% done, 89% by bytes):

- **D3D, D3DX, XGRPH (246 functions): done.** Nothing reaches them except the four D3D8 entry points the XMV decoder
  calls, which we answer ourselves (`FUNC_AT` in `d3dSeam.cpp`), and one D3DX matrix inverse.
- **XPP (177): done.** USB peripherals (pads, memory units); input goes through our XInput layer.
- **DSOUND (401): done.** The only DirectSound the game still reached was the XMV decoder's stream for the movie
  audio, and the decoder is no longer reached (below). The four functions left live are DSOUND's static
  constructors in the C runtime's initialiser table (each stores one vtable pointer).
- **XMV (60 functions, 157 KB): done.** The FMV decoder, driven by the game's five movie functions
  (`platform.movie`). Those are now `engine/Fmv.cpp`, an FFmpeg player (`docs/fmv.md`), so XMV is no longer reached.
  It was the last XDK library the game ran. Ghidra has defined only about a fifth
  of its code. The movies are WMV2 video with Xbox IMA ADPCM audio; see the FMV notes in
  `architecture/audio-loading-saves-scripts.md`.
- **XAPI and C runtime (343: 32 replaced, 59 dead, 252 live).** About 100 of the live ones are the C runtime
  (`__ftol2`, `sprintf`, `fmodf`, `memcpy`), which game code calls everywhere. Those go away by themselves as game
  functions become ours, because our compiler brings its own. About 35 are XAPI and kernel wrappers (files,
  threads, timers, memory). The rest (about 155) are unnamed.
- **Eurocom's Xbox layer (272): 57% done, 68% by bytes.** Graphics, input and the sound backend are mostly ours.
  What is left is the file and save code (28 live) and the game-facing halves of the sound
  and draw layers (`psiSFX*`, `psiDraw*`), which belong with their callers.

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
| **loader** | 58 | 22 | 3 | 33 | **43%** | 20 | 51% |
| **ui** | 345 | 75 | 5 | 265 | **23%** | 185 | 36% |
| ui.frontend | 300 | 59 | 2 | 239 | 20% | 164 | 35% |
| ui.hud | 45 | 16 | 3 | 26 | 42% | 20 | 43% |
| **audio** | 135 | 14 | 1 | 120 | **11%** | 29 | 5% |
| **platform** | 272 | 139 | 16 | 117 | **57%** | 51 | 68% |
| platform.files | 43 | 7 | 8 | 28 | 35% | 6 | 48% |
| platform.gfx | 132 | 83 | 4 | 45 | 66% | 33 | 71% |
| platform.input | 14 | 14 | 0 | 0 | 100% | 3 | 100% |
| platform.movie | 5 | 5 | 0 | 0 | 100% | 1 | 100% |
| platform.sound | 57 | 26 | 2 | 29 | 49% | 5 | 75% |
| platform.system | 21 | 4 | 2 | 15 | 29% | 2 | 17% |
| **lib** | 1227 | 32 | 938 | 257 | **79%** | 419 | 92% |
| lib.d3d | 246 | 0 | 245 | 1 | 100% | 72 | 99% |
| lib.dsound | 401 | 0 | 397 | 4 | 99% | 118 | 100% |
| lib.xapi | 343 | 32 | 59 | 252 | 27% | 48 | 32% |
| lib.xmv | 60 | 0 | 60 | 0 | 100% | 157 | 100% |
| lib.xpp | 177 | 0 | 177 | 0 | 100% | 23 | 100% |
| **game code (above)** | 2500 | 442 | 25 | 2033 | **19%** | 858 | 21% |
| **platform + libraries** | 1499 | 171 | 954 | 374 | **75%** | 470 | 89% |
| **everything** | 3999 | 613 | 979 | 2407 | **40%** | 1328 | 45% |

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
