# Credits screen (action engine, default.xbe)

Extracted 29 Sept 2026 from the raw bytes of `Menu_SetupCredits` (never decompile it in Ghidra: 25 KB of
straight-line code, ~30 s). Data: `data/credits.csv`; extractor: `tools/ui/extract_credits.py`.

## Functions

| Address | Name | What it does |
|---|---|---|
| 0x76460 | `Menu_InitCredits` (`void __stdcall (void)`) | `g_creditsTable (0x224f68) = NULL` - forgets the cached table (does not free it; the block is from `Menu_Malloc`'s pool) |
| 0x76470 | `Menu_SetupCredits` (`CreditsEntry *(uint *numLines_out)`) | builds the table, see below; returns it and writes 578 to `*numLines_out` |
| 0x8ded0 | `P_CREDITS_Handler` | the page handler: spawns one line every 14 frames and scrolls them |
| 0x6d460 | `Txt_BindLabel(label, 0)` | the only helper `Menu_SetupCredits` calls (209 times) |
| 0x72d40 | `Menu_Malloc(size, 0xc)` | the heap copy (first call only) |

`Menu_AddRow` / `Menu_AddRowPercentage` are **not** used by the credits.

## Menu_SetupCredits

There are no per-line helper calls and no conditions (no language, platform or region checks): it is one
basic block that fills a local `CreditsEntry[578]` (0x1b18 bytes, at `[esp+0xc]`) field by field:

```c
typedef struct CreditsEntry {   // 12 bytes, pack 1 (byte 11 is never written)
    char *txt_left;             // +0
    char *txt_right;            // +4  NULL on centred lines (the right control is hidden)
    uchar modifiers_left;       // +8  style 0/1/2
    uchar modifiers_right;      // +9  style 0/1/2
    uchar centred;              // +10 1 = one centred line (left text only)
} CreditsEntry;
```

Each text is either a literal pointer (`mov dword [esp+x], imm32`, or via `esi`/`edi` loaded with one;
0x160d8c `" "` is the pooled blank used for empty cells) or `Txt_BindLabel(label, 0)` resolved right there
(`push 0; push label; call 0x6d460; mov [esp+x], eax`). The labels are mostly bank 1 (`0x0100xxxx`) with 17 in
bank 0 (`0x000002a4`..`0x000002b4`, the common role titles: Project Lead, Senior Producer, Testers, Audio Lead,
Special Thanks...).

At the end: `if (g_creditsTable == NULL) { g_creditsTable = Menu_Malloc(0x1b18, 0xc); memcpy(it, local, 0x1b18); }`,
`*numLines_out = 0x242; return g_creditsTable;`. So the labels are resolved (and the pointers into the text
bank captured) only on the first entry after `Menu_InitCredits`; later entries rebuild the local array and
throw it away.

## P_CREDITS_Handler (0x8ded0)

Controls: `0x10000213` (left column) and `0x10000214` (right column) are arrays of 26 (0x1a) text controls
fetched with `__Menu_SendEx(mgr, hash, i, 0x39 GetControl)`; `0x10000215` is the fade overlay.
Offsets `+0x72` / `+0x74` of `M_CONTROL` are its y / x (shorts).

- **0x4c (enter)**: `frame = 0; table = Menu_SetupCredits(&count); index = 0; line = 0;` remembers the page it
  came from (7th stack argument); hides 0x10000213 and 0x10000214 (`0x2b, 1`); colour of 0x10000215 = 0;
  colour of 0x1000023c = `GameState.WeaponUpgradeRelated ? 0x404040ff : 0xff`; `fading = 0`; saves
  `SFXMusicGetVolume()`, calls `FUN_0007fd70()`, `SFXStartMusic(0x27, 0)`.
- **0x4d (kill)**: `SFXStopMusic(); SFXMusicSetVolume(saved); Menu_RestartFrontEndLoop();`
- **0x51**: `Menu_InitCredits()`.
- **0x50 (per frame)**, only if `table != NULL`:
  1. `++frame; if (frame % 14 == 0 && line < count)`: take control pair `index` of both columns (bail out of
     the frame if either is missing); both `y = 0x200`; left: show (`0x2b, 2`), SetText (`0x18`) `txt_left`,
     format (`0x23`) by `modifiers_left`; right: format by `modifiers_right` (always, even when centred);
     then if `!centred`: both `x = 0xfe`, show right, SetText `txt_right`; else left `x = 0x20c`, hide right.
     `index = (index + 1) % 26; ++line;`
  2. Music fade: `if (frame / 14 > count - 50)` volume = `saved * (1/700.0) * (count*14 - frame)`, clamped at 0
     (`__ftol2`); i.e. linear over the last 50 lines (700 frames), silent when the last line spawns.
  3. Exit: `if (!fading && frame / 14 > count + 18)`: `fading = 1`; delayed (0x13 frames) to the manager:
     `0x44 GoPage 0x40000002` if it came from page `0x40000053`, else `0x5f` (return to previous page);
     plus a delayed `0x68`; and `Process_Create(control 0x10000215, {type 0xe1, 0xff, 0}, 0xf)` (fade to black).
  4. Every one of the 26 pairs present: `y -= 2` (2 px per frame, so 28 px between lines).
- There is no input handling: the credits cannot be skipped from this handler.

The format strings (`0x23`) are text control codes: style 0 = `DAT_0015eb18` `FF 02 FE 03`, style 1 =
`DAT_00161954` `FF 01 FE 03`, style 2 = `DAT_0016195c` `FF 03 FE 03` (`FF n` looks like a colour index, `FE 3`
a font/size selector; not yet confirmed). In the data, style 0 = names, 1 = company / section headings
(EUROCOM, ELECTRONIC ARTS, MGM Interactive Inc., ...), 2 = role titles.

Timing at count = 578: last line spawns at frame 8092; music fades from frame 7406; the exit fires at frame
8358 (597 * 14); a line lives 26 * 14 = 364 frames (y 512 down to -216) before its control is reused.

## data/credits.csv

UTF-8 (the XBE strings are cp1252), every field quoted, one row per `CreditsEntry`, in table order:

| Column | Meaning |
|---|---|
| `index` | 0..577 |
| `centred` | byte +10 |
| `left_style`, `right_style` | bytes +8 / +9 |
| `left_label`, `right_label` | `0x%08x` text label passed to `Txt_BindLabel(label, 0)`, empty if a literal |
| `left_text`, `right_text` | the literal string (a single space `" "` is the blank pointer 0x160d8c); both label and text empty = NULL |
| `comment` | English text of the labels from UKTxt.Dat (informative only; filled when run with `--bank`) |

Round trip checks done by the extractor: 578 entries x 5 fields each written exactly once, 209 label cells =
209 `Txt_BindLabel` calls (all with second argument 0), all 497 distinct string immediates in the function are
used by entries, 578 = the 0x242 written to `*numLines_out` = 0x1b18 / 12.

Content: 270 centred lines, 308 two-column lines. Order: 22 lines of legal (labels), song credits, "James Bond
007(TM): NightFire(TM)" / "Development and Production", then the two-column Eurocom / Electronic Arts / EA
localisation / QA sections (rows 51-359), then centred voice, motion capture, outsourcing, MGM, Danjaq and
Special Thanks sections, "James Bond will return..." at row 565 and 12 trailing blank lines.

Quirks to keep verbatim: repeated names (Tim Rogers, Bill Beacham, Stephen Tang, Chris Jung, Chris Smith,
Matteo Milandri, Nadine Monschau twice in the German column, Danny Bilson); trailing spaces ("Nicklas
Mether ", "Sam Yazmadjian "), a leading space (" Localisations BV"), labels reused (e.g. `0x000002b4`
Special Thanks twice, `0x01000226` Executive Producer three times); non-ASCII cp1252 names (Vásquez, Maëlenn,
Frédéric, Gély, Pinés, Sánchez-Real, Göthe, Miché) and (C)/(R)/(TM) in the legal labels.

## Proposed reimplementation

Recommend a **generated, checked-in** table: `tools/ui/gen_credits_table.py` reads `data/credits.csv` and writes
`src/action/ui/CreditsData.inc` (committed, with a "generated - do not edit" header), so the build needs no
Python and the CSV stays the reviewable source. Emit literals as cp1252 bytes with `\xNN` escapes split into
separate string pieces (`"V\xe1" "squez"`) so a following hex digit is never swallowed, and emit identical
literals once (the original pools them; not observable, but keeps it small).

```c
typedef struct { uchar centred, lstyle, rstyle; Action_TranslatedText llabel, rlabel; const char *ltext, *rtext; } CreditsLine;
static const CreditsLine g_creditsLines[578] = {
#include "CreditsData.inc"   // { 1, 0, 0, 0x010002a5, NULL_LABEL, NULL, NULL }, ...
};

// AUTOINJECT
CreditsEntry *Menu_SetupCredits(uint *numLines_out)
{
    if (g_creditsTable == NULL) {
        CreditsEntry *t = (CreditsEntry *)Menu_Malloc(sizeof(CreditsEntry) * 578, 0xc);
        for (int i = 0; i < 578; i++) {
            const CreditsLine *l = &g_creditsLines[i];
            t[i].txt_left  = l->llabel != NULL_LABEL ? Txt_BindLabel(l->llabel, 0) : (char *)l->ltext;
            t[i].txt_right = l->rlabel != NULL_LABEL ? Txt_BindLabel(l->rlabel, 0) : (char *)l->rtext;
            t[i].modifiers_left = l->lstyle; t[i].modifiers_right = l->rstyle; t[i].centred = l->centred;
        }
        g_creditsTable = t;
    }
    *numLines_out = 578;
    return g_creditsTable;
}
```

(`NULL_LABEL` = `Action_TranslatedText_NULLVALUE` (0xFFFFFFFF, src/action/assets.h); 0 is not free since
bank-0 labels such as `0x000002a4` are used.) Resolving the labels inside the `if` instead of
always is behaviourally identical: the original's per-call resolution is discarded when the cache exists.

Must preserve:
- the cache in the original global 0x224f68 (shared with `Menu_InitCredits` and the handler), allocated with
  `Menu_Malloc(0x1b18, 0xc)`, and the label resolution happening at first entry (language already loaded);
- `*numLines_out = 578` and the exact row order, styles, blanks and trailing blank lines (they set the timing:
  14 frames per line, exit at `frame / 14 > count + 18`, music fade over the last 50 lines);
- the handler constants: 26 recycled control pairs, y start 0x200, x 0xfe / 0x20c, 2 px per frame, the exit
  pages (0x40000053 -> GoPage 0x40000002, else 0x5f), delay 0x13, fade process 0xe1 / 0xff / 0xf, music 0x27.

`P_CREDITS_Handler` can then be reimplemented separately (it only reads the table through the struct), or left
as the original.
