# The savestate (`LS_*`) subsystem

`engine.savestate` in `tools/subsystems_action.txt` (0x0006eb20, "LS (mission state, save blocks)") is the action
engine's **Codename (profile) save serialiser**: it turns the player's settings, campaign progress, multiplayer
handicap, global options, cheats and bonus unlocks into a list of labelled, bit-packed blocks, hands the result to
the platform save code (`psiSaveData` / `psiLoadData`, already ours), and on load unpacks the blocks back into the
game's globals. 20 functions, 3.8 KB of original code, none reimplemented yet.

This folder documents it, as reviewed from the Xbox `default.xbe` (names and signatures from the PS2 build's
symbols, layouts from the Xbox binary), ahead of reimplementing it.

| document | covers |
|---|---|
| this page | what it is and is not, when it runs, the inventory with status, what is ours, unknowns, order and testing |
| [blocks.md](blocks.md) | the buffers, the block format, every field of the six blocks with bit offsets and Xbox addresses, a decoded save file as a test vector, the globals |
| [functions.md](functions.md) | function by function: signature and calling convention, what it does, callers and callees, quirks to keep |
| [related.md](related.md) | the two neighbours people mistake for it: the driving-engine hand-over (`Boot_LoadPTPData`) and the in-level carry-over / "continue" (`Player_RamSave` / `Player_RamLoad`) |

Evidence is marked where it matters: *disasm* (checked in the disassembly), *decompile*, *xrefs*, *bytes* (read
from the XBE or from a save file), *PS2* (the PS2 build agrees), *inference*. Invented names are in italics with
"(invented)".

## What it is - and what it is not

**It is not a checkpoint or in-level state system.** Nothing in `LS_*` is called during play, at a level start,
on death or on a level change, and no object type registers save or restore callbacks with it (the only callers
are front-end menu code and `Boot_LoadPTPData`, *xrefs*). The game has no world-state snapshot at all:

- "Continue" after death and the hand-over between the parts of a multi-part mission are done by
  `Player_RamSave` / `Player_RamLoad` (player subsystem, 0x000abdf0 / 0x000ae700), which copy the player's
  health, armour, weapons and ammo, the mission statistics, the timers and the script switch channels into a RAM
  block, reload the sub-level from its start with a warm reset, and copy them back. See [related.md](related.md).
- Returning from the driving engine (a separate executable) restores state from the launch data
  (`PTPDATA`, `Boot_LoadPTPData` 0x00019db0), not from a save block. `LS_LoadFromBuffer` is called there, but only
  as a "is there a codename" guard. See [related.md](related.md).

What *is* saved, per Codename (one 166-byte file in our port, `saves/<codename>.dat`), in six blocks:

| bit | label | contents | applied on load |
|---|---|---|---|
| 0x01 | `PLRS` | one player's control options (12 fields, 19 bits) | all, into `PlayerInputs[slot]` |
| 0x02 | `MSSN` | Nightfire progress word, per level the best score and Bond moments, the weapon-upgrade flag | all; the score tables are rebuilt by `PlrStats_SetupScoreTable` |
| 0x04 | `MPSG` | one player's multiplayer flag and health modifier | both, into `MPSettings.Player[slot]` |
| 0x08 | `GSET` | music and SFX volume, language, subtitles, sound mode, widescreen, split-screen layout, 7 reserved words, a version 4 | only volumes, subtitles and layout; language, sound mode and widescreen are read and dropped (the Xbox dashboard owns them; the PS2 applies mode and widescreen) |
| 0x10 | `CHET` | immortal, all weapons, unlimited ammo | all, into `CheatInfo` |
| 0x20 | `BNUS` | the 64-bit bonus/unlock word | via `Menu_SetBonus(lo, hi, slot, 0)` |

## When it runs

All entry points are polled once per frame from front-end page handlers, through `Menu_UpdateMessageBox`
(0x000757d0, ui.frontend, live), which switches on `ls.operation` (`MENU_LS` at 0x0017d540, typed in
`src/action/ui/Menu.h`) and hands the returned state to `XBox_DoSaveFlow` (0x0008f3f0, live, the message-box UI):

```
page handler ── (op, mask, slot) ──► Menu_UpdateMessageBox(managerNum, mask, slot)       every frame while ls.busy
                                      op 0 load   ─► LS_Load(ls.codename, slot, mask)
                                      op 1 save   ─► LS_Save(ls.codename, slot, mask)    (only if ls+0x3d and a codename)
                                      op 2/4 enum ─► Menu_EnumSaves        op 3 delete ─► psiDeletingData
                                      └─► XBox_DoSaveFlow(manager, state, &ls)  (state = LS_* return value)
LS_Save:  LS_GetSaveStuff(mask, slot) → DstData[slot] ─► psiSaveData ─► copy to InBufData[slot] ─► poll ─► 7 ok / 8 failed
LS_Load:  clear InBufData[slot] ─► psiLoadData ─► poll ─► 7: LS_GetLoadStuff(mask, slot) applies blocks / 8 failed
LS_FlushStates: both state machines back to 0 (Menu_UpdateMessageBox, XBox_DoSaveFlow, C_LBERROPTIONS_Handler)
```

The masks every caller passes (*disasm* of the 18 call sites of `Menu_UpdateMessageBox`, and of the three of
*Menu_StartLS* (invented, `FUN_0007fef0`)):

| caller | operation | mask | slot |
|---|---|---|---|
| `C_SBNFCN_Handler`, `C_SBCNSELECT_Handler` (via *Menu_StartLS*) | load a codename | 0xff | 0 |
| `P_CNSELECT`, `P_CNMENU`, `P_CNNAME`, `C_SBCNOPTIONS`, `C_KEYBOARD`, `P_NFSELECT`, `P_NFRESULTS` (and ours: `ui_codenames.cpp`, `ui_secrets.cpp`) | load or save, per `ls.operation` | 0xff | 0 (a register the callers zero, by inference for the `ESI`/`EDI` ones) |
| `C_RBMPCNAME_Handler` (via *Menu_StartLS*) | load a multiplayer player's codename | 0x25 (`PLRS`+`MPSG`+`BNUS`), 0x2d (+`GSET`) when only one player has joined | the player (0-3) |
| `P_MPJOIN_Handler` 0x87cca | continue that load | 0x25 | the player |
| `P_PARISENUM`, `P_NFSELECT`, `P_MPJOIN`, `P_CNSELECT` (the others) | enumerate (op 2/4) | 0 or 4, unused | 0 |

So **every save writes all six blocks** (mask 0xff); the "keep the old copy of an unmasked block" path in
`LS_GetSaveStuff` is never exercised by the shipped menus. Partial masks happen only on multiplayer loads, where
the slot is the player index - the slot doubles as the `PlayerInputs` / `MPSettings` / bonus player index for the
per-player blocks.

When those pages run, in game terms: loading a codename at the Codenames screen or Nightfire mission select;
saving after creating or renaming a codename, after changing codename options or entering a secret code, and at
the mission results page (`P_NFRESULTS`) after a mission. A free-space check before the first save of a new
codename uses *LS_GetFullSaveSize* (invented, `FUN_0006f950`).

## Inventory

All live (none replaced, none dead, *function_coverage.py*). Size in bytes.

| address | name | size | role | external callers |
|---|---|---|---|---|
| 0x0006eb20 | `LS_FlushStates` | 16 | reset both state machines | `Menu_UpdateMessageBox`, `XBox_DoSaveFlow`, `C_LBERROPTIONS_Handler` |
| 0x0006eb30 | `LS_FindBlockByLabel` (Xbox name; inlined on the PS2) | 128 | walk `InBufData[slot]` for a label - **usercall** (slot in `AX`, label in `EDI`) | - |
| 0x0006ebb0 | `LS_LoadFromBuffer` | 96 | copy a buffer into `InBufData[0]` if its first word is non-zero | `Boot_LoadPTPData` |
| 0x0006ec10 | `LS_GetSaveStuff` | 192 | build the save in `DstData[slot]` | - (and *LS_GetFullSaveSize*) |
| 0x0006ecd0 | `LS_GetLoadStuff` | 96 | apply the masked blocks of `InBufData[slot]` - **usercall** (slot in `BX`) | - |
| 0x0006ed30 | `LS_MakePlrSettings` | 432 | `PLRS` make | via table |
| 0x0006eee0 | `LS_LoadPlrSettings` | 288 | `PLRS` load | via table |
| 0x0006f000 | `LS_MakeCheats` | 192 | `CHET` make | via table |
| 0x0006f0c0 | `LS_LoadCheats` | 80 | `CHET` load | via table |
| 0x0006f110 | `FUN_0006f110` = `LS_MakeMission` (PS2) | 288 | `MSSN` make | via table |
| 0x0006f230 | `LS_LoadMission` | 272 | `MSSN` load | via table |
| 0x0006f340 | `LS_MakeBonus` | 176 | `BNUS` make | via table |
| 0x0006f3f0 | `LS_LoadBonus` | 96 | `BNUS` load | via table |
| 0x0006f450 | `LS_MakeMPSettings` | 176 | `MPSG` make | via table |
| 0x0006f500 | `LS_LoadMPSettings` | 80 | `MPSG` load | via table |
| 0x0006f550 | `LS_MakeGlobalSettings` | 464 | `GSET` make | via table |
| 0x0006f720 | `LS_LoadGlobalSettings` | 352 | `GSET` load | via table |
| 0x0006f880 | `LS_Save` | 208 | save state machine | `Menu_UpdateMessageBox` |
| 0x0006f950 | `FUN_0006f950` = *LS_GetFullSaveSize* (invented; Xbox only) | 16 | `LS_GetSaveStuff(0xff, 0)` | `FUN_0008fcc0`, `FUN_0008fd90` |
| 0x0006f960 | `LS_Load` | 208 | load state machine | `Menu_UpdateMessageBox` |

Outside the range but used only by it (engine.util, all live): `BIN_PushBits` 0x0006b1d0 (128), `BIN_PullBits`
0x0006b250 (144), `BIN_PullBits_U32` 0x0006b2e0, `BIN_PullBits_U8` 0x0006b310, `BIN_PullBits_U16` 0x0006b340 (48
each). Their only callers are the twelve make/load functions (*xrefs*), so they belong to the same port.

## What is ours already

Nothing in the range. Around it (grep of `src/action` for `AUTOINJECT` / `FUNC_AT`):

- **Ours**: `psiSaveData`, `psiLoadData`, the save enumeration (`src/action/engine/psiSave.cpp`), the free-space
  helpers `SaveDrive_FreeBlocks` / `SaveDrive_BlocksFor` and their thunks; `Menu_GetNightfireStatus`,
  `Menu_SetNightfireStatus`, `Menu_SetBonus` (`src/action/ui/MenuUnlocks.cpp`); `PlrStats_GetScoreTable`
  (`src/action/game/sp/PlayerStats.cpp`); `GetLanguage` (`src/action/engine/Text.cpp`); `BIN_GetByte/Word/DWord`
  (`src/action/util/bin.cpp`, not the bit functions); the front-end pages that call `Menu_UpdateMessageBox` in
  `src/action/ui/ui_codenames.cpp` and `ui_secrets.cpp`.
- **Still original**: `Menu_UpdateMessageBox`, *Menu_StartLS* (`FUN_0007fef0`), `XBox_DoSaveFlow`,
  `Menu_EnumSaves`, `C_LBERROPTIONS_Handler`, `FUN_0008fcc0` / `FUN_0008fd90`, `Boot_LoadPTPData`,
  `PlrStats_SetupScoreTable` (0x000b1530), `Menu_GetBonus` (0x0007c660, Xbox `FUN_0007c660`), the SFX volume and
  mode getters/setters (0x000c5060, 0x000c5090, 0x000c5430, 0x000c6350, 0x000c6360), the `BIN_*Bits` functions.
- **Types we already have** for the globals it touches: `PlayerInput` (`src/action/input.h`, 0x158 bytes),
  `MPSettings_t` / `MPSettings_PerPlayer` (`src/action/game/mp/multiplayer.h`), `CheatInfo_t`, `GameState_t`
  (`src/action/game.h`, `game.cpp`), `SCORETABLE` / `ScoreCategory` (`PlayerStats.h`), `MENU_LS` (`Menu.h`).

Left: 20 + 5 functions, about 4.2 KB, plus `PlrStats_SetupScoreTable` (0x000b1530, 0x1c4 bytes, player.PlrStat)
if the `MSSN` load is to stop calling original code.

## Unknowns and risks

1. **`ls+0x3d` (`MENU_LS.field12_0x3d`)** gates every save (`Menu_UpdateMessageBox` op 1 refuses without it). It
   is set by `P_PARISENUM_Handler` and restored from the driving hand-over (`PTPDATA.ECDataBuf+0x84`); by inference
   "this session has a codename to save to". Not needed to port LS itself, but needed to drive a save in a test.
2. **The two usercall functions.** `LS_FindBlockByLabel` (slot in `AX`, label in `EDI`, length pointer on the
   stack) and `LS_GetLoadStuff` (mask on the stack, slot in `BX`). Ghidra's prototypes for both are wrong
   (*disasm*). Both are called only from inside LS, so porting the subsystem as one unit needs no thunks; porting
   either alone does (the `FS_MatchFilenameToHeader` pattern in `src/action/engine/FS.cpp`).
3. **Hazards in the original that a faithful port inherits** (none reachable with the files the game itself
   writes): `LS_FindBlockByLabel` loops forever on a block with a non-zero label and a zero length, and can read
   up to 7 bytes past a slot; `LS_LoadMission` trusts the 8-bit score count against 20-entry stack arrays (a count
   over 20 overruns its stack); nothing checks that a save fits its 0x1000-byte slot; `LS_LoadFromBuffer` copies
   0x12c0 bytes into a 0x1000-byte slot (from `Boot_LoadPTPData`, overrunning into `InBufData[1]` - harmless, see
   [functions.md](functions.md)). Decide per hazard whether to keep, assert (`NF_WARN`/`NF_ASSERT`, see
   `docs/gamecube-checks.md`) or bound it; the shadow tests must avoid feeding the original the infinite loop.
4. **The `MSSN` load wipes score results.** `PlrStats_SetupScoreTable` copies the temporary tables by index, so a
   load zeroes every level's `timeTaken`, `parTime`, difficulty fields, `baseScore` and every category's
   `achieved`/`rating`/`points` except Bond moments, keeping only `bestScore` (*decompile*). The categories'
   targets and `score` (+0x34) are untouched. Presumably harmless because `PlrStat_GetScore` refills them before
   they are shown; worth one look before relying on it.
5. **`psiLoadData` size contract.** `LS_Load` asks for exactly the size a fresh full save would have *now*
   (166 bytes with 12 score entries). Ours reads up to that and ignores the file's own length; the original
   (`FUN_000e3580`) validated a header. Only matters if the block layout ever changes.
6. **Field meanings not pinned down**: `MPSettings_PerPlayer.SomeField2` (the `MPSG` flag); the sense of
   `PlayerInput+0x0b` (Ghidra "hudVisibility"; `Boot_LoadPTPData` writes `fHudAlwaysVisible == 0` into it) and of
   `+0x08` (Ghidra "crosshairOff", but written as `fCrossHairOff == 0`, so ours, `crosshairsEnabled`, is right);
   the seven reserved `GSET` words and the version 4 (never checked on load).
7. **The slot argument's register values** at the `P_NFRESULTS`, `P_NFSELECT` and `P_CNSELECT` call sites are
   registers (`ESI`, `EDI`) assumed zero by inference; a save to a slot other than 0 has not been seen.

## Suggested order

The subsystem is small, self-contained and byte-testable, so it can go in two or three commits:

1. **The bit functions** (`BIN_PushBits`, `BIN_PullBits` and the three wrappers) into `src/action/util/bin.cpp`,
   with a shadow over random values, widths 1-32 and bit offsets 0-7 (including `BIN_PullBits`' second, sign
   word - see [functions.md](functions.md)).
2. **The twelve make/load functions and the block table**, in a new `src/action/engine/SaveBlocks.cpp` (or
   `src/action/game/LS.cpp`), with our own copy of the table pointing at ours. Make functions compared byte for
   byte and length for length; load functions compared on every global they write. `LS_LoadMission` can keep
   calling the original `PlrStats_SetupScoreTable` at first.
3. **The drivers**: `LS_FindBlockByLabel`, `LS_GetSaveStuff`, `LS_GetLoadStuff`, `LS_Save`, `LS_Load`,
   `LS_FlushStates`, `LS_LoadFromBuffer`, *LS_GetFullSaveSize*, all at once (removes the usercall problem), owning
   `DstData`, `InBufData`, the two states and the statics (all private to LS, *xrefs*).
4. Later, with the front end: `Menu_UpdateMessageBox`, *Menu_StartLS*, `FUN_0008fcc0`/`FUN_0008fd90`,
   `XBox_DoSaveFlow`; and `PlrStats_SetupScoreTable` with the score code.

Add the GameCube-style checks (`docs/gamecube-checks.md`) as each function is written: slot < 4, save size <=
0x1000, block length >= 8, score count <= 20 (or <= `ARRAY_SIZE(ScoringTable)`).

## Testing

Per the project's targeted-testing rule: shadow tests plus the replays that reach the change.

- **`SaveShadow.cpp`** in `src/action/devtools/`, run from `MenuShadowTests=on` (before the game, with the others
  in `MenuProbe.cpp`) and from a replay step once a codename is loaded. Pattern as `SecretsShadow.cpp` /
  `UnlocksShadow.cpp`: snapshot the touched globals, run the original (via `XbeOriginalScope` once ours is
  injected), snapshot, restore, run ours, compare, restore.
  - make: for slots 0-3 and several seeded states (random `PlayerInputs`, `MPSettings.Player[0..3]`, `CheatInfo`,
    bonus words, Nightfire status, `ScoringTable` best scores and Bond moments, `GameState+0x52`), compare the
    returned buffer up to the length, the length, and the static buffer's trailing byte.
  - load: feed both the blocks of `Release/saves/BOND.dat` (decoded in [blocks.md](blocks.md)) and randomised
    blocks; compare `PlayerInputs[slot]` (0x158 bytes), `MPSettings.Player[slot]`, `CheatInfo`, `SubtitlesEnabled`,
    `MultiplayerLayout_LeftRightOrTopBtm`, the volume getters, `GameState`, `menu_bonus` and the upgrade state
    `Menu_SetBonus` rebuilds (`UnlocksShadow.cpp` has the snapshot), the Nightfire status, and the whole
    `ScoringTable` with its category tables (`ScoreShadow.cpp` has that block).
  - drivers: seed `InBufData[slot]` with crafted block lists (missing blocks, reordered blocks, junk after the
    last), run `LS_GetSaveStuff` with every mask 0x00-0x3f and compare `DstData[slot]` and the size; run
    `LS_GetLoadStuff` likewise. Never give the original a zero-length block.
  - `LS_Save`/`LS_Load` end to end on a scratch codename (say `SHADOWT`): the returned state sequence over three
    calls, the file bytes, `InBufData`, then delete the file. Restore the real codename's buffers afterwards.
- **Replays** (`tools/ui/scripts/`): `codenames.txt` loads the codename at `P_CNSELECT` and walks its menus;
  `secrets_passport.txt` / `secrets_walk.txt` reach Secret Unlocks, where entering a code saves. Which existing
  script ends in a save was not checked; a short new one (change an option on the codename wheel, accept the save)
  is probably needed, plus one that loads a codename for a second multiplayer player to reach the 0x25/0x2d path.
  Compare `Release/saves/*.dat` byte for byte before and after (keep a copy of `BOND.dat`; a save is
  deterministic for a given state), and the option pages' screenshots after a load.
