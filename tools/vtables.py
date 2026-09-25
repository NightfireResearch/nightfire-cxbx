#!/usr/bin/env python3
"""Find the driving engine's vtables, their slots, and which classes they belong to.

A method declared // VIRTUAL(n) in an overlay class is called through the object's own vtable, so the call
reaches whichever override the object's class has. tools/preprocess.py checks the declaration against every
implementation that can be in slot n - which needs to know every vtable of the class and of the classes derived
from it. Nothing in Ghidra's export says that, so it is read from the binary, from three patterns:

  own      a constructor C::C storing a vtable at [this]: C's own vtable;
  derived  a call to a constructor B::B followed, before the next call, by a store of another vtable to the
           object - a class derived from B whose constructor was inlined (the scheduler's four schedules);
  derived  a call to B::operator_new followed by a vtable store, or by a call to a constructor D::D - an object of
           a class allocated by B's allocator, so derived from B (every event: they live in the event buffer);

plus, in any function, one vtable stored over another at the same [reg] - the second derives from the first's
class (a base constructor inlined into the derived one).

A vtable is an address stored to [reg] whose first word is a function; its slots run while the words are
functions and stop at the next vtable found.

    python tools/vtables.py          # writes tools/vtables_driving.json

Re-run it after re-syncing tools/functions_driving.json, as with tools/abi_facts.py; the output is committed
because CI builds without the disc. See docs/driving-injection-framework.md, "VIRTUAL(n)".
"""

import json
import os
import sys
from collections import defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from abi_facts import Image, ROOT

from capstone import x86

LOOKAHEAD = 16   # instructions searched after a constructor or allocator call for the vtable store


def main(side="driving"):
    image = Image(os.path.join(ROOT, "disc", "Driving.xbe"))
    symbols = json.load(open(os.path.join(ROOT, "tools", f"functions_{side}.json"), encoding="utf-8"))
    names = {int(s["address"], 16): s["name"] for s in symbols}
    starts = sorted(names)
    function_starts = set(starts)

    def class_of(name, method):
        # "C::C" -> C for method "ctor"; "C::operator_new" -> C for method "new"
        if "::" not in name:
            return None
        cls, m = name.rsplit("::", 1)
        if method == "ctor" and m == cls.split("::")[-1]:
            return cls
        if method == "new" and m in ("operator_new", "operator new"):
            return cls
        return None

    def is_vtable(address):
        first = image.u32(address)
        return first is not None and first in function_starts

    def vtable_store(insn):
        # mov dword ptr [reg], imm - a vtable pointer written to an object's first word
        ops = insn.operands
        if insn.mnemonic != "mov" or len(ops) != 2 or ops[0].type != x86.X86_OP_MEM or ops[1].type != x86.X86_OP_IMM:
            return None, None
        if ops[0].mem.disp != 0 or ops[0].mem.index != 0 or ops[0].mem.base == 0:
            return None, None
        value = ops[1].imm & 0xffffffff
        return (value, ops[0].mem.base) if is_vtable(value) else (None, None)

    own = defaultdict(set)
    derived = defaultdict(set)
    vtables = set()

    for n, start in enumerate(starts):
        end = starts[n + 1] if n + 1 < len(starts) else start + 0x1000
        insns, address = [], start
        while address < end:
            insn = image.insn(address)
            if insn is None:
                break
            insns.append(insn)
            address += insn.size
        here = class_of(names[start], "ctor")
        last_by_reg = {}
        for i, insn in enumerate(insns):
            vt, reg = vtable_store(insn)
            if vt is not None:
                vtables.add(vt)
                if here:
                    own[here].add(vt)
                previous = last_by_reg.get(reg)
                if previous is not None and previous != vt:
                    derived[("vt", previous)].add(vt)
                last_by_reg[reg] = vt
                continue
            ops = insn.operands
            if insn.mnemonic == "call" and ops and ops[0].type == x86.X86_OP_IMM:
                target = names.get(ops[0].imm, "")
                base = class_of(target, "ctor") or class_of(target, "new")
                if base:
                    for later in insns[i + 1:i + 1 + LOOKAHEAD]:
                        lv, _ = vtable_store(later)
                        if lv is not None:
                            vtables.add(lv)
                            derived[base].add(lv)
                            break
                        if later.mnemonic == "call" and later.operands and later.operands[0].type == x86.X86_OP_IMM:
                            sub = class_of(names.get(later.operands[0].imm, ""), "ctor")
                            if sub and sub != base:
                                derived[base].add(("class", sub))
                            break
                last_by_reg = {}   # a call leaves the registers undefined
            elif insn.mnemonic in ("ret", "jmp"):
                last_by_reg = {}

    ordered = sorted(vtables)
    slots = {}
    for k, vt in enumerate(ordered):
        limit = ordered[k + 1] if k + 1 < len(ordered) else vt + 0x400
        entries, address = [], vt
        while address < limit:
            value = image.u32(address)
            if value is None or value not in function_starts:
                break
            entries.append("0x%08x" % value)
            address += 4
        slots["0x%08x" % vt] = entries

    # Resolve "a vtable stored over vtable X" to the class owning X, and "class D" to D's own vtables.
    owner = {vt: cls for cls, vts in own.items() for vt in vts}
    resolved = defaultdict(set)
    for key, items in derived.items():
        cls = owner.get(key[1]) if isinstance(key, tuple) else key
        if cls is None:
            continue
        for item in items:
            if isinstance(item, tuple):
                resolved[cls].update(own.get(item[1], ()))
                resolved[cls].add(("class", item[1]))
            else:
                resolved[cls].add(item)

    classes = {}
    for cls in sorted(set(own) | set(resolved)):
        classes[cls] = {
            "own": sorted("0x%08x" % v for v in own.get(cls, ())),
            "derived_vtables": sorted("0x%08x" % v for v in resolved.get(cls, ()) if not isinstance(v, tuple)),
            "derived_classes": sorted(v[1] for v in resolved.get(cls, ()) if isinstance(v, tuple)),
        }
    out = {"vtables": slots, "classes": classes}
    path = os.path.join(ROOT, "tools", f"vtables_{side}.json")
    with open(path, "w", newline="\n") as f:
        json.dump(out, f, indent=1, sort_keys=True)
        f.write("\n")
    print(f"{len(slots)} vtables, {len(classes)} classes with a vtable -> {os.path.relpath(path, ROOT)}")
    for name in ("Schedule", "Event", "RSceneObj"):
        c = classes.get(name)
        if c:
            print(f"  {name}: own {c['own']}, {len(c['derived_vtables'])} derived vtables, "
                  f"derived classes {c['derived_classes'][:6]}{'...' if len(c['derived_classes']) > 6 else ''}")


if __name__ == "__main__":
    main()
