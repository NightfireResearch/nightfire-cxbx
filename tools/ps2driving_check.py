"""Offline checks of the PS2 release's driving data against the Xbox's (docs/ps2-driving.md).

    python tools/ps2driving_check.py list <archive.viv>
    python tools/ps2driving_check.py compare <ps2 DRIVING dir> <xbox driving dir> [--by-file]
    python tools/ps2driving_check.py headers <ps2 DRIVING dir> <xbox driving dir> <ext>
    python tools/ps2driving_check.py syms <DRIVING.SYM> [pattern]
    python tools/ps2driving_check.py carp <ps2 dir> <xbox dir> <archive> <file.crp|.gal> [...]
    python tools/ps2driving_check.py geoprims <ps2 dir> <xbox dir> <archive> <file.crp> [...]
    python tools/ps2driving_check.py objects <ps2 dir> <xbox dir>
    python tools/ps2driving_check.py shapes <ps2 dir> <xbox dir>
    python tools/ps2driving_check.py banks <ps2 dir> <xbox dir>
    python tools/ps2driving_check.py streams <ps2 dir> <xbox dir>

list, compare and headers work on whole files; carp compares the data-group records of CARP files and galleries
(UGroup, engine/UGroup.h); objects counts the EAGL ELF objects' symbol classes and the render methods they name
(eagl/Loader.cpp); geoprims decodes a CARP file's PS2 GeoPrims as src/driving/ps2/Ps2Model.cpp does and compares
them with the Xbox twin's (counts, bounds, the methods each model pairs); shapes the image types of the SHPS/SHPX
containers; banks the sample headers of the BNKl banks; streams the SCHl/SCDl blocks of the .asf streams.

Both discs keep the driving engine's files in EA BIGF archives (.viv, .mus, .spe), whose entries may be
RefPack-compressed (0x10 0xfb). The BIGF and RefPack readers follow the extractor project's
common/extraction/extract_bigf.py (Q:\\nightfire-re, Nightfire Research Team), and our FileSys.cpp.
Reads only; writes nothing.
"""

import collections
import os
import re
import struct
import sys


def read_bigf(path):
    """The directory of a BIGF archive: [(name, offset, size)] (offsets and sizes big-endian)."""
    with open(path, "rb") as f:
        header = f.read(16)
        if header[:4] != b"BIGF":
            raise ValueError(f"{path}: not a BIGF archive")
        count, dir_size = struct.unpack_from(">II", header, 8)
        f.seek(0)
        directory = f.read(dir_size + 16)
    entries = []
    p = 16
    for _ in range(count):
        offset, size = struct.unpack_from(">II", directory, p)
        p += 8
        end = directory.index(b"\0", p)
        entries.append((directory[p:end].decode("latin1"), offset, size))
        p = end + 1
    return entries


def is_refpack(data):
    return len(data) >= 5 and data[0] in (0x10, 0x11) and data[1] == 0xFB


def refpack_size(data):
    return (data[2] << 16) | (data[3] << 8) | data[4]


def unrefpack(data):
    """RefPack (EA's LZ77) decompression."""
    size = refpack_size(data)
    out = bytearray(size)
    src = 5 if data[0] == 0x10 else 8
    dst = 0
    n = len(data)
    while src < n:
        b0 = data[src]
        if b0 < 0x80:
            b1 = data[src + 1]
            src += 2
            lit = b0 & 3
            out[dst:dst + lit] = data[src:src + lit]
            src += lit
            dst += lit
            dist = ((b0 & 0x60) << 3) + b1 + 1
            run = ((b0 >> 2) & 7) + 3
        elif b0 < 0xC0:
            b1, b2 = data[src + 1], data[src + 2]
            src += 3
            lit = b1 >> 6
            out[dst:dst + lit] = data[src:src + lit]
            src += lit
            dst += lit
            dist = ((b1 & 0x3F) << 8) + b2 + 1
            run = (b0 & 0x3F) + 4
        elif b0 < 0xE0:
            b1, b2, b3 = data[src + 1], data[src + 2], data[src + 3]
            src += 4
            lit = b0 & 3
            out[dst:dst + lit] = data[src:src + lit]
            src += lit
            dst += lit
            dist = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1
            run = ((b0 & 0x0C) << 6) + b3 + 5
        else:
            lit = (b0 & 0x1F) * 4 + 4
            src += 1
            if lit > 0x70:
                lit = b0 & 3
                out[dst:dst + lit] = data[src:src + lit]
                break
            out[dst:dst + lit] = data[src:src + lit]
            src += lit
            dst += lit
            continue
        start = dst - dist
        if dist >= run:
            out[dst:dst + run] = out[start:start + run]
        else:
            for i in range(run):
                out[dst + i] = out[start + i]
        dst += run
    return bytes(out)


def entry_data(f, offset, size):
    f.seek(offset)
    return f.read(size)


def unpacked(data):
    return unrefpack(data) if is_refpack(data) else data


def archives(folder):
    out = {}
    for name in os.listdir(folder):
        if os.path.splitext(name)[1].lower() in (".viv", ".mus", ".spe"):
            out[name.lower()] = os.path.join(folder, name)
    return out


def extension(name):
    ext = os.path.splitext(name)[1].lower()
    return ext if ext else "(none)"


# The PS2 file of a name with its extension, and the Xbox's twin: textures (shapes), fonts, movies.
PS2_TO_XBOX_EXTENSION = {".ssh": ".xsh", ".sfn": ".xfn", ".mpc": ".mad"}


def xbox_name(name):
    stem, ext = os.path.splitext(name.lower())
    return stem + PS2_TO_XBOX_EXTENSION.get(ext, ext)


def cmd_list(path):
    with open(path, "rb") as f:
        for name, offset, size in read_bigf(path):
            head = entry_data(f, offset, min(size, 16))
            packed = f"refpack -> {refpack_size(head)}" if is_refpack(head) else ""
            print(f"{offset:10x} {size:10d}  {name}  {packed}")


def cmd_compare(ps2_dir, xbox_dir, by_file):
    """Each entry of each PS2 archive against the entry of the same name in the Xbox's archive of the same
    name: identical bytes, identical once unpacked, the same size unpacked, or different."""
    ps2 = archives(ps2_dir)
    xbox = archives(xbox_dir)
    per_ext = collections.defaultdict(collections.Counter)
    sizes = collections.defaultdict(lambda: [0, 0])
    unique_ps2 = {}
    unique_xbox = {}
    for arc in sorted(ps2):
        if arc not in xbox:
            print(f"{arc}: PS2 only")
            continue
        pe = read_bigf(ps2[arc])
        xe = {n.lower(): (o, s) for n, o, s in read_bigf(xbox[arc])}
        pnames = {xbox_name(n) for n, _, _ in pe}
        with open(ps2[arc], "rb") as pf, open(xbox[arc], "rb") as xf:
            for name, offset, size in pe:
                ext = extension(name)
                pdata = entry_data(pf, offset, size)
                key = xbox_name(name)
                if key not in xe:
                    per_ext[ext]["PS2 only"] += 1
                    unique_ps2[key] = pdata
                    if by_file:
                        print(f"{arc}: {name}: PS2 only ({size})")
                    continue
                xdata = entry_data(xf, *xe[key])
                if pdata == xdata:
                    verdict = "identical"
                else:
                    pu = unpacked(pdata)
                    xu = unpacked(xdata)
                    if pu == xu:
                        verdict = "identical unpacked"
                    elif len(pu) == len(xu):
                        same = sum(1 for a, b in zip(pu, xu) if a == b) if len(pu) < 1 << 20 else -1
                        verdict = "same size unpacked"
                        if by_file:
                            print(f"{arc}: {name}: same size {len(pu)}, {same} bytes equal")
                    else:
                        verdict = "different"
                        if by_file:
                            print(f"{arc}: {name}: {len(pu)} (PS2) vs {len(xu)} (Xbox)")
                    sizes[ext][0] += len(pu)
                    sizes[ext][1] += len(xu)
                per_ext[ext][verdict] += 1
            for name in xe:
                if name not in pnames:
                    ext = extension(name)
                    per_ext[ext]["Xbox only"] += 1
                    if by_file:
                        print(f"{arc}: {name}: Xbox only")
    for arc in sorted(xbox):
        if arc not in ps2:
            print(f"{arc}: Xbox only")
    columns = ["identical", "identical unpacked", "same size unpacked", "different", "PS2 only", "Xbox only"]
    print()
    print("| ext | " + " | ".join(columns) + " | differing, unpacked: PS2 / Xbox bytes |")
    print("|---|" + "---|" * (len(columns) + 1))
    total = collections.Counter()
    for ext in sorted(per_ext, key=lambda e: -sum(per_ext[e].values())):
        c = per_ext[ext]
        total.update(c)
        s = sizes[ext]
        print(f"| {ext} | " + " | ".join(str(c[k]) for k in columns) + f" | {s[0]:,} / {s[1]:,} |")
    print("| all | " + " | ".join(str(total[k]) for k in columns) + " | |")


def cmd_headers(ps2_dir, xbox_dir, ext):
    """The first 32 bytes (unpacked) of the first few files of one extension on both discs."""
    ps2 = archives(ps2_dir)
    xbox = archives(xbox_dir)
    shown = 0
    for arc in sorted(ps2):
        if arc not in xbox:
            continue
        xe = {n.lower(): (o, s) for n, o, s in read_bigf(xbox[arc])}
        with open(ps2[arc], "rb") as pf, open(xbox[arc], "rb") as xf:
            for name, offset, size in read_bigf(ps2[arc]):
                if extension(name) != ext or xbox_name(name) not in xe:
                    continue
                pu = unpacked(entry_data(pf, offset, size))
                xu = unpacked(entry_data(xf, *xe[xbox_name(name)]))
                print(f"{arc}: {name}  PS2 {len(pu)}  Xbox {len(xu)}")
                print("   PS2 ", pu[:32].hex(" "))
                print("   Xbox", xu[:32].hex(" "))
                shown += 1
                if shown >= 6:
                    return


def cmd_syms(path, pattern):
    """DRIVING.SYM: a table of (address, string offset) pairs, then the names."""
    data = open(path, "rb").read()
    count = struct.unpack_from("<I", data, 4)[0] // 8
    regex = re.compile(pattern, re.I) if pattern else None
    shown = 0
    for i in range(count):
        address, name_at = struct.unpack_from("<II", data, i * 8)
        name = data[name_at:data.index(b"\0", name_at)].decode("latin1")
        if regex is None or regex.search(name):
            print(f"{address:08x} {name}")
            shown += 1
    print(f"{shown} of {count} symbols", file=sys.stderr)


def paired_entries(ps2_dir, xbox_dir, wanted, archives_only=(".viv", ".mus", ".spe")):
    """(archive, PS2 name, PS2 data, Xbox data or None) for every PS2 entry `wanted(name)` accepts, unpacked."""
    ps2 = archives(ps2_dir)
    xbox = archives(xbox_dir)
    for arc in sorted(ps2):
        if not arc.endswith(archives_only) or arc not in xbox:
            continue
        xe = {n.lower(): (o, s) for n, o, s in read_bigf(xbox[arc])}
        with open(ps2[arc], "rb") as pf, open(xbox[arc], "rb") as xf:
            for name, offset, size in read_bigf(ps2[arc]):
                if not wanted(name.lower()):
                    continue
                pdata = unpacked(entry_data(pf, offset, size))
                xn = xbox_name(name)
                xdata = unpacked(entry_data(xf, *xe[xn])) if xn in xe else None
                yield arc, name.lower(), pdata, xdata


def entries_of(folder, wanted):
    """(archive, name, unpacked data) for every entry `wanted(name)` accepts, in one disc's archives."""
    for arc, path in sorted(archives(folder).items()):
        with open(path, "rb") as f:
            for name, offset, size in read_bigf(path):
                if wanted(name.lower()):
                    yield arc, name.lower(), unpacked(entry_data(f, offset, size))


# ---- data groups (UGroup): a 16-byte record {tag, flags, count, data}; a group's items are an array of records,
# its flags >> 5 subgroups first, then `count` data items; a group's data word counts 16-byte units from the
# group, an item's (flag 2) bytes from the item; flag 1 marks an indexed tag (type in the high half).

def tag_text(tag, flags):
    if flags & 1:
        return struct.pack(">H", tag >> 16).decode("latin1") + "%04x" % (tag & 0xFFFF)
    return struct.pack(">I", tag).decode("latin1")


def group_items(data, at=0, path=""):
    """[(path, payload size, count, payload offset or None)] for every data item under the group at `at`."""
    out = []
    tag, flags, count, word = struct.unpack_from("<IIII", data, at)
    items = at + word * 16
    groups = flags >> 5
    for i in range(groups + count):
        r = items + i * 16
        t, f, c, w = struct.unpack_from("<IIII", data, r)
        name = path + "/" + tag_text(t, f)
        if i < groups:
            out.extend(group_items(data, r, name))
        else:
            out.append((name, f >> 8, c, r + w if f & 2 else None))
    return out


def group_records(data):
    """{(path, occurrence): payload}"""
    seen = collections.Counter()
    out = {}
    for name, size, count, offset in group_items(data):
        seen[name] += 1
        out[name, seen[name]] = data[offset:offset + size] if offset is not None else b""
    return out


def record_type(path):
    """A record's type for the census: its group's tag and its own, an indexed tag's type only."""
    parent, own = path.rsplit("/", 1)
    return parent.split("/")[-1] + "/" + (own[:2] if len(own) == 6 else own)


def cmd_carp(ps2_dir, xbox_dir, specs):
    agg = collections.defaultdict(collections.Counter)
    for spec in specs:
        arc, name = spec
        got = [(p, x) for a, n, p, x in paired_entries(ps2_dir, xbox_dir, lambda n: n == name.lower())
               if a == arc.lower()]
        if not got:
            print(f"{arc}: {name}: not found")
            continue
        p, x = group_records(got[0][0]), group_records(got[0][1])
        for key in set(p) | set(x):
            a = agg[record_type(key[0])]
            if key in p and key in x:
                if p[key] == x[key]:
                    a["same"] += 1
                elif len(p[key]) == len(x[key]):
                    a["same size"] += 1
                    a["words differing"] += sum(1 for i in range(0, len(p[key]) - 3, 4)
                                                if p[key][i:i + 4] != x[key][i:i + 4])
                    a["words"] += len(p[key]) // 4
                else:
                    a["differ"] += 1
            elif key in p:
                a["PS2 only"] += 1
            else:
                a["Xbox only"] += 1
            a["PS2 bytes"] += len(p.get(key, b""))
            a["Xbox bytes"] += len(x.get(key, b""))
    columns = ["same", "same size", "differ", "PS2 only", "Xbox only", "PS2 bytes", "Xbox bytes"]
    print("| record | " + " | ".join(columns) + " | words differing in same-size records |")
    print("|---|" + "---|" * (len(columns) + 1))
    for t in sorted(agg, key=lambda t: -(agg[t]["PS2 bytes"] + agg[t]["Xbox bytes"])):
        a = agg[t]
        share = f"{a['words differing']:,} / {a['words']:,}" if a["words"] else ""
        print(f"| {t} | " + " | ".join(f"{a[c]:,}" for c in columns) + f" | {share} |")


# ---- EAGL objects: MIPS ELF relocatable files (eagl/Loader.cpp). A PS2 object in a .dat/.rel pair is one file cut
# in two (the .rel continues the .dat); a CARP file carries one as its ELFd/ELFr items.

def elf_symbols(image):
    """[(name, value, section index)] of an ELF image."""
    shoff = struct.unpack_from("<I", image, 0x20)[0]
    shnum = struct.unpack_from("<H", image, 0x30)[0]
    sections = [struct.unpack_from("<IIIIIIIIII", image, shoff + i * 40) for i in range(shnum)]
    out = []
    for s in sections:
        if s[1] != 2:   # SHT_SYMTAB
            continue
        strtab = sections[s[6]][4]
        for i in range(s[5] // 16):
            name_at, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", image, s[4] + i * 16)
            end = image.index(b"\0", strtab + name_at)
            out.append((image[strtab + name_at:end].decode("latin1"), value, shndx))
    return out


def eagl_objects(folder):
    """(archive, name, image) for every EAGL object on one disc: .o files, .dat files (with their .rel when the .dat
    alone is cut short) and the objects inside CARP files."""
    for arc, path in sorted(archives(folder).items()):
        if not arc.endswith(".viv"):
            continue
        entries = {n.lower(): (o, s) for n, o, s in read_bigf(path)}
        with open(path, "rb") as f:
            def load(n):
                return unpacked(entry_data(f, *entries[n]))
            for name in entries:
                if name.endswith(".o"):
                    yield arc, name, load(name)
                elif name.endswith(".dat"):
                    data = load(name)
                    if data[:4] != b"\x7fELF":
                        continue
                    shoff = struct.unpack_from("<I", data, 0x20)[0]
                    rel = name[:-4] + ".rel"
                    if shoff + 40 > len(data) and rel in entries:
                        data += load(rel)
                    yield arc, name, data
                elif name.endswith(".crp"):
                    data = load(name)
                    dat = None
                    for item, size, count, offset in group_items(data):
                        if item.endswith("ELFd"):
                            dat = data[offset:offset + size]
                        elif item.endswith("ELFr") and dat is not None:
                            image = dat
                            if struct.unpack_from("<I", dat, 0x20)[0] + 40 > len(dat):
                                image = dat + data[offset:offset + size]
                            yield arc, name, image
                            dat = None


def cmd_objects(ps2_dir, xbox_dir):
    for label, folder in (("PS2", ps2_dir), ("Xbox", xbox_dir)):
        classes = collections.Counter()
        methods = set()
        seen = set()
        for arc, name, image in eagl_objects(folder):
            if (name, len(image)) in seen:
                continue
            seen.add((name, len(image)))
            for symbol, value, shndx in elf_symbols(image):
                if symbol.startswith("__") and ":::" in symbol:
                    classes[symbol[2:symbol.index(":::")] + ("" if shndx else " (referenced)")] += 1
                elif symbol and shndx == 0 and "MicroCode" not in symbol and not symbol.startswith("event."):
                    methods.add(symbol)
        print(f"{label}: {len(seen)} distinct objects")
        for k, v in classes.most_common():
            print(f"  {v:7,} {k}")
        print(f"  render methods named but not defined ({len(methods)}): {', '.join(sorted(methods))}")


# ---- SHAPE containers (SHPS on the PS2, SHPX on the Xbox): magic, size, image count, directory id, then 8-byte
# {name, offset} entries from +0x10; an image starts with its type byte, then the next block's offset (24 bits),
# width and height (eagl/Realgraph.cpp).

def shape_images(data):
    count = struct.unpack_from("<I", data, 8)[0]
    out = {}
    for i in range(count):
        name, offset = struct.unpack_from("<4sI", data, 16 + i * 8)
        width, height = struct.unpack_from("<HH", data, offset + 4)
        out[name.decode("latin1")] = (data[offset], width, height)
    return out


def cmd_shapes(ps2_dir, xbox_dir):
    types = {"PS2": collections.Counter(), "Xbox": collections.Counter()}
    pairs = collections.Counter()
    for arc, name, p, x in paired_entries(ps2_dir, xbox_dir, lambda n: n.endswith(".ssh"), (".viv",)):
        pi = shape_images(p)
        for t, w, h in pi.values():
            types["PS2"][t] += 1
        if x is None:
            pairs["PS2 files without an Xbox twin"] += 1
            continue
        xi = shape_images(x)
        for t, w, h in xi.values():
            types["Xbox"][t] += 1
        for k in set(pi) & set(xi):
            pairs["images in both"] += 1
            if pi[k][1:] == xi[k][1:]:
                pairs["the same size"] += 1
            elif pi[k][1:] == xi[k][2:0:-1]:
                pairs["width and height swapped"] += 1
            else:
                pairs["another size"] += 1
        pairs["PS2 only"] += len(set(pi) - set(xi))
        pairs["Xbox only"] += len(set(xi) - set(pi))
    for label in ("PS2", "Xbox"):
        print(f"{label} image types: " + ", ".join(f"{t} (0x{t:02x}) x{n:,}" for t, n in sorted(types[label].items())))
    print("by name: " + ", ".join(f"{k} {v:,}" for k, v in pairs.items()))


# ---- EA sound: 'PT' patch attributes (snd/Banks.h tags), BNKl banks, SCxl streams

def patch_attributes(data, at):
    out = {}
    while at < len(data):
        tag = data[at]
        at += 1
        if tag == 0xFF:
            break
        if tag in (0xFC, 0xFD, 0xFE):
            continue
        n = data[at]
        at += 1
        out[tag] = int.from_bytes(data[at:at + n], "big")
        at += n
    return out


def bank_samples(data):
    count = struct.unpack_from("<H", data, 6)[0]
    out = []
    for i in range(count):
        offset = struct.unpack_from("<I", data, 0x14 + i * 4)[0]
        at = 0x14 + i * 4 + offset
        if offset and data[at:at + 2] == b"PT":
            out.append(patch_attributes(data, at + 4))
    return out


def cmd_banks(ps2_dir, xbox_dir):
    for label, folder in (("PS2", ps2_dir), ("Xbox", xbox_dir)):
        reps = collections.Counter()
        tags = collections.Counter()
        samples = 0
        for arc, name, data in entries_of(folder, lambda n: n.endswith(".bnk")):
            for attrs in bank_samples(data):
                samples += 1
                reps[attrs.get(0xA0, "absent")] += 1
                tags.update(attrs.keys())
        print(f"{label}: {samples:,} samples; sample rep (0xa0): {dict(reps)}; tags present: "
              + ", ".join(f"0x{t:02x} x{n}" for t, n in sorted(tags.items())))


def scl_blocks(data):
    out = []
    at = 0
    while at + 8 <= len(data):
        size = struct.unpack_from("<I", data, at + 4)[0]
        if size < 8:
            break
        out.append(data[at:at + size])
        at += size
    return out


def cmd_streams(ps2_dir, xbox_dir):
    result = collections.Counter()
    headers = collections.Counter()
    for arc, name, p, x in paired_entries(ps2_dir, xbox_dir, lambda n: n.endswith(".asf"), (".mus", ".spe")):
        if x is None:
            result["no Xbox twin"] += 1
            continue
        pb, xb = scl_blocks(p), scl_blocks(x)
        result["streams"] += 1
        result["blocks"] += len(pb)
        result["blocks identical"] += sum(1 for a, b in zip(pb, xb) if a == b)
        result["block counts differ"] += len(pb) != len(xb)
        pa, xa = patch_attributes(pb[0], 12), patch_attributes(xb[0], 12)
        headers[tuple(sorted((f"0x{k:02x}", pa.get(k), xa.get(k)) for k in set(pa) | set(xa)
                             if pa.get(k) != xa.get(k) and k != 0x85))] += 1
    print(", ".join(f"{k} {v:,}" for k, v in result.items()))
    print("header attributes that differ (tag, PS2, Xbox), sample counts aside:")
    for k, v in headers.most_common():
        print(f"  {v:4} streams: {k}")


# ---- GeoPrims (src/driving/ps2/Ps2Model.cpp): a PS2 GeoPrim is {render method, 0}; the method is a child of a PS2
# render method whose compiled program names the TARs and GeoPrimState that changed since the GeoPrim before it and,
# after a 0x04000002 word, a table of DMA CALL tags, one a variation, each calling a block that unpacks its colours
# and calls the geometry chain (header V4-32 with the vertex count, three V4-8 words of strip starts at three a
# vertex, positions V3-16 in 1/256ths offset by 0x8000 or float, texture coordinates V2-16 or float, ending in RET).
# An Xbox GeoPrim is {its RenderMethod child (parent at +0x2c), parameters}; the parameters of the opcode-9 packets
# are its streams.

class Elf:
    """An EAGL object with its relocations, unrelocated: pointers are (symbol name, section, offset)."""

    def __init__(self, image):
        self.image = image
        shoff = struct.unpack_from("<I", image, 0x20)[0]
        shnum = struct.unpack_from("<H", image, 0x30)[0]
        self.sections = [struct.unpack_from("<IIIIIIIIII", image, shoff + i * 40) for i in range(shnum)]
        self.symbols = []
        self.relocations = {}
        for s in self.sections:
            if s[1] == 2:
                strtab = self.sections[s[6]][4]
                for i in range(s[5] // 16):
                    name_at, value, size, info, other, shndx = struct.unpack_from("<IIIBBH", image, s[4] + i * 16)
                    end = image.index(b"\0", strtab + name_at)
                    self.symbols.append((image[strtab + name_at:end].decode("latin1"), value, shndx))
        for s in self.sections:
            if s[1] == 9:
                for i in range(s[5] // 8):
                    offset, info = struct.unpack_from("<II", image, s[4] + i * 8)
                    self.relocations[s[7], offset] = info >> 8

    def u32(self, section, offset):
        return struct.unpack_from("<I", self.image, self.sections[section][4] + offset)[0]

    def at(self, section, offset):
        return self.sections[section][4] + offset

    def ptr(self, section, offset):
        r = self.relocations.get((section, offset))
        if r is None:
            return None
        name, value, shndx = self.symbols[r]
        return name, shndx, value + self.u32(section, offset)

    def models(self):
        return [s for s in self.symbols if s[0].startswith("__Model:::") and s[2]]

    def draw_list(self, model):
        """(the list's place, [GeoPrim (section, offset)]) - WalkDrawList over every block"""
        p = self.ptr(model[2], model[1] + 0xcc)
        if p is None:
            return None, []
        section, at = p[1], p[2]
        start = (section, at)
        limit = self.u32(section, at) & 0xffff
        at += 4
        entry, out = limit, []
        while True:
            if entry == 0xffff:
                g = self.ptr(section, at)
                if g:
                    out.append((g[1], g[2]))
                at += 4
            else:
                at += 8
            entry = self.u32(section, at) & 0xffff
            at += 4
            if not entry > limit:
                return start, out


UNPACK_SIZES = {0x4: 8, 0x5: 4, 0x8: 12, 0x9: 6, 0xa: 3, 0xc: 16, 0xd: 8, 0xe: 4}


def vif_unpacks(image, at, end):
    """[(format, count, VU address and flags, masked, data offset)] of the VIF codes from at to end"""
    out = []
    while at + 4 <= end:
        code = struct.unpack_from("<I", image, at)[0]
        at += 4
        command, count, immediate = code >> 24 & 0x7f, code >> 16 & 0xff, code & 0xffff
        if command & 0x60 == 0x60:
            n = count or 256
            out.append((command & 0xf, n, immediate, bool(command & 0x10), at))
            at += (n * UNPACK_SIZES.get(command & 0xf, 2) + 3) & ~3
        elif command == 0x20:
            at += 4
        elif command in (0x30, 0x31):
            at += 16
        elif command in (0x50, 0x51):
            at += immediate * 16
    return out


def ps2_geoprim(elf, section, offset):
    """(method, vertex count, strips, positions, texture coordinates, variations) of a PS2 GeoPrim, or None"""
    record = elf.ptr(section, offset)
    method = elf.ptr(record[1], record[2] + 8)
    program = elf.ptr(record[1], record[2])
    table = None
    for k in range(400):
        if elf.u32(program[1], program[2] + 4 * k) == 0x04000002:
            table = elf.ptr(program[1], program[2] + 4 * k + 4)
            break
    if table is None:
        return None
    variations, chain = 0, None
    while (elf.u32(table[1], table[2] + 16 * variations) >> 28 & 7) == 5:
        block = elf.ptr(table[1], table[2] + 16 * variations + 4)
        if chain is None:
            chain = elf.ptr(block[1], block[2] + 4) if elf.u32(block[1], block[2]) >> 28 & 7 == 5 else block
        variations += 1
    unpacks, at = [], chain[2]
    while True:
        tag = elf.u32(chain[1], at)
        unpacks += vif_unpacks(elf.image, elf.at(chain[1], at + 8), elf.at(chain[1], at + 16 + (tag & 0xffff) * 16))
        if tag >> 28 & 7 != 1:
            break
        at += 16 + (tag & 0xffff) * 16
    count = struct.unpack_from("<I", elf.image, unpacks[0][4])[0]
    starts = b"".join(elf.image[u[4]:u[4] + 4] for u in unpacks[1:4])
    cuts = [0]
    for b in starts:
        if b == 0:
            break
        cuts.append(min(b // 3, count))
    cuts.append(count)
    positions = texcoords = None
    for f, n, imm, masked, data in unpacks[4:]:
        if positions is None and f in (0x9, 0x8, 0xc):
            if f == 0x9:
                raw = struct.unpack_from("<%dH" % (3 * n), elf.image, data)
                positions = [tuple((raw[3 * i + k] - 0x8000) / 256 for k in range(3)) for i in range(n)]
            else:
                step = 16 if f == 0xc else 12
                positions = [struct.unpack_from("<3f", elf.image, data + step * i) for i in range(n)]
        elif positions is not None and texcoords is None and f in (0x5, 0x4):
            texcoords = ([struct.unpack_from("<2H", elf.image, data + 4 * i) for i in range(n)] if f == 0x5 else
                         [struct.unpack_from("<2f", elf.image, data + 8 * i) for i in range(n)])
    return method[0], count, list(zip(cuts, cuts[1:])), positions, texcoords, variations


def xbox_geoprim(elf, section, offset):
    """(parent method, index count, positions) of an Xbox GeoPrim"""
    method = elf.ptr(section, offset)
    parent = elf.ptr(method[1], method[2] + 0x2c)
    packets = elf.ptr(method[1], method[2])
    at, param, indices, positions = packets[2], 0, 0, None
    while True:
        header = elf.u32(packets[1], at)
        if header == 0:
            break
        op, length = header >> 16, header & 0xffff
        if op not in (4, 25, 26, 36):
            count = elf.u32(section, offset + 4 + 8 * param)
            data = elf.ptr(section, offset + 8 + 8 * param)
            if op == 2:
                indices = elf.u32(data[1], data[2])
            elif op == 9 and elf.u32(packets[1], at + 8) == 0 and elf.u32(packets[1], at + 12) == 12:
                positions = [struct.unpack_from("<3f", elf.image, elf.at(data[1], data[2]) + 12 * i)
                             for i in range(count)]
            param += 1
        at += 4 * length
    return parent[0] if parent else None, indices, positions


def bounds(points):
    return [min(p[k] for p in points) for k in range(3)] + [max(p[k] for p in points) for k in range(3)]


def carp_object(data):
    image = None
    for item, size, count, offset in group_items(data):
        if item.endswith("ELFd"):
            image = data[offset:offset + size]
        elif item.endswith("ELFr") and image is not None:
            if struct.unpack_from("<I", image, 0x20)[0] + 40 > len(image):
                image += data[offset:offset + size]
            return Elf(image)
    return None


def cmd_geoprims(ps2_dir, xbox_dir, specs):
    for arc, name in specs:
        got = [(p, x) for a, n, p, x in paired_entries(ps2_dir, xbox_dir, lambda n: n == name.lower())
               if a == arc.lower()]
        if not got:
            print(f"{arc}: {name}: not found")
            continue
        pe, xe = carp_object(got[0][0]), carp_object(got[0][1])
        xbox_models = {m[0]: m for m in xe.models()}
        stats = collections.Counter()
        pairs = collections.Counter()
        texcoord_ranges = collections.defaultdict(lambda: [1 << 30, -(1 << 30)])
        worst = []
        lists = set()
        for model in pe.models():
            start, prims = pe.draw_list(model)
            if start is None or start in lists:
                continue
            lists.add(start)
            stats["models"] += 1
            ps2_points = []
            methods = []
            for g in prims:
                prim = ps2_geoprim(pe, *g)
                if prim is None:
                    stats["PS2 GeoPrims without a table"] += 1
                    continue
                method, count, strips, positions, texcoords, variations = prim
                methods.append(method)
                stats["PS2 GeoPrims"] += 1
                stats["PS2 vertices"] += count
                stats["PS2 triangles"] += sum(max(0, b - a - 2) for a, b in strips)
                stats["PS2 variations"] += variations
                ps2_points += positions or []
                if texcoords and isinstance(texcoords[0][0], int):
                    r = texcoord_ranges[method]
                    r[0] = min(r[0], min(min(t) for t in texcoords) - 0x8000)
                    r[1] = max(r[1], max(max(t) for t in texcoords) - 0x8000)
            twin = xbox_models.get(model[0])
            if twin is None:
                stats["models without an Xbox twin"] += 1
                continue
            xbox_points = []
            xprims = [xbox_geoprim(xe, *g) for g in xe.draw_list(twin)[1]]
            for parent, indices, positions in xprims:
                stats["Xbox GeoPrims"] += 1
                stats["Xbox vertices"] += len(positions or [])
                xbox_points += positions or []
            for method in set(methods):
                for parent in set(p[0] for p in xprims):
                    pairs[method, parent] += 1
            if ps2_points and xbox_points:
                d = max(abs(a - b) for a, b in zip(bounds(ps2_points), bounds(xbox_points)))
                stats["models with both bounds"] += 1
                stats["bounds within 0.05"] += d < 0.05
                worst.append((d, model[0]))
        print(f"{arc} {name}:")
        for k, v in stats.items():
            print(f"  {k}: {v:,}")
        worst.sort(reverse=True)
        print("  largest bounds differences: " + ", ".join(f"{d:.3f} {n[10:50]}" for d, n in worst[:3]))
        print("  PS2 method -> Xbox parent methods of the same models (models):")
        for (method, parent), v in pairs.most_common():
            print(f"    {v:5}  {method} -> {parent}")
        print("  16-bit texture coordinates less 0x8000, by method (min, max):")
        for method, r in sorted(texcoord_ranges.items()):
            print(f"    {method}: {r[0]}, {r[1]}")


def main(argv):
    if len(argv) >= 2 and argv[0] == "list":
        cmd_list(argv[1])
    elif len(argv) >= 3 and argv[0] == "compare":
        cmd_compare(argv[1], argv[2], "--by-file" in argv)
    elif len(argv) >= 4 and argv[0] == "headers":
        cmd_headers(argv[1], argv[2], argv[3].lower())
    elif len(argv) >= 2 and argv[0] == "syms":
        cmd_syms(argv[1], argv[2] if len(argv) > 2 else None)
    elif len(argv) >= 5 and len(argv) % 2 == 1 and argv[0] == "carp":
        cmd_carp(argv[1], argv[2], list(zip(argv[3::2], argv[4::2])))
    elif len(argv) >= 5 and len(argv) % 2 == 1 and argv[0] == "geoprims":
        cmd_geoprims(argv[1], argv[2], list(zip(argv[3::2], argv[4::2])))
    elif len(argv) == 3 and argv[0] in ("objects", "shapes", "banks", "streams"):
        {"objects": cmd_objects, "shapes": cmd_shapes, "banks": cmd_banks, "streams": cmd_streams}[argv[0]](
            argv[1], argv[2])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
