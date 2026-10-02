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

**Done** is replaced plus dead: what no longer needs reimplementing. The plain replaced count, 717 of 3,999 (18%), is
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
| **ui** | 345 | 75 | 5 | 265 | **23%** | 185 | 36% |
| ui.frontend | 300 | 59 | 2 | 239 | 20% | 164 | 35% |
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
| **game code (above)** | 2500 | 443 | 25 | 2032 | **19%** | 858 | 21% |
| **platform + libraries** | 1499 | 274 | 1068 | 157 | **90%** | 470 | 95% |
| **  without the C runtime** | 1258 | 271 | 987 | 0 | **100%** | 436 | 100% |
| **everything** | 3999 | 717 | 1093 | 2189 | **45%** | 1328 | 47% |

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
