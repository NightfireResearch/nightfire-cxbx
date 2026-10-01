# Drones, part B: the behaviour tables, senses, combat and animation

Research notes for a later reimplementation of the action engine's AI characters ("drones") in 007: Nightfire
(Xbox `default.xbe`). Every address is Xbox unless marked PS2 (`/PS2_EU_51258/ACTION.ELF`, which has symbols) or
GC (`/GameCube_US/Nightfire.elf`, which keeps debug strings). Nothing in the repository or in Ghidra was changed.

Other parts: **A** covers the drone lifecycle, the state machine (`NDrone2_StateFuncs` 0x177ca0, the `DSTATE`
enum) and the `Drone_tag`/`DCVars` layout; **C** covers multiplayer bots (`BOT_*`, the `DSTATE_Bot*` states) and
navigation (`AINetwork_*`). This part touches their data only where it has to and says so.

How claims were checked: **verified** = read in the disassembly or in a decompile whose types/argument counts
were checked against the disassembly; **decompile** = read in Ghidra's decompile only; **inferred** = my reading
of what a value means; **guess** = weaker. Many drone helpers use custom register conventions (EAX/ESI/EDI/EBX
arguments); the decompiler shows those as `unaff_*`/`in_EAX` and gets their signatures wrong - each such case is
flagged.

Files in this folder:

| File | What |
|---|---|
| `README.md` | this document |
| `behaviour_properties.md` | the 91 behaviour properties: bit layout, meaning, level usage, every reader and setter |
| `behaviour_property_layout.csv` | raw decode of the property layout table 0x1634e0 / mask table 0x163598 |
| `level_drone_behaviours.csv` | every drone placement in the 32 level bundles (555) with its behaviour block and stats decoded |
| `drone_mode_settings.csv` | `DroneModeSettings` 0x1776f8 (36 x 12 bytes) |
| `drone_type_settings.csv` | `DroneTypeSettings` 0x1778a0 (85 x 12 bytes) |
| `anim_states.csv` | `DroneAnimStates` 0x1664c0 (119 x 12 bytes) with each state's transition list decoded |
| `anim_info_rows.csv` | `Drone_AnimInfo` 0x166a58 (436 x 24 bytes) |
| `anim_tables.csv` | `Drone_AnimTables` 0x169338 (436 rows x 27 anim-set columns x 4 bytes): anim script per row and set |
| `drone_sfx_info.csv` | `Drone_SfxInfo` 0x174b28 (346 x 32 bytes), for reference only |
| `function_inventory.csv` | 233 functions in this area: address, size, callers, already ours? |

The scripts that produced the CSVs (capstone over `disc/default.xbe`, and the level tools from the
`blender-exports` branch over `build/anim/lvl/07*.bin`) were research tools and are not in the repository (see ../README.md).

---

## 1. Overview: is there a "big table of behaviours"?

Yes, but it is not a script or bytecode. The drone AI is C code (one function per state in
`NDrone2_StateFuncs`, part A) steered by **data** at four levels:

1. **The behaviour block in each placement** (level file, not XBE). Every drone placement in the shipped levels
   (555 of 555, all `mode` = 100) carries a *behaviour class* (0..16), two *attack modes*, two **91-property
   bitfields** (3 dwords each: a primary and a secondary behaviour, swapped when the drone is alerted) and a
   bit-packed per-class *stats* record. `behaviour_util_get` 0x194d0 unpacks it; `behaviour_util_getProperty`
   0x19440 / `setProperty` 0x19470 read and write single properties through a layout table in the XBE
   (0x1634e0). The properties are flags such as "reacts to sounds", "uses cover", "may surrender", "broadcasts
   an alarm on sighting", "leans out of cover". This is the level designers' tool-driven table (the editor wrote
   the 0x005b0003 header = "3 words, 91 properties").
2. **Two static type tables in the XBE**: `DroneModeSettings` 0x1776f8 (old-style fixed modes 0..35: Normal,
   Guard, Sniper, Hostage, Civilian, Ninja, Zoe, Bot...) and `DroneTypeSettings` 0x1778a0 (85 *DTYPEs*: initial
   state, alert state, init and per-frame control hooks). A drone's DTYPE is computed from its behaviour class
   and attack mode by `NDrone2_GetDTYPENEW` (0x3fae0).
3. **Global tuning floats** (`DroneDamage_*`, `DroneArmour_*`, `DroneFiring_*`, `DroneCaptain_Mod_*`,
   0x164068..0x1640e4), set per level by `ReadTuningVars` (already ours, `src/action/game/obj/Player.cpp`).
4. **The drone animation tables in the XBE**: 119 abstract *anim states* (`DroneAnimStates`), 436 *anim rows*
   (`Drone_AnimInfo`), a 436 x 27 grid mapping (row, anim set) to an anim script hash (`Drone_AnimTables`), and
   per-state transition lists. States call animations by anim-state id; the tables pick the actual `06xxxxxx`
   script for the drone's weapon/character *anim set*.

The only "interpreted" content drones run is ordinary **anim scripts** (`06xxxxxx` files, docs/anim
spec-script): `DSTATE_PlayScript` plays the placement's `playScript` hash, and script *events* call back into
`DroneAnim_EventFunc` 0x33590 (throw grenade, change weapon, melee hit, swap datum...). There is no drone
bytecode. The cut-scene script system (`Script_*`) only touches drones through `Drone_EnableAll` (freeze/unfreeze
all, with two hashcode special cases), `Drone_CoderCreate` (spawn one with a fixed start state) and
`Drone_AIVolume_Create` (verified from xrefs).

---

## 2. The behaviour block

### 2.1 Placement layout (level file)

`level_tag_Drone` in Ghidra already names the keys (key k = dword at `level_tag+0x2c+4k`; docs on the
`blender-exports` branch, `docs/level/objects-actors.md`, list keys 0-14). Keys 15-32 are the behaviour block,
passed to `NDrone2_DoModeSettingsNEW` 0x41230 as `&level_tag.key15` when key 5 (`mode`) >= 0x24 (verified in
`NDrone2_DefaultInit` 0x417f0: `< 0x24` -> `DoModeSettingsOLD`, else `NEW`):

| key | dword | read by `behaviour_util_get` as | check (else "Invalid behaviour data!" on GC, defaults on Xbox) |
|---|---|---|---|
| 15 | behaviourClass | `bs[0]`, copied to drone+0xab | < 0x11 |
| 16 | behaviour1Mode | `bs[1]` -> drone+0xac (attack mode for behaviour 1) | < 6 |
| 17 | behaviour1Header | lo16 = word count n (1..3), hi16 = property count (1..0x5b) | always 0x005b0003 in the levels |
| 18..17+n | behaviour 1 words | copied to drone+0x3d8 (`maybeFirstBehaviour`) | |
| next | behaviour2Mode | `bs[4]` -> drone+0xad | < 6 |
| next | behaviour2Header | as above | 0x005b0003 |
| next n | behaviour 2 words | copied to drone+0x3e4 (`maybeSecondBehaviour`) | |
| next | statsOverrideHeader | if bit 31 set and `hi16 == 0x8001`: lo16 = total words (6), 1/3 per record | 0x80010006 in every drone |
| next | stats words | 3 records, each 49 bits: widths {8,3,8,8,5,8,8,1}, LSB first, written to `drone_stats[class][0..2]` | |

Verified in the disassembly of 0x194d0: the header checks, the two copies (only if the destination pointer in
the `_BehaviourStruct` is non-null), the bit loop. On failure it zeroes both behaviours, sets class 0 and headers
3/0x5b, and returns 0; `DoModeSettingsNEW` ignores the return value (the GC build printed the message). A field
that crossed a word boundary would be truncated (the extractor masks within one dword) - with these widths none
does.

`_BehaviourStruct` (the local in `DoModeSettingsNEW`, PS2 type name; 0x1c bytes, verified):
`+0 u32 class, +4 u32 mode1, +8 u16 count1, +0xa u16 props1, +0xc u32* dest1, +0x10 u32 mode2, +0x14 u16,
+0x16 u16, +0x18 u32* dest2`.

### 2.2 The property layout table (XBE, static)

`behaviour_util_getProperty(id, words)` = `(words[t[id].b0 & 7] & mask[t[id].b1]) >> (t[id].b0 >> 3)`, with
`t` = 2-byte entries at 0x1634e0 and `mask` = u32 table at 0x163598 (32 single bits, then 0x3, 0xe0000, 0x180).
`setProperty` clears the mask and ORs in `value << shift` (no masking of `value`). Verified from both
decompiles and the table bytes. 91 properties (0..0x5a): 88 single bits and three multi-bit fields:

| id | where | width | meaning |
|---|---|---|---|
| 0x20 | word 1 bits 0-1 | 2 | initial alertness index into `{0, 0, 0.66, 1.0}` (a stack array in `DoModeSettingsNEW`) -> drone+0x3fc and +0x408 (behaviour 1), +0x400 (behaviour 2) |
| 0x30 | word 1 bits 17-19 | 3 | tested only as non-zero ("uses cover") |
| 0x44 | word 2 bits 7-8 | 2 | tested only as non-zero (cover distance cap) |

Entry 0x5b (word 0 bit 0) is a terminator/duplicate and is never used. The full list with meanings, per-property
level usage and every reader/setter is in `behaviour_properties.md`. Summary of what the code reads:

- **Senses/alerting**: 0x1f hears sounds; 0x3c can spot opponents; 0x51 reacts to the player even when not
  suspicious; 0x26/0x27/0x28/0x29/0x4f "contagion" - become alerted when a nearby drone of the same group has
  alert-cause bits (saw opponent / was shot / saw a body / saw a surrender); 0x32 broadcast when hurt; 0x33
  broadcast an alarm when sighting; 0x31 dead body is noticeable; 0x55 mission fails if this drone sees you.
- **Reaction on first sighting** (`FUN_0003ea80`, see 4.3): 0x43 surrender, 0x1a challenge (Castle/Tower),
  0x1b coin flip, 0x18 attack, 0x19 combat move, 0x4a grenade.
- **Combat moves**: 0x06 back off, 0x11 dodge, 0x13 strafe-dodge, 0x2e roll, 0x41 strafe, 0x42 step,
  0x40 none at all; 0x17 keep firing at the last seen position.
- **Cover**: 0x30 uses cover, 0x44 distance cap, 0x0f crouch cover, 0x10 crouch-only, 0x0a/0x0b/0x0d lean,
  0x0c step out.
- **Misc**: 0x05/0x08/0x09 captain on difficulty Normal/Hard/Easy; 0x1c resists stun grenades; 0x34/0x36/0x3a/
  0x3b explosive awareness; 0x46/0x47 idle fidgets; 0x4c head tracking; 0x53/0x54 mission fail on death;
  0x59 delete at goal; 0x1d/0x24/0x25 AI path and patrol setup; 0x14-0x16 door opening; 0x56/0x4e/0x4b/0x52
  special DTYPE/mission hooks.
- **Never read on Xbox** (set in the levels, dead data): 0x03, 0x04, 0x07, 0x0e, 0x12, 0x1e, 0x22, 0x23, 0x2a-0x2d,
  0x2f, 0x35, 0x37-0x39, 0x3d-0x3f, 0x45, 0x48, 0x49, 0x4d, 0x58, 0x5a. (Scanned: no code reads the behaviour
  words except through `getProperty`; see `behaviour_properties.md`.)

Code also *writes* properties at run time (e.g. `DoModeSettingsNEW` sets 0x1a/0x56 by level, 0x1d/0x38 by
death-script hash; `DefaultInit` clears 0x14-0x16 for class 12, forces 0x0,0x1,0x6,0x41,0x42 for Castle Exterior/
Courtyard weapon 0x16; `BulletImpact` clears 0x10 when a drone is shot out of `UnderCover`), so a reimplementation
must keep the words mutable per drone.

### 2.3 Primary and secondary behaviour

`drone+0x3d4` points at the words in use: behaviour 1 (+0x3d8) at start; `NDrone2_ChangeToAttackMode` 0x397f0
switches to behaviour 2 (+0x3e4) and sets drone+0x29 = 1 when the drone has an alert state (+0x4a0), and
`FUN_00039790`/`FUN_000397c0` (PS2 `DroneFunc_Set1stBehaviour`/`Set2ndBehaviour`) flip it explicitly. Both only
act when `diVars.mode == 100` (drone+0x8a8). Verified.

### 2.4 The stats table (`drone_stats`, BSS 0x1d7830)

17 classes x 3 records x 0x20 bytes (8 u32 fields), filled from placements (last writer wins; zero at boot).
`behaviour_util_getStats(class, j)` = `0x1d7830 + class*0x60 + j*0x20` (verified). **The only caller,
`DoModeSettingsNEW`, always passes j = 1** (verified: `push 1` at 0x412fe), so records 0 and 2 - which in the
level data are clearly the Easy and Hard variants - are decoded and never used. Record 1 goes to the drone:

| field | bits | drone | used by | level values (record 1, per class) |
|---|---|---|---|---|
| 0 | 8 | +0x98 `bulletAccuracy` (really *in*accuracy) | hit chance `100 - 5*acc` % (`DroneWeap_DoBulletAccuracy`), MP aim | 9, 8, 7, 6, 4, 4, 3; 80 for civilians |
| 1 | 3 | +0x99 aggression (PS2 `baseAggressionLevel`) | burst delay x {1.66,1.33,1,0.66,0.33} (`DroneWeap_BurstDelay`) | 1..3 |
| 2 | 8 | +0x90 `health` (as float) | damage | 10, 12, 12, 15, 22, 25, 28; ninja 50 |
| 3 | 8 | +0x9a | never read | |
| 4 | 5 | +0x9b | never read | |
| 5 | 8 | +0x9c reaction | `DroneFunc_ReactionTime` | 80..160 |
| 6 | 8 | +0x9d recovery | `DroneFunc_RecoverTime`, bot stun | 80..160 |
| 7 | 1 | +0x9e | never read (1 in record 1 only) | |

If record 1's health is 0 the drone gets 10 hp and accuracy 5. Every placement of a class carries identical stats
(one distinct set per class across all 32 bundles), so a reimplementation can treat this as a per-class constant
table but must still load it from the level to be faithful. Full decode in `level_drone_behaviours.csv`.

Health is then adjusted: class `0xbc == 0x10` -> 100; captains x `DroneCaptain_Mod_Health`, damage multiplier
`DroneCaptain_Mod_BulletDamage`, accuracy / `DroneCaptain_Mod_BulletAccuracy`; DTYPE 0xd (Ninja) -> 100 hp,
accuracy 0; astronauts from `weapon_data[0x33]` (`NDrone2_DefaultInit`, part A's function; decompile).

---

## 3. Type tables (XBE, static)

### 3.1 `DroneModeSettings` 0x1776f8 (old-style modes)

36 entries x 12 bytes: `{u8 dtype, u8 side, u16 0, float initialAlertness, void (*init)(Drone_tag*)}`
(verified in `NDrone2_DoModeSettingsOLD` 0x414d0; PS2 symbol `DroneModeSettings`, 35 entries + terminator
`0xffff`). `side` goes to drone+0x119 (`isCivilian`: 1 enemy, 2 ally, 3 civilian - inferred from its uses).
Used when key 5 < 0x24: no shipped level placement does that, but `Drone_CoderCreate` (script-spawned drones)
passes mode 0, and the secondary-mode key (drone+0x11e) indexes the same table for drone+0xaa. After the init
hook, OLD mode forces a fixed set of properties on (0x18, 0x26-0x29, 0x31-0x33, 0x4f, 0x1f, 0x3c, 0x30, 0x0a,
0x0c, 0x37, 0x38) plus class-specific ones.

PS2 names the init functions, so the Xbox table decodes (Xbox folded identical functions together):

| mode | name (PS2) | dtype | side | alert | Xbox init |
|---|---|---|---|---|---|
| 0 | Normal | 0 | 1 | 0 | 0x408a0 (shared "calls Defaults") |
| 1 | Guard | 1 | 1 | 0 | 0x408a0 |
| 2 | Retreater | 1 | 1 | 1.0 | 0x408a0 |
| 3 | Sniper | 2 | 1 | 0 | 0x408b0 |
| 4 | Stealth | 1 | 1 | 0 | 0x40960 |
| 5 | Attacker | 6 | 1 | 0 | 0x408a0 |
| 6 | RunToPoint | 7 | 1 | 1.0 | 0x40980 |
| 7 | Assassin | 3 | 1 | 0 | 0x408a0 |
| 8 | HostageKiller | 4 | 1 | 0 | 0x409b0 |
| 9, 10 | Hostage, HostageTied | 5 | 3 | 0 | 0x40a80 |
| 11 | JustStand4Demo | 8 | 3 | 0 | 0x40ae0 |
| 12 | DeleteMe | 21 | 1 | 0 | empty stub 0xe0ec0 |
| 13 | Civilian | 9 | 3 | 0 | 0x40b00 |
| 14 | CivilianScared | 10 | 3 | 1.0 | 0x40c10 |
| 15 | MissionFailer | 11 | 1 | 0 | 0x40ce0 |
| 16 | Mayhew | 12 | 2 | 0 | 0x40d10 (shared with 26) |
| 17 | Ninja | 13 | 1 | 0 | empty stub |
| 18 | AlarmRaiser | 14 | 1 | 1.0 | 0x40d30 |
| 19 | SearchLight | 15 | 1 | 0 | 0x40d60 |
| 20 | Ambush | 16 | 1 | 0 | 0x40d80 |
| 21 | Zoe | 17 | 2 | 0 | 0x40da0 |
| 22 | PartyGirl | 18 | 3 | 0 | 0x40dd0 |
| 23 | CivilianGuard | 19 | 3 | 0 | 0x40e80 |
| 24 | Interogator | 20 | 1 | 0 | 0x408a0 |
| 25 | CivDoorGuard | 22 | 3 | 0 | 0x40f50 |
| 26 | TruckDriver | 23 | 3 | 0 | 0x40d10 |
| 27, 28 | CastleChatGuard1/2 | 24 | 1 | 0 | 0x41030 / 0x41050 |
| 29 | SniperAlert | 25 | 1 | 1.0 | 0x41080 |
| 30 | PartyGirlLooker | 18 | 3 | 0 | 0x41120 |
| 31 | (none) | 2 | 1 | 0 | null |
| 32 | (none) | 26 | 3 | 0 | null |
| 33 | Bot | 30 | 2 | 0 | empty stub (PS2 has `init_DMODE_Bot`) |
| 34 | (none) | 8 | 3 | 0 | null |
| 35 | terminator | 0 | 0 | 0 | null |

The Ghidra names `NDrone2_init_DMODE_Defaults` on 0x408a0 and `NDrone2_init_DMODE_HostageTied` on 0x40a80 are
the folded functions, not unique ones.

### 3.2 `DroneTypeSettings` 0x1778a0 (DTYPEs)

85 entries x 12 bytes: `{s16 initState, s16 alertState, void (*init)(Drone_tag*), void (*control)(DCVars*)}`
(verified: `FUN_0003fe30` reads +0/+2/+4, `Drone_Control` 0x31530 calls +8 per frame; PS2 symbol, 1020 bytes).
`initState < 0` means "choose in code" (4 Idle or 6 InitPatrol from props 0x24/0x25, 0x29/0x2a for snipers, fixed
states for DTYPEs 4, 7, 0x10, 0x13, 0x16, 0x18). DTYPE names from the mode table:

| dtype | name | init state | alert state | hooks |
|---|---|---|---|---|
| 0 | Normal | computed | 0 | |
| 1 | Guard | 6 InitPatrol | 0 | |
| 2 | Sniper | 41 SniperIdle | 42 SniperAim | init 0x411f0 |
| 3 | Assassin | computed | 0 | |
| 4 | HostageKiller | 8 | 0 | |
| 5 | Hostage | 10 | 10 | |
| 6 | Attacker | 86 Attack | 0 | |
| 7 | RunToPoint | 133 EnemyRunToPoint | 133 | |
| 8 | JustStand | 183 Tester1 | 183 | |
| 9 | Civilian | 16 CivilianInit | 18 CivilianScared | |
| 10 | CivilianScared | 18 | 18 | |
| 11 | MissionFailer | computed | 181 FailMission | |
| 12 | Mayhew (ally lead) | 28 AllyLeadInit | 28 | |
| 13 | Ninja | 166 NinjaStand | 167 NinjaAttack | control 0x3f910 (PS2 `ControlDTYPE_Ninja`) |
| 14 | AlarmRaiser | 137 RunToAlarm | 137 | |
| 15 | SearchLight | 0 | 0 | |
| 16 | Ambush | 55 AmbushInit | 86 Attack | |
| 17 | Zoe | 4 Idle | 0 | control 0x3f8a0 (PS2 `ControlDTYPE_Zoe`) |
| 18 | PartyGirl | 46 | 0 | |
| 19 | CivilianGuard | 48 | 102 DrawWeapon | |
| 20 | Interogator | computed | 0 | |
| 21 | DeleteMe | 180 | 180 | |
| 22 | CivDoorGuard | 49 | 0 | |
| 23 | TruckDriver | 50 | 18 | |
| 24 | CastleChatGuard | 54 | 0 | |
| 25 | SniperAlert | 42 SniperAim | 0 | init 0x41200 `NDrone2_initDTYPE_SniperAlert` (also `DroneTypeSettings_AttackMode` 0x1779d0 used by `ChangeToAttackMode`) |
| 26 | (unnamed) | computed | 0 | |
| 27, 28 | Abseil (set from death-script hashes 0x6000892/0x600089c) | 142 AbseilInit | 0 | |
| 29 | Astronaut | 189 AstronautLaunch | 0 | control 0x3f930 (PS2 `ControlDTYPE_Astronaut`) |
| 30 | Bot | 195 BotInit | 0 | init = empty stub (part C) |
| 31..84 | (placeholders) | 196..249 (one per `DSTATE_Bot*` state) | 0 | none - apparently generated, never selected by the SP code I read |

State numbers are the `DSTATE` enum in `src/action/game/drone/NDrone2.h`; I checked the ones above against
`NDrone2_StateFuncs` (they match).

### 3.3 How a drone gets its DTYPE (`FUN_0003fe30` = PS2 `NDrone2_GetDroneTypeAttackTypeFriend`)

Verified from the disassembly where it matters. `FUN_0003fae0` (PS2 `NDrone2_GetDTYPENEW`) takes the class on the
stack, the **attack mode in ESI** and the **drone in EDI**:

- prop 0x10 set and mode 0 -> 0x10 (Ambush);
- class 0..9 (enemies): Castle Indoors 1 -> 0x13 or 0x16 (prop 0x4b), else by mode;
- class 10/11: civilian 9 (or 0x12 PartyGirl if drone+0x15) / 10 when alertness >= 1 (Castle: props 0x1f off, 0x54 on);
- class 12: 0xc (Mayhew) or 0x11 (Zoe, when `0xbc == 0xc`);
- class 13: 0x17 TruckDriver if `0xbc == 0xf`, else 9/10; class 14: 5 if prop 0x4e, else 9/10;
  class 15: 0xd Ninja; class 16: 0x1e Bot;
- then by mode: 0 -> 0, 2 -> 2 Sniper (0x19 if alertness >= 1), 3 -> 7 RunToPoint (0xe AlarmRaiser if prop 0x56),
  4 -> 4 HostageKiller, 5 -> 0x15 DeleteMe.

Results: drone+0xa8 = DTYPE for mode 0 (computed inline), +0xa9 = DTYPE for mode 1 (`ESI = +0xac`, 0x3ff9c),
+0xaa = DTYPE for mode 2 (`ESI = +0xad`, 0x3ffec) unless there is no secondary. drone+0x44 = side from the class
(10,11,13,14 -> 3; 12 -> 2; else 1). Then +0x49e/+0x4a0 = init/alert state from `DroneTypeSettings`, and alerted
drones (+0x408 >= 1) have their init state promoted (4,5,6,0x30,0x31,0x36 -> 0x56 Attack; 0x10,0x12,... -> 0x12;
0x29 -> 0x2a; 0xa6 -> 0xa7).

---

## 4. Senses: how a drone becomes alert

### 4.1 Alertness and cause bits

- drone+0x408 `cumulativeAlertnessScaled` 0..1: 0 calm, 0.66 suspicious, 0.75 "noticed", 1.0 alerted. Initial
  value from prop 0x20 (or the mode table).
- drone+0x3f8 **alert-cause bits** (verified by finding each writer): 1 saw the opponent (`FUN_0003ea80` with prop
  0x33), 2 was hurt (`SendHurtMessage`, `DSTATE_Taser`, `ExplosiveImpact`), 4 surrendered (`DSTATE_Surrender_Anim`),
  8 saw a dead body (`DSTATE_Dead` with prop 0x31, not for head-shots), 0x10 attack message
  (`NDrone2_SendAttackMessage`), 0x20 interrogation assist (`FUN_000644c0`, `SetInterogateAssist`). The high 16
  bits hold causes copied from the drone that alerted this one.
- drone+0x3f4 status flags (A's layout; ones used here: 0x100 active, 0x200/0x400 dying/disabled, 0x800 attacking
  from cover, 0x2000 contagion enabled, 0x10000 vision enabled, 0x20000 already alerted, 0x200000 already raised
  alarm, 0x400000 deaf, 0x800000 ignores alerts, 0x4000000 in attack mode).

### 4.2 Sight (`DroneVision_ProcessDroneSight` 0x661f0, called from `Drone_InitComms`)

Runs when there are drones and at least 15 frames have passed since two NPCGlobals timestamps (+0x1a8, +0x1ac).

1. `DroneVision_FindAlertedDrones` 0x65d60: pairwise "contagion" over the drone list, round-robin from where it
   stopped last frame (NPCGlobals +0x238/+0x23c), at most 5 positive checks per frame; skipped entirely on
   Henderson D and Space Station D. For each pair `DroneVision_DetermineAlertCheck` calls
   `DroneVision_ConsiderAlerted` 0x65950 on the calm one: requires +0x3f4 & 0x2000, not a civilian ally (side 2),
   not DTYPE 5, at least 60 s x `FRAME_RATE_DIV` since the other was alerted, and a cause bit on the other drone
   that one of this drone's properties accepts (8 -> 0x26/0x29, 2 -> 0x27, 4 -> 0x4f, 1 -> 0x28, 0x10 always).
   Then `DroneVision_DroneCanSeeDrone` 0x658e0 (within 50 m, visibility >= 0.7, head-to-head line of sight) and
   the drone is alerted: +0x3f4 |= 0x20000, causes copied up, `Drone_Message(obj, 0x1f)` -> `DroneFunc_DoForcedAttack`
   in the current state. (Power Station drones of class 5/6 just shout sfx 0xd0 instead of checking.)
2. Per drone: `Drone_VisibilityForPosition` (lighting/shadow volumes) into drone+0x428 (decompile), aim point
   update, and `FUN_000656a0` = sight test: `FUN_00065510` computes a **sight strength** into +0x1d0:
   - within 2 m and range > 0 and visible: 1.0;
   - else `s = 0.7 - (dist - R)/R` with `R = (2*alertness + 1) * range(+0xcc)`, times
     `sin(((fov + 22.5 deg) - |angle|)/fov * 90 deg)` when a half-FOV (+0xc8, default pi/2) is set (0 outside the
     cone), times the target's visibility (+0x1cc) unless the drone is alerted with causes, clamped at 0.
   - `DroneVision_ObjectVisibility` 0x643c0 is the simpler boolean cone used for drone-sees-drone: within 2 m ->
     1; else in the +0xc8 cone and within min(50, +0xcc).
   A strength >= 0.7 makes the drone a candidate.
3. One line-of-sight raycast budget per frame, round robin (`FUN_000645c0`, NPCGlobals +0x234; +0x232 counts
   raycasts): `FUN_00066110` sets +0x1c8 bit 2 (can see) from `FUN_00066030`/`DroneVision_CanSeeObjectFrom`
   (head/bone positions, `AINetwork_TestRayCels` for other cels, `Collide_LineOfSight` mask 0xa27).
4. For each drone with LOS: `DroneVision_HaveOpponentSight` 0x65730: after `DroneFunc_ReactionTime` frames of
   continuous sight (+0x200 counts them; `(1 - alertness) * 30 * (200*FRAME_RATE_DIV - reaction)/100`, only on
   Henderson B/C, Castle Exterior, Tower A/B - elsewhere a flat 10 x `FRAME_RATE_DIV`) sets +0x1c8 |= 4, logs the
   sighting, stores last-seen position/rotation, and on the first sighting with prop 0x3c sends message 0xf to
   itself (plus a delayed 0xa and music event 0xf for DTYPE 4; prop 0x55 -> mission fail reason 10). Squad chatter
   (`NewSightTalk`/`LostSightTalk`) is rate limited through NPCGlobals.

States then call `DroneVision_EnemyLookForOpponent` 0x65360 each update: returns 0x56 (Attack) when the drone has
seen its opponent (+0x1c8 & 4) and prop 0x3c, unless it is the player and the drone is below 0.66 without prop
0x51, or it is fleeing (DTYPE 7/0xe in state 0x85/99 without prop 0x57).

### 4.3 First reaction (`FUN_0003ea80`, from `DroneFunc_FirstAttack` 0x3ec20 in `DSTATE_Attack`/`CivilianScared`)

Verified from the decompile: `NDrone2_CheckSurrender` -> 0x3f Surrender_Anim; prop 0x33 -> broadcast 0x11 to
drones within 20 m (`NDrone2_DroneAlertToObject`) once; prop 0x1a on Castle Indoors 1 / RefRoom -> 0x3c Interogate,
on Tower A/B -> 0x3e CivilianChallenge; seen-player talk; prop 0x1b -> 50/50 stay or combat move; prop 0x18 ->
stay (0); prop 0x19 -> `NDrone2_ChooseCombatMove`; prop 0x4a -> 0x2d GrenadeThrow.
`NDrone2_ReactToOpponentSighted` (0x64910/0x6491d) is the bot version with the same properties (part C).

`NDrone2_CheckSurrender` 0x38bd0: prop 0x43, alertness < 0.66, within 2 m, the opponent's facing angle
(+0x188) > 120 deg away (i.e. the player is behind or beside it - inferred), visible, the opponent armed and facing
the drone within 45 deg. `CheckUnsurrender` 0x38ca0: gets up when the opponent is > 12 m, or > 2 m and unarmed, or
more than 90 deg off, or out of sight.

### 4.4 Hearing (`DroneFunc_HandleSoundAlerts` 0x3b410, from `Drone_InitComms`)

Prop 0x1f, not `switch_BLIND_DRONES`, not deaf (+0x3f4 & 0x400000); on Castle Indoors 1 only armed drones hear.
`Sound_Alertness` 0xcbb10 sums over the active dynamic sounds (skipping stopped/looping ones and those with zero
`alertnessRelated`): `alertness * (d <= inner ? 1 : (outer - d)/(outer - inner))` for sounds within the outer
radius; sfx 0x11c/0x11d are ignored on Tower A/B; sfx 0x248 is forced to full radius and 10. The loudest source is
returned. The drone adds `0.01 x sum` to its alertness (+0x40c raw, +0x410 scaled, +0x408 cumulative) and, if the
source is in a cel and it has been > 60 x `FRAME_RATE_DIV` frames or the sound is louder than the last, builds an
AI target at the source (+0x7b4..) and sends itself message 0x14. `Sound_SetAlertness` (called by
`AnimObjectUpdate` for script sounds and `Effect_Player`) and `Sound_ModAlertness` (`Effect_Body`) set the per-sound
value; `Sound_ZeroAlertness` clears them each frame (already ours).

Message 0x14 in a state goes to `DroneVision_EnemyAlerts` -> `DroneVision_AlertSound` 0x64f20: within 50 m (or any
distance for Power Station class 5/6), civilian-type DTYPEs (9, 0x12, 0x13, 0x16) go straight to Attack above 0.7;
others return 0xa2 (`DSTATE_HeardNoise`) when scaled sound alertness > 0.2 or alertness > 0.66. `NDrone2_SetSoundAlertRoute`
0x43a40 copies the sound target to the goal (+0x77c) and sets up a route; the `HeardNoise*` states (part A) use it.

### 4.5 Group alerts (messages 0x11-0x16, 0x1e)

`NDrone2_DroneAlertToObject` 0x38f40 / `DroneAlertToPosition` 0x38ea0 fill an alert record in NPCGlobals
(+0x240..+0x280: source object, sender, frame, message, position, cel, rotation, velocity, three floats) and
broadcast the message with `Drone_SM_BroadcastMsg(msg, 0x1e5870, 0x1e, sender's state machine)`. Senders: 0x11
sighting (prop 0x33), 0x12 hurt (prop 0x32), 0x13 surrender, 0x15 dead body, 0x16 / 0x1e forced attack.

In single player the state handlers pass these to `DroneVision_EnemyAlerts` 0x65220, which records the return
position (+0x49c state, +0x4a4 feet, +0x4b0 cel) and calls a per-message helper - **but the 0x11, 0x12, 0x13 and
0x15 helpers (0x65120, 0x65160, 0x651a0, 0x651e0) always return 0** (verified in the disassembly: they test the
property and then `XOR AX,AX`). Only 0x14 (sound) and 0x1e (-> 0x56 Attack, or the bot reaction) do anything
there. The full group-alert reaction (`FUN_00038ff0`: same group, shouting range, props 0x28/0x27/0x4f/0x29 ->
Attack / SeenDroneShot / SeenSurrenderedDrone / SeenDeadBody) is called only from `DSTATE_BotGlobal` (part C).
So single-player alerts spread by **seeing** an alerted drone (4.2 step 1) and by sound, not by the broadcast.
(Inference from the call graph; worth confirming in play.)

`DroneFunc_CheckAlarmRaised` 0x3a100 is already ours.

---

## 5. Combat

### 5.1 Target selection (`NDrone2_FindOpponent` 0x37c10)

Single player (decompile): enemies target the player (`glb_players[0]`, or the override in NPCGlobals +0x1a4 when
+0x1a0 is set), clearing it while the player is dead. Allies (side 2) first look for a hostile copter in a narrow
cone, then round-robin through the drone list for the nearest visible enemy drone (side 1, alive, active), one LOS
test per call. The multiplayer branch (scores 10 players by distance, team, threat) is part C.
`NDrone2_SetOpponent` 0x37a10 stores it at +0x148, computes the aim point (+0x19c: position or the last-seen
position when prop 0x17 and sight was lost > 5 x `FRAME_RATE_DIV`, plus a bone offset from `FUN_000378f0` using the
bone table 0x178088 {2,3,5,0x31}), and allocates a target slot (+0x14c, 0..7, 0xff none) shared with cover
visibility (5.3).

### 5.2 Firing (`DroneWeap_HandleFiring` 0x67e10 from `NDrone2_ControlSTANDARD`)

- `DroneWeap_DoOpponentTargetting` 0x67190 tracks whether the player is moving (+0x190) and "first moved" /
  "first stopped" windows (+0x191..+0x194) against `DroneFiring_TargetFirstMoved_TimeToHit` /
  `TargetFirstStopped_TimeToHit`.
- `DroneWeap_DoFiring` 0x67d80: new burst length when +0x844 is 0 (`DroneWeap_NextBulletTime`); stops if sight lost
  > 15 frames unless prop 0x17; waits for +0x848; `DroneWeap_Ready2Fire` 0x66920 (opponent alive, anim slot mode
  != 4, weapon datum aimed within 20 deg of the aim point - 30 deg for Ninjas); then `DroneWeap_FireWeapon`.
- `DroneWeap_BurstDelay` 0x672d0 (verified in the disassembly): `Normal x aggressionScale x rand(0.5..1)`, clamped
  to [`BurstDelay_Min`, `BurstDelay_Max`]; if the target is closer than `MinDist` -> `Min`, farther than `MaxDist`
  -> `Max`; times `FRAME_RATE_DIV`. **Ghidra's labels at 0x164094 and 0x16409c are wrong** (they say MinDist/
  MaxDist; the code and `ReadTuningVars` show Min/Max; 0x1640a0/0x1640a4 are MinDist/MaxDist).
- `DroneWeap_DoBulletAccuracy` 0x66f90: hit chance `(100 - 5*acc)` x TooClose/TargetFirstMoved/TargetMoving/
  TargetFirstStopped (the last only on levels 0x700000b..0x700000d with anim set 0x13) x difficulty accuracy
  (1 Easy, 2 Normal, 3-4 Hard) x TooClose again. A hit needs `rand(100) < chance` and, beyond TooClose distance, at
  least `NewSighting_TimeToHit` seconds of sight and no "first shot" flag (+0x41). A hit zeroes the aim error
  (+0x1a8); a miss uses a slowly rotating Lissajous offset (0.5, 1.5, 1.5 m).
- `DroneWeap_FireWeapon` 0x677f0: muzzle light, shell/effect object, `Bullet_init` per `numBulletsPerShot` of
  `weapon_data[currentWeaponId]` from the weapon datum towards aim point + error, casing, ammo (+0x840), 3D sound.
- Damage to the player: `Drone_ModPlayerHitDamage` 0x31eb0 -> `Drone_ModBulletDamage` 0x31de0: x `TooClose_Damage`
  within TooClose distance, x drone+0xe4 (2.0 for some weapons, captain modifier), x `PlayerBackShot_Damage` when
  hitting from behind with anim set 0x12 (`Drone_MayDealBackshotDamage`). Class 0xc drones do no damage.

### 5.3 Cover

Placements (already mapped by the level work): `Drone_CoverCornerNode` 0x2fe20 (type 0) and `Drone_CoverLowNode`
0x30000 (type 1) append a 0x78-byte `CoverNode_tag` to the array at NPCGlobals +0x288 (count +0x28c?), and
`Drone_AIPoint` 0x30190 a 0x5c-byte point (array NPCGlobals +0x1380, count +0x137c). **The Xbox build has no bound
checks** - the GC build warned at > 0xff (docs/gamecube-checks.md rows 69-71) and wrote anyway; on Xbox the
arrays are allocated elsewhere (part C / `Drone_PostLoad_Init`). CoverNode layout (verified from the writers):
`+0 type, +1 enable ch, +2 disable ch, +3/+4 low-cover anim states, +5/+6 corner k6/k7 (default 8), +8 cover
angle (rad, default 45 deg), +0xc flags (corner: 4/8 side usage, 0x20/0x40 stand, 0x80/0x100 crouch; low: 1, 2),
+0x10 cel, +0x14 owner, +0x18 position, +0x24 rotation, +0x30 AI emitter, +0x58 u32[8] per-target visibility flags`.

- `Drone_ProcessCoverNodes` 0x311e0 (every 5 frames) and `Drone_IsCoverNodeUsable` 0x30af0 keep, for each of 8
  target slots (NPCGlobals +0x290, 0x18 bytes each: flags, object, position), a flags word per node: switch
  channels allow it, >= 4 m from the target, the angle to the target within the node's arc (bits 2/4/0x10/0x20
  sides, 0x48 low cover, 0x200 exposed, 0x400 target can see the node, 0x80 too close).
- `NDrone2_CoverAvailable` 0x46a10 (prop 0x30) -> `NDrone2_FindCover` 0x46550: nearest node on the drone's own AI
  path by emitter distance (bytes; 0xff unreachable), within combat range, usable for its target
  (`FUN_00044310`: prop 0x44 caps distance at combatRange x 1.5; nobody else within 1.5 m), then claims it
  (+0x838), picks the side and sets the goal.
- `DroneAnim_CoverAnim` 0x34750 drives the cover animation (5.5 / 6.4).

### 5.4 Combat moves (`NDrone2_ChooseCombatMove` 0x45e50)

Returns a DSTATE or 0. Nothing when attacking from cover (+0x3f4 & 0x10) or prop 0x40. Builds a candidate list
from `DroneWeap_OpponentIsAimingAtMe` and the property/anim checks (strafe-dodge 0x13/anim 0x29, strafe 0x41, roll
0x2e, step 0x42, dodge 0x11/anim 5 at >= 6 m, plain stand), picks one at random and resolves it to a state
(0x67 AimStand 3/4 of the time as the fallback, 0x6f AimCrouch...). The `NDrone2_Can*` helpers test the property,
`DroneAnim_CanDoAnimState` and a short collision probe (`FUN_000453e0`).

### 5.5 Grenades and melee

- Grenades: `DSTATE_GrenadeThrow` plays an anim whose script event 1/9 calls `DroneWeap_ThrowGrenade` 0x66cd0:
  weapon from +0x850 (count +0x852 decremented; drones passing a byte test that is probably the Ninja DTYPE
  (value 0xd) throw 0x35, or 0x36 one time in 3 - decompile only), from bone datum 0x15, towards a target position
  kept in the drone (offset not pinned down), pitched +45 deg under 10 m, -45 deg over 18 m. Grenade kind/count come from
  placement key 11 (hi nibble kind: 1 -> 0x36, 2 -> 0x35, 4 -> 0x34, 5 -> 0x3a+n, 6 -> 0x6c).
- Melee: script events 5/10/0xb -> `DroneWeap_CloseCombatImpact` 0x66500: within 1.75 m and 60 deg (2 m for DTYPE 7
  unarmed), fires a pseudo-bullet `weapon_data[99..102]`.
- Being punched: message 6 -> `DSTATE_PunchImpact` (or `NDrone2_PunchImpact` 0x3e010 in place): alertness 1, hit
  effects, hurt broadcast, knock-out at health <= 0 (`DSTATE_KnockedOut_Anim`, or +0xb0 = `BotImpactPunch`), mission
  fail labels for unarmed civilians (Castle Indoors 1 "cover lost", elsewhere "killed civilian", except Tower A/B).
  **The decompile of `NDrone2_PunchImpact` has a wrong signature** (it shows a `DCVars_tag` by value); its caller
  in `DroneFunc_HandleImpact` passes registers too - check the disassembly before porting.

### 5.6 Damage, impacts, knock-outs

`DroneFunc_HandleImpact` 0x3e6a0 dispatches impact messages (verified list): 6 punch, 7 taser (a counter +0xa0
grows by `FRAME_RATE_MUL`; > 2 x health -> `DSTATE_Taser`), 8 bullet (`NDrone2_BulletImpact`), 9 explosion, 0x17
smoke (unless props 0x36/0x34) -> `SmokedOut`, 0x18 stun grenade (prop 0x1c resists: flinch anims 0x57/0x11/0x19 for
anim set 5), 0x19 stun dart. `NDrone2_HitDamage` 0x3d670 scales damage by body part (`hitLocation` 5 head x
`DroneDamage_Head` - except Ninjas, the astronaut skin and Evil Base class 0x10 which take 2 x torso; arms
0x14,0x15,0x17,0x20,0x23,0x27; legs 0x31..0x38; else torso) and difficulty, applies armour flags +0x9f (1 vest,
2 jacket, 4 helmet, 8 combat) - **but the only writer of +0x9f found is `DefaultInit` storing 0**, so drone armour is
inert on Xbox (`Drone_armour_values` 0x1640e8 is unreferenced). Only drones with +0x14 set lose health.
`NDrone2_BulletImpact` 0x3e220: alertness 1, hurt broadcast, then alive -> location impact anim (if allowed) or the
state's impact state; dead -> `Death_Anim` (Abseil/Astronaut/Bot variants). `DroneFunc_RecoverTime` 0x3a4d0:
`(200 - recovery) x base / 100`, base 60 s, or per stun weapon 0x35 30 s, 0x43 60 s, 0x44 120 s, 0x4a/0x4c 10 s.

---

## 6. Animation

The generic engine (scripts, blending, events) is in docs/anim on the `blender-exports` branch
(`spec-script.md`). This is the drone layer on top of it.

### 6.1 Tables (XBE, static)

**`DroneAnimStates` 0x1664c0**, 119 x 12 bytes (Ghidra type exists but has no fields). Verified layout:

| off | type | meaning | used by |
|---|---|---|---|
| +0 | u16 | first anim row (variants are consecutive rows) | `CanDoAnimState`, cover checks |
| +2 | u8 | call kind -> drone+0x45a | `CallAnim` |
| +3 | u8 | priority -> drone+0x45b and the slot | `CallAnim`, `SetDAnimVeryInternal` |
| +4, +5 | u8 | not read by the functions I traced | |
| +8 | ptr | transition list | `FUN_00033040` |

Transition list entries, 6 bytes, terminated by 0xff (verified in the disassembly of 0x33040):
`{u8 toState (0 = any), u8 variantCount, s16 row (0 = nothing to play, -1 = not allowed), u8 blendFrames, u8 flag}`.
The list belongs to the **current** anim state; state 0's list is the generic fallback ("from anywhere to state
X play its base row with a 30-frame blend"). Lists live at 0x164120..0x1664bf.

**`Drone_AnimInfo` 0x166a58**, 436 rows x 24 bytes (decoded from `DroneAnim_SetDAnimVeryInternal` 0x32f30,
which copies a row into the drone's current-anim slot at +0x464):

| off | type | slot field | meaning |
|---|---|---|---|
| +0 | u32 | +0x46c | flags for `DroneAnim_SetHTAnim` (bit 0 loop; 0x100 rotate from root on blend) |
| +4 | u32 | +0x488 | mostly 0 (float-looking values in 23 rows) |
| +8 | s16 | +0x466 | the anim state this row belongs to |
| +0xa | u16 | +0x490 | follow-on row played when this one ends (0 if it is itself) |
| +0xc | u8 | +0x464 | completion mode (1 loop-until-called, 2/9 complete at end, 5 interruptible, 6 caps blend...) |
| +0xe | u8 | +0x480 | blend frames (8 in 407 rows) |
| +0xf | u8 | +0x482 | ? |
| +0x10 | u8 | +0x481 | move state (PS2 `DroneAnim_CurrentMoveState` reads it) |
| +0x14 | float | +0x48c | playback speed (1.0, 1.1, 1.5) |

**`Drone_AnimTables` 0x169338**, 436 rows x 27 columns x `{u16 scriptLo, u16 startFrame}`: the script is
`0x06000000 | scriptLo`, `startFrame` goes to +0x46a (999 = last frame). Column = drone+0xbe, the **anim set**;
column 0 is the fallback when a set has no entry (`CanDoAnimState`). Anim sets seen being assigned (verified in
`NDrone2_DefaultInit`, `DroneWeap_ChangeWeapon`, `DoModeSettingsOLD`): 1 hostage, 2 women (skins 0xe-0x11, 0x52),
3, 4/8 pistols (8 for class 0xc), 5 rifles, 6 SMGs, 7, 9 snipers, 0xa, 0xb, 0xc default unarmed, 0xd, 0xe
civilians, 0xf script-driven, 0x10, 0x12, 0x13. Row x column usage is in `anim_tables.csv`.

Smaller tables: cover anims 0x164030..0x164054 (10 pointers, index `side(1,2) + action*2`, to six-entry u16 lists
of anim states per cover phase: e.g. side 1 lean `{0x10,0x10,0x13,0x58,0x10,0x1a}`) and 0x164064 (low cover
`{0x11,0x11,0x0a,0x58,0x11,0x0a}`), read by `FUN_00033820`; `DroneData_AnimFunc` 0x1640fc (row 0xdc ->
`FUN_00035ec0`) and `DroneData_DAStateFunc` 0x16410c (state 0xa -> empty stub), both `{u16 key, fn}` lists ended by
0xffff (PS2 symbols); the low-cover anim choices `{0x11,5,10,7,0x1d}` are a stack array in `Drone_CoverLowNode`.

### 6.2 The anim-call protocol

States never play scripts directly; they *request* an anim state:

- `DroneAnim_CallAnim(state, variant, endDState, endMsg, speed, ?, dcv)` 0x341c0 records the request (+0x454
  state, +0x456 pending, +0x458 current state, +0x45a kind, +0x45b priority, +0x45c variant, +0x460 frame; +0x484
  speed, +0x492/+0x494 what to do when it finishes). Unavailable states are remapped (many -> 10 or 0x21 armed/
  unarmed stand, 0x24 last resort). Asking for the state already playing just updates the end action.
- `DroneAnim_CallHandler` 0x35cb0, once per frame from `Drone_Control`: when the anim finished (slot mode 1/2/9),
  `DroneAnim_CallFullyComplete` -> `DroneAnim_SetEndAIState` sets `endDState` and/or sends `endMsg` to itself (so
  "play this then go to state X" is data in the call). Otherwise, if `DroneAnim_CanSetAnimCall` allows (not mid
  one-shot, priorities), `FUN_00033de0` (PS2 `GetDAnimForCall`) finds the row through the current state's
  transition list, falling back to state 0's, and `DroneAnim_SetDAnimInternal` 0x34550 plays it.
- `SetDAnimInternal` fills the slot from the tables (above), sets the row's per-frame hooks from the two DroneData
  lists (+0x440/+0x444), and `DroneAnim_SetHTAnim` 0x33f20 starts the script: blend from the previous one
  (`AnimScriptAppendBlend`, the old one kept at +0x434, blend length from the call), speed, start frame, the event
  callback `DroneAnim_EventFunc`, loop flag 0x80000000 or a stop callback `DroneAnim_AnimEndCallback`.
- `DroneAnim_SetDAnimNow` 0x35dc0 / `DroneAnim_SetAnimD` 0x35e10 play a row immediately (death, special);
  `DroneAnim_SetScript` 0x34680 plays an arbitrary script hash as the current anim (used by `PlayScript`,
  `ActionAnim`, `SpecialDeath_Anim`, `CastleChatGuard1`, `TruckDriverInit`). The GC build checked that the script
  slot was set afterwards ("Couldn't set script", gamecube-checks row 89).
- Script events (`DroneAnim_EventFunc` 0x33590, verified list): 1/9 throw grenade, 2 change weapon to +0x45,
  3/4 swap the weapon datum between hand (0x28) and holster (100) with entity 0x2000312, 5/10 melee 1, 0xb melee 2,
  6 message 0x20 to itself, 7 clear +0x475, 0xe message 0xb to the partner, 0xf/0x10/0x12 set/clear +0x20/+0x21
  (the "can fire" flags used by `DroneAnim_PlayFiringAnim`). Only while alive and only from the current script
  (+0x42c) for the combat events. GC rows 91/92 ("Drone has no second weapon") were checks on event 2.

### 6.3 Other anim helpers

`DroneAnim_CanDoAnimState` 0x330c0 (state has a row for this anim set or column 0), `DroneAnim_SetStandIdleAnim`
0x35310 (idle 0x24, fidgets 0x25-0x27 when in `Idle`/`Patrol` 0x2f with prop 0x47, 5-10 s apart),
`DroneAnim_LocationDeathAnim`/`LocationImpactAnim` (body-part reactions, +0xfc return state),
`DroneAnim_PlayFiringAnim` 0x35e60 (anim row 0x5c + fire for "fire from script" weapons), `DroneAnim_SnapRotate`,
root-motion callback 0x33320 (set in `DefaultInit`).

### 6.4 Cover animation (`DroneAnim_CoverAnim(dcv, action, endState)` 0x34750)

Action 0/6 enter/reset (6 also picks a random pose among lean/step-out, stand or crouch, via the prop checks
`DroneAnim_CanStandLean`, `CanStandStepOut`, `FUN_000338b0` = PS2 `CanCrouchLean`, `FUN_00033990` =
`CanCrouchStepOut`, `FUN_00033870` = `ForceCrouchCover`), 1-4 phase changes, 3 fire (0x58), 5 leave (set flag 8 and
go to `endState`), 7 change pose at random (only 1 in 2 calls). The pose (+0x83f) and phase (+0x83e) index the
cover anim table (6.1). Low cover uses `DroneAnim_ValidateCoverAnim` (falls back to 7 or 10).

---

## 7. Drone fields used by this area (offsets; part A owns the layout)

`+0x14` takes damage, `+0x15` female/civilian anims, `+0x19` captain, `+0x1b` ignores punches/impacts, `+0x20/+0x21`
fire flags from script events, `+0x29` using behaviour 2, `+0x31/+0x32` new/old format, `+0x3a` head-shot,
`+0x3b..+0x41` firing state, `+0x44` side, `+0x45` second weapon, `+0x90` health, `+0x94` max health, `+0x98..+0x9e`
stats, `+0x9f` armour (always 0), `+0xa0` taser counter, `+0xa4` accuracy float, `+0xa8/+0xa9/+0xaa` DTYPEs,
`+0xab` class, `+0xac/+0xad` attack modes, `+0xbc` character class (from the skin), `+0xbe` anim set, `+0xc0`
saved anim set, `+0xc8` half FOV, `+0xcc` sight range, `+0xd8..` combat ranges (placement key 14), `+0xe4` damage
multiplier, `+0xe8` idle timeout, `+0xf0` current DSTATE (Ghidra types it as `cel_tag*`), `+0xfc` return state,
`+0x130` hit effect scale, `+0x134` last damage, `+0x148` opponent, `+0x14c` target slot, `+0x168` distance to
target, `+0x17c/+0x188` angles, `+0x18e` aim bone, `+0x190..+0x194` target motion flags, `+0x19c` aim point,
`+0x1a8` aim error, `+0x1b4` aim offset, `+0x1c8` sight flags (2 LOS, 4 seen, 8 first sighting handled, 0x20
reacted, 0x40 previous LOS), `+0x1cc/+0x1d0` visibility/sight strength, `+0x1d4/+0x1d8` first/last seen frame,
`+0x1e8/+0x1f4` last seen position/rotation, `+0x200` frames seen, `+0x204` frames since seen, `+0x2cc` head yaw,
`+0x3d4` behaviour pointer, `+0x3d8/+0x3e4` behaviours, `+0x3f4` status, `+0x3f8` alert causes, `+0x3fc/+0x400`
initial alertness, `+0x408` alertness, `+0x40c/+0x410` sound alertness, `+0x414` sound source, `+0x424` alerting
drone, `+0x42c/+0x434` current/previous script, `+0x43c` last impact frame, `+0x440/+0x444` row hooks, `+0x448`
root yaw, `+0x450` playScript hash, `+0x454..+0x460` anim call, `+0x464..+0x494` current anim slot, `+0x498` idle
timer, `+0x49e/+0x4a0` init/alert state, `+0x7b4..+0x7e8` heard-sound target, `+0x838` cover node, `+0x83c..+0x83f`
cover side/flags/phase/pose, `+0x840/+0x842` ammo/clip, `+0x844` burst, `+0x848/+0x84c` next/last shot frame,
`+0x850/+0x852` grenade kind/count, `+0x8a8` placement mode (100).

---

## 8. Function inventory

`function_inventory.csv` lists 233 functions (named ones matched by pattern, plus unnamed ones I identified, with
PS2 names where the match is solid). Already ours: `DroneFunc_CheckAlarmRaised`, `Sound_SetAlertness`,
`Sound_ModAlertness`, `Sound_ZeroAlertness`. Everything else in this area is original code. The biggest:
`NDrone2_DefaultInit` 4768 (part A), `NDrone2_FindOpponent` 2656, `DroneAnim_LocationDeathAnim` 1136,
`DroneAnim_CoverAnim` 1088, `DroneFunc_ConsiderExplosive` 1056, `DroneWeap_FireWeapon` 1024,
`DroneFunc_HandleImpact` 992, `DroneVision_ConsiderAlerted` 896.

Custom calling conventions to watch (verified): `FUN_0003fae0` (ESI mode, EDI drone), `FUN_00033040` (AX to-state,
DX from-state, ESI/EDI out pointers), `FUN_00033de0` (EBX drone), `DroneAnim_SetDAnimVeryInternal` (EAX slot,
fastcall), `DroneVision_ConsiderAlerted`/`HaveOpponentSight`/`SightStrength` (ESI drone, EDI other),
`DroneVision_AlertSound` and the 0x651x0 helpers (ESI message, EAX dcv), `DroneWeap_BurstDelay` (ESI drone),
`DroneWeap_NextBulletTime` (EDI dcv plus two stack bytes), `DroneAnim_ValidateCoverAnim` (ESI dcv, DI state),
`DroneAnim_PlayFiringAnim`, `FUN_000344f0` (ESI drone).

---

## 9. Proposed reimplementation order

Follow the repo's pattern: data first, leaves next, each function shadow-tested against the original on the same
state where possible (as `src/action/devtools/UpgradeShadow.cpp` / `AnimShadow.cpp` do), game logic in its own
units (memory: keep game logic separate).

**Step 0 - generated data (a tool, not hand-written).** Write `tools/drone_tables.py` (like `tools/uihandler.py`)
that reads the XBE and emits C tables with static asserts on sizes: the property layout (0x1634e0/0x163598) as an
enum of 91 named properties plus word/shift/mask constants; `DroneModeSettings` and `DroneTypeSettings` with their
function pointers resolved to our symbols or `XBE fn` placeholders; `DroneAnimStates` + the transition lists,
`Drone_AnimInfo`, `Drone_AnimTables` (47 KB - generate, never hand-type), the cover anim lists, the DroneData hook
lists, the aim bone table 0x178088. Keep the originals referenced by address until each consumer is ours, and
have the tool compare the generated bytes with the XBE.

**Step 1 - pure leaves (easy shadow tests: same inputs, compare outputs bit for bit).**
`behaviour_util_getProperty`, `setProperty`, `getStats`, `behaviour_util_get` (compare the whole
`_BehaviourStruct` + drone words + `drone_stats` after each placement load); `Sound_Alertness`;
`DroneFunc_ReactionTime`, `DroneFunc_RecoverTime`, `DroneWeap_BurstDelay` (needs `Float_FRand` seeded identically -
run original and ours with the RNG state saved/restored, the way the upgrade shadow does), `Drone_ModBulletDamage`,
`Drone_MayDealBackshotDamage`, `NDrone2_HitDamage` (returns via +0x134/+0x90: snapshot and compare);
`DroneAnim_CanDoAnimState`, `FUN_00033040`/`FUN_00033de0` (transition lookup), `FUN_00033820`, the `CanStand*/
CanCrouch*` cover checks, `DroneVision_ObjectVisibility`, `FUN_00065510` (sight strength), `NDrone2_CanSeeObject`
(raycast; deterministic given the world), `FUN_0003fae0`/`FUN_0003fe30` (DTYPE selection - snapshot the drone
before/after).

**Step 2 - setup (shadow once per drone at load).** `NDrone2_DoModeSettingsNEW`/`OLD` and the `init_DMODE_*`
hooks, `NDrone2_initDTYPE_*`, the DTYPE/state application (0x40430); compare the whole Drone_tag after
`NDrone2_DefaultInit` (with part A).

**Step 3 - animation layer (shadow per call, compare the slot +0x454..+0x498 and the script list).**
`DroneAnim_SetDAnimVeryInternal`, `SetDAnimInternal`, `SetHTAnim`, `CallAnim`, `CallHandler`, `CanSetAnimCall`,
`CallFullyComplete`, `SetEndAIState`, `SetScript`, `SetDAnimNow`, `SetAnimD`, `EventFunc`, then `CoverAnim`,
`SetStandIdleAnim`, the location impact/death anims. `SetHTAnim` touches the anim engine, which is partly ours
already (docs/anim); the rest is table lookups.

**Step 4 - senses (need replays).** `DroneVision_*`, `DroneFunc_HandleSoundAlerts`, `FUN_0003ea80`,
`NDrone2_CheckSurrender/Unsurrender`, `NDrone2_FindOpponent` (SP branch), `NDrone2_SetOpponent`. These run on
round-robin cursors in NPCGlobals and on raycast budgets, so per-call shadowing is possible (restore NPCGlobals
+0x1a0..+0x280 and the drone before the second run) but the useful test is a **replay**: drive a level with
`drive_game.ps1` along a fixed input script and diff per-frame drone state (DSTATE, +0x408, +0x3f8, +0x1c8, +0x148)
between original and ours. Record `Rand_Rand`/`Float_FRand` call sequences to catch RNG divergence.

**Step 5 - combat.** `DroneWeap_*` (DoOpponentTargetting, DoBulletAccuracy, Ready2Fire, NextBulletTime, DoFiring,
FireWeapon, HandleFiring, ThrowGrenade, CloseCombatImpact, ChangeWeapon), cover selection
(`Drone_IsCoverNodeUsable`, `Drone_ProcessCoverNodes`, `NDrone2_FindCover`, `CoverAvailable`), `ChooseCombatMove`
and the `Can*` moves, impacts (`DroneFunc_HandleImpact`, `BulletImpact`, `PunchImpact` - check its registers,
`ExplosiveImpact`, `ConsiderExplosive`). Shadow the pure parts; replay the rest.

Then the states themselves (part A's order), which mostly call the functions above.

**Things a faithful port must keep:** the always-zero SP alert helpers (4.5), stats records 0/2 unused, armour
inert, the 15-frame vision gate and one-raycast-per-frame budget, level-hash special cases (Castle Indoors 1,
Tower A/B, Power Station, Evil Base, Henderson D, Space Station D - all listed in the functions above), mutable
behaviour words, the missing cover/AI point bounds (add an assert, not a behaviour change).

---

## 10. Open questions

- Meanings of properties marked **(guess)** or "?" in `behaviour_properties.md` (0x14-0x16 doors, 0x21, 0x34/0x36/
  0x3a/0x3b explosives, 0x00-0x02 movement) need the reading states (part A) or a play test toggling each.
- `DroneAnimStates` +4/+5 and `Drone_AnimInfo` +4/+0xf are not read by the functions I traced; `anim_states.csv`
  has them raw.
- Anim state ids have no names; the rows' scripts (`anim_tables.csv`) can be named by playing them with the
  docs/anim tools (a good follow-up: export each state's column-0 script to see what it is).
- Where the cover-node and AI-point arrays are allocated and their capacity (part C / `Drone_PostLoad_Init`).
- Whether single-player drones really never react to the broadcast messages 0x11/0x12/0x13/0x15 (4.5): confirm in
  play by shooting a drone near another one out of its sight.
