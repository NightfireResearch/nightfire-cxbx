"""Complete sheet names the 63-character cut left unfinished, where the answer is not in doubt.

    python complete_cut.py   -> results/cut-name-completions.json (+ .md), read by make_ps2_batch.py

Rules, each checked against the text the sheet kept (a completion must start with it, spaces aside):
  fgThis     "global constructors keyed to RLightManager::fgThis_RLightManag" -> RLightManager::fgThis_RLightManager
             (the engine's singleton variables are all fgThis_<Class>)
  auf        exactly one Agent Under Fire (GameCube) function, demangled, starts with the cut text
  sibling    a cut method whose neighbouring rows share the rest of its name (listed by hand below)
  sgi        SGI STL algorithm templates, whose parameter lists are fixed by the STL source: the element type
             comes from the visible first argument ("X *"), e.g. __push_heap<X *, int, X>
Everything else stays uncompleted and is listed for a manual check. Read-only.
"""

import json
import os
import re

from lib import names, sheet
from lib.cw_demangle import demangle
from lib.index import Index

HERE = os.path.dirname(os.path.abspath(__file__))

# SGI STL (the PS2 build's STL): template parameter lists by function, in terms of the iterator's element X.
SGI = {
    "__push_heap": ["X *", "int", "X"], "__adjust_heap": ["X *", "int", "X"], "__make_heap": ["X *", "X", "int"],
    "__partial_sort": ["X *", "X"], "__unguarded_partition": ["X *", "X"], "__introsort_loop": ["X *", "X", "int"],
    "__unguarded_linear_insert": ["X *", "X"], "__unguarded_insertion_sort_aux": ["X *", "X"],
    "__uninitialized_copy_aux": ["X *", "X *"], "__lower_bound": ["X *", "X", "int"],
    "remove_copy": ["X *", "X *", "X"], "find": ["X *", "X"], "__lexicographical_compare_3way": ["X *", "X *"],
}
SIBLING = {
    # row: completion (the neighbouring rows: CopyPositionalLightsToPlatformSpecific,
    # SetAmbientPlatformSpecific, DisableDirectionalDiffuseToPlatformSpecific)
    3132: "RLightManager::EnableAndCopyDirectionalDiffuseToPlatformSpecific",
}


def squash(s):
    return re.sub(r"\s+", "", s)


def sgi(text):
    """'void __push_heap<AttributeStoreBlock *, int, AttributeStoreBlo' -> '__push_heap<...full...>' or None."""
    body = re.sub(r"^(?:[\w:]+\s*\**\s+)+(?=[\w:~]+<)", "", text)
    m = re.match(r"^([\w:]+)<(.*)$", body)
    if not m or m.group(1) not in SGI:
        return None
    first = m.group(2).split(",")[0].strip()
    if not first.endswith("*"):
        return None
    x = first[:-1].strip()
    args = [a.replace("X", x).replace("* *", "**") for a in SGI[m.group(1)]]
    full = f"{m.group(1)}<{', '.join(args)}>"
    return full if squash(full).startswith(squash(body)) else None


def main():
    ix = Index()
    auf = []
    with open(os.path.join(HERE, "data", "auf-functions.json")) as f:
        for _, n in json.load(f):
            d = demangle(n)
            auf.append(d[0] if d else n)
    out, left = [], []
    for r in ix.rows:
        if not r["truncated"] or sheet.complete_name(r["name"]) is not None:
            continue
        text = r["name"]
        m = re.match(r"^global (constructors|destructors) keyed to (.*)$", text)
        key = m.group(2) if m else text
        done = None
        if r["row"] in SIBLING:
            done = (SIBLING[r["row"]], "sibling")
        elif m and re.search(r"::fgThis_\w*$", key):
            cls, var = key.rsplit("::", 1)
            if cls.split("::")[-1].startswith(var[len("fgThis_"):]):
                done = (f"{cls}::fgThis_{cls.split('::')[-1]}", "fgThis")
        if done is None:
            hits = sorted({a for a in auf if squash(a).startswith(squash(key)) and len(a) > len(squash(key))})
            if len(hits) == 1:
                done = (hits[0], "auf")
        if done is None and not m:
            s = sgi(key)
            if s:
                done = (s, "sgi")
        if done is None:
            left.append(r)
            continue
        full = done[0]
        if m:
            full = f"global {m.group(1)} keyed to {full}"
        out.append({"row": r["row"], "cut": r["name"], "complete": full, "rule": done[1]})
    with open(os.path.join(HERE, "results", "cut-name-completions.json"), "w") as f:
        json.dump(out, f, indent=1)
    lines = ["# Sheet names cut at 63 characters", "", f"{len(out)} completed (rule in brackets), {len(left)} not.", "",
             "## Completed", ""] + [f"- row {c['row']}: `{c['cut']}` -> `{c['complete']}` ({c['rule']})" for c in out]
    lines += ["", "## Not completed (manual check)", ""] + [f"- row {r['row']}: `{r['name']}`" for r in left]
    with open(os.path.join(HERE, "results", "cut-name-completions.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{len(out)} completed, {len(left)} left", {k: sum(1 for c in out if c['rule'] == k) for k in ("fgThis", "auf", "sibling", "sgi")})


if __name__ == "__main__":
    main()
