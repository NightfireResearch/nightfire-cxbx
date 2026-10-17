# The drone (AI character) system

"Drones" are every computer-controlled character in the action engine: guards, snipers, civilians, hostages,
allies, the party girls and truck drivers, the ninjas and astronauts - and the multiplayer bots, which are drones
too - the only non-human participants in a multiplayer match are bots; no single-player drone appears there. This
folder documents how the system is built, as reviewed from the Xbox `default.xbe` (with the PS2 build's
symbols for names and the GameCube build's debug messages), ahead of reimplementing it. Almost none of it is ours
yet: 4 of the ~337 core functions, none of the bot or navigation ones.

| document | covers |
|---|---|
| [architecture/](architecture/README.md) | lifecycle (creation from level data, per-frame order, deletion), the message-driven state machine and its 250-entry table, `Drone_tag` / `DCVars_tag` / `DIVars_tag`, drone modes and types |
| [behaviours/](behaviours/README.md) | the behaviour data in each placement, the property/stat tables, senses (sight, hearing, alerts), combat (firing, cover, grenades, melee, damage), animation tables |
| [bots-and-navigation/](bots-and-navigation/README.md) | multiplayer bots (their states, goals, personalities, stats, weapons) and the navigation both use (AI paths, bounds, A*, emitters, route following) |

Each has appendices (full tables, field-by-field readers and writers, function inventories with "already ours?").
The evidence behind each claim is marked in place: disassembly, decompile, xrefs, table bytes, PS2 match, or
inference. Three central claims were re-checked against the XBE when these were written: the state table (250
pointers at 0x177ca0, 238 distinct), the bot stats table (0xe-byte entries at 0x163628, 16-bit health at +4) and the
stats record index (`NDrone2_DoModeSettingsNEW` asks `behaviour_util_getStats(class, 1)`, always 1).

## The shape of it

```
level placement (DIVars: 33 keys, incl. a behaviour block)
   │  Drone_Create → NDrone2_CreateFromDIVars → NDrone2_CreateObj      (0x978-byte Drone_tag, on NDrone2List)
   ▼  first frame: Drone_PostLoad_Init → NDrone2_SetupGroups (spawners) → NDrone2_DefaultInit → Drone_SM_InitObject
mode + type ──── DroneModeSettings[36] / DroneTypeSettings[85] (XBE)  ← key5 < 0x24: DoModeSettingsOLD
   │             behaviour class + attack modes → GetDTYPENEW        ← otherwise:   DoModeSettingsNEW
   ▼
every frame: control_movement_object_handler
   Drone_InitComms (global AI work, delayed messages)
   Drone_Control per drone:
      PreDroneControl (timers, switch-driven mode changes → messages)
      type control fn (Zoe/Ninja/Astronaut) or NDrone2_ControlSTANDARD:
          Update message → state machine; perception; movement (routes, LinkCreep); collision; firing
      PostDroneControl; animation; DroneAnim_CallHandler (anim-end → state changes)
state machine: one function per state, (DCVars*, Drone*, obj*, MsgObject*) - Enter/Exit/Update are messages;
   unhandled messages go to DSTATE_Global (DSTATE_BotGlobal for bots); Drone_SM_SetState is the only writer
bots: drone type 30 + BOT_vars_t (0x768, one per bot player); DSTATEs 195..249; BOT_validateStateChange vetoes
navigation: map block 0x05 → AI paths/bounds; goal marking + A* per request (costs spread drones over links);
   emitters = one Dijkstra flood per point of interest (pickups, objectives, cover, AI points)
```

## "Is there a big behaviour table - and scripting?"

A big table, yes; scripting, no. There is no bytecode or drone language - it is data steering C code, in four
layers (behaviours/README.md):

1. **Per placement, in the level file**: a behaviour class (0-16), two attack modes, and two sets of 91 behaviour
   properties (flags such as "hears sounds", "uses cover", "may surrender", "raises the alarm on sight"; a primary
   set and one swapped in when alerted), plus packed stats records. All 555 placements in the 32 level bundles use
   this format; `level_drone_behaviours.csv` decodes every one.
2. **Static type tables in the XBE**: 36 drone modes and 85 drone types (initial and alert state, init and
   per-frame hooks), byte-identical to the PS2's named tables.
3. **Per-level tuning floats**, set by `ReadTuningVars` (already ours).
4. **Animation tables**: 119 anim states with transition lists, 436 animation rows, a 436 x 27 grid of anim
   scripts by row and anim set, and cover-animation lists.

The "scripted" feel comes from the state machine: 250 small message handlers (in the style of Rabin's state machine
language), several of which play level scripts (`DSTATE_PlayScript`) or run mission sequences.

## Findings worth knowing before porting

The suspected game bugs below (unused stats records, armour, group alerts, unread properties) are **kept as they
are** in the reimplementation: behaviour first, fixes later if ever, and then as opt-in changes.

- **Header bugs** in `src/action/game/drone/NDrone2.h` (and Ghidra's DSTATE enum) - **fixed**, with the Ghidra side in
  `driving-symbol-matching/results/structs/action-drone.json`. The enum's own names are not in
  any build (no debug info), but the PS2 ELF's symbol table names the function in every slot of its state table
  (0x0029c120, mangled C++ symbols in `.strtab`, e.g. `NDrone2_DSTATE_AllyLeadDone__FP10DCVars_tag...`), and four
  slots disagree with the header: 35 holds AllyLeadDone (header: AllyFollowDone), 39 AllyFollowDone (UNKNOWN1), 139
  DonePressAlarm (DronePressAlarm; 138 is PressAlarm) and 171 NinjaGetCloseToPlayer (NinjaGetTooCloseToPlayer).
- **`BOT_stats_t.health` is 16-bit** (now fixed, every field named), not the byte `BOT.h` had: of the 29 entries at 0x163628, entry 24 stores 300
  (`2c 01`) at +4, the only value over 255 and the only one with a non-zero byte at +5. `BOT.cpp` and `BOT.h` also
  disagreed on `BOT_respawn`'s return type (`bool`, as Ghidra has it).
- **12 state slots are linker-folded** onto identical bodies (the eight bot personality states, BotAttackNoOpponent
  and BotStuck all point at 0x61470, plus three pairs): 238 distinct functions for 250 states.
- **Only the middle stats record is used**: levels store three per class (Easy/Normal/Hard by the look of it), and
  the only caller always asks for record 1.
- **Drone armour does nothing on Xbox**: its only writer stores 0.
- **Single-player drones seem not to react to the group-alert broadcasts** (their handlers test the property, then
  return 0); alerts spread by sight of an alerted drone within 50 m and by sound. Worth confirming in play.
- **23 behaviour properties are set by levels but never read** by the Xbox code.
- **Ghidra's `DroneFiring_BurstDelay_MinDist/MaxDist` labels** at 0x164094/0x16409c are the Min/Max delays.
- **No bounds checks** on the cover-node and AI-point tables (the GameCube warned above 0xff; see
  docs/gamecube-checks.md), nor on the 50-entry AI path/bound tables.
- **Custom register conventions**: `Drone_SM_RouteMsgDCV` (DCVars in ESI), `GetDTYPENEW` (ESI/EDI) and about a dozen
  navigation and animation helpers - each needs its original callers covered (replace them too, or a naked shim).
- **Shared scratch state in navigation**: goal marking writes bits into the node flags that A* then reads, so the two
  must stay back to back; per-link "used" counts reset every frame.

## Plan

One order across the three areas (each document has the detail for its own part):

0. **Foundations** (done, 30 Sept 2026: the structs and enums below in Drone.h, NDrone2.h, BOT.h and AINetwork.h;
   `tools/drone_tables.py` generates DroneTables.h/.inc for the state, mode, type, behaviour-layout and bot-stats
   tables, checked at start by devtools/DroneTablesCheck.cpp - the animation and bot state-class tables are left for
   their steps). Fix the header bugs; add the DMODE/DTYPE/message enums, the state-machine and message structs,
   `DIVars` as 33 named keys, `Drone_tag` field by field with offset asserts, `BOT_stats_t`/`BOT_vars_t`, the AI
   path/bound structs. Write **`tools/drone_tables.py`** (in the style of `tools/uihandler.py`) to generate, and check
   against the XBE bytes: the state table (each entry the original address until that state is ours, switched over
   from the AUTOINJECT tags), the mode and type tables, the behaviour property layout, the anim state/row/grid and
   cover tables, and the bot stats and state-class tables. Nothing changes behaviour at this step.
1. **Pure leaves, shadow-tested per call**: `behaviour_util_*`, the state-machine core (`Drone_SM_SetState`,
   `RouteMsgDCV` by recording its (state, message) calls), reaction/burst/damage helpers (RNG state saved and
   restored), the anim transition lookup, sight strength, `GetDTYPENEW`; navigation helpers (route flags, link
   lookup, distances, emitters, bounds geometry); bot leaves.
2. **Creation and setup - the most valuable test**: run the original and ours for every drone placement in every
   level and compare the whole 0x978-byte `Drone_tag` (`DoModeSettings*`, the mode/type init hooks, `DefaultInit`,
   `PostLoad_Init`, spawners). Navigation data (`AIPath_Parse` etc.) the same way at level load.
3. **Searches**: `NodeSearch`, A*, `EmitPath`, `CalcRoute`, shadowed with node flags / used counts / route structs
   snapshotted and restored around each pair.
4. **The per-frame skeleton** (`Pre`/`PostDroneControl`, `ControlSTANDARD`, `Drone_Control`, `InitComms`, `Delete`)
   and route following: replays (HendersonA and Castle Courtyard patrols, an MP bot match on FortKnox) with a per-drone
   state dump each frame. Replays are not repeatable in play (docs/ui/README.md): compare state sequences and early
   frames, not late pixels.
5. **The states, in groups**, shadowed per call by hooking `NDrone2_ProcessStateMachine` during replays: trivial and
   folded ones; Global, entry and patrol; reactions and death with `HandleImpact`; civilians, hostages, allies,
   snipers; combat and perception; ninja, astronaut; bots last (`BOTSTATE_pickGoal`, `processGoals`, `BotGlobal`).

Add each function's GameCube checks as it lands (docs/gamecube-checks.md, section B).

## Open questions

- The meaning of the properties marked as guesses, and names for the 119 anim states (play each state's scripts with
  the docs/anim tools).
- About 10 message ids with handlers but no sender found; the full case list of `BOT_validateStateChange`.
- Nothing found reads the per-frame danger flags (0x100/0x200) on navigation links; bot trait bits 1, 2 and 4 are only
  partly decoded.
- Field tables were scraped from decompiles, so accesses through plain integers can be missing: "no writer found" is
  not proof there is none.

The scripts that produced the appendices (table dumpers, level decoders, decompile scrapers) were research tools and
are not in the repository; `tools/drone_tables.py` in step 0 is where the table decoding belongs for good.
