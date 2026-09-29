"""Nightfire action-engine levels (07xxxxxx.bin bundles): how a level is assembled, its placements, its static
world, rooms/portals, collision, paths, AI paths, ambient radiators, map sounds and particle definitions.

Spec: docs/level/spec-world.md. Everything is from the Xbox default.xbe (addresses below are Xbox).

Load order (LoaderProcess 0xbe7b0, parsemap_parsemap 0xa6b20, ResetMap_Load 0xbfb60):
  every bundle entry in directory order; types 0/2/11 = object packs -> parsemap(hash, isMap=0); type 1 = the map
  file -> parsemap(hash, isMap=1), always the last entry. Per file: blocks in order (entities register their
  hashcodes as they are parsed), block 0x1a runs parsemap_block_map_data_static 0xa6550 AT ONCE (static cels), then
  for packs only parsemap_block_map_data_dynamic(doCreation=0) (creates nothing but Searchlights - there are none in
  packs). After the whole bundle: build_link_objects_to_rooms 0x20f70, then the MAP FILE's placements again through
  map_data_dynamic(doCreation=1) = every dynamic object, then build_LinkDoors2Portals, AIPath_BindNodes.

Importable API
    Level(path_or_bytes, name=None)     .files [LevelFile] in load order, .map (the type-1 file or None)
        .placements()                   every placement of every file, in load order
        .resolve(pl, static)            -> (LevelFile, parse_gfx.Entity) or None, with the game's rules
        .static_cels()                  [Cel] as parsemap_block_map_data_static + parseentityfixup_entity build them
        .link_rooms()                   approximates build_link_objects_to_rooms (needs collision; slowish)
        .static_world(link=True)        [(Entity, 4x4 world matrix, Cel)] = what View_DrawCelGlist draws
        .portals(), .paths(), .ai_paths(), .radiators(), .map_sounds(), .particles()
    decode_placements(data, blk)        [Placement]
    decode_collision(data, blk)         Collision (boxes, triangles in entity space, surface bytes)
    rot_zyx(rot) / rot_xyz(rot) / matrix(...)   the engine's Euler matrices (column-vector 3x3 / 4x4 lists)

CLI
    python parse_level.py validate [-v]               decode every placement/block of all 32 bundles, print census
    python parse_level.py dump BUNDLE [OUT.json]       every placement of one bundle (+ static cels) as JSON
    python parse_level.py obj BUNDLE OUT.obj [--coll OUTC.obj] [--all]   the static world (and collision) as OBJ
BUNDLE is a path or a level hashcode such as 07000001.
"""
import collections
import hashlib
import json
import math
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.normpath(os.path.join(HERE, '..', 'anim')))
import bundle      # noqa: E402
import parse_gfx   # noqa: E402
from nfpaths import LVL  # noqa: E402

# ---------------------------------------------------------------------------------------------------------------
# Names
# ---------------------------------------------------------------------------------------------------------------
PLACE_NAMES = {
    15: 'Drone', 32: 'Breakable', 33: 'Ripples', 34: 'Ladder', 36: 'PlayerNewStartPos', 37: 'MPSpawnPoint',
    38: 'MPObject', 39: 'GunTurret', 40: 'Sensor', 41: 'Switch', 42: 'SkyObject', 43: 'LeafGen', 44: 'Lightning',
    45: 'AddNewStartPos2', 46: 'Lock', 47: 'Copter', 48: 'Monitor', 49: 'FuseBox', 50: 'RainBox', 51: 'Hint',
    52: 'GunImp', 53: 'PCQWorm', 54: 'Sub', 55: 'MiniSub', 56: 'SpaceLaser', 57: 'SpaceMissile', 58: 'Grapple',
    59: 'DynamicObject', 62: 'Wire', 63: 'CreepWall', 65: 'InverterTrigger', 74: 'Apocalypse', 78: 'Light',
    113: 'Rotor', 210: 'Shooter', 212: 'Cloud', 213: 'OneSided', 216: 'RigidBody', 217: 'ScriptPlayer', 218: 'Car',
    219: 'Door', 220: 'Trigger', 222: 'Searchlight', 223: 'Env', 224: 'Creature', 225: 'Destroy', 226: 'SS',
    227: 'Flicker', 228: 'Hurt', 229: 'DroneCoverCorner', 230: 'DroneCoverLow', 231: 'ThirdCam',
    232: 'TriggerLoadLevel', 233: 'DroneAIPoint', 234: 'TriggerTouchOnce', 235: 'TriggerTouch',
    236: 'TriggerMultiplexIn', 237: 'TriggerMultiplexSIn', 238: 'TriggerMultiplexOut', 239: 'TriggerMultiplexOrIn',
    240: 'Pickup', 241: 'DroneSpawner', 242: 'Emitter', 243: 'EnvTree', 244: 'TriggerMoviePlayer',
    245: 'DroneAIVolume1', 248: 'DroneAIVolume2', 249: 'SoundTrigger', 250: 'ThirdIcon', 251: 'MusicTrigger',
    252: 'Corona', 253: 'CamSubject', 254: 'Mine',
}
# static placement types (placementType & 0xffff as parseentityfixup_entity 0xa4790 switches on it)
STATIC_NAMES = {0xc047: 'room', 0xc023: 'room_underwater', 0x8019: 'object', 0x8018: 'object_8018',
                0x8017: 'water_surface', 0x803d: 'object_803d'}
# the keys (level_tag dwords 0x2c + 4k) parseentityfixup_entity reads, per static type
STATIC_KEYS = {
    0xc047: {0: 'ambient r (cel+0x8a, byte; all three 0 -> 0x3c grey)', 1: 'ambient g (+0x8c)', 2: 'ambient b (+0x8e)',
             3: 'indoor: non-zero sets cel bit 0x10000000 (no visible breath; sound listener reverb on)',
             4: 'music event id (cel+0x91): Player_Update calls Music_Event(id, 1) while the player is in the room',
             5: 'switch channel (cel+0x92): when switch_channels[ch] is on, the alternate ambient is used',
             6: 'alternate ambient r (+0x8b)', 7: 'alternate ambient g (+0x8d)', 8: 'alternate ambient b (+0x8f)',
             9: 'sound environment (cel+0x88, s16) -> SFXSetEnvironment; > 0x13 also turns the listener reverb on'},
    0xc023: {0: 'ambient r', 1: 'ambient g', 2: 'ambient b', 3: 'indoor bit 0x10000000', 4: 'music event id'},
    0x8019: {0: 'alpha mode (cel+0x93 low 2 bits): 0 opaque, 1 alpha = 255 - speed*frame, 2 alpha = speed*frame (wraps)',
             1: 'alpha speed (cel+0x93 high nibble; default 1)'},
}
ROOM_TYPES = (0xc047, 0xc023)
LINKED_TYPES = (0x8017, 0x8018, 0x8019)          # build_link_objects_to_rooms links these (0x803d becomes 0x28019)

LEVEL_NAMES = {
    0x07000001: 'HendersonA', 0x07000002: 'HendersonB', 0x07000003: 'HendersonC', 0x07000004: 'HendersonD',
    0x07000005: 'CastleExterior', 0x07000006: 'CastleCourtyard', 0x07000007: 'CastleIndoors1',
    0x07000008: 'CastleIndoors2', 0x07000009: 'TowerA', 0x0700000a: 'TowerB', 0x0700000b: 'TowerC',
    0x0700000c: 'PowerStationA1', 0x0700000d: 'PowerStationA2', 0x07000011: 'Tower2A', 0x07000012: 'Tower2B',
    0x07000013: 'Tower2C', 0x07000014: 'EvilBase', 0x07000015: 'EvilSilo', 0x07000016: 'EvilBaseC',
    0x0700001b: 'SpaceStationD', 0x07000021: 'SpaceStation', 0x07000022: 'Facility', 0x07000023: 'Atlantis',
    0x07000024: 'SkyRail', 0x07000025: 'SubPen', 0x07000026: 'StealthShip', 0x07000027: 'FortKnox',
    0x07000028: 'MissileSilo', 0x07000029: 'SnowBlind', 0x07000048: 'Menu_Pre', 0x0700004a: 'Tower2Elevator',
    0x0700004b: 'Ravine', 0x07000500: 'movie/background bundle (ResetMap_Load loads it for FMV states)',
}
# psiFog 0xddb00: fog is hard-coded per level (near, far, r, g, b); every other level draws without fog
FOG = {
    0x07000002: (10.0, 240.0, 0x3b, 0x3f, 0x80), 0x07000004: (7.0, 55.0, 0x0b, 0x12, 0x1d),
    0x07000005: (26.0, 213.0, 0x35, 0x48, 0x66), 0x07000006: (12.0, 96.0, 0x5a, 0x75, 0x98),
    0x07000007: (10.0, 150.0, 0xc4, 0x7f, 0x60), 0x07000008: (10.0, 200.0, 0x50, 0x64, 0x82),
    0x07000009: (10.0, 114.0, 0x79, 0x80, 0x96), 0x0700000a: (10.0, 100.0, 0x86, 0x7f, 0x84),
    0x0700000c: (45.0, 145.0, 0xd2, 0xd2, 0xc1), 0x0700000d: (20.0, 196.0, 0x99, 0x8b, 0x70),
    0x0700000f: (0.0, 20.0, 0x59, 0xab, 0x5a), 0x07000010: (10.0, 86.0, 0x89, 0x98, 0xaf),
    0x07000014: (4.0, 77.0, 0x80, 0x80, 0xbe), 0x07000016: (7.0, 151.0, 0x80, 0x80, 0xdb),
    0x07000024: (10.0, 179.0, 0x18, 0x2f, 0x53), 0x07000027: (17.0, 182.0, 0xb8, 0xab, 0xce),
    0x07000028: (10.0, 122.0, 0x83, 0xb6, 0x7b), 0x0700004a: (3.0, 35.0, 0x16, 0x1f, 0x34),
}

# ---------------------------------------------------------------------------------------------------------------
# Matrices. The engine's _MATRIX: m[0..2] = image of x, m[4..6] = image of y, m[8..10] = image of z, m[12..14] =
# translation (MatrixMultiplyVector 0xd7500). Here: 3x3/4x4 row-major lists for column vectors, M @ [x y z 1].
# ---------------------------------------------------------------------------------------------------------------
def _mul3(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)] for i in range(3)]


def _rx(t):
    c, s = math.cos(t), math.sin(t)
    return [[1, 0, 0], [0, c, -s], [0, s, c]]


def _ry(t):
    c, s = math.cos(t), math.sin(t)
    return [[c, 0, s], [0, 1, 0], [-s, 0, c]]


def _rz(t):
    c, s = math.cos(t), math.sin(t)
    return [[c, -s, 0], [s, c, 0], [0, 0, 1]]


def rot_zyx(rot):
    """RotMatrixZYX 0xd6710 (View_DrawCelGlist: how static cels are DRAWN): Rz(z) Ry(y) Rx(x), x applied first.
    = Blender Euler mode 'XYZ' with the same angles (in the engine's y-up frame)."""
    return _mul3(_rz(rot[2]), _mul3(_ry(rot[1]), _rx(rot[0])))


def rot_xyz(rot):
    """RotMatrix 0xd6560 / RotTransMatrix 0xd6630 (cel bounding sphere, cel COLLISION, sky objects): Rx Ry Rz,
    z applied first. For single-axis rotations (all but 17 static placements) it equals rot_zyx."""
    return _mul3(_rx(rot[0]), _mul3(_ry(rot[1]), _rz(rot[2])))


def quat_matrix(q):
    """Quat_QuatToMat 0xd5800 (standard, q = x y z w). NOTE the placement quat (+0x24) is the CONJUGATE (inverse)
    of the Euler orientation: quat_matrix(conj(q)) == rot_xyz(rot) for all 876 rotated static placements (the 17
    multi-axis ones included, where it does NOT equal the rot_zyx the game draws them with); for rotated dynamic
    placements 3074 of 3211 match (62 multi-axis ones match rot_zyx instead, 75 match neither)."""
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def matrix(r3, pos):
    return [[r3[0][0], r3[0][1], r3[0][2], pos[0]], [r3[1][0], r3[1][1], r3[1][2], pos[1]],
            [r3[2][0], r3[2][1], r3[2][2], pos[2]], [0.0, 0.0, 0.0, 1.0]]


def xform(m, p):
    return (m[0][0] * p[0] + m[0][1] * p[1] + m[0][2] * p[2] + m[0][3],
            m[1][0] * p[0] + m[1][1] * p[1] + m[1][2] * p[2] + m[1][3],
            m[2][0] * p[0] + m[2][1] * p[1] + m[2][2] * p[2] + m[2][3])


IDENTITY = matrix([[1, 0, 0], [0, 1, 0], [0, 0, 1]], (0, 0, 0))

# ---------------------------------------------------------------------------------------------------------------
# Placements (block 0x1a)
# ---------------------------------------------------------------------------------------------------------------
REC_SIZE = 0x4c
LEVEL_TAG_SIZE = 0xb0          # the zeroed stack buffer; key k -> dword at 0x2c + 4k, so keys 0..32 fit


class Placement:
    """TARGET_PLACEMENT, 0x4c + 8n bytes:
    +0x00 u16 celEntityNum   +0x02 u16 (uninitialised padding: garbage, never read)
    +0x04 u32 glistHashcode (-1 = entity celEntityNum of the SAME file)   +0x08 u32 placementType
    +0x0c vec3 pos   +0x18 vec3 rot (Euler radians)   +0x24 quat (x y z w; conjugate of the Euler orientation)
    +0x34 vec3 scale (1,1,1 on every static placement; not read for statics)
    +0x40 u16 pathFrames  +0x42 u16 pathRate  +0x44 u16 pathIndex  +0x46 u16 (garbage padding)
    +0x48 u32 n   +0x4c n x {u32 key, u32 value}"""
    __slots__ = ('file', 'index', 'off', 'ent', 'pad02', 'glist_hash', 'type', 'pos', 'rot', 'quat', 'scale',
                 'path_frames', 'path_rate', 'path_index', 'pad46', 'kv')

    def __init__(self, data, off, index, file=None):
        self.file, self.index, self.off = file, index, off
        self.ent, self.pad02, self.glist_hash, self.type = struct.unpack_from('<HHII', data, off)
        self.pos = struct.unpack_from('<3f', data, off + 0x0c)
        self.rot = struct.unpack_from('<3f', data, off + 0x18)
        self.quat = struct.unpack_from('<4f', data, off + 0x24)
        self.scale = struct.unpack_from('<3f', data, off + 0x34)
        self.path_frames, self.path_rate, self.path_index, self.pad46 = struct.unpack_from('<4H', data, off + 0x40)
        n = struct.unpack_from('<I', data, off + 0x48)[0]
        self.kv = [struct.unpack_from('<II', data, off + REC_SIZE + 8 * i) for i in range(n)]

    @property
    def size(self):
        return REC_SIZE + 8 * len(self.kv)

    # --- type flags
    @property
    def type_id(self):
        return self.type & 0xff

    @property
    def is_static(self):          # parsemap_block_map_data_static: (type & 0x8000) != 0 (so 0xffffffff counts)
        return bool(self.type & 0x8000)

    @property
    def is_dynamic(self):         # map_data_dynamic: (type & 0xa000) == 0 and a glist -> parsemap_create_dynamic_objects
        return (self.type & 0xa000) == 0

    @property
    def is_unplaced(self):        # 0x2000 without 0x8000: neither handler takes it; the game ignores it
        return (self.type & 0xa000) == 0x2000

    def type_split(self):
        t = self.type
        if t == 0xffffffff:
            return dict(raw=t, kind='none')
        d = dict(raw=t, id=t & 0xff, static=bool(t & 0x8000), room=bool(t & 0x4000), unplaced=bool(t & 0x2000),
                 other_bits=t & ~0xe0ff)
        if t & 0x8000:
            d['kind'] = STATIC_NAMES.get(t & 0xffff, 'static_other')
        elif t & 0x2000:
            d['kind'] = 'unplaced'
        else:
            d['kind'] = PLACE_NAMES.get(t & 0xff, 'unknown%d' % (t & 0xff))
        return d

    # --- level_tag (what the Create functions receive)
    @property
    def keys(self):
        return {k: v for k, v in self.kv}

    def level_tag(self, paths=None):
        """The 0xB0-byte level_tag exactly as map_data_dynamic builds it: +2 u16 pathFrames, +4 u16 pathRate,
        +8 path pointer (here: the path's frame list index, or its frames if `paths` is given; only when
        pathFrames != 0), then dword[0x2c + 4k] = value for each {k, value} (later keys overwrite earlier)."""
        buf = bytearray(LEVEL_TAG_SIZE)
        if self.path_frames:
            struct.pack_into('<HH', buf, 2, self.path_frames, self.path_rate)
            struct.pack_into('<I', buf, 8, self.path_index)
        for k, v in self.kv:
            if 0x2c + 4 * k + 4 <= LEVEL_TAG_SIZE:
                struct.pack_into('<I', buf, 0x2c + 4 * k, v)
        return bytes(buf)

    def dword(self, k, default=0):
        return self.keys.get(k, default)

    def as_float(self, k, default=0.0):
        v = self.keys.get(k)
        return default if v is None else struct.unpack('<f', struct.pack('<I', v))[0]

    def to_json(self, level=None, static=None):
        d = dict(file=self.file.name if self.file else None, index=self.index, type=self.type_split(),
                 ent=self.ent, glist_hash='%08x' % self.glist_hash, pos=self.pos, rot=self.rot, quat=self.quat,
                 scale=self.scale, path=dict(frames=self.path_frames, rate=self.path_rate, index=self.path_index),
                 keys={str(k): v for k, v in self.kv})
        if level is not None:
            r = level.resolve(self, self.is_static if static is None else static)
            d['glist'] = None if r is None else dict(file=r[0].name, entity=r[1].index, name=r[1].name,
                                                     hash='%08x' % r[1].hash,
                                                     mesh=bool(r[1].mesh and not r[1].mesh.empty))
        return d


def decode_placements(data, blk, file=None):
    out = []
    p, e = blk.off + 4, blk.off + blk.size
    while p < e:
        pl = Placement(data, p, len(out), file)
        out.append(pl)
        p += pl.size
    if p != e:
        raise ValueError('placement records overrun block at 0x%x' % blk.off)
    return out


# ---------------------------------------------------------------------------------------------------------------
# Collision (blocks 0x2e/0x2f, parsemap_block_Coll_Data_New 0xa63f0; read by Intersect_RayGeom 0x29a30 etc.)
# ---------------------------------------------------------------------------------------------------------------
class CollBox:
    __slots__ = ('index', 'bmin', 'bmax', 'childA', 'childB', 'triEnd', 'chunk', 'vt')

    @property
    def leaf(self):
        return self.childA < 0


class Collision:
    """payload: u32 version (5 in every block) u16 nBoxes u16 nTris u16 nChunks u16 pad
       version 5: +0x0c u32 size (summed into OldCollisionSize) + 12 zero bytes; data at payload+0x1c
       chunks  nChunks * 64 bytes: a pool of int16 words: vertices (3 x s16) and planes (3 x s16 normal*16383,
               f32 distance) addressed by u16 WORD offsets from a leaf's chunk base
       boxes   nBoxes * 48 (COLLBOX_tag): f32 min[3]; s16 childA; s16 childB; f32 max[3]; s16 triEnd; u16 chunk;
               f32 ox, oy, oz, scale.  Inner node: childA >= 0, children = boxes[childA], boxes[childB].
               Leaf: childA < 0 (0x8000 | n), triangles [childB, triEnd), vertices at
               chunks + 64*chunk + 2*idx, position = s16 * scale + (ox, oy, oz) (Intersect_UnpackTriangleVerticesToWorld)
       tris    nTris * 8: u16 i0, i1, i2 (word offsets), u16 plane (word offset, Intersect_UnpackTrianglePlaneEq)
       surface nTris bytes (padded to 4): low 6 bits = surface type (0x0d/0x0e skipped by ray mask bit 1,
               0x10 by bit 2); the upper 2 bits are flags
    Coordinates are the entity's own space; a static cel's collision is placed with RotTransMatrix(rot, pos)
    (Rx Ry Rz order), or identity when pos and rot are both zero (cel flag 0x8000000, Collide_Intersect 0x2be10)."""

    def __init__(self, data, blk):
        o = blk.off + 4
        self.version, self.nBoxes, self.nTris, self.nChunks = struct.unpack_from('<IHHH', data, o)
        self.size_word = struct.unpack_from('<I', data, o + 0xc)[0] if self.version == 5 else None
        base = o + 0xc + (0x10 if self.version == 5 else 0)
        self.chunks = base
        self.box_off = base + self.nChunks * 64
        self.tri_off = self.box_off + self.nBoxes * 48
        self.surf_off = self.tri_off + self.nTris * 8
        self.end = blk.off + blk.size
        self.data = data
        self.boxes = []
        for i in range(self.nBoxes):
            b = CollBox()
            p = self.box_off + 48 * i
            b.index = i
            b.bmin = struct.unpack_from('<3f', data, p)
            b.childA, b.childB = struct.unpack_from('<hh', data, p + 12)
            b.bmax = struct.unpack_from('<3f', data, p + 16)
            b.triEnd, b.chunk = struct.unpack_from('<hH', data, p + 28)
            b.vt = struct.unpack_from('<4f', data, p + 32)
            self.boxes.append(b)
        self._tris = None

    def surface(self, t):
        return self.data[self.surf_off + t]

    def vertex(self, box, word):
        x, y, z = struct.unpack_from('<3h', self.data, self.chunks + 64 * box.chunk + 2 * word)
        ox, oy, oz, s = box.vt
        return (x * s + ox, y * s + oy, z * s + oz)

    def plane(self, box, word):
        nx, ny, nz, d = struct.unpack_from('<3hf', self.data, self.chunks + 64 * box.chunk + 2 * word)
        k = 6.103888e-05
        return (nx * k, ny * k, nz * k, d)

    def triangles(self):
        """[(v0, v1, v2, surface byte, plane, leaf index)] in entity space, each triangle once"""
        if self._tris is None:
            out = []
            for b in self.boxes:
                if not b.leaf:
                    continue
                for t in range(b.childB, b.triEnd):
                    i0, i1, i2, pw = struct.unpack_from('<4H', self.data, self.tri_off + 8 * t)
                    out.append((self.vertex(b, i0), self.vertex(b, i1), self.vertex(b, i2), self.surface(t),
                                self.plane(b, pw), b.index))
            self._tris = out
        return self._tris

    def validate(self):
        errs = []
        if self.version != 5:
            errs.append('version %d' % self.version)
        slen = self.end - self.surf_off
        if not (self.nTris <= slen < self.nTris + 4 + 32):
            errs.append('surface bytes %d for %d tris' % (slen, self.nTris))
        covered = [0] * self.nTris
        for b in self.boxes:
            if b.leaf:
                if not (0 <= b.childB <= b.triEnd <= self.nTris):
                    errs.append('leaf %d range %d..%d' % (b.index, b.childB, b.triEnd))
                    continue
                for t in range(b.childB, b.triEnd):
                    covered[t] += 1
            elif not (0 <= b.childA < self.nBoxes and 0 <= b.childB < self.nBoxes):
                errs.append('node %d children %d %d' % (b.index, b.childA, b.childB))
        if self.nBoxes and any(c != 1 for c in covered):
            errs.append('%d triangles not covered exactly once' % sum(1 for c in covered if c != 1))
        if errs:
            return errs
        bad_box = bad_plane = 0
        for v0, v1, v2, s, pl, bi in self.triangles():
            b = self.boxes[bi]
            for v in (v0, v1, v2):
                if any(v[k] < b.bmin[k] - 0.02 or v[k] > b.bmax[k] + 0.02 for k in range(3)):
                    bad_box += 1
                    break
            e1 = [v1[k] - v0[k] for k in range(3)]
            e2 = [v2[k] - v0[k] for k in range(3)]
            n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
            ln = math.sqrt(sum(x * x for x in n))
            if ln > 2e-4:           # ignore sliver triangles (quantisation noise; area < 1e-4)
                dot = sum(n[k] * pl[k] for k in range(3)) / ln
                dist = sum(pl[k] * v0[k] for k in range(3)) + pl[3]
                if dot < 0.98 or abs(dist) > 0.02 + 1e-4 * abs(pl[3]):
                    bad_plane += 1
        if bad_box:
            errs.append('%d triangles outside their leaf box' % bad_box)
        if bad_plane:
            errs.append('%d triangles disagree with their plane' % bad_plane)
        return errs


def decode_collision(data, blk):
    return Collision(data, blk)


def _pt_tri_dist2(p, a, b, c):
    """squared distance point-triangle (Ericson, Real-Time Collision Detection 5.1.5)"""
    ab = [b[i] - a[i] for i in range(3)]
    ac = [c[i] - a[i] for i in range(3)]
    ap = [p[i] - a[i] for i in range(3)]

    def dot(u, v):
        return u[0] * v[0] + u[1] * v[1] + u[2] * v[2]

    def d2(q):
        return sum((p[i] - q[i]) ** 2 for i in range(3))
    d1, d2_ = dot(ab, ap), dot(ac, ap)
    if d1 <= 0 and d2_ <= 0:
        return d2(a)
    bp = [p[i] - b[i] for i in range(3)]
    d3, d4 = dot(ab, bp), dot(ac, bp)
    if d3 >= 0 and d4 <= d3:
        return d2(b)
    vc = d1 * d4 - d3 * d2_
    if vc <= 0 and d1 >= 0 and d3 <= 0:
        v = d1 / (d1 - d3)
        return d2([a[i] + v * ab[i] for i in range(3)])
    cp = [p[i] - c[i] for i in range(3)]
    d5, d6 = dot(ab, cp), dot(ac, cp)
    if d6 >= 0 and d5 <= d6:
        return d2(c)
    vb = d5 * d2_ - d1 * d6
    if vb <= 0 and d2_ >= 0 and d6 <= 0:
        w = d2_ / (d2_ - d6)
        return d2([a[i] + w * ac[i] for i in range(3)])
    va = d3 * d6 - d5 * d4
    if va <= 0 and (d4 - d3) >= 0 and (d5 - d6) >= 0:
        w = (d4 - d3) / ((d4 - d3) + (d5 - d6))
        return d2([b[i] + w * (c[i] - b[i]) for i in range(3)])
    den = 1.0 / (va + vb + vc)
    v, w = vb * den, vc * den
    return d2([a[i] + ab[i] * v + ac[i] * w for i in range(3)])


def _box_dist2(p, bmin, bmax):
    return sum(max(bmin[i] - p[i], 0, p[i] - bmax[i]) ** 2 for i in range(3))


def collision_nearest(coll, p):
    """distance from p (entity space) to the nearest collision triangle (inf if none)"""
    best = float('inf')
    tris = coll.triangles()
    byleaf = collections.defaultdict(list)
    for t in tris:
        byleaf[t[5]].append(t)
    leaves = sorted((_box_dist2(p, coll.boxes[i].bmin, coll.boxes[i].bmax), i) for i in byleaf)
    for bd, i in leaves:
        if bd >= best:
            break
        for v0, v1, v2, s, pl, bi in byleaf[i]:
            d = _pt_tri_dist2(p, v0, v1, v2)
            if d < best:
                best = d
    return math.sqrt(best)


# ---------------------------------------------------------------------------------------------------------------
# Other map blocks
# ---------------------------------------------------------------------------------------------------------------
def decode_portals(data, blk):
    """0x21 parsemap_block_portal_data 0xa6260: u32 n, n x {vec3 quad[4]; u16 celA; u16 celB; u16 flags; u16 pad}.
    celA/celB are ENTITY indices of the same file; the portal links the room cels whose glist is that entity.
    celB gets a portal into celA (open unless flags & 2), celA one into celB (open unless flags & 1)."""
    n = struct.unpack_from('<I', data, blk.off + 4)[0]
    out = []
    for i in range(n):
        p = blk.off + 8 + 56 * i
        quad = [struct.unpack_from('<3f', data, p + 12 * k) for k in range(4)]
        a, b, fl, pad = struct.unpack_from('<4H', data, p + 48)
        out.append(dict(index=i, quad=quad, celA=a, celB=b, flags=fl, pad=pad))
    return out


def decode_path(data, blk):
    """0x19 (kept only for map files, m_paths[i] = payload): frames of 28 bytes {vec3 pos; quat (x y z w)}, the
    Spline_Interp 0xd8df0 frame list (Catmull-Rom on positions, slerp on quats). The placement's +0x40 is the
    frame count it uses (path_frames), +0x42 the rate, +0x44 the path index."""
    pl = data[blk.off + 4:blk.off + blk.size]
    return [dict(pos=struct.unpack_from('<3f', pl, i), quat=struct.unpack_from('<4f', pl, i + 12))
            for i in range(0, len(pl) - len(pl) % 28, 28)]


def decode_ai_paths(data, blk):
    """0x05 AIPath_Parse 0xa6c40 (map files only): u32 version (8, else ignored), u32 count, then records:
    char name[0x80]; u32 flags (bit 0: 1 = a BOUND (area outline), 0 = a PATH (route graph));
    u32 [8] (zero in the data); u32 nodeCount; u32 extraBytes; u8 extra[extraBytes];
    path : nodeCount x 0x40 {u16 index, u16 pathNo, u32 link (0x2468abce = none), u32 0x87654321, u32 0x9abcdef0,
           vec3 pos, f32 1.0, ... sentinels and 3 x f32 0.1234}; u32 linkCount; linkCount x 16
           {u16 index, u16 pathNo, u32 0, u16 nodeA, u16 nodeB, f32}
    bound: nodeCount x 0x20 {same first 0x20 bytes}; u32 linkCount; linkCount x 20 {u16 index, u16 pathNo, u32 0,
           u16 nodeA, u16 nodeB, u32 0x01234567 x 2}
    (the runtime overwrites +0/+2 of nodes and links with the index and the path number, and adds 0.4 to node y)"""
    o = blk.off + 4
    ver, cnt = struct.unpack_from('<II', data, o)
    out = dict(version=ver, paths=[], end_ok=None)
    if ver != 8:
        return out
    p = o + 8
    for r in range(cnt):
        name = parse_gfx.cstr(data[p:p + 0x80])
        dw = struct.unpack_from('<11I', data, p + 0x80)
        flags, mid, nn, extra = dw[0], dw[1:9], dw[9], dw[10]
        bound = bool(flags & 1)
        xb = data[p + 0xac:p + 0xac + extra]
        q = p + 0xac + extra
        ns = 0x20 if bound else 0x40
        nodes = []
        for i in range(nn):
            nodes.append(dict(index=struct.unpack_from('<H', data, q)[0], pos=struct.unpack_from('<3f', data, q + 0x10),
                              link=struct.unpack_from('<I', data, q + 4)[0]))
            q += ns
        lc = struct.unpack_from('<I', data, q)[0]
        q += 4
        ls = 20 if bound else 16
        links = []
        for i in range(lc):
            a, b = struct.unpack_from('<HH', data, q + 8)
            links.append((a, b))
            q += ls
        out['paths'].append(dict(name=name, flags=flags, bound=bound, mid=mid, extra=list(xb), nodes=nodes,
                                 links=links))
        p = q
    out['end_ok'] = p == blk.off + blk.size
    out['end_gap'] = blk.off + blk.size - p
    return out


def decode_radiators(data, blk):
    """0x26 Light_SetAmbientRadiators 0x6e3b0 (map files): u32 n, n x {vec3 pos; f32 range2; f32 1/range2;
    f32 r, g, b (0..1)} -> point lights (Light list) with brightness/range sqrt(range2) and colour *255"""
    n = struct.unpack_from('<I', data, blk.off + 4)[0]
    out = []
    for i in range(n):
        v = struct.unpack_from('<8f', data, blk.off + 8 + 32 * i)
        out.append(dict(pos=v[0:3], range=math.sqrt(max(v[3], 0.0)), range2=v[3], inv_range2=v[4],
                        colour=v[5:8]))
    return out


def decode_map_sounds(data, blk):
    """0x28 Sound_LoadMapSounds 0xcb870 -> GMapSounds 0x299ae8, played by HandleMapSoundAllocation 0xcbf90:
    u32 n, n x {u32 unused; u32 sfxId (low 20 bits; 0xffff = none); vec3 pos; f32 volume/pitch (ftol'd, 100);
    f32 radiusOuter (start within it, stop beyond 1.2x); f32 radiusInner}"""
    n = struct.unpack_from('<I', data, blk.off + 4)[0]
    out = []
    for i in range(n):
        p = blk.off + 8 + 32 * i
        u0, sfx = struct.unpack_from('<II', data, p)
        pos = struct.unpack_from('<3f', data, p + 8)
        a, ro, ri = struct.unpack_from('<3f', data, p + 20)
        out.append(dict(unused=u0, sfx=sfx, sfx_id=sfx & 0xfffff, pos=pos, param=a, radius_outer=ro, radius_inner=ri))
    return out


def decode_particles(data, blk):
    """0x30 parsemap_block_particles 0x68020 (map files): particle-system DEFINITIONS (no positions) registered in
    the hashtable under their own hashcode; Emitter placements refer to them.
    u32 n; n x {u32 word (low byte 4 = version, else stop; bits 8+ kept as a u16); u32 hash (registered);
    u32 texture hash (-1 none; the hashtable entry's first dword is kept); u32 hash2 (-1 none);
    u16 a; u16 b; f32 x 10; u32 nKeys; nKeys x {f32 size xyz (*0.5 at load); f32; f32}}"""
    p, e = blk.off + 4, blk.off + blk.size
    n = struct.unpack_from('<I', data, p)[0]
    p += 4
    out = []
    for i in range(n):
        w, h, th, h2 = struct.unpack_from('<4I', data, p)
        if w & 0xff != 4:
            break
        a, b = struct.unpack_from('<HH', data, p + 16)
        fl = struct.unpack_from('<10f', data, p + 20)
        nk = struct.unpack_from('<I', data, p + 60)[0]
        keys = [struct.unpack_from('<5f', data, p + 64 + 20 * k) for k in range(nk)]
        out.append(dict(hash=h, flags=w >> 8, texture=th, hash2=h2, a=a, b=b, floats=fl, keys=keys))
        p += 64 + 20 * nk
    return out, e - p


# ---------------------------------------------------------------------------------------------------------------
# Level
# ---------------------------------------------------------------------------------------------------------------
class LevelFile:
    """one 01xxxxxx file of a bundle, as the loader sees it"""

    def __init__(self, order, entry_type, name, data):
        self.order, self.entry_type, self.name, self.data = order, entry_type, name, data
        self.is_map = entry_type == 1
        self.pack = parse_gfx.Pack(data)
        self.entities = self.pack.entities
        self.placements = []
        self.blocks = collections.defaultdict(list)
        for b in self.pack.blocks:
            self.blocks[b.type].append(b)
        for b in self.blocks[0x1a]:
            self.placements += decode_placements(data, b, self)
        self._coll = {}

    def collision(self, ent):
        if ent.coll_block is None:
            return None
        if ent.index not in self._coll:
            self._coll[ent.index] = Collision(self.data, ent.coll_block)
        return self._coll[ent.index]

    def __repr__(self):
        return '<%s type %d %s>' % (self.name, self.entry_type, 'map' if self.is_map else 'pack')


class Cel:
    """a cel_tag (0x94 bytes, build_alloc_cel 0x20700) made from one static placement"""
    __slots__ = ('placement', 'file', 'entity', 'bits', 'kind', 'pos', 'rot', 'eflags', 'bbmin', 'bbmax',
                 'centre', 'radius', 'ambient', 'ambient_alt', 'switch_channel', 'music_event', 'flag_10000000',
                 'field88', 'alpha_mode', 'alpha_speed', 'room', 'children', 'room_dist', 'shear_height', 'ident')

    @property
    def world(self):
        """the matrix View_DrawCelGlist 0xda670 draws with: RotMatrixZYX(rot) + pos (flag 8: camera-facing rotation
        instead; flag 4: random 0.95..1.05 scale per axis each frame)"""
        return matrix(rot_zyx(self.rot), self.pos)

    @property
    def world_collision(self):
        """Collide_Intersect 0x2be10: identity when bits & 0x8000000, else RotTransMatrix(rot, pos) (Rx Ry Rz)"""
        return IDENTITY if self.ident else matrix(rot_xyz(self.rot), self.pos)

    @property
    def is_room(self):
        return bool(self.bits & 0x40000)

    @property
    def drawable(self):
        """has geometry and is not hidden by entity flag 0x10 (View_AddCels skips glist flags & 0x10)"""
        m = self.entity.mesh
        return m is not None and not m.empty and not (self.eflags & 0x10)

    @property
    def drawn(self):
        """drawable and reachable by View_AddCels: a room (through the portal walk) or an object linked to a room"""
        if not self.drawable:
            return False
        if self.is_room:
            return True
        return self.room is not None and (self.bits & 0xffff) in LINKED_TYPES


def fixup(cel, pl):
    """parseentityfixup_entity 0xa4790 for the static types that occur (placement type & 0xffff)"""
    t = pl.type & 0xffff
    lt = pl.keys
    cel.bits = t
    zero = all(v == 0 for v in cel.pos) and all(v == 0 for v in cel.rot)
    if t in (0x8017, 0x8018, 0x8019):
        if zero:
            cel.bits |= 0x8000000
        if t == 0x8019:
            v = ((lt.get(1, 0) << 4) | lt.get(0, 0)) & 0xff        # cel+0x93 = key1 << 4 | key0
            cel.alpha_mode, cel.alpha_speed = v & 3, v >> 4
    elif t == 0x803d:
        cel.bits = 0x28019
    elif t == 0xc023:
        cel.bits = 0x0804c023 | (0x10000000 if lt.get(3, 0) else 0)
        cel.shear_height = cel.bbmax[1]
        cel.pos = (0.0, 0.0, 0.0)
        cel.music_event = lt.get(4, 0) & 0xff
        cel.field88 = 0x9898
        cel.ambient = tuple(lt.get(k, 0) & 0xff for k in (0, 1, 2))
        if cel.ambient == (0, 0, 0):
            cel.ambient = (0x3c, 0x3c, 0x3c)
    elif t == 0xc047:
        cel.bits = 0x0804c047 | (0x10000000 if lt.get(3, 0) else 0)
        cel.pos = (0.0, 0.0, 0.0)
        cel.music_event = lt.get(4, 0) & 0xff
        cel.switch_channel = lt.get(5, 0) & 0xff
        cel.ambient = tuple(lt.get(k, 0) & 0xff for k in (0, 1, 2))
        cel.ambient_alt = tuple(lt.get(k, 0) & 0xff for k in (6, 7, 8))
        cel.shear_height = -500.0
        cel.field88 = lt.get(9, 0) & 0xffff
        if cel.ambient == (0, 0, 0):
            cel.ambient = (0x3c, 0x3c, 0x3c)
    cel.ident = bool(cel.bits & 0x8000000)
    cel.flag_10000000 = bool(cel.bits & 0x10000000)


class Level:
    def __init__(self, src, name=None):
        if isinstance(src, (bytes, bytearray)):
            data, self.name = bytes(src), name
        else:
            if not os.path.exists(src) and os.path.exists(os.path.join(LVL, src + '.bin')):
                src = os.path.join(LVL, src + '.bin')
            data = open(src, 'rb').read()
            self.name = name or os.path.basename(src)[:8].lower()
        self.hash = int(self.name, 16) if self.name else None
        self.entries = list(bundle.walk(data))
        self.files = []
        self.other = collections.Counter()
        for typ, nm, off, ln in self.entries:
            if typ in (0, 1, 2, 11) and nm.lower().startswith('01'):
                self.files.append(LevelFile(len(self.files), typ, nm.lower(), data[off:off + ln]))
            else:
                self.other[typ] += 1
        maps = [f for f in self.files if f.is_map]
        self.map = maps[-1] if maps else None
        # the hashtable as it grows file by file; first definition kept (hashtable_additem does not replace, and a
        # duplicate insert uses a stale insertion index - duplicates are counted in self.dup_hashes)
        self.hashtable = {}
        self.defined_by = {}
        self.dup_hashes = []
        for f in self.files:
            for e in f.entities:
                if e.hash != 0xffffffff:
                    if e.hash in self.hashtable:
                        self.dup_hashes.append((e.hash, self.hashtable[e.hash][0].name, f.name))
                    else:
                        self.hashtable[e.hash] = (f, e)
                        self.defined_by[e.hash] = f.order
        self._cels = None
        self._linked = False

    def placements(self):
        for f in self.files:
            for pl in f.placements:
                yield pl

    def resolve(self, pl, static):
        """The celglist the game uses. glist -1: pCelList[celEntityNum] of the SAME file; static accepts
        celEntityNum <= count (off by one: == count reads past the array), dynamic < count. Otherwise
        hashtable_getitem: for a static placement only files loaded so far (this one included) are visible."""
        f = pl.file
        if pl.glist_hash == 0xffffffff:
            if pl.ent < len(f.entities):
                return f, f.entities[pl.ent]
            return None
        hit = self.hashtable.get(pl.glist_hash)
        if hit is None:
            return None
        if static and self.defined_by[pl.glist_hash] > f.order:
            return None
        return hit

    def static_cels(self):
        if self._cels is not None:
            return self._cels
        cels = []
        for f in self.files:
            for pl in f.placements:
                if not pl.is_static:
                    continue
                r = self.resolve(pl, True)
                if r is None:
                    continue          # the game: no cel (index > count) or a crash (NULL glist); never in the data
                c = Cel()
                c.placement, c.file, c.entity = pl, r[0], r[1]
                e = r[1]
                c.pos, c.rot = tuple(pl.pos), tuple(pl.rot)
                c.eflags = e.flags
                c.bbmin, c.bbmax = e.bbmin, e.bbmax
                c.ambient = (0x50, 0x50, 0x50)
                c.ambient_alt = None
                c.switch_channel = 0
                c.music_event = 0
                c.field88 = 0
                c.alpha_mode, c.alpha_speed = 0, 1
                c.shear_height = -500.0
                c.room, c.children, c.room_dist = None, [], None
                fixup(c, pl)
                # parseentity_transform_bounding_box 0xa4b60: RotMatrix (Rx Ry Rz!) + pos applied to the sphere
                c.centre = xform(matrix(rot_xyz(c.rot), c.pos), e.sphere[:3])
                c.radius = e.sphere[3]
                cels.append(c)
        self._cels = cels
        return cels

    def dynamic_objects(self):
        """[(Placement, (LevelFile, Entity))] = what ResetMap_Load's map_data_dynamic(doCreation=1) hands to
        parsemap_create_dynamic_objects: the MAP file's placements with (type & 0xa000) == 0 and a glist.
        (Object packs' own placements run with doCreation=0 and create nothing.)"""
        out = []
        if self.map:
            for pl in self.map.placements:
                if pl.is_dynamic:
                    r = self.resolve(pl, False)
                    if r is not None:
                        out.append((pl, r))
        return out

    def rooms(self):
        return [c for c in self.static_cels() if c.is_room]

    def link_rooms(self):
        """build_link_objects_to_rooms 0x20f70: for each 0x8017/8018/8019 cel, over the room cels whose bbox
        contains its sphere centre and whose radius >= its radius, Collide_SphereIntersect(centre, 4001, room)
        and keep the room with the nearest hit. Approximated here by the nearest collision triangle (room
        collision is at identity). Objects with no room are never drawn by the game."""
        if self._linked:
            return
        rooms = self.rooms()
        for c in self.static_cels():
            if (c.bits & 0xffff) not in LINKED_TYPES:
                continue
            best, bestd = None, 4000.0
            for r in rooms:
                if not (c.radius <= r.radius and all(r.bbmin[k] <= c.centre[k] <= r.bbmax[k] for k in range(3))):
                    continue
                coll = r.file.collision(r.entity)
                if coll is None:
                    continue
                d = collision_nearest(coll, c.centre)
                if d < bestd + 1.0 and (best is None or d < bestd):
                    best, bestd = r, d
            if best is not None:
                c.room, c.room_dist = best, bestd
                best.children.append(c)
                if (c.bits & 0xffff) == 0x8017:
                    best.shear_height = c.bbmin[1]
        self._linked = True

    def static_world(self, link=True, only_drawn=True):
        """[(Entity, world 4x4, Cel)] of the static cels that draw geometry. With link=False every drawable cel is
        returned (skipping the room-linking pass, which the game needs for an object to be drawn at all)."""
        cels = self.static_cels()
        if link:
            self.link_rooms()
        out = []
        for c in cels:
            ok = c.drawn if (link and only_drawn) else c.drawable
            if ok:
                out.append((c.entity, c.world, c))
        return out

    # --- level-wide blocks (map file)
    def _map_blocks(self, t):
        return self.map.blocks[t] if self.map else []

    def portals(self):
        out = []
        if not self.map:
            return out
        # the handler walks the world cel list (newest first) and keeps the LAST match, i.e. the earliest-created
        # static cel of ANY kind whose glist is pCelList[cel] of the map file
        by_ent = {}
        for c in self.static_cels():
            if c.entity in self.map.entities and c.entity.index not in by_ent and c.file is self.map:
                by_ent[c.entity.index] = c
        prevA = prevB = None
        for b in self._map_blocks(0x21):
            for p in decode_portals(self.map.data, b):
                # the handler never resets its two cel pointers between portals: with no match it links the
                # PREVIOUS portal's cels again (07000012 portals 35-52, entity 552 = a ScriptPlayer, not a cel)
                a, bb = by_ent.get(p['celA']), by_ent.get(p['celB'])
                p['stale'] = a is None or bb is None
                p['cel_A'] = a if a is not None else prevA
                p['cel_B'] = bb if bb is not None else prevB
                prevA, prevB = p['cel_A'], p['cel_B']
                p['roomA'], p['roomB'] = a, bb
                out.append(p)
        return out

    def paths(self):
        return [decode_path(self.map.data, b) for b in self._map_blocks(0x19)]

    def ai_paths(self):
        bl = self._map_blocks(0x05)
        return decode_ai_paths(self.map.data, bl[0]) if bl else None

    def radiators(self):
        return [r for b in self._map_blocks(0x26) for r in decode_radiators(self.map.data, b)]

    def map_sounds(self):
        return [s for b in self._map_blocks(0x28) for s in decode_map_sounds(self.map.data, b)]

    def particles(self):
        """particle definitions of every file (0x30 is parsed for packs and the map alike), later files win"""
        out = {}
        for f in self.files:
            for b in f.blocks[0x30]:
                for p in decode_particles(f.data, b)[0]:
                    out[p['hash']] = p
        return list(out.values())

    def fog(self):
        return FOG.get(self.hash)


# ---------------------------------------------------------------------------------------------------------------
# OBJ output
# ---------------------------------------------------------------------------------------------------------------
def write_obj(path, items, coll_items=None):
    """items: [(group name, Mesh, 4x4)]; triangles reversed so they wind counter-clockwise seen from outside
    (spec-mesh 6); y up as in the game."""
    with open(path, 'w') as f:
        vbase = 1
        for gname, mesh, m in items:
            vs = mesh.vertices
            tris = mesh.triangles()
            if not tris:
                continue
            f.write('o %s\n' % gname)
            for v in vs:
                f.write('v %.5f %.5f %.5f\n' % xform(m, v['pos']))
            for a, b, c, pi in tris:
                f.write('f %d %d %d\n' % (vbase + a, vbase + c, vbase + b))
            vbase += len(vs)
        for gname, coll, m in (coll_items or []):
            tris = coll.triangles()
            if not tris:
                continue
            f.write('o %s\n' % gname)
            for v0, v1, v2, s, pl, bi in tris:
                for v in (v0, v1, v2):
                    f.write('v %.5f %.5f %.5f\n' % xform(m, v))
            for i in range(len(tris)):
                f.write('f %d %d %d\n' % (vbase + 3 * i, vbase + 3 * i + 2, vbase + 3 * i + 1))
            vbase += 3 * len(tris)


def level_obj(lv, out, coll_out=None, all_drawable=False):
    lv.link_rooms()
    items = []
    for e, m, c in lv.static_world(link=True, only_drawn=not all_drawable):
        room = c if c.is_room else c.room
        rn = room.entity.name if room else 'NOROOM'
        items.append(('%s/%s_%s_%d' % (rn, 'room' if c.is_room else 'obj', e.name, c.placement.index), e.mesh, m))
    write_obj(out, items)
    if coll_out:
        citems = []
        for c in lv.static_cels():
            coll = c.file.collision(c.entity)
            if coll is not None:
                citems.append(('coll_%s_%d' % (c.entity.name, c.placement.index), coll, c.world_collision))
        write_obj(coll_out, [], citems)
    return items


# ---------------------------------------------------------------------------------------------------------------
# Validation over all bundles
# ---------------------------------------------------------------------------------------------------------------
def validate(verbose=False, lvl=LVL):
    import glob
    C = collections.Counter()
    types = collections.Counter()
    keysets = collections.defaultdict(set)
    problems = []
    seen = set()
    link_stats = collections.Counter()
    for fn in sorted(glob.glob(os.path.join(lvl, '07*.bin'))):
        lv = Level(fn)
        C['bundles'] += 1
        if lv.map is None:
            problems.append('%s: no map file' % lv.name)
        C['dup_hash'] += len(lv.dup_hashes)
        for f in lv.files:
            key = hashlib.md5(f.data).digest()        # dedupe by content: many pack names differ between bundles
            distinct = key not in seen
            seen.add(key)
            for pl in f.placements:
                C['placements'] += 1
                ts = pl.type_split()
                label = ts['kind'] if 'id' not in ts else (
                    '%s(0x%x)' % (ts['kind'], pl.type & 0xffff) if ts['static'] or ts['unplaced'] else
                    '%s %d' % (ts['kind'], ts['id']))
                if ts.get('other_bits'):
                    problems.append('%s %s #%d: type 0x%x has unexpected bits' % (lv.name, f.name, pl.index, pl.type))
                if distinct:
                    types[(label, 'map' if f.is_map else 'pack')] += 1
                    keysets[label].update(k for k, v in pl.kv)
                if any(k > 32 for k, v in pl.kv):
                    problems.append('%s %s #%d: key beyond the level_tag' % (lv.name, f.name, pl.index))
                if pl.is_static or pl.is_dynamic:
                    r = lv.resolve(pl, pl.is_static)
                    if r is None:
                        why = 'index' if pl.glist_hash == 0xffffffff else 'hash'
                        C['unresolved_%s_%s_%s' % ('static' if pl.is_static else 'dynamic',
                                                   'map' if f.is_map else 'pack', why)] += 1
                        if pl.is_static and pl.type != 0xffffffff or (pl.is_dynamic and f.is_map and verbose):
                            problems.append('%s %s #%d %s: glist %s ent %d unresolved' % (
                                lv.name, f.name, pl.index, label, '%08x' % pl.glist_hash, pl.ent))
                    elif pl.glist_hash != 0xffffffff and r[0] is not f:
                        C['glist_from_other_file'] += 1
                if pl.path_frames and f.is_map:
                    C['with_path'] += 1
                    if pl.path_index >= len(f.blocks[0x19]):
                        problems.append('%s #%d: path index %d of %d' % (lv.name, pl.index, pl.path_index,
                                                                         len(f.blocks[0x19])))
                    else:
                        nfr = len(decode_path(f.data, f.blocks[0x19][pl.path_index]))
                        C['path_frames==n' if nfr == pl.path_frames else 'path_frames!=n'] += 1
                if pl.is_static and pl.type != 0xffffffff:
                    if any(abs(s - 1) > 1e-6 for s in pl.scale):
                        C['static_scale!=1'] += 1
                    if sum(abs(a) > 1e-6 for a in pl.rot) > 1:
                        C['static_multi_axis_rot'] += 1
            # blocks
            for b in f.blocks[0x19]:
                if (b.size - 4) % 28:
                    problems.append('%s %s: path block not a multiple of 28' % (lv.name, f.name))
                C['paths'] += 1
            if not f.is_map:
                for t in (0x05, 0x19, 0x26, 0x28):      # handle_block_id ignores these unless isMap
                    if f.blocks[t] and distinct:
                        C['pack_block_0x%02x_ignored' % t] += len(f.blocks[t])
                for b in f.blocks[0x30]:                  # particles are registered from packs too
                    ps, gap = decode_particles(f.data, b)
                    if distinct:
                        C['particle_defs_in_packs'] += len(ps)
                    if gap:
                        problems.append('%s %s: particles leave %d bytes' % (lv.name, f.name, gap))
                n = sum(struct.unpack_from('<I', f.data, b.off + 4)[0] for b in f.blocks[0x21])
                if n:
                    C['pack_portals'] += n
            if distinct:
                for e in f.entities:
                    if e.coll_block is not None:
                        coll = f.collision(e)
                        C['coll_blocks'] += 1
                        C['coll_tris'] += coll.nTris
                        errs = coll.validate()
                        if errs:
                            C['coll_bad'] += 1
                            if len([p for p in problems if 'collision' in p]) < 20:
                                problems.append('%s %s ent %d %s collision: %s' % (lv.name, f.name, e.index, e.name,
                                                                                  '; '.join(errs)))
                f._coll.clear()
        # level-wide
        if lv.map:
            m = lv.map
            ap = lv.ai_paths()
            if ap is not None:
                C['ai_path_blocks'] += 1
                C['ai_paths'] += sum(1 for p in ap['paths'] if not p['bound'])
                C['ai_bounds'] += sum(1 for p in ap['paths'] if p['bound'])
                if not ap['end_ok']:
                    problems.append('%s: AI paths end %s bytes before the block end' % (lv.name, ap.get('end_gap')))
                for p in ap['paths']:
                    for a, b in p['links']:
                        if a >= len(p['nodes']) or b >= len(p['nodes']):
                            problems.append('%s AI path %s: link to a missing node' % (lv.name, p['name']))
                            break
            for b in m.blocks[0x26]:
                n = struct.unpack_from('<I', m.data, b.off + 4)[0]
                if b.size != 8 + 32 * n:
                    problems.append('%s: radiator block size' % lv.name)
                C['radiators'] += n
            for b in m.blocks[0x28]:
                n = struct.unpack_from('<I', m.data, b.off + 4)[0]
                if b.size != 8 + 32 * n:
                    problems.append('%s: sound block size' % lv.name)
                C['map_sounds'] += n
            for b in m.blocks[0x30]:
                ps, gap = decode_particles(m.data, b)
                C['particle_defs'] += len(ps)
                if gap:
                    problems.append('%s: particles leave %d bytes' % (lv.name, gap))
            cels = lv.static_cels()
            rooms = [c for c in cels if c.is_room]
            C['static_cels'] += len(cels)
            C['rooms'] += len(rooms)
            for p in lv.portals():
                C['portals'] += 1
                if p['stale']:
                    C['portal_no_cel_reuses_previous'] += 1
                elif not (p['roomA'].is_room and p['roomB'].is_room):
                    C['portal_to_non_room_cel'] += 1
            if verbose:
                lv.link_rooms()
                for c in cels:
                    if (c.bits & 0xffff) in LINKED_TYPES:
                        src = 'map' if c.file.is_map else 'pack'
                        link_stats['%s_linked' % src if c.room else '%s_no_room' % src] += 1
                        if c.room is None and c.drawable:
                            link_stats['%s_no_room_but_drawable' % src] += 1
                    if c.drawn:
                        link_stats['drawn_cels'] += 1
                        if c.eflags & 8:
                            link_stats['drawn_billboard_flag8'] += 1
                        if c.eflags & 4:
                            link_stats['drawn_jitter_flag4'] += 1
                        if any(abs(a) > 1e-6 for a in c.rot):
                            link_stats['drawn_rotated'] += 1
                        if any(abs(a) > 1e-6 for a in c.pos):
                            link_stats['drawn_translated'] += 1
    print('== counts')
    for k, v in sorted(C.items()):
        print('  %-40s %d' % (k, v))
    if link_stats:
        print('== room linking (approximation of build_link_objects_to_rooms)')
        for k, v in sorted(link_stats.items()):
            print('  %-40s %d' % (k, v))
    print('== placement types over distinct files (label, file kind): count; keys used')
    for (label, kind), v in sorted(types.items(), key=lambda kv: (-kv[1], kv[0])):
        ks = sorted(keysets[label])
        print('  %-34s %-4s %6d  keys %s' % (label, kind, v, (('%d-%d' % (ks[0], ks[-1])) if ks else '-')))
    print('== problems: %d' % len(problems))
    for p in problems[:200]:
        print('  ' + p)
    return problems


# ---------------------------------------------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------------------------------------------
def main(argv):
    if not argv or argv[0] in ('-h', '--help'):
        print(__doc__)
        return 0
    cmd = argv[0]
    if cmd == 'validate':
        validate(verbose='-v' in argv)
        return 0
    if cmd == 'dump':
        lv = Level(argv[1])
        lv.link_rooms()
        out = dict(level=lv.name, name=LEVEL_NAMES.get(lv.hash), fog=lv.fog(),
                   files=[dict(order=f.order, name=f.name, entry_type=f.entry_type, entities=len(f.entities),
                               placements=len(f.placements)) for f in lv.files],
                   placements=[pl.to_json(lv) for pl in lv.placements()],
                   static_cels=[dict(file=c.file.name, placement=c.placement.index, entity=c.entity.name,
                                     bits='0x%x' % c.bits, room=(c.room.entity.name if c.room else None),
                                     drawn=c.drawn, world=c.world, ambient=c.ambient)
                                for c in lv.static_cels()],
                   portals=[dict(celA=p['celA'], celB=p['celB'], flags=p['flags'], quad=p['quad'])
                            for p in lv.portals()],
                   radiators=lv.radiators(), map_sounds=lv.map_sounds())
        s = json.dumps(out, indent=1)
        if len(argv) > 2:
            open(argv[2], 'w').write(s)
        else:
            print(s)
        return 0
    if cmd == 'obj':
        lv = Level(argv[1])
        coll = argv[argv.index('--coll') + 1] if '--coll' in argv else None
        items = level_obj(lv, argv[2], coll, all_drawable='--all' in argv)
        print('%s: %d static pieces written to %s' % (lv.name, len(items), argv[2]))
        return 0
    print('unknown command %s' % cmd)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
