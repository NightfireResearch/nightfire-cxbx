# Neighbours: the driving hand-over and the in-level carry-over

Neither is part of `engine.savestate`, and neither uses save blocks, but both are what "restoring state" means in
play, so they are described here to save the next reader the search.

## The in-level carry-over: `Player_RamSave` / `Player_RamLoad` (player subsystem)

There is no checkpoint snapshot of the world. A mission's parts are separate levels (`HT_Level_HendersonB`, `C`,
`D`, the castle, tower, power station, silo parts, ...), and both "continue after death" and the walk from one part
to the next reload a level from its start with a **warm reset** (`IsWarmReset`, 0x00279250, set from
`ResetMap_LevelToLoad(hash, warmReset, bypassFmv)`, 0x000bddf0). What survives is what `Player_RamSave` copied to a
fixed RAM area beforehand:

| RAM (Xbox) | size | copied from (`Player_RamSave` 0x000abdf0) |
|---|---|---|
| 0x00276768 | 14 dwords | `PlrMissionStats`, from `timesDetected` on (the mission statistics) |
| 0x002767a0 | 0x42 | `BLData` +0x114 (16 dwords and a short) |
| 0x002767e4 | 0x558 | `BLData` +0x158 (0x156 dwords: the weapon/ammo state) |
| 0x00276d3c | 10 floats | `SSysItems[0].countdownSeconds` onwards (restored through `SSys_RamLoad`, 0x000cfb40) |
| 0x00276d64 | float | `BLData.health` |
| 0x00276d68 | dword | `BLData` +0x840 (armour, by inference) |
| 0x00276d6c | 0x400 | `switch_channels_time` (256 dwords) |
| 0x0027716c | 5 dwords | `BondMoments` from short index 0x65 |
| 0x00277180 | short | the weapon being switched to (`animState->switchingToWeaponId`) |
| 0x00277182 | 0x33 | `BLData.wpnStats[0x71]` +8 onwards (12 dwords, a short, a byte) |
| 0x002771b5 | 0x100 each | `switch_channels`, `switch_channels_hold` (0x001df238), `switch_channels_prev` |

So the script switch channels (the level's global flags) carry over too; doors, drones, pickups and other objects
do not - they come back as the level file places them.

- **Saved** by `Script_EventHandler` event 0x12 (0x000c2c88, then `ResetMap_LevelToLoad(arg, warm)`) and by
  `Trigger_Activate` (0x000d2c17: a level-change trigger, which also stamps `switch_channels_time`, plays music
  event 3, pauses, pushes game-flow state 7 and calls `ResetMap_LevelToLoad(trigger+0x1c, warm)`).
- **Restored** by `Player_RamLoad(bl, isContinue)` (0x000ae700), which does nothing unless `IsWarmReset`:
  - from `Player_InitWeapon` (0x000bc1e0) with `isContinue = 0`, for the follow-on levels listed in its switch
    (Henderson B-D, castle courtyard and indoors, tower B/C, power station A2, tower 2, evil silo / base C, ...);
  - from `P_ENDMISSION_Handler` (0x00083f69) when the player picks the first option after death:
    `ResetMap_LevelToLoad(GameState.CurrentLevelHashcode, warm = true, bypassFmv = true)` then
    `Player_RamLoad(player 0, 1)`. With `isContinue` set, the saved health is raised to at least
    `ContinueHealthBoostEasy/Medium/Hard` (0x0017e680/4/8) by `GameState.difficultyModifier`, and the raised value
    is written back to the RAM copy. The second option restarts the mission cold from `Mission_BaseMapHCode()`.

Status: all live (the player subsystem, `docs/architecture/player-weapons-mp.md` lists it under death and pain).
If a future task is "checkpoints", this is the code to port, not `LS_*`.

## The driving hand-over: `Boot_LoadPTPData` (0x00019db0, engine.flow, live)

The driving levels run in a separate executable. Going there, the action engine writes the shared
`sNightFireShared_tag` (0xa50 bytes: `PTP_Eurocom ECDataBuf` 0x4b0, `PTP_EA EACDataBuf` 0x4b0, then version,
volumes, level status, controller and display options, scoring; typed in `src/action/game.h`) into the launch
data; coming back, `Boot_LoadPTPData` (called from `src/action/game.cpp`) reads it with `psiGetDrivingData`
(0x000dfb70) into `TmpBuff` (0x001d88e0), copies it to `PTPDATA` (0x001d7e90), and if `TmpBuff.Version` == 1:

1. `LS_LoadFromBuffer(&PTPDATA, 0x12c0)` - returns 0, and the restore is skipped, if the first four bytes (the
   codename's first characters, `ECDataBuf` +0) are zero. Its copy into `InBufData` is of the PTP struct, not of
   save blocks (see [functions.md](functions.md)).
2. Restores everything field by field from `ECDataBuf` and the tail: mission map and status, difficulty, the
   codename (`ls.codename`, 32 bytes from `ECDataBuf` +0), `ls.slot` (+0x80), `ls+0x3d` (+0x84), player 0's
   control options, cheats, multiplayer rules, Nightfire status, `Menu_SetBonus` (+0x140 lo, +0x13c hi), the
   weapon-upgrade flag (+0x150), default codename labels, driving scores, language, sound mode and volumes,
   subtitles, widescreen.
3. Chooses the menu page to return to and calls `ResetMap_LevelToLoad(HT_Level_Menu_Pre, ...)`.

So the profile survives the trip through the driving engine by being carried field by field in the launch data,
not by an `LS` save/load. A reimplementation of `LS_LoadFromBuffer` only has to keep its return value and its copy
for this path.
