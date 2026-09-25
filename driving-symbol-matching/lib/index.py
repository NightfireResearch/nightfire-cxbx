"""Join the sheet to the two programs' current names, from the latest snapshots.

    sheet row -> PS2 address   the sheet's own address, else the one PS2 function carrying the row's name
                               between the neighbouring rows' known addresses (green rows named in Ghidra
                               but not copied back to the sheet)
    Xbox function -> sheet row the one row whose name the Xbox function already carries
"""

import bisect
import glob
import json
import os

from lib import sheet

HERE = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def latest_snapshot(program):
    paths = sorted(glob.glob(os.path.join(HERE, "data", "snapshots", f"{program}-*.json")))
    if not paths:
        raise FileNotFoundError(f"no snapshot of {program}: run snapshot.py {program}")
    with open(paths[-1]) as f:
        return json.load(f)["functions"]


def key(name):
    """Comparable form of a sheet or Ghidra name: 'A::operator delete(void *' -> 'A::operator_delete'."""
    return sheet.base_name(name).replace(" ", "_")


class Index:
    def __init__(self, xbox="Driving.xbe", ps2="DRIVING.ELF"):
        self.rows = sheet.load()
        self.xbox = {int(f["address"], 16): f for f in latest_snapshot(xbox)}
        self.ps2 = {int(f["address"], 16): f for f in latest_snapshot(ps2)}
        self.ps2_addrs = sorted(self.ps2)
        self.xbox_addrs = sorted(self.xbox)
        self._row_ps2()
        self._infill()
        self._xbox_rows()

    def _row_ps2(self):
        rows = self.rows
        known = [(i, r["ps2"]) for i, r in enumerate(rows) if r["ps2"] is not None]
        for r in rows:
            r["ps2_from"] = "sheet" if r["ps2"] is not None else None
        # Neighbouring known addresses bound where an unrecorded row's function can be.
        k = 0
        for i, r in enumerate(rows):
            while k < len(known) and known[k][0] < i:
                k += 1
            if r["ps2"] is not None:
                continue
            lo = known[k - 1][1] if k > 0 else 0
            hi = known[k][1] if k < len(known) else 1 << 32
            if hi < lo:  # the slip went backwards here; no safe window
                continue
            want = key(r["name"])
            a, b = bisect.bisect_left(self.ps2_addrs, lo), bisect.bisect_right(self.ps2_addrs, hi)
            hits = [x for x in self.ps2_addrs[a:b] if key(self.ps2[x]["qualified"]) == want]
            if len(hits) == 1:
                r["ps2"], r["ps2_from"] = hits[0], "ghidra"

    def _infill(self):
        """PS2 addresses from retail layout (lib/ps2_infill.py), unless PS2 Ghidra already names it otherwise."""
        from lib.ps2_infill import infill

        self.ps2_conflicts = []
        res_path = os.path.join(HERE, "results", "ps2-name-resolutions.json")
        resolved = {}
        if os.path.exists(res_path):
            with open(res_path) as f:
                for d in json.load(f)["resolutions"]:
                    resolved[d["row"]] = (int(d["ps2"], 16), d["verdict"])
        taken = {r["ps2"] for r in self.rows if r["ps2"] is not None}
        for i, (a, how) in infill(self).items():
            r = self.rows[i]
            have = self.ps2[a]["qualified"]
            verdict = resolved.get(r["row"])
            if verdict and verdict[0] == a and verdict[1] in ("reject", "open"):
                continue
            if verdict and verdict[0] == a and verdict[1] == "sheet":
                r["ps2"], r["ps2_from"] = a, "infill-" + how
                continue
            if a in taken:
                self.ps2_conflicts.append((r, a, how, have, "address already has a sheet row"))
            elif not have.startswith("FUN_") and not _same(have, r["name"]):
                self.ps2_conflicts.append((r, a, how, have, "PS2 Ghidra name differs"))
            else:
                r["ps2"], r["ps2_from"] = a, "infill-" + how

    def _xbox_rows(self):
        by_key = {}
        for i, r in enumerate(self.rows):
            by_key.setdefault(key(r["name"]), []).append(i)
        self.xbox_row = {}
        for a, f in self.xbox.items():
            hits = by_key.get(key(f["qualified"]), [])
            if len(hits) == 1:
                self.xbox_row[a] = hits[0]



def _same(ghidra_name, sheet_name):
    g, s = key(ghidra_name).lower(), key(sheet_name).lower()
    return g == s or s.endswith("::" + g) or g.replace("_", "") == s.replace("_", "").replace("::", "")


def body_size(f):
    """Bytes in the function's first body range ('0005b820 - 0005b863' -> 0x44)."""
    lo, _, hi = (f.get("body") or "").partition(" - ")
    try:
        return int(hi.split(",")[0].split()[0], 16) - int(lo, 16) + 1
    except ValueError:
        return None
