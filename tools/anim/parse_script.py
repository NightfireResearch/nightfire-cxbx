#!/usr/bin/env python3
"""Disassembler / validator for 007 Nightfire action-engine animation script files (hash type 0x06).

Format (all little-endian, derived from Xbox AnimScriptInit 0x12620, AnimProcessScriptData 0x110c0,
AnimProcessScriptCmds 0x17c50, AnimScriptPeekSeq 0x125b0; PS2 AnimScriptPeekSfx 0x1c7418):

  +0  u32  hashcode of this file (0x06xxxxxx)
  +4  s16  numFrames   -> script+0xac (script frames run 1..numFrames, 60 Hz units)
  +6  u16  info:  bits 0-8  numCmds (9 bits; the Xbox runtime copies only the low byte to script+0xae)
                  bits 9-14 rate   (always 60 in the dump; copied to script+0xaf, never read by anim code)
                  bit  15   loop   (script flags |= 0x80000000 -> AnimScriptEnd forces playMode 1 = loop)
  +8  commands, each starting with u16 (op | len<<8), followed by fixed operands + len extra words:
        op 0  SEQ    start, end, seqId [+len]   play sequence 0x04000000|seqId over script frames start..end
        op 1  SFX    frame, sfxId      [+len]   Sound_Play3D(sfxId) when frame is crossed
        op 2  NOP    w1, w2            [+len]   (skipped by the interpreter)
        op 4  EVENT  frame, type, [param if len>=1]
        op 3 / op>=5: never occur; the Xbox interpreter would advance only `len` words (bug-prone),
                      AnimProcessScriptData would advance len+3 (op 3) or len+1 (op>=5).
  then zero padding up to a 32-byte multiple.

Usage: parse_script.py [dumpdir] [-v]   (-v prints every script's disassembly)
"""
import glob
import os
import struct
import sys
from collections import Counter

from nfpaths import DUMP

EVENT_NAMES = {
    0: "FOOTSTEP_TOGGLE",     # ScriptFootstepFoot ^= 1; footstep type 1 for 0x6000099/0x60000ae else 2
    1: "PARENT_TOGGLE_8F4",   # parent->extraObjectData+0x8f4 ^= 1 (PS2 +0x964)
    2: "CALLBACK",            # script->onEvent(obj, animObj, script, param)  (script+0x6c)
    3: "STOP_SOUND",          # Sound_StopAllWithId(param)
    4: "FOOTSTEP_LEFT",       # ScriptFootstepFoot = 0 (+ footstep type)
    5: "FOOTSTEP_RIGHT",      # ScriptFootstepFoot = 1 (+ footstep type)
    6: "EFFECT_C0",           # Effect_Create(obj->animState, obj, 0xc0, 1.0)
    7: "FIRE_WEAPON",         # Bullet_init from weapon datum + muzzle light + gas
}
FIXED_WORDS = {0: 3, 1: 2, 2: 2, 4: 2}   # operand words after the opcode word (before `len` extras)


def seq_info(dump, seq_hash):
    """Return (exists, isMorph) for a sequence file; isMorph = (u32 at +0xc) bit 3."""
    p = os.path.join(dump, "%08x.bin" % seq_hash)
    if not os.path.exists(p):
        return False, None
    d = open(p, "rb").read(16)
    return True, bool(struct.unpack_from("<I", d, 12)[0] & 8)


def parse(path, dump):
    d = open(path, "rb").read()
    name = os.path.basename(path)
    errs, warns, lines, refs = [], [], [], []
    evs = Counter()
    if len(d) < 8:
        return None, ["file shorter than header"], warns, lines, refs, Counter(), evs
    h, nframes, info = struct.unpack_from("<IhH", d, 0)
    ncmd, rate, loop = info & 0x1FF, (info >> 9) & 0x3F, info >> 15
    if "%08x.bin" % h != name.lower():
        errs.append("hashcode 0x%08x != filename" % h)
    if h >> 24 != 6:
        errs.append("not a type-0x06 file")
    if ncmd > 255:
        errs.append("numCmds %d > 255: Xbox runtime truncates to a byte" % ncmd)
    if ncmd > 32:
        errs.append("numCmds %d > 32: fired-mask bits alias" % ncmd)
    lines.append("%s  frames=%d rate=%d loop=%d cmds=%d" % (name, nframes, rate, loop, ncmd))
    ops = Counter()
    p = 8
    for i in range(ncmd):
        if p + 2 > len(d):
            errs.append("cmd %d runs past EOF" % i)
            break
        op, ln = d[p], d[p + 1]
        ops[(op, ln)] += 1
        if op not in FIXED_WORDS:
            errs.append("cmd %d @%#x: unknown op %d (len %d)" % (i, p, op, ln))
            break
        nw = FIXED_WORDS[op] + ln
        if p + 2 + 2 * nw > len(d):
            errs.append("cmd %d runs past EOF" % i)
            break
        w = struct.unpack_from("<%dH" % nw, d, p + 2)
        bit = ncmd - 1 - i       # bit of script+0x78 (fired mask) used by this command
        if op == 0:
            s, e, sid = w[0], w[1], w[2]
            sh = 0x04000000 | sid
            ok, morph = seq_info(dump, sh)
            refs.append((sh, ok))
            if not ok:
                errs.append("cmd %d: seq %08x missing from dump" % (i, sh))
            if not (1 <= s <= e):
                errs.append("cmd %d: bad range %d..%d" % (i, s, e))
            elif e > nframes:
                warns.append("cmd %d: seq range %d..%d ends past script numFrames %d (harmless: script clamps)" % (i, s, e, nframes))
            lines.append("  @%02x [bit%2d] SEQ    frames %4d..%-4d seq %08x%s%s%s" % (
                p, bit, s, e, sh, " (morph/facial)" if morph else "",
                "" if ok else " MISSING", (" extra=%s" % list(w[3:])) if ln else ""))
        elif op == 1:
            f, sfx = w[0], w[1]
            if not (1 <= f <= nframes):
                errs.append("cmd %d: sfx frame %d outside 1..%d" % (i, f, nframes))
            lines.append("  @%02x [bit%2d] SFX    frame %4d sfx %#06x%s" % (
                p, bit, f, sfx, (" extra=%s" % list(w[2:])) if ln else ""))
        elif op == 2:
            lines.append("  @%02x [bit%2d] NOP    %s" % (p, bit, list(w)))
        elif op == 4:
            f, typ = w[0], w[1]
            par = w[2] if ln >= 1 else None
            if not (1 <= f <= nframes):
                errs.append("cmd %d: event frame %d outside 1..%d" % (i, f, nframes))
            if typ not in EVENT_NAMES:
                errs.append("cmd %d: unknown event type %d" % (i, typ))
            evs[(typ, par) if typ in (2, 3) else typ] += 1
            if typ in (2, 3) and par is None:
                errs.append("cmd %d: event %d reads a param word it does not have" % (i, typ))
            lines.append("  @%02x [bit%2d] EVENT  frame %4d %-18s%s%s" % (
                p, bit, f, EVENT_NAMES.get(typ, "type%d" % typ),
                "" if par is None else " param=%d (%#x)" % (par, par),
                (" extra=%s" % list(w[3:])) if ln > 1 else ""))
        p += 2 + 2 * nw
    tail = d[p:]
    if any(tail):
        errs.append("%d non-zero bytes after last command @%#x" % (sum(1 for b in tail if b), p))
    if len(d) % 32:
        errs.append("size %d not a multiple of 32" % len(d))
    lines.append("  used %d bytes, %d bytes zero padding" % (p, len(tail)))
    return (h, nframes, rate, loop, ncmd, p), errs, warns, lines, refs, ops, evs


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    verbose = "-v" in sys.argv
    dump = args[0] if args else DUMP
    files = sorted(glob.glob(os.path.join(dump, "06*.bin")))
    nclean = 0
    nwarn = 0
    allops, rates, loops, sizes, evs = Counter(), Counter(), Counter(), Counter(), Counter()
    seqrefs, missing, peekable = set(), set(), 0
    for f in files:
        hdr, errs, warns, lines, refs, ops, ev = parse(f, dump)
        evs.update(ev)
        nwarn += bool(warns)
        allops.update(ops)
        if hdr:
            rates[hdr[2]] += 1
            loops[hdr[3]] += 1
            sizes[os.path.getsize(f)] += 1
            d = open(f, "rb").read()
            # AnimScriptPeekSeq condition: byte at +7 == 0x78 (rate 60, no loop, <256 cmds) -> seq = word at +0xe
            if d[7] == 0x78 and d[8] == 0:
                peekable += 1
        for sh, ok in refs:
            seqrefs.add(sh)
            if not ok:
                missing.add(sh)
        if verbose or errs or warns:
            print("\n".join(lines))
        for e in errs:
            print("  !! " + e)
        for w_ in warns:
            print("  (warning) " + w_)
        if not errs:
            nclean += 1
    print("\n=== %d/%d script files parse cleanly (every byte accounted for)" % (nclean, len(files)))
    print("files with (data-level) warnings: %d" % nwarn)
    print("op/len histogram:", dict(sorted(allops.items())))
    print("event types (type or (type,param)):", {(EVENT_NAMES.get(k, k) if not isinstance(k, tuple) else (EVENT_NAMES[k[0]], k[1])): v for k, v in sorted(evs.items(), key=str)})
    print("rate field:", dict(rates), " loop bit:", dict(loops), " file sizes:", dict(sorted(sizes.items())))
    print("distinct sequences referenced: %d, missing from dump: %d %s" % (
        len(seqrefs), len(missing), sorted("%08x" % m for m in missing)[:20]))
    print("scripts AnimScriptPeekSeq can peek (byte7==0x78 and cmd0 is SEQ): %d" % peekable)
    allseq = {int(os.path.basename(p)[:8], 16) for p in glob.glob(os.path.join(dump, "04*.bin"))}
    print("04*.bin files in dump: %d, not referenced by any script: %d" % (len(allseq), len(allseq - seqrefs)))


if __name__ == "__main__":
    main()
