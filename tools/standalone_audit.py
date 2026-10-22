#!/usr/bin/env python3
"""Every raw XBE address left in an engine's source, classified: the worklist for a build that runs with no original
code or data mapped (docs/driving/standalone.md).

    python tools/standalone_audit.py                 counts by class, then by file
    python tools/standalone_audit.py --action        the action engine's (default.xbe, src/action), with any of these
    python tools/standalone_audit.py --md            the same as the Markdown tables docs/driving/standalone.md keeps
    python tools/standalone_audit.py --list CLASS    every occurrence of one class (a prefix will do: "code.")
    python tools/standalone_audit.py --file PATH     every occurrence in the files whose path contains PATH

It reads src/driving outside devtools/ and the generated files, and Driving.xbe's sections, functions
(tools/functions_driving.json), vtables (tools/vtables_driving.json) and globals (tools/global_coverage.py's reading
of our source); with --action, src/action and default.xbe's (no vtables file: the class is empty). A literal is a hex
number of five to eight digits whose value lies inside the image (0x00010000-0x00254f40, the action engine's to
0x0030b660); anything else is a constant, not an address, and is not counted. The classes:

  tag.func_at        the address in a // FUNC_AT(...) or AUTOGEN(...) tag: injection only
  tag.global         the address in a // XBE_GLOBAL(...) tag: the coverage tools' key, our definition follows
  tag.comment        any other comment, or inside a string
  patch.site         an address a seam writes a jump or call over (WriteJump, PatchSite, WriteJmpTo, XBE_ENTRY...)
  code.called        a function's address cast to a function pointer, or loaded into a register and called
  code.pointer       a function's address used as a value (a table, a callback, a thread entry)
  code.other         an address inside code that is not a function's entry (D3D's state inside its section, a
                     callback's address in the middle of what Ghidra calls one function)
  constant           a number that lands inside code by chance: written with fewer than eight digits (sizes, masks,
                     sizes), a row of a data table, a #define of a bare number or a flags field's initialiser
  import.slot        a kernel import slot (the thunk table at 0x00189be0, the action engine's at 0x0015d160)
  vtable             the start of one of the game's vtables (or the word before it)
  rdata.string       a NUL-terminated string in .rdata
  rdata.constant     anything else in .rdata: constant tables, floats, the game's const objects
  data.owned         inside a global we own (the game's copy, which nothing should use any more)
  data.global        a #define of a global still at the game's address (.data/.bss)
  data.raw           another address in .data/.bss: a raw access, or an offset into a global another line defines
  image.header       the XBE's header and certificate (0x00010000-0x00011000)
  image.other        DOLBY's DSP image and $$XTIMAGE
  injected           anything in code the standalone build leaves out: inside #ifndef NIGHTFIRE_STANDALONE (the seams'
                     and the start-up's patching), or an .inc only included there (the seams' entry tables)
"""
import bisect
import json
import os
import re
import struct
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import global_coverage as gc   # noqa: E402

ENGINE = "action" if "--action" in sys.argv else "driving"
gc.use_engine(ENGINE)
SRC = os.path.join(ROOT, "src", ENGINE)
# Every section's "executable" flag is set but $$XTIMAGE's, so the code is told by name
if ENGINE == "driving":
    IMAGE_START, IMAGE_END = 0x00010000, 0x00254F40
    CODE_SECTIONS = {".text", "D3DX", "D3D", "XGRPH", "DSOUND", "XPP"}
    THUNKS = (0x00189BE0, 0x00189D70)   # the kernel import table: 99 slots and the terminating zero
else:
    IMAGE_START, IMAGE_END = 0x00010000, 0x0030B660
    CODE_SECTIONS = {".text", "D3DX", "D3D", "XGRPH", "DSOUND", "XMV", "XPP"}
    THUNKS = (0x0015D160, 0x0015D2E4)   # 96 slots and the terminating zero
VTABLES = os.path.join(ROOT, "tools", "vtables_%s.json" % ENGINE)
HEX = re.compile(r"(?<![\w.])0x([0-9a-fA-F]{5,8})(?:u|U|ul|UL)?\b")
PATCH_CALLS = re.compile(r"\b(WriteJump|WriteJmpTo|WriteCall|PatchSite|WriteMemory|WriteFunctionPointerTo|"
                         r"XBE_ENTRY\w*|XbeOriginal_\w+|WriteBytes|FillBytes)\s*\(")
FN_CAST = re.compile(r"\(\s*(?:__cdecl|__stdcall|__fastcall|__thiscall)?\s*\*\s*\)|XbeOriginal\s*<|\bmov\s+e[a-d]x\s*,")

CLASSES = ["tag.func_at", "tag.global", "tag.comment", "injected", "patch.site", "code.called", "code.pointer", "code.other",
           "constant", "import.slot", "vtable", "rdata.string", "rdata.constant", "data.owned", "data.global", "data.raw",
           "image.header", "image.other"]
WORKLIST = [c for c in CLASSES if not c.startswith("tag.") and c not in ("constant", "injected")]
GUARD = re.compile(r"\s*#\s*(?:ifndef\s+NIGHTFIRE_STANDALONE|if\s+!\s*defined\s*\(?\s*NIGHTFIRE_STANDALONE)")
STANDALONE_ONLY = re.compile(r"\s*#\s*(?:ifdef\s+NIGHTFIRE_STANDALONE|if\s+defined\s*\(?\s*NIGHTFIRE_STANDALONE)")


def injected_lines(lines):
    """Per line: whether the standalone build leaves it out (inside #ifndef NIGHTFIRE_STANDALONE, or the #else of an
    #ifdef NIGHTFIRE_STANDALONE)."""
    stack, out = [], []
    for line in lines:
        if re.match(r"\s*#\s*if", line):
            stack.append("injected" if GUARD.match(line) else "standalone" if STANDALONE_ONLY.match(line) else "")
        elif re.match(r"\s*#\s*else", line) and stack:
            stack[-1] = {"injected": "standalone", "standalone": "injected"}.get(stack[-1], "")
        elif re.match(r"\s*#\s*endif", line) and stack:
            stack.pop()
            out.append(True if "injected" in stack else False)
            continue
        out.append("injected" in stack)
    return out


def injected_includes():
    """The .inc files every #include of which is in code the standalone build leaves out."""
    seen = defaultdict(list)
    for path in source_files():
        if path.endswith(".inc"):
            continue
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        for line, injected in zip(lines, injected_lines(lines)):
            m = re.match(r'\s*#\s*include\s+"([^"]+\.inc)"', line)
            if m:
                target = os.path.normpath(os.path.join(os.path.dirname(path), m.group(1)))
                seen[target].append(injected)
    return {t for t, flags in seen.items() if all(flags)}


def is_table_row(code):
    """Whether a line is only numbers and k-constants between braces and commas: a row of a data table. (Split, not
    one regular expression, which backtracks without end on a long row.)"""
    words = [w for w in re.split(r"[\s{},\\]+", code) if w]
    return bool(words) and all(re.fullmatch(r"0x[0-9a-fA-F]+u?|\d+|k\w+", w) for w in words)


def is_flags(code):
    """Whether the number is a mask: a #define of a bare number, or a flags or mask field's initialiser"""
    return (re.fullmatch(r"\s*#\s*define\s+\w+\s+0x[0-9a-fA-F]+u?\s*(//.*)?", code) is not None
            or re.search(r"\.\w*(?:[Ff]lags|[Mm]ask)\s*=\s*0x[0-9a-fA-F]+u?\s*,", code) is not None)


def xbe_sections():
    data = open(gc.XBE, "rb").read()
    base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x11C)[0]
    headers = struct.unpack_from("<I", data, 0x120)[0] - base
    out = []
    for i in range(count):
        flags, va, vs, ra, rs, name_at = struct.unpack_from("<IIIIII", data, headers + i * 0x38)
        at = name_at - base
        out.append(dict(name=data[at:data.index(b"\0", at)].decode("ascii", "replace"), start=va, end=va + vs,
                        raw=ra, rawsize=rs))

    def read(address, size):
        for s in out:
            if s["start"] <= address < s["end"]:
                lo = address - s["start"]
                chunk = data[s["raw"] + lo: s["raw"] + min(s["rawsize"], lo + size)]
                return chunk + b"\0" * (min(size, s["end"] - address) - len(chunk))
        return b""
    return out, read


def is_string(read, address):
    raw = read(address, 256)
    end = raw.find(b"\0")
    if end < 1:
        return False
    text = raw[:end]
    return all(32 <= c < 127 or c in (9, 10, 13) for c in text) and (end >= 2 or address % 4 == 0)


def strip_comments(lines):
    """Per line: (code with comments and strings blanked, comment text, string spans), tracking /* */ across lines."""
    out = []
    in_block = False
    for line in lines:
        code, comment = [], []
        i, n = 0, len(line)
        while i < n:
            if in_block:
                j = line.find("*/", i)
                if j < 0:
                    comment.append(line[i:])
                    i = n
                else:
                    comment.append(line[i:j])
                    i = j + 2
                    in_block = False
                code.append(" ")
                continue
            c = line[i]
            if line.startswith("//", i):
                comment.append(line[i:])
                break
            if line.startswith("/*", i):
                in_block = True
                i += 2
                continue
            if c in "\"'":
                j = i + 1
                while j < n and line[j] != c:
                    j += 2 if line[j] == "\\" else 1
                comment.append(line[i:j + 1])   # strings count with the comments: not code
                code.append(" " * (j + 1 - i))
                i = j + 1
                continue
            code.append(c)
            i += 1
        out.append(("".join(code), " ".join(comment)))
    return out


def source_files():
    for folder, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != "devtools"]
        for f in sorted(files):
            if f.endswith((".c", ".cpp", ".h", ".hpp", ".inc")) and not f.startswith("autogenerated") and \
                    f not in ("Vtables.h", "Vtables.cpp"):
                yield os.path.join(folder, f)


def audit():
    sections, read = xbe_sections()
    code_ranges = [(s["start"], s["end"]) for s in sections if s["name"] in CODE_SECTIONS]
    rdata = next(s for s in sections if s["name"] == ".rdata")
    data = next(s for s in sections if s["name"] == ".data")
    funcs = {int(f["address"], 16): f["name"] for f in json.load(open(gc.FUNCTIONS))}
    entries = sorted(funcs)
    vtables = {int(a, 16) for a in json.load(open(VTABLES))["vtables"]} if os.path.exists(VTABLES) else set()
    globals_, _ = gc.read_source()
    owned = sorted((g["address"], g["address"] + g["size"], g["name"]) for g in globals_ if g["owned"])
    owned_starts = [o[0] for o in owned]
    game_defines = {(g["file"], g["line"]) for g in globals_ if not g["owned"]}

    def in_owned(a):
        i = bisect.bisect_right(owned_starts, a) - 1
        return owned[i][2] if i >= 0 and owned[i][0] <= a < owned[i][1] else None

    def function_containing(a):
        i = bisect.bisect_right(entries, a) - 1
        return funcs[entries[i]] if i >= 0 else "?"

    hits = []   # (class, file, line, address, detail, text)
    injected_files = injected_includes()
    for path in source_files():
        rel = os.path.relpath(path, ROOT).replace("\\", "/")
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        injected = [True] * len(lines) if os.path.normpath(path) in injected_files else injected_lines(lines)
        for i, (code, comment) in enumerate(strip_comments(lines), 1):
            text = lines[i - 1].strip()
            for m in HEX.finditer(comment):
                a = int(m.group(1), 16)
                if not IMAGE_START <= a < IMAGE_END:
                    continue
                if re.search(r"\b(FUNC_AT|AUTOGEN|AUTOINJECT|AUTOLTCG|VIRTUAL)\s*\(\s*0x", comment):
                    cls = "tag.func_at"
                elif re.search(r"XBE_GLOBAL\s*\(", comment):
                    cls = "tag.global"
                else:
                    cls = "tag.comment"
                hits.append((cls, rel, i, a, "", text))
            is_define = re.match(r"\s*#\s*define\s+\w+", code) is not None
            for m in HEX.finditer(code):
                a = int(m.group(1), 16)
                if not IMAGE_START <= a < IMAGE_END:
                    continue
                detail = ""
                if injected[i - 1]:
                    cls = "injected"
                elif PATCH_CALLS.search(code):
                    cls = "patch.site"
                    detail = funcs.get(a, function_containing(a) + "+")
                elif len(m.group(1)) < 8 or is_table_row(code) or is_flags(code):
                    cls = "constant"   # 0x40350, 0x10000, a row of a data table: a number that lands in the image
                elif any(lo <= a < hi for lo, hi in code_ranges):
                    if a in funcs:
                        cls = "code.called" if FN_CAST.search(code) else "code.pointer"
                        detail = funcs[a]
                    else:
                        cls = "code.other"
                        detail = "inside " + function_containing(a)
                elif THUNKS[0] <= a < THUNKS[1]:
                    cls = "import.slot"
                    detail = "slot %d" % ((a - THUNKS[0]) // 4)
                elif a in vtables or a + 4 in vtables or a + 8 in vtables:
                    cls = "vtable"
                elif in_owned(a):
                    cls = "data.owned"
                    detail = in_owned(a)
                elif rdata["start"] <= a < rdata["end"]:
                    cls = "rdata.string" if is_string(read, a) else "rdata.constant"
                elif data["start"] <= a < data["end"]:
                    cls = "data.global" if is_define and (rel, i) in game_defines else "data.raw"
                elif a < 0x11000 and re.search(r"\)\s*0x0*%x" % a, code, re.I):
                    cls = "image.header"   # read through a cast: the XBE's header and certificate
                elif a < 0x11000:
                    cls = "constant"
                else:
                    cls = "image.other"
                hits.append((cls, rel, i, a, detail, text))
    return hits


def main():
    args = sys.argv[1:]
    hits = audit()
    by_class = Counter(h[0] for h in hits)
    files = defaultdict(Counter)
    for h in hits:
        files[h[1]][h[0]] += 1

    if "--list" in args or "--file" in args:
        key = args[args.index("--list" if "--list" in args else "--file") + 1]
        for cls, path, line, a, detail, text in hits:
            if (cls.startswith(key) if "--list" in args else key in path):
                print("%-15s %s:%d  %08x %s  | %s" % (cls, path, line, a, detail, text[:110]))
        return

    if "--md" in args:
        work = sum(by_class[c] for c in WORKLIST)
        print("| Class | Literals |")
        print("|---|---:|")
        for c in CLASSES:
            print("| `%s` | %d |" % (c, by_class[c]))
        print("| **worklist (all but `tag.*` and `constant`)** | **%d** |" % work)
        print()
        cols = ["code.called", "code.pointer", "import.slot", "vtable", "rdata.string", "rdata.constant",
                "data.owned", "data.global", "data.raw", "patch.site"]
        print("| File | " + " | ".join("`%s`" % c for c in cols) + " | other |")
        print("|---|" + "---:|" * (len(cols) + 1))
        ranked = sorted(files.items(), key=lambda kv: -sum(kv[1][c] for c in WORKLIST))
        for path, counts in ranked:
            total = sum(counts[c] for c in WORKLIST)
            if total == 0:
                continue
            other = total - sum(counts[c] for c in cols)
            print("| %s | %s | %s |" % (path.replace("src/%s/" % ENGINE, ""),
                                        " | ".join(str(counts[c] or "") for c in cols), other or ""))
        return

    print("%d address literals in src/%s (outside devtools and generated files)" % (len(hits), ENGINE))
    for c in CLASSES:
        print("  %-15s %6d" % (c, by_class[c]))
    print("  %-15s %6d" % ("worklist", sum(by_class[c] for c in WORKLIST)))
    print("\nby file (worklist classes):")
    ranked = sorted(files.items(), key=lambda kv: -sum(kv[1][c] for c in WORKLIST))
    for path, counts in ranked:
        total = sum(counts[c] for c in WORKLIST)
        if total:
            print("  %5d  %-55s %s" % (total, path, ", ".join("%s %d" % (c, counts[c]) for c in WORKLIST if counts[c])))


if __name__ == "__main__":
    main()
