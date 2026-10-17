#!/usr/bin/env python3
"""Compares two runs' results (build/remote/<name> folders): the dumped frames up to a tick, pixel by pixel, and the
sound call traces (sndtrace.log) up to the same tick. One line per pair; the first differing trace lines when they
differ.

    python tools/runner/compare.py build/baselines/lbase_m1_idle build/remote/new_m1_idle [maxtick]

maxtick defaults to 9000, where tools/runner/round.sh's lockstep runs stop. A shorter run (round.sh's quick rounds)
compares as a prefix of a longer baseline: frames are paired by the tick drive.log says each was dumped at, and
only those at or before maxtick count. Without a drive.log, frames pair in order.

    python tools/runner/compare.py --sound build/baselines/lloc_m1_idle build/remote/lwsl_m1_idle

--sound compares the sound traces only: for a pair whose frames cannot match - a Linux build against a Windows
baseline (another renderer) made on the same machine - but whose simulation should.
"""
import glob
import os
import re
import sys

from PIL import Image, ImageChops


def frames(folder):
    found = glob.glob(os.path.join(folder, "d3d9_dump_frame_*.bmp"))
    return sorted(found, key=lambda f: int(re.findall(r"_(\d+)\.bmp$", f)[0]))


def dump_ticks(folder):
    """{frame number: tick} from the run's drive.log ("dumping frame N (...), T ticks after the car appeared")."""
    path = os.path.join(folder, "drive.log")
    if not os.path.exists(path):
        return None
    ticks = {}
    for line in open(path, encoding="utf-8", errors="replace"):
        m = re.search(r"dumping frame (\d+) .*?, (\d+) ticks after", line)
        if m:
            ticks[int(m.group(1))] = int(m.group(2))
    return ticks


def frames_to(folder, maxtick):
    """The run's frames dumped at or before maxtick, as {key: file}: keyed by tick, or by order without a drive.log."""
    found = frames(folder)
    ticks = dump_ticks(folder)
    if not ticks:
        return {i: f for i, f in enumerate(found)}
    by_tick = {}
    for f in found:
        tick = ticks.get(int(re.findall(r"_(\d+)\.bmp$", f)[0]))
        if tick is not None and tick <= maxtick:
            by_tick[tick] = f
    return by_tick


def compare_frames(base, new, maxtick):
    a, b = frames_to(base, maxtick), frames_to(new, maxtick)
    differ = 0
    for key in sorted(set(a) & set(b)):
        d = ImageChops.difference(Image.open(a[key]).convert("RGB"), Image.open(b[key]).convert("RGB")).convert("L")
        differ += d.width * d.height - d.histogram()[0]
    mismatch = "" if set(a) == set(b) else " (COUNT MISMATCH)"
    return "frames %d vs %d, %d px differ%s" % (len(a), len(b), differ, mismatch), differ == 0 and not mismatch


# The stream's memory overhead is the size of the library's records, which hold pointers: it differs between 32-
# and 64-bit builds, and nothing else in the trace depends on it (docs/driving/64-bit.md)
OVERHEAD = re.compile(r"(SNDSTRM_overhead\([^)]*\) = |SNDSTRM_create\(.*, mem, )0x[0-9a-f]+")


def trace(path, maxtick):
    lines = []
    for line in open(path, encoding="utf-8", errors="replace"):
        tick = line.split(" ", 1)[0]
        if tick.isdigit() and int(tick) > maxtick:
            break
        lines.append(OVERHEAD.sub(r"\1<overhead>", line.rstrip("\n")))
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
    args = sys.argv[1:]
    sound_only = "--sound" in args
    args = [a for a in args if a != "--sound"]
    if len(args) < 2:
        print(__doc__)
        return 2
    base, new = args[0], args[1]
    maxtick = int(args[2]) if len(args) > 2 else 9000
    frame_text, frames_same = ("frames not compared", True) if sound_only else compare_frames(base, new, maxtick)
    trace_text, traces_same, context = compare_traces(base, new, maxtick)
    print("%s: %s; %s" % (os.path.basename(os.path.normpath(new)), frame_text, trace_text))
    for line in context:
        print(line)
    return 0 if frames_same and traces_same else 1


if __name__ == "__main__":
    sys.exit(main())
