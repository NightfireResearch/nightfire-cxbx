# The action engine's menus

How the front end and pause menus of `default.xbe` work, what is reimplemented, and the tools for checking a
reimplementation against the original. The plan and its state: [PLAN.md](PLAN.md).

| Document | Covers |
|---|---|
| [messages.md](messages.md) (+ `messages.json`) | every menu message (97), who sends it, which controls handle it, its arguments; control types; the control, page and manager layouts |
| [handlers.md](handlers.md) | every page/control handler: size, messages, lists, sub-controls, difficulty, a proposed order |
| [items.md](items.md) (+ `items.json`) | the 12 M_ITEM lists entry by entry, with the text of every label |
| [secrets.md](secrets.md) | Secret Unlocks: the 52 codes and what each does |
| [credits.md](credits.md) | how the credits are built and scrolled; `data/credits.csv` |
| [movie-selector.md](movie-selector.md) | the leftover movie selector (P_FMVTEST): what it plays, and why it is unreachable |

## In short

A menu is a manager (`manager[n]`, 0x25f1d0, 0x1d8 bytes each) holding pages (`P_*`, 0x40xxxxxx) of controls
(`C_*` and sub-controls, 0x10xxxxxx), loaded from the menu data. Input becomes messages (`MessageType`, in
`src/action/ui/ui.h`): a control handles what it can and passes the rest to its page, then the manager, then
`Handler_HandleMessage`, which calls the handler for the page's or control's hashcode (`tools/uihandler.py`
generates the dispatch in `ui.cpp`). The wheels with an iris are Scroll controls over an M_ITEM list; the rows of
options picked with left and right are Radio controls.

Names: the game keeps none for messages, labels or sub-controls. Text labels are named after their English text
(`python tools/ui/text_bank.py <UKTxt.Dat> <label>` prints it; the banks come out of the filesys archives with
`tools/anim/edl.py` from the blender-exports branch, or any EDL reader), sub-controls `SUB_<owner>_<role>`, and
invented function names in Ghidra carry an "INVENTED NAME" plate comment.

## Reimplemented

| File | What |
|---|---|
| `ui_nightfire.cpp` | `sp_level`, `difficulty`; P_NFMAP, C_SBNFMAP, P_NFDFCTY, C_SBNFDFCTY |
| `ui_mp.cpp` | `mp_level`, `mp_scenario`, `mp_characters`, `mp_characters_small`, `mp_options`, `mp_bots`; P_MPMAP, C_SBMPMAP, P_MPSCENARIO, C_SBMPSCEN, P_MPOPTIONS, C_SBMPOPTIONS, P_MPBOTS, C_SBBOTS, P_MPBOTCHOOSE, C_SBMPBTCHOOSE, C_RBMPSETUP, P_MPPLAYERMODS, P_MPENVIROMODS |
| `ui_codenames.cpp` | `cn_options`; P_CNMENU, C_SBCNOPTIONS |
| `ui_secrets.cpp` | Menu_SpecialCodenameCheck, Menu_UpgradeCheat, P_CNNAME, C_KEYBOARD |
| `ui_credits.cpp` | Menu_InitCredits, Menu_SetupCredits (from `CreditsData.inc`, generated from `data/credits.csv`), P_CREDITS |
| `ui_dossier.cpp` | `ds_options`, `ds_weapons`, `ds_gadgets`; the dossier pages (earlier work) |

The item lists stay in the game's memory, reached through address macros in `ui.h` (`sp_level` is
`(*(M_ITEM(*)[12])0x0017c580)`), because original code still run - unlocking missions and characters, cheats,
upgrades - writes them. Their shipped contents are in the source beside their handlers as `<name>_shipped`,
checked against the game's at start (`MenuCheckLists`); once nothing original writes a list, its macro becomes a
definition initialised from them. (An alternative was built and is not used: `// RELOCATE` on an array of ours plus
`tools/data_refs.py` repoints every code reference at it - `src/common/xbeRelocate.h`.)

Not yet: P_MPDEBRIEFING and P_MPCONFIRM (the last two list users; they need a played match to test), and the
handlers without lists (`handlers.md` step 2).

## Checking against the original

`src/action/devtools/MenuProbe.cpp`, off unless `settings.ini` asks:

- `MenuLog=on` logs every handler message; `MenuLogSkip=0x50,0x51` leaves out the per-frame ones.
- `MenuScript=<file>` replays pad input from a script (`tools/ui/scripts/`): `wait`, `waitpage <page>` (so a run
  is timed from the page, whatever the boot took), `press`, `hold`, `focus <control> [id]`, `gopage <page>`,
  `shot <name>`, `log`, `secretstest`, `quit`.
- `MenuOriginal=<hash>:<address>,<address>` runs those handlers or functions as the original code.
- `MenuCheckLists=on` compares the lists' shipped contents in our source with the game's (all 12 identical).

`tools/ui/run_menu.sh <script> [name]` runs a script from a fresh boot (language, intro, start page) in
`build/menurun/<name>/`, with a copy of `Release/saves` so the start page leads to the main menu, and writes the
named log (`menu.log`, by `tools/ui/menu_log.py`) and PNG screenshots. With `ORIGINAL=...` it runs the originals;
`tools/ui/compare_shots.py <run> <run>` compares the screenshots. Every reimplemented handler above was run both
ways through the scripts in `tools/ui/scripts/`: identical screenshots and identical message logs (the one
difference being the random map Quick Game picks). `devtools/SecretsShadow.cpp` feeds all 52 codes through the
original and our code check from four starting states (224 runs, no differences).

## Ghidra

`tools/ui/make_ghidra_json.py` writes `driving-symbol-matching/results/structs/action-ui.json` (MessageType and
CONTROL_TYPE, M_CONTROL, M_WIDGET, M_PAGE, M_MANAGER, M_MESSAGE, MENU_LS, MPJoinSlot, and seven prototype
fixes) for `ghidra/NightfireStructs.py`, which now takes enums. Renamed in Ghidra during the pass, with plate
comments: Menu_CreateOptionBoxLabel (0x7f3a0, the label overload of Menu_CreateOptionBox), Menu_StopFrontEndMusic
(0x7fd70), Menu_ValidateCodename (0x76330), Menu_CodenameExists (0x75f40), Menu_AllJoinedPlayersReady (0x753f0).
