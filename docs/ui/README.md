# The action engine's menus

How the front end and pause menus of `default.xbe` work, what is reimplemented, and the tools for checking a
reimplementation against the original. The plan and its state: [PLAN.md](PLAN.md).

| Document | Covers |
|---|---|
| [framework.md](framework.md) | the framework under the handlers: managers and menu sets, the menu data, finding controls, the frame loop, input and focus, dispatch and return values, the page stack, drawing, timers, text, sounds, shared helpers; what is ours |
| [rewrite.md](rewrite.md) | plan for rewriting the 64 remaining handlers: classification, table-driven patterns, infrastructure first, order, verification and replay coverage, size, risks |
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
| `ui_mp.cpp` | `mp_level`, `mp_scenario`, `mp_characters`, `mp_characters_small`, `mp_options`, `mp_bots`; P_MPMAP, C_SBMPMAP, P_MPSCENARIO, C_SBMPSCEN, P_MPOPTIONS, C_SBMPOPTIONS, P_MPBOTS, C_SBBOTS, P_MPBOTCHOOSE, C_SBMPBTCHOOSE, C_RBMPSETUP, P_MPCONFIRM, P_MPDEBRIEFING, P_MPPLAYERMODS, P_MPENVIROMODS, Menu_GetMPSkins |
| `ui_codenames.cpp` | `cn_options`; P_CNMENU, C_SBCNOPTIONS |
| `MenuUnlocks.cpp` | the campaign's progress and rewards: missions open, bonus masks, upgrades, MP characters/scenarios unlocked |
| `ui_secrets.cpp` | Menu_SpecialCodenameCheck, Menu_UpgradeCheat, P_CNNAME, C_KEYBOARD |
| `ui_credits.cpp` | Menu_InitCredits, Menu_SetupCredits (from `CreditsData.inc`, generated from `data/credits.csv`), P_CREDITS |
| `ui_dossier.cpp` | `ds_options`, `ds_weapons`, `ds_gadgets`; the dossier pages (earlier work) |

The item lists are ours: arrays in the source beside their handlers. They lived in the game's memory, reached
through address macros, until every function that refers to them was reimplemented - the handlers, and the
progress/unlock functions in `MenuUnlocks.cpp` (Menu_Get/SetNightfireStatus, Menu_SetBonus, Menu_SetLevelBonus,
Menu_GetObjectUpgradeLevel, Menu_UnlockMPSkins, Menu_UnlockMPSettings; Menu_GetMPSkins is in `ui_mp.cpp`). Nothing
original refers to the game's copies now (every reference site checked against Ghidra), so the arrays are the
definitions. A consequence for A/B runs: an original handler run with `MenuOriginal` reads the game's copy, which
no longer changes. (An alternative was built and is not used: `// RELOCATE` on an array plus `tools/data_refs.py`
repoints every code reference at it - `src/common/xbeRelocate.h`.)

Not yet: the handlers without lists (`handlers.md` step 2).

## Checking against the original

`src/action/devtools/MenuProbe.cpp`, off unless `settings.ini` asks:

- `MenuLog=on` logs every handler message; `MenuLogSkip=0x50,0x51` leaves out the per-frame ones.
- `MenuScript=<file>` replays pad input from a script (`tools/ui/scripts/`): `wait`, `waitpage <page>` (so a run
  is timed from the page, whatever the boot took), `press`, `hold`, `focus <control> [id]`, `gopage <page>`,
  `shot <name>`, `log`, `secretstest`, `poke <address> <value>`, `seed`, `quit`.
- `MenuOriginal=<hash>:<address>,<address>` runs those handlers or functions as the original code.
- `MenuCheckLists=on` compares the lists in our source with the game's copies (all 12 identical).
- `MenuShadowTests=on` runs the shadow tests at start, before the game (no display needed): `SecretsShadow.cpp`
  (every code, 224 runs), `UnlocksShadow.cpp` (the seven progress/unlock functions over their inputs, 1440 runs),
  `UpgradeShadow.cpp` (the weapon upgrade readers) and `MemShadow.cpp` (the heap allocator, ~19000 random calls
  on two scratch heaps).

`tools/ui/run_menu.sh <script> [name]` runs a script from a fresh boot (language, intro, start page) in
`build/menurun/<name>/`, with a copy of `Release/saves` so the start page leads to the main menu, and writes the
named log (`menu.log`, by `tools/ui/menu_log.py`) and PNG screenshots. With `ORIGINAL=...` it runs the originals;
`tools/ui/compare_shots.py <run> <run>` compares the screenshots. Every reimplemented handler above was run both
ways through the scripts in `tools/ui/scripts/`: identical screenshots and identical message logs (the one
difference being the random map Quick Game picks). `devtools/SecretsShadow.cpp` feeds all 52 codes through the
original and our code check from four starting states (224 runs, no differences).

> **WARNING: replays are not deterministic in play, only in the menus.** The random number generator is never
> seeded, so its sequence is fixed, but the game logic steps by real elapsed time rather than a fixed tick per
> frame. How many random numbers have been drawn by a given poll frame therefore depends on how many frames were
> rendered, and AI, physics and animation drift between runs too. The `seed` script command puts the generator back
> to its boot state, which makes the *next* random choice repeatable (`mp_start.txt` uses it so Quick Game always
> picks the same level) - but everything after it drifts again: two seeded runs of `mp_start.txt` load the same level
> and then differ within seconds. Seed right before the random step, take in-level screenshots as soon after it as
> possible, and do not expect pixel-identical shots from the middle of a match. Making play repeatable would need a
> fixed-timestep mode.

## Ghidra

`tools/ui/make_ghidra_json.py` writes `driving-symbol-matching/results/structs/action-ui.json` (MessageType and
CONTROL_TYPE, M_CONTROL, M_WIDGET, M_PAGE, M_MANAGER, M_MESSAGE, MENU_LS, MPJoinSlot, and seven prototype
fixes) for `ghidra/NightfireStructs.py`, which now takes enums. Renamed in Ghidra during the pass, with plate
comments: Menu_CreateOptionBoxLabel (0x7f3a0, the label overload of Menu_CreateOptionBox), Menu_StopFrontEndMusic
(0x7fd70), Menu_ValidateCodename (0x76330), Menu_CodenameExists (0x75f40), Menu_AllJoinedPlayersReady (0x753f0).
