"""The PS2 symbol spreadsheet: download it and load it as a list of rows.

The xlsx export keeps the cell colours (the CSV export does not). The colour is on column D, the known PS2
retail address. Sizes and slips in the sheet are formulas; they are recomputed here rather than trusted.
"""

import json
import os
import urllib.request

SHEET_ID = "1dYU9zFLiWFfm3QR-ocu1MgkimK66zhSUX48rUAA0R1o"
HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
XLSX = os.path.join(HERE, "data", "sheet.xlsx")
JSON = os.path.join(HERE, "data", "sheet.json")

COLOURS = {
    "FF00FF00": "green",
    "FFFF9900": "orange",
    "FFFF0000": "red",
    "FFB7B7B7": "grey",
    "FFEA9999": "pink",
}


def download():
    url = f"https://docs.google.com/spreadsheets/d/{SHEET_ID}/export?format=xlsx"
    os.makedirs(os.path.dirname(XLSX), exist_ok=True)
    urllib.request.urlretrieve(url, XLSX)


def _hex(value):
    """The sheet stores some addresses as numbers ("107338.0"); they are still hex digits."""
    if value is None:
        return None
    s = str(value).strip()
    if s.endswith(".0"):
        s = s[:-2]
    if s.lower().startswith("0x"):
        s = s[2:]
    try:
        return int(s, 16)
    except ValueError:
        return None


def parse():
    import openpyxl

    ws = openpyxl.load_workbook(XLSX).worksheets[0]
    rows = []
    obj = None
    for r in ws.iter_rows(min_row=2):
        cells = [c.value for c in r[:8]]
        fill = r[3].fill
        colour = COLOURS.get(fill.fgColor.rgb, fill.fgColor.rgb) if fill and fill.fill_type else None
        sym, name = _hex(cells[0]), cells[2]
        if name is None:
            continue
        name = str(name)
        low = name.lower()
        if low.endswith((".obj", ".o", ".o)", ".obj)")) or ":" + chr(92) in name:  # object-file marker rows
            obj = name.split(chr(92))[-1]  # backslash: heredocs have eaten literal ones before
            continue
        rows.append({
            "row": r[0].row,
            "sym": sym,
            "name": name,
            "truncated": len(name) >= 63,
            "ps2": _hex(cells[3]),
            "colour": colour,
            "guess": _hex(cells[5]),
            "note": cells[7],
            "obj": obj,
        })
    for a, b in zip(rows, rows[1:]):
        a["size"] = b["sym"] - a["sym"] if a["sym"] is not None and b["sym"] is not None else None
    with open(JSON, "w") as f:
        json.dump(rows, f, indent=0)
    return rows


def load():
    with open(JSON) as f:
        return json.load(f)


def base_name(name):
    """'Class::Method(args' -> 'Class::Method', the part a Ghidra function name holds."""
    return name.split("(")[0].strip()
