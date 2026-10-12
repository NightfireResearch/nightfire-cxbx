#!/usr/bin/env python3
"""Writes src/driving/devtools/OwnedGlobals.inc: every driving-engine global we own, as {game address, size, &ours}.

    python tools/owned_globals.py            write the table (the pre-build runs this, after tools/preprocess.py)
    python tools/owned_globals.py --check    fail if it is out of date
    python tools/owned_globals.py --list     also print what was left out, and why

An owned global is an XBE_GLOBAL tag directly above a definition of our own (tools/global_coverage.py reads them;
a tag above a #define at the game's address is still the game's). Once a global is ours, nothing live updates the
game's copy at the tagged address, but an original run by a shadow test (its entry bytes swapped back in) still
reads and writes that copy. devtools/OwnedGlobals.cpp uses the table to lend our values to the game's copies as an
original's window opens, to snapshot and restore both copies, and to translate an address in one copy to the other.

The .inc has five parts: the headers that declare the globals; a static_assert of each one's size against its tag;
the table; and the touchers - for each original function, the globals whose game copy it reaches: it names an
address inside one (tools/xrefs_driving.json), or calls a function of the game's that does and that we have not
replaced (so it runs as the original wherever it is called from). A window lends only what its functions reach.

Kept for translation but never lent: const data and anything in .rdata (the same bytes in both copies), SKIP, and
SHARED - allocator state the originals use over the same memory as the port, so they are pointed at ours instead:
the fifth part lists every instruction naming one, which OwnedGlobals.cpp rewrites while a window is open.
Left out (--list names each):
  - statics inside a function: they cannot be named from outside it;
  - file statics and anonymous-namespace globals not exported from a header (an extern in the owning file's header
    makes them reachable);
  - class members not declared in a header we can include.
"""
import bisect
import json
import os
import re
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import global_coverage as gc   # noqa: E402

gc.use_engine("driving")
SRC = os.path.join(ROOT, "src", "driving")
OUT = os.path.join(SRC, "devtools", "OwnedGlobals.inc")
DATA = 0x001b3da0   # .data starts here; below it is .rdata, which holds nothing mutable

# Owned globals never lent, by name: why.
SKIP = {
    "gFeedbackThreadStack": "the feedback thread's stack, in use while it runs",
    "SoundMutex": "the sound library's live critical section: copying it would corrupt the lock",
    # The C runtime's state: the shadow tests (CRuntimeShadowA/B) set both copies themselves, per side
    "OnExitBegin": "the C runtime's exit list: CRuntimeShadowA sets the game's copy for the original",
    "OnExitEnd": "the C runtime's exit list: CRuntimeShadowA sets the game's copy for the original",
    "InitLocksCount": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "FacetList": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "GlobalLocale": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "ClassicImpl": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "ClassicLocale": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "GlobalTidyRegistered": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "StandardStreams": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "AtExitCount": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
    "AtExitFunctions": "the C++ library's state: CRuntimeShadowB sets the game's copy for the original",
}
# Owned globals the originals must share with the port, by name: allocator state over memory both sides use. While
# a window is open, every original instruction naming one is pointed at ours instead (the redirects).
SHARED = {
    "Pool": "shared: EAGLAnim's object pool",
    "FactoryBlock": "shared: EAGLAnim's factory block",
    "ScratchBuffers": "shared: EAGLAnim's scratch buffers",
}


def strip_code(text):
    """The text with comments, strings and character literals blanked (newlines kept), for brace matching."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", text[i:j]))
            i = j
        elif c in "\"'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            j = min(j + 1, n)
            out.append(c + " " * (j - i - 2) + c if j - i >= 2 else " " * (j - i))
            i = j
        elif c == "#":   # a preprocessor line, with its continuations
            j = i
            while True:
                k = text.find("\n", j)
                if k < 0:
                    k = n
                    break
                if text[k - 1] != "\\":
                    break
                j = k + 1
            out.append(re.sub(r"[^\n]", " ", text[i:k]))
            i = k
        else:
            out.append(c)
            i += 1
    return "".join(out)


def scopes_by_line(text):
    """For each line (1-based), the stack of scopes open at its start: ('namespace', name or ''), ('class', name),
    ('function', ''), ('extern', ''), ('init', '') or ('other', '')."""
    code = strip_code(text)
    stack, result, line = [], {1: []}, 1
    last = 0   # where the current statement began
    for i, c in enumerate(code):
        if c == "\n":
            line += 1
            result[line] = list(stack)
        elif c == "{":
            head = " ".join(code[last:i].split())
            if re.search(r"\bnamespace\s*(\w+(::\w+)*)?\s*$", head):
                name = re.search(r"\bnamespace\s*(\w+(::\w+)*)?\s*$", head).group(1) or ""
                stack.append(("namespace", name))
            elif re.search(r'\bextern\s*"', head) or re.search(r'extern\s*""\s*$', head):
                stack.append(("extern", ""))
            elif re.search(r"\b(class|struct|union)\s+(?:alignas\s*\([^)]*\)\s*)?(\w+)[^;()]*$", head) and \
                    not any(s[0] == "function" for s in stack):
                stack.append(("class", re.search(r"\b(class|struct|union)\s+(?:alignas\s*\([^)]*\)\s*)?(\w+)",
                                                 head).group(2)))
            elif re.search(r"\benum\b", head):
                stack.append(("other", ""))
            elif re.search(r"=\s*$", head) or (stack and stack[-1][0] == "init"):
                stack.append(("init", ""))
            elif any(s[0] == "function" for s in stack):
                stack.append(("other", ""))
            elif ")" in head:
                stack.append(("function", ""))
            else:
                stack.append(("other", ""))
            last = i + 1
        elif c == "}":
            if stack:
                stack.pop()
            last = i + 1
        elif c == ";":
            last = i + 1
    return result


def declaration_info(line):
    """(name, qualified-by-class prefix, static, const) of a one-line definition."""
    code = re.sub(r'"(\\.|[^"\\])*"', '""', line).split("//")[0]
    code = re.sub(r"alignas\s*\([^)]*\)", "", code)
    code = code.split("=")[0].split("{")[0].rstrip().rstrip(";")
    code = re.sub(r"(\[[^\]]*\])+\s*$", "", code).rstrip()
    m = re.search(r"((?:\w+::)*)(\w+)\s*$", code)
    if not m:
        m = re.search(r"\(\s*\*\s*((?:\w+::)*)(\w+)\s*\)", code)   # a function pointer
        if not m:
            return None
        before = code[:m.start()]
    else:
        before = code[:m.start()]
    prefix, name = m.group(1).rstrip(":"), m.group(2)
    static = re.search(r"\bstatic\b", before) is not None
    after_star = before.rsplit("*", 1)[1] if "*" in before else before
    const = re.search(r"\b(const|constexpr)\b", after_star) is not None
    return name, prefix, static, const


def header_files():
    for folder, dirs, files in os.walk(SRC):
        dirs[:] = [d for d in dirs if d != "devtools"]
        for f in files:
            if f.endswith((".h", ".hpp")) and not f.startswith("autogenerated"):
                yield os.path.join(folder, f)


def find_declaration(headers, index, name, namespaces, class_name):
    """The header declaring the global in the given scope, or None."""
    want_ns = "::".join(n for n in namespaces if n)
    for path, i in index.get(name, ()):
        lines, scopes = headers[path]
        code = lines[i - 1].split("//")[0]
        scope = scopes.get(i, [])
        ns = "::".join(s[1] for s in scope if s[0] == "namespace")
        if class_name:
            cls = [s[1] for s in scope if s[0] == "class"]
            if cls and cls[-1] == class_name and re.search(r"\bstatic\b", code) and ns == want_ns and \
                    re.search(r"\b%s\s*(\[[^\]]*\])*\s*;" % re.escape(name), code):
                return path
        elif re.search(r"\bextern\b", code) and not any(s[0] in ("class", "function") for s in scope) and \
                ns == want_ns and \
                re.search(r"\b%s\s*(\[[^\]]*\])*\s*\)?\s*(\([^)]*\))?\s*[;,]" % re.escape(name), code):
            return path
    return None


def replaced_functions():
    """The original entry points our jumps cover (as tools/global_coverage.py's CallGraph finds them)."""
    replaced = set()
    for path in gc.PATCH_FILES:
        if os.path.exists(path):
            for m in re.finditer(r"WriteJmpTo\(\s*(0x[0-9a-fA-F]+)", open(path, encoding="utf-8", errors="replace").read()):
                replaced.add(int(m.group(1), 16))
    for path in gc.source_files():
        for m in re.finditer(r"WriteJump\(\s*(0x[0-9a-fA-F]+)", open(path, encoding="utf-8", errors="replace").read()):
            replaced.add(int(m.group(1), 16))
    for path in gc.SEAM_TABLES:
        for m in re.finditer(r"^XBE_ENTRY\w*\(\s*\w+\s*,\s*(0x[0-9a-fA-F]+)", open(path, encoding="utf-8").read(), re.M):
            replaced.add(int(m.group(1), 16))
    return replaced


def touchers(entries):
    """(function entry, table index) for each original function that reaches an owned global's game copy: it names
    an address inside it (tools/xrefs_driving.json), or calls a function of the game's that does (one we have not
    replaced, so it runs as the original wherever it is called from; a replaced callee runs ours unless its own
    entry is swapped back too, and then it is a toucher in its own right). Calls through pointers are not followed."""
    xrefs = json.load(open(gc.XREFS))
    replaced = replaced_functions()
    ranges = sorted((g["address"], g["address"] + g["size"], i) for i, (g, _, _, keep) in enumerate(entries) if not keep)
    starts = [r[0] for r in ranges]
    direct, callees = defaultdict(set), defaultdict(set)
    for to, _, fn, kind in xrefs:
        if not fn:
            continue
        to, fn = int(to, 16), int(fn, 16)
        if kind == "C":
            callees[fn].add(to)
            continue
        at = bisect.bisect_right(starts, to) - 1
        if at >= 0 and to < ranges[at][1]:
            direct[fn].add(ranges[at][2])
    reach = {}

    def reached(f, stack):
        if f in reach:
            return reach[f]
        if f in stack:
            return direct[f]
        stack.add(f)
        found = set(direct[f])
        for c in callees[f]:
            if c not in replaced:
                found |= reached(c, stack)
        stack.discard(f)
        reach[f] = found
        return found

    sys.setrecursionlimit(20000)
    pairs = set()
    for f in set(direct) | set(callees):
        for i in reached(f, set()):
            pairs.add((f, i))
    return sorted(pairs)


def redirects(entries):
    """(instruction, table index) for each original instruction that names an address inside a SHARED global."""
    out = set()
    ranges = sorted((g["address"], g["address"] + g["size"], i) for i, (g, _, _, keep) in enumerate(entries)
                    if g["name"] in SHARED)
    starts = [r[0] for r in ranges]
    for to, frm, fn, kind in json.load(open(gc.XREFS)):
        if not fn or kind == "C":
            continue
        to = int(to, 16)
        at = bisect.bisect_right(starts, to) - 1
        if at >= 0 and to < ranges[at][1]:
            out.add((int(frm, 16), ranges[at][2]))
    return sorted(out)


def main():
    owned = [g for g in gc.read_source()[0] if g["owned"]]
    headers = {}
    for path in header_files():
        text = open(path, encoding="utf-8", errors="replace").read()
        headers[path] = (text.splitlines(), scopes_by_line(text))
    index = {}   # each identifier's header lines
    for path, (lines, _) in headers.items():
        for i, line in enumerate(lines, 1):
            for word in set(re.findall(r"\w+", line.split("//")[0])):
                index.setdefault(word, []).append((path, i))
    texts = {}
    entries, left_out = [], []
    for g in sorted(owned, key=lambda g: g["address"]):
        path = os.path.join(ROOT, g["file"])
        if path not in texts:
            text = open(path, encoding="utf-8", errors="replace").read()
            texts[path] = (text.splitlines(), scopes_by_line(text))
        lines, scopes = texts[path]
        line = lines[g["line"] - 1]
        where = "%s:%d" % (g["file"], g["line"])
        info = declaration_info(line)
        if not info:
            left_out.append((g, where, "definition not understood"))
            continue
        name, prefix, static, const = info
        scope = scopes.get(g["line"], [])
        # Kept for translating addresses between the copies, but never lent or snapshotted
        keep = SKIP.get(g["name"]) or SHARED.get(g["name"]) or ("in .rdata" if g["address"] < DATA else None) or ("const" if const else None)
        if any(s[0] in ("function", "other", "init") for s in scope):
            left_out.append((g, where, "a static inside a function"))
            continue
        namespaces = [s[1] for s in scope if s[0] == "namespace"]
        classes = [s[1] for s in scope if s[0] == "class"]
        if "" in namespaces:
            left_out.append((g, where, "in an anonymous namespace: export it from the header to lend it"))
            continue
        if classes:   # an inline static member, defined in its class
            class_path = classes
        else:
            class_path = [p for p in prefix.split("::") if p] if prefix else []
        if static and not classes:
            left_out.append((g, where, "file static: export it from the header to lend it"))
            continue
        qualified = "::".join([n for n in namespaces] + class_path + [name])
        if path.endswith((".h", ".hpp")) and not class_path:
            header = path
        elif class_path:
            header = (find_declaration(headers, index, name, namespaces + class_path[:-1], class_path[-1]) or
                      find_declaration(headers, index, name, namespaces + class_path, None))   # a namespace, not a class
            if header is None and path.endswith((".h", ".hpp")):
                header = path
        else:
            header = find_declaration(headers, index, name, namespaces, None)
        if header is None:
            left_out.append((g, where, "not declared in a header: export it to lend it"))
            continue
        entries.append((g, qualified, header, keep))

    includes = sorted({os.path.relpath(h, os.path.dirname(OUT)).replace("\\", "/") for _, _, h, _ in entries})
    out = ["// This file is autogenerated by tools/owned_globals.py. Do not modify.",
           "// The driving engine's owned globals: the game's copy (address, size) and ours. Included by",
           "// devtools/OwnedGlobals.cpp; %d globals (%d lent), %d left out (python tools/owned_globals.py --list)." %
           (len(entries), sum(1 for e in entries if not e[3]), len(left_out)),
           "#ifdef OWNED_GLOBALS_INCLUDES"]
    out += ['#include "%s"' % inc for inc in includes]
    out += ["#elif defined(OWNED_GLOBALS_CHECKS)"]
    out += ["static_assert(sizeof(%s) == 0x%x);" % (q, g["size"]) for g, q, _, _ in entries]
    out += ["#elif defined(OWNED_GLOBALS_TOUCHERS)"]
    out += ["    { 0x%08x, %d }," % pair for pair in touchers(entries)]
    out += ["#elif defined(OWNED_GLOBALS_REDIRECTS)"]
    out += ["    { 0x%08x, %d }," % pair for pair in redirects(entries)]
    out += ["#else"]
    for g, qualified, header, keep in entries:
        out.append("    { 0x%08x, 0x%x, %s, (void *)&%s },   // %s%s" % (
            g["address"], g["size"], "false" if keep else "true", qualified, g["file"][len("src/driving/"):],
            ", not lent: " + keep if keep else ""))
    out += ["#endif", ""]
    text = "\n".join(out)

    if "--list" in sys.argv:
        for g, where, why in left_out:
            print("left out: %-36s 0x%08x %-48s %s" % (g["name"], g["address"], where, why))
    old = open(OUT, encoding="utf-8").read() if os.path.exists(OUT) else None
    if "--check" in sys.argv:
        if old != text:
            sys.exit("%s is out of date: run python tools/owned_globals.py" % os.path.relpath(OUT, ROOT))
        return
    if old != text:
        with open(OUT, "w", encoding="utf-8", newline="\n") as f:
            f.write(text)
    print("owned_globals: %d globals in the table (%d lent), %d left out" %
          (len(entries), sum(1 for e in entries if not e[3]), len(left_out)))


if __name__ == "__main__":
    main()
