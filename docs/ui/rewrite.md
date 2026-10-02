# Rewriting the remaining menu handlers - plan

A plan for reimplementing every page (`P_*`) and control (`C_*`) handler still running as original code, written
2 Oct 2026 from the state of `dev` on that day. The framework the handlers stand on is described in
[framework.md](framework.md); the per-handler inventory is [handlers.md](handlers.md) (its step 2); messages are in
[messages.md](messages.md). This document classifies what is left, says what to build before starting, in what order
to go, how to check each step, and what it will cost.

## 1. In numbers

- 99 handlers are dispatched by the original `Handler_HandleMessage` (0x8e320). 35 are ours; **64 are left**: the
  63 that `python tools/function_coverage.py ui.frontend` lists as live (40.3 KB of original code), plus
  C_CHCHWEAP (0.2 KB), which reports as dead only because our generated dispatcher forgets to call it
  ([framework.md §14](framework.md#14-findings-that-correct-or-extend-the-other-documents)).
- By kind (§3): 16 checkbox toggles, 8 debug/test pages, 4 movie pages, 8 boot and main-menu pages, 7 option pages,
  6 codename/save pages, 6 multiplayer join/setup handlers, 5 results/records handlers, 4 in-game handlers.
- Reachable on a retail disc: 40 of the 64. The other 24 - the 16 toggles, C_LBPMMAP, P_TWEAKS, P_TWEAKS2,
  P_CHEATMEDAL, P_NIS, C_NIS, P_FMVTEST and P_FMVPLAYER - live behind the debug page 0x40000007 or the movie
  selector, which nothing opens (§3.1, §3.2).
- Covered by an existing replay (any message other than creation-time 0x51/0x54 in one of the 156 logs under
  `build/menurun/`): 23 of the 64. **41 are reached by no replay** (§6.3).
- Estimated size: 3500-4500 lines for the handlers (about 2000 of them table-driven or near it), plus 1000-1500 for
  the helper functions they share that are worth owning at the same time (§5). The framework itself (dispatch,
  control types, menu scripts: about 52 KB of original code) is a separate, later job of roughly 5000-6000 lines and
  is not needed to finish the handlers.

## 2. What the handlers can rely on today

Every handler written so far calls the framework through its original addresses (`__Menu_Send`, `__Menu_SendEx`,
`__Menu_SendMessage`, `Manager_SendMessage`, the iris and option-box helpers are all `// AUTOGEN` declarations), and
the dispatcher that calls the handlers is ours. That arrangement works and needs no change to finish the handlers:
the 35 done ones were verified identical to the original on top of it. The handler rewrite and a framework rewrite
can therefore be separated, handlers first.

Facts from [framework.md](framework.md) that shape handler code:

- All pages of a menu set are built when the manager is created, so each control's `ControlCreated` (0x51) runs
  then, not on page entry (§3 there); a 0x51 handler that reads a global reads it at menu creation.
- The page's P_ handler receives Select (and AltSelect1/2, BackOnControl) for *every* control on its page, with
  a1 = page and a2 = control, before the control's own C_ handler gets it (§7 there).
- A handler's return value matters only for messages outside the seven "default script" ids; returning false then
  re-sends the message to the page, which replays the control's creation script for 0x51 and does nothing else
  visible. Return true, as the originals do, unless the original returns false.
- `__Menu_Send` reaches every page; `__Menu_SendEx` only the current page and the one under an overlay, by id.
- Text given by pointer must outlive the label: the originals sprintf into static buffers; keep those static (and
  owned per the globals rule: address macro until every user is ours, then a definition).
- Option and message boxes: `Menu_CreateOptionBox[Label]` opens P_MESSAGEBOX as an overlay and the page polls
  `Menu_UpdateOptionBox(&type)` in its PageUpdate (it keeps getting 0x50 under the overlay).
- The first PageEnter of a manager has the last page in the menu file as its previous page, and that page gets a
  PageLeave at start-up (P_WINGAME in the front end).
- Ghidra's rendering of text-label constants in these handlers is wrong (ORs of unrelated names); take every label
  and colour from the assembly.

## 3. The 64 handlers by kind

Sizes are bytes to the next function, as `function_coverage` counts them. "Replay" names the run (or script) whose
log shows the handler doing something; "-" means none.

### 3.1 Checkbox toggles - 16 handlers, 1.7 KB, debug page only

C_CHCHFLY 0x81a50 (96), C_CHCHDRONES 0x81ab0 (80), C_CHCHBLIND 0x81b00 (80), C_CHCHWEAP 0x81b50 (192), C_CHCHHEALTH
0x81c10 (80), C_CHCHHUD 0x81c60 (208), C_CHCHDEBUG 0x81d30 (80), C_CHCHCOORDS 0x81d80 (80), C_CHCHWS 0x81dd0 (192),
C_CHCHALLOWFREEZE 0x81e90 (80), C_CHCHDUMMY 0x81ee0 (80), C_CHCHZEROG 0x81f80 (96), C_CHCHLOCKUP 0x81fe0 (80),
C_CHCHBRIGHT 0x82030 (80), C_CHCHUNLOCK 0x82080 (80), C_CHCHCONTROLS 0x820d0 (80).

All the same shape as the two already done in `ui_cheats.cpp`: Select stores the checkbox's value (`GetValue`) in a
global (`switch_*`, `menu_unlock_everything` 0x25d79e...), ControlCreated sets the checkbox from it; five add one call
(FLY/ZEROG `Player_ChangeSubState`, WS `Camera_CalcViewAngles`, HUD `HUD_Enable`, WEAP `Player_EquipWeapon` +
`Player_CheckWeaponsLoaded`). **Table-driven**: one row per hashcode {global, width, side effect}, one shared function
- but each needs its own AUTOINJECT entry point, so the table drives sixteen two-line wrappers (or the dispatcher
calls the shared function directly for these hashcodes; see §4.1). They live on page 0x40000007, which nothing opens,
but their 0x51 runs every time the game is paused (the pause manager builds that page too). Replay: -.

### 3.2 Debug and test pages - 8 handlers, 8.4 KB, unreachable on retail

| Handler | Address | Bytes | What | State |
|---|---|---|---|---|
| C_LBPMMAP | 0x81930 | 192 | level list on 0x40000007; Select loads the level | `ResetMap_LevelToLoad`, `GameFlow_PushState`, `MenuManager_Delete` |
| P_TWEAKS | 0x82760 | 3168 | 18 scrolls bound to drone/player damage floats: Enter sets names and values, PageUpdate prints each value with `%f` into a static buffer, Select stores all and goes back, Y goes to P_TWEAKS2 | `DroneDamage_*`, `DroneArmour_*`, `Plr_DMod_*` |
| P_TWEAKS2 | 0x833c0 | 2384 | the same for ~15 more tunables | tunables |
| P_CHEATMEDAL | 0x83d10 | 320 | list 0x1000021c sets `switch_channels[99/100]` to a medal | |
| P_NIS | 0x82120 | 112 | ticks the scripted scene C_NIS started; restores player and camera at its end | `Script_Update`, `Player_Enable`, `Camera_*` |
| C_NIS | 0x82190 | 1488 | list of level scripts; loads and plays one | `Script_Load`/`Play`, loadables, camera; the GameCube build has a check here ("Script 0x%x:%d loading...", [gamecube-checks.md](../gamecube-checks.md)) |
| P_FMVTEST | 0x85a50 | 640 | movie list ([movie-selector.md](movie-selector.md)) | `movie_hashcode` |
| P_FMVPLAYER | 0x85cd0 | 128 | plays it | `Menu_PlayMovie`, `psiMovieLoop` |

P_TWEAKS and P_TWEAKS2 are **table-driven**: {scroll hashcode, value-label hashcode, name string, float*, scale}; the
originals' read and write scales agree (value = slider x scale), and Enter happens to visit two rows in a different
order (harmless). Replay: P_FMVTEST and P_FMVPLAYER (`movie_selector.txt`); the rest -.

### 3.3 Movie pages - 4 handlers, 0.9 KB

P_INTRO 0x85240 (176: EA and MGM idents, then P_START), P_ESTHERO 0x852f0 (128: FMV_TITLES, then P_MAIN), P_TRAILER
0x8dca0 (304: the Die Another Day trailer, reached from a button on P_CNAVOPTIONS), P_WINGAME 0x8ddd0 (256: the
end-of-game movie, then P_CREDITS). P_TRAILER and P_WINGAME share P_ATTRACT's structure (already ours in
`ui_credits.cpp`): start with `Menu_PlayMovie`, poll `psiMovieFinished` in PageUpdate, on the end unpause audio,
`psiStopBackgroundMovie`, restart the front-end loop, unlock input, move on. One helper serves the three. P_WINGAME
also receives the start-up PageLeave of every front-end boot. Replay: P_INTRO, P_ESTHERO, P_WINGAME (leave only) in
every boot; P_TRAILER -.

### 3.4 Boot and main menu - 8 handlers, 2.7 KB

| Handler | Address | Bytes | What | Replay |
|---|---|---|---|---|
| P_LANGUAGE | 0x84130 | 688 | language page: idle timeout picks the default, pulsing; `Txt_SetLanguage`, FUN_00084080/FUN_000840d0/FUN_000dbdc0 | enter/leave only (every boot; the timeout fires) |
| C_LANGUAGE | 0x843e0 | 544 | a language button (id = language): sets text and sound language, GoPage P_INTRO | - |
| P_START | 0x854c0 | 368 | legal text 0x375, blinking prompt, attract after 0xa8c idle frames, input delay | every boot |
| P_PARISENUM | 0x85630 | 336 | the save check before the main menu; first-boot Paris mission (`Menu_RunMiniMission`), Xbox free-space box | every boot |
| P_MAIN | 0x85780 | 288 | fade-in Process, attract after idle | every boot |
| C_GONIGHTFIRE / C_GOMULTIPLAYER / C_GOCODENAMES | 0x858a0 / 0x85930 / 0x859c0 | 144 each | fade the main menu (Process on 0x100000ed), delayed GoPage | every branch |

The three C_GO* differ only in the target page: one function and a table.

### 3.5 Option pages - 7 handlers, 7.8 KB

| Handler | Address | Bytes | Controls | State |
|---|---|---|---|---|
| P_CNOPTIONS | 0x8d0f0 | 1136 | 8 radios | `PlayerInputs[0]`: vibration, auto-aim, crosshair, crouch, manual aim, weapon switch, flashing objects, HUD; `cn_modified_flag` |
| P_CNMPOPTIONS | 0x8d560 | 576 | 3 radios | `MPSettings.Players[0]`, `PlayerInputs[0].autoaimMp` |
| P_CNAVOPTIONS | 0x8d7a0 | 1280 | 2 scrolls, 4 radios, 2 buttons | SFX/music volume (`SFXSetVolume`, `SFXMusicSetVolume`), subtitles, split screen; buttons to P_CREDITS, P_TRAILER (menu-data scripts), P_SCREENADJUST |
| P_MPRULES | 0x89b20 | 1408 | 2 radios | duration and points/lives per game mode: tables of limits and labels by mode |
| P_MPBOTSETUP | 0x8ade0 | 2736 | 8 radios | a bot's playing flag, accuracy, aggression, health, speed, personality, two health reactions; fixed bots show 0x010002d1 |
| P_CNCONTROLS | 0x8ce70 | 400 | radio C_RBCONTROL | fills the 8 controller schemes |
| C_RBCONTROL | 0x8d000 | 240 | | scheme change → `Menu_DisplayControllerStyle`; Select applies (`Menu_ChangeControllerStyle`); X inverts |

The first five follow the pattern P_MPPLAYERMODS/P_MPENVIROMODS already use (`ui_mp.cpp`): PageEnter clears each radio,
adds (label, value) items and selects the current value; Select (any control on the page) reads every radio back,
stores, and sends Back. **Table-driven**: {radio hashcode, item list, field accessor}. Keep the originals' quirks:
P_CNOPTIONS' "flashing objects" uses value 2 for on, its "HUD" radio is inverted (On = 0); labels are bound at Enter
(not re-bound on a language change). P_MPRULES' tables differ by mode and P_MPBOTSETUP enables/disables rows, so they
need a little code around the table. Replay: P_MPBOTSETUP (`multiplayer_bots.txt`); the rest -.

### 3.6 Codenames and saves - 6 handlers, 4.0 KB

| Handler | Address | Bytes | What |
|---|---|---|---|
| P_NFSELECT | 0x85d50 | 1104 | campaign codename select: codename wheel, load flow, message boxes |
| C_SBNFCN | 0x861a0 | 512 | its wheel (`Menu_UpdateCodenameWheel`), new codename, free-space check |
| P_CNSELECT | 0x8be40 | 1312 | Codenames page: create / edit / delete |
| C_SBCNSELECT | 0x8c360 | 656 | its wheel |
| C_LBERROPTIONS | 0x8fbd0 | 240 | save-error choices on P_AFTERRESULTS: retry, continue without saving, dashboard |
| C_LBMSGOPTIONS | 0x761c0 | 128 | the option box's list (fully read: [framework.md §13](framework.md#13-the-helper-layer-the-handlers-call)) |

State: `ls` (`MENU_LS`), `codename_buf`, the save files, `cn_modified`. Helpers: `Menu_UpdateMessageBox`,
FUN_0007fef0 *(invented `Menu_StartLoadSave`)*, FUN_0007edf0 (PS2 `Menu_SelectCodenameInControl`), FUN_0008fcc0 /
FUN_0008fd90 (Xbox save space), `Menu_MapDefaultCodename`, `Menu_PutCodenamesIntoControl`, `XBox_DoSaveFlow`.
Replay: P_NFSELECT, C_SBNFCN, P_CNSELECT, C_SBCNSELECT (Select only), C_LBMSGOPTIONS; C_LBERROPTIONS -.

### 3.7 Multiplayer join and setup - 6 handlers, 3.9 KB

P_MPJOIN 0x87880 (1360), P_MPSETUP 0x882c0 (624), C_RBMPSTART 0x87dd0 (720: "press A to join" rows), C_RBMPCNAME
0x880a0 (544: codename per agent), C_RBMPFINISH 0x88530 (496: "ready" rows), C_MPDBG 0x8bdd0 (112: not a debug control - "DBG" is
"debriefing": the four player columns of P_MPDEBRIEFING, ids 0-3; A restores the match settings and goes to
P_MPSCENARIO, Y replays the map). Per-player focus (`SetPlayerFocus`, PlayerFocusGained/Lost), one radio instance per agent told apart
by id, `__Menu_SendEx`, `Menu_UpdateMPControllers`, `Menu_AllJoinedPlayersReady`, `Menu_MPFadeWhenReady`, codenames.
Their partner C_RBMPSETUP is already ours. Replay: all but C_MPDBG, with one pad only (§6.4).

### 3.8 Results and records - 5 handlers, 4.3 KB

P_NFRESULTS 0x84600 (1504: mission score, medal, `Menu_SetLevelBonus` (ours), `PlrStats_DoneBetter`, autosave box),
P_NFSTATS 0x84e10 (1072: statistics list via `Menu_AddRow`/`Menu_AddRowPercentage`), P_NFBONUS 0x84be0 (560: rewards
won, `Menu_GetLevelBonuses`), C_RBDSRECORDS 0x86ed0 (544: dossier records row, medal icons), C_RBDSREWARDS 0x87140
(624: dossier rewards row). State: `PlrStat_*` scores, the bonus words, save data. Replay: -.

### 3.9 In game - 4 handlers, 7.0 KB

| Handler | Address | Bytes | What |
|---|---|---|---|
| C_GCPAUSE | 0x80590 | 5024 | fourteen instances on P_PAUSE by id: GainFocus on a tab button (0 objectives text, 1 objectives list, 3 controls, 4 scores) shows that panel; id 1 Select opens an objective's description in a box; id 6 the resume/restart/quit list, ids 12/13 the confirmation; id 9 the controller-style radio (Scroll changes style, X inverts); restart/quit reload the level or the front end (`ResetMap_LevelToLoad`, `GameFlow_PushState(7, 80.0, 0xff)`, `GameState.ReloadMenupage` = P_NFMAP or P_MPDEBRIEFING); the scores panel fills a 5-column list from `PlrStat_GetScore` (single player) or `Menu_GetMPScore` by team |
| P_PAUSE | 0x80030 | 1376 | the pause page itself: texts, list set-up, the controller-style list |
| P_ENDMISSION | 0x83e50 | 336 | failed mission: restart from checkpoint (`Player_RamLoad`), restart, quit to P_NFMAP |
| C_KEYPAD | 0x83fa0 | 224 | the keypad lock's digits (`GlobalVars.DecoderDisplay`) |

C_GCPAUSE is one function in the original; split it by id into one function per panel/role behind a small switch -
the id is the natural seam. It writes two List fields by raw offset (+0x569 and +0x26c of the control) that `ui.h`
does not lay out yet. Statics 0x25d820 (restart vs quit) and 0x25d821 (confirmation open). Replay: -.

## 4. What to build first

### 4.1 Put the dispatcher right (small, do first)

In `tools/uihandler.py` (then regenerate `ui.cpp`):

1. Add `0x100000e7: "C_CHCHWEAP"`.
2. Return false for unknown hashcodes, and true for 0x100001eb (C_SBSCREEN), 0x40000047 (P_SCREENADJUST) and
   0x4000004d (P_FMV), as the original does. This restores the original's second play of the creation script for
   handler-less controls; check a front-end replay's screenshots and the creation burst of the log afterwards.
3. Drop or gate the "UNHANDLED MESSAGE HANDLER" printf: it fires for every message to an anonymous control
   (0x10000001 and the other handler-less sub-controls), which is normal, not an error.
4. Optionally let the table map several hashcodes to one shared function (the toggles, the three C_GO* buttons).
   The dispatcher is the only caller of every handler, so nothing needs a separate entry point per original address;
   only `function_coverage`'s bookkeeping wants each original address marked as replaced (an AUTOINJECT stub each,
   or a way to mark an address done without one).

### 4.2 Keep the probe working when the dispatch chain becomes ours

`MenuLog` and `MenuOriginal` work by redirecting 0x8e320 (`XbeOriginal_Redirect(kHandlerHandleMessage, ...)` in
`MenuProbe.cpp`): every message reaches the handlers through that address because the caller, `Manager_SendMessage`,
is original. The moment `Manager_SendMessage` is ours and calls `Handler_HandleMessage` by name, the redirect is
bypassed and both the log and A/B runs silently stop working. Before reimplementing `Manager_SendMessage`, give the
dispatch a hook - a function pointer the probe can set, or have our caller call through the address - and make
`run_menu.sh` fail loudly when a log comes out empty. (Not needed for the handlers alone.)

### 4.3 A typed layer over the messages

Handlers today use `__Menu_Send(m, hash, MessageType_X, (int)ptr, 0)` and the few macros in `ui.h` (`RADIO_*`,
`LABEL_*`, `CONTROL_GET`). For 64 more, a header of small inline functions *(invented `ui_api.h`)* pays for itself:
`Ui_SetText(m, hash, str)`, `Ui_SetTextLabel(m, hash, label)`, `Ui_SetState(m, hash, state)`,
`Ui_SetValue`/`Ui_GetValue`, `Ui_SetTextEx(m, hash, id, str)` for instances, `Ui_RadioFill(m, hash, items, n,
selected)`, `Ui_ListAddRow`... They must compile to exactly the original sends (same id, same argument order, same
search function - `__Menu_Send` vs `__Menu_SendEx` matters, §2). Also lay out the control types handlers poke by
offset (List +0x26c, +0x569; Label's colours) in `ui.h`.

### 4.4 Own the shared helpers alongside, not before

The handlers call about 40 original helpers (framework.md §13). They do not have to be ours first - the done handlers
call the originals - but owning a helper together with its users keeps globals clean (the iris state, the option-box
result, the `ls` record, the codename buffer) and is cheaper than coming back. Worth doing with the handler batch
that uses them most:

| Helper group | Size | With |
|---|---|---|
| `Menu_StartIris` (+ its five fragments), `Menu_PlayIris`, `Menu_ChangePageCloseIris` | 0.6 KB | first batch (used by ~25 handlers, 16 already ours) |
| `Menu_CreateOptionBox`, `Menu_CreateOptionBoxLabel`, `Menu_UpdateOptionBox`, C_LBMSGOPTIONS | 0.7 KB | first batch |
| `Menu_PlayMovie`, `Menu_StopMovie`, `psiMovieLoop`, `Menu_RestartFrontEndLoop`, `Menu_StopFrontEndMusic` | 0.3 KB | movie pages |
| `Menu_DisplayControllerStyle`, `...List`, `Menu_UpdateControllerLabelString` | 5.8 KB, mostly tables | P_CNCONTROLS / C_GCPAUSE |
| `Menu_AddRow`, `Menu_AddRowPercentage`, `Menu_GetLevelBonuses`, medal helpers | 1 KB | results/records |
| codename and save helpers, `Menu_UpdateMessageBox`, `XBox_DoSaveFlow` | 5.8 KB | codename/save batch |
| MP helpers (`Menu_UpdateMPControllers`, `Menu_GetBotShortName`, ...) | 1.5 KB | MP batch |

### 4.5 A menu-data tool

`tools/ui/menu_data.py` *(proposed)*: extract the menu file from a level bundle (the archive reader is `edl.py` on
the blender-exports branch) and list, per menu set and page, every control (hashcode, type, id, rectangle, flag, skin),
its setup messages and its script messages - the inventory framework.md §3 summarises. It answers "which instances of
C_GCPAUSE exist and what ids", "which sub-controls does this page own", "is this page reachable", and it can generate
the SUB_ hashcode names a handler needs. The scratch parser used for framework.md is about 60 lines.

### 4.6 Probe steps the uncovered pages need

- `openmenu <set> <page> <manager> <status>` *(proposed)*: call `MenuManager_Create` directly, to reach
  P_ENDMISSION (set 0x80000004) and the keypad (0x80000003), which `gopage` cannot (they are in other menu sets).
- A way to log creation-time messages for one run (`run_menu.sh` always skips 0x50/0x51; the toggles and every
  `SetRange` live in 0x51).
- A log comparison that ignores heap addresses if those differ between builds (`a=`/`b=` are control pointers).

## 5. Order

Each batch: decompile, read the assembly for constants, write, add the GameCube checks
([gamecube-checks.md](../gamecube-checks.md); only C_NIS has one), build, then the targeted test named for it (not
the whole replay suite). Harnesses and new probe steps go in `src/action/devtools/`, not in the handlers.

| # | Batch | Handlers | Risk | Test |
|---|---|---|---|---|
| 0 | Infrastructure | dispatcher fix (§4.1), `ui_api.h` (§4.3), menu-data tool (§4.5), probe steps (§4.6) | low; the dispatcher fix changes original-visible behaviour slightly | front-end replay before/after |
| 1 | Small and shared | C_GO* x3, C_LBMSGOPTIONS, C_MPDBG, P_INTRO, P_ESTHERO, P_TRAILER, P_WINGAME + iris and option-box helpers | low | boot replays (all reach these), `credits.txt`, `mp_confirm.txt` extended to press A on the debriefing (C_MPDBG), new AV-options script for P_TRAILER |
| 2 | Toggles and debug | 16 toggles, C_LBPMMAP, P_CHEATMEDAL, P_NIS, P_TWEAKS, P_TWEAKS2, P_FMVTEST, P_FMVPLAYER | low; unreachable on retail | new `debug_menu.txt`: pause in a level, `gopage 0x40000007`, toggle each, `movie_selector.txt` |
| 3 | Option pages | P_CNOPTIONS, P_CNMPOPTIONS, P_CNAVOPTIONS, P_MPRULES, P_MPBOTSETUP, P_CNCONTROLS, C_RBCONTROL (+ controller-style display) | medium: writes player settings saved with the codename | extended `codenames.txt` (each option page, change, back, the lose-changes box), `multiplayer.txt` to rules |
| 4 | Boot flow | P_LANGUAGE, C_LANGUAGE, P_START, P_PARISENUM, P_MAIN | medium: first-boot paths (no saves → Paris), idle timers, language change | boot replays with and without `Release/saves`, a script pressing A on the language page |
| 5 | Results and records | P_NFRESULTS, P_NFSTATS, P_NFBONUS, C_RBDSRECORDS, C_RBDSREWARDS | medium-high: save data, medals; hard to reach | dossier script (records/rewards), results via a played level then `poke` GameState.ReloadMenupage (0x1f659c) = P_NFRESULTS and a `level` to the front end - untried |
| 6 | Codenames and saves | P_NFSELECT, C_SBNFCN, P_CNSELECT, C_SBCNSELECT, C_LBERROPTIONS + save helpers | high: save files, Xbox storage paths, error flows | `codenames.txt`, `nightfire.txt` (load), new create/delete script on a copied saves folder |
| 7 | Multiplayer join | P_MPJOIN, P_MPSETUP, C_RBMPSTART, C_RBMPCNAME, C_RBMPFINISH | high: per-player focus, more than one pad | `mp_setup.txt`, `multiplayer.txt` (pad 0 only) |
| 8 | In game | P_PAUSE, C_GCPAUSE (split by id), P_ENDMISSION, C_KEYPAD, C_NIS | high: size, level reloads, MP scores | new `pause.txt` (each tab by `focus 0x10000028 <id>`, controller style, X, restart/quit confirm then cancel), `openmenu` for P_ENDMISSION and the keypad |

Then, if wanted, the framework (a separate project): `Page_SendMessage` and `Manager_SendMessage` (with the hook of
§4.2), `MenuManager_Update`/`Monitor`/`Create`/`Delete`, `Page_Update`, the Send family, the control types (Label
first: it draws almost everything), menu scripts and processes, components. Its risks are different: sprite
behaviour, struct layouts of eleven control types, and losing the A/B harness.

## 6. Verification

### 6.1 What exists

`ORIGINAL=<hash>:<addr> tools/ui/run_menu.sh <script>` runs chosen handlers as original code; two runs compared with
`tools/ui/compare_shots.py` (screenshots) and a diff of `menu.log` (named messages). That is the check for every
batch. Two limits from experience: an original handler that reads data we have moved into our source (the M_ITEM
lists) reads the stale game copy, so A/B means nothing for it - the same will apply to any global a batch turns into
a definition; and restoring an original *helper* with `MenuOriginal=<addr>` only affects original callers, since our
code calls our version by name.

### 6.2 Shadow tests

Where a function is pure enough, run it and the original on the same inputs at start (`MenuShadowTests`, as
`UnlocksShadow.cpp`): candidates are `Menu_GetLevelBonuses`, `Menu_GetBotShortName`, `Menu_GetMPScore`, the
language mappings FUN_00084080/FUN_000840d0, P_MPRULES' limit tables, `Menu_FindControl` on a synthetic page.

### 6.3 Replay coverage of the remaining handlers

From the 156 logs in `build/menurun/` (messages other than 0x51/0x54):

| Reached | Handlers |
|---|---|
| Yes (23) | C_GOCODENAMES, C_GOMULTIPLAYER, C_GONIGHTFIRE, C_LBMSGOPTIONS, C_RBMPCNAME, C_RBMPFINISH (0x5c only), C_RBMPSTART, C_SBCNSELECT (Select only), C_SBNFCN, P_CNSELECT, P_ESTHERO, P_FMVPLAYER, P_FMVTEST, P_INTRO, P_LANGUAGE (enter/leave), P_MAIN, P_MPBOTSETUP, P_MPJOIN, P_MPSETUP, P_NFSELECT, P_PARISENUM, P_START, P_WINGAME (start-up leave only) |
| No (41) | the 16 toggles, C_LBPMMAP, P_TWEAKS, P_TWEAKS2, P_CHEATMEDAL, P_NIS, C_NIS, P_TRAILER, C_LANGUAGE, P_CNOPTIONS, P_CNMPOPTIONS, P_CNAVOPTIONS, P_MPRULES, P_CNCONTROLS, C_RBCONTROL, C_LBERROPTIONS, C_MPDBG, P_NFRESULTS, P_NFSTATS, P_NFBONUS, C_RBDSRECORDS, C_RBDSREWARDS, P_PAUSE, C_GCPAUSE, P_ENDMISSION, C_KEYPAD |

Already-ours handlers with no replay either: C_SBDOSSIER, C_SBDSWPSCROLL, C_SBDSGTSCROLL, P_DSWEAPONS, P_DSGADGETS,
P_DSRECORDS, P_DSREWARDS (`nightfire.txt` enters P_DOSSIER and leaves), P_MPPLAYERMODS, P_MPENVIROMODS,
C_CHCHMUSIC, C_CHCHDRAWALL. New scripts for the batches above would cover these as well.

### 6.4 Limits of the replays

- The script drives pad 0 only (it is laid over port 0 after the poll), so the join and setup pages can be tested
  with one agent. Testing two or more needs the probe to drive other ports.
- Play is not deterministic (README warning); menus are. The pause menu is safe (the game is paused while it shows);
  the results pages depend on how the level went, so compare them A/B from the same saved state rather than across
  plays.
- Save-touching pages change `build/menurun/<name>/saves`, a fresh copy per run - fine, but compare runs started
  from the same copy.

## 7. Risks

1. **Coverage.** 41 of the 64 are reached by no replay, and some (results, end of mission, keypad, save errors) need
   new probe steps or a played level. Without them a batch cannot be shown identical; budget script work per batch.
2. **The probe hook** (§4.2) - only when the framework is touched, but it fails silently.
3. **Fidelity in small things**: the order of sends (the screen is the same but the log differs), which search
   function a send uses, the quirks (inverted radios, value 2 for on, P_CNOPTIONS' bound-once labels), pointer text
   in static buffers, creation-time 0x51 behaviour, and the dispatcher default (§4.1).
4. **Shared globals**: handler statics, sprintf buffers, the iris state, the option-box result, `ls` and
   `codename_buf` are used by original helpers as well; moving them into our source before all users are ours breaks
   the originals (and A/B).
5. **Struct layouts**: C_GCPAUSE and the option box write List/Memo fields by raw offset; the control types beyond
   M_CONTROL/M_PAGE/M_MANAGER are not laid out.
6. **Size and branching of the big three**: C_GCPAUSE (5 KB, fourteen roles), P_TWEAKS (3 KB, mechanical) and
   P_MPBOTSETUP (2.7 KB). Split by role and keep each piece testable.
7. **Save data and storage**: codename create/delete, free-space and too-many-codenames paths on the Xbox storage
   layer are hard to trigger and easy to get subtly wrong.
8. **Unreachable code**: 24 handlers are debug-only. Doing them keeps the dispatcher whole and costs little, but
   they cannot be checked by playing - only through `gopage` on a paused game.
