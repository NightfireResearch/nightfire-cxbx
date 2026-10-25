# Secret Unlocks (cheat codes) - action engine, Xbox EU `default.xbe`

Researched 29 Sept 2026 from the Ghidra program `/Xbox_EU/default.xbe` (read only) and the retail disc.
Addresses are Xbox virtual addresses.

## Where it lives

The Secret Unlocks page is **not a page of its own**. It is the codename name-entry page `P_CNNAME`
(0x40000020) running in "cheat mode", with the on-screen keyboard `C_KEYBOARD` (0x10000074).

Path through the menus: Main menu -> Codenames (`C_GOCODENAMES`) -> pick a codename (`P_CNSELECT`
0x4000001b) -> codename menu `P_CNMENU` (0x4000001d), iris wheel `C_SBCNOPTIONS` -> item 0 **"Secret Unlocks"**
-> `P_CNNAME` in cheat mode. The page is always there: the wheel item's `enabled` is 1 in the data.

You type a code of up to 8 characters and press the keyboard's Done key. On a match the unlock is applied
straight away to the in-memory codename (bonus bits, mission-enabled flags, gadget upgrades) and an "Unlock
successful" box appears. A wrong code does nothing: no message and no sound, and the typed text stays in the
field. The unlock only persists if the codename is then saved (P_CNMENU "Save Codename"). Leaving P_CNMENU
with B asks "Are you sure you want to exit and lose your changes?" because `cn_modified_flag` is set.

Nothing is hashed or obfuscated. The 52 codes are plain ASCII in `.rdata` (0x160d90-0x160f68). They are
copied into a 52-entry table on the stack every time a code is checked (Menu_SpecialCodenameCheck).

## Functions on the path

| Address | Name | Role |
|---|---|---|
| 0x8c7f0 | `C_SBCNOPTIONS_Handler` | Codename-menu wheel; on Select, item 0 -> `GoPage(P_CNNAME)` |
| 0x8c5f0 | `P_CNMENU_Handler` | Codename menu; tells the entry routes apart by the previous page |
| 0x8ca10 | `P_CNNAME_Handler` | Sets `cheat_mode` on page entry, fills in the title, clears the buffer, sets up the keyboard |
| 0x8cc50 | `C_KEYBOARD_Handler` | Edits the text buffer; on Done in cheat mode calls the code check |
| 0x7d110 | `Menu_SpecialCodenameCheck` | Builds the code table, runs `strcmp`, applies the effect |
| 0x7cfb0 | `Menu_UpgradeCheat` | Works out a bonus mask that holds at least N upgrades of one gadget |
| 0x76240 | `Menu_GetNightfireStatus` | Packs `sp_level[i].enabled` into a 12-bit mask |
| 0x762d0 | `Menu_SetNightfireStatus` | The reverse (0 -> 3); used by `LS_LoadMission` |
| 0x7cb10 | `Menu_SetBonus` | Stores the 64-bit bonus, then rebuilds the upgrades (`ResetUpgrade` 0xb9e60, `Set_Upgrade` 0xb9e30) |
| 0xb1740 | `PlrStarts_ProcessRewardCounter` | Looks up a reward slot: tests its bit, or sets it |
| 0x7f3a0 | `Menu_CreateOptionBox` | Message box ("Unlock successful") |
| 0x6f110 | `LS_MakeMission` (FUN_0006f110) | Save: 32-bit mission mask from `Menu_GetNightfireStatus` |
| 0x6f230 | `LS_LoadMission` | Load: `Menu_SetNightfireStatus(mask)` |
| 0x6f340 / 0x6f3f0 | `LS_MakeBonus` / `LS_LoadBonus` | Save/load `bonus[0]` as hi32, lo32 |
| 0x6f000 / 0x6f0c0 | `LS_MakeCheats` / `LS_LoadCheats` | **Not related**: save the `CheatInfo` debug flags (Immortal, AllWeapons, UnlimitedAmmo), one bit each |

`Menu_SpecialCodenameCheck` has only one caller, `C_KEYBOARD_Handler`. The address also shows up as DATA
inside `Menu_SetupCredits`, but that is a false reference.

## Globals

| Address | Type / name | Meaning |
|---|---|---|
| 0x245200 | `char kbd_text[]` | Keyboard text buffer. Cheat mode allows 8 characters plus NUL |
| 0x17d608 | `u8 del_when_keypressed` | 1 = the next key press (other than Done) clears the buffer first |
| 0x25d7de | `u8 cheat_mode` | 1 = P_CNNAME was entered from P_CNMENU (Secret Unlocks) |
| 0x25d7dd | `u8 cn_modified_flag` | The codename has unsaved changes |
| 0x25d6e8 / 0x25d6ec | `u32 bonus[2*n]` (lo, hi) | 64-bit reward mask per codename slot; `bonus[p]` = `*(u64*)(0x25d6e8 + 8p)`. The code check uses slot 0 |
| 0x25d78d | `u8` | In `Menu_SetBonus`: when set, always replaces (the OR path is off). The check passes or=0, so it replaces either way |
| 0x17c580 | `M_ITEM sp_level[12]` | Single-player mission list; `.enabled` at +0x10 (0x17c590 + 24*i), `.identifier` = level hashcode |
| 0x17f478 | `RewardTableEntry RewardsTable[12]` | 64 bytes each: level hashcode, `upgrades[5]` {short objId; u16 type} (slot 0 unused, slots 1-4 used), 5 name labels, 5 sprites |
| 0x17cec8 | `M_ITEM cn_options[7]` | Codename-menu wheel items (0 = Secret Unlocks, 6 = Save Codename) |
| 0x17d540 | `ls` | Codename load/save state (field2 @+8 = page to return to, field4 @+0x10 = slot, codename copy @+0x18) |
| 0x25ed68 | `u16[4]` | Strings for keys 100-103: 0xc4, 0xc2, 0xc6, 0xc0 (A-umlaut, A-circumflex, AE, A-grave) |

Text labels used: `SECRET_UNLOCKS` 0x10002d4 "Secret Unlocks" (the page title, and the wheel item title),
0x10002d7 "Enter secret codes to unlock missions and characters." (the wheel item description),
`UNLOCK_SUCCESS` 0x100029d "Unlock successful", `ENTER_NEW_CODENAME` 0x1f5 (the title when not in cheat
mode), 0x31f "This codename already exists..." (not cheat mode), 0x10002d3 "Are you sure you want to exit
and lose your changes?".

## Handlers, message by message

Messages: 0x4b = Select (A/Start), 0x4c = page entered (`arg2` = hashcode of the previous page),
0x50 = per-frame update, 0x5f = return to the previous page, 0x44 = GoPage, 0x39 = get a sub-control,
0x18 = set text, 0x2f = ask a key control for its character string, 0x22 = move the manager's cursor.
0x61 also reaches the keyboard's select path; its name is unknown.

### C_SBCNOPTIONS_Handler (0x8c7f0)
- 0x49 / 0x54: `Menu_UpdateWheel(mgr, ctl, cn_options, 0x100000ff, 0x100001a5, 0x100001ec, 0x10000109, msg==0x49)`.
- 0x51: `__Menu_SendMessage(ctl, 0x27, 0, 6)`.
- 0x4b: `sel = __Menu_SendMessage(ctl, 0x40 /*GetWheelValue*/, 0, 0) & 0xff`. Then
  0 -> GoPage `P_CNNAME` (0x40000020), 1 -> `P_CNCONTROLS` 0x40000022, 2 -> 0x4000003e, 3 -> `P_CNOPTIONS`
  0x4000002d, 4 -> `P_CNMPOPTIONS` 0x4000002e, 5 -> `P_CNAVOPTIONS` 0x40000031, 6 -> save the codename.

### P_CNNAME_Handler (0x8ca10)
- 0x4c (entered):
  ```
  cheat_mode = (arg2 == P_CNMENU);            // came from the codename menu -> Secret Unlocks
  ls.field0 = 0; del_when_keypressed = 1;
  if (cheat_mode) { kbd_text[0] = 0;  Send(mgr, 0x10000242 /*title*/, 0x18, Txt_BindLabel(0x10002d4), 0); }
  else            { Send(mgr, 0x10000242, 0x18, Txt_BindLabel(0x1f5), 0); strcpy(kbd_text, "BOND"); }
  Send(mgr, 0x10000075 /*text field*/, 0x18, kbd_text, 0);
  done = SendEx(mgr, C_KEYBOARD, 0x3ea, 0x39, 0, 0);   // the Done key's control
  Manager_SendMessage(&manager[mgr], 0x22, 0, done);   // cursor starts on Done
  *(u32*)0x25ed68 = 0x00c200c4; *(u32*)0x25ed6c = 0x00c000c6;
  for k in 0..3: SendEx(mgr, C_KEYBOARD, 100+k, 0x18, 0x25ed68 + 2k, 0);   // accented keys
  ```
- 0x50 (update): `Menu_UpdateMessageBox(...)`; `r = Menu_UpdateOptionBox(&type)`.
  - type 4 ("codename exists, overwrite?", not cheat mode) and r == 1: overwrite, set `ls` for a save
    (field0=1, field4=999, field2=P_CNMENU, field3=1, field8=1, field9=0, field5=0, field17=0), update the message box.
  - type 0xc ("Unlock successful") and r != 0: `Manager_SendMessage(&manager[mgr], 0x5f, 0, 0)` returns to P_CNMENU.

### C_KEYBOARD_Handler (0x8cc50)
Only 0x4b and 0x61 do anything; everything else returns true. `key = ctl->field_0x20` is the key's identifier
from the menu data: 1000 = Delete, 1001 = Space, 1002 = Done, anything else is a character key.
```
if (key != 1002 && del_when_keypressed) {
    kbd_text[0] = 0; del_when_keypressed = 0;
    if (key == 1001) return true;              // Space only clears
}
switch (key) {
case 1000: kbd_text[strlen(kbd_text)-1] = 0; break;           // no check for an empty buffer
case 1001: if (strlen(kbd_text) < 8) strcat(kbd_text, " "); break;
case 1002:
    if (cheat_mode) {
        if (Menu_SpecialCodenameCheck(kbd_text)) {
            Menu_CreateOptionBox(mgr, 0x100029d /*Unlock successful*/, 0xc, 1, 0);
            cn_modified_flag = 1;
        }                                        // no match: nothing happens
        return true;
    }
    /* not cheat mode (naming a codename): FUN_00076330 checks the name (a bad one shows a type-6 box and sets
       del_when_keypressed = 1), copies it to ls.codename, FUN_00075f40 checks for a duplicate
       (0x31f box, type 4), otherwise PlrStats_ResetScoring and set ls for a save (as above) */
    break;
default:   // character key
    if (strlen(kbd_text) < 8) { const char *s = (char*)__Menu_SendMessage(ctl, 0x2f, 0, 0); strncat(kbd_text, s, 1); }
}
return true;
```
The check is case-sensitive (plain `strcmp`), so codes must match the keyboard's upper-case letters and
the spaces exactly ("HUGE EGO", "ZERO G", "Q LAB", "AU PP7", "AU P2K", "NUMBER 1").

### P_CNMENU_Handler (0x8c5f0), the parts that matter here
- 0x4c: if the previous page is neither `P_CNSELECT` nor (`P_CNNAME` with cheat_mode 0), it only restarts the
  iris (so coming back from Secret Unlocks keeps `cn_modified_flag`). Otherwise it clears `cn_modified_flag`,
  selects item 7 of the wheel, and puts the codename name into label 0x100001f9.
- 0x6b (back): if `cn_modified_flag`, `Menu_CreateOptionBox(mgr, 0x10002d3, 0xd, 1, 1)` and `*(int*)arg2 = -2`.
- 0x50: when the box returns 3 -> 0x5f back.

## Menu_SpecialCodenameCheck (0x7d110): exact behaviour

```
bool Menu_SpecialCodenameCheck(const char *in)
{
    u64  b  = bonus[0];                 // lo = *(u32*)0x25d6e8, hi = *(u32*)0x25d6ec
    u32  lo = (u32)b, hi = (u32)(b >> 32);
    u32  m  = Menu_GetNightfireStatus(); // bit i = sp_level[i].enabled
    u64  U[12] = {                       // Menu_UpgradeCheat(b, objId, count), all computed up front
      UpgradeCheat(b,0,1), UpgradeCheat(b,3,1), UpgradeCheat(b,6,1), UpgradeCheat(b,9,1),
      UpgradeCheat(b,0,2), UpgradeCheat(b,12,1), UpgradeCheat(b,0,3), UpgradeCheat(b,15,1),
      UpgradeCheat(b,18,1), UpgradeCheat(b,21,1), UpgradeCheat(b,9,2), UpgradeCheat(b,24,1) };
    struct { char code[32]; u32 missions; u32 pad; u64 bonus; u8 pad2[8]; } T[52];  // 56 bytes each
    ... fill T from the table below ...
    for (i = 0; i < 52; i++)
        if (strcmp(in, T[i].code) == 0) {
            u32 mm = T[i].missions ? T[i].missions : 3;
            for (j = 0; j < 12; j++) sp_level[j].enabled = (mm >> j) & 1;   // bytes at 0x17c590 + 24*j
            Menu_SetBonus((u32)T[i].bonus, (u32)(T[i].bonus >> 32), 0, 0); // replace bonus[0], rebuild upgrades
            return true;                                                    // returned as EAX=1, EDX=0
        }
    return false;
}
```
Every entry writes both fields. A mission code leaves the bonus as it was (`bonus = b`), and a bonus code
leaves the missions as they were (`missions = m`). So each code changes only one of the two.

**Mission and skin codes use XOR, so they toggle.** Typing POWDER when Alpine Escape is already open
locks it again. Typing BOWLER twice takes Oddjob away again. PASSPORT, PARTY and GAMEROOM use OR/set and
the upgrade codes only add, so those never take anything away. If every mission bit ends up clear, the
missions are forced to 3 (Paris Prelude and The Exchange stay open).

### Menu_UpgradeCheat (0x7cfb0)
`u64 Menu_UpgradeCheat(u64 bonus, u32 objId, u8 count)` (cdecl; the caller pushes count, objId, hi, lo):
```
owned = 0;
for lvl 0..11, slot 1..4: PlrStarts_ProcessRewardCounter(&bonus, sp_level[lvl].identifier, slot, 0, &ri);
                          if (ri.hasMedal && ri.objType == 4 /*WeaponUpgrade*/ && ri.objId == objId) owned++;
if (owned < count)
  for lvl 0..11, slot 1..4: ProcessRewardCounter(&bonus, hash, slot, 0, &ri);
                            if (ri.objType == 4 && !ri.hasMedal && ri.objId == objId)
                              { ProcessRewardCounter(&bonus, hash, slot, 1 /*set*/, &ri); owned++; }
                            if (owned == count) return bonus;
return bonus;
```
(The lookup of the level hashcode by index goes through `sp_level[lvl].identifier`, or -1 if there is none.)

### How the bonus bits work (PlrStarts_ProcessRewardCounter, 0xb1740)
Each reward slot has a bit in the 64-bit bonus. For cards, MP characters, modes and modifiers the bit is
`objId`. For a weapon upgrade the bit is `objId + k`, where k is the number of earlier table slots with
the same upgrade objId. So a gadget that can be upgraded 3 times owns bits objId..objId+2, which is why
the upgrade ids go up in threes. When set != 0 the slot's bit is ORed in. For upgrades,
`upgradeLevel` = the number of owned bits for that objId.

RewardsTable (0x17f478), slots 1-4 per level, as bit numbers:

| Level | Card | Slot 2 | Upgrade | MP skin |
|---|---|---|---|---|
| 0 Paris Prelude | 30 Dominique | 43 Jaws | 24 Missile | 52 Renard |
| 1 The Exchange | 26 Zoe | 38 Oddjob | 0 Pistol (bit 0) | 45 Baron Samedi |
| 2 Alpine Escape | 27 Mil. snowmobile | 56 Assassination | 3 Grapple | 53 Max Zorin |
| 3 Enemies Vanquished | 28 Vanquish | 42 Scaramanga | 6 Camera | 48 May Day |
| 4 Double Cross | 29 Mayhew | 57 Uplink | 9 Sniper (bit 9) | 46 Xenia |
| 5 Night Shift | 35 Kiko | 58 Team KOTH | 0 Pistol (bit 1) | 50 Christmas Jones |
| 6 Chain Reaction | 31 Rook | 40 Wai Lin | 12 Dart gun | 47 Goldfinger |
| 7 Phoenix Fire | 32 Alura | 59 Demolition | 0 Pistol (bit 2) | 54 Drake (suit) |
| 8 Deep Descent | 33 Vanquish sub | 41 Nick Nack | 15 Decryptor | 49 Elektra King |
| 9 Island Infiltration | 34 Ultralight | 60 Protection | 18 Stunner | 39 Bond (tux) |
| 10 Countdown | 36 Drake | 62 Explosive scenery (MP modifier) | 21 Laser | 51 Pussy Galore |
| 11 Equinox | 37 James Bond | 61 GoldenEye Strike | 9 Sniper (bit 10) | 55 Bond (space suit) |

Bit 44 is not used. Bits 38-55 are MP skins, 56-61 MP scenarios, 62 the MP modifier.

## The code table

"hi" means the high 32 bits of the bonus (bit n of hi = bonus bit 32+n). "m" is the current mission mask,
bit i = `sp_level[i]`. The table index is the order of the `strcmp` (the first match wins; there are no
duplicate strings). The strings are in `.rdata`, at 0x160f5c (PASSPORT) going down to 0x160d90 (LAUNCH).

| # | Code | Effect | Stored as |
|---|---|---|---|
| 0 | PASSPORT | All missions open | missions = 0xffffffff |
| 1 | POWDER | Toggle 2 Alpine Escape | m ^ 0x004 |
| 2 | TRACTION | Toggle 3 Enemies Vanquished | m ^ 0x008 |
| 3 | BONSAI | Toggle 4 Double Cross | m ^ 0x010 |
| 4 | HIGHRISE | Toggle 5 Night Shift | m ^ 0x020 |
| 5 | MELTDOWN | Toggle 6 Chain Reaction | m ^ 0x040 |
| 6 | FLAME | Toggle 7 Phoenix Fire | m ^ 0x080 |
| 7 | AQUA | Toggle 8 Deep Descent | m ^ 0x100 |
| 8 | PARADISE | Toggle 9 Island Infiltration | m ^ 0x200 |
| 9 | BLASTOFF | Toggle 10 Countdown | m ^ 0x400 |
| 10 | VACUUM | Toggle 11 Equinox | m ^ 0x800 |
| 11 | PARTY | All MP skins (bits 38-55 except 44) | hi \|= 0x00ffefc0 |
| 12 | BOWLER | Toggle Oddjob (38) | hi ^= 0x40 |
| 13 | BLACKTIE | Toggle Bond tux (39) | hi ^= 0x80 |
| 14 | MARTIAL | Toggle Wai Lin (40) | hi ^= 0x100 |
| 15 | BITESIZE | Toggle Nick Nack (41) | hi ^= 0x200 |
| 16 | JOELWADE | Toggle Nick Nack (41), same as BITESIZE | hi ^= 0x200 |
| 17 | ASSASSIN | Toggle Scaramanga (42) | hi ^= 0x400 |
| 18 | DENTAL | Toggle Jaws (43) | hi ^= 0x800 |
| 19 | NUMBER 1 | Toggle Drake suit (54) | hi ^= 0x400000 |
| 20 | VOODOO | Toggle Baron Samedi (45) | hi ^= 0x2000 |
| 21 | JANUS | Toggle Xenia Onatopp (46) | hi ^= 0x4000 |
| 22 | MIDAS | Toggle Goldfinger (47) | hi ^= 0x8000 |
| 23 | BADGIRL | Toggle May Day (48) | hi ^= 0x10000 |
| 24 | SLICK | Toggle Elektra King (49) | hi ^= 0x20000 |
| 25 | NUCLEAR | Toggle Christmas Jones (50) | hi ^= 0x40000 |
| 26 | CIRCUS | Toggle Pussy Galore (51) | hi ^= 0x80000 |
| 27 | HEADCASE | Toggle Renard (52) | hi ^= 0x100000 |
| 28 | BLIMP | Toggle Max Zorin (53) | hi ^= 0x200000 |
| 29 | HUGE EGO | Toggle Max Zorin (53), same as BLIMP | hi ^= 0x200000 |
| 30 | ZERO G | Toggle Bond space suit (55) | hi ^= 0x800000 |
| 31 | GAMEROOM | All MP scenarios (56-61) | hi \|= 0x3f000000 |
| 32 | TARGET | Toggle Assassination (56) | hi ^= 0x1000000 |
| 33 | TRANSMIT | Toggle Uplink (57) | hi ^= 0x2000000 |
| 34 | TEAMWORK | Toggle Team King of the Hill (58) | hi ^= 0x4000000 |
| 35 | TNT | Toggle Demolition (59) | hi ^= 0x8000000 |
| 36 | GUARDIAN | Toggle Protection (60) | hi ^= 0x10000000 |
| 37 | ORBIT | Toggle GoldenEye Strike (61) | hi ^= 0x20000000 |
| 38 | BOOM | Toggle Explosive Scenery (62) | hi ^= 0x40000000 |
| 39 | Q LAB | All gadget upgrades | U[10]\|U[11]\|U[9]\|U[8]\|U[7]\|U[6]\|U[5]\|U[2]\|U[1] (sniper x2, missile, laser, stunner, decryptor, pistol x3, dart, camera, grapple) |
| 40 | AU PP7 | Pistol upgrade 1 | U[0] = UpgradeCheat(b,0,1) |
| 41 | LIFTOFF | Grapple upgrade | U[1] (3,1) |
| 42 | SHUTTER | Camera upgrade | U[2] (6,1) |
| 43 | SCOPE | Sniper upgrade 1 | U[3] (9,1) |
| 44 | P2000 | Pistol upgrade 2 | U[4] (0,2) |
| 45 | SLEEPY | Dart gun upgrade | U[5] (12,1) |
| 46 | AU P2K | Pistol upgrade 3 | U[6] (0,3) |
| 47 | SESAME | Decryptor upgrade | U[7] (15,1) |
| 48 | ZAP | Stunner upgrade | U[8] (18,1) |
| 49 | PHOTON | Laser upgrade | U[9] (21,1) |
| 50 | MAGAZINE | Sniper upgrade 2 | U[10] (9,2) |
| 51 | LAUNCH | Missile upgrade | U[11] (24,1) |

The upgrade names come from the reward labels ("Upgrade - Pistol" etc.). The reward table does not name
the three pistol levels. PP7 -> P2K -> gold is inferred from the codes. Entries 50 and 51 do not show up in
the Ghidra decompile, which types the stack array as `SpecialCodenameEntry[50]`. They are there in the
disassembly (0x7e8b9-0x7e9da, strings 0x160d98 "MAGAZINE" and 0x160d90 "LAUNCH"), and the compare loop
runs while `i < 0x34`. "EUROCOM" (0x160d84) sits next to the table in `.rdata` but is not one of the codes.

Where it is stored: missions -> the `sp_level[i].enabled` bytes (0x17c590 + 24*i), saved in the codename
save's mission block (`LS_MakeMission`: the first 32 bits = `Menu_GetNightfireStatus()`). Bonus -> `bonus[0]`
0x25d6e8/0x25d6ec, saved by `LS_MakeBonus` (hi32 then lo32; loaded by `LS_LoadBonus` -> `Menu_SetBonus`).
Upgrades -> rebuilt from the bonus by `Menu_SetBonus` (`ResetUpgrade(0)`, then `Set_Upgrade(0, objId, level)`
for every owned type-4 slot).

## Reimplementation outline

New compilation unit (e.g. `src/action/ui/ui_secrets.cpp`), in the house style (`// AUTOINJECT` above the
reimplemented functions, `// AUTOGEN` above the declarations of originals it calls):

1. **Menu_SpecialCodenameCheck (0x7d110)** - reimplement with the table as `static const` data:
   `{ const char *code; enum { MISSION_SET, MISSION_XOR, HI_OR, HI_XOR, UPGRADES } kind; u32 value; }`,
   where an UPGRADES row holds a list of (objId, count) pairs (Q LAB = the nine pairs). Work out only the
   matching row (the original computes all 12 `UpgradeCheat` masks up front; that has no side effects, so
   it is safe to skip). Keep the details: `missions == 0 -> 3`; always call
   `Menu_SetBonus(lo, hi, 0, 0)` on a match, even for mission codes (it rebuilds the upgrades); case-sensitive
   compare; return true/false in EAX with EDX = 0 (the caller tests EAX|EDX).
2. **Menu_UpgradeCheat (0x7cfb0)** - reimplement (pseudocode above). Calls the original
   `PlrStarts_ProcessRewardCounter` (0xb1740, cdecl `(u32 bonus[2]*, HASHCODE, u32 slot, char set, REWARDINFO_tag*)`).
3. **C_KEYBOARD_Handler (0x8cc50)** and **P_CNNAME_Handler (0x8ca10)** - reimplement from the per-message
   descriptions (the non-cheat naming branch calls the originals `FUN_00076330` validate-name,
   `FUN_00075f40` name-exists, `PlrStats_ResetScoring`, `Menu_UpdateMessageBox`). Note that Ghidra calls
   P_CNNAME `__stdcall`, but the dispatcher calls it like every other handler (cdecl, 6 args).
4. Keep as originals: `Menu_GetNightfireStatus`, `Menu_SetBonus`, `Menu_CreateOptionBox` (0x7f3a0),
   `Menu_UpdateOptionBox`, `Txt_BindLabel`, `__Menu_Send`/`__Menu_SendEx`/`__Menu_SendMessage`, `Manager_SendMessage`.

Data needed: the 52 strings and their effects (above); the globals `kbd_text` 0x245200, `del_when_keypressed`
0x17d608, `cheat_mode` 0x25d7de, `cn_modified_flag` 0x25d7dd, `bonus` 0x25d6e8, `sp_level` 0x17c580 (already in
Menu.h), `ls` 0x17d540, the key strings at 0x25ed68; hashcodes P_CNMENU/P_CNNAME/C_KEYBOARD (assets.h) plus the
sub-controls 0x10000242 (page title label) and 0x10000075 (text field), which need names in assets.h
(e.g. `SUB_P_CNNAME_TITLE`, `SUB_P_CNNAME_TEXT`); the keyboard key identifiers 1000/1001/1002/100-103.

A shadow test (a separate unit, per the house rule) can feed every code, plus some misses, through the
original and the reimplementation from the same starting bonus/mission state and compare `bonus[0]` and
`sp_level[].enabled` afterwards. Include the toggle case (the same code twice) and an all-zero mission mask.
