# Driving engine symbol matching

Putting the canonical names from the PS2 symbol spreadsheet onto Driving.xbe in Ghidra. The sheet maps
names to PS2 retail addresses only, so the work is matching PS2 functions to Xbox functions.

## Safety rules

- Everything here reads Ghidra through `lib/ghidra_ro.py`. It sends only GET requests to a whitelist of read
  endpoints, and always names the program (an unnamed request hits whichever program is current).
- Writes go through `apply.py` only. It runs dry by default. With `--apply` it snapshots first, then checks
  every item against live Ghidra: the current name must match what was reviewed, the old name must not be used
  in `src/driving`, and new names must be unique. One failed check stops the whole batch. It reads back each
  write and logs old and new values for `--undo`.
- Conventions (user, 25 Sept 2026): a folded function takes its first canonical name by sheet row, with the
  full list in the plate comment. MSVC's vtable slot 0 is `Class::scalar_deleting_destructor`, and the
  function it calls that stores the class's vtable is `Class::~Class`. Functions a vtable reaches but Ghidra
  never made into functions may be created. PS2 syscall stubs keep their friendly names (SetupHeap, not RFU061),
  as do written-out templates and GCC static initialisers; the symbol file's name goes in the plate comment.
- `snapshot.py` records every function's name, namespace, signature and plate comment, enough to undo names
  and signatures. It does not replace a copy of the Ghidra project; take one of those before a batch too.
- Beware MCP tools that sound read-only but aren't: `disassemble_bytes` disassembles into the listing
  (it was used once, on 25 Sept 2026, at Xbox 0x1d090-0x1d0bf, a real vtable target).

## Layout

| path | what |
|---|---|
| `lib/sheet.py` | download (xlsx keeps the colours) and parse the sheet |
| `lib/ghidra_ro.py` | read-only Ghidra HTTP client (port 8089) |
| `lib/index.py` | sheet row <-> PS2 address <-> Xbox names, from the latest snapshots |
| `snapshot.py` | `python snapshot.py [program]`: take a snapshot to `data/snapshots/` |
| `region.py` | `python region.py 0x59900 0x5bb00`: Xbox range beside its sheet window |
| `vtable_pairs.py` | `python vtable_pairs.py AICharacterBond`: PS2 and Xbox vtables slot by slot |
| `lib/image.py` | both programs' section bytes, cached in `data/image/` |
| `validate_infill.py` | held-out accuracy test of the infill |
| `lib/ps2_infill.py` | PS2 addresses for sheet rows from retail layout (exact sizes between known rows) |
| `vtables.py` | every Xbox vtable (code-pointer runs stored by `mov [reg], imm32`) paired with the sheet's PS2 vtables |
| `propose_vtables.py` | Xbox name proposals from the certain vtable pairs; folded functions and destructors handled |
| `apply.py` | the only writer: `python apply.py results/batches/batch-001.json [--apply]`, `--undo <log>`, `--namespaces` (checklist of by-hand moves). A batch's `program` picks Driving.xbe (default) or DRIVING.ELF; an item with `"rename": false` gets only the plate note |
| `../ghidra/NightfireNamespaces.py` | run by hand in Ghidra (Script Manager, category Nightfire): moves the functions listed in `results/namespace-moves.json` (`python apply.py --pending-namespaces`) into their namespaces, after showing the list and asking; one undoable step |
| `classes.py` | which namespaces are C++ classes, from the symbol file (vtable, type_info, constructor/destructor, const method): `results/class-namespaces.json` and `.md` |
| `../ghidra/NightfireClasses.py` | run by hand in Ghidra: converts the listed namespaces into classes, after showing the list and asking; one undoable step |
| `lib/ghidra_rw.py` | write whitelist used by apply.py (create_function, rename, plate comment; Driving.xbe only) |
| `results/` | reports, proposals and approved batches (committed) |
| `data/` | sheet download, snapshots, caches (not committed) |

Setup: `pip install openpyxl`, then
`python -c "from lib import sheet; sheet.download(); sheet.parse()"`, `python snapshot.py`,
`python snapshot.py DRIVING.ELF`.

## Findings so far

- Sheet: 12,190 symbols, names cut at 63 characters, sparse `.obj` markers. Colour is on the PS2 address
  column. A green row without an address was named in Ghidra but not copied back; `lib/index.py` recovers
  those addresses by name, within the neighbouring rows' addresses.
- PS2 infill accuracy (`validate_infill.py`, 30% of known addresses hidden, 5 trials): "exact" 99.8%,
  "count" 99.9%, "exact-run" 99.4% (91% inside runs of equal sizes). It was 94%/78% until two fixes:
  "global constructors keyed to" rows are real functions and must stay in the walk, and a window whose retail
  span is longer than its sheet span (or under 60% of it) straddles a linker discontinuity and is skipped. The
  sheet lists EA's sound library twice. Exact-run and count-only names still need a second check before a
  batch takes them.
- Thunks show their target's name until given their own (`__pure_virtual` = `j __terminate` in retail).
  Snapshots record `thunk`, and the index treats a thunk that carries a real function's name as unnamed, so it
  can't match a sheet row or vote in a vtable pairing (23 such thunks on Xbox, 25 Sept 2026).
- Infill conflicts with existing PS2 names are settled in `results/ps2-name-resolutions.json` (the index
  reads it): "sheet" means the same function under another name, "reject"/"open" means don't place it.
- Sheet names are cut at 63 characters: 1,122 rows. Where the cut falls in the argument list the name is
  whole; where it falls in the name, `sheet.complete_name` completes constructors and destructors from the
  class, and the rest (242, mostly templates) are never proposed. Signatures from cut rows will need their
  arguments from the PS2 prototype.
  68% of functions keep exactly the same size between the symbol build and retail; the ~12% shrink is
  concentrated in the rest.
- A sheet size can include static functions the symbol file doesn't list (`deleteSysFiles`).
- Xbox order: compilation units stay together and in similar order, but the order within a unit is shuffled
  (87% of adjacent pairs are within 40 sheet rows; only 51% lie on one rising sequence).
- MSVC on Xbox folds identical functions, adds scalar deleting destructors, and emits STL and SEH helpers with no
  canonical name. See `results/worked-examples.md`.
