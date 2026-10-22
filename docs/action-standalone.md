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

## The game's state in our source (22 October 2026)

Step 3 of the plan. tools/own_globals_action.py turns the address macros global_coverage.py calls ready to own
(nothing live touches them) into definitions of ours: `#define Name U32_AT(0x...)` becomes an `XBE_GLOBAL` tag and
`uint32_t Name;`, a header's macro an `extern` with the definition in the .cpp of the same name (or `--into=` one);
`--initial` gives the ones with initial data in the XBE their values (scalars, a float in the fewest digits that give
back its bits, strings). 439 globals this round: 939 of 1080 are ours, and tools/standalone_audit.py's worklist is
778 literals (1312 before; the game's state 154, from 688).

**The originals use our copies in every run.** `AllOriginal_RelocateOwned` (devtools/AllOriginal.cpp), called by the
injector before anything runs an original, points every site tools/data_refs.py found at our copy, found by name in
the DLL's map: so the shadow tests' originals read and write what our code does, as the all-original runs already
did. The shadow tests name the game's state by its addresses; `XbeOurs(address)` is the same place in ours,
`GameSpan<T>` a run of addresses that may cross several of our globals (element by element), `XbeOursRead/Write/Fill`
a span copied or set, and `ShadowRegions::Add` splits a region the same way.

What it found:

| Found | Now |
|---|---|
| `t_hashtable`'s macro said 1000 entries and its tag 0x10 bytes; the table is the 0x8000 bytes up to `ht_insert_ind`, and nothing checks its count | `hashtable_entry t_hashtable[4096]` |
| `menu_unlock_everything` and `switch_allowFreeze` declared `char` in one file and `uint8_t` in another; the original sign-extends both (MOVSX into PTPDATA's `unlockEverything`) | `char` everywhere |
| 0x001d7824/5 named `bShadowCharacter`/`bCharacterLight` in Anim.cpp and the other way round where they are used (psiDraw.cpp, view.cpp: object flag bits 2 and 3) | `bCharacterLight` at 0x001d7824 |
| Second macros for an owned global in other files (Player.cpp's and ui_tweaks.cpp's `Plr_DMod_*`, multiplayer.h's assassin pair, and 100 more): those files read the game's copy | `extern`s; aliases under another name joined (`nDistanceTables`, `controller_mission_end`, `saveflow_dashboard`) |
| Switch.cpp read the security items 4 bytes past the table's start as its own 8-byte struct | `SSysItems[i].channel` |
| Two file statics both called `CelList` (Collide.cpp, build.cpp), which the map cannot tell apart, and function statics the map reader took for globals | Collide.cpp's is `StraddleCels`; the reader takes only `?Name@@3` |

tools/data_refs.py's rules, for state rather than constant tables: a plain `[address]` operand names its block
itself, and only a register-indexed operand or an immediate has to be named by the port of the function it is in (an
immediate can be the end of the table before a block, or an address the linker put inside one: WeaponDataTableInit
stores 0x001820a0, inside MapMusic, into weapon_data, and relocating it changed the game); an odd offset is a byte
field of an array of structures where the port names the block (manager[i]'s); a base folded for an index that starts
at one lands just before the block it indexes (manager[i] through 0x0025f1c9) and goes to that block; a value past the
nearest block's end inside an enclosing one goes to the enclosing one (BondMoments[0x65], after WireAnim inside it); a
loop's bound may be a little past a block's end (MenuManager_Init's `cmp eax, 0x0025fcd4`, the sixth manager's field),
given to the largest block it can end, and to one block only; and every entry the injection table patches is decoded
from, with a plain `[address]` taken without Ghidra's reference where Ghidra never made a function (the drone controls
at 0x0003f8a0, 0x0003f910 and 0x0003f930, whose original Ninja control read the game's switch channels: the long
CastleExterior recording parted at frame 22784). XbeRelocate accepts a value 0x200 either side of its block for those.

Not every global can move alone: the object list's head at 0x001df338 is used as if it were an object, its
next-object field DynamicObjList_FirstObj (0x001df34c); owning the field alone emptied the list (every recording
parted by frame 400), so the three stay at the game's address until the head moves whole.

Checks: all 19 shadow suites as before (290 result lines, 0 differences); the 13 recordings, every frame, on our
build and with the game's code as the original.

## Calls and code pointers (22 October 2026)

Every call our code made through an original's address, and every original's address it stored, is now a call to or
the address of our function: standalone_audit.py's `code.called` 174 to 0 and `code.pointer` 77 to 0 (the worklist
778 to 232, with `code.other`'s bit masks, which only looked like addresses, now counted as constants).

- **Calls to ports.** 131 `#define Name ((type)0x...)` macros of functions already ours became their headers'
  declarations. Where a macro's prototype differed from the port's, the original decided: BOTSTATE_combatWeaponChangeChoice
  takes a fifth argument it never reads (every caller pushes five), MP_sendTeamBotMessage's third is the message's
  `void *` payload (a weapon number for message 0x44), and the rest are casts the other callers already had.
- **The functions the linker made one.** The linker folds identical functions into one: a bare `ret` at 0x000e0ec0
  (View_Init, AnimCacheInit, Sensor_ApplyDamage, Window_Update, Menu_StringInit, the HUD panes and object types with
  no update or delete, the drone modes with no init...), `return 0` (Light_FindMapLight with psiLight_Create),
  `return false` and `return -1` (the particles). Each is now an empty function of its own where it is used.
- **Tables.** tools/drone_tables.py names a `FUNC_AT` port as well as an `AUTOINJECT` one (NDrone2_init_DMODE_Normal,
  the Zoe, Ninja and Astronaut controls, whose slot now returns `bool` as they do) and writes the shared empty
  function as `NDrone2_initNothing`; devtools/DroneTablesCheck.cpp takes a bare `ret` of ours for it.
- **Callbacks.** The player's camera updater (Player.cpp, Monitor.cpp) and the drones' root motion callback are ours;
  nothing compares them with the originals'.
- **The platform seams.** dsndSeam.cpp and d3dSeam.cpp named each library entry point's address only to give the
  compiler its prototype; `DSoundSeam<Prototype>::Traced` and `D3DSeam<Prototype>::Traced` take the prototype as a type.
  XboxFile.cpp's last error goes to Win32's alone (XboxStartup.cpp already points XAPI's at it), and XboxStartup.cpp
  calls Game_Main (the macro also had it `__stdcall`, which it is not).
- **The harness's stand-ins.** MenuProbe stood in front of the pads' poll and the handlers' messages by redirecting the
  originals' entries, and our callers went through those entries to meet it. They call `MenuProbe_PollDevices` and
  `MenuProbe_HandleMessage` (devtools/MenuProbe.h) instead, which go to the stand-ins while they are on and to ours
  otherwise; the standalone build answers them with ours.
- **D3D8's state.** The library's dirty flags, `D3DTextureState[4][32]` and `D3DRenderState[0xa6]` (0x001117cc to
  0x00111c68, all zero in the XBE, the driving engine's layout) are ours in d3dSeam.cpp; only our ports wrote them,
  and the library that read them is gone. The 128 vertex shader programs (0x0019cd08 to 0x001b4d78) are generated
  by tools/vertex_shaders_action.py into Direct3D/VertexShaders.inc.

Checks: the 19 shadow suites, 0 differences (effectport's fake for the cache flush thunk no longer logs its calls:
our ports call empty functions of their own there); the 13 recordings, every frame, on our build and with the game's
code as the original; MenuCheckLists' item lists and drone tables against the game's, 0 differ; and, for the D3D8
state and the shaders, 78 frames of three recordings (every 300th) byte for byte the same as before them.

## The rest of the game's data (22 October 2026)

The last 232 literals of standalone_audit.py's worklist, to none: every global and table the action engine's code
names is ours (global_coverage.py: 1033 of 1073 before the last hand-made ones), and what is left at the game's
addresses is the injector's own work, `#ifndef NIGHTFIRE_STANDALONE`.

**Tables with their data.** own_globals_action.py --initial writes a structure's or a table's initialiser from the
XBE through tools/xbe_layout.py: clang (the nf-cross image, the Linux build's flags) compiles the file with a probe of
the type and dumps its layout, and each field is written by name (designated initialisers, zero fields left out) -
a pointer as the function, table or string of ours it points at. `--any NAME[=LENGTH]` takes macros global_coverage.py
does not list, tools/tag_sizes_action.py tags the ones it cannot size, and driving_data_check.py --action compares
every initialised one with the XBE (620 of 625, 0 differ; the other five are static constants). Generated alongside: the fonts (tools/font_tables.py,
gfx/FontData.inc), the error screens' pictures (tools/error_images_action.py), and the drones' anim states, transition
lists and scripts (tools/drone_tables.py). The HUD panes' 22 sprite tables are each the pane's count long; the big
ones live in files of their own (sound/SFXOutputData.cpp, drone/DroneAnimInfo.cpp).

What the work turned up:

| Found | Now |
|---|---|
| HuffmanTables was 0x2788 bytes, over MAT_IDENTITY and a dozen globals after it: the symbols' list has 290 places, not 512 | 0x2410, the game's block |
| The object list's head: the start of an object as far as its links (its nextObject the first object) | `OBJLIST_HEAD`, owned whole |
| The anim states' transition lists are separate arrays, 4-aligned, some holding more than one list | one array per state's start |
| The corner cover anims' table is read from 4 bytes before it (the sides are 1 and 2) | `DroneCoverCornerAnims[mode][side - 1]` |
| The Inflate tables are read past their ends by an invalid symbol | one structure, in the game's order |
| `CrtDefaultLocaleInfo` is the C runtime's exception-action table (_XcptActTab), the per-thread data's 0x54 | `XcptActTab` |
| Menu_SetLevelBonus's stray write lands on the space after the time in the build string, not its last "8" | ours writes PauseBuildString[0x18] |
| `field12_0xc` of PlayerInput is the flashing objects' codename option | `flashingObjects` |
| Second names for one global: LastWalkTarget/WalkTestTarget, Font/CurrentFont, menu_bonus/cn_bonus, the drone list | one definition, the rest by name |
| The generated tables' tags are in .inc files, which data_refs.py, global_coverage.py and driving_data_check.py did not read: the originals kept the XBE's weapon table (which the game writes), fonts, drone anims, shaders and error images | .inc read; 625 initialised globals checked, not 365 |
| 18 operands Ghidra has no reference from (Boot_GetPTPData's `mov edi, 0x001d7e90` copy of the codename, the drone damage table's `fld`s, three font loops' bases folded for an index from one) | data_refs.py's CHECKED_SITES, each by hand |
| 35 masks of 0x00200000 (a drone's alarm-sent flag) taken for StringHeap's start: the originals tested and set our heap's address | `test`/`and`/`or`/`xor` immediates are never sites |
| Input_Update polled through psiInput_PollDevices, not the thunk at 0x000de5c0 the original calls | through the thunk |
| The animport suite crashed in its scripts group from this round's start, which its summary lines did not show (6ee861a0 says 19 clean): AnimPortShadow cleared and copied the anim globals' span (0x001d69c0-0x001d7830) through XbeOurs of its first address, over the globals of ours that follow that one; InflateShadow copied 0x2788 bytes of HuffmanTables | the span through XbeOursFill/Read/Write; the table's size; the suite runner reports a crash |
| The hudport suite crashed in its message groups from this round's start (the six after "sprites" never ran): its random messages hold characters past the fonts' last glyph (252), for which the glyph search reads the entry after the font's table (the game's as ours: after Font2Chars, FontTable[1], whose font address is a 0xd668-wide glyph), and a two-line subtitle with a glyph wider than its line ends in strcat(line, NULL) - the original's crash, which no text of the game's reaches | the suite's messages keep to the fonts' glyphs; its string heap is restored with each case |
| The originals of PlayerAnimSetInitNormal/Crouch kept the XBE's lists by class, Player_WeaponFiring's DecryptorDigits (a base folded for digit - '0') and ES_JumpToMusicMarker's SFXFilePath: the ports name them through a helper or a table, which data_refs.py's port check does not see | CHECKED_SITES |

**The start-up.** XboxStartup.cpp's patching of the XBE (the entry, XAPI's heap, the per-thread data, the WBINVDs
and the notification hook), its reads of the XBE header and TLS directory, and CRuntime.cpp's sprintf patch are the
injected build's only; the standalone build gets the header's stack size as a constant, no XAPI initialiser table (its
one entry is NULL), and the kernel's critical sections, KeBugCheck and contiguous memory over Win32, as the loader
answers them. Xbox_Entry is what its main will call.

## The standalone executable (22 October 2026)

`action_standalone.exe` links our action code and nothing else of the game: no loader image at 0x10000, no injection
DLL, no FUNC_AT patching, no default.xbe read at build or run time. `action_standalone_test.exe` is the same with the
time demos' player (devtools/Demo.cpp). `cmake --build . --config Release --target action_standalone
action_standalone_test`: action_standalone is in the default build, the test variant EXCLUDE_FROM_ALL. Both compile
the game's sources as actioninject does (`nf_action_sources` in CMakeLists.txt, the same definitions, no inlining),
with NIGHTFIRE_STANDALONE. The injected build (action + actioninject) is unchanged and stays the test build with the
shadows and the all-original runs.

src/action/standalone/StandaloneMain.cpp does what the loader and Inject() did, by calling our code: the launch options
(`LaunchOptions_Read`), a time demo's mode (`Demo_Init`, a no-op in action_standalone), the disc root
(`Xbox_SetDiscRoot` with settings.ini's DiscPath), FFmpeg (`Fmv_Init`), then the render window the D3D9 backend finds
by its class, `Xbox_Entry` (the game's thread at mainXapiStartup with the XBE header's 0x10000 stack), and the window's
messages on this thread. Closing the window ends the process. A fault nothing handles is reported with offsets into
the executable for action_standalone.map, and `NIGHTFIRE_REPORT_AFTER=<seconds>` reports where every thread is then.

Nothing is patched in it: the start-up's WBINVDs and notification hook, XAPI's heap, the per-thread data, the
injection table, the relocation of the owned globals and the resolution patches of Inject() are the injected build's
alone. src/action/standalone/NoDevtools.cpp answers the 19 devtools hooks game code calls as each devtool does when
nothing asks it to: no demo (the game's own clock, pads, mouse and sound device), `MenuProbe_PollDevices` and
`MenuProbe_HandleMessage` the game's own `psiInput_PollDevices` and `Handler_HandleMessage`, `Devtools_Tick`,
`SndTrace` and `FileDump_Save` nothing. Demo.cpp names our globals (`control_first_object`, the random generator's
four words in the game's order, so the checksums are the same) and leaves out, without the XBE, the kernel import slot
it patched for the originals, `Demo_RestoreOriginals` and the data-write and memory traces.

**Checked:** all 13 recordings match the game frame for frame on action_standalone_test.exe (tools/demo/play_demos.ps1 -Exe Release/action_standalone_test.exe), as on the injected build; action_standalone boots to the attract movies on the runner; both link in the Linux preset's MinGW build.

The two engines launch each other (common/launchInfo.cpp): a standalone engine starts the other's standalone
executable (action_standalone.exe, driving_standalone.exe), the injected builds the other's loader.

## Next

- 64-bit, then Linux, macOS and arm64, as the driving engine went (docs/driving/64-bit.md): the action engine's
  window, input and sound are Win32, XInput and XAudio2 here, where the driving engine's go through its SDL3 host
  layer.
