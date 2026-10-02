# Buffers, block format and the six blocks

All addresses are Xbox (`/Xbox_EU/default.xbe`). The PS2 layouts differ (other globals, and a different `GSET`).

## Globals

Every one of these is read or written only by LS functions (*xrefs*), so a port can own them outright.

| address | name (Ghidra) | type | meaning |
|---|---|---|---|
| 0x0017c230 | `NumSaveBlocks` | u32 = 6 | entries in the table |
| 0x0017c238 | `SaveIFFBlocks` | `IFFBlock[6]`, 0x10 each | `{u32 mask bit, char label[4], make fn, load fn}` (*bytes*, below) |
| 0x00215790 | `DstData` | u8[4][0x1000] | outgoing save, one 4 KB slot per slot number |
| 0x00219790 | `InBufData` | u8[4][0x1000] | last loaded (or last saved) save per slot |
| 0x0021d790 | `Load_State` | u16 | `LS_Load` state: 0 idle, 1 loading, 7 loaded, 8 failed |
| 0x0021d794 | `Save_State` | u16 | `LS_Save` state: 0 idle, 2 saving, 7 saved, 8 failed |
| 0x0021d798 | (`PLRS` buffer) | u8[0x1000] | `LS_MakePlrSettings`' block |
| 0x0021e798 | `s__0021e798` | u8[0x1000] | `LS_MakeCheats`' block |
| 0x0021f798 | (`MSSN` buffer) | u8[0x1000] | `LS_MakeMission`'s block |
| 0x00220798 | `Buf_109` | u8[0x1000] | `LS_MakeBonus`' block |
| 0x00221798 | (`MPSG` buffer) | u8[0x1000] | `LS_MakeMPSettings`' block |
| 0x00222798 | (`GSET` buffer) | u8[0x1000] | `LS_MakeGlobalSettings`' block |
| 0x00223798 | - | u32 | `LS_Save`'s last poll result |
| 0x0022379c | `Size_64` | u32 | `LS_Save`'s save size |
| 0x002237a0 | `No_63` | u16 | `LS_Save`'s slot |
| 0x002237a4 | `WW_62` | u16 | `LS_Save`'s mask |
| 0x002237a8 | `OldState_66` | u32 | `LS_Save`'s state on entry (its return value) |
| 0x002237ac | `ret_57` | u32 | `LS_Load`'s last poll result |
| 0x002237b0 | - | u32 | `LS_Load`'s size, passed to `psiLoadData` by address |
| 0x002237b4 | `No_55` | u16 | `LS_Load`'s slot |
| 0x002237b8 | `WW_54` | u16 | `LS_Load`'s mask |
| 0x002237bc | `OldState_58` | u32 | `LS_Load`'s state on entry (its return value) |

Each block buffer is 4 KB but holds at most 68 bytes; the size is just the build's static-buffer habit. The
statics from 0x223798 up are function statics of `LS_Save` / `LS_Load`; nothing reads them between calls, so a
port can make them locals (the size passed to `psiLoadData` by address only has to live through that call, which
is synchronous in ours).

### The table (*bytes*, 0x17c238)

| entry | mask | label | make | load |
|---|---|---|---|---|
| 0 | 0x01 | `PLRS` | 0x0006ed30 `LS_MakePlrSettings` | 0x0006eee0 `LS_LoadPlrSettings` |
| 1 | 0x02 | `MSSN` | 0x0006f110 `LS_MakeMission` | 0x0006f230 `LS_LoadMission` |
| 2 | 0x04 | `MPSG` | 0x0006f450 `LS_MakeMPSettings` | 0x0006f500 `LS_LoadMPSettings` |
| 3 | 0x08 | `GSET` | 0x0006f550 `LS_MakeGlobalSettings` | 0x0006f720 `LS_LoadGlobalSettings` |
| 4 | 0x10 | `CHET` | 0x0006f000 `LS_MakeCheats` | 0x0006f0c0 `LS_LoadCheats` |
| 5 | 0x20 | `BNUS` | 0x0006f340 `LS_MakeBonus` | 0x0006f3f0 `LS_LoadBonus` |

The label is stored as four ASCII bytes in file order (`50 4c 52 53` = "PLRS"), and compared as a little-endian
dword (`0x53524c50`). Callback types (PS2 signatures, matching the Xbox code):

```c
typedef uchar *LS_MakeFn(uint label, ushort slot, ushort *lengthOut);   // returns its static block
typedef void   LS_LoadFn(uchar *block, ushort slot);
```

## The block format

```
+0  char label[4]        copied from the table entry
+4  u32  length          in bytes, little-endian, header included: (bitsUsed + 7) >> 3
+8  fields, packed LSB-first from bit 0x40 (BIN_PushBits / BIN_PullBits), no alignment between fields
```

- The make functions write the label and length byte by byte (`length` bytes 0-3 are `n`, `n>>8`, `n>>16` and
  `(bits+7)>>27`, which is `n>>24`); `*lengthOut` gets the low 16 bits.
- Bits are written one at a time, each set or cleared (`BIN_PushBits` masks), so a rewrite overwrites every bit
  it covers. Bits after the last field in the last byte keep whatever the static buffer held - zero in practice,
  since every make writes the same fields every time.
- A save is the blocks concatenated in table order, **no padding**; lengths are odd numbers of bytes, so headers
  after the first are unaligned. `LS_FindBlockByLabel` reads them byte-wise.
- Missing blocks are allowed: a load applies only blocks that are found; a partial save omits an unmasked block
  that the old buffer did not have.
- Nothing marks the end: the walk stops at a zero label, at offset 0x1000, or at the match. `psiLoadData` clears
  the slot first, so a short file ends in zeros.

## The fields

Bit offsets are from the start of the block (0x40 = first field). "rd" is the reader `LS_Load*` uses.

### `PLRS` - one player's control options (19 bits, 11 bytes)

Player = slot; `PlayerInputs` at 0x001fe6d0, 0x158 bytes per player (`src/action/input.h`).

| bit | width | field (offset in `PlayerInput`) | rd |
|---|---|---|---|
| 64 | 1 | `inverted` (+0x00, u8) | U8 |
| 65 | 4 | `controlStyle` (+0x0e, s16, written sign-extended) | U16 |
| 69 | 4 | driving control style (+0x10, s16) | U16 |
| 73 | 1 | auto-aim single player (+0x01) | U8 |
| 74 | 1 | auto-aim multiplayer (+0x02) | U8 |
| 75 | 1 | `manualAimToggle` (+0x03) | U8 |
| 76 | 1 | auto-switch weapons (+0x0a, ours `autoSwitchBetterWeapon`) | U8 |
| 77 | 1 | crouch toggle/hold (+0x04) | U8 |
| 78 | 1 | `vibrationEnabled` (+0x09) | U8 |
| 79 | 1 | crosshair (+0x08, ours `crosshairsEnabled`; Ghidra's "crosshairOff" is the wrong sense) | U8 |
| 80 | 2 | flashing objects (+0x0c) | U8 |
| 82 | 1 | HUD (+0x0b, Ghidra "hudVisibility") | U8 |

The load writes only the low byte (U8) or low half (U16) of the pulled value, so the 4-bit styles come back
zero-extended.

### `MSSN` - campaign progress (537 bits with 12 levels, 68 bytes)

| bit | width | field |
|---|---|---|
| 64 | 32 | `Menu_GetNightfireStatus()` (0x00076240) - restored with `Menu_SetNightfireStatus` (0x000762d0) |
| 96 | 8 | `count` from `PlrStats_GetScoreTable(&count)` - 12 (`ScoringTable`, 0x0017f198, 12 x 0x3c) |
| 104 + 36 i | 32 | `ScoringTable[i].bestScore` (+0x30) |
| 136 + 36 i | 4 | `ScoringTable[i].statsTable[0].achieved` (Bond moments: category 0 at +0x08 of the level's category table) |
| 104 + 36 count | 1 | `GameState.WeaponUpgradeRelated` (0x001f65d2, `GameState`+0x52) - rd U8 |

On load the score part goes through a temporary `SCORETABLE[20]` (0x4b0 bytes) and `PlayerStats[20]` (0xb4 each,
0xe10 bytes), zeroed, with only `statsTable`, `bestScore` and Bond moments filled; then
`PlrStats_SetupScoreTable(temp, count)` (0x000b1530) copies them by index into `ScoringTable` - see
[functions.md](functions.md) for what that wipes. Bond moments are 4 bits: values above 15 would be cut.

### `MPSG` - one player's multiplayer settings (33 bits, 13 bytes)

Player = slot; `MPSettings` at 0x0025fe38, `Player[]` 0x30 bytes each (`src/action/game/mp/multiplayer.h`).

| bit | width | field | rd |
|---|---|---|---|
| 64 | 1 | `Player[slot].SomeField2` (0x0025fe60 + 0x30 slot) - meaning unknown | U32 |
| 65 | 32 | `Player[slot].HealthModifier` (0x0025fe64 + 0x30 slot), signed | U32 |

### `GSET` - global options (327 bits, 49 bytes)

| bit | width | field | on load |
|---|---|---|---|
| 64 | 7 | music volume, `SFXMusicGetVolume()` (0x000c6360) | `SFXMusicSetVolume` (0x000c6350), after all pulls |
| 71 | 7 | SFX volume, `SFXGetVolume()` (0x000c5090) | `SFXSetVolume` (0x000c5060), last |
| 78 | 7 | language, `GetLanguage()` (0x0006d130) | dropped |
| 85 | 1 | `SubtitlesEnabled` (0x001f6614) | written directly (U32) |
| 86 | 32 | sound mode, `SFXGetMode()` (0x000c5430) | dropped |
| 118 | 1 | `IsWidescreen` (0x001f6610) | dropped |
| 119 | 32 | `MultiplayerLayout_LeftRightOrTopBtm` (0x001f660c) | written directly (U32) |
| 151 + 32 k, k = 0..6 | 32 each | zero | read into a scratch word, dropped |
| 375 | 16 | 4 (a version, by inference) | read (U16), never checked |

The make calls `GetLanguage`, `SFXMusicGetVolume`, `SFXGetVolume`, `SFXGetMode` in that order before pushing
anything (*disasm*). On the PS2 the reserved area holds the screen position and the load applies sound mode and
widescreen (*PS2*); the Xbox build leaves language, sound mode and widescreen to the dashboard.

### `CHET` - cheats (3 bits, 9 bytes)

| bit | width | field (`CheatInfo` at 0x001f65dc) | rd |
|---|---|---|---|
| 64 | 1 | `Immortal` (0x001f65dc) | U32 |
| 65 | 1 | `AllWeapons` (0x001f65e0) | U32 |
| 66 | 1 | `UnlimitedAmmo` (0x001f65e4) | U32 |

The load writes the whole dword (0 or 1); the make pushes only bit 0 of each.

### `BNUS` - bonus/unlock word (64 bits, 16 bytes)

| bit | width | field |
|---|---|---|
| 64 | 32 | high word of player 0's bonus (`menu_bonus[1]`, 0x0025d6ec) |
| 96 | 32 | low word (`menu_bonus[0]`, 0x0025d6e8) |

Read through `Menu_GetBonus(0)` (0x0007c660, PS2 name; returns `{lo, hi}` of `menu_bonus[2*player]`) - always
player 0, whatever the slot. The load calls `Menu_SetBonus(lo, hi, slot, 0)` (ours), which replaces player
`slot`'s word and rebuilds that player's upgrades. So a multiplayer load for player 2 sets player 2's bonus from a
block that a save always fills from player 0's.

## A decoded save (test vector)

`Release/saves/BOND.dat` as of 2 October 2026 01:21 (it changes whenever the game saves; keep a copy), 166 bytes,
written by the original LS code through our `psiSaveData` (*bytes*):

```
00000000: 504c 5253 0b00 0000 00f2 014d 5353 4e44  PLRS.......MSSND
00000010: 0000 00ff 0f00 000c 5253 0300 0300 0000  ........RS......
00000020: 006e 5103 0002 0000 0000 0000 0000 0000  .nQ.............
00000030: 0000 0000 0000 0070 4831 0000 0000 0000  .......pH1......
00000040: 2058 3c00 5000 0000 0000 0000 0000 004d   X<.P..........M
00000050: 5053 470d 0000 0001 0000 0000 4753 4554  PSG.........GSET
00000060: 3100 0000 4b32 4000 0000 0000 0000 0000  1...K2@.........
00000070: 0000 0000 0000 0000 0000 0000 0000 0000  ................
00000080: 0000 0000 0000 0000 0000 0002 0043 4845  .............CHE
00000090: 5409 0000 0006 424e 5553 1000 0000 0108  T.....BNUS......
000000a0: 0001 0800 0049                           .....I
```

| offset | block | length | decoded |
|---|---|---|---|
| 0 | `PLRS` | 11 | inverted 0, style 0, driving style 0, auto-aim SP 1, MP 0, manual-aim toggle 0, auto-switch 1, crouch 1, vibration 1, crosshair 1, flashing 1, HUD 0 |
| 11 | `MSSN` | 68 | status 0xfff, count 12, (best score, Bond moments) = (217938, 3), 0, (217454, 2), 0, 0, 0, 0, (201863, 0), 0, (247170, 5), 0, 0; upgrade flag 0 |
| 79 | `MPSG` | 13 | flag 1, health modifier 0 |
| 92 | `GSET` | 49 | music 75, SFX 100, language 0, subtitles 0, sound mode 1, widescreen 0, layout 0, reserved 0 x 7, version 4 |
| 141 | `CHET` | 9 | immortal 0, all weapons 1, unlimited ammo 1 |
| 150 | `BNUS` | 16 | hi 0x01000801, lo 0x49000008 |

Full-save size = 11 + 68 + 13 + 49 + 9 + 16 = **166** bytes for 12 levels,
which is what `LS_Load` asks `psiLoadData` for and what *LS_GetFullSaveSize* returns.

A Python reader, for checking a port's output:

```python
import struct
def blocks(d):
    o = 0
    while o + 8 <= len(d) and d[o:o+4] != b'\0\0\0\0':
        n = struct.unpack_from('<I', d, o + 4)[0]
        yield d[o:o+4].decode(), d[o:o+n]
        o += n
def bits(b, pos, n):          # LSB-first, as BIN_PullBits
    return sum(((b[(pos + i) >> 3] >> ((pos + i) & 7)) & 1) << i for i in range(n))
```
