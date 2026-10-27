"""Offline checks of the GameCube US release's driving models against the US Xbox disc's (docs/gc-driving.md).

    python tools/gcdriving_check.py geoprims <gc driving dir> <xbox driving dir> [archive file.crp ...]
    python tools/gcdriving_check.py methods <gc driving dir>
    python tools/gcdriving_check.py anims <gc driving dir> <xbox driving dir>

geoprims decodes every CARP file's GameCube GeoPrims as src/driving/gc/GcModel.cpp does (or the files named) and
compares them with the Xbox twins' models of the same name: GeoPrim and vertex counts, how many models' bounds agree,
the Xbox parent methods each GameCube method's models use (the mapping table's evidence), the texture coordinates'
scale against the Xbox's and the vertex colours against the Xbox's, matched by vertex position. methods lists every
render method the GameCube's objects (CARP files and actors) name, with the display lists' indices per vertex and
primitive kinds. anims turns every animation bank round as src/driving/gc/GcObject.cpp does (over the file, passing
the relocation sites) and compares the bytes it touched with the Xbox twin's, by anim type. The C++ checker,
gcdrive_check, does the rest (data groups, images, banks).

Both discs keep the driving engine's files in EA BIGF archives whose entries may be RefPack-packed; the readers
follow the extractor project's common/extraction/extract_bigf.py (Q:\\nightfire-re). The GameCube's data groups are
big-endian with their flag bit-fields from the top bit down (src/driving/gc/GcUData.h); its EAGL objects are the
Xbox's little-endian ELF container with big-endian data around little-endian relocation sites. Reads only; writes
nothing.
"""

import collections
import os
import struct
import sys

ARCHIVES = ["misc.viv", "race.viv", "mis01.viv", "mis3.viv", "mis4.viv", "mis11.viv", "mis13a.viv", "mis13b.viv",
            "mis13c.viv"]

# Each GameCube render method's fields, in its *_FieldNames order (the GameCube's driving.elf registers them;
# BondCarGlass's from its GeoPrims, as no registration names it): the ones read here
FIELDS = {
    "BondTexturePacked": "Parameters ModelViewMatrix ViewMatrix Coordinates Colours UV DisplayList Texture "
                         "LightSources State",
    "BondTexture": "Parameters ModelViewMatrix ViewMatrix Coordinates Colours UV DisplayList Texture LightSources "
                   "State",
    "BondReflectTexturePacked": "Parameters ModelViewMatrix ModelMatrix ViewMatrix Coordinates Colours UV DisplayList "
                                "Texture EnvironmentMap CameraPos LightSources State",
    "BondDynamicTexture": "Parameters ModelViewMatrix ViewMatrix Coordinates Normals Colours UV DisplayList Texture "
                          "LightSources LightingProperties AmbientDiffuse State",
    "BondScrollTex1": "Parameters ModelViewMatrix ScrollUV SimStep Coordinates Colours UV DisplayList Texture State "
                      "Stage0",
    "BondScrollTex2": "Parameters ModelViewMatrix ScrollUV ScrollUV1 SimStep Coordinates Colours UV DisplayList "
                      "Texture Texture1 State Stage0 Stage1",
    "BondScrollTex3": "Parameters ModelViewMatrix ScrollUV ScrollUV1 ScrollUV2 SimStep Coordinates Colours UV "
                      "DisplayList Texture Texture1 Texture2 State Stage0 Stage1 Stage2",
    "BondFrameAnim": "Parameters ModelViewMatrix FrameAnim SimStep Coordinates Colours UV DisplayList Texture State",
    "BondUnderWater": "Parameters ModelViewMatrix ViewMatrix Coordinates Colours UV CausticData DisplayList Texture "
                      "CausticControl LightSources State",
    "BondUnderWaterPacked": "Parameters ModelViewMatrix ModelMatrix ViewMatrix Coordinates Colours UV CausticData "
                            "DisplayList Texture EnvironmentMap CameraPos CausticControl LightSources State",
    "BondWater": "Parameters ModelViewMatrix ModelMatrix SimStep UVScale1 UVScale2 UVScale3 UVScale4 UVScroll1 "
                 "UVScroll2 UVScroll3 UVScroll4 Coordinates Colours UV DisplayList Texture Texture1 Texture2 Texture3 "
                 "WaterRotToSun CameraPos State Stage0 Stage1 Stage2 Stage3",
    "BondKelp": "Parameters ModelViewMatrix ViewMatrix Coordinates Colours UV Normals DisplayList Texture LightSources "
                "State SwayData SimStep",
    "BondGouraud": "Parameters ModelViewMatrix Coordinates Colours DisplayList State",
    "BondCarTexture": "Parameters ModelViewMatrix ViewMatrix ModelMatrix Coordinates Normals UV DmgVec DmgNrm "
                      "DamageZones DmgZone DmgLen DisplayList Texture SphereMap SpecularMap SpecularMat "
                      "ReflectionInfo2 LightingProperties AmbientDiffuse State LightSources",
    "BondCarTextureDull": "Parameters ModelViewMatrix ViewMatrix Coordinates Normals UV DmgVec DmgNrm DamageZones "
                          "DmgZone DmgLen DisplayList Texture State LightSources LightingProperties AmbientDiffuse",
    "BondCarTextureSwitch": "Parameters ModelViewMatrix ViewMatrix Coordinates Normals UV DmgVec DmgNrm DamageZones "
                            "DmgZone DmgLen DisplayList Texture State LightSources LightingProperties AmbientDiffuse "
                            "SwitchParam SwitchData",
    "BondCarTextureNoDamage": "Parameters ModelViewMatrix ViewMatrix ModelMatrix Coordinates Normals UV DisplayList "
                              "Texture SphereMap SpecularMap SpecularMat ReflectionInfo2 LightingProperties "
                              "AmbientDiffuse State LightSources",
    "BondCarGlass": "Parameters ModelViewMatrix ViewMatrix ModelMatrix Coordinates Normals UV DmgVec DmgNrm "
                    "DamageZones DmgZone DmgLen DisplayList Texture DamageMap SphereMap SpecularMap SpecularMat "
                    "ReflectionInfo2 LightingProperties AmbientDiffuse State LightSources",
    "BondCarGouraudDull": "Parameters ModelViewMatrix ViewMatrix Coordinates Normals DmgVec DmgNrm DamageZones DmgZone "
                          "DmgLen ColourSlot ColourSet DisplayList State LightSources LightingProperties "
                          "AmbientDiffuse",
}
for name, base in [("BondScrollTex1Packed", "BondScrollTex1"), ("BondScrollTex2Packed", "BondScrollTex2"),
                   ("BondScrollTex3Packed", "BondScrollTex3"), ("BondFrameAnimPacked", "BondFrameAnim"),
                   ("BondWaterPacked", "BondWater"), ("BondGouraudPacked", "BondGouraud"),
                   ("BondCarTexturePacked", "BondCarTexture"), ("BondCarChrome", "BondCarTexture"),
                   ("BondCarTextureNoDamagePacked", "BondCarTextureNoDamage"),
                   ("BondReflectTexture", "BondReflectTexturePacked")]:
    FIELDS[name] = FIELDS[base]
FIELDS = {k: v.split() for k, v in FIELDS.items()}


def be32(b, o): return struct.unpack_from(">I", b, o)[0]
def le32(b, o): return struct.unpack_from("<I", b, o)[0]
def be16(b, o): return struct.unpack_from(">H", b, o)[0]
def le16(b, o): return struct.unpack_from("<H", b, o)[0]


# ---- archives

def read_bigf(path):
    with open(path, "rb") as f:
        header = f.read(16)
        count, dir_size = struct.unpack_from(">II", header, 8)
        f.seek(0)
        directory = f.read(dir_size + 16)
    entries, p = [], 16
    for _ in range(count):
        offset, size = struct.unpack_from(">II", directory, p)
        p += 8
        end = directory.index(b"\0", p)
        entries.append((directory[p:end].decode("latin1"), offset, size))
        p = end + 1
    return entries


def unrefpack(data):
    if len(data) < 5 or data[0] not in (0x10, 0x11) or data[1] != 0xFB:
        return data
    size = (data[2] << 16) | (data[3] << 8) | data[4]
    out = bytearray(size)
    src, dst, n = (5 if data[0] == 0x10 else 8), 0, len(data)
    while src < n:
        b0 = data[src]
        if b0 < 0x80:
            b1 = data[src + 1]; src += 2; lit = b0 & 3
            out[dst:dst + lit] = data[src:src + lit]; src += lit; dst += lit
            dist = ((b0 & 0x60) << 3) + b1 + 1; run = ((b0 >> 2) & 7) + 3
        elif b0 < 0xC0:
            b1, b2 = data[src + 1], data[src + 2]; src += 3; lit = b1 >> 6
            out[dst:dst + lit] = data[src:src + lit]; src += lit; dst += lit
            dist = ((b1 & 0x3F) << 8) + b2 + 1; run = (b0 & 0x3F) + 4
        elif b0 < 0xE0:
            b1, b2, b3 = data[src + 1], data[src + 2], data[src + 3]; src += 4; lit = b0 & 3
            out[dst:dst + lit] = data[src:src + lit]; src += lit; dst += lit
            dist = ((b0 & 0x10) << 12) + (b1 << 8) + b2 + 1; run = ((b0 & 0x0C) << 6) + b3 + 5
        else:
            lit = (b0 & 0x1F) * 4 + 4; src += 1
            if lit > 0x70:
                lit = b0 & 3; out[dst:dst + lit] = data[src:src + lit]; break
            out[dst:dst + lit] = data[src:src + lit]; src += lit; dst += lit
            continue
        start = dst - dist
        for i in range(run):
            out[dst + i] = out[start + i]
        dst += run
    return bytes(out)


class Disc:
    def __init__(self, folder):
        self.folder = folder
        self.dirs = {}

    def entries(self, archive):
        if archive not in self.dirs:
            path = os.path.join(self.folder, archive)
            self.dirs[archive] = read_bigf(path) if os.path.exists(path) else []
        return self.dirs[archive]

    def load(self, archive, name):
        for n, o, s in self.entries(archive):
            if n.lower() == name.lower():
                with open(os.path.join(self.folder, archive), "rb") as f:
                    f.seek(o)
                    return unrefpack(f.read(s))
        return None


# ---- data groups

def items(data, gc, offset=0, out=None):
    """the data records of a group tree: [(tag text, payload offset, size)]"""
    out = [] if out is None else out
    rd = be32 if gc else le32
    word, count, first = rd(data, offset + 4), rd(data, offset + 8), rd(data, offset + 12)
    groups = (word & 0x07ffffff) if gc else word >> 5
    base = offset + first * 16
    for i in range(groups):
        items(data, gc, base + i * 16, out)
    for i in range(count):
        at = base + (groups + i) * 16
        tag, w, d = rd(data, at), rd(data, at + 4), rd(data, at + 12)
        relative = (w & 0x40000000) if gc else (w & 2)
        size = (w & 0xffffff) if gc else w >> 8
        out.append((tag.to_bytes(4, "big").decode("latin1"), at + d if relative else 0, size))
    return out


# ---- EAGL objects

class Elf:
    """An EAGL ELF object, its two halves joined; pointers are relocation sites, read as (section, offset) or
    ("ext", name)"""

    def __init__(self, image):
        self.d = image
        shoff, shnum = le32(image, 0x20), le16(image, 0x30)
        self.secs = [struct.unpack_from("<10I", image, shoff + i * 40) for i in range(shnum)]
        self.syms, self.rel = [], {}
        for s in self.secs:
            if s[1] == 2:
                strs = self.secs[s[6]][4]
                for j in range(s[5] // 16):
                    nm, val, size, info, other, shndx = struct.unpack_from("<IIIBBH", image, s[4] + j * 16)
                    name = image[strs + nm:image.index(b"\0", strs + nm)].decode("latin1")
                    self.syms.append((name, val, shndx))
        for s in self.secs:
            if s[1] == 9:
                for j in range(s[5] // 8):
                    off, info = struct.unpack_from("<II", image, s[4] + j * 8)
                    self.rel[(s[7], off)] = info >> 8

    def at(self, sec, off):
        return self.secs[sec][4] + off

    def ptr(self, sec, off):
        r = self.rel.get((sec, off))
        if r is None:
            return None
        name, value, shndx = self.syms[r]
        if not shndx or shndx >= len(self.secs):
            return ("ext", name)
        return (shndx, value + le32(self.d, self.at(sec, off)))

    def models(self):
        return [(n[10:], v, s) for n, v, s in self.syms if n.startswith("__Model:::") and s and s < len(self.secs)]

    def draw_list(self, model, gc):
        """the list's place and its GeoPrims, as WalkDrawList reads it"""
        p = self.ptr(model[2], model[1] + 0xcc)
        if p is None or p[0] == "ext":
            return None, []
        sec, at = p
        rd = be32 if gc else le32
        limit = rd(self.d, self.at(sec, at)) & 0xffff
        at += 4
        entry, out = limit, []
        while True:
            if entry == 0xffff:
                g = self.ptr(sec, at)
                if g and g[0] != "ext":
                    out.append(g)
                at += 4
            else:
                at += 8
            entry = rd(self.d, self.at(sec, at)) & 0xffff
            at += 4
            if not entry > limit:
                return p, out


def carp_object(data, gc):
    dat = rel = b""
    for tag, payload, size in items(data, gc):
        if tag == "ELFd":
            dat = data[payload:payload + size]
        elif tag == "ELFr":
            rel = data[payload:payload + size]
    if not dat:
        return None
    if rel and le32(dat, 0x20) + 40 > len(dat):
        dat += rel
    return Elf(dat)


def gc_geoprim(e, sec, off):
    """(method, {field: (count, pointer)}) of a GameCube GeoPrim"""
    m = e.ptr(sec, off)
    if m is None or m[0] != "ext":
        return None, None
    fields = FIELDS.get(m[1])
    if fields is None:
        return m[1], None
    out = {}
    for i, name in enumerate(fields):
        out[name] = (be32(e.d, e.at(sec, off + 8 + i * 8)), e.ptr(sec, off + 12 + i * 8))
    return m[1], out


def gc_array(e, field, fmt):
    count, p = field
    if p is None or p[0] == "ext":
        return []
    size = struct.calcsize(fmt)
    a = e.at(*p)
    return [struct.unpack_from(fmt, e.d, a + i * size) for i in range(count)]


def gc_positions(e, method, fields):
    if method.endswith("Packed"):
        return [tuple(c / 256.0 for c in p) for p in gc_array(e, fields["Coordinates"], ">3h")]
    return gc_array(e, fields["Coordinates"], ">3f")


def parse_display_list(data, attributes, vertices):
    """[(command, [first index of each vertex])] or None, as GcModel.cpp parses it"""
    p, out = 0, []
    while p < len(data):
        if data[p] == 0:
            p += 1
            continue
        kind = data[p] & 0xf8
        if kind not in (0x98, 0x90) or p + 3 > len(data):
            return None
        count = be16(data, p + 1)
        p += 3
        if p + count * attributes * 2 > len(data):
            return None
        indices = []
        for v in range(count):
            t = [be16(data, p + (v * attributes + a) * 2) for a in range(attributes)]
            if any(i >= vertices for i in t):
                return None
            indices.append(t)
        p += count * attributes * 2
        out.append((kind, indices))
    return out or None


def xbox_geoprim(e, sec, off):
    """(parent method, [(element size, stream bytes)]) of an Xbox GeoPrim"""
    method = e.ptr(sec, off)
    parent = e.ptr(method[0], method[1] + 0x2c)
    packets = e.ptr(method[0], method[1])
    at, param, streams = packets[1], 0, []
    while True:
        header = le32(e.d, e.at(packets[0], at))
        if header == 0:
            break
        op, length = header >> 16, header & 0xffff
        if op not in (4, 25, 26, 36):
            count = le32(e.d, e.at(sec, off + 4 + 8 * param))
            data = e.ptr(sec, off + 8 + 8 * param)
            if op == 9 and data and data[0] != "ext":
                size = le32(e.d, e.at(packets[0], at + 12))
                a = e.at(*data)
                streams.append((size, e.d[a:a + count * size]))
            param += 1
        at += 4 * length
    return (parent[1] if parent and parent[0] == "ext" else None), streams


def bounds(points):
    return [min(p[k] for p in points) for k in range(3)] + [max(p[k] for p in points) for k in range(3)]


def cmd_geoprims(gc_dir, xbox_dir, specs):
    gc, xbox = Disc(gc_dir), Disc(xbox_dir)
    if not specs:
        seen = set()
        for a in ARCHIVES:
            for n, _, _ in gc.entries(a):
                if n.lower().endswith(".crp") and n.lower() not in seen:
                    seen.add(n.lower())
                    specs += [a, n]
    stats, pairs = collections.Counter(), collections.Counter()
    uv_ratio, colours = collections.defaultdict(collections.Counter), collections.defaultdict(collections.Counter)
    worst = []
    for arc, name in zip(specs[::2], specs[1::2]):
        gd, xd = gc.load(arc, name), xbox.load(arc, name)
        if gd is None or xd is None:
            continue
        ge, xe = carp_object(gd, True), carp_object(xd, False)
        if ge is None or xe is None:
            continue
        twins = {m[0]: m for m in xe.models()}
        lists = set()
        for model in ge.models():
            start, prims = ge.draw_list(model, True)
            if start is None or start in lists:
                continue
            lists.add(start)
            stats["models"] += 1
            points, by_position, methods = [], {}, set()
            for g in prims:
                method, fields = gc_geoprim(ge, *g)
                if fields is None:
                    stats["GeoPrims of a method with no field list here"] += 1
                    continue
                methods.add(method)
                stats["GC GeoPrims"] += 1
                pos = gc_positions(ge, method, fields)
                stats["GC vertices"] += len(pos)
                points += pos
                uv = gc_array(ge, fields["UV"], ">2h") if "UV" in fields else []
                if "Colours" in fields:
                    if method.endswith("Packed"):
                        cols = [((c >> 11) * 255 // 31, (c >> 5 & 63) * 255 // 63, (c & 31) * 255 // 31)
                                for (c,) in gc_array(ge, fields["Colours"], ">H")]
                    else:
                        cols = [c[:3] for c in gc_array(ge, fields["Colours"], ">4B")]
                else:
                    cols = []
                for i, p in enumerate(pos):
                    by_position.setdefault(tuple(round(c, 2) for c in p),
                                           (method, uv[i] if i < len(uv) else None, cols[i] if i < len(cols) else None))
            twin = twins.get(model[0])
            if twin is None:
                stats["models without an Xbox twin"] += 1
                continue
            xpoints = []
            for g in xe.draw_list(twin, False)[1]:
                parent, streams = xbox_geoprim(xe, *g)
                stats["Xbox GeoPrims"] += 1
                for m in methods:
                    pairs[m, parent] += 1
                pos = next((s for s in streams if s[0] == 12), None)
                if pos is None:
                    continue
                vertices = [struct.unpack_from("<3f", pos[1], i * 12) for i in range(len(pos[1]) // 12)]
                xpoints += vertices
                rest = [s for s in streams if s is not pos]
                uvs = next((s for s in rest if s[0] in (4, 8)), None)
                cols = next((s for s in rest if s[0] == 4 and s is not uvs), None)
                for i, v in enumerate(vertices):
                    found = by_position.get(tuple(round(c, 2) for c in v))
                    if found is None:
                        continue
                    method, guv, gcol = found
                    if uvs and guv and (i + 1) * uvs[0] <= len(uvs[1]):
                        xuv = (struct.unpack_from("<2h", uvs[1], i * 4) if uvs[0] == 4 else
                               tuple(f * 256 for f in struct.unpack_from("<2f", uvs[1], i * 8)))
                        for k in range(2):
                            if xuv[k] and guv[k]:
                                uv_ratio[method][round(guv[k] / xuv[k], 2)] += 1
                    if cols and gcol and (i + 1) * 4 <= len(cols[1]):
                        b, gr, r, a = struct.unpack_from("<4B", cols[1], i * 4)
                        colours[(method, parent)][tuple(round(x / max(1, y), 1) for x, y in zip((r, gr, b), gcol))] += 1
            if points and xpoints:
                d = max(abs(a - b) for a, b in zip(bounds(points), bounds(xpoints)))
                stats["models with both bounds"] += 1
                stats["bounds within 0.05"] += d < 0.05
                worst.append((d, model[0]))
    for k, v in stats.items():
        print(f"  {k}: {v:,}")
    worst.sort(reverse=True)
    print("  largest bounds differences: " + ", ".join(f"{d:.3f} {n[:40]}" for d, n in worst[:3]))
    print("  GameCube method -> Xbox parent methods of the same models (models):")
    for (method, parent), v in sorted(pairs.items(), key=lambda kv: (kv[0][0], -kv[1])):
        print(f"    {v:5}  {method} -> {parent}")
    print("  16-bit texture coordinates over the Xbox's in 1/256ths, by method (the commonest ratios):")
    for method, c in sorted(uv_ratio.items()):
        print(f"    {method}: " + ", ".join(f"{r} x{n}" for r, n in c.most_common(3)))
    print("  vertex colours, the Xbox's over the GameCube's per channel (the commonest ratios):")
    for key, c in sorted(colours.items(), key=lambda kv: str(kv[0])):
        print(f"    {key[0]} -> {key[1]}: " + ", ".join(f"{r} x{n}" for r, n in c.most_common(3)))


def cmd_methods(gc_dir):
    gc = Disc(gc_dir)
    methods, kinds, attributes = collections.Counter(), collections.Counter(), collections.Counter()
    seen = set()
    for a in ARCHIVES:
        for n, _, _ in gc.entries(a):
            ext = os.path.splitext(n)[1].lower()
            if n.lower() in seen or ext not in (".crp", ".dat"):
                continue
            seen.add(n.lower())
            data = gc.load(a, n)
            if ext == ".crp":
                e = carp_object(data, True)
            elif data[:4] == b"\x7fELF":
                rel = gc.load(a, n[:-4] + ".rel") or b""
                e = Elf(data + rel if le32(data, 0x20) + 40 > len(data) else data)
            else:
                continue
            if e is None:
                continue
            prims, lists = set(), set()
            for model in e.models():
                start, ps = e.draw_list(model, True)
                if start is None or start in lists:
                    continue
                lists.add(start)
                for g in ps:
                    if g in prims:
                        continue
                    prims.add(g)
                    method, fields = gc_geoprim(e, *g)
                    if method is None:
                        continue
                    methods[method] += 1
                    if fields is None or "DisplayList" not in fields or "Coordinates" not in fields:
                        continue
                    count, p = fields["DisplayList"]
                    if p is None or p[0] == "ext":
                        continue
                    dl = e.d[e.at(*p):e.at(*p) + count]
                    nv = fields["Coordinates"][0]
                    k = next((k for k in range(1, 7) if parse_display_list(dl, k, nv)), None)
                    attributes[(method, k)] += 1
                    for kind, _ in parse_display_list(dl, k, nv) or []:
                        kinds[kind] += 1
    for m, v in sorted(methods.items()):
        print(f"  {v:6}  {m}")
    print("  indices per vertex, by method:")
    for (m, k), v in sorted(attributes.items(), key=lambda kv: (kv[0][0], str(kv[0][1]))):
        print(f"  {v:6}  {m}: {k}")
    print("  primitives: " + ", ".join(f"0x{k:02x} x{v}" for k, v in sorted(kinds.items())))


class AnimSwapper:
    """GcObject.cpp's animation bank conversion over a file's bytes: each word or halfword turned round once, never a
    relocation site; the bytes it touched recorded, by anim type, to be compared with the Xbox twin's"""

    def __init__(self, e):
        self.e, self.d = e, bytearray(e.d)
        self.sites = set(e.secs[s][4] + off for (s, off) in e.rel)
        self.done, self.touched = set(), collections.defaultdict(set)

    def word(self, a, kind):
        self.touched[kind].update(range(a, a + 4))
        if a in self.sites or a in self.done:
            return
        self.done.add(a)
        self.d[a:a + 4] = self.d[a:a + 4][::-1]

    def half(self, a, kind):
        self.touched[kind].update(range(a, a + 2))
        if a in self.done:
            return
        self.done.add(a)
        self.d[a:a + 2] = self.d[a:a + 2][::-1]

    def target(self, a):
        """the address a relocation site at a points at"""
        for (s, off), sym in self.e.rel.items():
            if self.e.secs[s][4] + off == a:
                p = self.e.ptr(s, off)
                return None if p is None or p[0] == "ext" else self.e.at(*p)
        return None

    def anim(self, a, depth=0):
        if a is None or depth > 8 or a in self.done:
            return
        kind = be16(self.d, a)
        self.half(a, kind)
        self.half(a + 2, kind)
        if kind == 15:   # compound
            self.half(a + 8, kind)
            self.half(a + 10, kind)
            attributes = self.target(a + 4)
            if attributes is not None:
                self.word(attributes, kind)
                for i in range(le32(self.d, attributes)):
                    entry = attributes + 4 + i * 8
                    self.half(entry, kind)
                    self.half(entry + 2, kind)
                    size = le16(self.d, entry + 2)
                    if size == 4:
                        self.word(entry + 4, kind)
                    elif size == 2:
                        self.half(entry + 4, kind)
            for i in range(le16(self.d, a + 8)):
                self.anim(self.target(a + 12 + i * 4), depth + 1)
        elif kind == 1:   # raw events
            self.word(a + 4, kind)
            for i in range(le32(self.d, a + 4) * 4):
                self.word(a + 8 + i * 4, kind)
        elif kind in (11, 13):   # DeltaQuat, KeyQuat
            if kind == 11:
                self.half(a + 8, kind)
                rows, index = le16(self.d, a + 8), a + 10
            else:
                self.half(a + 12, kind)
                rows, index = le16(self.d, a + 12), a + 14
                times = self.target(a + 8)
                if times is not None:
                    for i in range(max(rows - 1, 0)):
                        self.half(times + i * 2, kind)
            rows = max(rows - 1, 0)
            info = self.target(a + 4)
            if info is not None:
                if info not in self.done:
                    self.half(info, kind)
                    count, bits = le16(self.d, info), self.d[info + 2]
                    for i in range(count * 3):
                        self.word(info + 4 + i * 4, kind)
                    if bits == 16:
                        for i in range(rows * count):
                            self.half(info + 4 + count * 12 + i * 2, kind)
                count = le16(self.d, info)
                for i in range(count // 4):
                    self.half(index + i * 2, kind)
        elif kind in (20, 21):   # DeltaF3, DeltaF1
            components = 3 if kind == 20 else 1
            self.half(a + 0xc, kind)
            self.half(a + 0xe, kind)
            self.half(a + 0x12, kind)
            keys, n = le16(self.d, a + 0xc), le16(self.d, a + 0xe)
            shift, constants = self.d[a + 0x10], self.d[a + 0x11]
            index, times = self.target(a + 4), self.target(a + 8)
            if index is not None:
                for i in range(n):
                    self.half(index + i * 2, kind)
            if times is not None:
                for i in range(max(keys - 1, 0)):
                    self.half(times + i * 2, kind)
            r = a + 0x14
            for i in range(n):
                for k in range(components * 2):
                    self.word(r + k * 4, kind)
                for k in range(components * 2):
                    self.half(r + components * 8 + k * 2, kind)
                r += components * 12
            values = n * components
            unit = 1 << shift
            block = (((unit + 1) * values) + 1) & ~1
            full, rem = keys >> shift, keys & (unit - 1)
            for b in range(full + (1 if rem else 0)):
                for i in range(values):
                    self.half(r + b * block + i * 2, kind)
            p = r + block * full
            if rem:
                p = (p + (rem + 1) * values + 1) & ~1
            if n == 0:
                p = (p + 1) & ~1
            for i in range(constants):
                self.half(p + i * 2, kind)
            q = (p + constants * 2 + 3) & ~3
            for i in range(constants * components):
                self.word(q + i * 4, kind)


def cmd_anims(gc_dir, xbox_dir):
    """every animation bank turned round as GcObject.cpp does, the bytes it touched against the Xbox twin's"""
    gc, xbox = Disc(gc_dir), Disc(xbox_dir)
    totals = collections.defaultdict(lambda: [0, 0])
    seen = set()
    for a in ARCHIVES:
        for n, _, _ in gc.entries(a):
            if not n.lower().endswith(".dat") or "\\anims\\" not in n.lower() or n.lower() in seen:
                continue
            seen.add(n.lower())
            objects = []
            for disc in (gc, xbox):
                d, rel = disc.load(a, n), disc.load(a, n[:-4] + ".rel") or b""
                objects.append(Elf(d + rel if rel and le32(d, 0x20) + 40 > len(d) else d))
            ge, xe = objects
            # The data is section 1 on both; the halves around it differ (the GameCube's .rel a half of its own)
            gbase, xbase, size = ge.secs[1][4], xe.secs[1][4], ge.secs[1][5]
            if size != xe.secs[1][5]:
                print(f"  {n}: the data sections' sizes differ")
                continue
            swapper = AnimSwapper(ge)
            for name, value, shndx in ge.syms:
                if name.startswith("__AnimationBank:::") and shndx and shndx < len(ge.secs):
                    bank = ge.at(shndx, value)
                    for k in range(5):
                        swapper.word(bank + k * 4, "bank")
                    anims = swapper.target(bank + 0xc)
                    for i in range(le32(swapper.d, bank + 4)):
                        swapper.anim(swapper.target(anims + i * 4))
            for kind, addresses in swapper.touched.items():
                for x in addresses:
                    if not gbase <= x < gbase + size or any(x - k in swapper.sites for k in range(4)):
                        continue
                    totals[kind][0] += 1
                    totals[kind][1] += swapper.d[x] == xe.d[xbase + x - gbase]
    print("  anim type: bytes turned round (or left) and compared, the Xbox twin's")
    for kind, (n, same) in sorted(totals.items(), key=lambda kv: str(kv[0])):
        print(f"    {kind}: {n:,} bytes, {100.0 * same / max(n, 1):.2f}% the Xbox's")


def main(argv):
    if len(argv) >= 4 and argv[1] == "geoprims":
        cmd_geoprims(argv[2], argv[3], argv[4:])
    elif len(argv) == 4 and argv[1] == "anims":
        cmd_anims(argv[2], argv[3])
    elif len(argv) == 3 and argv[1] == "methods":
        cmd_methods(argv[2])
    else:
        print(__doc__)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
