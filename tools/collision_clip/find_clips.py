"""Search a track for walls the car can pass through the way it does at the uw_mis11 cave seam
(docs/driving-collision.md, "Why the car goes through the cave wall").

    python find_clips.py [track] [--region R]      track defaults to uw_mis11; R, the collider's region radius at
                                                   crawling speed, defaults to 2.85 m

The flaw: WCollisionMgr::GetInstanceListGuts (0xc43c0) keeps an instance only when
max(|dx|,|dz|) + 0.25*min(|dx|,|dz|) < R + radius(+0x3c), measured from the car to the instance's centre, and some
instances' geometry reaches beyond that radius in world XZ. A triangle out there is never tested while the car is
near it, so the car's centre can pass through it. For every such triangle this samples points on it and keeps the
ones where:
  1. the instance is not listed with the car's centre anywhere from 3 m in front of the point up to the point
     itself, approaching along the triangle's normal (so a slow, head-on approach never sees it);
  2. one side is inside the level - rays from 1 m out on that side hit geometry in every axis direction; what lies
     straight out through the wall is reported (the void, or geometry some way on); --void-only keeps only the void;
  3. no triangle of an instance that IS listed along that approach crosses the path from 3 m in front to 3 m
     behind (another piece closing the gap).
Points are grouped by instance and strip; for each group it prints the worst-case margin and a Teleport= line
(3 m inside, facing the wall) to try in the game.
"""

import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import carp_collision as cc

args = [a for a in sys.argv[1:] if not a.startswith("--")]
track_name = args[0] if args else "uw_mis11"
REGION = float(sys.argv[sys.argv.index("--region") + 1]) if "--region" in sys.argv else 2.85
T = cc.Track(os.path.join(ROOT, "Release", "dump_driving", "data", "track", track_name + ".crp"))


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def add(a, b, k=1.0):
    return (a[0] + k * b[0], a[1] + k * b[1], a[2] + k * b[2])


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
    for pc in (only or pieces):
        # sphere cull
        oc = (pc.centre[0] - o[0], pc.centre[1] - o[1], pc.centre[2] - o[2])
        t = dot(oc, d)
        closest = dot(oc, oc) - t * t
        if closest > pc.reach * pc.reach or t < -pc.reach or t > maxlen + pc.reach:
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


candidates = [pc for pc in pieces if pc.xz_reach > pc.inst.radius + 1e-3]
print("%s: %d pieces, %d with geometry beyond their radius in world XZ (collider region radius %.2f m)"
      % (track_name, len(pieces), len(candidates), REGION))
for pc in sorted(candidates, key=lambda p: p.inst.radius - p.xz_reach):
    print("  ci%04d %-32s radius %6.2f  world-XZ reach %6.2f  (+%.2f)"
          % (pc.inst.index, pc.inst.article_name, pc.inst.radius, pc.xz_reach, pc.xz_reach - pc.inst.radius))

SAMPLES = [(u / 4.0, v / 4.0) for u in range(5) for v in range(5) if u + v <= 4]
spots = {}
checked = 0
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
                # 1: unlisted all the way in, head-on
                margins = [octagonal(add(p, n_in, k * 0.25), pc.centre) - limit for k in range(13)]
                if min(margins) <= 0:
                    continue
                # 2: inside on n_in's side; what lies straight out the other (None: the void)
                behind = ray_hits(add(p, n_in, -0.05), (-n_in[0], -n_in[1], -n_in[2]), 400.0)
                if behind is not None and "--void-only" in sys.argv:
                    continue
                if not enclosed(add(p, n_in, 1.0)):
                    continue
                # 3: nothing listed closes the gap along the approach
                start = add(p, n_in, 3.0)
                listed = [q for q in pieces if q is not pc and
                          min(octagonal(add(p, n_in, k * 0.5), q.centre) for k in range(7)) < q.inst.radius + REGION]
                blocker = ray_hits(start, (-n_in[0], -n_in[1], -n_in[2]), 6.0, only=listed)
                if blocker is not None:
                    continue
                key = (pc.inst.index, si)
                spots.setdefault(key, []).append((min(margins), p, n_in, ti, behind))

print("\nsampled %d points on triangles reaching beyond their piece's radius" % checked)
print("%d strips with points a slow head-on approach passes through:\n" % len(spots))
for (idx, si), pts in sorted(spots.items(), key=lambda kv: -max(x[0] for x in kv[1])):
    inst = T.instances[idx]
    margin, p, n_in, ti, behind = max(pts, key=lambda x: x[0])
    tris = sorted({x[3] for x in pts})
    voids = sum(1 for x in pts if x[4] is None)
    start = add(p, n_in, 3.0)
    heading = (-n_in[0], -n_in[1], -n_in[2])
    print("ci%04d %-30s strip %2d triangles %s: %d points, best margin %.2f m (unlisted by that much at the wall)"
          % (idx, inst.article_name, si, tris, len(pts), margin))
    print("   wall point (%.2f, %.2f, %.2f); behind it: %s" % (p + ("the void (nothing within 400 m)" if behind is None
          else "geometry %.1f m further on" % behind,)) + ("; %d of %d points open onto the void" % (voids, len(pts))))
    print("   Teleport=%.3f,%.3f,%.3f,%.4f,%.4f,%.4f" % (start + heading))
