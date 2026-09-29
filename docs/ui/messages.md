# Menu system message types (action engine, `default.xbe`)

Every id the menu system sends or handles, worked out from the Xbox EU binary (29 Sept 2026). The game has no
canonical names; the names here are invented from what each id does. Machine-readable copy: `messages.json`
(same data, generated from one table).

How the list was built:

- The per-type dispatchers behind `__Menu_SendMessage` (0x72630) were read in full: `Button_SendMessage`
  0x70e00, `Checkbox` 0x71380, `Combo` 0x71890, `Label` 0x90590, `List` 0x91b10, `Manager` 0x92c40, `Memo`
  0x95330, `Page` 0x95820, `Radio` 0x96380, `Scroll` 0x97870, `Spin` 0x986f0, `Text` 0x98be0, `Window` 0x99c60.
- Every call site of `__Menu_SendMessage`, `__Menu_Send`, `__Menu_SendEx`, `__Menu_SendEx2`,
  `Manager_SendMessage`, `__Menu_SendDelayed(Message)`, `Script_PlayDefault`, `Handler_HandleMessage` and the
  per-type functions was found with a capstone scan of the XBE (1758 calls; 1716 with a constant id).
- The 99 `P_*`/`C_*` handlers were decompiled and their comparisons on the message argument collected.
- The menu definition file (dir-file type 8, handed to `MenuManager_Load`; found inside the level bundles, e.g.
  archive entry crc 0x1ef0f73f for the front end) was parsed: its message records are listed under
  "Messages in menu data".

Caveat on the decompiler output: Ghidra renders some ids as ORs of the old enum names, e.g.
`MessageType_GetValue|MessageType_SetColour` is 0x5a, `GetValue|SetText` 0x58, `GetValue|GetNumItems` 0x73,
`GetValue|GetScrollValue` 0x75.

## How a message travels

`__Menu_SendMessage(control, id, a1, a2)` switches on `control->type` (+0x7b). It refuses when
`control == (M_CONTROL*)a1`, which stops a notification being sent back to the control named in it.

- **Commands** go down: a handler calls `__Menu_Send(manager, hashcode, id, a1, a2)` (every control with that
  hashcode on every page; hashcode -1 = all), `__Menu_SendEx(manager, hashcode, id_at_+0x20, id, a1, a2)`
  (current page, then the page under an overlay; control +0x20 must equal the item number),
  `__Menu_SendEx2` (same but +0x20 is a bit mask) or `__Menu_SendMessage` on a pointer from `0x39 GetControl`.
  `__Menu_SendDelayed(frames, manager, hashcode, id, a1, a2)` queues one (128 slots, fired by
  `Menu_ProcessDelayedMessages` at the start of `MenuManager_Update`).
- **Notifications** go up: a control sends `(parent, id, self, 0)`. Composite controls (List, Radio, Spin, Combo)
  resync from their embedded Scroll and resend with themselves as a1. `Page_SendMessage`:
  - 0x4b, 0x5d, 0x5e, 0x6d (with a2 == 0): first to the manager with `a1 = page, a2 = control` (so the `P_`
    handler sees it), then plays the control's default script, then to the manager with `a1 = control` (the
    `C_` handler);
  - 0x49, 0x4e, 0x4f, 0x51: default script, then to the manager with `a1 = control`;
  - 0x4a, 0x52, 0x53, 0x54, 0x61: straight to the manager;
  - anything else (including 0x00) stops at the page.
- `Manager_SendMessage` handles its own ids (0x22, 0x42-0x45, 0x47, 0x56, 0x5a, 0x5f, 0x68, 0x74); every other
  id is a notification: it calls `Handler_HandleMessage(index, (M_CONTROL*)a1, id, a1, a2)`, which picks the
  handler by `a1->hashcode`. Handler signature: `(uchar manager, M_CONTROL *control, uint hashcode, uint id,
  int a1, int a2)` - so the handler's 5th/6th parameters are a1/a2 of the message, and a1 is normally the
  control itself. For 0x49/0x4a/0x4b/0x4e/0x4f/0x52/0x53 the manager then plays the control's default script
  (not for pages); for the rest, if the handler returns false the message is passed to the current page with
  a2 = 1 (which stops it bubbling again). Return true = handled.
- **Menu scripts** (keyframes in the menu data) carry messages too: `Script_RunFrame` sends id < 0xe1 to the
  manager (target -3), to the control itself (-2) or by hashcode, with a1 = -4 meaning "result of the previous
  script message"; ids >= 0xe1 start a Process (only 0xe1, a colour fade, exists).

## Input to messages

Input flags of `Input_Action` / `Menu_InputAction`: 1 = held, 4 = newly pressed, 8 = auto-repeat (held more than
45 frames, every 8th frame). Pad mapping (psiInput_MapInputs): A = MENU_SELECT, B or BACK = MENU_BACK, Y =
MENU_ALTSELECT_1, X = MENU_ALTSELECT_2, d-pad or left stick = MENU_DIR_*, START = PAUSE. `Menu_InputAction`
suppresses SELECT/START while BACK is held and vice versa, and makes START count as SELECT when the manager
status is above 3.

| Input | Where | Result |
|---|---|---|
| B / BACK (pressed) | MenuManager_Update 0x94370 | focused control's script slot 5 (event 0x5b) if it has one, else sound 8 and `0x5f Back(0, player)` |
| d-pad (pressed or repeat) | MenuManager_Update | `Menu_FindControl` moves the cursor (1 up, 2 down, 4 left, 8 right) according to page +0xc0; only when no per-player focus is set |
| cursor now over another control | Page_Update 0x95a80 | `0x4f LoseFocus(old)`, `0x4e GainFocus(new)`, sound 7 |
| d-pad held on a focused Scroll | Scroll_Update 0x97be0 | value +-1 (repeat after 16 frames), `0x49 Scroll` to the parent; wraps when +0x144 is set |
| A | Button/Checkbox/Label/Memo/Scroll updates | `0x4b Select` (Checkbox toggles +0x1a8 first); TextBox types a character and sends `0x4a` |
| A held (repeat) | Button_Update | `0x61 SelectRepeat` |
| Y | Button/Label/Memo/Scroll updates | `0x5d AltSelect1` |
| X | same | `0x5e AltSelect2` |
| B on a focused Scroll | Scroll_Update | `0x6d BackOnControl` (the page-level Back also fires) |
| B on a focused Memo (pad 0) | Memo_Update 0x94cf0 | `0x52 MemoBack` |
| any, while +0x1d4 is set | all | ignored (`0x68 LockInput`) |

## GoPage (0x44) flags (a2)

| Bit | Meaning |
|---|---|
| 0x01 | do not push the current page on the Back stack |
| 0x02 | overlay: keep the current page drawn underneath (manager +0x1c0, +0x1d3 = 1), clears delayed messages, no 0x4d / 0x68 |
| 0x04 | going to P_MAIN does not clear the stack |
| 0x08 | no transition: send `0x6e PageResume` instead of `0x4c PageEnter` (and no 0x4d) |
| 0x10 | with 0x08: not even 0x6e |

Back uses `5 | (overlayMode ? 8 : 0)` and then `0x22 SetCursor` to restore the cursor; CreatePage uses 0x19;
`Menu_ChangePageCloseIris` sends `0x68 LockInput(1)` after 1 frame and GoPage after 10.

## Message table

| Id | Name | Sent by | Handled by | Args (a1, a2) | Returns | Evidence |
|---|---|---|---|---|---|---|
| 0x00 | `MessageType_TextEntryDone` | TextBox_Update ('+' key, or focus leaves while editing); Text_SendMessage(0x53) | sent to the textbox's parent page, which drops it (not in Page_SendMessage's switch) - effectively dead | a1 = textbox | - | 0x98de0, 0x98be0, 0x95820 |
| 0x0e | `MessageType_ListAddColumn` | C_GCPAUSE, P_NFSTATS, Menu_DisplayControllerStyleList | List | a1 = width in % of the list, a2 = alignment (as 0x59) | 1, or 0 if 5 columns exist | 0x91b10 |
| 0x0f | `MessageType_ListAddRow` | Menu_AddRow, Menu_AddRowPercentage | List | a1 = row (5 cells of 0x8c: text or sprite), a2 = item value | 1 | 0x91b10, 0x73a80 |
| 0x10 | `MessageType_AddItem` | ~330 sites: page/control handlers, Menu_AddItemsToControl, List(0x19 word wrap) | List, Radio (Combo not) | a1 = char* text, a2 = item value (returned by 0x35) | List: new item index; Radio: 1 | 0x91b10, 0x96380 |
| 0x11 | `MessageType_AddItemLabel` | menu data (setup records) | List, Radio | a1 = text label id (Txt_BindLabel), a2 = item value | as 0x10 | 0x91b10, 0x96380 |
| 0x12 | `MessageType_ScrollAddToMax` | List (item added/removed) | Scroll | a1 = delta added to max | 0 | 0x97870 |
| 0x13 | `MessageType_ListRemoveItem` | none found | List | a1 = index (probable) | 1 if removed | 0x91b10 |
| 0x14 | `MessageType_ScriptExitLoop` | none in code (menu scripts) | Button, Checkbox, Combo, Label, List, Radio, Scroll, Spin, Text, Window | - | Script_ExitLoop result | 0x96d50 |
| 0x15 | `MessageType_ScrollHideBar` | none found | Scroll | - (sets +0x13c bit0: bar not drawn, input still works) | 1 | 0x97870, 0x97be0 |
| 0x16 | `MessageType_ScriptPlay` | none in code (menu scripts) | Button, Checkbox, Label, List, Memo, Radio, Scroll, Spin, Text, Window | a1 = script hashcode, a2 = flag | Script_Play_UISTUFF result | 0x96ba0 |
| 0x17 | `MessageType_ClearItems` | handlers (P_CNOPTIONS, P_MPBOTSETUP...), Menu_AddItemsToControl, Menu_CreateOptionBox | List, Radio | - | List 1, Radio 0 | 0x91b10, 0x96380 |
| 0x18 | `MessageType_SetText` | ~315 sites; menu data | Label; Button/Checkbox/Radio/Spin/Text pass it to their label; Memo | a1 = char* (or 0); if a1 == 0, a2 = text label id; both 0 = blank | 1 | 0x90590, 0x95330 |
| 0x19 | `MessageType_ListSetCellText` | C_GCPAUSE, P_NFSTATS, List (wrap) | List | a1 = row<<16 \| column, a2 = char* (word-wraps into extra rows) | 1 | 0x91b10 |
| 0x1a | `MessageType_SetColour` | Menu_UpdateWheel, handlers, Process_RunFrame (fade 0xe1) | Label (+ owners) | a1 = colour; a2 = 0: fixed colour (auto highlight off, normal = highlight = a1); a2 != 0: tint only | 1 | 0x90590, 0x95e60 |
| 0x1b | `MessageType_SetHighlightColour` | menu data (every control) | Label, List, Memo | a1 = colour used when focused / cursor over | 1 | 0x90590, 0x8fff0 |
| 0x1c | `MessageType_SetNormalColour` | menu data (every control) | Label, List, Memo | a1 = colour when not focused (faded to with process 0xe1) | 1 | 0x90590, 0x8fff0 |
| 0x1d | `MessageType_SelectIndex` | Menu_SelectItemInControl, Menu_PutCodenamesIntoControl, C_GCPAUSE, P_PAUSE (menu data has it but MenuManager_Create skips it) | List, Radio | a1 = index | List: result of the 0x49 sent to the parent; Radio: same | 0x91b10, 0x96380, 0x93960 |
| 0x1e | `MessageType_SelectItemByValue` | ~50 sites (option pages) | List, Radio | a1 = item value, a2 = 1: silent (no 0x49 to the parent) | 1 / result of 0x49 | 0x91b10, 0x96380 |
| 0x1f | `MessageType_ListSetCurrentItemValue` | none found | List | a1 = value | 1 | 0x91b10 |
| 0x20 | `MessageType_SetId` | C_SBCNSELECT, C_SBNFCN (to C_SBCNOPTIONS) | Button, Scroll: control +0x20; Radio: value of the last item | a1 = id / value | 1 | 0x70e00, 0x97870, 0x96380 |
| 0x21 | `MessageType_SetScript` | menu data | all except Page, Manager, Combo | a1 = slot 0..6, a2 = script hashcode (slot table at +0x7c) | 1 | 0x90590 |
| 0x22 | `MessageType_SetCursor` | menu scripts (target manager), MenuManager_Create (0x10000002), Manager 0x5f, P_PAUSE, P_LANGUAGE... | Manager | a1 = control hashcode, or a1 = 0 and a2 = M_CONTROL* | 1 if found | 0x92c40 |
| 0x23 | `MessageType_SetFont` | menu data (record 0xfffffff8, 32-char name), P_CREDITS, P_NFRESULTS... | Label (sprintf name+suffix), List (a1 = 1/2/3 font id), Memo (font name) | a1 = font name or id | 1 | 0x90590, 0x91b10, 0x95330 |
| 0x24 | `MessageType_SetIcon` | Menu_PlayIris, Menu_UpdateWheel, handlers; menu data | Label (+ owners) | a1 = sprite hashcode (switches the label to icon mode) | 1 | 0x90590, 0x75c80 |
| 0x25 | `MessageType_ScrollSetMax` | Radio, List (internal) | Scroll, Spin | a1 = max | 1 | 0x97870 |
| 0x26 | `MessageType_ScrollSetMin` | none found | Scroll, Spin | a1 = min | 1 | 0x97870 |
| 0x27 | `MessageType_SetRange` | C_SB* handlers on 0x51, Memo_Update, Menu_PutCodenamesIntoControl | Scroll, Spin | a1 = min, a2 = max | 1 | 0x97870 |
| 0x28 | `MessageType_ListSetColumnWidth` | C_GCPAUSE, P_NFSTATS; menu data | List | a1 = column, a2 = width % (max 100) | 1 | 0x91b10 |
| 0x29 | `MessageType_SetPulseColour` | C_GCPAUSE, P_PAUSE | Label (+0x104), List (+0x540) | a1 = colour used when 0x70 pulsing is on | 1 | 0x90590, 0x8fff0 |
| 0x2a | `MessageType_ListSetOption564` | none found | List | a1 = byte stored in +0x564 (unknown) | 0 | 0x91b10 |
| 0x2b | `MessageType_SetState` | ~200 sites; menu data | Label, Button, Checkbox, Text, Scroll, Spin, Radio, List, Memo (control +0x7a) | a1 = 0 visible, 1 hidden, 2 shown but not selectable | 1 | 0x90590, 0x734d0 |
| 0x2c | `MessageType_ListSetTopRow` | List (FUN_00090f20) | List | a1 = first visible row | 1 | 0x91b10 |
| 0x2d | `MessageType_SetIconRect` | Menu_PlayIris, C_RBMPSTART; menu data | Label | a1 = x<<16\|y, a2 = w<<16\|h in the sprite sheet | 1 | 0x90590 |
| 0x2e | `MessageType_SetValue` | ~86 sites; menu data | Scroll (clamped, then 0x54 to parent), Spin (inverted), Checkbox (bool) | a1 = value | 1 / 0 out of range | 0x97870, 0x986f0, 0x71380 |
| 0x2f | `MessageType_GetText` | C_KEYBOARD, C_RBMPCNAME | Label, Button, Memo | - | char* | 0x90590 |
| 0x30 | `MessageType_GetColour` | Process_Create (fade) | Label | - | current tint | 0x90590, 0x95de0 |
| 0x31 | `MessageType_GetHighlightColour` | none found | Label, List, Memo | - | colour | 0x90590 |
| 0x32 | `MessageType_GetNormalColour` | none found | Label, List, Memo | - | colour | 0x90590 |
| 0x33 | `MessageType_GetNumItems` | XBox_DoSaveFlow, Menu_CreateOptionBox, MenuManager_Monitor | List (count), Radio (count - 1), Combo (its list) | - | count | 0x91b10, 0x96380 |
| 0x34 | `MessageType_GetSelectedIndex` | C_GCPAUSE, Menu_PutCodenamesIntoControl, C_RBMPCNAME | List, Radio | - | index | 0x91b10, 0x96380 |
| 0x35 | `MessageType_GetSelectedItemValue` | ~55 sites | List, Radio | - | value given to 0x10/0x11 (0 if nothing selected) | 0x91b10, 0x96380 |
| 0x38 | `MessageType_GetMax` | Menu_UpdateWheel, Menu_UpdateCodenameWheel | Scroll, Spin | - | max | 0x97870 |
| 0x39 | `MessageType_GetControl` | ~60 sites; __Menu_SendDelayed, Page_Update, Menu_PlayIris | every control type except Page and Manager | - | M_CONTROL* (self) | all *_SendMessage |
| 0x3a | `MessageType_GetMin` | Spin | Scroll, Spin | - | min | 0x97870 |
| 0x3b | `MessageType_GetSelectedItemText` | Combo_Update (to its List, which ignores it) | Radio (item text), Text (edit buffer) | - | char* | 0x96380, 0x98be0, 0x719b0 |
| 0x3c | `MessageType_ListGetTopRow` | none found | List | - | row | 0x91b10 |
| 0x3d | `MessageType_GetId` | none found | Button, Checkbox, Label, List, Radio, Scroll, Spin, Text | - | control +0x20 | 0x70e00 |
| 0x3e | `MessageType_GetIconSize` | none found | Label | - | w<<16\|h | 0x90590 |
| 0x3f | `MessageType_GetIconPos` | none found | Label | - | x<<16\|y | 0x90590 |
| 0x40 | `MessageType_GetValue` | ~117 sites (P_TWEAKS, wheels, C_CHCH*) | Scroll (clamped), Spin, Checkbox, Radio (index) | - | value | 0x97870 |
| 0x41 | `MessageType_GetVisibleRows` | none found | List (+0x500); Combo returns +0x718 | - | rows | 0x91b10, 0x71890 |
| 0x42 | `MessageType_CreateObject` | MenuManager_Create (record 0xfffffff9); Manager to the page | Manager: allocate + init by type (NEW_CONTROL +0x21); Page: add child | a1 = NEW_CONTROL* (to Manager) / M_CONTROL* (to Page) | new control or 0 | 0x92c40, 0x95820 |
| 0x43 | `MessageType_CreatePage` | MenuManager_Create (record 0xfffffff0) | Manager (Page_Init, list add, GoPage flags 0x19) | a1 = page hashcode | M_PAGE* | 0x92c40 |
| 0x44 | `MessageType_GoPage` | ~56 sites; Menu_ChangePageCloseIris (delayed), Manager 0x5f; menu scripts | Manager | a1 = page hashcode, a2 = flags (see below) | 1 | 0x92c40 |
| 0x45 | `MessageType_SetSidePage` | none found | Manager (+0x1c4) | a1 = page hashcode or 0; sends 0x4d/0x4c to the old/new page | 1 | 0x92c40 |
| 0x47 | `MessageType_Destroy` | Manager/Page cascade, MenuManager_Delete | all | - | 0 | all *_SendMessage |
| 0x48 | `MessageType_IsCursorOverFocused` | none found | Page | - | bool | 0x95820 |
| 0x49 | `MessageType_Scroll` | Scroll_Update (d-pad / left stick while focused), List/Radio (0x1d, 0x1e, 0x54) | List/Radio/Spin/Combo resync and forward; Page -> handlers (C_*); default script slot 3 | a1 = control; a2 = 0, or 1 when Radio re-announces after 0x54 | handler result | 0x97be0, 0x96380 |
| 0x4a | `MessageType_TextChanged` | TextBox_Update (character added / deleted) | Page -> handlers | a1 = textbox | - | 0x98de0 |
| 0x4b | `MessageType_Select` | Button/Checkbox/Label/Memo/Scroll updates: A pressed (START too when manager status > 3) | Page -> P_ handler (a1 = page, a2 = control) then C_ handler (a1 = control); default script slot 2 | a1 = control | handler result | 0x70fe0, 0x72e20, 0x95820 |
| 0x4c | `MessageType_PageEnter` | Manager GoPage / SetSidePage | handlers (P_*); page window script slot 0 | a1 = page, a2 = previous page hashcode | - | 0x92c40 |
| 0x4d | `MessageType_PageLeave` | Manager GoPage / SetSidePage, MenuManager_Delete | handlers; page window script slot 1 | a1 = page, a2 = GoPage flags (or 0) | - | 0x92c40, 0x942b0 |
| 0x4e | `MessageType_GainFocus` | Page_Update (control under the cursor changed) | handlers; default script slot 1 | a1 = new control | - | 0x95a80 |
| 0x4f | `MessageType_LoseFocus` | Page_Update | handlers; default script slot 4 | a1 = old control | - | 0x95a80 |
| 0x50 | `MessageType_PageUpdate` | Page_Update, every frame while the manager takes input | handlers (iris animation, message boxes) | a1 = page | - | 0x95a80 |
| 0x51 | `MessageType_ControlCreated` | MenuManager_Create after each control's records | handlers (init range/value); default script slot 0 | a1 = control | - | 0x93960 |
| 0x52 | `MessageType_MemoBack` | Memo_Update: B on a focused memo (controller 0 only) | List/Radio/Spin treat it like 0x49; Page -> handlers | a1 = control | - | 0x94cf0 |
| 0x53 | `MessageType_TextEndEntry` | none found | Text (leave edit mode, sends 0x00); Page forwards | - | 1 | 0x98be0 |
| 0x54 | `MessageType_ValueSet` | Scroll after 0x2e SetValue | List/Radio resync (Radio re-sends 0x49 a2=1); Page -> handlers (wheels redraw without animation) | a1 = control | - | 0x97870 |
| 0x56 | `MessageType_SetPageOption` | menu scripts | Manager | a1 = option (only 1 = navigation mode, page +0xc0), a2 = value | 1 | 0x92c40 |
| 0x58 | `MessageType_SetAlpha` | none found | Label (sprite alpha, +0x1c \|= 4), List (+0x544), Memo (+0x27c) | a1 = alpha (0..0x80) | 1 | 0x90590 |
| 0x59 | `MessageType_ListSetColumnAlign` | C_GCPAUSE, P_PAUSE, Menu_CreateOptionBox, P_ENDMISSION, XBox_DoSaveFlow | List (+0x51c[col]) | a1 = column, a2 = alignment | 1 | 0x91b10 |
| 0x5a | `MessageType_SetPlayerFocus` | P_MPJOIN, C_RBMP* (multiplayer setup) | Manager: page +0xb0[player] = control; 0x60 to the old, 0x5c to the new | a1 = control hashcode, a2 = player 0..3 | 1 if found | 0x92c40 |
| 0x5b | `(script event) BackPressed` | MenuManager_Update: B plays slot 5 of the focused control and skips Back if one exists | not dispatched as a message | - | - | 0x94370, 0x96c70 |
| 0x5c | `MessageType_PlayerFocusGained` | Manager 0x5a | handlers (C_RBMP*) | a1 = control | - | 0x92c40 |
| 0x5d | `MessageType_AltSelect1` | Button/Label/Memo/Scroll updates: Y pressed (ACTION_MENU_ALTSELECT_1) | Radio/List forward; Page -> P_ then C_ handler | a1 = control | - | 0x70fe0 |
| 0x5e | `MessageType_AltSelect2` | Button/Label/Memo/Scroll updates: X pressed (ACTION_MENU_ALTSELECT_2) | as 0x5d | a1 = control | - | 0x70fe0 |
| 0x5f | `MessageType_Back` | MenuManager_Update (B / BACK), handlers, delayed | Manager: 0x6b veto query, then pop the page stack (GoPage flags 5\|8), or 0x63 when empty | a1 = 0 normal / 1 forced (no 0x6b), a2 = player | 1 | 0x94370, 0x92c40 |
| 0x60 | `MessageType_PlayerFocusLost` | Manager 0x5a | handlers | a1 = control | - | 0x92c40 |
| 0x61 | `MessageType_SelectRepeat` | Button_Update: A held (auto-repeat) | Page -> handlers (C_KEYBOARD) | a1 = control | - | 0x70fe0 |
| 0x62 | `MessageType_SetTextFlag2000` | P_PAUSE, P_ENDMISSION, C_KEYPAD | Label (+0x10e -> text sprite flag 0x2000), List (+0x567) | a1 = bool | 1 | 0x90590, 0x8fff0 |
| 0x63 | `MessageType_BackAtRoot` | Manager 0x5f with an empty page stack | handlers (P_MPSCENARIO, P_NFMAP go to P_MAIN); page window script slot 2 | a1 = page, a2 = page hashcode | - | 0x92c40 |
| 0x64 | `MessageType_MemoSetOption280` | P_NFRESULTS | Memo (+0x280 byte) | a1 = byte | 1 | 0x95330 |
| 0x65 | `MessageType_MemoGetNumLines` | Menu_CreateOptionBox, XBox_DoSaveFlow, MenuManager_Monitor | Memo (+0x281) | - | line count | 0x95330 |
| 0x66 | `MessageType_MemoSetOption282` | same as 0x65, P_MPDEBRIEFING | Memo (+0x282 byte) | a1 = byte | 0 | 0x95330 |
| 0x68 | `MessageType_LockInput` | P_* handlers, Menu_ChangePageCloseIris, Menu_PlayMovie/StopMovie, MenuManager_Monitor, GoPage (unlocks) | Manager (+0x1d4; clears all actions) | a1 = 1 lock / 0 unlock | 1 | 0x92c40, 0x94370 |
| 0x69 | `MessageType_ScrollNoWrap` | P_CNAVOPTIONS | Scroll (+0x144 wrap = 0, +0x145 proportional bar = 1) | - | 1 | 0x97870 |
| 0x6a | `MessageType_SaveFlowDone` | XBox_DoSaveFlow, C_LBERROPTIONS | handlers (P_CNMENU) | a1 = page, a2 = result | - | 0x8f3f0 |
| 0x6b | `MessageType_QueryBack` | Manager 0x5f (to the player's focused control, then the page) | handlers | a1 = control/page, a2 = int*: write 0xfffffffe to keep the page | - | 0x92c40 |
| 0x6c | `MessageType_SetIconSpriteFlags` | P_PAUSE, P_ENDMISSION | Label (+0xec) | a1 = sprite flags | 1 | 0x90590 |
| 0x6d | `MessageType_BackOnControl` | Scroll_Update: B on a focused scroll (lists/wheels) | List forwards; Page -> P_ then C_ handler (C_LB*OPTIONS) | a1 = control | - | 0x97be0 |
| 0x6e | `MessageType_PageResume` | Manager GoPage with flag 8 and not 0x10 | handlers (P_MPBOTS...) | a1 = page, a2 = previous page hashcode | - | 0x92c40 |
| 0x6f | `MessageType_SetInputDelay` | P_START | Page (+0xcc) | a1 = frames after entering before input is taken | 0 | 0x95820, 0x95a80 |
| 0x70 | `MessageType_SetPulse` | P_LANGUAGE, XBox_DoSaveFlow | Label (+0x10f), List (+0x568) | a1 = bool | 0 | 0x90590 |
| 0x73 | `MessageType_ListSetSelectionColour` | P_NFSTATS | List (+0x548) | a1 = colour (guess) | 0 | 0x91b10 |
| 0x74 | `MessageType_GetCurrentPage` | none found | Manager | - | hashcode of the current page (or of the page under an overlay) | 0x92c40 |
| 0x75 | `MessageType_SetLineSpacing` | C_GCPAUSE, P_PAUSE, Menu_CreateOptionBox, MenuManager_Monitor... | List, Memo | a1 = row height in % of the font height (sets visible rows) | 0 | 0x91b10, 0x95330 |
| 0x76 | `MessageType_SetAlwaysHighlighted` | P_MPBOTSETUP | Label (+0x110) | a1 = bool | 0 | 0x90590 |
| 0xe1 | `(script process) ColourFade` | script keyframes, Label_Update | Process_Create/Process_RunFrame (not a message) | a1 = target colour, a2 = flag | - | 0x95de0, 0x97370 |

Unused ids: 0x01-0x0d, 0x36, 0x37, 0x46, 0x55, 0x57, 0x67, 0x71, 0x72, 0x77-0xe0 are not handled by any
dispatcher nor sent anywhere. (Handler decompiles show long `case` runs such as 0x4d-0x69 - those are jump-table
filler falling through to "return true", not real handling.)

## Messages in menu data

`MenuManager_Create` (0x93960) reads the menu file as records `0xfffffff?` + payload:

| Record | Payload | Effect |
|---|---|---|
| f0 | page hash, manager id, movie hash, byte, skip size | `0x43 CreatePage` if the manager id matches, else skip |
| f3 | byte id, a1, a2 | message to the manager (none in the retail files) |
| f4 | target hash, byte id, a1, a2 | script keyframe message (target -3 manager, -2 self) |
| f6 | byte id, a1, a2 | setup message to the control just created (0x1d is skipped; colour 0x7d6d59ff is replaced by 0x645a49ff for 0x1a-0x1c) |
| f8 | 32-byte font name | `0x23 SetFont` |
| f9 | control descriptor (30 bytes) | `0x42 CreateObject`; the previous control gets `0x51 ControlCreated` |
| f2 / unknown | - | last `0x51`, then `0x44 GoPage(startPage, 1)` and `0x22 SetCursor(0x10000002)` |

Ids actually present (front end / an in-game level): setup 0x11, 0x18, 0x1b, 0x1c, 0x1d, 0x21, 0x24, 0x28,
0x2b, 0x2d, 0x2e; script 0x22, 0x2b, 0x44, 0x56.

## Existing names

ui.h (and Ghidra's `MessageType`) names that are right: 0x14 ScriptExitLoop, 0x16 ScriptPlay, 0x18 SetText,
0x1a SetColour, 0x24 SetIcon, 0x2e SetValue, 0x39 GetControl, 0x42 CreateObject, 0x44 GoPage, 0x47 Destroy,
0x49 Scroll, 0x4b Select, 0x33 GetNumItems (Ghidra), 0x40 GetValue (Ghidra).

Right but should drop the "Maybe": 0x4e GainFocus, 0x4f LoseFocus, 0x5f (ReturnPrevPage -> Back), 0x4c
(EnterPage -> PageEnter), 0x47 (Ghidra MaybeDestroy).

Misleading or wrong:

| Id | Old name | Why | New name |
|---|---|---|---|
| 0x10 | AddTextToScroll | adds an item to a Radio (type 9, option picker) or List (type 6); a Scroll (type 10) has no items | AddItem |
| 0x17 | MaybeInitScroll | clears the items | ClearItems |
| 0x1e | SelectScrollItem | selects the item whose value matches a1 | SelectItemByValue |
| 0x35 | GetScrollValue | value of the selected list/radio item | GetSelectedItemValue |
| 0x39 | GetCombo (Ghidra) | every control returns itself | GetControl |
| 0x40 | GetWheelValue (ui.h) | generic value; wheels are Scroll controls | GetValue |
| 0x51 | MaybeGetWheelNumItems | sent once per control after the menu file creates it | ControlCreated |
| 0x52 | MaybeRadioSetIndex1 (Ghidra) | Memo sends it on B; lists/radios treat it as a scroll | MemoBack |
| 0x54 | Enter | a Scroll's value was set by code (0x2e); wheels redraw without animating | ValueSet |
| 0x5d / 0x5e | MaybeRadioSetIndex2/3 (Ghidra) | Y / X pressed on a control | AltSelect1 / AltSelect2 |
| 0x68 | MaybePlayMovie (Ghidra) | locks/unlocks menu input (manager +0x1d4) | LockInput |

`ControlType_Scroll = 0x0a` in ui.h is correct for the Scroll control, but the `SCROLL_*` macros talk to Radio
controls (type 9, the left/right option pickers: 275 of the 353 AddItem/ClearItems/SelectItemByValue/
GetSelectedItemValue sends with a known target) and List controls (type 6, 73).

## Control types (M_CONTROL +0x7b)

Ghidra already has `CONTROL_TYPE` with these values; type 4 is unused (no allocator in `Manager 0x42`, no case
in any switch). Handler prefixes follow the type (checked against the menu data): GO button (also C_KEYBOARD/C_KEYPAD keys, C_LANGUAGE), CH checkbox, LB list (also C_GCPAUSE, C_NIS), RB radio (but C_RBMPSTART/C_RBMPFINISH are Memo),
SB scroll/wheel. Radio is really a left/right option picker (a label plus an embedded Scroll that holds the index), not a group of
radio buttons.

| Id | Name | Notes |
|---|---|---|
| 1 (0x1) | `ControlType_Button` | Button_*; M_BUTTON 0x1ac; label at +0x94; prefix GO_ in handler names |
| 2 (0x2) | `ControlType_Checkbox` | Checkbox_*; 0x1ac; value byte +0x1a8; prefix CH |
| 3 (0x3) | `ControlType_Combo` | Combo_*; 0x71c; label + list |
| 5 (0x5) | `ControlType_Label` | Label_*; M_LABEL 0x114; also the iris/wheel picture |
| 6 (0x6) | `ControlType_List` | List_*; M_LIST 0x56c; scroll at +0x128; prefix LB (list box). The 'scroll' of ui.h's SCROLL_* macros |
| 7 (0x7) | `ControlType_Manager` | M_MANAGER 0x1d8 |
| 8 (0x8) | `ControlType_Page` | M_PAGE 0xd0 |
| 9 (0x9) | `ControlType_Radio` | Radio_*; 0x308; label +0x94, scroll +0x1a8; prefix RB |
| 10 (0xa) | `ControlType_Scroll` | Scroll_*; M_SCROLL 0x148; value/min/max slider; the iris wheels are Scroll controls (prefix SB) |
| 11 (0xb) | `ControlType_Spin` | Spin_*; 0x318; label + scroll, value inverted |
| 12 (0xc) | `ControlType_Text` | Text_*/TextBox_Update; M_TEXTBOX 0x9b0; on-screen character grid |
| 13 (0xd) | `ControlType_Window` | Window_*; 0x9c; a page's background/script holder (Window_Get) |
| 200 (0xc8) | `ControlType_Memo` | Memo_*; 0x284; multi-line text with scroll at +0x94 |

## Struct fields learned

Offsets are into the objects as the Xbox binary uses them. Ghidra's `M_CONTROL` is only 0x7c long and misses
the slot table; `M_MANAGER` calls the current page `someHashcode` and the manager index `mCtrl`.

### M_CONTROL

| Offset | Size | Name | Type | Meaning |
|---|---|---|---|---|
| 0x0 | 0x4 | next | M_CONTROL* | next in the page's control list (page +0x94) |
| 0x4 | 0x4 | prev | M_CONTROL* | previous; Page_Update walks the list from the tail through this |
| 0xc | 0x4 | parent | M_CONTROL* | page for controls, manager for pages; notifications go here |
| 0x10 | 0x4 | extraControlHash | HASHCODE | page only: a control Page_Update also updates (looked up with 0x39) |
| 0x18 | 0x4 | hashcode | HASHCODE | C_/P_ hashcode, used by Handler_HandleMessage |
| 0x1c | 0x4 | drawState | uint | 0x10 normal, 0x20 focused, 0x40 pressed/checked, 0x80 cursor over; textbox bit31 = editing, Label bit2 = fixed alpha |
| 0x20 | 0x4 | id | int | 0x20 SetId / 0x3d GetId; matched by __Menu_SendEx (== item) and __Menu_SendEx2 (bitmask) |
| 0x24 | 0x4 | framesFocused | uint | counter reset when focus arrives (Page_Update) |
| 0x28 | 0x10 | scripts | LINKEDLIST | scripts owned by the control |
| 0x38 | 0x4 | selectedScript | M_SCRIPT* |  |
| 0x3c | 0x4 | scriptPlaying | M_KEYFRAME* |  |
| 0x50 | 0x4 | lastScriptResult | int | result of the previous script message; a script arg1 of -4 means 'use this' |
| 0x58 | 0x4 | scriptExecutionFlags | uint |  |
| 0x5c | 0x10 | processes | LINKEDLIST | running processes (0xe1 colour fades) |
| 0x70 | 0x2 | x | short | relative to the page |
| 0x72 | 0x2 | y | short |  |
| 0x74 | 0x2 | width | short |  |
| 0x76 | 0x2 | height | short |  |
| 0x78 | 0x2 | depth | ushort | draw/hit order; lower wins in Menu_CursorOverMe |
| 0x7a | 0x1 | state | byte | 0x2b SetState: bit0 hidden, bit1 not selectable, bit2 'was hidden' (internal); navigation skips controls with state != 0 |
| 0x7b | 0x1 | type | CONTROL_TYPE | see ControlType |
| 0x7c | 0x1c | defaultScripts | M_SCRIPT*[7] | 0x21 SetScript slots; Script_PlayDefault: 0 = 0x4c/0x51, 1 = 0x4d/0x4e, 2 = 0x4b/0x63, 3 = 0x49, 4 = 0x4f, 5 = 0x5b (B); pages use their Window's table |

### M_MANAGER (0x1d8 bytes, `manager[5]`)

| Offset | Size | Name | Type | Meaning |
|---|---|---|---|---|
| 0x0 | 0x7c | control | M_CONTROL | type = 7 |
| 0x94 | 0x4 | cursorSprite | sprite* | the cursor; its position picks the focused control |
| 0x98 | 0x10 | pages | LINKEDLIST | all pages (link at page +0xa4) |
| 0xa8 | 0x8 | stack | STACKINFO | page history for Back: 8-byte entries {M_PAGE*, focused control} |
| 0xb0 | 0x100 | stackMem | int[64] |  |
| 0x1b0 | 0x4 | currentMovie | HASHCODE | background movie now playing (page +0xc4) |
| 0x1b4 | 0x4 | status | uint | MenuManager_GetStatus; > 3 means START also selects |
| 0x1b8 | 0x4 | startPage | HASHCODE | MenuManager_Create param_2 |
| 0x1bc | 0x4 | currentPage | M_PAGE* | Ghidra 'someHashcode' (it is a pointer) |
| 0x1c0 | 0x4 | underPage | M_PAGE* | page left visible under an overlay (GoPage flag 2) |
| 0x1c4 | 0x4 | sidePage | M_PAGE* | 0x45 SetSidePage |
| 0x1c8 | 0x4 | closeAction | GameActions_tag | action that deletes this manager (MenuManager_Monitor) |
| 0x1cc | 0x2 | whichPlayer | short | -1 = any controller |
| 0x1ce | 0x1 | startPlayer | byte |  |
| 0x1cf | 0x1 | field_0x1cf | byte | set to 7 when creation finishes |
| 0x1d0 | 0x1 | index | byte | manager number (Ghidra mCtrl); first argument of every handler |
| 0x1d1 | 0x1 | exists | bool |  |
| 0x1d2 | 0x1 | takingInput | bool | Page_Update param; controls only react to input when set |
| 0x1d3 | 0x1 | overlayMode | bool | set by GoPage flag 2; makes Back add flag 8 |
| 0x1d4 | 0x1 | inputLocked | bool | 0x68 LockInput |
| 0x1d5 | 0x1 | didPauseAudio | bool |  |

### M_PAGE (0xd0 bytes, starts with M_CONTROL)

| Offset | Size | Name | Type | Meaning |
|---|---|---|---|---|
| 0x94 | 0x10 | controls | LINKEDLIST | the page's controls |
| 0xa4 | 0x8 | pageLink | LINK | next/prev in the manager's page list |
| 0xac | 0x4 | focused | M_CONTROL* | control under the cursor |
| 0xb0 | 0x10 | playerFocus | M_CONTROL*[4] | 0x5a SetPlayerFocus; while any is set the d-pad does not move the cursor |
| 0xc0 | 0x4 | navigationMode | int | 0x56 option 1: 1 = four-way (default), 2 = up/down, 3 = left/right, other = none |
| 0xc4 | 0x4 | backgroundMovie | HASHCODE | from the page record; 0 stops the movie |
| 0xc8 | 0x4 | framesShown | int | reset by GoPage; frame 1 finds the help control |
| 0xcc | 0x4 | inputDelay | int | 0x6f; no input until framesShown reaches it |

## Open questions

- 0x52: only Memo_Update sends it (B, pad 0 only), yet List/Radio/Spin treat it exactly like 0x49 Scroll.
  Either a second scroll-like source existed (PS2?) or the name should be something more general.
- 0x00 TextEntryDone never reaches a handler (Page_SendMessage drops it). Check whether a textbox is ever
  parented to something other than a page, or whether the text-entry pages read the buffer on 0x4a/0x4b.
- Unconfirmed meanings: 0x13 (argument taken as index from the register flow), 0x15 (bar hidden), 0x2a (List
  +0x564), 0x58 (alpha for Label, unknown for List/Memo), 0x62 (sprite flag 0x2000 - shadow?), 0x64/0x66
  (Memo +0x280/+0x282), 0x69 (no wrap + proportional bar), 0x73 (List +0x548, a colour), 0x0e/0x59 alignment
  values (1..3; Font_GetAlignment uses 2 right, 3 centre).
- 0x13, 0x15, 0x1f, 0x26, 0x31, 0x32, 0x3c-0x3f, 0x45, 0x48, 0x53, 0x58, 0x74 have handlers but no sender in
  code or menu data (possibly used by scripts in other menu files, or leftovers).
- Radio 0x33 returns count - 1 while List returns count; Radio 0x20 sets the last item's value while
  Button/Scroll 0x20 set +0x20 - both look like original quirks, keep them.
- Only two menu files were parsed (front end crc 0x1ef0f73f and one level 0x3c9eb1e6); the other ~28 level
  bundles have their own copies (pause menu), probably identical.
- PS2 names were not consulted for message ids (the PS2 build has no enum either as far as known).
