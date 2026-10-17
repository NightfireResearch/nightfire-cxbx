# Worked examples (25 Sept 2026): proposals, nothing applied

Each proposal gives its evidence. None of it is in Ghidra yet: review first, then apply.

Evidence strength: **vtable** (same slot in both vtables) > **body** (both decompilations read side by side and
agree) > **size+order** (the retail sizes and the sheet order agree over a run) > **calls/strings** alone.

## A. Bond.obj to Scheduler.obj, Xbox 0x59900-0x5bb00 (`region-00059900-0005bb00.md`)

Compilation units come out in the same order on both platforms (Bond, EventManager, GameLoop, RealClock,
OptionParser, Scheduler), but the order inside GameLoop differs: sheet 2333, 2334, 2335, 2337, 2338; Xbox 2333,
2338, 2335, 2337, 2334.

| Xbox | proposal | evidence | confidence |
|---|---|---|---|
| FUN_0005b870 | `OptionParser::OptionParser(char *, char *)` (row 2364) | body: PS2 0x1790f8 has the same logic step for step: zero `this[0..1]`, strstr for the key at a line start, key must be followed by ' ' or '=', skip ` =#([`, length up to '\n' via strchr, trim `]` then `)` | high |
| FUN_00059ca0 | not a sheet name: MSVC STL `vector<T>::_Xlen` ("vector<T> too long" throw) | body | high, but it's an invented name |
| FUN_00059c30 | not a sheet name: a USingletonManager teardown (KillAll, then frees the vector) | body; callers Bond_StartUpSystem, Bond_CleanUp | medium, invented name |
| FUN_00059d20 | not a sheet name: static-local accessor (guard bit, zeroes 3 words, `atexit`) returning &DAT_001e47c0, the singleton manager | body | medium, invented name |
| FUN_0005b0a0 | not a sheet name: string assign(const char *) helper (strlen, then FUN_00013630) | body | low: leave unnamed |
| FUN_00059900 | unknown: strcpy into `this+0x4b4`, called from ApplicationMemoryHeapConfig | - | leave |

On Xbox, `GameLoop_LoadDrawUnloadScreens`, `GameLoop_DumpMainBigFile` (MainGameLoop calls StopUsingBigFile
directly), `Bond_SetExitToFrontEnd`, `SafePrintChannel` and the small EventManager/RegisterEvent functions are
inlined or missing, so an unnamed gap is usually *not* the missing sheet name.

**PS2 side** (for the sheet project): the retail sizes after `deleteSysFiles` match the sheet exactly, so
0x1790f8 = OptionParser::OptionParser (0x200), 0x1792f8 = ReadInteger (0x50), 0x179348 =
ReadUnsignedInteger (0x50), 0x179398 = ReadFloat (0x58), 0x1793f0 = ReadChar (0x40). The sheet gives
`deleteSysFiles` 0x780, but retail has 0x2b8 + three unnamed functions (0x400, 0x68, 0x60) adding up to 0x780.
**So a sheet size can include static functions the symbol file doesn't list.**

## B. AICharacterBond vtable (`vtable-AICharacterBond.md`)

The PS2 vtable (0x367e38, from the sheet) and the Xbox one (0x18a688) both have 33 slots. Slots 1, 2, 4 and 5 are
already named identically on both sides. The PS2 slots are named from the sheet: the AICharacter rows after
`SetCustomTarget` and the AICharacterBond rows after `~AICharacterBond` have exactly the retail sizes (8, 0x20,
8, 8 x 24 ...; 0x38, 0x48, 0x38, 0x108 ...), and their order is the declaration order the vtable follows (Bond
overrides at slots 7-9, 15, 18, 30, 32).

| slot | canonical | Xbox | proposal | confidence |
|---|---|---|---|---|
| 0 | `AICharacterBond::~AICharacterBond` | FUN_0001cc00 | MSVC *scalar deleting destructor* (calls 0x1cbf0, frees 0x150 if flag & 1). The real destructor is FUN_0001cbf0 (resets vptr, jumps to the base destructor 0x1c1d0) | high; naming convention to agree |
| 7, 9 | `AICharacterBond::DoInitial`, `::DoIdling` | FUN_0001d0c0 | **identical-code folding**: PS2 0x11a2e0 and 0x11a360 have identical block hashes; MSVC merged them. One name plus a plate comment listing both | high |
| 8 | `AICharacterBond::DoNeutral` | 0x1d090, **not a function in Ghidra** | create the function, then name it. Sets state 2, looks up an animation, calls 0x12640 (PS2: SetNewAnimation) | high |
| 15 | `AICharacterBond::DoArming` | FUN_0001d0e0 | vtable, size 0xd4 vs 0x108 | high |
| 18 | `AICharacterBond::DoFiring` | FUN_0001d1c0 | vtable, 0xdd vs 0x114 | high |
| 30 | `AICharacterBond::HandleInterrupts` | FUN_0001d2a0 | vtable, 0x284 vs 0x2a0 | high |
| 32 | `AICharacterBond::UpdateRotPos` | FUN_00023950 | vtable + body (GetRotPos into a local, then MATRIX4 mult into this+0x10); sits far from the other Bond methods, maybe folded with another class's identical copy | medium-high |
| 3 | `AICharacter::GetVehiclePtr` | dummyGetNullValue | folded return-0 stub; keep the shared name | - |
| 10-14, 16, 17, 19-29 | `AICharacter::DoWalking` ... `DoDead` | dummyNullFunction | folded empty stubs; keep | - |
| 31 | `AICharacter::IsTooFarAway` | Generic_FuncReturnsFalse | folded; keep | - |
| 6 | (PS2 0x119138 unnamed, 8 bytes) | FUN_00015770 | unknown; leave | - |

Also noticed: PS2 has two functions named `AICharacterBond::Init` (0x119cc0 and 0x119e20). By sheet order
and size, 0x119b98 and 0x119cc0 are probably the two `GetRotPos` overloads (rows 450-451), and 0x119cc0's name is
wrong.

## What this changes in the plan

1. **Vtables are the best automatic source.** 382 PS2 vtables have addresses in the sheet, and
   `tools/vtables_driving.json` has the Xbox ones. The Xbox also has virtuals Ghidra never made into functions.
2. **The PS2 side often needs infilling first** (sheet order + exact retail sizes), before a name can cross.
   Where sizes match exactly over a run, that is close to certain.
3. **Folding and MSVC-only functions** (scalar deleting destructors, STL throws, static accessors, SEH funclets)
   need their own conventions: one Xbox function can carry several canonical names, and some Xbox functions
   have no canonical name at all.
