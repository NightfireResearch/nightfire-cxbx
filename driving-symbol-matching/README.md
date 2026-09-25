# Driving engine symbol matching

Putting the canonical names from the PS2 symbol spreadsheet onto Driving.xbe in Ghidra. The sheet maps
names to PS2 retail addresses only, so the work is matching PS2 functions to Xbox functions.

## Safety rules

- Everything here reads Ghidra through `lib/ghidra_ro.py`. It sends only GET requests to a whitelist of read
  endpoints, and always names the program (an unnamed request hits whichever program is current).
- Writes will go through one script only (not written yet). It will take a fresh snapshot first, run dry by
  default, write only proposals the user has approved, and log every change it makes.
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
| `results/` | reports and reviewed proposals (committed) |
| `data/` | sheet download, snapshots, caches (not committed) |

Setup: `pip install openpyxl`, then
`python -c "from lib import sheet; sheet.download(); sheet.parse()"`, `python snapshot.py`,
`python snapshot.py DRIVING.ELF`.

## Findings so far

- Sheet: 12,190 symbols, names cut at 63 characters, sparse `.obj` markers. Colour is on the PS2 address
  column. A green row without an address was named in Ghidra but not copied back; `lib/index.py` recovers
  those addresses by name, within the neighbouring rows' addresses.
- A sheet size can include static functions the symbol file doesn't list (`deleteSysFiles`).
- Xbox order: compilation units stay together and in similar order, but the order within a unit is shuffled
  (87% of adjacent pairs are within 40 sheet rows; only 51% lie on one rising sequence).
- MSVC on Xbox folds identical functions, adds scalar deleting destructors, and emits STL and SEH helpers with no
  canonical name. See `results/worked-examples.md`.
