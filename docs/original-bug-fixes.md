# Fixes for the original game's bugs

The rule for reimplemented code is exact behaviour, bugs included (docs/style-guide.md). The exceptions are listed
here: bugs in the original game that the port fixes, each behind `FIX_ORIGINAL_GAME_BUGS`.

The CMake option `NIGHTFIRE_FIX_ORIGINAL_GAME_BUGS` (on by default) defines `FIX_ORIGINAL_GAME_BUGS` as 1 or 0 for
every target. With it off, every function behaves as the original does. That is the build to use when comparing
against the original code, or when recording lockstep baselines meant to match an original run.

Rules for adding a fix:
- It goes behind `#if FIX_ORIGINAL_GAME_BUGS`, and the `#else` branch keeps the original behaviour exactly.
- The function's comment describes the original behaviour, then gives the fix in one line with a pointer here.
- It gets an entry below: where it is, what the original does (with the instruction that shows it), what the fix
  does, and what the fix changes in play or in testing.
- Only bugs, not taste. A fix must leave everything the original gets right unchanged. Gameplay changes, features
  and options belong outside the core.

## Action engine

### Locks_Init: keycode digits never include 9

- **Where:** `src/action/game/sp/Locks.cpp`, `Locks_Init` (default.xbe 0x000d01c0).
- **Original:** each of the four digits of the 51 keycodes is `'0' + Rand_Random() % 9` (`MOV ECX,0x9; DIV ECX` at
  0x000d020f), so a keycode never contains a 9.
- **Fix:** `% 10`, so every digit 0-9 can come up.
- **Effect:** the keycodes the game generates differ. Each call still draws one `Rand_Random` per digit, so the random
  sequence after it is unchanged.

### Menu_UpdateWheel: an empty wheel slot is coloured from item 999

- **Where:** `src/action/ui/Menu.cpp`, `Menu_UpdateWheel` (default.xbe 0x0007f680).
- **Original:** the wheel's four outer slots are 999 when the list ends before them, but all five colour messages
  are sent regardless (0x0007f9fa-0x0007fac1), each reading the item's enabled byte at `items + idx * 0x18 + 0x10`,
  so an empty slot reads item 999, past the end of the list.
- **Fix:** no colour message for an empty slot. Its text is already blank.
- **Effect:** an empty slot keeps the colour it had. Nothing visible changes, since the slot shows no text; the fix
  removes the read past the list (which on the host may fall outside the allocation).

### Sprite_Create2 and psiDrawSprites: a whole-texture sprite loses its last column and row

- **Where:** `src/action/gfx/Sprite.cpp`, `Sprite_Create2` (default.xbe 0x000cedf0), and
  `src/action/engine/psiSprite.cpp`, `psiDrawSprites` (0x000de3d0).
- **Original:** a sprite whose SpriteInfo gives no box in its sheet is drawn at the texture's size, but spans one
  texel less than it (`MOVZX EAX,word ptr [ESI + 0x28]; DEC EAX` at 0x000ceecd, and 0x000ceee4 for the height). The
  draw takes the span as a count, so a W-texel texture is drawn from texels 0 to W - 1 over W pixels: the last
  column and row barely show and the rest is stretched by 1/(W - 1). The final mission's Space pane shows it most:
  its 16 x 16 missile rings lose their right and bottom edges. `hashtable_set_sprite` gives the full size, so a ring
  the pane re-textures is drawn whole and one it has not is clipped.
- **Fix:** `Sprite_Create2` spans the whole texture. With that alone, a sprite drawn larger than its texels on a
  wrapping texture blends the texture's opposite edge into its far edge (a red line along the top of the screen
  under the damage overlay, a stripe down the right of the vision overlay, a line under the Space pane's doors), so
  `psiDrawSprites` also clamps a wrapping texture's sprite whose texels stay inside the texture. A sprite that runs
  past an edge (the static's random scroll) keeps the texture's own addressing.
- **Effect:** drawing only; no game state changes, and the recordings still match frame for frame. Across the
  eleven recordings, every 900th frame compared: the Space pane's rings and doors, the 007 logo and the battery icon
  are drawn whole.

## Candidates: bugs kept as the original has them

Found while porting the last of the action engine (20 Oct 2026). None is fixed: each port does what the original
does, and each would need its own entry above before a fix goes in.

Sound (src/action/sound/SFX.cpp, SFXStream.cpp):
- SFXStart3D ignores its volume argument: it always sets VolumeOverRide to 100, so the per-sound volume from
  Sound_Play/Sound_SetVolume never reaches the mixer.
- SFXReleaseStreamSystem frees the stream read buffers through an empty function, and ES_InitialStreamBuffer
  allocates 8 x 0x4800 again on every SFXReset: every level load leaks them.
- Streamed sounds (a FileRef below 0) can never play: SFXInitialiseStream gives them streams from index 1 and there
  is one stream, and their lookup table pointer (0x2997cc) is never set.
- ES_JumpToMusicMarker reads BaseMusicMarkerStart[-1] when no jump is pending; a multi-sample sound with SampleCount
  0 reads CuedSamples[-1].

Bots and multiplayer (src/action/game/drone/BOT*.cpp, src/action/game/mp/):
- MP_RegisterPickup, taking over the pickup with the shortest life, sets mpIndex on the last pickup it looked at,
  not on the one it takes.
- BOTSTATE_checkAttackMove returns the move's state even when the move test says there is no room.
- BOT_validateStateChange refuses the three impact states and BotHeardNoise.

Drones (src/action/game/drone/):
- NDrone2_HandleDroneAlert: the global alarm reads a bot's sender as a drone (a NULL or non-drone sender is a NULL
  read); message 0x13's range test looks inverted; messages 0x11 and 0x12 count as handled out of shouting range.
- NDrone2_MoveTestRelative reads drone->dtype when the drone is NULL and `unscaled` is 0.
- NDrone2_DSTATE_BotGlobal messages 0x40/0x41 compare the objective getter's result with -1, not NULL, so
  NDrone2_SetOpponent(NULL) can happen.
- BotAttackStrafeAimLeft waits Rand_Rand(8) extra seconds, Right Rand_Rand(4).

Menus and boot (src/action/ui/, src/action/game.cpp):
- A Combo control's label stays blank: its list never answers GetSelectedItemText.
- C_RBMPFINISH: B adds the handicap items to the title's control rather than the radio.
- P_MPSETUP drops the players' claims on the one-per-game characters but keeps the bots'.
- psiGetDrivingData writes 0xc00 bytes into Boot_LoadPTPData's 0xa50-byte buffer at 0x1d88e0 (the buffer stays at
  the game's address, so the overrun lands where it always did); Boot_LoadPTPData then reads 0x12c0 bytes with
  LS_LoadFromBuffer, past PTPDATA.
- AnimDebugCreate (RefRoom) has no NULL check for a skin that is not loaded.

HUD (src/action/ui/HUD.cpp):
- The zoomed crosshair's box in its sheet, HUDCrossCoords[1] (1, 2, 29 x 29), takes in row 30, the first row of
  crosshair 6's box (0, 30): the tip of that icon's arrow (alpha 19 and 108 at x 5-6) is drawn as a red dot at the
  reticle's bottom left. Row 30 also holds the reticle's own bottom edge (x 13-17), so the box cannot simply end a
  row sooner. The dot shows whichever way a pixel centre maps to a texel (at 1:1 the last row samples v 30.5, or
  30.03 with D3D8 pixel centres): it is in the original's data, not our texel mapping. Crosshairs 4-8 are never
  chosen (Check_Target sets 2 or 3, HUD_UpdateCrossHair 1 when zoomed), so a replacement of the sheet
  (textures/09c4f43a25829f60.dds, tools/texture_replacement.py) with crosshair 6's box cleared removes the dot
  without a change to the core. The PS2 data's build converts the sheet, so its replacement has another name:
  textures/a5348873cd7b9888.dds, the same box cleared in the PS2's sheet (docs/ps2-data.md, `ps2data_check texhash`).

## Driving engine

### Scheduler::Run: a fractional timeScale loses its fraction

- **Where:** `src/driving/Scheduler.cpp`, `Scheduler::Run` (Driving.xbe 0x0005ba80).
- **Original:** the ticks to simulate are `(clock - lastTickCount) × timeScale`, truncated. A call that comes to no
  whole tick leaves `lastTickCount` alone. Any other call sets it to the clock, and the fraction left over is dropped.
  With the clock moving one tick per call, a timeScale of 0.75 runs one tick every two calls (0.5), and 1.5 runs no
  faster than 1. ESetSimRate events set the timeScale for slow and fast motion, so those scenes run at the wrong rate.
  On a PC the game loop runs many times per clock tick, so the clock usually moves at most one tick per call and the
  error is at its worst.
- **Fix:** the fraction is carried in `g_pendingTicks` and added to the next call's ticks, so the simulation runs at
  the timeScale. It is reset when a step of 12 ticks or more is dropped and after a cinematic skip. Measured in the
  underwater level (25 Sept 2026): 0.75 runs at 0.75 and 1.5 at 1.5. Slow motion still lowers the frame rate, as on
  the console, because only a call that simulates a tick renders.
- **Effect:** scenes with a fractional ESetSimRate run at their intended speed. Integer timeScales (1, the speed
  cheat's 2, the cinematic skip) run as before. Lockstep runs through such a scene differ between the two settings,
  so record the baseline with the setting the comparison build uses. Built without the fix, lockstep
  (`Teleport_BeforeRun`) leaves `oneTickPerRun` off below a timeScale of 1. The original's truncation turns one
  scaled tick into none and never advances, so the forced one-tick mode would hang.
