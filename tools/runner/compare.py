#!/usr/bin/env python3
"""Compares two runs' results (build/remote/<name> folders): the dumped frames, in order, pixel by pixel, and the
sound call traces (sndtrace.log) up to a tick. One line per pair; the first differing trace lines when they differ.

    python tools/runner/compare.py build/baselines/lbase_m1_idle build/remote/new_m1_idle [maxtick]

maxtick defaults to 9000, where tools/runner/round.sh's lockstep runs stop.
"""
import glob
import os
import re
import sys

from PIL import Image, ImageChops


def frames(folder):
    found = glob.glob(os.path.join(folder, "d3d9_dump_frame_*.bmp"))
    return sorted(found, key=lambda f: int(re.findall(r"_(\d+)\.bmp$", f)[0]))


def compare_frames(base, new):
    a, b = frames(base), frames(new)
    differ = 0
    for fa, fb in zip(a, b):
        d = ImageChops.difference(Image.open(fa).convert("RGB"), Image.open(fb).convert("RGB")).convert("L")
        differ += d.width * d.height - d.histogram()[0]
    mismatch = "" if len(a) == len(b) else " (COUNT MISMATCH)"
    return "frames %d vs %d, %d px differ%s" % (len(a), len(b), differ, mismatch), differ == 0 and not mismatch


def trace(path, maxtick):
    lines = []
    for line in open(path, encoding="utf-8", errors="replace"):
        tick = line.split(" ", 1)[0]
        if tick.isdigit() and int(tick) > maxtick:
            break
        lines.append(line.rstrip("\n"))
    return lines


def compare_traces(base, new, maxtick):
    pa, pb = os.path.join(base, "sndtrace.log"), os.path.join(new, "sndtrace.log")
    if not os.path.exists(pa) or not os.path.exists(pb):
        return "no sound trace", True, []
    a, b = trace(pa, maxtick), trace(pb, maxtick)
    for i, (x, y) in enumerate(zip(a, b)):
        if x != y:
            context = ["  base " + line[:170] for line in a[max(0, i - 3):i + 4]]
            context += ["  new  " + line[:170] for line in b[max(0, i - 3):i + 4]]
            return "sound %d vs %d lines to tick %d, FIRST DIFFERENCE at line %d" % (len(a), len(b), maxtick, i + 1), False, context
    same = len(a) == len(b)
    return "sound %d vs %d lines to tick %d, %s" % (len(a), len(b), maxtick, "identical" if same else "LENGTHS DIFFER"), same, []


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 2
    base, new = sys.argv[1], sys.argv[2]
    maxtick = int(sys.argv[3]) if len(sys.argv) > 3 else 9000
    frame_text, frames_same = compare_frames(base, new)
    trace_text, traces_same, context = compare_traces(base, new, maxtick)
    print("%s: %s; %s" % (os.path.basename(os.path.normpath(new)), frame_text, trace_text))
    for line in context:
        print(line)
    return 0 if frames_same and traces_same else 1


if __name__ == "__main__":
    sys.exit(main())
