# Function by function

Xbox addresses; PS2 names (the PS2 build's symbols) unless marked. Every function here is live original code.
"cdecl" means the caller pops. Where Ghidra's prototype is wrong the real convention comes from the disassembly.

## The drivers

### `LS_FlushStates` - 0x0006eb20, 16 bytes

`void LS_FlushStates(void)`. Writes 0 to `Load_State` (0x0021d790) and `Save_State` (0x0021d794), both as u16.
Callers: `Menu_UpdateMessageBox` (a save refused for want of a codename), `XBox_DoSaveFlow` (when the flow
finishes), `C_LBERROPTIONS_Handler` (the error box dismissed).

### `LS_FindBlockByLabel` - 0x0006eb30, 128 bytes (Xbox name; no PS2 symbol, inlined there)

**Usercall** (*disasm*): slot in `AX` (zero-extended), label (a dword, the four ASCII bytes little-endian) in
`EDI`, `ushort *lengthOut` on the stack (caller pops); returns the block pointer in `EAX` or 0. Ghidra shows
`(lengthOut, slot, label)` all on the stack, which is wrong.

```c
uchar *p = InBufData + slot * 0x1000;  uint offset = 0;
while (offset < 0x1000) {                      // (also "while p != 0", always true)
    uint label = p[0..3] (byte-wise, LE), length = p[4..7] (byte-wise, LE);
    if (label == wanted) { *lengthOut = (ushort)length; return p; }
    if (label == 0) break;
    offset += length;  p += length;
}
*lengthOut = 0;  return 0;
```

Quirks to keep or guard: a zero length with a non-zero, non-matching label never advances (infinite loop); the
header read at an offset just under 0x1000 reads up to 7 bytes into the next slot (or past `InBufData` for slot 3);
a length that walks past the slot is not caught until the next test. Callers: `LS_GetSaveStuff`,
`LS_GetLoadStuff`.

### `LS_LoadFromBuffer` - 0x0006ebb0, 96 bytes

`int __cdecl LS_LoadFromBuffer(uchar *addr, uint lenBytes)` (PS2: returns bool). If the first four bytes of
`addr` (read byte-wise) are all zero, returns 0. Otherwise zeroes `InBufData[0]` (0x400 dwords - slot 0 only),
copies `lenBytes` bytes from `addr` to `InBufData` with no bound (`rep movsd` then `rep movsb`), returns 1.

The only caller, `Boot_LoadPTPData` (0x00019db0), passes `(&PTPDATA, 0x12c0)`: `PTPDATA` (0x001d7e90) is the
0xa50-byte `sNightFireShared_tag` from the driving engine, followed in memory by `TmpBuff` (0x001d88e0), the copy
it was made from. So the copy is the PTP struct itself, codename first - not save blocks - and overruns
`InBufData[0]` by 0x2c0 bytes into `InBufData[1]` (still inside `InBufData`). The PS2 does the same with the same
constant (*PS2*). In effect it is a "the hand-over carries a codename" guard: a zero result skips the whole
restore. What it leaves in `InBufData[0]` would only matter to a later partial save from slot 0, and every save
uses mask 0xff. A faithful port copies the same 0x12c0 bytes (the overrun stays inside our own `InBufData` if it
is laid out as one 0x4000-byte array); see [related.md](related.md).

### `LS_GetSaveStuff` - 0x0006ec10, 192 bytes

`ushort __cdecl LS_GetSaveStuff(ushort mask, uint slot)` (slot used as u16). Returns the save size in `AX`.

```c
ushort size = 0;
for (i = 0; i < NumSaveBlocks; i++) {
    ushort len = 0;  uchar *src;
    if (SaveIFFBlocks[i].bitfield & mask) src = SaveIFFBlocks[i].makeFunction(label_dword, slot, &len);
    else                                  src = LS_FindBlockByLabel(slot, label_dword, &len);   // the last loaded copy
    if (src) { memcpy(DstData + slot * 0x1000 + size, src, len); size += len; }
}
return size;
```

The make function gets the slot as a full 32-bit push (`EBP`); the length local is a zeroed dword of which the
callee writes the low half. No check that `size` stays under 0x1000 (the six blocks total 166 bytes, so it does).
Callers: `LS_Save` (mask, slot), `LS_Load` (0xff, 0 - only for the size), *LS_GetFullSaveSize* (0xff, 0).

### `LS_GetLoadStuff` - 0x0006ecd0, 96 bytes

**Usercall** (*disasm*): `ushort mask` on the stack, slot in `BX` (Ghidra's `BX` parameter is right; its
`CONCAT22(unaff, BX)` is the upper half of `EBX`, masked off by `LS_FindBlockByLabel`). For each table entry whose
bit is in `mask`, finds the block in `InBufData[slot]` and, if found, calls `loadFunction(block, slot)` (cdecl,
slot pushed as all of `EBX`). Blocks not in the mask or not in the buffer are left alone. Only caller: `LS_Load`,
on a successful load.

### `LS_Save` - 0x0006f880, 208 bytes

`uint __cdecl LS_Save(char *codename, uint slot, ushort mask)` (PS2: `ushort slot`). Returns the state it found
on entry (`Save_State`, u16, zero-extended).

```c
old = Save_State;
if (old == 0) {
    slot16 = slot; mask16 = mask;
    size = LS_GetSaveStuff(mask, slot);
    psiSaveData(DstData + slot * 0x1000, size, codename);        // 0x000dfe10, ours
    memcpy(InBufData + slot * 0x1000, DstData + slot * 0x1000, size);   // even if the save fails
    Save_State = 2;
} else if (old != 2) return old;
r = psiSavingData();       // FUN_000dfc30 (invented name): 2 if psiInternalLoadingDataState == 4 (written), else 4
if (r == 2) Save_State = 7;  else if (r == 4) Save_State = 8;
return old;
```

The poll never returns anything but 2 or 4, so a save goes 0 → 2 → 7 or 8 inside the first call; the caller sees
0, then 7 or 8 on the next call (the return value lags one call). It stays at 7/8 until `LS_FlushStates`. Our
`psiSaveData` sets 4 on success and 5 on failure, matching the original's codes, so 7 = saved, 8 = failed. Only
caller: `Menu_UpdateMessageBox` op 1.

### `LS_Load` - 0x0006f960, 208 bytes

`uint __cdecl LS_Load(char *codename, ushort slot, ushort mask)`. Returns the state found on entry.

```c
old = Load_State;
if (old == 0) {
    slot16 = slot; mask16 = mask;
    size = LS_GetSaveStuff(0xff, 0);        // the size of a full save of the current state; rebuilds DstData[0]
    memset(InBufData + slot * 0x1000, 0, 0x1000);
    psiLoadData(InBufData + slot * 0x1000, &size, codename);    // 0x000dfbe0, ours
    Load_State = 1;
} else if (old != 1) return old;
r = psiLoadingData();      // 0x000dfc10: 2 if psiInternalLoadingDataState == 1 (read), else 4
if (r == 2) { Load_State = 7; LS_GetLoadStuff(mask, slot); }    // slot passed in EBX
else if (r == 4) Load_State = 8;
return old;
```

Note the busy code is 1 here and 2 in `LS_Save`. The requested size is today's full-save size, not the file's;
ours reads up to that many bytes and reports success on any non-empty file. The `LS_GetSaveStuff(0xff, 0)` call
rebuilds `DstData[0]` as a side effect (harmless; nothing reads it until the next save rebuilds it again). Only
caller: `Menu_UpdateMessageBox` op 0.

### *LS_GetFullSaveSize* (invented) - `FUN_0006f950`, 16 bytes, Xbox only

`ushort LS_GetFullSaveSize(void)` = `LS_GetSaveStuff(0xff, 0)` (Ghidra types the result `char *`). Callers:
`FUN_0008fcc0` (*Menu_CheckSaveSpace*, invented: compares `SaveDrive_BlocksFor(size)` with
`SaveDrive_FreeBlocks()`, and the saved-codename count with 0xffc, and puts up the "not enough space" / "too many
codenames" box) and `FUN_0008fd90` (sends the player to the dashboard with the blocks needed,
`WriteStateFileAndLaunch`). Same side effect on `DstData[0]`.

## The make functions

All: `uchar *__cdecl LS_MakeX(uint label, ushort slot, ushort *lengthOut)`. Each writes the label bytes into its
own static buffer ([blocks.md](blocks.md)), sets a local bit cursor to 0x40, pushes its fields with
`BIN_PushBits(buf, &cursor, value, width)`, writes the length `(cursor + 7) >> 3` into bytes 4-7 and the low 16
bits into `*lengthOut`, and returns the buffer. Field lists are in [blocks.md](blocks.md); here only what differs.

| function | address | uses slot | reads | notes |
|---|---|---|---|---|
| `LS_MakePlrSettings` | 0x0006ed30 | yes, `PlayerInputs[slot]` | 12 fields of `PlayerInput` | shorts pushed sign-extended (`MOVSX`) |
| `LS_MakeMission` (`FUN_0006f110`) | 0x0006f110 | no | `Menu_GetNightfireStatus`, `PlrStats_GetScoreTable`, `GameState+0x52` | walks the score table with a pointer at each entry's +0x18 (`statsTable`), stride 0x3c: pushes `entry+0x30` and `*(statsTable)+8` |
| `LS_MakeMPSettings` | 0x0006f450 | yes, `MPSettings.Player[slot]` (stride 0x30, computed `slot*3 << 4`) | `+0x28`, `+0x2c` | |
| `LS_MakeGlobalSettings` | 0x0006f550 | no | `GetLanguage`, `SFXMusicGetVolume`, `SFXGetVolume`, `SFXGetMode`, 3 globals | values pushed in the order music, SFX, language, ... (getters called first) |
| `LS_MakeCheats` | 0x0006f000 | no | `CheatInfo` +0, +4, +8 | |
| `LS_MakeBonus` | 0x0006f340 | **no** - always `Menu_GetBonus(0)` | `menu_bonus[0..1]` | the only one that zeroes `*lengthOut` first; pushes hi then lo |

## The load functions

All: `void __cdecl LS_LoadX(uchar *block, ushort slot)` (PS2 signature; the Xbox ones that ignore the slot are
typed with fewer parameters by Ghidra, harmlessly under cdecl). Each starts a cursor at 0x40 and pulls in the
same order as the make.

| function | address | uses slot | writes |
|---|---|---|---|
| `LS_LoadPlrSettings` | 0x0006eee0 | yes | `PlayerInputs[slot]` fields via `BIN_PullBits_U8` / `_U16` |
| `LS_LoadMission` | 0x0006f230 | no | Nightfire status, score tables, `GameState+0x52` (below) |
| `LS_LoadMPSettings` | 0x0006f500 | yes | `MPSettings.Player[slot]` +0x28, +0x2c (U32) |
| `LS_LoadGlobalSettings` | 0x0006f720 | no | `SubtitlesEnabled`, layout (during the pulls), then `SFXMusicSetVolume(music)`, `SFXSetVolume(sfx)`; language, mode, widescreen, reserved and version are pulled into locals and dropped |
| `LS_LoadCheats` | 0x0006f0c0 | no | `CheatInfo` three dwords (U32) |
| `LS_LoadBonus` | 0x0006f3f0 | yes | `Menu_SetBonus(lo, hi, slot, 0)` (pulls hi then lo) |

### `LS_LoadMission` in detail

Frame of 0x12d4 bytes (hence `__alloca_probe`, 0x000ee460, at entry).

1. Pull 32 bits → `Menu_SetNightfireStatus` (0x000762d0, ours).
2. Zero `SCORETABLE temp[20]` (300 dwords) and `PlayerStats stats[20]` (900 dwords, stride 0xb4).
3. Pull the 8-bit count. For i < count: `temp[i].statsTable = &stats[i]`; pull 32 bits into `temp[i].bestScore`
   (+0x30); pull 4 bits into `stats[i]+8` (Bond moments' `achieved`). **No check that count <= 20.**
4. Pull 1 bit (U8) into `GameState.WeaponUpgradeRelated` (0x001f65d2).
5. `PlrStats_SetupScoreTable(temp, count)` (0x000b1530, live, player.PlrStat). For each i < count it copies, by
   index, into `ScoringTable[i]` (0x0017f198 + 0x3c i) and that level's category table
   (`ScoringTable[i].statsTable`):
   - for each of the nine 0x14-byte categories: `achieved` (+8), `rating` (+0xc), `points` (+0x10) - all zero
     from the temporary except category 0's `achieved` (Bond moments);
   - `SCORETABLE` +0x1c..+0x30: `timeTaken`, `parTime`, `difficultyMultiplier`, `difficultyBonus`, `baseScore`
     (all zero) and `bestScore`.
   It does not touch `levelHashcode`, the thresholds, `statsTable`, `score` (+0x34), `isAction`, or the
   categories' `target` and `maxPoints`.

A port that keeps calling the original `PlrStats_SetupScoreTable` must build the temporary in the same shape
(`SCORETABLE` 0x3c with `statsTable` at +0x18, `PlayerStats` stride 0xb4 with Bond moments at +8); one that
inlines it should reproduce exactly the fields listed.

## The bit helpers (engine.util, 0x0006b1d0-0x0006b370)

- `BIN_PushBits(uchar *buf, uint *cursor, uint value, byte width)` (0x0006b1d0, 128): for each of `width` bits,
  LSB first, sets or clears bit `cursor & 7` of `buf[cursor >> 3]` from `(value >> i) & 1`, then
  `*cursor += width`. Width 0 writes nothing.
- `BIN_PullBits(uchar *buf, uint *cursor, uint out[2], uchar width)` (0x0006b250, 144): gathers `width` bits LSB
  first into `out[0]`; `out[1]` is the OR of each gathered bit's arithmetic `>> 31`, i.e. 0xffffffff only if bit
  31 was pulled and set - a sign word nobody reads, but it is **written**, so the output must have 8 bytes.
  `*cursor += width`.
- `BIN_PullBits_U32 / _U8 / _U16(buf, cursor, out, width)` (0x0006b2e0 / 0x0006b310 / 0x0006b340, 48 each): call
  `BIN_PullBits` into an 8-byte local and store the low 4 / 1 / 2 bytes to `*out`.

Their only callers are the make/load functions above (*xrefs*).

## Callers outside the subsystem (live, for reference)

- `Menu_UpdateMessageBox` (0x000757d0): `(managerNum, ushort mask, byte slot)`. Returns at once unless `ls.busy`;
  guards against re-entry with a static flag; op 0 `LS_Load(ls.codename, slot, mask)`; op 1 needs `ls+0x3d` and a
  non-empty codename, else sets `ls+0x3a = 1`, clears `busy`, calls `LS_FlushStates` and returns; otherwise
  `LS_Save(ls.codename, slot, mask)` and `ls+0x3b = 1`; op 2/4 `Menu_EnumSaves(managerNum, 0/1)`; op 3 delete
  (a profiling hook where `psiDeleteData` was, then `psiDeletingData`). Then `XBox_DoSaveFlow(manager +
  managerNum, state, &ls)`.
- *Menu_StartLS* (invented, `FUN_0007fef0`, 176 bytes): fills `ls` (operation, slot, return page, flags, codename
  copied to `ls.codename` at 0x0017d558), sets `busy`, and calls `Menu_UpdateMessageBox(manager, mask, slot)`.
  Callers `C_SBNFCN_Handler`, `C_SBCNSELECT_Handler` (load, 0xff) and `C_RBMPCNAME_Handler` (load, 0x25/0x2d, the
  player).
- `XBox_DoSaveFlow` (0x0008f3f0, 2 KB): the message boxes. Treats states 0-3 as in progress (texts chosen from
  `psiInternalLoadingDataState`), 4/6 and 7/8 as finished; calls `LS_FlushStates` when the flow ends.
