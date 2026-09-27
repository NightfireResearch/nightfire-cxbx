"""Search a track for walls the car can pass through the way it does at the uw_mis11 cave seam
(docs/driving-collision.md, "Why the car goes through the cave wall").

    python find_clips.py [track] [--region R] [--all] [--dense] [--angled] [--void-only]

      track        defaults to uw_mis11
      --region R   the collider's region radius at crawling speed (default 2.85 m)
      --all        search every piece, not only those whose geometry overhangs their stored radius (the distance
                   approximation can over-read by up to ~3%, which for a big piece exceeds the region radius)
      --dense      45 samples a triangle instead of 15
      --angled     approaches up to 70 degrees off the wall's normal, not only head-on
      --void-only  keep only walls with nothing behind them (out of bounds)

The flaw: WCollisionMgr::GetInstanceListGuts (0xc43c0) keeps an instance only when
max(|dx|,|dz|) + 0.25*min(|dx|,|dz|) < R + radius(+0x3c), measured from the car to the instance's centre, and some
instances' geometry reaches beyond that. A triangle out there is never tested while the car is near it, so the car's
centre can pass through it. For every such triangle this samples points on it and keeps the ones where:
  1. one side is inside the level: rays from 1 m out on that side hit geometry in every axis direction;
  2. along an approach from that side, the instance is not listed with the car's centre anywhere from 3 m out up to
     the point itself (so a slow approach never sees the wall);
  3. no triangle of an instance that IS listed along that approach crosses the path from 3 m out to 3 m past
     (another piece closing the gap).
What lies on past the wall along the approach is reported: the void (nothing within 400 m: out of bounds) or
geometry some way on (into another space). Points are grouped by instance and strip; for each group it prints the
best margin and a Teleport= line (3 m back along the best approach, facing the wall) to try in the game.
Limits: file positions (moving pieces and swapped articles are not modelled); only the car's centre line is
checked for blockers; reachability in normal play is not judged.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import carp_collision as cc

args = [a for a in sys.argv[1:] if not a.startswith("--") and not a.replace(".", "").isdigit()]
track_name = args[0] if args else "uw_mis11"
REGION = float(sys.argv[sys.argv.index("--region") + 1]) if "--region" in sys.argv else 2.85
ALL = "--all" in sys.argv
DENSE = "--dense" in sys.argv
ANGLED = "--angled" in sys.argv
VOID_ONLY = "--void-only" in sys.argv
T = cc.Track(os.path.join(ROOT, "Release", "dump_driving", "data", "track", track_name + ".crp"))


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def add(a, b, k=1.0):
    return (a[0] + k * b[0], a[1] + k * b[1], a[2] + k * b[2])


def neg(a):
    return (-a[0], -a[1], -a[2])


def norm(a):
    l = math.sqrt(dot(a, a)) or 1.0
    return (a[0] / l, a[1] / l, a[2] / l)


def octagonal(p, c):
    dx, dz = abs(p[0] - c[0]), abs(p[2] - c[2])
    return max(dx, dz) + 0.25 * min(dx, dz)


# World-space triangles per instance, with a bounding sphere that really contains them.
class Piece:
    pass


pieces = []
for inst in T.instances:
    art = inst.article()
    if art is None:
        continue
    pc = Piece()
    pc.inst = inst
    pc.centre = inst.position()
    pc.tris = []
    for si, (sflags, verts, attrs) in enumerate(art.strips):
        w = [inst.to_world(v) for v in verts]
        for i in range(len(verts) - 2):
            surf, fflags, _ = attrs[i]
            if fflags & 0x20:   # the collider's face mask skips these
                continue
            pc.tris.append((w[i], w[i + 1], w[i + 2], si, i))
    pc.reach = max((math.dist(pc.centre, v) for t in pc.tris for v in t[:3]), default=0.0)
    pc.xz_reach = max((math.hypot(v[0] - pc.centre[0], v[2] - pc.centre[2]) for t in pc.tris for v in t[:3]),
                      default=0.0)
    pieces.append(pc)


def ray_hits(o, d, maxlen, only=None):
    """Nearest hit distance of the ray o + t*d (0 < t <= maxlen) against every piece (or `only`), or None."""
    best = None
    for pc in (only if only is not None else pieces):
        oc = (pc.centre[0] - o[0], pc.centre[1] - o[1], pc.centre[2] - o[2])
        t = dot(oc, d)
        if dot(oc, oc) - t * t > pc.reach * pc.reach or t < -pc.reach or t > maxlen + pc.reach:
            continue
        for a, b, c, _, _ in pc.tris:
            e1 = (b[0] - a[0], b[1] - a[1], b[2] - a[2])
            e2 = (c[0] - a[0], c[1] - a[1], c[2] - a[2])
            h = cc.cross(d, e2)
            det = dot(e1, h)
            if abs(det) < 1e-9:
                continue
            f = 1.0 / det
            s = (o[0] - a[0], o[1] - a[1], o[2] - a[2])
            u = f * dot(s, h)
            if u < 0 or u > 1:
                continue
            q = cc.cross(s, e1)
            v = f * dot(d, q)
            if v < 0 or u + v > 1:
                continue
            tt = f * dot(e2, q)
            if 1e-4 < tt <= maxlen and (best is None or tt < best):
                best = tt
    return best


AXES = [(1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]


def enclosed(p):
    return all(ray_hits(p, a, 400.0) is not None for a in AXES)


def approaches(n_in):
    """Directions to back away from the wall along (the car travels the opposite way): the normal, and with
    --angled rings of eight at 30, 50 and 70 degrees off it."""
    yield n_in
    if not ANGLED:
        return
    helper = (1.0, 0.0, 0.0) if abs(n_in[0]) < 0.9 else (0.0, 0.0, 1.0)
    e1 = norm(cc.cross(n_in, helper))
    e2 = cc.cross(n_in, e1)
    for tilt in (30.0, 50.0, 70.0):
        ct, st = math.cos(math.radians(tilt)), math.sin(math.radians(tilt))
        for k in range(8):
            a = 2 * math.pi * k / 8
            yield norm(tuple(ct * n_in[i] + st * (math.cos(a) * e1[i] + math.sin(a) * e2[i]) for i in range(3)))


overhanging = [pc for pc in pieces if pc.xz_reach > pc.inst.radius + 1e-3]
print("%s: %d pieces, %d with geometry beyond their radius in world XZ (collider region radius %.2f m)"
      % (track_name, len(pieces), len(overhanging), REGION))
for pc in sorted(overhanging, key=lambda p: p.inst.radius - p.xz_reach):
    print("  ci%04d %-32s radius %6.2f  world-XZ reach %6.2f  (+%.2f)"
          % (pc.inst.index, pc.inst.article_name, pc.inst.radius, pc.xz_reach, pc.xz_reach - pc.inst.radius))
candidates = pieces if ALL else overhanging
print("searching %d pieces%s%s%s" % (len(candidates), ", dense sampling" if DENSE else "",
                                     ", angled approaches" if ANGLED else "", ", void only" if VOID_ONLY else ""))

STEPS = 8 if DENSE else 4
SAMPLES = [(u / float(STEPS), v / float(STEPS)) for u in range(STEPS + 1) for v in range(STEPS + 1) if u + v <= STEPS]

spots = {}
checked = 0
enclosed_cache = {}
for pc in candidates:
    limit = pc.inst.radius + REGION
    for a, b, c, si, ti in pc.tris:
        if max(octagonal(v, pc.centre) for v in (a, b, c)) <= limit:
            continue   # no part of this triangle can be out of reach
        n = norm(cc.cross((b[0] - a[0], b[1] - a[1], b[2] - a[2]), (c[0] - a[0], c[1] - a[1], c[2] - a[2])))
        for u, v in SAMPLES:
            p = (a[0] + u * (b[0] - a[0]) + v * (c[0] - a[0]), a[1] + u * (b[1] - a[1]) + v * (c[1] - a[1]),
                 a[2] + u * (b[2] - a[2]) + v * (c[2] - a[2]))
            if octagonal(p, pc.centre) <= limit:
                continue
            checked += 1
            for side in (1.0, -1.0):
                n_in = (n[0] * side, n[1] * side, n[2] * side)   # towards the playable side
                if not enclosed(add(p, n_in, 1.0)):
                    continue
                for back in approaches(n_in):
                    # 2: unlisted all the way in along this approach
                    margin = min(octagonal(add(p, back, k * 0.25), pc.centre) - limit for k in range(13))
                    if margin <= 0:
                        continue
                    ahead = neg(back)
                    # what lies on past the wall (None: the void)
                    behind = ray_hits(add(p, ahead, 0.05), ahead, 400.0)
                    if behind is not None and VOID_ONLY:
                        continue
                    # 3: nothing listed closes the gap along the approach
                    listed = [q for q in pieces if q is not pc and
                              min(octagonal(add(p, back, k * 0.5), q.centre) for k in range(7)) < q.inst.radius + REGION]
                    if ray_hits(add(p, back, 3.0), ahead, 6.0, only=listed) is not None:
                        continue
                    spots.setdefault((pc.inst.index, si), []).append((margin, p, back, ti, behind))

print("\nsampled %d points on triangles reaching beyond their piece's reach" % checked)
print("%d strips with points a slow approach passes through:" % len(spots))
print("")
for (idx, si), pts in sorted(spots.items(), key=lambda kv: -max(x[0] for x in kv[1])):
    inst = T.instances[idx]
    margin, p, back, ti, behind = max(pts, key=lambda x: x[0])
    tris = sorted({x[3] for x in pts})
    voids = sum(1 for x in pts if x[4] is None)
    start = add(p, back, 3.0)
    print("ci%04d %-30s strip %2d triangles %s: %d approaches, best margin %.2f m, %d of them into the void"
          % (idx, inst.article_name, si, tris, len(pts), margin, voids))
    print("   wall point (%.2f, %.2f, %.2f); past it: %s" % (p + (
        "the void (nothing within 400 m)" if behind is None else "geometry %.1f m on" % behind,)))
    print("   Teleport=%.3f,%.3f,%.3f,%.4f,%.4f,%.4f" % (start + neg(back)))
