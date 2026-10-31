# Pre-release `cached_filesys.dat` (6 October 2002)

`prerelease_cached_filesys/cached_filesys.dat` (784,334,848 bytes) was recovered from a debug Xbox. It is the action
engine's hard-disk cache of its file archive, `z:\cached_filesys.dat`. It is not a copy of the disc. It holds:

- the full archive index of a pre-release build;
- 16 files that build cached, all verified by their CRCs;
- about 390 MB of leftover data from earlier builds' caches;
- 323 MB of a hard-disk sector test pattern.

Everything below was checked against the retail EU disc in `disc/`. `tools/prerelease/` extracts the level bundles
and compares them with retail (Reproducing, at the end); the one-off scripts and small extracts of the first study
are in `build/prerelease/` (gitignored).

## Format

The format comes from the original `FS_Init` (0xe2a40) and `FS_StateMachineIterate` (0xe2d90) in `default.xbe`. Our
`src/action/engine/FS.cpp` leaves the cache out.

- **Bytes 0-0x137ff: a copy of the archive index.** This is the same 0x13800-byte block as the start of
  `filesys.d00`: a 0x20-byte header, then 0x26-byte entries `{nameCrc, dataCrc, verifiedAt, offsetLow, offsetHigh,
  storedLen, len, u16 flags, filesysIdx, offsetInPart}`. `nameCrc` is the zlib CRC-32 of the lower-cased path.
- **Header fields that differ from the disc copy:**
  - +0x04 is the CRC-32 of the whole 0x13800 bytes, taken with this field zeroed.
  - +0x08 is a boot count, incremented on every `FS_Init`.
- **The cache is valid only for its own build.** At boot, `FS_Init` compares the cache's +0x00, +0x0c, +0x10, +0x14,
  +0x18 and +0x1c with the disc's, and checks the CRC. On any mismatch it does not clear the file. It only sets the
  file's size to 0x2ec00000, which is exactly the size of this file. Whatever an earlier build left in the file
  therefore stays there.
- **Each file is cached at its global archive offset.** The cache offset is `offsetLow | offsetHigh << 32`, the
  file's offset across the whole `filesys.dNN` set. The cached bytes are the stored (EDL-compressed) bytes.
- **Entry flags:**
  - bit 0: cached;
  - bit 1: EDL-compressed;
  - bit 2: never cached (the 116 uncompressed sound sample banks and music streams).
- **`verifiedAt` (+0x08 in each entry)** is the boot count at which the cached copy last passed its CRC check
  (state 0xA).
- **The header is written again after every successful cache write** (state 6, then 7: `calcCrcAndWriteCacheToFile`).
  So the cached flags on disk can be trusted.

This cache's header: boot count 3, CRC valid. The build fields are +0x0c 0x07f84000 and +0x14 0x2b5636a6; retail has
0x07f9a800 and 0x1de4dcfe. There are 385 entries, the same number as retail, and the set of names is identical.

## What the file holds

| Bytes | What | Matches retail? |
|---|---|---|
| 0x13800 (80 KB) | the pre-release index (all 385 entries) | names identical; 51 files have the same data CRC, 334 differ |
| 35.0 MB | the 16 files the pre-release build cached (all pass their CRCs) | 6 small ones byte-identical; 10 differ |
| 303 MB | 160+ complete EDL sections left by earlier builds | 65 small copies are byte-identical to retail; everything else differs |
| 322.6 MB | disk test pattern: each 512-byte sector is `{u32 LBA, 252 u16 counting by 2, u32 LBA}`, with one constant LBA offset across the file | not game data |
| ~86 MB | fragments: tails of older compressed files whose first sectors were overwritten (EDL block headers inside), and other small pieces | not identified |
| 0.1 MB | zeros | |

Byte for byte, under 0.3 MB of the file equals retail data.

### The pre-release index against retail

All names were recovered from their CRCs except 41 entries. The named entries are:

- the 32 level bundles `07xxxxxx.bin`;
- the sound banks `<LANG>/SB_n/SB_m.SFX|SHF|SBF` (no path prefix);
- `SBINFO.SBI`, `MFXINFO.MXI`, `woman.rle`;
- the nine text banks.

The unnamed entries are the 16 music/stream files (flag 4, all identical to retail) and 25 small files.

| Kind | Same as retail | Different |
|---|---|---|
| level bundles (32) | 0 | 32 (every one; sizes differ by up to 0x30000) |
| text banks (9) | 0 | 9 |
| sound bank `.SFX`/`.SHF`/`.SBF` (300) | 16 | 284 |
| `SBINFO.SBI`, `MFXINFO.MXI`, `woman.rle` | 3 | 0 |
| unnamed (41) | 32 (the 16 music/stream files and 16 small files) | 9 (small, 0x13000-0x47800 bytes unpacked) |

No level or file is unique to the pre-release build: it has the same 385 names as retail, `07000999.bin` included.

### The 16 cached pre-release files

| Entry | Name | Cache offset | vs retail |
|---|---|---|---|
| 0x7df17198 | `07000005.bin` (CastleExterior) | 0x033d1800 | different |
| 0x3a510b48 | `07000006.bin` (CastleCourtyard) | 0x04155800 | different |
| 0x1ef0f73f | `07000048.bin` (front end) | 0x17b16800 | different |
| 0xe729d14f | `07000500.bin` (shared) | 0x19e91000 | different (3 of 17 sub-files) |
| 0x06054770 | `UKTxt.Dat` | 0x1a0b3000 | different (see below) |
| 0xadce45b9 / 0xc9425554 | `ENGLISH/SB_0/SB_3.SFX/.SHF` | 0x19f43800 | different (smaller) |
| 0x1fee99a9 / 0x7b628944 | `ENGLISH/SB_0/SB_4.SFX/.SHF` | 0x19f45800 | different (smaller) |
| 0x8ee22f84 | `ENGLISH/SB_0/SB_0.SHF` | 0x1a0f5800 | different |
| 0xea6e3f69, 0x287ca1e1, 0x65cd55a6, 0x97c3e51c, 0x2a5b2328, 0x6dfb59f8 | `SB_0.SFX`, `SBINFO.SBI`, `MFXINFO.MXI`, `woman.rle`, two small unnamed | | identical |

### Leftovers from earlier builds

The earlier builds' files sit at their own builds' offsets, so they are found by content. Every complete EDL section
was decompressed (the decompressor is from `tools/anim/edl.py`, commit 7af6075). Bundles were matched by their
directories and map files (entry type 1).

- **27 level bundles from earlier builds, 6 of them cut off.** `tools/prerelease/extract.py` finds them (the
  table below). With the pre-release build's own 07000005, 07000006, 07000048 and 07000500 that makes 31 bundles.
  Every one is a build of a retail level; none is a cut level. (An earlier count of 29 took the pre-release
  copies of 05 and 06 for older ones.)
- **At least three earlier builds:**
  - The bundles' menu file `08000001` comes in three sizes: 27520 (01-0d, the same as the pre-release), 27776
    (12-15 and 23) and 27456 (24-29). Retail's is 27424.
  - Old copies of the same small group of files sit at four different offsets: 0x19c3f800, 0x19d3e000, 0x19dfc800
    and 0x19fa3000.
- **Three older `UKTxt.Dat` banks**, with 2235, 2645 and 2656 entries. The pre-release bank has 2724 and retail has
  2800.
- **About 60 older sound-bank header files** (`.SFX`/`.SHF` pairs, by their sizes). No sample data survives: `.SBF`
  is never cached.

The level bundles (cache offset, EDL blocks decoded of those the section header lists, sub-files present of those
the directory lists):

| Level | Copies |
|---|---|
| 07000001-04, 08-0d | one older build each, complete (0x00013800 ... 0x09fa4000) |
| 07000005 CastleExterior | older 0x0338b800: 0/22 blocks, 32/1251 sub-files (sequences only; the pre-release copy overwrote the rest); pre-release 0x033d1800 |
| 07000006 CastleCourtyard | pre-release 0x04155800 only |
| 07000012-15 | one older build each, complete |
| 07000023 Atlantis | older 0x1092e000: 24/26 blocks, 1860/1861 sub-files - all but the map file |
| 07000024-28 | one older build each, complete |
| 07000029 SnowBlind | older 0x167ca800: 24/27 blocks, 1910/1911 sub-files - all but the map file |
| 07000048 (front end) | older 0x17740000: 8/18 blocks, 6/7 sub-files (no map file); pre-release 0x17b16800 |
| 07000500 (shared) | older 0x19d27000 and 0x19c95800 (16/17 sub-files, no map file), 0x19a76800, 0x19b74800; pre-release 0x19e91000 |

The 07000013 copy carries many of Tower2Elevator (0700004a)'s files too, so its directory overlaps 4a's slightly
more than 13's; its map file is 13's.

## Differences worth knowing

### 1. A cut multiplayer character skin (confident)

The five complete old MP bundles (07000024-28) each contain two files that are in no retail bundle:

- **skin `0500009d`** (2144 bytes). It has one rigged mesh, `02000916`, on skeleton 0.
- **graphics pack `010001e4`** (170,848 bytes), which holds that mesh and five DXT1 textures: `newelite_legs`,
  `sniperboots`, `elite_bod`, `sniperhands` and **`Mp_eliteface2`**.

**Its retail counterpart.** Retail keeps only the sibling pair: skin `0500009e` with pack `010001e5`. That pack's
textures are `newelite_legs`, `sniperboots` and `elite_bod` as TD duplicates of `010001e4`'s, plus `elite_b_hands`
and `Mp_elitefaceb`.

**It is gone from retail.** The retail XBE's MP character table (file offset 0x154700, `{skin, pack, n, 0}` records)
lists `0500009e`/`010001e5`, but has no record for `0500009d`/`010001e4`.

**Extracted:** `build/prerelease/unique/` holds the skin, the pack and the five textures as PNG. The face texture shows
a helmeted soldier.

### 2. Level changes (confident in what changed; the reasons are not in the data)

`tools/prerelease/diff_all.py` compares each recovered bundle with the next build of its level and that with
retail (`tools/level/diff_level.py`, `export_level_diff.py`). Counts below are the map file's dynamic placements
(the objects the game creates) unless they say otherwise. "Older" is an earlier build's copy; "pre" the
pre-release build's.

**Across every level.**

- **Weapon numbers shifted.** In every older bundle the Drone `weapon` and Pickup `weaponId` keys hold the same
  objects' values shifted by fixed amounts: 17 -> 18, 25 -> 28, 22 -> 25, 27 -> 30, 41 -> 44, 69 -> 71 and so on,
  the same mapping in every level. Pre-release against retail shifts 21 -> 22, 35 -> 36, 23 -> 24. Most "keys
  changed" counts in the reports are this alone.
- **Sub-files.** Most animation sequences differ only in their bounding-sphere floats (+0x10-0x1f), and most
  graphics packs only in placement padding, the texture list headers and the smaller mip levels: the reports count
  these as trivial. In each pair two or three game scripts (type 7) and the menu file `08000001` really changed.
- **Ambient radiators** (map block 0x26): 07000001 60 -> 26, 02 91 -> 50, 04 74 -> 26, 15 489 -> 163. The other
  levels have as many as retail.

**Per level** (older or pre -> retail; the full lists are in the reports):

| Level | Placements | What changed |
|---|---|---|
| 07000001 HendersonA (older) | 206 -> 205 | one Light removed; one TriggerTouch moved 1.2 and its glist changed; 13 Drones and 2 Pickups by weapon number only. Rooms unchanged |
| 07000002 HendersonB (older) | 263 -> 266 | the garden re-cut into fewer rooms (42 -> 28): `garden1`-`garden6`, `garden8`, `garden8_roof` and two other cels lie on retail room `garden7`'s surface, and `waterarea2`-`waterarea7` on `waterarea1`'s (all 16 fully covered), so the portals between them go too (62 -> 37); 3 cels added. TriggerTouchOnce 22 -> 24, a TriggerMultiplexSIn added, a TriggerTouch replaced; 8 switch channels rewired |
| 07000003 HendersonC (older) | 253 -> 242 | RigidBody 12 -> 3, Door 15 -> 13 (the removed doors' paths go too: 23 -> 21 object paths), one Pickup replaced and two moved about 7; one cel added (`opendoorsbedrtoom`) |
| 07000004 HendersonD (older) | 28 -> 28 | weapon numbers only; one cel added |
| 07000005 CastleExterior (pre) | 228 -> 227 | one DroneCoverCorner removed; three Drones moved (up to 6.8). 10 entities' shapes changed, among them the rooms `Room_Road_B`, `Room_Outerwall_Corner` and `Room_outerwall_B4`, and the helicopter parts in pack 01000051 |
| 07000006 CastleCourtyard (pre) | 447 -> 450 | Destroy 4 -> 6, Pickup 10 -> 11 (two added, one removed); three Drones moved (up to 3.5); two Doors gain open and close sounds. 47 entities' shading changed, 5 shapes |
| 07000008 CastleIndoors2 (older) | 182 -> 184 | Pickup 6 -> 8, one moved 10.6; five Doors gain open and close sounds; three DroneCoverLow change their animation |
| 07000009 TowerA (older) | 89 -> 91 | two Hints added; one Pickup moved 7.7 |
| 0700000a TowerB (older) | 281 -> 283 | two Hints added; static cels 4 added, 3 moved, 39 with changed keys, and 4 removed whose surfaces retail rooms `Office-A` and `Office-J+K` now carry; 35 entity shapes changed |
| 0700000b TowerC (older) | 164 -> 163 | one SoundTrigger removed, three set to keep playing; one TriggerTouchOnce moved 1.0; 5 channels rewired |
| 0700000c PowerStationA1 (older) | 264 -> 268 | DynamicObject 3 -> 5, a SoundTrigger and a TriggerTouchOnce added; six Destroy objects' health 0 -> 20; static cels 7 added; of the 5 removed, two meshless blockers and a pallet stack are gone and the rest replaced by other geometry |
| 0700000d PowerStationA2 (older) | 261 -> 281 | DroneCoverLow 60 -> 68, DroneCoverCorner 52 -> 56, Drone 39 -> 41 (3 added, 1 removed), Pickup 14 -> 16, a DroneSpawner, DynamicObject, ThirdIcon and TriggerTouch added; 11 static cels added; an AI path added |
| 07000012 Tower2B (older) | 335 -> 312 | Breakable 126 -> 97, all four Hurt volumes removed, TriggerTouchOnce 23 -> 28, SkyObject 1 -> 3, a Destroy, DroneSpawner, Hint, Pickup and DroneAIPoint added, a Door and a DroneCoverCorner removed; `drawWeapon` 10 -> 1 on 28 Drones; a room added (`corridors_b_4A`), 20 cels added; of 6 cels removed, 5 now lie on retail rooms (`Office-A`, `Office_N-security room`, `MERGED_4 RIGHT`) and one meshless blocker's space holds other geometry; 53 entity shapes changed; 6 AI paths changed |
| 07000013 Tower2C (older) | 116 -> 121 | the three Shooters removed; Pickup 3 -> 7, SkyObject 2 -> 4, TriggerTouchOnce 6 -> 8; `drawWeapon` 10 -> 1 on 10 Drones, two Drones' skins changed |
| 07000014 EvilBase (older) | 688 -> 745 | 35 Emitters added (none before); DynamicObject 26 -> 45; Drone 60 -> 61 (6 added, 5 removed), DroneSpawner 11 -> 14, ScriptPlayer 12 -> 15, InverterTrigger 1 -> 4, Door 59 -> 55, Pickup 34 -> 32 (four added, six removed, nine moved, one of them by 92); 42 switch channels rewired; room `Underwater_section1` added, 63 cels added; the 42 removed cels (41 `Fire` cels and one more) lie on retail cels `FX_Fire_Small`, `FX_Fire_Large` and room `CorridorA8_1` - the same surfaces under new names; 30 of 74 object paths changed |
| 07000015 EvilSilo (older) | 296 -> 254 | the older build has corridors between the hub rooms on three floors (cels' centres at heights 24, 33 and 42); retail keeps only the top floor's. The 24 lower `room_corridor_*` rooms (A and B) and their 34 `corridor_*` and lamp cels are gone - retail has no surface within 0.2 of 99% of them and no geometry in their bounds - as are the 8 `ladderbit*` rooms; 25 smaller cels removed (codepads and others) lie on retail's `Silo_D*` cels. Rooms 91 -> 59, portals 121 -> 62 with them; Door 82 -> 54, and 14 more moved about 1 with their paths; Corona 90 -> 76; five Mines moved |
| 07000024 SkyRail (older) | 112 -> 112 | a Car replaced, another moved 15.9; Pickups by weapon number; a room added (`MHU1C1`) |
| 07000025 SubPen (older) | 126 -> 126 | four Hurt volumes moved, two of them now `hurtType` 6 with `damage` 1000 (were 0 and 1); three MPObjects moved |
| 07000026 StealthShip (older) | 96 -> 99 | rooms 5 -> 1: `Upstairsstorage`, `Upstairsstairs` and their copies are re-cut into retail room `stealthship`, which covers all of their surfaces, so the 7 portals between them go; cels `waterA` and `Corona_Demolition` gone; Hurt 1 -> 4; all four Cars moved (up to 9) |
| 07000027 FortKnox (older) | 113 -> 113 | weapon numbers only; one Car turned |
| 07000028 MissileSilo (older) | 82 -> 83 | Pickup 16 -> 17 (two added, one removed); 7 static cels added; cel `Corona_Demolition` gone (as in 24, 25, 26 and 27) |
| 07000023 Atlantis, 07000029 SnowBlind (older) | - | map file cut off: sub-files only |
| 07000048 front end, 07000500 shared | - | they place nothing: sub-files only |

**Where removed rooms went.** A cel or room missing from the later build is not always space taken out.
`diff_level.py` samples each removed cel's surface and looks for the later build's surfaces within 0.2 units:
"re-cut" when 60% or more is still covered (it names the cels that cover it), "replaced" when less is but the
later build has other triangles inside its bounds, "gone" otherwise (a cel without a mesh is judged by its bounds).
Over all pairs: 07000002's 16, 0a's 4, 12's 5, 14's 42 and 26's 5 are re-cut; 15's lower corridors and ladder
rooms (66 cels and rooms), 0c's two blockers and a pallet stack, and `Corona_Demolition` in the five multiplayer
maps are gone; 0c's other two and 12's blocker are replaced. In the scenes and renders re-cut cels are violet and
replaced ones orange, not red.

**Sub-files only one build has.**

- Older 07000001-04 hold skin `05000002` and pack `010000bd`; retail 01-03 add packs `01000216`, `01000217` and
  skins `050000c2`, `050000c3`.
- Older 07000023-29 hold the cut MP skin `0500009d` and pack `010001e4` (section 1); retail adds packs `01000213`,
  `01000215`, `0100021b` and skins `050000c0`, `050000c6`.
- Retail 07000009, 0a, 0b, 14 and 15 add pack `0100021a`.
- Older 0700000a, 12 and 14 hold game scripts retail does not (`06000044`, `06000045`; `06000041`, `06000042`;
  `06000744`); retail 12 and 14 add `06000a25`, `06000708` and `06000a3b`.

**How sure the matching is.** 4,497 placements were matched across the 21 pairs with a map file on both sides. Two
matches were ambiguous (another candidate scored within 0.05; one is a TriggerTouchOnce in 07000012), and four were
between identical placements, where the choice does not matter. Matching is by type, then nearest position within
4 units (each differing key, glist or path adds 0.5), then identical keys anywhere within 1000 units, then up to
two differing keys within 16 units. Anything left is added or removed, so a placement moved further than that with
its keys changed shows as one removed and one added.

**What could not be compared.** 07000023, 07000029 and the two oldest 07000500 copies lost only their last entry,
the map file, so nothing placed in them survives. The older 07000005 kept 32 of 1251 sub-files (animation
sequences, all identical to the pre-release copy's), and the older 07000048 6 of 7.

### 3. Text (confident)

These are the English text banks: the pre-release's, three older ones and retail's.

- **Pre-release `UKTxt.Dat` against retail** (`build/prerelease/text/UKTxt_pre_vs_retail.txt`): 154 labels differ.
  - The controller presets were named "Agent Under Fire 1" and "Agent Under Fire 2". Retail names them "GoldenEye"
    and "Classic Bond".
  - "Goldeneye Strike" became "GoldenEye Strike".
  - Several weapon and upgrade descriptions changed. For example, "bio-target designator" moved from a
    magnification upgrade (0x3b4) into the base description of the surveillance gadget (0x337).
  - The credits titles changed.
  - 76 labels at the end of the retail bank are new. They include:
    - the MP character names (0x010002e2-0x010002fe);
    - the option descriptions;
    - the "Brake/Reverse" and "Handbrake" controls;
    - two mission-failure messages ("Kiko Escaped", "Missile Launched on Target").
- **The oldest bank (2235 entries)** has text that no retail bank has (`build/prerelease/text/text_not_in_retail.txt`).
  Among it:
  - the MP teams are "Red"/"Blue", not "Phoenix"/"MI6";
  - PS2-style prompts ("L1");
  - bot health, reaction and recovery descriptions in finer steps;
  - medal messages ("You won a platinum medal") and "Reward Card: ULTIMATE LASER";
  - mission objectives that retail does not have, such as "Rescue Geisha Girls", "Retrieve Dragon Safe Contents",
    "Defeat the Assassin", "Escort Mayhew to Bunker" and "Destroy C5 Door with mounted Laser on catwalk";
  - "Streets Of Tokyo";
  - older weapon names: "Storm M9-32", "AT400 Scorpion", "Sentinel AT420", "Kowloon Type 42" / "K-42", "Ronin
    Suitcase Gun", "Shaver Grenade";
  - a placeholder, `<<HINT TEXT NEEDED>>`;
  - early versions of the briefing and cutscene lines.
- **The two middle banks** add "Enter Cheat Code" / "Cheat Activated" and "Continue playing to unlock this
  Enviro-Mod", and use "Scaramanga's henchman".

### 4. Sound bank headers (likely, not decoded)

- The pre-release `SB_0`/`SB_3`/`SB_4` `.SFX` and `.SHF` are a little smaller than retail's: 0x8d60 against 0x8ff4,
  and so on.
- About 60 older `.SFX`/`.SHF` versions survive from earlier builds.
- They describe sound effects, not samples. Nothing reads them yet.

## Confidence

- **Format, index, and the 16 cached files:** certain. The behaviour is from the original code, and every file passes
  its CRC.
- **Which retail level each old bundle is:** confident. Each surviving map file has the retail level's map file
  name, and directory overlap is 0.92-1.0 with it (0.77 for 07000013, which also carries Tower2Elevator's files);
  the bundles without their map file overlap 0.98 or more.
- **The order of the 07000500 copies** is a guess: they are chained by how many of their sub-files equal the
  pre-release copy's, fewest first.
- **The level changes (section 2):** confident in the counts and the matched pairs; see there for how many matches
  were ambiguous.
- **The leftover files predate the pre-release build:** confident. The cache keeps only the newest header and never
  clears data, and every leftover file sits off the pre-release layout. Their exact build dates are unknown: the
  bundles carry a 20-byte hash, not a date.
- **The cut MP skin:** confident that it exists and that retail dropped it. What it was meant to be beyond "an elite
  variant" is not known.
- **The 86 MB of fragments:** not identified. They are mostly tails of older compressed files, which cannot be
  decompressed without their first blocks.

## Reproducing

```
set NF_DISC=Q:\nightfire-cxbx\disc\eurocom\filesys.d%02d
set NF_ANIM_LVL=Q:\nightfire-cxbx\build\anim\lvl          (the retail bundles: tools/anim/extract_all.py)
python tools/prerelease/extract.py prerelease_cached_filesys/cached_filesys.dat C:/nightfire-prerelease/bundles
python tools/prerelease/diff_all.py C:/nightfire-prerelease/bundles C:/nightfire-prerelease --blender <blender.exe>
```

- `extract.py` (a few minutes, about 580 MB) checks the index, writes each bundle as `<level>_pre.bin` or
  `<level>_old_<cache offset>.bin`, and `manifest.json` with each one's retail match, completeness and menu file
  size. A cut-off section is decoded up to the last block it still holds (`tools/prerelease/cachefs.py`), and
  `trim()` in `tools/anim/bundle.py` reads what is there.
- `diff_all.py` (about 15 minutes with the renders) writes `reports/<level>_<from>_vs_<to>.json` and `.md`,
  `glb/<same>.glb`, `renders/<same>_top.png` and `summary.json`. In the scenes and renders: unchanged in grey,
  added in the later build in green, removed in red (violet when the later build's cels still cover the removed
  cel's surface, orange when other geometry fills its space), moved or changed in yellow with the old position as a pale
  ghost and a line, changed geometry in cyan (magenta for the old version, which the render leaves out). The renders
  lay the differences over the roofs and put a dot on each added, removed, moved or turned object.
- The text, small-file and sound-bank findings above came from one-off scripts kept in `build/prerelease/scripts/`
  (not in the repository).
