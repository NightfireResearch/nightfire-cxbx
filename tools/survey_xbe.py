"""Reports the facts about an XBE that decide how much work the standalone loader needs for it.

Deliberately static and independent of Ghidra, so it can be run against either XBE without disturbing a live
analysis session.

Counting FS-segment accesses by byte pattern alone is useless - the executable sections contain read-only
data, and a two-byte prefix matches it constantly. So each candidate is decoded far enough to read its
displacement, and only displacements inside the KPCR (below 0x100) are counted. Checked against the action
engine, where Ghidra reports 38 FS accesses, this method finds the same order of magnitude instead of 137.
"""
import struct
import sys

XOR_ENTRY_RETAIL = 0xA8FC57AB
XOR_THUNK_RETAIL = 0x5B6D40B6

# (prefix bytes, offset of the modrm byte or None, offset of the disp32)
FS_FORMS = [
    ("mov eax, fs:[disp]",   b"\x64\xa1",       None, 2),
    ("mov fs:[disp], eax",   b"\x64\xa3",       None, 2),
    ("mov reg, fs:[disp]",   b"\x64\x8b",       2,    3),
    ("mov fs:[disp], reg",   b"\x64\x89",       2,    3),
    ("movzx reg, fs:[disp]", b"\x64\x0f\xb6",   3,    4),
    ("push fs:[disp]",       b"\x64\xff",       2,    3),
    ("pop fs:[disp]",        b"\x64\x8f",       2,    3),
]


def u32(data, offset):
    return struct.unpack_from('<I', data, offset)[0]


def count_fs_accesses(blob):
    """Returns {displacement: count} for plausible KPCR/TEB reads and writes."""
    found = {}
    for name, prefix, modrm_at, disp_at in FS_FORMS:
        start = 0
        while True:
            i = blob.find(prefix, start)
            if i < 0:
                break
            start = i + 1
            if i + disp_at + 4 > len(blob):
                continue
            # A memory operand with a bare disp32 is mod=00, rm=101.
            if modrm_at is not None and (blob[i + modrm_at] & 0xC7) != 0x05:
                continue
            disp = u32(blob, i + disp_at)
            if disp < 0x100:
                found[disp] = found.get(disp, 0) + 1
    return found


def survey(path):
    data = open(path, 'rb').read()
    base = u32(data, 0x104)
    section_count = u32(data, 0x11c)
    section_headers = u32(data, 0x120) - base
    thunk = u32(data, 0x158) ^ XOR_THUNK_RETAIL

    print("%s" % path)
    print("  base 0x%08x, image 0x%08x (%.1f MB), entry 0x%08x, %d sections"
          % (base, u32(data, 0x10c), u32(data, 0x10c) / 1048576.0,
             u32(data, 0x128) ^ XOR_ENTRY_RETAIL, section_count))

    sections = []
    for i in range(section_count):
        off = section_headers + i * 0x38
        sections.append((u32(data, off), u32(data, off + 4), u32(data, off + 8),
                         u32(data, off + 12), u32(data, off + 16)))

    thunk_off = None
    for flags, vaddr, vsize, raw, rawsize in sections:
        if vaddr <= thunk < vaddr + rawsize:
            thunk_off = raw + (thunk - vaddr)
    count = 0
    if thunk_off is not None:
        while u32(data, thunk_off + count * 4) != 0 and count < 400:
            count += 1
    print("  kernel imports: %d" % count)

    blob = b""
    for flags, vaddr, vsize, raw, rawsize in sections:
        if flags & 4:
            blob += data[raw:raw + rawsize]

    fs = count_fs_accesses(blob)
    total = sum(fs.values())
    print("  FS-segment accesses with a KPCR-sized displacement: %d" % total)
    for disp in sorted(fs):
        meaning = {
            0x00: "SEH exception list - same on Win32, works as-is",
            0x04: "Xbox: TLS array.  Win32: stack base  <-- the incompatible one",
            0x20: "Xbox: KPCR.Prcb.  Win32: process id",
            0x24: "Xbox: current IRQL.  Win32: thread id",
            0x28: "Xbox: current KTHREAD.  Win32: ActiveRpcHandle (unused, free)",
            0x2c: "Win32 TLS array - already correct",
        }.get(disp, "")
        print("    +0x%02x  %4d  %s" % (disp, fs[disp], meaning))
    print()


for path in sys.argv[1:]:
    survey(path)
