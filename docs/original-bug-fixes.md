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
