# The menu framework under the handlers (action engine, `default.xbe`)

What the page (`P_*`) and control (`C_*`) handlers stand on: the managers, the menu data, the control tree, the
per-frame loop, input and focus, message dispatch and its return values, the page stack, drawing, timers, text,
sounds and the shared helpers. Researched 2 Oct 2026 from Ghidra `/Xbox_EU/default.xbe` (read only; the PS2
`/PS2_EU_51258/ACTION.ELF` for names), the retail XBE (capstone scans), the menu data in the front-end bundle
(`07000048.bin`, archive crc 0x1ef0f73f) and one level bundle (crc 0x3c9eb1e6), and the 156 `menu.log` files under
`build/menurun/`. [messages.md](messages.md) has every message id and the control/page/manager layouts and is not
repeated here; [handlers.md](handlers.md) has every handler. The plan for rewriting the handlers built on this:
[rewrite.md](rewrite.md).

Names: functions without a name in Ghidra are given by address; names proposed here are marked *(invented)*.
Ghidra renders text-label constants in this code as ORs of unrelated enum names (`QUIT_CONFIRMATION|KEY_1`) -
read the assembly for them.

## Contents

1. [Status: what is ours](#1-status-what-is-ours)
2. [Managers](#2-managers)
3. [Menu data](#3-menu-data)
4. [The control tree and finding controls](#4-the-control-tree-and-finding-controls)
5. [One frame](#5-one-frame)
6. [Input, focus and the cursor](#6-input-focus-and-the-cursor)
7. [Dispatch and return values](#7-dispatch-and-return-values)
8. [Pages: GoPage, the stack, Back, overlays](#8-pages-gopage-the-stack-back-overlays)
9. [Drawing](#9-drawing)
10. [Animation and timers](#10-animation-and-timers)
11. [Text](#11-text)
12. [Sounds](#12-sounds)
13. [The helper layer the handlers call](#13-the-helper-layer-the-handlers-call)
14. [Findings that correct or extend the other documents](#14-findings-that-correct-or-extend-the-other-documents)
15. [Open questions](#15-open-questions)

## 1. Status: what is ours

`ui.frontend` (0x70d40-0x99d70, `tools/subsystems_action.txt`) is 300 functions. From
`python tools/function_coverage.py ui.frontend` (2 Oct 2026), grouped by layer:

| Layer | Live (original) | Done (ours or dead) | Notes |
|---|---|---|---|
| Page/control handlers | 63 functions, 40.3 KB | 36 (35 ours + C_CHCHWEAP dead, see §14) | the subject of [rewrite.md](rewrite.md) |
| Core: managers, pages, dispatch, input, delayed messages, sounds, alpha | 45, 15.1 KB | 6, 4.7 KB | ours: `Handler_HandleMessage` (generated), the four delayed-message functions, `Menu_ClearStack` |
| Control types (Button ... Window, Component skins, sprite glue) | 56, 32.9 KB | 1 (`Menu_DeleteSprite`) | List_SendMessage 4 KB, List_Update 2.9 KB, Scroll_Update 2.4 KB the largest |
| Menu scripts and processes (`Script_*`, `Process_*` at 0x96000-0x97870) | 20, 4.0 KB | 0 | keyframe animation of controls; not the engine's level scripts |
| Helpers handlers call (`Menu_*`) | 40, 11.4 KB | 16, 4.5 KB | iris, wheel, option box, controller-style display, MP, medals, movies |
| Saves and codenames | 15, 5.8 KB | 0 | `XBox_DoSaveFlow`, `Menu_UpdateMessageBox`, codename helpers |
| Data builders | 0 | 2, 31.6 KB | `Menu_SetupCredits`, `Menu_SpecialCodenameCheck` |

Outside `ui.frontend` the menus lean on code that is already ours: `Txt_BindLabel` and the text-bank loader,
`Sprite_Create`/`Create2`/`Delete`/`SetText`/`BuildList`, `hashtable_set_sprite`, `Sound_PlayExt`,
`psiMovieFinished`, `Game_Run` (which calls `MenuManager_Update` then `MenuManager_Monitor`) and `Mission_Update`
(which opens the end-of-mission menu). Still original: `Font_*` (text extent, alignment, word wrap),
`Input_ClearAllActions`, `Sprite_SetFmt`/`SetTextNFmt`/`SetParams`, all of `LS_*` (save blocks), `psiMovieLoop`.

The source files: `src/action/ui/Manager.{h,cpp}` and `MenuManager.{h,cpp}` only declare the original functions
(`// AUTOGEN`); `Menu.{h,cpp}` has the delayed messages, `Menu_UpdateWheel`, `Menu_AddItemsToControl`,
`Menu_ClearStack` and AUTOGEN declarations for the rest; `ui.cpp` is the generated dispatcher.

## 2. Managers

`manager[5]` at 0x25f1d0, 0x1d8 bytes each (`M_MANAGER` in `ui.h`). A manager is one menu instance; the index is the
first argument of every handler. `MenuManager_Create(menuSet, startPage, whichPlayer, managerIndex, closeAction,
pauseAudio, status)` (0x93960) fills one from the loaded menu file. Every creation site, from a scan of the XBE:

| Caller | Menu set | Start page | Manager | whichPlayer | Close action | Pause audio | Status |
|---|---|---|---|---|---|---|---|
| `ResetMap_Load` 0xbfed0 (only when `GameState.CurrentLevelHashcode` matches, test at 0xbfeb2: the front-end level) | `GameState.ReloadMainMenu` (+0x20) | `GameState.ReloadMenupage` (+0x1c) | 0 | -1 (any pad) | 0x28 (none) | 0 | 5 |
| `Player_Update` 0xad579 (START in play) | 0x80000002 | P_PAUSE | the player's controller port | same port | 0x1e (START again closes) | 1 | 3 |
| `MenuManager_Monitor` 0x94ad9 (controller back after a pull) | 0x80000002 / 0x80000004 | P_PAUSE / P_ENDMISSION | 0 | the player | 0x1e / 0x28 | 1 | 3 / 4 |
| `MenuManager_Monitor` 0x948f4 (controller pulled) | 0x80000002 | P_MESSAGEBOX | 0 | 0 | 0x28 | 1 | 2 |
| `Mission_Update` (ours, `game/sp/Mission.cpp`) | 0x80000004 | P_ENDMISSION | 0 | 0 | 0x28 | 0 | 4 |
| `Lock_Update` case 3, 0xd1aef (the keypad lock object) | 0x80000003 | 0x40000021 *(invented `P_KEYPAD`)* | 1 | the player's port | 0x22 | 0 | 1 |

So manager 0 is the front end, player 1's pause menu, the end-of-mission menu and the "controller removed" box;
managers 1-3 are the other players' pause menus in split screen (and the keypad uses 1); manager 4 is never created
by anything found. Creation refuses (returns -1) if no menu file is loaded (`file_start`) or the slot exists.

- **Status** (+0x1b4): 1 keypad, 2 pad-removed box, 3 pause, 4 end of mission, 5 front end. `MenuManager_GetStatus`
  is the highest status of all five; `MenuManager_Update` does nothing when it is 0 (no menu), and
  `Menu_InputAction` treats START as SELECT when it is above 3.
- **Close action** (+0x1c8): `MenuManager_Monitor` deletes the manager when that game action is newly pressed on its
  player's pad and creation finished (+0x1cf == 7); 0x28 means none.
- **Ordering rule**: `MenuManager_Update` and `MenuManager_Monitor` skip manager *n* > 0 while manager *n*-1 exists
  (they test `manager[n-1].exists` through the odd-looking address 0x25f1c9 + n*0x1d8). With two pause menus open,
  only the lower-numbered one runs.
- **whichPlayer** (+0x1cc): the pad the manager reads; -1 = any of the four (Back is tried on each).
  `startPlayer` (+0x1ce) keeps the original value (C_GCPAUSE uses it for that player's controller style).
- `MenuManager_Delete(index)` (0x942b0) unpauses audio if it paused it, consumes the close action
  (`Menu_SetAction`) and unpauses the game if that was START, sends PageLeave to the current page and `Destroy` to
  the manager (every page and control freed, sprites deleted, stack emptied), zeroes the slot, stops the background
  movie and clears all actions.
  Handlers call it to leave a menu for play (C_SBNFMAP, P_MPCONFIRM, C_GCPAUSE, P_ENDMISSION...).
- `MenuManager_Load` (0x92b50) only records the file pointer; `MenuManager_Init` (0x92ae0) clears the slots at boot.

`MenuManager_Monitor` also runs the "controller removed" flow (`controller_state`, `counter_152`, `player_index`
statics): when a player's port is lost in play it deletes managers 0-3, pauses, opens P_MESSAGEBOX with "waiting for
controller %d" in 0x1000011f (sizing the box by hand as `Menu_CreateOptionBox` does), and when the pad is back
deletes it and opens the pause menu (or the end-of-mission menu if that was up).

## 3. Menu data

Pages and controls are data, behaviour is code. The menu file (directory file type 8, inside each level bundle) is a
stream of dword-tagged records that `MenuManager_Create` interprets as it builds the manager:

| Tag | Payload | Effect |
|---|---|---|
| 0xfffffff0 | page hash, menu set, background movie, flag byte, size of the page's records | if the set matches and the flag is 0 or 1: `CreatePage` (Page_Init, add to the manager, `GoPage(page, 0x19)`); else skip the page |
| 0xfffffff9 | control descriptor, 30 bytes (below) | the previous control gets `ControlCreated` (0x51, through the manager and then the page with a2 = 1); then `CreateObject` if the flag byte is 0 or 1, else the control and its records are skipped |
| 0xfffffff6 | byte id, a1, a2 | setup message to the control just created (0x1d skipped; colour 0x7d6d59ff replaced by 0x645a49ff for 0x1a-0x1c) |
| 0xfffffff8 | 32-byte font name (2-byte chars folded) | `SetFont` |
| 0xfffffff7 | script hash, dword | new menu script on the control (FUN_000969a0 *(invented `Script_New`)*) |
| 0xfffffff5 | keyframe: 5 words + dword | `Script_AddKeyFrame` |
| 0xfffffff4 | target, byte id, a1, a2 | message carried by that keyframe (`Script_AddMessage`; target -3 manager, -2 the control) |
| 0xfffffff3 | byte id, a1, a2 | message to the manager (none in the retail files) |
| 0xfffffffd / fc / fb | skin set / skin / skin piece | the Component (skin) tables, per manager (§9) |
| 0xfffffffe | dword | ignored |
| 0xfffffff2 or anything else | - | last `ControlCreated`; cursor sprite created; `GoPage(startPage, 1)`; `SetCursor(0x10000002)` |

The control descriptor, as read (`local_b4`...): hash (4), type (1), flag (1), x, y, w, h (2 each), depth (2), **id**
(4, becomes control +0x20 - the instance number `__Menu_SendEx` matches), skin index (2), two dwords (1 on lists,
scrolls and their kin; meaning unknown). A full-screen Label at (<=0, <=0) of at least 640x480 is clamped to exactly
640x480; control 0x10000224 is moved to (0x181, 0xe2).

**Flags 2 and 3 are platform variants that the Xbox skips**: P_CNCONTROLS and P_CNDRIVINGCONTROLS each have three
copies (flags 1, 2, 3 - different controller pictures), and the help-text labels 0x10000244 on P_MESSAGEBOX and
P_AFTERRESULTS carry flag 3. Only flag 0/1 records are built.

**Every page of the set is built when the manager is created**, not when it is shown. Consequences a handler
rewrite must keep: every control's `ControlCreated` (0x51) handler runs at manager creation (so a checkbox shows the
value its global had then); `__Menu_Send` by hashcode reaches controls on pages that are not showing; and the
current page during creation is the last one built, so the first `GoPage(startPage)` sends **PageLeave to the last
page in the file** and the start page's PageEnter has that page as its "previous page" (a2). In the front end the
last page is P_WINGAME: every boot log starts with `P_WINGAME 0x4d PageLeave` then `P_LANGUAGE 0x4c (b=P_WINGAME)`.

### What the shipped files hold

Parsed with a scratch parser (not in the repo; [rewrite.md](rewrite.md) proposes one for `tools/ui/`). Control counts
include the page's Window control (type 13), which holds the page's background and scripts.

Front end (`07000048.bin`, 56 page records, all set 0x80000002): P_LANGUAGE (10 controls), P_FMV (2), P_INTRO (1),
P_ATTRACT (3), P_START (4; its script GoPages P_PARISENUM), P_PARISENUM (4), 0x40000015 (7), P_NFRESULTS (12),
P_NFBONUS (8), P_NFSTATS (6), P_MAIN (7), P_FMVTEST (2), P_FMVPLAYER (2), P_NFSELECT (16), P_NFMAP (17), P_DOSSIER
(14), P_DSRECORDS (16), P_DSREWARDS (18), P_DSGADGETS (14), P_DSWEAPONS (14), P_NFDFCTY (15), P_MPJOIN (29), P_MPMAP
(15), P_MPSCENARIO (15), P_MPSETUP (28), P_MPOPTIONS (16), P_MPCONFIRM (68), P_MPRULES (10), P_MPENVIROMODS (16),
P_MPPLAYERMODS (16), P_MPBOTS (15), P_MPBOTCHOOSE (15), P_MPBOTSETUP (22), P_MPDEBRIEFING (43), P_CNSELECT (16),
P_CNMENU (15), P_CNNAME (46), P_CNCONTROLS (54/44/52 by flag), P_CNDRIVINGCONTROLS (41/34/36), P_CNOPTIONS (24),
P_CNMPOPTIONS (12), P_CNAVOPTIONS (24; its scripts GoPage P_CREDITS and P_TRAILER), P_TRAILER (2), P_SCREENADJUST
(29), P_AFTERRESULTS (6), P_MESSAGEBOX (6), 0x40000041 (1), P_PS2MEMCARDINIT (2), 0x40000052 (2), P_ESTHERO (1),
P_CREDITS (55), P_WINGAME (1).

In-level menu file (one per level bundle; the one parsed is crc 0x3c9eb1e6): set 0x80000002: P_PAUSE (18),
P_CHEATMEDAL (2), **0x40000007** *(invented `P_DEBUGMENU`)* (24), P_NIS (2), P_TWEAKS (56), P_TWEAKS2 (47),
P_MESSAGEBOX (4); set 0x80000004: P_ENDMISSION (6); set 0x80000003: 0x40000021 *(`P_KEYPAD`)* (24: eleven C_KEYPAD
buttons with ids 0-9 and their icons).

0x40000007 is the developers' cheat page: C_LBPMMAP (the level list), the eighteen C_CHCH* checkboxes, and three
buttons whose scripts GoPage P_NIS, P_TWEAKS and P_CHEATMEDAL. Nothing in the code or the menu data goes to
0x40000007 (no immediate anywhere in the XBE, no script), so - like P_FMVTEST ([movie-selector.md](movie-selector.md))
- it and everything only it leads to are unreachable on a retail disc. Because the pause manager builds every page
of set 0x80000002 in the level file, the debug pages and their controls do exist (and get 0x51) whenever the game is
paused, so `gopage 0x40000007` after pausing reaches them.

### Hashcodes

Pages are 0x40xxxxxx, controls 0x10xxxxxx; a page's Window control has a hashcode of its own (P_PAUSE's is
0x40000001, P_AFTERRESULTS' 0x40000029). 0x10000001 is the shared "anonymous" hashcode of decorative labels and
buttons (frames, icons, static text) - sending to it by hashcode reaches all of them. Several sub-controls are shared
between pages (the iris labels 0x10000102..0x10000113 are reused by companion pages), and one handler hashcode can
have many instances told apart by id: C_GCPAUSE has fourteen on P_PAUSE (ids 0, 1, 3, 4 the tab buttons, 6 the
restart/quit list, 8 objectives/scores, 9 the controller-style radio, 12/13 the confirm text and list, 14, 16...),
C_KEYPAD eleven, C_KEYBOARD's keys their character codes, C_RBMP* one per agent.

## 4. The control tree and finding controls

Manager → pages (`pages` list, +0x98) → controls (page +0x94, linked through `next`/`prev`; `parent` points back
up). There are no nested pages; composite controls embed their parts inline rather than as list members: Button,
Checkbox, Radio and Spin have a Label at +0x94, Radio a Scroll at +0x1a8, List a Scroll at +0x128, Memo a Scroll at
+0x94, Combo a Label and a List. Control sizes (from the `CreateObject` allocations in `Manager_SendMessage`):
Button 0x1ac, Checkbox 0x1ac, Combo 0x71c, Label 0x114, List 0x56c, Radio 0x308, Scroll 0x148, Spin 0x318, Text
0x9b0, Window 0x9c, Memo 0x284, Page 0xd0. Only M_CONTROL, M_WIDGET, M_SCROLL (partly), M_PAGE and M_MANAGER are laid
out in `ui.h`; the other types' fields are known only through messages.md and Ghidra's `M_LABEL`/`M_LISTBOX`/...

Finding a control - the semantics differ and handlers rely on them:

| Function | Searches | Matches | Returns |
|---|---|---|---|
| `__Menu_Send(m, hash, id, a1, a2)` 0x72820 | **every page** of the manager, every control (gives up after 1000 steps) | hashcode, or all if hash = -1 | the **last non-zero** result of the matching controls |
| `__Menu_SendEx(m, hash, n, id, a1, a2)` 0x728f0 | current page, then (if nothing non-zero) the page under an overlay | hashcode and control +0x20 == n | the last match's result on the page that answered |
| `__Menu_SendEx2` 0x729e0 | as SendEx | (+0x20 & n) != 0 | as SendEx; only `Menu_DisplayControllerStyle` uses it |
| `__Menu_SendMessage(c, id, a1, a2)` 0x72630 | - | - | the type's `*_SendMessage`; 0 when c is NULL or c == (M_CONTROL*)a1 |
| `CONTROL_GET(m, hash)` (ui.h) | = `__Menu_Send(m, hash, 0x39 GetControl)` | | so the last control with that hashcode on any page |

Because `__Menu_Send` searches all pages, a handler setting the text of a sub-control shared between two pages sets
both, and `CONTROL_GET` of an instanced hashcode gets the last instance in list order.

## 5. One frame

`Game_Run` (ours) calls `MenuManager_Update` (0x94370) and then `MenuManager_Monitor` (0x94720) once per game
frame, before `Mission_Update`. The menu counts frames, not time: every timer below is in calls of this loop.

`MenuManager_Update`, if any manager has a status:

1. `Menu_AlphaUpdate` (a sine wave: `alpha = sin(counter) * 64`, counter += 0.2, used for pulsing highlights and the
   cursor), `Menu_UpdateTextFmt`, `Menu_ProcessDelayedMessages` (ours).
2. While a menu movie plays: `Menu_UpdateMovieCounter`, and SKIP_CUTSCENE stops it (`Menu_StopMovie`).
3. For each existing manager (subject to the ordering rule in §2), unless input is locked: for its pad (or each of
   four if whichPlayer is -1), BACK newly pressed → play script slot 5 of the player's focused control (or the page's
   focused control); if no such script and not in overlay mode: sound 8 and `Back(0, player)`, and **return** (no
   further managers or pages are updated this frame).
4. Write the cursor sprite's colour from the alpha wave; if no per-player focus is set and input is not locked,
   d-pad presses/repeats (flags 0xc) move the cursor by `Menu_FindControl` according to the page's navigation mode
   (1 four-way, 2 up/down, 3 left/right).
5. `Page_Update(m, currentPage, 1)`, `Page_Update(m, underPage, 1)`, `Page_Update(m, sidePage, 0)`.

`Page_Update(m, page, takingInput)` (0x95a80):

1. `framesShown++`; the control under the cursor (`Menu_GetControl`) becomes `focused`; if it changed and input is
   taken: `Menu_AlphaReset`, `LoseFocus` (0x4f) for the old and `GainFocus` (0x4e) for the new (both through the
   manager, so the handlers see them), sound 7 if both are on the same page.
2. On the first frame (not in overlay mode) `Page_FindHelpControl`; every frame `Page_SetHelpText` (the help line).
3. Every control, walking the list **from the tail**: `Script_RunFrame` (its keyframe script), then if not hidden:
   per-player focus (moves the cursor onto a control a player has focus on), `framesFocused++`, input suppressed
   while `framesShown < inputDelay`, and `Menu_Update` - the per-type `*_Update`, which reads input, sends
   notifications (Select, Scroll...) and positions the control's sprites.
4. The cursor sprite is placed over the focused control; the page's `extraControlHash` control (looked up with
   0x39) is updated as if focused.
5. **PageUpdate (0x50)**: to the current page when input is taken; in overlay mode it goes instead with a1 = the
   page being updated, so the page *under* the overlay keeps getting 0x50 (that is how a page polls
   `Menu_UpdateOptionBox` while its message box is showing) and the overlay page itself (P_MESSAGEBOX, no handler)
   gets one too. Side pages never get 0x50.

`MenuManager_Monitor` handles close actions and the controller-removed flow (§2).

## 6. Input, focus and the cursor

- `Menu_InputAction(player, action, flags)` (0x72e20) wraps `Menu_GetAction`: SELECT/START are ignored while BACK is
  held and BACK while SELECT or START is held; with status > 3 SELECT and START are interchangeable. Flags: 1 held,
  4 newly pressed, 8 auto-repeat. `Menu_GetAction` (0x72d70) records the frame (`last_action`) and pad
  (`last_controller`, read back by `Menu_GetLastController`) of any action it reports; that is the basis of the idle
  counter (`Menu_GetNoInputCount` / `Menu_ResetNoInputCount`, used by P_START, P_MAIN and P_LANGUAGE for their
  timeouts).
- **Focus is the cursor.** Each manager has a cursor sprite (+0x94); `Menu_GetControl` hit-tests it against the
  current page's controls (`Menu_CursorOverMe`, lowest depth wins) and the hit becomes `focused`. `SetCursor` (0x22)
  moves the cursor onto a control (menu data: every page's Window script sets it on entry). Controls with
  `state != 0` (hidden or inert) and Windows are skipped by navigation.
- `Menu_FindControl(m, dir)` (0x734d0, dir 1 up 2 down 4 left 8 right) scores every other selectable control by its
  offset in the direction (twice the distance along it plus a tenth of the offset across, overlapping extents
  required), keeps a second list for wrap-around, sorts both with `QuickSort`/`Compare_MenuCtrl` and puts the
  cursor on the best (sizing it 10 px larger than the control).
- **Per-player focus** (page +0xb0[4], `SetPlayerFocus` 0x5a): on the multiplayer join/setup pages each agent's row
  holds its player's focus; while any is set the d-pad does not move the cursor and `Page_Update` points the cursor
  (and `whichPlayer`) at each player's control in turn while updating it, so each row reads its own pad.
- `LockInput` (0x68) sets +0x1d4 and clears all actions; `GoPage` (not an overlay) unlocks. `SetInputDelay` (0x6f)
  holds input off for N frames after a page is entered (P_START).
- Which button gives which message per control type is in [messages.md](messages.md#input-to-messages). A control
  sends its notification to its parent page only when the manager is taking input and is not locked.

## 7. Dispatch and return values

The route of a notification, with what the return values do (refining [messages.md](messages.md#how-a-message-travels)):

1. The control's `*_Update` sends `(page, id, control, 0)`.
2. `Page_SendMessage` (0x95820): for Select, AltSelect1/2 and BackOnControl with a2 == 0 it first sends
   `(manager, id, page, control)` - **the P_ handler sees the selection of any control on its page before the
   control's own handler** (that is why P_CNOPTIONS, P_TWEAKS and the MP mods pages commit everything on any A).
   Then, for those and Scroll/GainFocus/LoseFocus/ControlCreated, it plays the control's default script
   (`Script_PlayDefault`), and for those plus TextChanged/MemoBack/TextEndEntry/ValueSet/SelectRepeat, if a2 == 0,
   sends `(manager, id, control, 0)`. Everything else stops at the page (0x6f SetInputDelay is stored).
3. `Manager_SendMessage` (0x92c40), for any id it does not own: if there is a current page, calls
   `Handler_HandleMessage(index, (M_CONTROL*)a1, id, a1, a2)`. Then:
   - for Scroll, TextChanged, Select, GainFocus, LoseFocus, MemoBack, TextEndEntry: returns 1 if a1 is a page,
     else plays a1's default script and returns whether one played;
   - for anything else, **if the handler returned false**, re-sends `(currentPage, id, a1, 1)`; the page, seeing
     a2 = 1, does not bubble again, but still plays the control's default script for ControlCreated (slot 0) - the
     only id where a false return has a visible effect (the creation script plays twice). Otherwise returns 0.
4. `Handler_HandleMessage` (0x8e320, ours, generated by `tools/uihandler.py`) switches on `a1->hashcode`.

**Return conventions.** A handler's boolean matters only through step 3; no code reads it otherwise. Handlers return
true almost everywhere. Results go back through other channels: QueryBack (0x6b) writes 0xfffffffe into `*a2` to
keep the page; option boxes leave their result in a global (§13). Handlers do not call each other and there is no
"default handler": unhandled cases fall through to `return true`, and the only defaults are the control's and the
page Window's default scripts (slots: 0 ControlCreated/PageEnter, 1 GainFocus/PageLeave, 2 Select/BackAtRoot,
3 Scroll, 4 LoseFocus, 5 B; PageEnter/PageLeave/BackAtRoot play the page Window's slots).

**The original dispatcher's default is false; ours is true.** The original 0x8e320 returns `XOR AL,AL` for any
hashcode it does not know, and `MOV AL,1` for three it knows but whose handlers the Xbox build compiled to nothing:
0x100001eb C_SBSCREEN, 0x40000047 P_SCREENADJUST, 0x4000004d P_FMV (the PS2 build still has
`C_SBSCREEN_Handler`, `P_SCREENADJUST_Handler`, `P_FMV_Handler` and `P_PS2MEMCARDINIT_Handler`). The generated
`ui.cpp` returns `hashcode & 0xffffff00` - non-zero for every 0x10/0x40 hashcode, i.e. true - and prints
"UNHANDLED MESSAGE HANDLER", which is why the MenuProbe logs show `-> 1` for every anonymous sub-control. The effect
today is limited to the double creation script above, but it should be put right before more of the chain becomes
ours. The generated switch also **omits C_CHCHWEAP** (0x100000e7 → 0x81b50, dispatched by the original at
0x8e674): see §14.

Sending a command down returns the target's own result (messages.md lists them); `__Menu_Send` returns the last
non-zero one, which is how `CONTROL_GET` works.

## 8. Pages: GoPage, the stack, Back, overlays

`GoPage(page, flags)` (0x44, in `Manager_SendMessage`), flags as in [messages.md](messages.md#gopage-0x44-flags-a2):

1. `Input_ClearAllActions(-1)`. Walk every page of the manager:
2. The target: `Menu_EnableSounds(page)` (§12); unless flag 1, push `{currentPage, currentPage->focused}` (8 bytes,
   `Menu_Malloc`) on the stack (64 entries); going to P_MAIN without flag 4 empties it (`Menu_ClearStack`, ours).
   An existing under-page is hidden and updated once. The current page is hidden (state 1) and updated once; without
   flag 2 it gets PageLeave (unless flag 8), the overlay ends and input is unlocked; with flag 2 it becomes the
   under-page (overlay mode), and all delayed messages are cleared (`Menu_ClearDelayedMessages`).
   The target becomes current with depth 5, state 0, `framesShown = 0`; its background movie starts or stops if it
   differs (not for overlays); then PageEnter (a2 = previous page hashcode) and the Window's slot-0 script - or, if
   there is no such script, the Window is stopped and placed at (0, 0) - unless flag 8, which sends PageResume
   instead (none with flag 0x10). A left-behind under-page gets PageLeave and its Window's slot-1 script (or is
   moved to (2000, 2000)); in overlay mode it is instead drawn behind (depth 0xfffb, state 2, inert).
3. Every other page that is neither current, under nor side: hidden, depth 0, updated once (unless flag 8), and every
   control's sprites freed (`Component_Free`, Label sprites deleted). Pages are never destroyed while the manager
   lives.
4. A final `Page_Update` of the current page.

`Back(forced, player)` (0x5f): unless forced, QueryBack to the player's focused control and then to the page; either
can veto. Then, if the stack is not empty, the page's Window slot-2 script plays if it has one, else
`GoPage(top.page, 5 | (overlay ? 8 : 0))` (no push, no P_MAIN clear, and PageResume rather than PageEnter when leaving
an overlay) and `SetCursor(top.focused)`; the entry is popped and freed. With an empty stack: BackAtRoot (0x63) to the
page's handler (P_MPSCENARIO and P_NFMAP use it to go to P_MAIN).

`SetSidePage` (0x45) shows a second page next to the current one (depth 0xfff6, updated without input); nothing in
the retail code or data sends it. `GetCurrentPage` (0x74) answers the under-page's hashcode in overlay mode.

Message boxes are overlays: `Menu_CreateOptionBox` does `GoPage(P_MESSAGEBOX, 2)`, and C_LBMSGOPTIONS closes it with
`Back(1)` - forced, so no QueryBack, and with flag 8 so the page underneath gets PageResume, not PageEnter.

## 9. Drawing

The menu never draws directly; it maintains engine sprites (the sprite system is ours) which the renderer's 2D pass
draws.

- **Labels** (text, or an icon from a sprite sheet) keep a template sprite inside the control (`textSprite` in
  Ghidra's `M_LABEL`) and a live sprite at +0xe8 (`Menu_CreateSprite` = `Sprite_Create`/`Sprite_Create2` with
  +0x28 = 7). Each `Label_Update` positions the template from the page and control rectangles and the font's
  alignment, re-binds the text label if it has one (so a language change shows at once), clips it to the width
  (`Menu_ClipString`), applies the colour state and copies 0x44 bytes of template into the live sprite. A hidden
  label (or one on a hidden page) deletes its live sprite.
- **Skins (Components)**: other control types draw their frame from skin pieces defined by the 0xfffffffd/c/b
  records: per manager, a set of skins, each a list of 0x38-byte pieces with anchors (left/right/centre, top/bottom/
  middle), stretch factors and a draw-state mask. `Component_SetupInstance` (0x71e00) lays a control's pieces out
  for its current draw state (+0x1c: 0x10 normal, 0x20 focused, 0x40 pressed, 0x80 cursor over), creating the
  sprites on first use (control +0x08 array, +0x6c count) and hiding pieces whose mask does not match. The skin
  index is the descriptor's word after the id.
- **Depth**: a sprite's draw order is the control's depth (+0x78) minus the page's; the current page is depth 5,
  an under-page 0xfffb, a side page 0xfff6.
- **Colours**: normal and highlight colours per Label/List/Memo (setup messages 0x1b/0x1c), pulse colour (0x29, with
  the alpha wave when 0x70 pulse is on), and fades between them: when a label that had focus for 6 frames or more
  loses it, a Process of type 0xe1 (`Process_Create(control, msg, 15)`, run by `Process_RunFrame`) fades it back to
  the normal colour.
- **Lists and memos** build their rows as text sprites per cell (List: up to 5 columns of 0x8c-byte cells, text or
  sprite; Memo: word-wrapped lines with `Font_WordWrapString`), scrolled through their embedded Scroll.
- **Background movies**: page +0xc4 from the page record; `GoPage` plays it looped with `Menu_PlayMovie(hash, 0, 1,
  0, 0, 0)` or stops it; full-screen movie pages (P_INTRO, P_ESTHERO, P_ATTRACT, P_TRAILER, P_WINGAME, P_FMVPLAYER)
  call `Menu_PlayMovie` themselves and poll `psiMovieFinished` in PageUpdate.
- The cursor is a sprite too; its colour pulses with the alpha wave and it is sized to the focused control.

## 10. Animation and timers

All frame-counted:

| Timer | Where | Unit |
|---|---|---|
| Delayed messages | `__Menu_SendDelayed[Message]`, 128 slots, fired by `Menu_ProcessDelayedMessages` (ours) | frames; cleared by overlays and C_LBERROPTIONS |
| Menu scripts | `Script_RunFrame` per control per Page_Update; keyframes interpolated linearly or by spline (`Script_InterpolateLine`/`Spline`); messages on keyframes | frames |
| Processes (colour fades) | `Process_RunFrame` | frames |
| Iris | globals 0x25d7ac (tick), 0x25d7b0 (frame 1-10), 0x25d7b8 (hold), 0x25d7b9 (running) | advances every second call of `Menu_PlayIris(1, ...)` from a PageUpdate |
| Page | `framesShown` (+0xc8), `inputDelay` (+0xcc) | frames |
| Control | `framesFocused` (+0x24); Scroll auto-repeat after 16 frames; input repeat after 45 frames every 8th | frames |
| Idle | `Menu_GetNoInputCount` | frames (P_START: attract after 0xa8c) |
| Pulse | `Menu_AlphaUpdate` | 0.2 rad per frame |

**The iris** (the shutter over the wheel pages): `Menu_StartIris(op, m, label)` (0x7f260; Ghidra cut its jump-table
cases off as FUN_0007f285, FUN_0007f296, FUN_0007f2a2, FUN_0007f2b4 and FUN_0007f2cf, which `function_coverage`
lists as separate live functions) sets the state - op 0: frame 5, running; 1: frame 1, running; 2: restart at
frame 1 unless already running; 3: frame 5, running, held; 4: frame 10 - and draws it once. `Menu_PlayIris(advance,
m, label)` (0x75c80) sets the label's icon to frames 0x030000bf..0x030000c3 (1/9, 2/8, 3/7, 4/6, 5; 10 = frame 1 with
another rectangle) and advances on every second call; above frame 10 it stops. `Menu_ChangePageCloseIris(page, m,
label)` (0x7f320) starts it at frame 1, queues `LockInput(1)` after 1 frame and `GoPage(page, ls.field3_0xc)` after
10 - the GoPage flags come from the codename load/save record (`MENU_LS` +0x0c), normally 0.

## 11. Text

`Txt_BindLabel(label, 0)` (ours) returns the string for a text label in the current language bank;
`tools/ui/text_bank.py` reads the same banks offline. Two ways text reaches a control:

- `SetText` (0x18) with a char* (most handlers: the pointer must stay valid - handlers sprintf into static buffers,
  and those buffers are globals the game owns until their users are all ours), or with a1 = 0 and a2 = a label id,
  which the Label keeps and re-binds on every update.
- `AddItem` (0x10) with a char* (handlers bind the label first) or `AddItemLabel` (0x11) with a label id (menu
  data only).

Fonts are set by name from the menu data (`SetFont`, with a suffix per state) or by id on lists (1/2/3). The
help line at the bottom of a page is found on its first frame (`Page_FindHelpControl`) and refreshed every frame
(`Page_SetHelpText`). `Menu_UpdateTextFmt`/`Menu_GetNormalTextFmt` (PS2 name) keep the shared text format.

## 12. Sounds

`Menu_PlaySound(n)` (0x730f0), all through `Sound_PlayExt(..., 100.0)`:

| n | Sound | When | Gated by bit |
|---|---|---|---|
| 0 | MENU_ITEM_SELECT | A (Button, Checkbox, Combo, Label, Memo, Scroll updates) | 1 |
| 1 | MENU_ITEM_SELECT | Y (AltSelect1) | 2 |
| 2 | MENU_ITEM_SELECT | X (AltSelect2) | 4 |
| 3, 5 | MENU_ITEM_UP | a focused Scroll stepped one way (Scroll_Update) | always |
| 4, 6 | MENU_ITEM_DOWN | stepped the other way | always |
| 7 | MENU_ITEM_SELECT | focus moved (Page_Update) | always |
| 8 | MENU_ITEM_DOWN | B / Back (MenuManager_Update) | 8 |
| 9 | none | (Scroll_Update sends it; no case) | - |

The gate is a per-page table in `Menu_EnableSounds(page)` (0x73010), applied on every GoPage: P_MAIN, P_START,
0x40000015 → 1 (select only); P_NFMAP, P_NFSELECT, P_PAUSE, 0x40000005 → 0xb; P_CNCONTROLS → 0xd; P_MPDEBRIEFING →
3; P_NFRESULTS, P_NFBONUS → 7; P_NFSTATS, P_DSRECORDS, P_DSREWARDS, P_DSGADGETS, P_DSWEAPONS, P_CNDRIVINGCONTROLS → 8
(back only); every other page 9 (select and back). Handlers play no menu sounds of their own; they control music
(`SFXMusicSetVolume`, `Menu_RestartFrontEndLoop`, `Menu_StopFrontEndMusic`, `SFXFadeDown`) around movies and level
starts.

## 13. The helper layer the handlers call

The functions that more than one handler shares, with their state. Ours marked **ours**.

| Helper | Address | Used by | What it does / state |
|---|---|---|---|
| `Menu_StartIris`, `Menu_PlayIris`, `Menu_ChangePageCloseIris` | 0x7f260, 0x75c80, 0x7f320 | all wheel pages and their wheels, P_NFSELECT, P_CNSELECT | §10 |
| `Menu_UpdateWheel` | 0x7f680 **ours** | every SB wheel | five labels by id 0-4, colours, icon, description; starts the iris |
| `Menu_UpdateCodenameWheel` | 0x7fb20 | P_NFSELECT, C_SBNFCN, P_CNSELECT, C_SBCNSELECT | the same over `codename_buf` |
| `Menu_SelectItemInControl`, `Menu_AddItemsToControl` (**ours**), `Menu_GetItemFromHash` | 0x759d0, 0x75950, 0x7cf70 | M_ITEM pages | |
| `Menu_CreateOptionBox` / `Menu_CreateOptionBoxLabel` | 0x75fb0 / 0x7f3a0 | 13 sites | text into Memo 0x1000011f; C_LBMSGOPTIONS cleared and, if `yesNo`, given one item; GoPage P_MESSAGEBOX as an overlay; box sized to the text; cursor on C_LBMSGOPTIONS. Remembers `type` (0x224f60) and `yesNo` (0x224f64) |
| `C_LBMSGOPTIONS_Handler` | 0x761c0 | the box's list | A: result = 1 (3 if yesNo), B: 2 (4); `Back(1)` |
| `Menu_UpdateOptionBox(&type)` | 0x761a0 | pages' PageUpdate | returns and clears the result (0x224f5c), gives the box's type |
| `Menu_UpdateMessageBox(m, ...)` | 0x757d0 | codename/save pages | steps the load/save/enumerate/delete operation in `ls` (`MENU_LS`, 0x17d540) and calls `XBox_DoSaveFlow` (0x8f3f0), which shows the save UI and sends SaveFlowDone (0x6a) |
| FUN_0007fef0 *(invented `Menu_StartLoadSave`)* | 0x7fef0 | C_SBNFCN, C_RBMPCNAME, C_SBCNSELECT | fills `ls` (operation, slot, return page, GoPage flags, codename) and makes the first `Menu_UpdateMessageBox` call |
| FUN_0007edf0 = PS2 `Menu_SelectCodenameInControl` | 0x7edf0 | P_NFSELECT, P_CNSELECT | finds a codename in `codename_buf`, `SetValue` on the wheel |
| FUN_0008fcc0 *(invented `XBox_CheckSaveSpace`)*, FUN_0008fd90 *(invented `XBox_FreeSpaceLaunch`)* | 0x8fcc0, 0x8fd90 | codename pages, P_PARISENUM | Xbox free-blocks / too-many-codenames box; launches the dashboard (`WriteStateFileAndLaunch`) |
| `Menu_MapDefaultCodename`, `Menu_UpdateDefaultCodename`, `Menu_ValidateCodename`, `Menu_CodenameExists`, `Menu_PutCodenamesIntoControl` | 0x7eee0, 0x7f100, 0x76330, 0x75f40, 0x754c0 | codename and MP pages | |
| `Menu_DisplayControllerStyle` / `...List` / `Menu_UpdateControllerLabelString` | 0x73d10 / 0x749c0 / 0x74910 | P_CNCONTROLS, C_RBCONTROL / C_GCPAUSE | 5.8 KB of button-picture labels per scheme (uses `__Menu_SendEx2`) |
| `Menu_ChangeControllerStyle` | 0x72f60 **ours** | C_RBCONTROL, C_GCPAUSE | |
| `Menu_AddRow`, `Menu_AddRowPercentage` | 0x73a80, 0x73bc0 | P_NFSTATS | list rows |
| `Menu_GetLevelBonuses`, `Menu_HasMedal`, `Menu_GetBestMedal` | 0x7f500, 0x7ce80, 0x7ce20 | results/records pages | |
| `Menu_PrepareBots`, `Menu_IsBotGood`, `Menu_GetMPScore`, `Menu_GetBotShortName`, `Menu_StoreMPSettings`, `Menu_RestoreMPSettings`, `Menu_UpdateMPControllers`, `Menu_AllJoinedPlayersReady` (PS2 `Menu_MPAreWeReady`), `Menu_MPFadeWhenReady` | | MP pages, C_GCPAUSE | `Menu_IsBotGood` has an inline copy in `Menu.cpp` |
| `Menu_PlayMovie`, `Menu_StopMovie`, `psiMovieLoop`, `Menu_RestartFrontEndLoop`, `Menu_StopFrontEndMusic` | 0x7fe00... | movie pages, GoPage | movie flags in 0x25d7bd/0x25d7be (`ui_credits.cpp` names them) |
| `Menu_RunMiniMission`, `Menu_WillRunMiniMission` | 0x7cd50, 0x7ccf0 | P_PARISENUM | the first-boot Paris mission |
| FUN_00084080 *(invented `Menu_SetSoundLanguage`)*, FUN_000840d0 *(invented `Menu_LanguageToButton`)* | | P_LANGUAGE, C_LANGUAGE | language index → `SFXSetLanguage`; language → button id |

## 14. Findings that correct or extend the other documents

- **C_CHCHWEAP is not dispatched by our `Handler_HandleMessage`.** The original sends 0x100000e7 to
  C_CHCHWEAP_Handler (0x81b50; the call at 0x8e68a); `tools/uihandler.py` has "TODO: Default case - CHCHWEAP?" in its
  table and leaves it out, so `function_coverage` reports the handler DEAD and the debug page's "all weapons"
  checkbox does nothing. Reachable only through the debug page, so no retail difference, but it belongs in the table.
- **Default return** (§7): ours true, original false, with three ids true.
- handlers.md's step-2 list has 70 entries; 6 of them (C_CHCHMUSIC, C_CHCHDRAWALL, P_ATTRACT, P_CNNAME, C_KEYBOARD,
  P_CREDITS) are done, leaving 64 including C_CHCHWEAP.
- The three menu sets and five managers (§2), all pages built at creation (§3), platform-variant flags (§3), the
  debug page 0x40000007 and its unreachability (§3), and the startup PageLeave to the last page (§3) are new.
- Identified: FUN_0007edf0 is the PS2's `Menu_SelectCodenameInControl`; the FUN_0007f2xx fragments are
  `Menu_StartIris`; FUN_0008aa49 is the split-off body of P_MPBOTCHOOSE (already in handlers.md).
- The control descriptor's id field (+0x20) and skin index (§3), the control allocation sizes (§4), the sound gates
  (§12) and the option-box result codes (§13) were not written down before.
- C_MPDBG is not a debug control (handlers.md calls it one): its four instances (ids 0-3) are the player columns
  of P_MPDEBRIEFING, the page after every match; "DBG" stands for debriefing.
- C_GCPAUSE's GainFocus on a tab button (ids 0, 1, 3, 4) switches the panel - the pause menu's tabs change on hover,
  not on A.

## 15. Open questions

- The descriptor's two trailing dwords (1 on lists and scrolls) and the Window's id values (2 on P_PAUSE's, 0xf on
  most) are unexplained.
- Only two menu files were parsed; the other level bundles are assumed to carry the same in-level file.
- `Menu_UpdateTextFmt` and the help-text pair (`Page_FindHelpControl`, `Page_SetHelpText`) were not read in detail.
- The Component (skin) piece layout (0x38 bytes) is only partly understood: anchors and stretch flags 1, 2, 4, 8,
  0x100, 0x200, 0x400, 0x800, 0x1000 are used in `Component_SetupInstance`.
- Whether anything outside the menus reads a menu manager's state besides `MenuManager_GetStatus`,
  `MenuManager_Exists` and `user_restarted_or_quit` was not checked.
