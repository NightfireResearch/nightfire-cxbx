# Menu system pass - plan

Branch `ui-reimplementation` (from `dev`, 29 Sept 2026). The front end and pause menus of the action engine
(`default.xbe`): pages (`P_*`, hashcodes 0x40xxxxxx), controls (`C_*`, 0x10xxxxxx) and their handlers,
dispatched by `Handler_HandleMessage` (0x8e320, generated from `tools/uihandler.py` into `src/action/ui/ui.cpp`).

Names: the game has no canonical names for message types, text labels or sub-controls. Invented names follow
how the value is used; sub-controls are `SUB_<owning handler>_<ROLE>` (e.g. `SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT`),
text labels are named after their English string (tools/ui/text_bank.py reads the game's text banks).

## Checklist

### 0. Groundwork
- [x] Branch `ui-reimplementation`.
- [x] Text banks: `tools/ui/text_bank.py` decodes `UKTxt.Dat` etc. (from the filesys archives) exactly as
      `Txt_BindLabel` resolves a label, so every `Action_TranslatedText` can be named from its string.
- [x] Inventory: every handler (size, messages handled, M_ITEM lists and sub-controls used, reimplemented or
      not) - `docs/ui/handlers.md`, `docs/ui/items.md`, `docs/ui/items.json`.

### 1. MessageType
- [x] Every message id: senders, handling control types, arguments, return - `docs/ui/messages.md`/`.json`.
- [x] `MessageType` (97 values), `ControlType`, `ControlState`, `M_CONTROL`/`M_WIDGET`/`M_SCROLL`/`M_PAGE`/
      `M_MANAGER`/`M_MESSAGE` in `src/action/ui/ui.h`; existing code renamed (the old `M_MANAGER` was 0x1c0 bytes,
      really 0x1d8, so `manager[n]` was wrong for n > 0).
- [ ] Round-trip to Ghidra: `ghidra/NightfireStructs.py` now takes `enums`; an `action-ui.json` to write.

### 2. Secrets (cheat code) page
- [x] Mapped (`docs/ui/secrets.md`): P_CNNAME in secret mode + C_KEYBOARD; 52 plain codes (two stored as
      immediates), strcmp, toggles for missions and single rewards.
- [x] Reimplemented (`src/action/ui/ui_secrets.cpp`): Menu_SpecialCodenameCheck (table-driven),
      Menu_UpgradeCheat, P_CNNAME, C_KEYBOARD. Shadow test (`devtools/SecretsShadow.cpp`): every code from 4
      starting states against the original, 224 runs, 0 mismatches (and it catches a planted one-bit error).
      PASSPORT typed through the reimplemented keyboard gives the same screens and messages as the original.

### 3. Credits
- [x] `data/credits.csv` (578 lines) from `Menu_SetupCredits`' bytes (`tools/ui/extract_credits.py`).
- [x] `tools/ui/gen_credits_table.py` -> `src/action/ui/CreditsData.inc`; `src/action/ui/ui_credits.cpp`
      reimplements Menu_InitCredits, Menu_SetupCredits and P_CREDITS: screenshots at six points through the
      scroll and the exit, and the message log, identical to the original's.

### 4. Movie selector
- [x] P_FMVTEST / P_FMVPLAYER mapped (`docs/ui/movie-selector.md`): 22 movies by dev name; dead code on a retail
      disc (nothing sends GoPage 0x4000004f; the menu data has the page but no way in).

### 5. M_ITEM lists and handlers
- [x] All 12 static lists (0x17c580-0x17d540) in the source, each next to its handlers, byte-identical to the
      shipped data (`MenuCheckLists`); the `mp_scenario` locked label fixed (0x1ab "Fixed Gun Emplacements" ->
      0x010000ab "This scenario is locked.").
- [x] The lists are used in the game's memory (address macros in `ui.h`), since unlock code still run as the
      original writes them; their contents are in the source as `<name>_shipped`, to become the definitions once
      nothing original writes them. (`// RELOCATE` + `tools/data_refs.py`, repointing the code at our arrays, was
      built and dropped in favour of this.)
- [x] Reimplemented, and identical to the original in screenshots and messages: P_NFDFCTY, C_SBNFDFCTY, P_NFMAP,
      C_SBNFMAP, P_MPOPTIONS, P_MPBOTS, C_SBBOTS, P_CNMENU, C_SBCNOPTIONS.
- [ ] Still to do: P_MPBOTCHOOSE, C_SBMPBTCHOOSE, C_SBMPSCEN, C_SBMPOPTIONS, C_RBMPSETUP, P_MPDEBRIEFING,
      P_MPCONFIRM (and the non-list handlers in `docs/ui/handlers.md` step 2).

### 6. Probes (separate compilation units, `src/action/devtools/`)
- [x] `MenuProbe.cpp`: `MenuLog` (every handler message), `MenuScript` (replayed pad input: wait, waitpage,
      press, hold, focus, gopage, shot, log, secretstest, quit), `MenuOriginal` (run chosen handlers/functions as
      the original, for A/B runs), `MenuCheckLists`. `tools/ui/run_menu.sh` runs a script from a fresh boot in
      its own folder; `tools/ui/menu_log.py` names the log; `tools/ui/compare_shots.py` diffs two runs.

### 7. Wrap up
- [ ] Ghidra JSON; `docs/ui/README.md`.
