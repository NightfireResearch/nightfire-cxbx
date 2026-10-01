# Menu handler inventory (default.xbe)

Every page (`P_*`) and control (`C_*`) handler dispatched by `Handler_HandleMessage` (0x8e320): 99 handlers, 13 already reimplemented
(`tools/uihandler.py` `implemented`). Sizes are Ghidra function bodies. "Messages" are the message types the handler acts on (cases that
only `return true` are left out; names are the ones in `ui.h` or guesses marked ?). Sub-controls are 0x10xxxxxx hashcodes the handler uses
that are not in the dispatch list; the proposed names are in `items.json` (`subcontrols`). Labels are text labels passed to `Txt_BindLabel`
or written into M_ITEMs, found in the disassembly and in the decompiler output (best effort: labels chosen through tables are missed).

Difficulty: **trivial** = a page of iris / wheel / one global, only helpers already declared; **moderate** = several engine calls, game or
multiplayer state, save-state fields, or a few hundred bytes of logic; **hard** = large, calls unnamed `FUN_` functions, launches the game,
or owns save data / scripts / movies.

P_MPBOTCHOOSE_Handler is only 41 bytes in Ghidra because its EnterPage branch was split off into `FUN_0008aa49` (0x8aa49-0x8ab6b); the
numbers below include it.

## Proposed order

1. M_ITEM handlers not yet reimplemented, simplest first (their companion iris pages are slotted next to them): C_SBNFDFCTY, P_NFDFCTY, P_MPOPTIONS, P_MPBOTS, C_SBNFMAP, P_NFMAP, C_SBBOTS, C_SBCNOPTIONS, P_CNMENU, P_MPBOTCHOOSE, C_SBMPBTCHOOSE, C_SBMPSCEN, C_SBMPOPTIONS, C_RBMPSETUP, P_MPDEBRIEFING, P_MPCONFIRM.
2. The rest, trivial to hard, then by size: C_CHCHBLIND, C_CHCHHEALTH, C_CHCHCOORDS, C_CHCHDRAWALL, C_CHCHLOCKUP, C_CHCHBRIGHT, C_CHCHCONTROLS, C_CHCHDEBUG, C_CHCHALLOWFREEZE, C_CHCHDUMMY, C_CHCHUNLOCK, C_CHCHDRONES, C_CHCHZEROG, C_CHCHMUSIC, C_CHCHFLY, C_MPDBG, P_NIS, P_FMVPLAYER, P_ESTHERO, C_LBMSGOPTIONS, C_GONIGHTFIRE, C_GOMULTIPLAYER, C_GOCODENAMES, P_INTRO, C_CHCHWS, C_LBPMMAP, C_CHCHWEAP, C_CHCHHUD, C_KEYPAD, C_RBCONTROL, P_CHEATMEDAL, P_CNCONTROLS, P_FMVTEST, C_LBERROPTIONS, P_WINGAME, P_MAIN, P_TRAILER, P_PARISENUM, P_ENDMISSION, P_ATTRACT, P_START, C_SBNFCN, C_LANGUAGE, C_RBMPFINISH, P_NFBONUS, C_RBDSRECORDS, C_RBMPCNAME, P_CNMPOPTIONS, P_CNNAME, C_SBCNSELECT, P_MPSETUP, C_RBDSREWARDS, P_LANGUAGE, C_RBMPSTART, P_CNAVOPTIONS, P_NFSTATS, P_CNOPTIONS, P_MPRULES, C_KEYBOARD, P_NFSELECT, P_CREDITS, P_CNSELECT, P_MPJOIN, P_PAUSE, P_NFRESULTS, C_NIS, P_TWEAKS2, P_MPBOTSETUP, P_TWEAKS, C_GCPAUSE.

Before step 1: add `difficulty`, `mp_characters`, `mp_characters_small`, `mp_options`, `cn_options`, `mp_bots` (and `sp_level` as real data
instead of the Menu.h address macro) next to their handlers, fix the `mp_scenario` disabled label, and note that `sp_level`, `mp_scenario`,
`mp_characters(_small)` are written by engine functions that are still original code (Menu_SetNightfireStatus, Menu_SetLevelBonus,
Menu_SpecialCodenameCheck, Menu_UnlockMPSettings, Menu_UnlockMPSkins, Menu_GetMPSkins, Menu_GetLevelIndex...): once the lists move into
the source those functions must be reimplemented or patched to use the new copies, or the unlock state will diverge.

## Step 1: handlers that use M_ITEM lists

### 1. C_SBNFDFCTY_Handler - 0x00086410, 195 bytes

- Difficulty: **trivial** - wheel over `difficulty`; only known helpers
- What it does: Scroll/Enter: Menu_UpdateWheel(difficulty). Select: if unlocked, GameState.difficultyModifier = identifier (1..3), close iris to P_NFMAP. 0x51 (init): SetRange 0..2.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter
- M_ITEM lists: `difficulty` (enabled, identifier, list/icon)
- Pages referenced (targets or previous-page checks): P_NFMAP
- Sub-controls: 0x100000be `SUB_C_SBNFDFCTY_ICON`, 0x100000e6 `SUB_C_SBNFDFCTY_WHEEL_TEXT`, 0x10000102 `SUB_C_SBNFMAP_IRIS`, 0x10000104 `SUB_C_SBNFDFCTY_IRIS`, 0x100001ee `SUB_C_SBNFDFCTY_DESCRIPTION_TEXT`
- Labels: -
- Calls: Menu_UpdateWheel, Menu_ChangePageCloseIris

### 2. P_NFDFCTY_Handler - 0x000863a0, 112 bytes

- Difficulty: **trivial** - iris page, companion of C_SBNFDFCTY
- What it does: Enter (0x4c): start iris (0 when coming from P_NFSELECT and reset wheel to item 0, else 4). 0x50: play iris. No list itself.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_NFSELECT
- Controls: C_SBNFDFCTY
- Sub-controls: 0x10000104 `SUB_C_SBNFDFCTY_IRIS`
- Labels: -
- Calls: Menu_PlayIris, Menu_StartIris

### 3. P_MPOPTIONS_Handler - 0x00088e00, 153 bytes

- Difficulty: **trivial** - iris page plus one M_ITEM write
- What it does: Enter: select mp_options item 0 when arriving from P_MPSETUP; sets mp_options[1].enabled = (map != HT_Level_Ravine) - the AI Bots item is greyed out on Ravine. 0x50: play iris, Menu_UpdateOptionBox(NULL).
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: `mp_options` (enabled, list/icon)
- Pages referenced (targets or previous-page checks): P_MPSETUP
- Controls: C_SBMPOPTIONS
- Sub-controls: 0x10000107 `SUB_C_SBMPOPTIONS_IRIS`
- Labels: -
- Calls: Menu_PlayIris, Menu_UpdateOptionBox, Menu_StartIris, Menu_SelectItemInControl

### 4. P_MPBOTS_Handler - 0x0008a7f0, 108 bytes

- Difficulty: **trivial** - iris page, companion of C_SBBOTS
- What it does: Enter or 0x6e: start iris, set C_SBBOTS to CurrentlyEditingBotIdx + 1. No list itself.
- Messages: 0x4c EnterPage, 0x50 Tick, 0x6e
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MPOPTIONS
- Controls: C_SBBOTS
- Sub-controls: 0x10000113 `SUB_C_SBBOTS_IRIS`
- Labels: -
- Calls: Menu_PlayIris, Menu_StartIris

### 5. C_SBNFMAP_Handler - 0x00086690, 337 bytes

- Difficulty: **moderate** - starts a single player level (GameFlow_PushState, MPSettings reset, controller port)
- What it does: Wheel over sp_level. Select: if unlocked (or menu_unlock_everything) set GameState.NextLevelHashcode = identifier, single player MPSettings, PlayerInputs[0].controllerPort = Menu_GetLastController(), ResetMap_LevelToLoad, GameFlow_PushState(7, 80.0, 0xff), MenuManager_Delete, SFXFadeDown. 0x51: SetRange 0..11, SetValue 0. 0x5d (Y): close iris to P_DOSSIER.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter, 0x5d ButtonY?
- M_ITEM lists: `sp_level` (enabled, identifier, list/icon)
- Pages referenced (targets or previous-page checks): P_DOSSIER
- Sub-controls: 0x1000001f `SUB_C_SBNFMAP_DESCRIPTION_TEXT`, 0x10000020 `SUB_C_SBNFMAP_ICON`, 0x100000e3 `SUB_C_SBNFMAP_WHEEL_TEXT`, 0x10000102 `SUB_C_SBNFMAP_IRIS`
- Labels: -
- Calls: Menu_UpdateWheel, Menu_GetLastController, ResetMap_LevelToLoad, GameFlow_PushState, MenuManager_Delete, SFXFadeDown, Menu_ChangePageCloseIris

### 6. P_NFMAP_Handler - 0x000864f0, 413 bytes

- Difficulty: **moderate** - level_chosen state machine, GameFlow_PushState, stack check
- What it does: Enter: reset controller ports, Menu_RestartFrontEndLoop, iris (0 from P_NFDFCTY), button-hint text (0x283 or 0x208 depending on whether the page stack is empty), select the last unlocked sp_level. 0x50: play iris; level_chosen countdown shows 0x1000022c then loads the level. 0x63: GoPage P_MAIN.
- Messages: 0x4c EnterPage, 0x50 Tick, 0x63 Back?
- M_ITEM lists: `sp_level` (enabled)
- Pages referenced (targets or previous-page checks): P_MAIN, P_NFDFCTY
- Controls: C_SBNFMAP
- Sub-controls: 0x10000102 `SUB_C_SBNFMAP_IRIS`, 0x1000010e `SUB_P_NFMAP_BUTTON_HINT_TEXT`, 0x1000022c `SUB_P_NFMAP_LOADING_PANEL`
- Labels: 0x208 `BUTTONS_SELECT_DOSSIER_SCROLL_BACK`, 0x283 `BUTTONS_SELECT_DOSSIER_SCROLL_MAINMENU`
- Calls: Manager_SendMessage, Menu_PlayIris, ResetMap_LevelToLoad, GameFlow_PushState, MenuManager_Delete, SFXFadeDown, Menu_RestartFrontEndLoop, Menu_StartIris, Stack_IsEmpty

### 7. C_SBBOTS_Handler - 0x0008a860, 414 bytes

- Difficulty: **moderate** - writes icons into `mp_bots` at run time, reads mpbots, sprintf into a global
- What it does: Scroll/Enter: item 0 (Continue) gets the Quick Game icon; for a bot, "Playing : Yes/No" text into 0x10000243 and the bot skin icon copied from mp_characters (Menu_GetItemFromHash) into mp_bots[i]; then Menu_UpdateWheel(mp_bots). Select: item 0 returns, else CurrentlyEditingBotIdx = i-1 and close iris to P_MPBOTCHOOSE. 0x51: SetRange 0..6 (only 6 bots shown), SetValue 1.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter
- M_ITEM lists: `mp_bots` (list/icon), `mp_characters` (list/icon)
- Pages referenced (targets or previous-page checks): P_MPBOTCHOOSE
- Sub-controls: 0x10000110 `SUB_C_SBBOTS_WHEEL_TEXT`, 0x10000112 `SUB_C_SBBOTS_ICON`, 0x10000113 `SUB_C_SBBOTS_IRIS`, 0x10000243 `SUB_C_SBBOTS_PLAYING_TEXT`
- Labels: 0x181 `TXT_YES`, 0x182 `TXT_NO`, 0x295 `MP_CFG_BOT_PLAYING`
- Icons: 0x300013a `ICON_MPSCENARIO_QUICKGAME`
- Calls: sprintf, Menu_GetItemFromHash, Menu_UpdateWheel, Manager_SendMessage, Menu_ChangePageCloseIris

### 8. C_SBCNOPTIONS_Handler - 0x0008c7f0, 486 bytes

- Difficulty: **moderate** - touches the save/codename state `ls` and Menu_UpdateMessageBox
- What it does: Wheel over cn_options. Select: 0 Secret Unlocks -> GoPage P_CNNAME (the secrets page is P_CNNAME in cheat mode), 1 -> P_CNCONTROLS, 2 -> page 0x4000003e (driving controller; no handler), 3 -> P_CNOPTIONS, 4 -> P_CNMPOPTIONS, 5 -> P_CNAVOPTIONS, 6 Save Codename -> Menu_UpdateDefaultCodename or the save message box. 0x51: SetRange 0..6.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter
- M_ITEM lists: `cn_options` (list/icon)
- Pages referenced (targets or previous-page checks): P_CNNAME, P_CNCONTROLS, P_CNOPTIONS, P_CNMPOPTIONS, P_CNAVOPTIONS, 0x4000003e
- Sub-controls: 0x100000ff `SUB_C_SBCNOPTIONS_WHEEL_TEXT`, 0x10000109 `SUB_C_SBCNOPTIONS_IRIS`, 0x100001a5 `SUB_C_SBCNOPTIONS_ICON`, 0x100001ec `SUB_C_SBCNOPTIONS_DESCRIPTION_TEXT`
- Labels: -
- Calls: Menu_UpdateWheel, Manager_SendMessage, Menu_UpdateDefaultCodename, Menu_ChangePageCloseIris, Menu_UpdateMessageBox

### 9. P_CNMENU_Handler - 0x0008c5f0, 455 bytes

- Difficulty: **moderate** - save state `ls`, option/message boxes, cheat_mode
- What it does: Enter: clears cn_modified_flag, selects cn_options item 1, "Edit %s" title (0x010001e4 with the codename) into 0x100001f9, and rewrites cn_options[6].title: 0x14b "Codenames" when ls.field4_0x10 < 2 else 0x1f4 "Save Codename". 0x50: play iris, message/option boxes. 0x6a / 0x6b: leave page, "lose your changes?" box (0x010002d3).
- Messages: 0x4c EnterPage, 0x50 Tick, 0x6a, 0x6b
- M_ITEM lists: `cn_options` (list/icon, title)
- Pages referenced (targets or previous-page checks): P_CNSELECT, P_CNNAME, P_CNCONTROLS
- Controls: C_SBCNOPTIONS
- Sub-controls: 0x10000108 `SUB_C_SBCNSELECT_IRIS`, 0x10000109 `SUB_C_SBCNOPTIONS_IRIS`, 0x100001f9 `SUB_P_CNMENU_TITLE_TEXT`
- Labels: 0x10001e4 `CN_EDIT_CODENAME_FMT`, 0x10002d3 `EXIT_LOSING_CHANGES_CONFIRM`
- Calls: Menu_StartIris, Menu_SelectItemInControl, sprintf, Manager_SendMessage, Menu_CreateOptionBox, Menu_ChangePageCloseIris, Menu_PlayIris, Menu_UpdateMessageBox, Menu_UpdateOptionBox

### 10. P_MPBOTCHOOSE_Handler - 0x0008aa20, 332 bytes

- Difficulty: **moderate** - Ghidra splits the body into FUN_0008aa49; builds the run-time list `mp_stuff`
- What it does: Enter: Menu_UnlockMPSkins(0xff), iris, copies all 29 mp_characters into mp_stuff (0x245338) and clears .enabled for good characters in a non-team game once a good bot exists (Menu_IsBotGood) and for characters 0/12/14 (Bond variants) once one is taken; SetRange 0..28, SetValue bot skin. 0x50: play iris. Fix the function boundary (0x8aa20-0x8ab6b) before porting.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: `mp_characters` (identifier, list/icon), `mp_stuff` (enabled, list/icon)
- Pages referenced (targets or previous-page checks): P_MPBOTS
- Controls: C_SBMPBTCHOOSE
- Sub-controls: 0x1000018d `SUB_C_SBMPBTCHOOSE_IRIS`
- Labels: -
- Calls: Menu_PlayIris, Menu_UnlockMPSkins, Menu_StartIris, Menu_IsBotGood

### 11. C_SBMPBTCHOOSE_Handler - 0x0008ab70, 613 bytes

- Difficulty: **moderate** - copies BOT_getDefaultStats into mpbots, writes MPSettings.Players names
- What it does: Wheel over mp_stuff. Scroll/Enter: load default stats for the highlighted skin into mpbots.bot[CurrentlyEditingBotIdx]. Select: if enabled, set skin, team (Menu_IsBotGood), the unique-good-bot/Bond flags (DAT_002456b0/b1), copy the character name (mp_characters title) into MPSettings.Players[4+i].Name, GoPage P_MPBOTSETUP.
- Messages: 0x49 Scroll, 0x4b Select, 0x54 Enter
- M_ITEM lists: `mp_stuff` (enabled, list/icon), `mp_characters` (list/icon)
- Pages referenced (targets or previous-page checks): P_MPBOTSETUP
- Sub-controls: 0x1000018d `SUB_C_SBMPBTCHOOSE_IRIS`, 0x1000018f `SUB_C_SBMPBTCHOOSE_WHEEL_TEXT`, 0x10000194 `SUB_C_SBMPBTCHOOSE_ICON`, 0x100001f6 `SUB_C_SBMPBTCHOOSE_DESCRIPTION_TEXT`
- Labels: -
- Calls: BOT_getDefaultStats, Menu_UpdateWheel, Menu_IsBotGood, Menu_GetItemFromHash, Manager_SendMessage

### 12. C_SBMPSCEN_Handler - 0x00087460, 638 bytes

- Difficulty: **moderate** - Quick Game builds a whole match (Rand_Random, bots, MPSettings)
- What it does: Wheel over mp_scenario. Select: item 0 (Quick Game) = Arena, 10 points, 10 minutes, random mp_level of the first 7 (never Ravine), 3 bots with skins 1/3/2 and default stats, names from mp_characters, then P_MPCONFIRM; any other item sets MPSettings.GameMode = identifier and goes to P_MPMAP. 0x51: SetRange 0..12.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter
- M_ITEM lists: `mp_scenario` (enabled, identifier, list/icon), `mp_level` (identifier), `mp_characters` (list/icon)
- Pages referenced (targets or previous-page checks): P_MPMAP, P_MPCONFIRM
- Sub-controls: 0x1000009b `SUB_C_SBMPSCEN_DESCRIPTION_TEXT`, 0x1000009d `SUB_C_SBMPSCEN_ICON`, 0x100000ea `SUB_C_SBMPSCEN_WHEEL_TEXT`, 0x10000106 `SUB_C_MPSCENARIO_IRIS`
- Labels: -
- Calls: Menu_UpdateWheel, Menu_PrepareBots, Rand_Random, BOT_getDefaultStats, Menu_GetItemFromHash, Menu_ChangePageCloseIris

### 13. C_SBMPOPTIONS_Handler - 0x00088ea0, 771 bytes

- Difficulty: **moderate** - team counting over the player/bot tables, option boxes with sprintf
- What it does: Wheel over mp_options. Select 0 (Continue): Menu_PrepareBots, count good/bad players and bots; errors 0x01000311 (good vs good in a non-team game), 0x389 "no players on %s team", 0x39e "at least two players"; else GoPage P_MPCONFIRM. 1 -> P_MPBOTS, 2 -> P_MPRULES, 3 -> P_MPPLAYERMODS, 4 -> P_MPENVIROMODS. 0x51: one-time default bot skins 6..11, SetRange 0..4.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter
- M_ITEM lists: `mp_options` (enabled, list/icon)
- Pages referenced (targets or previous-page checks): P_MPRULES, P_MPPLAYERMODS, P_MPBOTS, P_MPENVIROMODS, P_MPCONFIRM
- Sub-controls: 0x100000f3 `SUB_C_SBMPOPTIONS_WHEEL_TEXT`, 0x100000f5 `SUB_C_SBMPOPTIONS_ICON`, 0x10000107 `SUB_C_SBMPOPTIONS_IRIS`, 0x100001ef `SUB_C_SBMPOPTIONS_DESCRIPTION_TEXT`
- Labels: 0x1c7 `MP_TEAM_PHOENIX`, 0x1c8 `MP_TEAM_MI6`, 0x389 `MP_NO_PLAYERS_ON_TEAM`, 0x39e `MP_NOT_ENOUGH_PLAYERS`, 0x1000311 `MP_GOOD_GUYS_WRONG_TEAM`
- Calls: Menu_UpdateWheel, Manager_SendMessage, Menu_ChangePageCloseIris, Menu_PrepareBots, Menu_CreateOptionBox, sprintf

### 14. C_RBMPSETUP_Handler - 0x00088720, 1700 bytes

- Difficulty: **hard** - 1700 bytes, per-player setup rows, unnamed FUN_000753f0, Menu_GetMPSkins
- What it does: Per-player row control of P_MPSETUP (instance number in field14_0x20): choose codename/team/character/handicap; team icons 0x030001a2/0x030001a3; reads mp_characters_small by skin; many messages (0x49, 0x4b, 0x54, 0x5c, 0x6b).
- Messages: 0x49 Scroll, 0x4b Select, 0x54 Enter, 0x5c, 0x6b
- M_ITEM lists: `mp_characters_small` (list/icon)
- Pages referenced (targets or previous-page checks): P_MPOPTIONS
- Controls: C_RBMPSETUP, C_RBMPFINISH
- Sub-controls: 0x100001a0 `SUB_P_MPSETUP_ROW_ICON`, 0x100001a2 `SUB_P_MPSETUP_ROW_PROMPT_TEXT`
- Labels: 0x1c7 `MP_TEAM_PHOENIX`, 0x1c8 `MP_TEAM_MI6`, 0x296 `MP_HEALTH_HANDICAP`, 0x37c `CFG_CHOOSE_TEAM`, 0x37d `CFG_CHOOSE_CHARACTER`, 0x39c `MP_PLAYER_READY`
- Icons: 0x30001a1 `ICON_MPSETUP_UNKNOWN_1A1`, 0x30001a2 `ICON_TEAM_MI6`
- Calls: __Menu_SendEx, Menu_GetItemFromHash, Manager_SendMessage, FUN_000753f0, Menu_IsBotGood, Menu_GetMPSkins

### 15. P_MPDEBRIEFING_Handler - 0x0008b890, 1337 bytes

- Difficulty: **hard** - 1337 bytes, scores table, Menu_GetMPScore, per-row SendEx
- What it does: Multiplayer results: sorts scores, fills rows 0x10000156-0x1000015c and 0x10000240 with names, icons from mp_characters_small, "%s wins" / draw texts.
- Messages: 0x4c EnterPage
- M_ITEM lists: `mp_characters_small` (list/icon)
- Controls: C_MPDBG
- Sub-controls: 0x10000156 `SUB_P_MPDEBRIEFING_TEXT_156`, 0x10000157 `SUB_P_MPDEBRIEFING_ICON`, 0x10000158 `SUB_P_MPDEBRIEFING_TEXT_158`, 0x10000159 `SUB_P_MPDEBRIEFING_TEXT_159`, 0x1000015a `SUB_P_MPDEBRIEFING_TEXT_15A`, 0x1000015b `SUB_P_MPDEBRIEFING_PANEL`, 0x1000015c `SUB_P_MPDEBRIEFING_TEXT_15C`, 0x1000023d `SUB_P_MPDEBRIEFING_TEXT_23D`, 0x10000240 `SUB_P_MPDEBRIEFING_TEXT_240`
- Labels: 0x4 `KEY_3`, 0x1c7 `MP_TEAM_PHOENIX`, 0x1c8 `MP_TEAM_MI6`, 0x100029f `MP_X_WINS`, 0x10002a0 `MP_DRAW_DESC`
- Calls: Menu_RestartFrontEndLoop, Menu_GetMPScore, __Menu_SendEx, Menu_GetItemFromHash, Menu_GetBotShortName, __ftol2, sprintf

### 16. P_MPCONFIRM_Handler - 0x000891e0, 2315 bytes

- Difficulty: **hard** - 2315 bytes, launches the match (Menu_StoreMPSettings, ResetMap_LevelToLoad, GameFlow_PushState)
- What it does: Summary page: map/scenario/weapon set/limits/friendly fire texts (0x1000022d-0x10000232) built with sprintf from mp_level, mp_scenario and labels 0x3d4-0x3da; player/team columns 0x10000233-0x1000023a with mp_characters_small icons; Select starts the game.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: `mp_characters_small` (list/icon), `mp_level` (list/icon), `mp_scenario` (list/icon)
- Sub-controls: 0x1000022d `SUB_P_MPCONFIRM_MAP_TEXT`, 0x1000022e `SUB_P_MPCONFIRM_SCENARIO_TEXT`, 0x1000022f `SUB_P_MPCONFIRM_WEAPONSET_TEXT`, 0x10000230 `SUB_P_MPCONFIRM_LIMIT_TEXT`, 0x10000231 `SUB_P_MPCONFIRM_POINTS_TEXT`, 0x10000232 `SUB_P_MPCONFIRM_FRIENDLYFIRE_TEXT`, 0x10000233 `SUB_P_MPCONFIRM_ICON`, 0x10000234 `SUB_P_MPCONFIRM_PANEL_234`, 0x10000235 `SUB_P_MPCONFIRM_PANEL_235`, 0x10000236 `SUB_P_MPCONFIRM_PANEL_236`, 0x10000237 `SUB_P_MPCONFIRM_TEXT_237`, 0x10000238 `SUB_P_MPCONFIRM_TEXT_238`, 0x10000239 `SUB_P_MPCONFIRM_PANEL_239`, 0x1000023a `SUB_P_MPCONFIRM_PANEL_23A`, 0x1000023e `SUB_P_MPCONFIRM_PANEL_23E`, 0x1000023f `SUB_P_MPCONFIRM_BOTS_TEXT`
- Labels: 0x1a0 `MP_WEAPSET_NORMAL`, 0x1a2 `MP_WEAPSET_PISTOLS`, 0x1a4 `MP_WEAPSET_AUTOMATIC`, 0x1a6 `MP_WEAPSET_SNIPERS`, 0x1a8 `MP_WEAPSET_EXPLOSIVES`, 0x1b2 `MP_RANDOM`, 0x1b6 `MP_OFF`, 0x1b7 `MP_ON`, 0x1c8 `MP_TEAM_MI6`, 0x24d `MP_CONFIRM_WEAPONSET`, 0x24f `MP_WEAPSET_EXPLOSIVES2`, 0x295 `MP_CFG_BOT_PLAYING`, 0x3ca `MP_UNLIMITED`, 0x3d4 `MP_CONFIRM_MAP`, 0x3d5 `MP_CONFIRM_SCENARIO`, 0x3d6 `MP_CONFIRM_DURATION`, 0x3d7 `MP_CONFIRM_LIVES`, 0x3d8 `MP_CONFIRM_POINTS`, 0x3da `MP_CONFIRM_FRIENDLYFIRE`, 0x3db `MP_WEAPSET_MI6`, 0x3dc `MP_WEAPSET_PHOENIX`, 0x3dd `MP_WEAPSET_MODERN`, 0x3de `MP_WEAPSET_STEALTHY`, 0x10001de `MP_MINUTES`
- Calls: __Menu_SendEx, sprintf, Menu_GetItemFromHash, Menu_StoreMPSettings, ResetMap_LevelToLoad, GameFlow_PushState, SFXFadeDown, MenuManager_Delete

## Step 2: remaining handlers

### 1. C_CHCHBLIND_Handler - 0x00081b00, 67 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle: Select stores the value in switch_BLIND_DRONES, 0x51 shows it.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 2. C_CHCHHEALTH_Handler - 0x00081c10, 67 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 3. C_CHCHCOORDS_Handler - 0x00081d80, 67 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 4. C_CHCHDRAWALL_Handler - 0x00081f30, 67 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 5. C_CHCHLOCKUP_Handler - 0x00081fe0, 67 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 6. C_CHCHBRIGHT_Handler - 0x00082030, 67 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 7. C_CHCHCONTROLS_Handler - 0x000820d0, 67 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 8. C_CHCHDEBUG_Handler - 0x00081d30, 69 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 9. C_CHCHALLOWFREEZE_Handler - 0x00081e90, 69 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 10. C_CHCHDUMMY_Handler - 0x00081ee0, 69 bytes

- Difficulty: **trivial** - cheat toggle: one global
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 11. C_CHCHUNLOCK_Handler - 0x00082080, 69 bytes

- Difficulty: **trivial** - cheat toggle: menu_unlock_everything
- What it does: Select stores menu_unlock_everything (0x25d79e), 0x51 shows it.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 12. C_CHCHDRONES_Handler - 0x00081ab0, 80 bytes

- Difficulty: **trivial** - cheat toggle
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: -

### 13. C_CHCHZEROG_Handler - 0x00081f80, 88 bytes

- Difficulty: **trivial** - cheat toggle + Player_ChangeSubState
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Player_ChangeSubState

### 14. C_CHCHMUSIC_Handler - 0x000819f0, 91 bytes

- Difficulty: **trivial** - cheat toggle + SFXMusicSetVolume
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: SFXMusicSetVolume

### 15. C_CHCHFLY_Handler - 0x00081a50, 93 bytes

- Difficulty: **trivial** - cheat toggle + Player_ChangeSubState
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Player_ChangeSubState

### 16. C_MPDBG_Handler - 0x0008bdd0, 105 bytes

- Difficulty: **trivial** - debug: restore MP settings or relaunch
- What it does: Select: Menu_RestoreMPSettings, GoPage P_MPSCENARIO. 0x5d: relaunch the last map.
- Messages: 0x4b Select, 0x5d ButtonY?
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MPSCENARIO
- Sub-controls: -
- Labels: -
- Calls: ResetMap_LevelToLoad, GameFlow_PushState, MenuManager_Delete, Menu_RestoreMPSettings, Manager_SendMessage

### 17. P_NIS_Handler - 0x00082120, 107 bytes

- Difficulty: **trivial** - scripted scene tick
- What it does: 0x50: Script_Update(FMVScript); when finished re-enable player and camera.
- Messages: 0x50 Tick
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Script_Update, control_movement_object_handler, Script_IsPlaying, Player_Enable, Camera_PopStates, Camera_Enable

### 18. P_FMVPLAYER_Handler - 0x00085cd0, 123 bytes

- Difficulty: **trivial** - movie page
- What it does: Plays movie_hashcode (chosen on P_FMVTEST) then returns.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Menu_PlayMovie, psiMovieLoop

### 19. P_ESTHERO_Handler - 0x000852f0, 124 bytes

- Difficulty: **trivial** - movie page
- What it does: Plays FMV_TITLES then GoPage P_MAIN.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MAIN
- Sub-controls: -
- Labels: -
- Calls: Menu_PlayMovie, psiMovieLoop

### 20. C_LBMSGOPTIONS_Handler - 0x000761c0, 126 bytes

- Difficulty: **trivial** - two messages, one global
- What it does: Message box list: Select / 0x6d set DAT_00224f5c and return to the previous page.
- Messages: 0x4b Select, 0x6d
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Manager_SendMessage

### 21. C_GONIGHTFIRE_Handler - 0x000858a0, 135 bytes

- Difficulty: **trivial** - main menu button
- What it does: Select: fade out main menu (Process_Create on 0x100000ed), delayed GoPage P_NFSELECT.
- Messages: 0x4b Select
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_NFSELECT
- Sub-controls: 0x100000ed `SUB_P_MAIN_MENU_GROUP`
- Labels: -
- Calls: Process_Create, __Menu_SendDelayedMessage

### 22. C_GOMULTIPLAYER_Handler - 0x00085930, 135 bytes

- Difficulty: **trivial** - main menu button
- What it does: Select: same as C_GONIGHTFIRE, delayed GoPage P_MPJOIN.
- Messages: 0x4b Select
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MPJOIN
- Sub-controls: 0x100000ed `SUB_P_MAIN_MENU_GROUP`
- Labels: -
- Calls: Process_Create, __Menu_SendDelayedMessage

### 23. C_GOCODENAMES_Handler - 0x000859c0, 135 bytes

- Difficulty: **trivial** - main menu button
- What it does: Select: same as C_GONIGHTFIRE, delayed GoPage P_CNSELECT.
- Messages: 0x4b Select
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_CNSELECT
- Sub-controls: 0x100000ed `SUB_P_MAIN_MENU_GROUP`
- Labels: -
- Calls: Process_Create, __Menu_SendDelayedMessage

### 24. P_INTRO_Handler - 0x00085240, 164 bytes

- Difficulty: **trivial** - movie page
- What it does: Plays the EA and MGM idents then GoPage P_START.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_START
- Sub-controls: -
- Labels: -
- Calls: psiMovieLoop, Manager_SendMessage, Menu_PlayMovie

### 25. C_CHCHWS_Handler - 0x00081dd0, 187 bytes

- Difficulty: **trivial** - cheat toggle + Camera_CalcViewAngles
- What it does: Tweaks-page toggle (widescreen).
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Camera_CalcViewAngles

### 26. C_LBPMMAP_Handler - 0x00081930, 189 bytes

- Difficulty: **trivial** - debug level list
- What it does: List of levels (adds HT_Level_Menu_Pre, selects current); Select loads the level.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_START
- Sub-controls: -
- Labels: -
- Calls: ResetMap_LevelToLoad, GameFlow_PushState, SFXFadeDown, MenuManager_Delete

### 27. C_CHCHWEAP_Handler - 0x00081b50, 189 bytes

- Difficulty: **trivial** - cheat toggle + Player_EquipWeapon
- What it does: Tweaks-page toggle (all weapons).
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Player_EquipWeapon, Player_CheckWeaponsLoaded

### 28. C_CHCHHUD_Handler - 0x00081c60, 204 bytes

- Difficulty: **trivial** - cheat toggle + HUD_Enable
- What it does: Tweaks-page toggle.
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: HUD_Enable

### 29. C_KEYPAD_Handler - 0x00083fa0, 218 bytes

- Difficulty: **trivial** - decoder keypad digits
- What it does: Select: append the digit (instance number) to GlobalVars.DecoderDisplay, show in 0x10000075. 0x51: "****".
- Messages: 0x4b Select, 0x51 Init
- M_ITEM lists: -
- Sub-controls: 0x10000075 `SUB_C_KEYPAD_ENTRY_TEXT`
- Labels: -
- Calls: sprintf

### 30. C_RBCONTROL_Handler - 0x0008d000, 234 bytes

- Difficulty: **trivial** - controller scheme list
- What it does: Scroll: Menu_DisplayControllerStyle. Select: Menu_ChangeControllerStyle, inversion. 0x5e (X): toggle inverted text 0x100000c9.
- Messages: 0x49 Scroll, 0x4b Select, 0x5e ButtonX?
- M_ITEM lists: -
- Sub-controls: 0x100000c9 `SUB_C_RBCONTROL_INVERT_TEXT`
- Labels: 0x206 `CONTROL_INVERTED`, 0x3bd `CONTROL_NORMAL`
- Calls: Menu_ChangeControllerStyle, Manager_SendMessage, Menu_DisplayControllerStyle

### 31. P_CHEATMEDAL_Handler - 0x00083d10, 288 bytes

- Difficulty: **trivial** - debug medal award
- What it does: Select: choice from 0x1000021c sets switch_channels[99/100] with a medal level.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Sub-controls: 0x1000021c `SUB_P_CHEATMEDAL_MEDAL_SCROLL`
- Labels: -
- Calls: MenuManager_Delete

### 32. P_CNCONTROLS_Handler - 0x0008ce70, 386 bytes

- Difficulty: **trivial** - fills the scheme list
- What it does: Enter: adds the 8 controller schemes (labels 0x28..0x73, 0x0100016d, 0x0100017c) to C_RBCONTROL.
- Messages: 0x4c EnterPage
- M_ITEM lists: -
- Controls: C_RBCONTROL
- Sub-controls: 0x100000c9 `SUB_C_RBCONTROL_INVERT_TEXT`
- Labels: 0x28 `CONTROLSCHEME_NIGHTFIRE`, 0x37 `CONTROLSCHEME_MOONRAKER`, 0x46 `CONTROLSCHEME_OCTOPUSSY`, 0x55 `CONTROLSCHEME_GOLDFINGER`, 0x64 `CONTROLSCHEME_DRNO`, 0x73 `CONTROLSCHEME_THUNDERBALL`, 0x206 `CONTROL_INVERTED`, 0x3bd `CONTROL_NORMAL`, 0x100016d `CONTROLSCHEME_GOLDENEYE`, 0x100017c `CONTROLSCHEME_CLASSICBOND`
- Calls: Menu_DisplayControllerStyle

### 33. P_FMVTEST_Handler - 0x00085a50, 633 bytes

- Difficulty: **trivial** - debug movie list
- What it does: Enter: fills 0x10000229 with 22 movie hashcodes (strings are in .rdata, not text labels); Select: movie_hashcode = value, GoPage P_FMVPLAYER.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_FMVPLAYER
- Sub-controls: 0x10000229 `SUB_P_FMVTEST_MOVIE_SCROLL`
- Labels: -
- Calls: -

### 34. C_LBERROPTIONS_Handler - 0x0008fbd0, 231 bytes

- Difficulty: **moderate** - save-error box: LS_FlushStates, WriteStateFileAndLaunch
- What it does: Error option list for save failures.
- Messages: 0x4b Select, 0x6d
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: WriteStateFileAndLaunch, Manager_SendMessage, Menu_ClearDelayedMessages, LS_FlushStates

### 35. P_WINGAME_Handler - 0x0008ddd0, 252 bytes

- Difficulty: **moderate** - background movie + music handling
- What it does: End-game movie, then GoPage P_CREDITS.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_CREDITS
- Sub-controls: -
- Labels: -
- Calls: psiMovieFinished, Menu_RestartFrontEndLoop, Manager_SendMessage, SFXUnPause, SFXUnPauseAllStreams, psiStopBackgroundMovie, SFXRemove, SFXGetVolume, psiStartBackgroundMovie

### 36. P_MAIN_Handler - 0x00085780, 279 bytes

- Difficulty: **moderate** - Process_Create fade, idle timer
- What it does: Main menu page: fade-in process, attract mode after idle.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_ATTRACT
- Sub-controls: 0x100000ed `SUB_P_MAIN_MENU_GROUP`, 0x10000225 `SUB_P_MPBOTSETUP_INFO_TEXT`
- Labels: -
- Calls: Menu_GetNoInputCount, Manager_SendMessage, Menu_ResetNoInputCount, Menu_RestartFrontEndLoop, __Menu_SendDelayedMessage, Process_Create

### 37. P_TRAILER_Handler - 0x0008dca0, 291 bytes

- Difficulty: **moderate** - background movie + music handling
- What it does: Plays FMV_TRAILER_DIEANOTHERDAY.
- Messages: 0x4b Select, 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: psiMovieFinished, Menu_RestartFrontEndLoop, Manager_SendMessage, SFXUnPause, SFXUnPauseAllStreams, psiStopBackgroundMovie, SFXRemove, SFXGetVolume, psiStartBackgroundMovie

### 38. P_PARISENUM_Handler - 0x00085630, 322 bytes

- Difficulty: **moderate** - mini mission (Menu_RunMiniMission), unnamed FUN_0008fd90/FUN_0008fcc0
- What it does: Paris mini mission / enumeration page.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_ESTHERO
- Sub-controls: 0x1000022c `SUB_P_NFMAP_LOADING_PANEL`
- Labels: -
- Calls: Menu_RunMiniMission, Menu_UpdateOptionBox, FUN_0008fd90, Menu_WillRunMiniMission, FUN_0008fcc0, Manager_SendMessage, Menu_UpdateMessageBox

### 39. P_ENDMISSION_Handler - 0x00083e50, 326 bytes

- Difficulty: **moderate** - restart/quit a mission (Player_RamLoad, ResetMap_LevelToLoad)
- What it does: Select on 0x100001a4: 0 restart from checkpoint, 1 restart mission, 2 quit to P_NFMAP.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_NFMAP
- Sub-controls: 0x10000001 `SUB_P_ENDMISSION_ITEM`, 0x1000006f `SUB_P_PAUSE_BUTTON_HINT_TEXT`, 0x10000184 `SUB_P_PAUSE_BACKGROUND`, 0x100001a4 `SUB_P_ENDMISSION_CHOICE_SCROLL`
- Labels: -
- Calls: ResetMap_LevelToLoad, Mission_BaseMapHCode, Player_RamLoad, SFXFadeDown, GameFlow_PushState, MenuManager_Delete

### 40. P_ATTRACT_Handler - 0x00085370, 334 bytes

- Difficulty: **moderate** - background movie + music handling
- What it does: Attract-mode movie after the idle timeout.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MAIN
- Sub-controls: -
- Labels: -
- Calls: psiMovieFinished, Menu_RestartFrontEndLoop, Manager_SendMessage, SFXUnPause, SFXUnPauseAllStreams, psiStopBackgroundMovie, Menu_PlayMovie, SFXRemove, SFXGetVolume, psiStartBackgroundMovie

### 41. P_START_Handler - 0x000854c0, 359 bytes

- Difficulty: **moderate** - idle timer, delayed messages, legal text
- What it does: Press START page: legal text 0x375, blinking "Press START", attract mode after 0xa8c idle frames.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_ATTRACT
- Sub-controls: 0x100000fa `SUB_P_START_PRESS_START_TEXT`, 0x10000197 `SUB_P_START_LEGAL_TEXT`
- Labels: 0x2fb `PRESS_START`, 0x375 `LEGAL_NOTICE`
- Calls: Menu_GetNoInputCount, Manager_SendMessage, Menu_RestartFrontEndLoop, sprintf, __Menu_SendDelayed, Menu_ResetNoInputCount

### 42. C_SBNFCN_Handler - 0x000861a0, 467 bytes

- Difficulty: **moderate** - codename wheel, default codename, unnamed FUN_0008fcc0/FUN_0007fef0
- What it does: Nightfire codename selection wheel (Menu_UpdateCodenameWheel, not an M_ITEM list).
- Messages: 0x49 Scroll, 0x4b Select, 0x54 Enter, 0x5d ButtonY?
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_CNNAME, P_NFSELECT
- Controls: C_SBCNOPTIONS
- Sub-controls: 0x100000e4 `SUB_C_SBNFCN_WHEEL_TEXT`, 0x10000103 `SUB_C_SBNFCN_IRIS`, 0x1000010e `SUB_P_NFMAP_BUTTON_HINT_TEXT`, 0x100001cc `SUB_C_SBNFCN_ICON`, 0x100001f2 `SUB_C_SBNFCN_DESCRIPTION_TEXT`
- Labels: 0x208 `BUTTONS_SELECT_DOSSIER_SCROLL_BACK`, 0x10000bb `DEFAULT_PROFILE_NO_SAVE_WARNING`
- Calls: FUN_0008fcc0, Menu_MapDefaultCodename, Menu_ChangePageCloseIris, Menu_CreateOptionBox, FUN_0007fef0, Menu_UpdateCodenameWheel

### 43. C_LANGUAGE_Handler - 0x000843e0, 487 bytes

- Difficulty: **moderate** - Txt_SetLanguage / SFXSetLanguage, unnamed FUN_000dbdc0
- What it does: Language buttons (instance number -> tLANGUAGE).
- Messages: 0x4b Select, 0x4e GainFocus
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_INTRO
- Sub-controls: 0x1000006f `SUB_P_PAUSE_BUTTON_HINT_TEXT`
- Labels: -
- Calls: Txt_SetLanguage, SFXSetLanguage, FUN_000dbdc0, Manager_SendMessage

### 44. C_RBMPFINISH_Handler - 0x00088530, 487 bytes

- Difficulty: **moderate** - per-player ready rows
- What it does: "Press A when ready" rows.
- Messages: 0x5c, 0x60, 0x6b
- M_ITEM lists: -
- Controls: C_RBMPSETUP
- Sub-controls: 0x100001a0 `SUB_P_MPSETUP_ROW_ICON`, 0x100001a2 `SUB_P_MPSETUP_ROW_PROMPT_TEXT`
- Labels: 0x1e9 `PRESS_A_WHEN_READY`
- Calls: Manager_SendMessage, __Menu_SendEx, Process_Destroy

### 45. P_NFBONUS_Handler - 0x00084be0, 509 bytes

- Difficulty: **moderate** - level bonus display (Menu_GetLevelBonuses)
- What it does: Bonus/reward page after a mission.
- Messages: 0x4b Select, 0x4c EnterPage, 0x5d ButtonY?, 0x5e ButtonX?
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_NFMAP, P_DOSSIER, P_NFSTATS, P_WINGAME
- Sub-controls: 0x100001cf `SUB_P_NFBONUS_ICON`, 0x100001d0 `SUB_P_NFBONUS_TEXT`
- Labels: -
- Calls: Menu_GetLevelBonuses, sprintf, Manager_SendMessage

### 46. C_RBDSRECORDS_Handler - 0x00086ed0, 533 bytes

- Difficulty: **moderate** - score tables, PlrStats_GetLevelTotals, medals
- What it does: Records row: best score, medal icon (0x0300013d-0x03000140) and text.
- Messages: 0x49 Scroll
- M_ITEM lists: -
- Sub-controls: 0x10000175 `SUB_C_RBDSRECORDS_TEXT_175`, 0x10000176 `SUB_C_RBDSRECORDS_PANEL`, 0x10000177 `SUB_C_RBDSRECORDS_TEXT_177`, 0x10000178 `SUB_C_RBDSRECORDS_TEXT_178`, 0x100001f8 `SUB_C_RBDSRECORDS_MEDAL_TEXT`
- Labels: 0x306 `MEDAL_PLATINUM`, 0x307 `MEDAL_GOLD`, 0x308 `MEDAL_SILVER`, 0x309 `MEDAL_BRONZE`
- Icons: 0x300013d `ICON_MEDAL_BRONZE`, 0x300013e `ICON_MEDAL_GOLD`, 0x300013f `ICON_MEDAL_PLATINUM`, 0x3000140 `ICON_MEDAL_SILVER`
- Calls: PlrStats_GetLevelTotals, Menu_GetLevelBonuses, sprintf, PlrStarts_ProcessRewardCounter

### 47. C_RBMPCNAME_Handler - 0x000880a0, 535 bytes

- Difficulty: **moderate** - per-player codename choice, unnamed FUN_0007fef0
- What it does: Codename row on P_MPSETUP.
- Messages: 0x4b Select, 0x5c, 0x6b
- M_ITEM lists: -
- Controls: C_RBMPSTART
- Sub-controls: 0x100001a0 `SUB_P_MPSETUP_ROW_ICON`, 0x100001a2 `SUB_P_MPSETUP_ROW_PROMPT_TEXT`
- Labels: 0x1c3 `PLAYER`
- Calls: Manager_SendMessage, __Menu_SendEx, Menu_MapDefaultCodename, FUN_0007fef0, sprintf

### 48. P_CNMPOPTIONS_Handler - 0x0008d560, 565 bytes

- Difficulty: **moderate** - codename MP options, cn_modified_flag
- What it does: Scrollers 0x10000128/0x1000011c/0x10000129 <-> MPSettings.Players[0] and PlayerInputs[0].autoaimMp.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Sub-controls: 0x1000011c `SUB_P_CNMPOPTIONS_HEALTH_SCROLL`, 0x10000128 `SUB_P_CNMPOPTIONS_PLAYER_TOGGLE_SCROLL`, 0x10000129 `SUB_P_CNMPOPTIONS_AUTOAIM_SCROLL`
- Labels: 0x1b6 `MP_OFF`, 0x1b7 `MP_ON`
- Calls: Manager_SendMessage

### 49. P_CNNAME_Handler - 0x0008ca10, 570 bytes

- Difficulty: **moderate** - keyboard page shared by new codename and Secret Unlocks
- What it does: Title 0x10000242: "Enter New Codename" or "Secret Unlocks" (cheat_mode); PlrStats_ResetScoring.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_CNMENU
- Controls: C_KEYBOARD
- Sub-controls: 0x10000075 `SUB_C_KEYPAD_ENTRY_TEXT`, 0x10000242 `SUB_P_CNNAME_TITLE_TEXT`
- Labels: 0x1f5 `ENTER_NEW_CODENAME`, 0x10002d4 `SECRET_UNLOCKS`
- Calls: Menu_UpdateMessageBox, Menu_UpdateOptionBox, Manager_SendMessage, PlrStats_ResetScoring, __Menu_SendEx

### 50. C_SBCNSELECT_Handler - 0x0008c360, 597 bytes

- Difficulty: **moderate** - codename wheel, delete/create, unnamed FUN_0008fcc0/FUN_0007fef0
- What it does: Codenames wheel (Menu_UpdateCodenameWheel).
- Messages: 0x49 Scroll, 0x4b Select, 0x54 Enter, 0x5d ButtonY?, 0x5e ButtonX?
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_CNSELECT, P_CNMENU, P_CNNAME
- Controls: C_SBCNOPTIONS
- Sub-controls: 0x100000ec `SUB_C_SBCNSELECT_WHEEL_TEXT`, 0x10000108 `SUB_C_SBCNSELECT_IRIS`, 0x100001cd `SUB_C_SBCNSELECT_ICON`, 0x100001f1 `SUB_C_SBCNSELECT_DESCRIPTION_TEXT`, 0x10000220 `SUB_C_SBCNSELECT_BUTTON_HINT_TEXT`
- Labels: 0x320 `CODENAME_DELETE_CONFIRM`, 0x10000b4 `BUTTONS_EDIT_CREATE_DELETE_SCROLL_BACK`, 0x10000b9 `BUTTONS_EDIT_CREATE_SCROLL_BACK`
- Calls: FUN_0008fcc0, Menu_MapDefaultCodename, Menu_ChangePageCloseIris, FUN_0007fef0, Menu_CreateOptionBox, Menu_UpdateCodenameWheel

### 51. P_MPSETUP_Handler - 0x000882c0, 623 bytes

- Difficulty: **moderate** - Menu_UpdateMPControllers, Menu_GetMPSkins
- What it does: Multiplayer controller setup page.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Controls: C_RBMPSETUP, C_RBMPFINISH
- Sub-controls: 0x100001a0 `SUB_P_MPSETUP_ROW_ICON`, 0x100001a2 `SUB_P_MPSETUP_ROW_PROMPT_TEXT`
- Labels: 0x1c7 `MP_TEAM_PHOENIX`, 0x1c8 `MP_TEAM_MI6`, 0x37c `CFG_CHOOSE_TEAM`, 0x37d `CFG_CHOOSE_CHARACTER`
- Calls: Menu_UpdateMPControllers, __Menu_SendEx, Menu_GetMPSkins, Manager_SendMessage

### 52. C_RBDSREWARDS_Handler - 0x00087140, 624 bytes

- Difficulty: **moderate** - Menu_GetLevelBonuses, 8 sub-controls
- What it does: Rewards row: reward names/icons per medal.
- Messages: 0x49 Scroll, 0x54 Enter
- M_ITEM lists: -
- Sub-controls: 0x100001d2 `SUB_C_RBDSREWARDS_TEXT_1D2`, 0x100001d3 `SUB_C_RBDSREWARDS_TEXT_1D3`, 0x100001d4 `SUB_C_RBDSREWARDS_TEXT_1D4`, 0x100001d5 `SUB_C_RBDSREWARDS_TEXT_1D5`, 0x100001d6 `SUB_C_RBDSREWARDS_ICON_1D6`, 0x100001d7 `SUB_C_RBDSREWARDS_ICON_1D7`, 0x100001d8 `SUB_C_RBDSREWARDS_ICON_1D8`, 0x100001d9 `SUB_C_RBDSREWARDS_ICON_1D9`
- Labels: -
- Calls: Menu_GetLevelBonuses

### 53. P_LANGUAGE_Handler - 0x00084130, 638 bytes

- Difficulty: **moderate** - unnamed FUN_00084080/FUN_000840d0/FUN_000dbdc0, Txt_SetLanguage
- What it does: Language selection page.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_INTRO
- Controls: C_LANGUAGE
- Sub-controls: 0x1000006f `SUB_P_PAUSE_BUTTON_HINT_TEXT`
- Labels: -
- Calls: Menu_GetNoInputCount, Txt_SetLanguage, FUN_00084080, FUN_000dbdc0, Manager_SendMessage, FUN_000840d0, __Menu_SendEx

### 54. C_RBMPSTART_Handler - 0x00087dd0, 664 bytes

- Difficulty: **moderate** - per-player join rows, Process_Destroy, Menu_MPFadeWhenReady
- What it does: "Press A to join" rows on P_MPSETUP.
- Messages: 0x4b Select, 0x5c, 0x60, 0x6b
- M_ITEM lists: -
- Controls: C_RBMPCNAME
- Sub-controls: 0x100001a0 `SUB_P_MPSETUP_ROW_ICON`, 0x100001a2 `SUB_P_MPSETUP_ROW_PROMPT_TEXT`
- Labels: 0x1e8 `PRESS_A_TO_JOIN`, 0x1e9 `PRESS_A_WHEN_READY`, 0x37b `CFG_CHOOSE_CODENAME`, 0x39c `MP_PLAYER_READY`
- Icons: 0x30001a0 `ICON_MPSETUP_JOIN`
- Calls: Process_Destroy, __Menu_SendEx, Menu_MPFadeWhenReady, Manager_SendMessage, Menu_PutCodenamesIntoControl

### 55. P_CNAVOPTIONS_Handler - 0x0008d7a0, 991 bytes

- Difficulty: **moderate** - volumes (SFX/music), subtitles, split screen, screen adjust page
- What it does: AV options page; sliders 0x10000134/0x10000135 x5 into SFXSetVolume/SFXMusicSetVolume.
- Messages: 0x4b Select, 0x4c EnterPage, 0x4d, 0x4e GainFocus, 0x4f LoseFocus, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_CNMENU, P_SCREENADJUST
- Sub-controls: 0x10000134 `SUB_P_CNAVOPTIONS_SFX_VOLUME_SLIDER`, 0x10000135 `SUB_P_CNAVOPTIONS_MUSIC_VOLUME_SLIDER`, 0x10000136 `SUB_P_CNAVOPTIONS_SUBTITLES_SCROLL`, 0x10000137 `SUB_P_CNAVOPTIONS_SCREEN_ADJUST_BUTTON`, 0x10000198 `SUB_P_CNAVOPTIONS_SCROLL`, 0x1000019a `SUB_P_CNAVOPTIONS_SPLITSCREEN_SCROLL`, 0x10000223 `SUB_P_CNAVOPTIONS_PANEL`
- Labels: 0x1b6 `MP_OFF`, 0x1b7 `MP_ON`, 0x2b9 `MP_SPLIT_DIR_VERTICAL`, 0x2ba `MP_SPLIT_DIR_HORIZONTAL`
- Calls: SFXGetVolume, SFXMusicGetVolume, SFXMusicSetVolume, SFXSetVolume, Manager_SendMessage

### 56. P_NFSTATS_Handler - 0x00084e10, 1065 bytes

- Difficulty: **moderate** - score rows (Menu_AddRow...)
- What it does: Mission statistics table.
- Messages: 0x4c EnterPage
- M_ITEM lists: -
- Sub-controls: 0x10000022 `SUB_P_NFSTATS_TABLE`
- Labels: 0x15a `STATS_OPPONENTS`, 0x162 `STATS_SUBTOTAL`, 0x163 `STATS_TOTAL`, 0x164 `STATS_DIFFICULTY_BONUS`, 0x10001e1 `STATS_CURRENT`, 0x10001e2 `STATS_TARGET`, 0x1000204 `STATS_CATEGORY`
- Calls: PlrStat_GetScore, __ftol2, Menu_AddRow, Menu_AddRowPercentage, SeparateNumber

### 57. P_CNOPTIONS_Handler - 0x0008d0f0, 1124 bytes

- Difficulty: **moderate** - 8 scrollers bound to PlayerInputs[0]
- What it does: Advanced options: vibration, auto-aim, crosshair, crouch mode, manual aim mode, auto weapon switch, flashing objects, HUD.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Sub-controls: 0x1000011a `SUB_P_CNOPTIONS_VIBRATION_SCROLL`, 0x1000011b `SUB_P_CNOPTIONS_AUTOAIM_SCROLL`, 0x10000124 `SUB_P_CNOPTIONS_CROSSHAIR_SCROLL`, 0x10000125 `SUB_P_CNOPTIONS_CROUCH_MODE_SCROLL`, 0x10000126 `SUB_P_CNOPTIONS_MANUAL_AIM_MODE_SCROLL`, 0x100001cb `SUB_P_CNOPTIONS_AUTO_SWITCH_WEAPONS_SCROLL`, 0x10000226 `SUB_P_CNOPTIONS_FLASHING_OBJECTS_SCROLL`, 0x1000023b `SUB_P_CNOPTIONS_HUD_SCROLL`
- Labels: 0x1b6 `MP_OFF`, 0x1b7 `MP_ON`, 0x2a2 `OPTION_TOGGLE`, 0x2a3 `OPTION_HOLD`
- Calls: Manager_SendMessage

### 58. P_MPRULES_Handler - 0x00089b20, 1398 bytes

- Difficulty: **moderate** - 1398 bytes of per-mode limit tables
- What it does: Duration and points/lives scrollers; labels change by game mode.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Sub-controls: 0x10000008 `SUB_P_MPRULES_DURATION_SCROLL`, 0x100000f6 `SUB_P_MPRULES_POINTS_SCROLL`
- Labels: 0x24c `MP_RULES_POINTS`, 0x3ca `MP_UNLIMITED`, 0x3d3 `MP_RULES_LIVES`
- Calls: Manager_SendMessage

### 59. C_KEYBOARD_Handler - 0x0008cc50, 535 bytes

- Difficulty: **hard** - Menu_SpecialCodenameCheck (the cheat code table), unnamed FUN_00076330/FUN_00075f40, save data
- What it does: On-screen keyboard: codename entry and cheat codes.
- Messages: 0x4b Select, 0x61
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_CNMENU
- Sub-controls: -
- Labels: 0x31f `CODENAME_OVERWRITE_CONFIRM`, 0x100029d `UNLOCK_SUCCESS`
- Calls: Menu_SpecialCodenameCheck, Menu_CreateOptionBox, FUN_00076330, FUN_00075f40, PlrStats_ResetScoring, Menu_UpdateMessageBox

### 60. P_NFSELECT_Handler - 0x00085d50, 1044 bytes

- Difficulty: **hard** - codename/save management, unnamed FUN_0007edf0/FUN_0008fd90
- What it does: Nightfire (campaign) codename select page.
- Messages: 0x4c EnterPage, 0x50 Tick, 0x6a, 0x6b, 0x6e
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MAIN, P_NFDFCTY
- Controls: C_SBNFCN, C_SBCNSELECT
- Sub-controls: 0x100000e4 `SUB_C_SBNFCN_WHEEL_TEXT`, 0x100000ee `SUB_P_NFSELECT_MENU_GROUP`, 0x10000103 `SUB_C_SBNFCN_IRIS`, 0x100001cc `SUB_C_SBNFCN_ICON`, 0x100001f2 `SUB_C_SBNFCN_DESCRIPTION_TEXT`
- Labels: -
- Calls: FUN_0007edf0, Menu_StartIris, Menu_UpdateMessageBox, __Menu_SendDelayedMessage, Process_Create, Menu_ChangePageCloseIris, Menu_UpdateCodenameWheel, Menu_PlayIris, Menu_PutCodenamesIntoControl, Menu_UpdateOptionBox, FUN_0008fd90, Menu_MapDefaultCodename

### 61. P_CREDITS_Handler - 0x0008ded0, 1069 bytes

- Difficulty: **hard** - Menu_SetupCredits (huge), music, unnamed FUN_0007fd70
- What it does: Credits page (see PLAN step 3).
- Messages: 0x4c EnterPage, 0x4d, 0x50 Tick, 0x51 Init
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MAIN, P_WINGAME
- Sub-controls: 0x10000213 `SUB_P_CREDITS_CONTROL_213`, 0x10000214 `SUB_P_CREDITS_CONTROL_214`, 0x10000215 `SUB_P_CREDITS_CONTROL_215`, 0x1000023c `SUB_P_CREDITS_CONTROL_23C`
- Labels: -
- Calls: Menu_InitCredits, Menu_SetupCredits, SFXMusicGetVolume, FUN_0007fd70, SFXStartMusic, __profiling_or_debugging_hook_point, SFXStopMusic, SFXMusicSetVolume, Menu_RestartFrontEndLoop, __Menu_SendEx, __ftol2, __Menu_SendDelayedMessage, Process_Create

### 62. P_CNSELECT_Handler - 0x0008be40, 1244 bytes

- Difficulty: **hard** - codename/save management, unnamed FUN_000dfec0/FUN_0008fd90/FUN_0007edf0
- What it does: Codenames page: create/edit/delete.
- Messages: 0x4c EnterPage, 0x50 Tick, 0x6a, 0x6b, 0x6e, other (catch-all)
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MAIN, P_CNMENU
- Controls: C_SBCNSELECT
- Sub-controls: 0x100000ec `SUB_C_SBCNSELECT_WHEEL_TEXT`, 0x10000108 `SUB_C_SBCNSELECT_IRIS`, 0x10000122 `SUB_P_CNSELECT_MENU_GROUP`, 0x100001cd `SUB_C_SBCNSELECT_ICON`, 0x100001f1 `SUB_C_SBCNSELECT_DESCRIPTION_TEXT`
- Labels: 0x265 `SAVE_DELETE_FAILED`
- Calls: FUN_0007edf0, Menu_StartIris, Menu_UpdateMessageBox, __Menu_SendDelayedMessage, Process_Create, Menu_ChangePageCloseIris, Menu_UpdateCodenameWheel, Menu_PlayIris, Menu_PutCodenamesIntoControl, Menu_MapDefaultCodename, Menu_UpdateOptionBox, FUN_0008fd90, FUN_000dfec0, Menu_CreateOptionBox

### 63. P_MPJOIN_Handler - 0x00087880, 1304 bytes

- Difficulty: **hard** - unnamed FUN_000753f0, controllers, codenames, catch-all message group
- What it does: Multiplayer join page.
- Messages: 0x4c EnterPage, 0x50 Tick, 0x6a, 0x6b
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_MAIN, P_MPSCENARIO
- Controls: C_RBMPSTART, C_RBMPSETUP, C_RBMPCNAME
- Sub-controls: 0x10000121 `SUB_P_MPJOIN_MENU_GROUP`, 0x100001a2 `SUB_P_MPSETUP_ROW_PROMPT_TEXT`
- Labels: 0x1c1 `MP_PROMPT_START_GAME`
- Calls: __Menu_SendDelayedMessage, Process_Create, Menu_UpdateMessageBox, Manager_SendMessage, __Menu_SendEx, FUN_000753f0, Menu_PutCodenamesIntoControl, Menu_UpdateMPControllers

### 64. P_PAUSE_Handler - 0x00080030, 1372 bytes

- Difficulty: **hard** - pause menu, controller style list, many texts
- What it does: In-game pause page.
- Messages: 0x4c EnterPage, 0x50 Tick, 0x6b
- M_ITEM lists: -
- Controls: C_GCPAUSE
- Sub-controls: 0x1000006f `SUB_P_PAUSE_BUTTON_HINT_TEXT`, 0x100000c9 `SUB_C_RBCONTROL_INVERT_TEXT`, 0x100000d6 `SUB_P_PAUSE_TEXT`, 0x10000184 `SUB_P_PAUSE_BACKGROUND`
- Labels: 0x28 `CONTROLSCHEME_NIGHTFIRE`, 0x37 `CONTROLSCHEME_MOONRAKER`, 0x46 `CONTROLSCHEME_OCTOPUSSY`, 0x55 `CONTROLSCHEME_GOLDFINGER`, 0x64 `CONTROLSCHEME_DRNO`, 0x73 `CONTROLSCHEME_THUNDERBALL`, 0x206 `CONTROL_INVERTED`, 0x3bd `CONTROL_NORMAL`, 0x100016d `CONTROLSCHEME_GOLDENEYE`, 0x100017c `CONTROLSCHEME_CLASSICBOND`, 0x1000217 `BUTTONS_SELECT_SCROLL_NEXT_CONTINUE`
- Calls: __Menu_SendEx, MenuManager_GetStatus, Menu_UpdateOptionBox, __ftol2, Manager_SendMessage, sprintf

### 65. P_NFRESULTS_Handler - 0x00084600, 1451 bytes

- Difficulty: **hard** - mission scoring, Menu_SetLevelBonus (save data), medals
- What it does: Mission results page.
- Messages: 0x4b Select, 0x4c EnterPage, 0x50 Tick, 0x5d ButtonY?, 0x5e ButtonX?
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_NFMAP, P_DOSSIER, P_NFSTATS, P_NFBONUS, P_WINGAME
- Sub-controls: 0x1000002b `SUB_P_NFRESULTS_TEXT_02B`, 0x1000010e `SUB_P_NFMAP_BUTTON_HINT_TEXT`, 0x10000165 `SUB_P_NFRESULTS_ITEM`, 0x10000166 `SUB_P_NFRESULTS_ICON`, 0x10000167 `SUB_P_NFRESULTS_TEXT_167`, 0x10000168 `SUB_P_NFRESULTS_TEXT_168`, 0x100001d1 `SUB_P_NFRESULTS_SCORE_TEXT`
- Labels: 0x283 `BUTTONS_SELECT_DOSSIER_SCROLL_MAINMENU`, 0x2c7 `DEBRIEF_CONGRATULATION_PLATINUM`, 0x2c8 `DEBRIEF_CONGRATULATION_GOLD`, 0x2c9 `DEBRIEF_CONGRATULATION_SILVER`, 0x2ca `DEBRIEF_CONGRATULATION_BRONZE`, 0x2cc `DEBRIEF_TARGET_PLATINUM`, 0x2cd `DEBRIEF_TARGET_GOLD`, 0x2ce `DEBRIEF_TARGET_SILVER`, 0x2cf `DEBRIEF_TARGET_BRONZE`, 0x2fd `MISSION_FAILED`, 0x2fe `MISSION_FAIL_KIA`, 0x10001fd `DEBRIEF_NICE_WORK_FMT`, 0x10001fe `DEBRIEF_AGENT`
- Icons: 0x300013d `ICON_MEDAL_BRONZE`, 0x300013e `ICON_MEDAL_GOLD`, 0x300013f `ICON_MEDAL_PLATINUM`, 0x3000140 `ICON_MEDAL_SILVER`, 0x300019a `ICON_MEDAL_NONE`
- Calls: Menu_RestartFrontEndLoop, Manager_SendMessage, PlrStat_GetScore, sprintf, Mission_Status, SeparateNumber, Menu_SetLevelBonus, PlrStats_DoneBetter, Menu_UpdateDefaultCodename, Menu_UpdateOptionBox, Menu_UpdateMessageBox

### 66. C_NIS_Handler - 0x00082190, 1482 bytes

- Difficulty: **hard** - script loading (Script_Load/Play, Loadable), camera
- What it does: In-game scripted scene control.
- Messages: 0x4b Select, 0x51 Init, 0x5d ButtonY?
- M_ITEM lists: -
- Sub-controls: -
- Labels: -
- Calls: Script_IsPlaying, isLoadable, SFXPauseAllStreams, LoadableLoad, SFXUnPauseAllStreams, hashtable_getitem, Script_Load, Player_Disable, Script_Play, Camera_PushStates, Camera_Enable

### 67. P_TWEAKS2_Handler - 0x000833c0, 2381 bytes

- Difficulty: **hard** - 2381 bytes, ~30 debug sub-controls
- What it does: Debug tweaks page 2.
- Messages: 0x4b Select, 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Sub-controls: 0x100001df `SUB_P_TWEAKS2_SLIDER_1DF`, 0x100001e0 `SUB_P_TWEAKS2_SLIDER_1E0`, 0x100001e1 `SUB_P_TWEAKS2_SLIDER_1E1`, 0x100001e2 `SUB_P_TWEAKS2_SLIDER_1E2`, 0x100001e3 `SUB_P_TWEAKS2_SLIDER_1E3`, 0x100001e4 `SUB_P_TWEAKS2_TEXT_1E4`, 0x100001e6 `SUB_P_TWEAKS2_TEXT_1E6`, 0x100001e7 `SUB_P_TWEAKS2_TEXT_1E7`, 0x100001e8 `SUB_P_TWEAKS2_TEXT_1E8`, 0x100001e9 `SUB_P_TWEAKS2_TEXT_1E9`, 0x100001ea `SUB_P_TWEAKS2_TEXT_1EA`, 0x100001fe `SUB_P_TWEAKS2_SLIDER_1FE`, 0x100001ff `SUB_P_TWEAKS2_TEXT_1FF`, 0x10000200 `SUB_P_TWEAKS2_SLIDER_200`, 0x10000201 `SUB_P_TWEAKS2_SLIDER_201`, 0x10000202 `SUB_P_TWEAKS2_TEXT_202`, 0x10000203 `SUB_P_TWEAKS2_TEXT_203`, 0x10000205 `SUB_P_TWEAKS2_SLIDER_205`, 0x10000206 `SUB_P_TWEAKS2_SLIDER_206`, 0x10000207 `SUB_P_TWEAKS2_SLIDER_207`, 0x10000208 `SUB_P_TWEAKS2_SLIDER_208`, 0x10000209 `SUB_P_TWEAKS2_TEXT_209`, 0x1000020a `SUB_P_TWEAKS2_TEXT_20A`, 0x1000020b `SUB_P_TWEAKS2_TEXT_20B`, 0x1000020c `SUB_P_TWEAKS2_TEXT_20C`, 0x1000020d `SUB_P_TWEAKS2_PANEL`, 0x1000020e `SUB_P_TWEAKS2_TEXT_20E`, 0x1000020f `SUB_P_TWEAKS2_SLIDER_20F`, 0x10000210 `SUB_P_TWEAKS2_SLIDER_210`, 0x10000211 `SUB_P_TWEAKS2_TEXT_211`, 0x10000212 `SUB_P_TWEAKS2_TEXT_212`
- Labels: -
- Calls: sprintf, __ftol2, Manager_SendMessage

### 68. P_MPBOTSETUP_Handler - 0x0008ade0, 2721 bytes

- Difficulty: **hard** - 2721 bytes, bot statistics scrollers
- What it does: Bot setup page: playing, accuracy, aggression, health, speed, personality, two health-reaction fields; fixed bots show 0x010002d1.
- Messages: 0x4b Select, 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Sub-controls: 0x10000116 `SUB_P_MPBOTSETUP_PLAYING_SCROLL`, 0x10000185 `SUB_P_MPBOTSETUP_AGGRESSION_SCROLL`, 0x10000186 `SUB_P_MPBOTSETUP_ACCURACY_SCROLL`, 0x10000187 `SUB_P_MPBOTSETUP_HEALTH_SCROLL`, 0x10000188 `SUB_P_MPBOTSETUP_REACTION2_SCROLL`, 0x1000018a `SUB_P_MPBOTSETUP_REACTION1_SCROLL`, 0x1000018c `SUB_P_MPBOTSETUP_SPEED_SCROLL`, 0x10000195 `SUB_P_MPBOTSETUP_PERSONALITY_SCROLL`, 0x10000225 `SUB_P_MPBOTSETUP_INFO_TEXT`
- Labels: 0x181 `TXT_YES`, 0x182 `TXT_NO`, 0x233 `BOT_PERSONALITY_COLLECTOR`, 0x234 `BOT_PERSONALITY_GUARDIAN`, 0x235 `BOT_PERSONALITY_TEAMPLAYER`, 0x237 `BOT_PERSONALITY_BERSERKER`, 0x238 `BOT_PERSONALITY_GREEDY`, 0x239 `BOT_PERSONALITY_VENGEFUL`, 0x23c `BOT_SPEED_SLOW`, 0x23d `BOT_SPEED_NORMAL`, 0x23e `BOT_SPEED_FAST`, 0x241 `BOT_ACCURACY_POOR`, 0x242 `BOT_ACCURACY_AVERAGE`, 0x243 `BOT_ACCURACY_GOOD`, 0x244 `BOT_ACCURACY_VERYGOOD`, 0x24a `BOT_PERSONALITY_JUDGE`, 0x24b `BOT_PERSONALITY_ASSASSIN`, 0x31b `BOT_AGGRESSION_HIGH`, 0x31c `BOT_AGGRESSION_VERYHIGH`, 0x3bd `CONTROL_NORMAL`, 0x10002d0 `BOT_PERSONALITY_NONE`, 0x10002d1 `BOT_STATS_FIXED`
- Calls: __Menu_SendEx, Menu_IsBotGood, Manager_SendMessage

### 69. P_TWEAKS_Handler - 0x00082760, 3126 bytes

- Difficulty: **hard** - 3126 bytes, ~40 debug sub-controls, sprintf of floats
- What it does: Debug tweaks page 1.
- Messages: 0x4b Select, 0x4c EnterPage, 0x50 Tick, 0x5d ButtonY?
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_TWEAKS2
- Sub-controls: 0x100001a6 `SUB_P_TWEAKS_SLIDER_1A6`, 0x100001a7 `SUB_P_TWEAKS_SLIDER_1A7`, 0x100001a8 `SUB_P_TWEAKS_SLIDER_1A8`, 0x100001a9 `SUB_P_TWEAKS_SLIDER_1A9`, 0x100001aa `SUB_P_TWEAKS_SLIDER_1AA`, 0x100001ab `SUB_P_TWEAKS_SLIDER_1AB`, 0x100001ac `SUB_P_TWEAKS_SLIDER_1AC`, 0x100001ad `SUB_P_TWEAKS_SLIDER_1AD`, 0x100001ae `SUB_P_TWEAKS_SLIDER_1AE`, 0x100001af `SUB_P_TWEAKS_SLIDER_1AF`, 0x100001b0 `SUB_P_TWEAKS_SLIDER_1B0`, 0x100001b2 `SUB_P_TWEAKS_SLIDER_1B2`, 0x100001b3 `SUB_P_TWEAKS_SLIDER_1B3`, 0x100001b4 `SUB_P_TWEAKS_SLIDER_1B4`, 0x100001b5 `SUB_P_TWEAKS_SLIDER_1B5`, 0x100001b6 `SUB_P_TWEAKS_SLIDER_1B6`, 0x100001b7 `SUB_P_TWEAKS_SLIDER_1B7`, 0x100001b8 `SUB_P_TWEAKS_SLIDER_1B8`, 0x100001b9 `SUB_P_TWEAKS_TEXT_1B9`, 0x100001ba `SUB_P_TWEAKS_TEXT_1BA`, 0x100001bb `SUB_P_TWEAKS_TEXT_1BB`, 0x100001bc `SUB_P_TWEAKS_TEXT_1BC`, 0x100001bd `SUB_P_TWEAKS_TEXT_1BD`, 0x100001be `SUB_P_TWEAKS_TEXT_1BE`, 0x100001bf `SUB_P_TWEAKS_TEXT_1BF`, 0x100001c0 `SUB_P_TWEAKS_TEXT_1C0`, 0x100001c1 `SUB_P_TWEAKS_TEXT_1C1`, 0x100001c2 `SUB_P_TWEAKS_TEXT_1C2`, 0x100001c3 `SUB_P_TWEAKS_TEXT_1C3`, 0x100001c4 `SUB_P_TWEAKS_TEXT_1C4`, 0x100001c5 `SUB_P_TWEAKS_TEXT_1C5`, 0x100001c6 `SUB_P_TWEAKS_TEXT_1C6`, 0x100001c7 `SUB_P_TWEAKS_TEXT_1C7`, 0x100001c8 `SUB_P_TWEAKS_TEXT_1C8`, 0x100001c9 `SUB_P_TWEAKS_TEXT_1C9`, 0x100001ca `SUB_P_TWEAKS_TEXT_1CA`, 0x100001de `SUB_P_TWEAKS_TEXT_1DE`
- Labels: -
- Calls: __ftol2, sprintf, Manager_SendMessage

### 70. C_GCPAUSE_Handler - 0x00080590, 4954 bytes

- Difficulty: **hard** - 4954 bytes: objectives, scores, restart/quit, controller styles
- What it does: In-game pause control.
- Messages: 0x49 Scroll, 0x4b Select, 0x4e GainFocus, 0x5e ButtonX?
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_NFMAP
- Controls: C_GCPAUSE, C_LBMSGOPTIONS
- Sub-controls: 0x1000006f `SUB_P_PAUSE_BUTTON_HINT_TEXT`
- Labels: 0x15a `STATS_OPPONENTS`, 0x15b `STATS_DISPATCHED`, 0x15c `STATS_SUBDUED`, 0x15e `STATS_ACCURACY_RATING`, 0x15f `STATS_HEALTH_REMAINING`, 0x160 `STATS_TIME`, 0x161 `STATS_007_BONUS`, 0x163 `STATS_TOTAL`, 0x165 `STATS_BOND_MOVES`, 0x16e `STATS_SURRENDERED`, 0x1c7 `MP_TEAM_PHOENIX`, 0x1c8 `MP_TEAM_MI6`, 0x1ff `RESTART_CONFIRMATION`, 0x200 `QUIT_CONFIRMATION`, 0x206 `CONTROL_INVERTED`, 0x3bd `CONTROL_NORMAL`, 0x10001e1 `STATS_CURRENT`, 0x10001e2 `STATS_TARGET`, 0x1000204 `STATS_CATEGORY`, 0x1000217 `BUTTONS_SELECT_SCROLL_NEXT_CONTINUE`, 0x1000218 `BUTTONS_HINT_SCROLL_NEXT_CONTINUE`, 0x100021a `BUTTONS_INVERT_SCROLL_NEXT_CONTINUE`, 0x100021b `BUTTONS_NEXT_CONTINUE`, 0x10002d6 `BUTTONS_SELECT_BACK_SCROLL_CONTINUE`
- Calls: __Menu_SendEx, Mission_NumVisObjectives, Mission_ObjectiveState, Menu_DisplayControllerStyleList, Menu_GetMPScore, sprintf, PlrStat_GetScore, Timer_Seconds2String, Mission_BaseMapHCode, ResetMap_LevelToLoad, SFXFadeDown, GameFlow_PushState, MenuManager_Delete, Menu_CreateOptionBox, Menu_ChangeControllerStyle

## Already reimplemented

### P_DOSSIER_Handler - 0x00086810, 127 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Iris page for the dossier wheel.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: -
- Pages referenced (targets or previous-page checks): P_NFMAP, P_NFRESULTS, P_NFBONUS
- Controls: C_SBDOSSIER
- Sub-controls: 0x1000010b `SUB_C_SBDOSSIER_IRIS`
- Labels: -
- Calls: Menu_PlayIris, Menu_StartIris

### C_SBDOSSIER_Handler - 0x00086890, 267 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Wheel over ds_options.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter
- M_ITEM lists: `ds_options` (list/icon)
- Pages referenced (targets or previous-page checks): P_DSRECORDS, P_DSREWARDS, P_DSGADGETS, P_DSWEAPONS
- Sub-controls: 0x1000010a `SUB_C_SBDOSSIER_ICON`, 0x1000010b `SUB_C_SBDOSSIER_IRIS`, 0x1000010d `SUB_C_SBDOSSIER_WHEEL_TEXT`, 0x100001ed `SUB_C_SBDOSSIER_DESCRIPTION_TEXT`
- Labels: -
- Calls: Menu_UpdateWheel, Manager_SendMessage, Menu_ChangePageCloseIris

### P_DSWEAPONS_Handler - 0x000869d0, 237 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Rewrites ds_weapons[0] (PP7/P2K/gold variants) by upgrade level.
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: `ds_weapons` (description, list/icon, title)
- Controls: C_SBDSWPSCROLL
- Sub-controls: 0x1000016e `SUB_C_SBDSWPSCROLL_IRIS`
- Labels: 0x33d `WEAPON_PP7_NAME`, 0x33e `WEAPON_P2K_NAME`, 0x359 `WEAPON_PP7_DESC`, 0x35a `WEAPON_P2K_DESC`, 0x3c5 `WEAPON_PP7_GOLD_NAME`, 0x3c6 `WEAPON_PP7_GOLD_DESC`, 0x3c7 `WEAPON_P2K_GOLD_NAME`, 0x3c8 `WEAPON_P2K_GOLD_DESC`
- Icons: 0x30000f2 `ICON_DS_WEAPON_P2K`, 0x30000f3 `ICON_DS_WEAPON_PP7`, 0x300014f `ICON_DS_WEAPON_PP7_GOLD`, 0x3000150 `ICON_DS_WEAPON_P2K_GOLD`
- Calls: Menu_PlayIris, Menu_GetObjectUpgradeLevel, Menu_StartIris

### C_SBDSWPSCROLL_Handler - 0x00086ad0, 338 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Wheel over ds_weapons, upgrade text.
- Messages: 0x49 Scroll, 0x51 Init, 0x54 Enter
- M_ITEM lists: `ds_weapons` (description, identifier, list/icon)
- Sub-controls: 0x1000016e `SUB_C_SBDSWPSCROLL_IRIS`, 0x10000170 `SUB_C_SBDSWPSCROLL_ICON`, 0x10000171 `SUB_C_SBDSWPSCROLL_WHEEL_TEXT`, 0x10000172 `SUB_C_SBDSWPSCROLL_DESCRIPTION_TEXT`
- Labels: 0x3b1 `REWARD_UPGRADE_MAGNIFICATION`, 0x3b2 `REWARD_UPGRADE_MAGNIFICATION_AND_CLIP`
- Calls: Menu_UpdateWheel, Menu_GetObjectUpgradeLevel, sprintf

### P_DSGADGETS_Handler - 0x00086c30, 111 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Rewrites ds_gadgets[6].icon (branded or generic shaver).
- Messages: 0x4c EnterPage, 0x50 Tick
- M_ITEM lists: `ds_gadgets` (list/icon)
- Controls: C_SBDSGTSCROLL
- Sub-controls: 0x1000016d `SUB_C_DSGADGETS_IRIS`
- Labels: -
- Icons: 0x300018f `ICON_DS_GADGET_SHAVER_GENERIC`
- Calls: Menu_PlayIris, Menu_StartIris

### C_SBDSGTSCROLL_Handler - 0x00086ca0, 422 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Wheel over ds_gadgets, upgrade text.
- Messages: 0x49 Scroll, 0x51 Init, 0x54 Enter
- M_ITEM lists: `ds_gadgets` (description, identifier, list/icon)
- Sub-controls: 0x1000016a `SUB_C_SBDSGTSCROLL_WHEEL_TEXT`, 0x1000016b `SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT`, 0x1000016c `SUB_C_SBDSGTSCROLL_ICON`, 0x1000016d `SUB_C_DSGADGETS_IRIS`
- Labels: 0x3b3 `REWARD_UPGRADE_RANGE`, 0x3b4 `REWARD_UPGRADE_MAGNIFICATION_AND_BIOTARGET`, 0x3b5 `REWARD_UPGRADE_DARTGUN_STRONGER_SEDATIVE`, 0x3b6 `REWARD_UPGRADE_PDA_SPEED`, 0x3b7 `REWARD_UPGRADE_RANGE_AND_CHARGE`, 0x3b8 `REWARD_UPGRADE_LASER_SPEED`
- Calls: Menu_UpdateWheel, Menu_GetObjectUpgradeLevel, sprintf

### P_DSRECORDS_Handler - 0x00086e80, 72 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Menu_AddItemsToControl(sp_level) into C_RBDSRECORDS.
- Messages: 0x4c EnterPage
- M_ITEM lists: `sp_level` (list/icon)
- Controls: C_RBDSRECORDS
- Sub-controls: -
- Labels: -
- Calls: Menu_AddItemsToControl

### P_DSREWARDS_Handler - 0x000870f0, 72 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Menu_AddItemsToControl(sp_level) into C_RBDSREWARDS.
- Messages: 0x4c EnterPage
- M_ITEM lists: `sp_level` (list/icon)
- Controls: C_RBDSREWARDS
- Sub-controls: -
- Labels: -
- Calls: Menu_AddItemsToControl

### P_MPSCENARIO_Handler - 0x000873b0, 167 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Menu_UnlockMPSettings, select current mode in mp_scenario.
- Messages: 0x4c EnterPage, 0x50 Tick, 0x63 Back?
- M_ITEM lists: `mp_scenario` (list/icon)
- Pages referenced (targets or previous-page checks): P_MAIN
- Controls: C_SBMPSCEN
- Sub-controls: 0x10000106 `SUB_C_MPSCENARIO_IRIS`
- Labels: -
- Calls: Manager_SendMessage, Menu_PlayIris, Menu_StartIris, Menu_UnlockMPSettings, Menu_SelectItemInControl

### P_MPMAP_Handler - 0x00087710, 116 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Select current map in mp_level.
- Messages: 0x4c EnterPage
- M_ITEM lists: `mp_level` (list/icon)
- Pages referenced (targets or previous-page checks): P_MPSCENARIO
- Controls: C_SBMPMAP
- Sub-controls: 0x10000105 `SUB_C_MP_IRIS`
- Labels: -
- Calls: Menu_PlayIris, Menu_StartIris, Menu_SelectItemInControl

### C_SBMPMAP_Handler - 0x00087790, 200 bytes (reimplemented)

- Difficulty: **trivial** - reimplemented
- What it does: Wheel over mp_level.
- Messages: 0x49 Scroll, 0x4b Select, 0x51 Init, 0x54 Enter
- M_ITEM lists: `mp_level` (enabled, identifier, list/icon)
- Pages referenced (targets or previous-page checks): P_MPSETUP
- Sub-controls: 0x1000000a `SUB_C_SBMPMAP_ICON`, 0x1000000b `SUB_C_SBMPMAP_DESCRIPTION_TEXT`, 0x100000e9 `SUB_C_SBMPMAP_WHEEL_TEXT`, 0x10000105 `SUB_C_MP_IRIS`
- Labels: -
- Calls: Menu_UpdateWheel, Menu_ChangePageCloseIris

### P_MPENVIROMODS_Handler - 0x0008a0a0, 842 bytes (reimplemented)

- Difficulty: **moderate** - reimplemented
- What it does: Option scrollers for environment mods.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Sub-controls: 0x1000009f `SUB_C_MPENVIROMODS_GUNEMPLACEMENTS`, 0x100000a2 `SUB_C_MPENVIROMODS_RESPAWNMODE`, 0x1000019b `SUB_C_MPENVIROMODS_EXPLOSIVESCENERY`, 0x10000227 `SUB_C_MPENVIROMODS_GRAPPLE`, 0x10000228 `SUB_C_MPENVIROMODS_MINIVEHICLES`
- Labels: 0x1b0 `MP_RESPAWN_NEAR`, 0x1b1 `MP_RESPAWN_FAR`, 0x1b2 `MP_RANDOM`, 0x1b6 `MP_OFF`, 0x1b7 `MP_ON`, 0x1000076 `LOCKED`, 0x10002d8 `MP_RC_TANK`, 0x10002d9 `MP_RC_HELI`
- Calls: Manager_SendMessage

### P_MPPLAYERMODS_Handler - 0x0008a3f0, 1023 bytes (reimplemented)

- Difficulty: **moderate** - reimplemented
- What it does: Option scrollers for player mods.
- Messages: 0x4b Select, 0x4c EnterPage
- M_ITEM lists: -
- Sub-controls: 0x10000080 `SUB_C_MPPLAYERMODS_PROFESSIONALMODE`, 0x10000081 `SUB_C_MPPLAYERMODS_FRIENDLYFIRE`, 0x10000084 `SUB_C_MPPLAYERMODS_WEAPONSET`, 0x1000008c `SUB_C_MPPLAYERMODS_LOCATIONDAMAGE`, 0x1000008d `SUB_C_MPPLAYERMODS_TEAMID`
- Labels: 0x1a0 `MP_WEAPSET_NORMAL`, 0x1a2 `MP_WEAPSET_PISTOLS`, 0x1a4 `MP_WEAPSET_AUTOMATIC`, 0x1a6 `MP_WEAPSET_SNIPERS`, 0x1a8 `MP_WEAPSET_EXPLOSIVES`, 0x1b2 `MP_RANDOM`, 0x1b6 `MP_OFF`, 0x1b7 `MP_ON`, 0x24f `MP_WEAPSET_EXPLOSIVES2`, 0x3db `MP_WEAPSET_MI6`, 0x3dc `MP_WEAPSET_PHOENIX`, 0x3dd `MP_WEAPSET_MODERN`, 0x3de `MP_WEAPSET_STEALTHY`
- Calls: Manager_SendMessage
