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
  never made into functions may be created.
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
| `apply.py` | the only writer: `python apply.py results/batches/batch-001.json [--apply]`, `--undo <log>` |
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
- PS2 infill accuracy (`validate_infill.py`, 30% of known addresses hidden, 5 trials): equal-count modes
  99.8% ("exact") and 99.9% ("count"); **exact-run 94%, and 78% inside runs of equal sizes**. Exact-run is a
  hint only: names resting on it need a body comparison or the slot-order check before a batch takes them.
  68% of functions keep exactly the same size between the symbol build and retail; the ~12% shrink is
  concentrated in the rest.
- A sheet size can include static functions the symbol file doesn't list (`deleteSysFiles`).
- Xbox order: compilation units stay together and in similar order, but the order within a unit is shuffled
  (87% of adjacent pairs are within 40 sheet rows; only 51% lie on one rising sequence).
- MSVC on Xbox folds identical functions, adds scalar deleting destructors, and emits STL and SEH helpers with no
  canonical name. See `results/worked-examples.md`.
