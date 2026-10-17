"""Annotate a crash report with the function containing each address.

    python tools/symbolise.py crash.log                 # driving engine symbols (tools/functions_driving.json)
    python tools/symbolise.py --action crash.log        # action engine symbols (tools/functions_action.json)
    python tools/symbolise.py --map Release/driving_standalone.map crash.log
                                                        # our own code, from a linker map (MSVC /MAP or ld -Map)

Every address in the input that falls inside a known XBE function is followed by
"<name>+<offset>"; with --map (repeatable), so is every address inside that map's module, rebased by the
"<module> loaded at 0x..." line of the report (the standalone executable's reports start with one; the loader prints
the injection DLL's to stdout, so for drivinginject.map pass the drive log, or --base). A report from the standalone
executable ("[standalone]" lines) has no XBE, so the XBE symbols are left out for it. The rest of each line is left
alone, so the output reads like the input.
"""
import bisect
import json
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def load(path):
    functions = json.load(open(path))
    starts = []
    names = []
    for f in sorted(functions, key=lambda f: int(f["address"], 16)):
        starts.append(int(f["address"], 16))
        names.append(f["name"])
    return starts, names


def name_for(starts, names, address):
    i = bisect.bisect_right(starts, address) - 1
    if i < 0:
        return None
    # No sizes in the export, so the next function's start bounds this one; a gap of more than 64 KB is
    # data or padding, not a function that long.
    if i + 1 < len(starts) and address >= starts[i + 1]:
        return None
    if address - starts[i] > 0x10000:
        return None
    return "%s+0x%x" % (names[i], address - starts[i])


def undecorate(name):
    """A readable form of an MSVC decorated name: ?Run@Scheduler@@QAEXXZ -> Scheduler::Run (no types)."""
    if not name.startswith("?"):
        return name
    special = {"?0": None, "?1": "~"}   # constructor, destructor: the class's name
    body = name[1:]
    kind = None
    if body[:2] in special:
        kind = body[:2]
        body = body[2:]
    parts = ["`anonymous namespace'" if p.startswith("?A0x") else p for p in body.split("@@", 1)[0].split("@")]
    if not parts or any(p == "" or p.startswith("?") for p in parts):
        return name
    if kind is not None:
        parts.insert(0, (special[kind] or "") + parts[0])
    return "::".join(reversed(parts))


def load_map(path):
    """(module name, preferred base, sorted addresses, names) of the functions in a linker map."""
    preferred, symbols = None, []
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.match(r"\s*Preferred load address is ([0-9a-fA-F]+)", line)
        if m:
            preferred = int(m.group(1), 16)
            continue
        m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{8,16})\s+f\s", line)   # MSVC
        if m:
            symbols.append((int(m.group(2), 16), undecorate(m.group(1))))
            continue
        m = re.match(r"\s+0x([0-9a-fA-F]{8,16})\s+([A-Za-z_?.$][^\s=]*)\s*$", line)                 # GNU ld
        if m:
            symbols.append((int(m.group(1), 16), m.group(2)))
    symbols.sort()
    if preferred is None:
        preferred = symbols[0][0] & ~0xffff if symbols else 0
    module = os.path.splitext(os.path.basename(path))[0].lower()
    return module, preferred, [a for a, _ in symbols], [n for _, n in symbols]


def main(argv):
    symbols = os.path.join(HERE, "functions_driving.json")
    files, maps, bases = [], [], {}
    args = iter(argv)
    for arg in args:
        if arg == "--action":
            symbols = os.path.join(HERE, "functions_action.json")
        elif arg == "--driving":
            symbols = os.path.join(HERE, "functions_driving.json")
        elif arg == "--map":
            maps.append(load_map(next(args)))
        elif arg == "--base":   # --base <module>=0x<address>, where the report does not say
            module, _, value = next(args).partition("=")
            bases[os.path.splitext(module)[0].lower()] = int(value, 16)
        else:
            files.append(arg)
    starts, names = load(symbols)

    lines = list(open(files[0], errors="replace") if files else sys.stdin)
    standalone = any("[standalone]" in line for line in lines)
    for line in lines:
        m = re.search(r"(\S+)\s+loaded at 0x([0-9a-fA-F]{8,16})", line)
        if m:
            bases.setdefault(os.path.splitext(os.path.basename(m.group(1)))[0].lower(), int(m.group(2), 16))

    # each map's code where the report says it was loaded: (start, end, rebase, addresses, names)
    modules = []
    for module, preferred, addresses, symbol_names in maps:
        if not addresses:
            continue
        shift = bases.get(module, preferred) - preferred
        modules.append((addresses[0] + shift, addresses[-1] + shift + 0x10000, shift, addresses, symbol_names))

    def annotate_ours(match):
        address = int(match.group(0), 16)
        for start, end, shift, addresses, symbol_names in modules:
            if start <= address < end:
                i = bisect.bisect_right(addresses, address - shift) - 1
                if i >= 0:
                    return "%s %s+0x%x" % (match.group(0), symbol_names[i], address - shift - addresses[i])
        if standalone:
            return match.group(0)
        if not 0x00010000 <= address < 0x00260000:   # the XBE's image
            return match.group(0)
        name = name_for(starts, names, address)
        return match.group(0) if name is None else "%s %s" % (match.group(0), name)

    pattern = re.compile(r"0x(?:[0-9a-fA-F]{16}|[0-9a-fA-F]{8})\b")   # a 32-bit or a 64-bit report's
    for line in lines:
        sys.stdout.write(pattern.sub(annotate_ours, line))


if __name__ == "__main__":
    main(sys.argv[1:])
