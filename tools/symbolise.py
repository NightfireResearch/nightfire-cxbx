"""Annotate a loader crash report with the Ghidra function containing each XBE address.

    python tools/symbolise.py crash.log                 # driving engine symbols (tools/functions_driving.json)
    python tools/symbolise.py --action crash.log        # action engine symbols (tools/functions_action.json)

Every 0x000xxxxx-looking number in the input that falls inside a known function is followed by
"<name>+<offset>". The rest of the line is left alone, so the output reads like the input.
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


def main(argv):
    symbols = os.path.join(HERE, "functions_driving.json")
    files = []
    for arg in argv:
        if arg == "--action":
            symbols = os.path.join(HERE, "functions_action.json")
        elif arg == "--driving":
            symbols = os.path.join(HERE, "functions_driving.json")
        else:
            files.append(arg)
    starts, names = load(symbols)

    pattern = re.compile(r"0x000([0-9a-fA-F]{5})\b")

    def annotate(match):
        address = int(match.group(0), 16)
        name = name_for(starts, names, address)
        return match.group(0) if name is None else "%s %s" % (match.group(0), name)

    source = open(files[0], errors="replace") if files else sys.stdin
    for line in source:
        sys.stdout.write(pattern.sub(annotate, line))


if __name__ == "__main__":
    main(sys.argv[1:])
