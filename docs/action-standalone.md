# The action engine's data in our source

Every function of the action engine is ours (tools/function_coverage.py). What still ties the DLL to default.xbe is
the game's data: the globals our source reaches at their XBE addresses, the strings and tables it names by address,
and the injection itself. This is the worklist for a build that loads no XBE, done the way the driving engine's was
(docs/driving/standalone.md), with one difference: the moved data is relocated, so the original code still in the
image uses our copy too.

## The plan

1. Constant data first: tables and values the game never changes. Moving them is safe at any time, before or after
   the all-original runs (all the original code against all of ours over the time demos), because the game's copy
   and ours hold the same bytes.
2. The game's state (globals the game writes) stays at its XBE address until the all-original runs are done, or
   moves with every original reference pointed at ours.
3. Then the code pointers, the platform seams and the start-up: the standalone target.

## Moving a global: `readonly relocate`

A moved global is an ordinary definition with a typed initialiser, beside the code that uses it, declared in its
header. The XBE_GLOBAL tag above it gives the block it replaces and two words:

    // XBE_GLOBAL(0x0016199c, 0x4) readonly relocate
    float TurretRange = 2500.0f;

- `readonly`: neither copy is ever written, so an original that still reads the game's copy reads the same values
  (tools/global_coverage.py).
- `relocate`: the injector rewrites every instruction of the game's code that holds an address inside the block to
  hold the same offset into ours (src/common/xbeRelocate.h). The sites come from tools/data_refs.py, which decodes
  each candidate dword as an instruction operand and keeps it only where Ghidra also has a data reference from that
  instruction into the block (tools/xrefs_action.json); the list is committed (tools/data_refs_action.json), so the
  build reads no XBE. Three rules keep it from moving what is not the block's: where two blocks overlap (the drone
  mode table's last entry is the type table's first eight bytes, read as the game reads them), an address belongs to
  the block starting nearest below it; an odd offset through a register is left alone (the Inflate length tables are
  indexed from 0x0018cc5b + 257 + n, inside the security system's messages); a site is the block's only if our port
  of the function it is in names the block, or one of its macros, itself or in a function it names (an adaptor's C
  core, a helper) - a base folded from another table can land in a block at an even offset too
  (Player_WeaponFiring's `DecryptorDigits[digit - '0']` is 0x001819f0, inside RadarIcons); and the block's end moves
  only as a `cmp` in a function that also uses the block (a loop's bound: MapMusic's search). Whatever is left alone
  reads the game's identical copy. tools/preprocess.py writes an `XbeRelocate` per site, and a `static_assert` that the
  definition is the block's size, into the injection table, so the definition must be declared in a header
  src/inject_action.cpp includes. A site under a jump over a function's entry is rewritten in the recorded original
  bytes too (XbeOriginal_Write), so a shadow test that swaps the original back in gets the relocated reference.

Relocating keeps one copy in use. An original a shadow test runs, the all-original runs, and our code all read our
table, and a pointer into it that one of them stores (an object's `HEINFO_tag *`, a control's format) is the same
pointer whichever stored it. What it cannot find - a pointer in the game's own data, a base folded outside the
block - still reads the game's identical copy.

Scalars are plain globals, not `const`: MSVC folds a `const float` into its neighbours at compile time (2500.0f *
0.2f becomes one constant), where the original multiplied in the x87's precision, from memory. Tables keep the
constness their declarations had (the drone tables are `const`; a table the game's code hands out non-const pointers
into is not).

## Tools

| Tool | What it does |
|---|---|
| `python tools/standalone_audit.py --action` | every raw default.xbe address left in src/action, classified (code called by address, code pointers, `.rdata` strings and constants, game globals, raw data addresses); `--list CLASS`, `--file PATH` |
| `NIGHTFIRE_DATA_WRITES=<file>` (src/action/devtools/DataWrites.cpp) | from the start of `Inject()`, the bytes of .rdata and .data that change in a run, with the first frame each changed; for the time demos and the menu replays |
| `python tools/data_writes.py RUN...` | the globals still at the game's address, classed `constant` (initial data, no write reference, never changed in any run), `zero`, `pointer` (the address is taken, so a write through it is possible: read the uses), `written` and `unsized`; `--owned` lists owned globals whose game copy changed |
| `python tools/data_refs.py action` | the relocation sites of every `relocate` global (capstone, the XBE) |
| `python tools/driving_data_check.py --action` | every owned global with an initialiser, found in the built actioninject.dll by its map, compared byte for byte with the XBE (a pointer of ours matches a pointer to the same text, or to the game's copy of a global we own) |

## Results (20 October 2026)

`NIGHTFIRE_DATA_WRITES` over the nine time demos: of 645 globals still at the game's address, 11 constants, 3 always
zero, 47 whose address is taken but which never changed, 470 written, 114 without a size. The owned globals' game
copies never changed (the two listed are tagging quirks: drone_stats' two-dimensional array define, and a comment
naming GRAVITY_VECTOR's address).

The byte check found three owned tables that were not the game's:

| Global | Was | Is now |
|---|---|---|
| `LanguageFileNames` (Text.cpp) | "UKTxt.dat" ... | "UKTxt.Dat" ..., the XBE's case |
| `SightSprInfo` (HUD.cpp) | the scope's side bars (640 - 480) / 2 wide, the right one at x 560 | 81 wide, the right one at 559: a pixel over the scope |
| `RedeemerSpriteInfo` (HUD.cpp) | entry 1 (the static lines) 127 x 127 of the sheet | 127 x 255 |

Moved so far, each byte-identical to the XBE (`driving_data_check.py --action`, which now also checks that each
code pointer in them leads to the port of the game's function) and relocated (51 globals):

| Where | What |
|---|---|
| game/drone/DroneTables.cpp | the six drone tables, from tools/drone_tables.py's initialisers |
| game/drone/DroneVision.cpp | Drone_View_Bones (with its -1 end) |
| game/obj | EffectInfo (the materials; the first field is the material's name), GunTypes, CopterBits, UseableGuns, MetalDebris and StoneDebris, SatchelDigits and DecryptorDigits, PunchSkin, TurretRange and TurretRefire |
| game/weapon_stats.cpp | ammo_data |
| game/sp/SSys.cpp | the security system's four message tables |
| engine/Anim.cpp | the facial scripts, both skeletons' joint boxes |
| engine/Text.cpp | TextMsgFormats |
| gfx/Font.cpp | the special characters, and the formats and strings several places share (each named by what it holds, the old names aliases of them: CentredFormat is FontFormat2Centred) |
| sound | MapMusic, MapDroneData, Snd2Lbl, SubtitleDurationModification |
| ui | BulletImg and RadarIcons (BulletImg's last row is the radar icons' first sixteen bytes, as the game's tables overlap), SpinZero, BotString |
| game/obj/ControlFuncs.cpp | control_funcs, the object types' Update, Collide and Delete functions: each slot our port of the game's |
| util/Random.cpp | the generator's multipliers |

The shadow suites caught a relocation the first rules made wrongly: before RadarIcons was ours, HUD_RadarUpdate's
reads of it (0x001819a0 + icon * 0x14) lay inside BulletImg's block and were moved into our BulletImg, past its end
(hudport's multiplayer panes, 4618 cases). Owning RadarIcons and the port rule fixed it.

An index past a moved table now reads past ours, not the game's: EffectInfo with a material of 23 or more (only bad
map data has one) read the material names after the game's table. The effect shadow's random materials stay among
the 23 since (EffectPortShadow.cpp).

## The all-original runs: `NIGHTFIRE_ORIGINAL=game`

Every function of the game's own code (tools/subsystems_action.txt: all but `platform.*` and `lib.*`, 2,437
functions) runs as the Xbox's original over a time demo, with only the platform layer and what the demos hang on
ours (src/action/devtools/AllOriginal.cpp): each original's entry is swapped back in and a jump written over our
port's, so a direct call from our code that stays reaches the original as well. The demo hooks' hosts stay ours
(`kKeep`: mainloop, GameFlow_Main and Game_Run for the mouse, the demo's clock, Mem_Init and
control_init_object_lists for its bookkeeping, the pad poll, the file reads, psiLaunchDriving).

Every global we own has the game's code's references pointed at ours for the run (the 51 `relocate` ones always,
the other 386 here): tools/data_refs.py finds their sites as for `relocate` (tools/data_refs_action.json), and the
build writes them to src/action/devtools/autogenerated_all_original.inc; our copy is found by name in the DLL's map
at run time, so no global needs a declaration the injector can see. Four statics the map cannot tell apart keep
two copies.

For narrowing a difference down: `NIGHTFIRE_ORIGINAL_ONLY=<subsystem prefixes>` runs only those subsystems as the
original, `NIGHTFIRE_ORIGINAL_AT=<addresses>` only those functions.

What the first runs found (21 October 2026, CastleIndoors1):

| Found | Fixed |
|---|---|
| `dirFileBuf` (Loader.cpp) was ours with no XBE_GLOBAL tag, so the originals kept the game's copy | tagged (0x00279168) |
| our `#define DirFileHash ((HASHCODE)DirFileHashValue)` hid the loader's uses of the global from data_refs.py's port check | macros count as names for what they expand to |
| `GS_SetRefreshRate` worked out `FRAME_RATE_MUL` in float: 1.1999999 at 50 Hz, where the original's x87 gives 1.2 | the reciprocal kept as the x87 does (`Extended`); the recordings rebased |
| `build_link_world_to_viewer` returned nothing, where the original leaves the viewer in EAX and the original Camera_Create keeps it | returns the viewer, and our Camera_Create keeps it |

Then the whole of the game's code as the original matched every frame of CastleIndoors1 (635 frames). Each of the
28 subsystems alone as the original matched too.

### Fixed: differences that changed the game

A port that differs from the original in what the game does cannot be fixed without the recordings parting from
what was played (a rebase of CastleIndoors1 onto the first one went another way at a door). These were kept ours
in the all-original runs (`kKeep`) while they were found, then fixed all at once and the missions recorded again
(21 October 2026, tests/demos/action/README.md); every recording now matches the all-original runs:

| Function | Ours, before | The original, and ours now |
|---|---|---|
| GS_SetRefreshRate | `FRAME_RATE_MUL = (1.0f / rate) * 60.0f` in float: 1.1999999 at 50 Hz | the reciprocal on the x87, unrounded: 1.2 |
| Sensor_Update | the camera's sighting count tested after its increment, so it raises the alarm a frame early; its timers (the beeps, the two seconds to the alarm, the sweep's half-way sound) compared as stored floats | the count before the increment (`if (count++ > 30)`); the sums compared as the x87 holds them, unrounded |
| View_3DPoint2Screen | `(1.0f / z) * x * projectionScaleX` in float | `(1.0 / z) * projScaleX * x` on the x87, unrounded until stored |
| Mission_MonitorObjectives (Objective) | `completedChannel` and `revealedChannel` signed: a channel of 128 or more indexes before switch_channels, and `revealedChannel != 0xff` is always true (0xff is -1), so a "none" reads switch_channels[-1] | unsigned bytes; 0xff is none |
| Searchlight (ObjData_Spotlight, its create parameters), rotor | their switch channels signed `char`: a channel of 128 or more indexes before switch_channels | MOVZX: unsigned |
| psiGetTimeIn100ths | `(timestamp() - (float)INITIALISATION_TIME) * 0.1f`: the start time rounded to a float | the difference and `* 0.1` in double on the x87, then `__ftol2` (Ftol64) |
| HUD_UpdateRedeemerPane | an early port: positions and the static's tint in float and int, the angles from `M_PI` (doubles), a debug printf | on the x87 with the original's float constants, `__ftol2`, the tint a short |
| Player_PositionCamera | the crouch offset's `FRAME_RATE_MUL * 0.0225f` and sum in double (SSE2) | on the x87; 1 ulp apart in the view's height by EvilBase's frame 302, which moves the objects placed round the view |

The signed channels were a class: `clang -Wchar-subscripts` over the action engine lists 166 places a plain `char`
indexes an array. Nearly all are weapon numbers (0-114), players, counts and object types, which never reach 128, so
the sign cannot matter; the switch channels (0-255) are the ones that can, and each was checked against the
original's load (MOVZX).

The bug fixes our build makes on purpose (FIX_ORIGINAL_GAME_BUGS: Locks_Init's keypad digits, Menu_UpdateWheel's
empty slots) are kept ours too, and so is one departure: the original's AnimFrameSet blends the bones a skin does not
use from uninitialised stack, ours from zeros (docs/nightfire-port-lifts.md), which only the original chain under
AnimObjectUpdate shows.

### The hunt

`NIGHTFIRE_ORIGINAL=game` over every recording, stopping at the first frame that differs (`NIGHTFIRE_DEMO_STOP`);
then the smallest set of originals that makes it differ by a few frames later, by delta debugging over
`NIGHTFIRE_ORIGINAL_AT` (`NIGHTFIRE_DEMO_UNTIL` ends each try there); each found set kept ours
(`NIGHTFIRE_ORIGINAL_KEEP`) and the recording played again. `NIGHTFIRE_DEMO_DUMP_OBJECTS` and
`NIGHTFIRE_DEMO_DUMP_VIEWER` print what the checksums cover, to diff two runs. What it taught:

- Every global we own needs its tag, or a mixed set of originals and ours uses two copies and the hunt names the
  wrong function (dirFileBuf, GMapSounds, DoneBetter were untagged).
- Our ports call each other's C cores, not the game's addresses, so an original inside a chain of ours never runs;
  only a set with the chain's outermost original shows it (AnimObjectUpdate, AnimSeqTick, AnimFrameSet).
- The recordings were made on our build, so a fix that changes the game parts them from what was played, and a
  rebase plays the same input another way. The fixes go in together, checked on rebases with the originals, and
  the recordings are then made again once.

With the 12 kept, all 11 recordings matched every frame with the rest of the game's code as the original (21 October
2026).

## Left for the standalone target

- Table entries that are still the game's code: the drone mode table's `NDrone2_init_DMODE_Normal` (a jump to
  `NDrone2_init_DMODE_Defaults`), three type-table controls Ghidra never made functions (0x0003f8a0, 0x0003f910,
  0x0003f930), and the linker's shared empty function (0x000e0ec0) in it and in `control_funcs`.
- Tables read through a base outside them: the cover anims' pointer lists (NDrone2_Cover.cpp), the Inflate
  length and distance tables (Inflate.cpp).
- The language page's help lines (ui_frontend.cpp): our table of the game's string addresses, whose pointers labels
  keep.
- PAUSE_BUILD_STRING, which an original bug writes into (Menu_SetLevelBonus), and the menus' def_codename: state.
