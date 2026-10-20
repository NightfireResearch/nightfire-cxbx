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

## Left for the standalone target

- Table entries that are still the game's code: the drone mode table's `NDrone2_init_DMODE_Normal` (a jump to
  `NDrone2_init_DMODE_Defaults`), three type-table controls Ghidra never made functions (0x0003f8a0, 0x0003f910,
  0x0003f930), and the linker's shared empty function (0x000e0ec0) in it and in `control_funcs`.
- Tables read through a base outside them: the cover anims' pointer lists (NDrone2_Cover.cpp), the Inflate
  length and distance tables (Inflate.cpp).
- The language page's help lines (ui_frontend.cpp): our table of the game's string addresses, whose pointers labels
  keep.
- PAUSE_BUILD_STRING, which an original bug writes into (Menu_SetLevelBonus), and the menus' def_codename: state.
