"""Prototypes and return types for every named Xbox game function the struct work hasn't covered, for
ghidra/NightfireStructs.py. Read-only.

    python protos_all.py     -> results/structs/protos-all.json   (sheet signatures, overloads by stack cleanup)
                                results/structs/returns-all.json  (return types only, for functions that already
                                                                    have a prototype from the struct files)

Prototypes (functions in game code, named, not already in a results/structs file):
  - the name has one sheet row with a complete signature -> its parameters;
  - several rows (overloads) -> the one whose stack arguments match the Xbox RET n (n/4 slots, double and
    long long count two), when exactly one does; __cdecl functions (RET 0) with several candidates are skipped;
  - __thiscall when the function reads ECX before writing it, sits in a vtable, is a constructor or destructor,
    or its sheet signature ends in const; otherwise __cdecl. JMP thunks are skipped.
  Parameter types come from the sheet; types without a Ghidra struct become int / void *.
Return types (from the disassembly, only when certain):
  - constructor -> Class * (when the struct exists; else void *); destructor -> void;
  - no write to EAX/AX/AL anywhere and no call or jump that could leave one -> void;
  - every RET preceded (after epilogue pops) by an FPU load -> float;
  - every RET preceded by SETcc AL / MOV AL,imm / XOR AL,AL, and the name starts Is/Has/Can/... -> bool.
"""

import collections
import glob
import json
import os
import re

from lib import ghidra_ro as g
from lib.index import Index
from struct_pilot import FORCE_MEMBER, reads_ecx_first

HERE = os.path.dirname(os.path.abspath(__file__))
GAME_END = 0x1503b0
BASIC = {"char": "char", "bool": "bool", "float": "float", "double": "double", "int": "int", "short": "short",
         "long": "int", "void": "void", "unsigned int": "uint", "unsigned": "uint", "unsigned short": "ushort",
         "unsigned char": "byte", "unsigned long": "uint", "signed char": "char", "long long": "longlong",
         "unsigned long long": "ulonglong"}
WIDE = {"double", "long long", "unsigned long long"}
BOOLISH = ("Is", "Has", "Can", "Should", "Was", "Are", "Does", "Did", "Will", "Allow", "Able", "Need")


def struct_types():
    known = {"MATRIX4", "COORD3", "COORD4", "COORD2", "_VECTOR"}
    done = set()
    for f in glob.glob(os.path.join(HERE, "results", "structs", "*.json")):
        if os.path.basename(f).startswith(("protos-all", "returns-all")):
            continue
        d = json.load(open(f))
        if d.get("program") != "Driving.xbe":
            continue
        known |= {t["name"] for t in d.get("types", [])}
        done |= {int(p["address"], 16) for p in d.get("prototypes", [])}
    return known, done


def ctype(p, known):
    p = p.strip().replace("const ", "").replace("volatile ", "")
    ptr = p.count("*") + p.count("&")
    base = re.sub(r"[\s*&]+$", "", p).strip()
    base = BASIC.get(base, base)
    if base not in known and base not in BASIC.values():
        base = "int" if ptr == 0 else "void"
    return base + " *" * ptr if ptr else base


def params_of(sig):
    m = re.search(r"\((.*)\)", sig)
    if not m:
        return None
    ps = [x.strip() for x in re.split(r",(?![^<]*>)", m.group(1)) if x.strip() and x.strip() != "void"]
    if any("(" in x or "..." in x for x in ps):
        return None   # function pointers, varargs: leave to a person
    return ps


def slots_of(ps):
    return sum(2 if re.sub(r"[\s*&]+$", "", x.replace("const ", "")).strip() in WIDE and "*" not in x and "&" not in x
               else 1 for x in ps)


def body(a):
    return [l.split(":", 1)[1].strip() for l in g.get("disassemble_function", program=g.XBOX, address=hex(a)).splitlines()
            if ":" in l]


def ret_bytes(ins):
    ns = [int(m.group(1), 16) if m.group(1).startswith("0x") else int(m.group(1))
          for s in ins for m in [re.match(r"RET (0x[0-9a-f]+|\d+)$", s)] if m]
    return max(ns) if ns else 0


EAX_W = re.compile(r"^(MOV|XOR|LEA|POP|MOVZX|MOVSX|SUB|ADD|AND|OR|INC|DEC|IMUL|SHL|SHR|SAR|NEG|NOT|CDQ|SBB|ADC|"
                   r"SET\w+|XCHG|CMOVE?\w*|MUL|DIV|IDIV|LODS\w*|IN|CWDE|CBW|MOVS\w*) (EAX|AX|AL|AH)\b")


def infer_return(leaf, owner, ins, known):
    if owner and leaf == owner.split("::")[-1]:
        return f"{owner} *" if owner in known else "void *"
    if owner and leaf == "~" + owner.split("::")[-1]:
        return "void"
    rets = [i for i, s in enumerate(ins) if s.startswith("RET")]
    if not rets:
        return None

    def before(i):
        j = i - 1
        while j >= 0 and re.match(r"(POP|ADD ESP|MOV ESP,EBP|LEAVE|MOV dword ptr FS:)", ins[j]):
            j -= 1
        return ins[j] if j >= 0 else ""
    last = [before(i) for i in rets]
    if all(x.startswith(("FLD", "FILD")) for x in last):
        return "float"
    if leaf.startswith(BOOLISH) and all(re.match(r"(SET\w+ AL|MOV AL,|XOR AL,AL)", x) for x in last):
        return "bool"
    writes_eax = any(EAX_W.match(s) or s.startswith(("CDQ", "CWDE", "LODS", "RDTSC", "CPUID")) for s in ins)
    tail = any(s.startswith("JMP ") and not re.match(r"JMP 0x[0-9a-f]+$", s) for s in ins)   # indirect jumps
    call_then_ret = any(x.startswith("CALL") for x in last)
    jmp_out = any(re.match(r"JMP 0x([0-9a-f]+)$", s) and not (rets and False) for s in ins[-1:])
    if not writes_eax and not tail and not call_then_ret and not jmp_out and not any(s.startswith("CALL") for s in ins[-2:]):
        return "void"
    return None


def caller_cleanup(a):
    """Stack bytes a __cdecl function takes, from its callers: the ADD ESP,k right after a CALL to it (0 when the
    next instruction isn't one, which also covers no arguments). None when callers disagree or there are none."""
    seen = set()
    for line in g.get("get_xrefs_to", program=g.XBOX, address=hex(a)).splitlines()[:6]:
        m = re.search(r"From ([0-9a-f]+) in", line)
        if not m or "CALL" not in line.upper() and "[" in line:
            continue
        site = int(m.group(1), 16)
        f = g.get("get_function_by_address", program=g.XBOX, address=hex(site))
        fm = re.search(r"at ([0-9a-f]+)", f) or re.search(r"0x([0-9a-f]+)", f)
        if not fm:
            continue
        ins = [l.split(":", 1) for l in g.get("disassemble_function", program=g.XBOX, address="0x" + fm.group(1)).splitlines() if ":" in l]
        for i, (ad, s_) in enumerate(ins):
            if not re.fullmatch(r"\s*[0-9a-f]+", ad):
                continue
            if int(ad, 16) == site and s_.strip().startswith("CALL") and i + 1 < len(ins):
                nxt = ins[i + 1][1].strip()
                k = re.match(r"ADD ESP,(0x[0-9a-f]+|\d+)$", nxt)
                seen.add(int(k.group(1), 0) if k else 0)
    return seen.pop() if len(seen) == 1 else None


def main():
    known, done = struct_types()
    q = g.qualified_names(g.XBOX)
    fx = dict(g.functions(g.XBOX))
    rows = collections.defaultdict(list)
    for r in Index().rows:
        if "(" in r["name"]:
            rows[r["name"].split("(")[0]].append(r)
    virtual = {int(s, 16) for v in json.load(open(os.path.join(HERE, "data", "vtables.json")))["xbox"] for s in v["slots"]}
    protos, returns, why, mismatches = [], [], collections.Counter(), []
    for a in sorted(fx):
        if a >= GAME_END:
            continue
        nm = q.get(a, fx[a])
        if re.match(r"(FUN|thunk_FUN|LAB)_", nm.split("::")[-1]):
            continue
        leaf = nm.split("::")[-1]
        owner = nm.rsplit("::", 1)[0] if "::" in nm else None
        ins = body(a)
        if ins and ins[0].startswith("JMP"):
            why["thunk"] += 1
            continue
        ret = infer_return(leaf, owner, ins, known)
        if a in done:
            if ret:
                returns.append({"address": f"0x{a:08x}", "return": ret, "return_only": True})
            continue
        rs = [r for r in rows.get(nm, []) if not r["truncated"]]
        cut = len(rows.get(nm, [])) - len(rs)
        pick = None
        if len(rs) == 1 and not cut:
            pick = rs[0]
        elif len(rs) > 1 or (rs and cut):
            n = ret_bytes(ins)
            if n == 0 and not (owner and reads_ecx_first(a)):
                n = caller_cleanup(a)      # __cdecl: the caller's ADD ESP,k after the call
            fits = [r for r in rs if params_of(r["name"]) is not None and slots_of(params_of(r["name"])) * 4 == n]
            if n is not None and len(fits) == 1 and not cut:
                pick, why["overload by RET n"] = fits[0], why["overload by RET n"] + 1
            else:
                why["overload unresolved"] += 1
        else:
            why["truncated" if cut else "no sheet row"] += 1
        if pick is None:
            if ret and nm not in rows:
                pass
            continue
        ps = params_of(pick["name"])
        if ps is None:
            why["function-pointer / varargs parameter"] += 1
            continue
        member = (a in virtual or leaf in FORCE_MEMBER or (owner and leaf in (owner.split("::")[-1], "~" + owner.split("::")[-1]))
                  or pick["name"].rstrip().endswith("const") or reads_ecx_first(a))
        cc = "__thiscall" if member and owner else ("__stdcall" if ret_bytes(ins) else "__cdecl")
        if cc != "__cdecl" and ret_bytes(ins) != slots_of(ps) * 4:
            why["stack size disagrees with the sheet"] += 1
            mismatches.append(f"0x{a:08x} {nm}: RET {ret_bytes(ins)}, sheet {pick['name']}")
            continue
        why[cc] += 1
        e = {"address": f"0x{a:08x}", "calling_convention": cc,
             "params": [{"name": f"param_{i + 1}", "type": ctype(p, known)} for i, p in enumerate(ps)],
             "sheet": pick["name"]}
        if ret:
            e["return"] = ret
        protos.append(e)
        why["prototype"] += 1
    out = os.path.join(HERE, "results", "structs")
    json.dump({"program": "Driving.xbe", "types": [], "prototypes": protos}, open(os.path.join(out, "protos-all.json"), "w"), indent=1)
    json.dump({"program": "Driving.xbe", "types": [], "prototypes": returns}, open(os.path.join(out, "returns-all.json"), "w"), indent=1)
    with open(os.path.join(HERE, "results", "protos-all-mismatches.txt"), "w") as f:
        f.write("Stack cleanup (RET n) disagrees with the sheet signature: misnamed, or a by-value struct/float argument\n")
        f.write("\n".join(mismatches) + "\n")
    rc = collections.Counter(p.get("return") for p in protos + returns if p.get("return"))
    print(f"protos-all.json: {len(protos)} prototypes; returns-all.json: {len(returns)} return-only; {dict(why)}")
    print(f"return types: {dict(rc.most_common(8))} ...")


if __name__ == "__main__":
    main()
