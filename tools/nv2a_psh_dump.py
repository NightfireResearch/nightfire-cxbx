#!/usr/bin/env python3
"""Decodes Xbox D3DPIXELSHADERDEF blobs (NV2A register combiner programs) into readable form.

Usage: python tools/nv2a_psh_dump.py Release/d3d9_pixel_shaders.log [--summary]

The input is the file drivinginject writes when it dumps pixel shader definitions: blocks of "==== pixel
shader N at ADDR" followed by 60 hex words. Each block is printed as the eight combiner stages (RGB and
alpha halves), the final combiner, the texture modes and the constant mappings - the same notation the
XDK's pixel shader assembler uses (A*B + C*D, r0/r1/t0..t3/v0/v1/c0/c1, input mappings as suffixes).

--summary prints only a histogram of texture modes, stage counts and the features used across all
shaders, which is what decides what a translator has to support first.
"""
import re, sys
from collections import Counter

REG = {0: "zero", 1: "c0", 2: "c1", 3: "fog", 4: "v0", 5: "v1", 8: "t0", 9: "t1", 10: "t2", 11: "t3",
       12: "r0", 13: "r1", 14: "v1r0sum", 15: "efprod"}
MAP = {0: "", 1: "~", 2: "_bx2", 3: "-_bx2", 4: "_bias", 5: "-_bias", 6: "_signed", 7: "-"}
TEXMODE = {0: "none", 1: "project2d", 2: "project3d", 3: "cubemap", 4: "passthru", 5: "clipplane",
           6: "bumpenvmap", 7: "bumpenvmap_lum", 8: "brdf", 9: "dot_st", 10: "dot_zw", 11: "dot_rflct_diff",
           12: "dot_rflct_spec", 13: "dot_str_3d", 14: "dot_str_cube", 15: "dpndnt_ar", 16: "dpndnt_gb",
           17: "dotproduct", 18: "dot_rflct_spec_const"}
OP = {0: "", 1: "_bias", 2: "_x2", 3: "_bx2", 4: "_x4", 6: "_d2"}

def inp(byte, alpha):
    reg = byte & 0xF; chan = (byte >> 4) & 1; m = (byte >> 5) & 7
    name = REG.get(reg, "?%d" % reg)
    if reg == 0:
        return {0: "0", 1: "1", 2: "-1", 3: "1", 4: "-0.5", 5: "0.5", 6: "0", 7: "0"}[m]
    if alpha:
        name += ".a" if chan else ".b"
    elif chan:
        name += ".a"
    return MAP[m] + name if not MAP[m].startswith("-") else "-(" + name + MAP[m][1:] + ")"

def inputs(word, alpha):
    return [inp((word >> s) & 0xFF, alpha) for s in (24, 16, 8, 0)]

def dest(n):
    return {0: "discard"}.get(n, REG.get(n, "?%d" % n))

def stage_text(icw, ocw, alpha):
    a, b, c, d = inputs(icw, alpha)
    cd = dest(ocw & 0xF); ab = dest((ocw >> 4) & 0xF); sm = dest((ocw >> 8) & 0xF)
    flags = ocw >> 12
    cddot = flags & 1; abdot = flags & 2; mux = flags & 4; op = OP.get((flags >> 3) & 7, "?op%d" % ((flags >> 3) & 7))
    cdb2a = flags & 0x40; abb2a = flags & 0x80
    parts = []
    if ab != "discard":
        parts.append("%s%s = %s %s %s%s" % (ab, op, a, "." if abdot else "*", b, " (blue->alpha)" if abb2a else ""))
    if cd != "discard":
        parts.append("%s%s = %s %s %s%s" % (cd, op, c, "." if cddot else "*", d, " (blue->alpha)" if cdb2a else ""))
    if sm != "discard":
        parts.append("%s%s = %s" % (sm, op, ("mux(%s*%s, %s*%s)" if mux else "%s*%s + %s*%s") % (a, b, c, d)))
    return "; ".join(parts) if parts else "(nothing)"

def decode(words):
    d = {}
    d["alphaIn"] = words[0:8]; d["finalABCD"] = words[8]; d["finalEFG"] = words[9]
    d["c0"] = words[10:18]; d["c1"] = words[18:26]; d["alphaOut"] = words[26:34]; d["rgbIn"] = words[34:42]
    d["compare"] = words[42]; d["fc0"] = words[43]; d["fc1"] = words[44]; d["rgbOut"] = words[45:53]
    d["count"] = words[53]; d["texmodes"] = words[54]; d["dotmap"] = words[55]; d["inputtex"] = words[56]
    d["c0map"] = words[57]; d["c1map"] = words[58]; d["fcconst"] = words[59]
    return d

def describe(n, addr, words):
    d = decode(words)
    count = d["count"] & 0xF
    out = ["==== pixel shader %d at %s: %d stage%s, mux on %s, %s%s" % (
        n, addr, count, "" if count == 1 else "s", "msb" if d["count"] & 0x100 else "lsb",
        "per-stage c0, " if d["count"] & 0x1000 else "shared c0, ", "per-stage c1" if d["count"] & 0x10000 else "shared c1")]
    modes = [(d["texmodes"] >> (5 * i)) & 0x1F for i in range(4)]
    out.append("  textures: " + ", ".join("t%d=%s" % (i, TEXMODE.get(m, "?%d" % m)) for i, m in enumerate(modes))
               + "  inputtex 0x%x dotmap 0x%x compare 0x%x" % (d["inputtex"], d["dotmap"], d["compare"]))
    out.append("  c0 map %08x  c1 map %08x  final consts %08x" % (d["c0map"], d["c1map"], d["fcconst"]))
    for i in range(count):
        out.append("  stage %d: rgb   %s   [c0 %08x c1 %08x]" % (i, stage_text(d["rgbIn"][i], d["rgbOut"][i], False), d["c0"][i], d["c1"][i]))
        out.append("           alpha %s" % stage_text(d["alphaIn"][i], d["alphaOut"][i], True))
    abcd = d["finalABCD"]; efg = d["finalEFG"]
    if abcd or efg:
        a, b, c, dd = inputs(abcd, False)
        e, f, g, _ = inputs(efg, False)
        gA = inp((efg >> 8) & 0xFF, True)
        flags = efg & 0xFF
        out.append("  final: rgb = %s*%s + (1-%s)*%s + %s ; alpha = %s ; ef = %s*%s%s%s%s  [consts %08x %08x]" % (
            a, b, a, c, dd, gA, e, f, " clampsum" if flags & 0x80 else "", " ~v1" if flags & 0x40 else "",
            " ~r0" if flags & 0x20 else "", d["fc0"], d["fc1"]))
    else:
        out.append("  final: (default: r0)")
    return "\n".join(out)

def main():
    path = sys.argv[1]
    summary = "--summary" in sys.argv
    text = open(path).read()
    blocks = re.findall(r"==== pixel shader (\d+) at (\w+)\n((?:\s+[0-9a-f ]+\n)+)", text)
    modes = Counter(); counts = Counter(); finals = Counter(); regs = Counter(); features = Counter()
    for n, addr, body in blocks:
        words = [int(w, 16) for w in body.split()]
        if len(words) != 60:
            continue
        if not summary:
            print(describe(int(n), addr, words))
            continue
        d = decode(words)
        count = d["count"] & 0xF
        counts[count] += 1
        for i in range(4):
            modes[TEXMODE.get((d["texmodes"] >> (5 * i)) & 0x1F, "?")] += 1
        finals["custom" if (d["finalABCD"] or d["finalEFG"]) else "default"] += 1
        for w in list(d["rgbIn"][:count]) + list(d["alphaIn"][:count]) + [d["finalABCD"], d["finalEFG"] & ~0xFF]:
            for s in (24, 16, 8, 0):
                regs[REG.get((w >> s) & 0xF, "?")] += 1
        for i in range(count):
            for ocw in (d["rgbOut"][i], d["alphaOut"][i]):
                fl = ocw >> 12
                if fl & 3: features["dot product"] += 1
                if fl & 4: features["mux"] += 1
                if (fl >> 3) & 7: features["output scale/bias"] += 1
                if fl & 0xC0: features["blue to alpha"] += 1
                for dst in (ocw & 0xF, (ocw >> 4) & 0xF, (ocw >> 8) & 0xF):
                    if dst in (8, 9, 10, 11): features["writes a t register"] += 1
                    if dst in (4, 5): features["writes a v register"] += 1
        if d["compare"]: features["compare mode set"] += 1
        if d["fcconst"] & 0x100: features["texmode adjust"] += 1
        if d["count"] & 0x100 == 0 and any((o >> 12) & 4 for o in list(d["rgbOut"][:count]) + list(d["alphaOut"][:count])):
            features["mux on lsb"] += 1
    if summary:
        print("shaders:", sum(counts.values()))
        print("stage counts:", dict(sorted(counts.items())))
        print("texture modes:", dict(modes.most_common()))
        print("final combiner:", dict(finals))
        print("registers read:", dict(regs.most_common()))
        print("features:", dict(features.most_common()))

if __name__ == "__main__":
    main()
