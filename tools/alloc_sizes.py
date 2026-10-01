#!/usr/bin/env python3
"""Check Ghidra's structure sizes against what the game actually allocates.

The overlay classes take their fields from Ghidra's structures (tools/structs_<side>.json), and the layout
checks prove our classes match Ghidra - not that Ghidra matches the game. This supplies a check from the
binary itself. EA's allocators take a size, and most take a name:

    UMemory::FastAlloc(0x4c, "RRenderHUDView")            0x00114750  (size, name)
    UMemory::Alloc(size, flags, name)                     0x00114470
    EAGL's allocator, through the pointer at 0x001caf68   (size, name) - 0x0007d040, installed by RRenderer
    ABaseSound::operator new(size, name)                  0x0011c910
    __builtin_new(size), Event::operator new(size), malloc(size)

and the object is usually handed straight to its constructor. So each allocation site gives a size, a name
string, and the class whose constructor runs on the result - and the three can be compared with Ghidra's
structure of that name. A derived class whose constructor was inlined shows its base class's constructor
after the allocation, so the constructor's class is only a lower bound; the name string, when there is one,
names the object actually allocated.

    python tools/alloc_sizes.py                  # report on the driving engine
    python tools/alloc_sizes.py --sites          # every allocation site, too
    python tools/alloc_sizes.py --json           # tools/alloc_sizes_driving.json, for tools/preprocess.py

Written for the driving engine (the allocators above are its addresses). See
docs/driving-injection-framework.md, "Checking Ghidra's structures against the binary".
"""

import json
import os
import re
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from abi_facts import Image, ROOT

from capstone import x86

# address -> (name, index of the size argument, index of the name argument or None, argument count)
ALLOCATORS = {
    0x00114750: ("UMemory::FastAlloc", 0, 1, 2),
    0x00114470: ("UMemory::Alloc", 0, 2, 3),
    0x0007d040: ("EAGL_allocator", 0, 1, 2),
    0x0011c910: ("ABaseSound::operator new", 0, 1, 2),
    0x001146a0: ("__builtin_new", 0, None, 1),
    0x0005a5a0: ("Event::operator new", 0, None, 1),
    0x001340e3: ("malloc", 0, None, 1),
}
EAGL_MALLOC_POINTER = 0x001caf68
EAGL_MALLOC = ("EAGLMalloc (via 0x001caf68)", 0, 1, 2)
LOOKBACK = 24      # instructions searched for a call's pushes
LOOKAHEAD = 20     # instructions searched for the constructor after it


def read_string(image, address):
    raw = b""
    for offset in range(0, 128, 16):
        from xbe_entry_points import read_at
        chunk = read_at(image.data, image.sections, address + offset, 16)
        if not chunk:
            return None
        raw += chunk
        if b"\0" in chunk:
            break
    text = raw.split(b"\0")[0]
    if not text or any(c < 0x20 or c > 0x7e for c in text):
        return None
    return text.decode("ascii")


def linear(image, start, end):
    out, address = [], start
    while address < end:
        insn = image.insn(address)
        if insn is None:
            break
        out.append(insn)
        address += insn.size
    return out


def register_value(insns, index, reg):
    # The constant most recently moved into `reg` before insns[index], if nothing else wrote it in between.
    for j in range(index - 1, max(-1, index - LOOKBACK), -1):
        insn = insns[j]
        ops = insn.operands
        if insn.mnemonic in ("call", "jmp", "ret") or insn.group(x86.X86_GRP_JUMP):
            return None
        if ops and ops[0].type == x86.X86_OP_REG and ops[0].reg == reg:
            if insn.mnemonic == "mov" and len(ops) == 2 and ops[1].type == x86.X86_OP_IMM:
                return ops[1].imm & 0xffffffff
            return None
    return None


def pushed_arguments(insns, index, count):
    # The arguments of the call at insns[index], first argument first: the last `count` pushes before it,
    # stopping at anything that would break a straight line of pushes (another call, a branch).
    args = []
    for j in range(index - 1, max(-1, index - LOOKBACK), -1):
        insn = insns[j]
        if insn.mnemonic in ("call", "ret") or insn.group(x86.X86_GRP_JUMP):
            break
        if insn.mnemonic == "push" and insn.operands:
            op = insn.operands[0]
            if op.type == x86.X86_OP_IMM:
                args.append(op.imm & 0xffffffff)
            elif op.type == x86.X86_OP_REG:
                args.append(register_value(insns, j, op.reg))
            else:
                args.append(None)
            if len(args) == count:
                break
    return args if len(args) == count else None


def constructor_after(insns, index, functions):
    # The first call after the allocation to a function named C::C - the constructor run on the new object.
    for insn in insns[index + 1:index + 1 + LOOKAHEAD]:
        if insn.mnemonic == "call" and insn.operands and insn.operands[0].type == x86.X86_OP_IMM:
            name = functions.get(insn.operands[0].imm)
            if name and "::" in name:
                cls, method = name.rsplit("::", 1)
                if method == cls.split("::")[-1]:
                    return cls
        if insn.mnemonic == "ret":
            break
    return None


def class_from_label(label):
    # "RRenderHUDView" -> itself; "EAGL::VertexBuffer new" -> "VertexBuffer"; anything with spaces or
    # punctuation beyond that is a description, not a class name.
    label = re.sub(r"\s+new$", "", label.strip())
    label = label.split("::")[-1]
    return label if re.match(r"^[A-Za-z_]\w*$", label) else None


def collect(side="driving"):
    image = Image(os.path.join(ROOT, "disc", "Driving.xbe"))
    symbols = json.load(open(os.path.join(ROOT, "tools", f"functions_{side}.json"), encoding="utf-8"))
    functions = {int(s["address"], 16): s["name"] for s in symbols}
    starts = sorted(functions)
    sites = []
    for n, start in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else start + 0x1000
        if start in ALLOCATORS:
            continue          # the allocators' own internals
        insns = linear(image, start, end)
        for i, insn in enumerate(insns):
            if insn.mnemonic != "call" or not insn.operands:
                continue
            op = insn.operands[0]
            allocator = None
            if op.type == x86.X86_OP_IMM:
                allocator = ALLOCATORS.get(op.imm)
            elif op.type == x86.X86_OP_MEM and op.mem.base == 0 and op.mem.index == 0 \
                    and (op.mem.disp & 0xffffffff) == EAGL_MALLOC_POINTER:
                allocator = EAGL_MALLOC
            if allocator is None:
                continue
            name, size_at, label_at, count = allocator
            args = pushed_arguments(insns, i, count)
            size = args[size_at] if args else None
            label = read_string(image, args[label_at]) if args and label_at is not None and args[label_at] else None
            sites.append({
                "site": insn.address, "in": functions[start], "allocator": name, "size": size,
                "label": label, "constructor": constructor_after(insns, i, functions),
            })
    return sites


def report(sites, show_sites=False, side="driving"):
    structs = {s["name"]: s for s in json.load(open(os.path.join(ROOT, "tools", f"structs_{side}.json")))}
    known = [s for s in sites if s["size"] is not None]
    print(f"{len(sites)} allocation sites, {len(known)} with a constant size, "
          f"{sum(1 for s in known if s['label'])} with a name, "
          f"{sum(1 for s in known if s['constructor'])} followed by a constructor")

    # Sizes observed per class: exact when the name string names it, a lower bound when only a constructor
    # (possibly a base class's) does.
    exact, lower = defaultdict(set), defaultdict(set)
    for s in known:
        by_label = class_from_label(s["label"]) if s["label"] else None
        if by_label:
            exact[by_label].add(s["size"])
        if s["constructor"] and s["constructor"] != by_label:
            lower[s["constructor"].split("::")[-1]].add(s["size"])

    rows = []
    for cls in sorted(set(exact) | set(lower)):
        ghidra = structs.get(cls)
        sizes = sorted(exact.get(cls, set()))
        bounds = sorted(lower.get(cls, set()))
        if ghidra is None:
            verdict = "no Ghidra structure" + ("; several sizes under this name" if len(sizes) > 1 else "")
        elif sizes and len(sizes) == 1 and ghidra["size"] == sizes[0]:
            verdict = "matches"
        elif sizes and len(sizes) == 1 and ghidra["size"] < sizes[0]:
            verdict = f"Ghidra's is {sizes[0] - ghidra['size']} bytes short"
        elif sizes and len(sizes) == 1:
            verdict = f"Ghidra's is {ghidra['size'] - sizes[0]} bytes too long"
        elif sizes:
            verdict = "several sizes allocated under this name"
        elif bounds and ghidra["size"] <= min(bounds):
            verdict = "consistent (constructor only)" if ghidra["size"] in bounds else "Ghidra's may be short (constructor only)"
        elif bounds:
            verdict = f"Ghidra's is longer than an allocation constructed as it ({min(bounds)} bytes)"
        rows.append((cls, sizes, bounds, ghidra["size"] if ghidra else None, verdict))

    def show(title, predicate):
        chosen = [r for r in rows if predicate(r[4])]
        print(f"\n{title}: {len(chosen)}")
        for cls, sizes, bounds, g, verdict in chosen:
            allocated = ", ".join(f"0x{x:x}" for x in sizes) or "-"
            constructed = ", ".join(f"0x{x:x}" for x in bounds) or "-"
            ghidra = f"0x{g:x}" if g is not None else "-"
            print(f"  {cls:34} allocated {allocated:14} constructed-as {constructed:18} Ghidra {ghidra:7} {verdict}")

    show("Ghidra structure contradicted by the binary", lambda v: "short" in v or "too long" in v or "longer" in v)
    show("Ghidra structure confirmed", lambda v: v.startswith("matches") or v.startswith("consistent"))
    show("Classes with a size from the binary and no Ghidra structure", lambda v: v.startswith("no Ghidra structure"))
    show("Several sizes under one name", lambda v: v.startswith("several"))

    if show_sites:
        print("\nAllocation sites:")
        for s in sites:
            size = f"0x{s['size']:x}" if s["size"] is not None else "?"
            print(f"  0x{s['site']:08x} {s['in'][:40]:40} {s['allocator'][:28]:28} {size:7} "
                  f"{s['label'] or '':32} {s['constructor'] or ''}")


def write_json(sites, side="driving"):
    # tools/alloc_sizes_<side>.json: per class, the sizes allocated under its name (exact) and the sizes of
    # allocations its constructor ran on (lower bounds). tools/preprocess.py pads an overlay class out to the
    # exact size when Ghidra's structure is shorter - see generate_layouts.
    exact, lower = defaultdict(set), defaultdict(set)
    for s in sites:
        if s["size"] is None:
            continue
        by_label = class_from_label(s["label"]) if s["label"] else None
        if by_label:
            exact[by_label].add(s["size"])
        if s["constructor"] and s["constructor"] != by_label:
            lower[s["constructor"].split("::")[-1]].add(s["size"])
    out = {cls: {"allocated": sorted(exact.get(cls, ())), "constructed": sorted(lower.get(cls, ()))}
           for cls in sorted(set(exact) | set(lower))}
    path = os.path.join(ROOT, "tools", f"alloc_sizes_{side}.json")
    with open(path, "w", newline="\n") as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"{len(out)} classes written to {os.path.relpath(path, ROOT)}")


if __name__ == "__main__":
    sites = collect()
    if "--json" in sys.argv:
        write_json(sites)
    else:
        report(sites, show_sites="--sites" in sys.argv)
