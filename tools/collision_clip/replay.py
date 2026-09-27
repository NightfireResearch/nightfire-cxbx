"""Replay of the Driving.xbe car-vs-world collision (RigidBody::CollideWithWorld 0xb1420) on uw_mis11.
See clip-explanation.md for the addresses behind each step. Standard library + tools/carp_collision.py."""
import sys, math
import os
_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))   # the repository
sys.path.insert(0, os.path.join(_ROOT, "tools"))
import carp_collision as cc

T = cc.Track(os.path.join(_ROOT, "Release", "dump_driving", "data", "track", "uw_mis11.crp"))


def add(a, b): return (a[0] + b[0], a[1] + b[1], a[2] + b[2])
def sub(a, b): return (a[0] - b[0], a[1] - b[1], a[2] - b[2])
def mul(a, s): return (a[0] * s, a[1] * s, a[2] * s)
def dot(a, b): return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]
def cross(a, b): return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
def norm(a):
    l = math.sqrt(dot(a, a))
    return mul(a, 1 / l)


# vanquishsub.crp 'Base' bbox -> RSceneObj::GetBoundingDimensions (0x8dfd0) half extents -> RigidBody ctor (0xb0b20)
HX, HY, HZ = 0.9193576574325562, 0.5778406858444214, 2.3167762756347656
RADIUS = math.sqrt(HX * HX + HY * HY + HZ * HZ)   # RigidBody +0x7c = |half extents|
DT = 0.02                                          # Sim step (50 Hz), [0x234e30]
MASK = 0x20                                        # CollideWithWorld sets manager +0x2c = 0x20 around the query


def car_axes(fwd, roll_deg=0.0):
    f = norm(fwd)
    r = norm(cross((0, 1, 0), f))       # left-handed: x = y cross z
    u = cross(f, r)
    if roll_deg:
        c, s = math.cos(math.radians(roll_deg)), math.sin(math.radians(roll_deg))
        r, u = add(mul(r, c), mul(u, s)), add(mul(u, c), mul(r, -s))
    return r, u, f


POINT_NAMES = ['RL-bot', 'RR-bot', 'FR-bot', 'FL-bot', 'FL-top', 'FR-top', 'RR-top', 'RL-top', 'nose', 'right', 'left']


def test_points(pos, axes, vel):
    """UpdatePositionAndOrientation 0xb0ec0 corners (x extent +0.18 for flags6d<4), then CollideWithWorld 0xb1420:
    points 0..7 = pos+corner, 8 = pos + fwd*(hz+0.05), 9/10 = pos +- right*hx; each + vel*dt."""
    r, u, f = axes
    hx = HX + 0.18
    loc = [(-hx, -HY, -HZ), (hx, -HY, -HZ), (hx, -HY, HZ), (-hx, -HY, HZ),
           (-hx, HY, HZ), (hx, HY, HZ), (hx, HY, -HZ), (-hx, HY, -HZ)]
    pts = [add(pos, add(add(mul(r, a), mul(u, b)), mul(f, c))) for a, b, c in loc]
    pts.append(add(pos, mul(f, HZ + 0.05)))
    pts.append(add(pos, mul(r, HX)))
    pts.append(add(pos, mul(r, -HX)))
    return [add(p, mul(vel, DT)) for p in pts]


# ---- WCollider region (Refresh 0xbe4b0, InRegion 0xbd820, FUN_000bd690, PrepareRegion 0xbe2d0) ----
class Collider:
    def __init__(self):
        self.valid = False
        self.prev = None
        self.centre = None
        self.R = 0
        self.list = None
        self.rebuilds = 0
        self.rebuilt = False

    def refresh(self, pos, r):
        inside = (self.valid and (self.R - r) >= 0 and
                  dot(sub(pos, self.centre), sub(pos, self.centre)) < (self.R - r) ** 2)
        self.rebuilt = not inside
        if not inside:
            if self.valid:
                d = sub(pos, self.prev)
                l = min(math.sqrt(dot(d, d)), 1.0)
                self.R = (l + r) * 1.1
                self.centre = add(pos, mul(d, 1.1))
            else:
                self.centre = pos
                self.R = r * 1.1
            self.list = instance_strip_list(self.centre, self.R)
            self.valid = True
            self.rebuilds += 1
        self.prev = pos


def cells_box(p, R):
    """WGrid::FindNodes(point, radius) 0xc6450 -> FindNodesBox 0xc62b0 (RangeCheckROWCOL 0xc5710)."""
    def rc(x, z):
        col = int((x - T.origin[0]) * T.inv_cell)
        row = int((z - T.origin[2]) * T.inv_cell)
        return min(max(row, 0), T.rows - 1), min(max(col, 0), T.cols - 1)
    r0, c0 = rc(p[0] - R, p[2] - R)
    r1, c1 = rc(p[0] + R, p[2] + R)
    return [r * T.cols + c for r in range(min(r0, r1), max(r0, r1) + 1) for c in range(min(c0, c1), max(c0, c1) + 1)]


def tri_list(art, s):
    sflags, verts, attrs = art.strips[s]
    return [(verts[i], verts[i + 1], verts[i + 2], attrs[i]) for i in range(len(verts) - 2)]


def instance_strip_list(p, R, only=None):
    """GetInstanceList 0xc4510 -> GetInstanceListGuts 0xc43c0 (octagonal test) -> GetInstanceStripList 0xc4120
    (WCollider flag61 = 0: 3D sphere test, then the strip is kept if any triangle centroid is within tri radius + R)."""
    seen = set()
    out = []
    cands = []
    for c in cells_box(p, R):
        cands += T.node_list(c, 0)
    cands += [i for k, i in T.dynamic if k == 0]   # dynamic lists (conservative)
    for i in cands:
        if i in seen or (only is not None and i not in only):
            continue
        seen.add(i)
        inst = T.instances[i]
        art = inst.article()
        if art is None:
            continue
        c = inst.position()
        dx, dz = abs(c[0] - p[0]), abs(c[2] - p[2])
        if max(dx, dz) + 0.25 * min(dx, dz) >= R + inst.radius:
            continue
        lp = inst.to_local(p)
        strips = []
        for s, (sc, sr, _) in enumerate(art.spheres):
            d = sub(sc, lp)
            if dot(d, d) >= (sr + R) ** 2:
                continue
            for a, b, c_, att in tri_list(art, s):
                cen = mul(add(add(a, b), c_), 1 / 3.0)
                rr = att[2] / 16.0 + R
                dd = sub(cen, lp)
                if dot(dd, dd) < rr * rr:
                    strips.append(s)
                    break
        if strips:
            out.append((i, strips))
    return out


def face_normal(a, b, c):
    """FUN_0005d3f0 / FUN_000beff0: (v1-v0) x (v0-v2), unit, forced +Y, y clamped to 0.9999 (not renormalised)."""
    n = cross(sub(b, a), sub(a, c))
    l = math.sqrt(dot(n, n))
    n = (0, 1, 0) if l == 0 else mul(n, 1 / l)
    if n[1] < 0:
        n = mul(n, -1)
    if n[1] >= 0.9999:
        n = (n[0], 0.9999, n[2])
    return n


def seg_space(start, end):
    """WWorldMath::MakeSegSpaceMatrix 0xd2d70: Y = unit(start-end), origin = start."""
    y = norm(sub(start, end))
    x0 = (0.0, 1.0, 0.0) if y[1] <= 0.5 else (1.0, 0.0, 0.0)
    z = norm(cross(x0, y))
    x = cross(y, z)
    return x, y, z


def to_seg(p, start, axes):
    d = sub(p, start)
    return (dot(d, axes[0]), dot(d, axes[1]), dot(d, axes[2]))


def inside2s(p, a, b, c):
    """FUN_000bebd0: two-sided XZ point-in-triangle."""
    e0 = (a[0] - b[0]) * (p[2] - b[2]) - (a[2] - b[2]) * (p[0] - b[0])
    e1 = (b[0] - c[0]) * (p[2] - c[2]) - (p[0] - c[0]) * (b[2] - c[2])
    e2 = (p[2] - a[2]) * (c[0] - a[0]) - (p[0] - a[0]) * (c[2] - a[2])
    return (e0 >= 0 and e1 >= 0 and e2 >= 0) or (e0 <= 0 and e1 <= 0 and e2 <= 0)


def closest_face(start, end, clist):
    """WWorldPos::FindClosestFace(seg) 0xd3110 -> FindFaceInCInst 0xc01d0 -> FindFaceInTriStrip 0xbf0e0.
    Returns best (f = d+0.5, inst, strip, tri, world verts, attrs), every pierced triangle as (d, inst, strip, tri,
    attrs, strip_sphere_passed), and the segment length."""
    ax = seg_space(start, end)
    L = math.sqrt(dot(sub(end, start), sub(end, start)))
    best = None
    allc = []
    for i, strips in clist:
        inst = T.instances[i]
        art = inst.article()
        ls = inst.to_local(start)
        le = inst.to_local(end)
        dv = sub(le, ls)
        inv = 1.0 / dot(dv, dv)
        ibest = None
        for s in strips:
            sc, sr, _ = art.spheres[s]
            t = min(max(dot(sub(sc, ls), dv) * inv, 0.0), 1.0)      # FUN_000bd470
            q = add(ls, mul(dv, t))
            dq = sub(sc, q)
            sphere_ok = dot(dq, dq) < sr * sr
            sbest = None
            for k, (a, b, c, att) in enumerate(tri_list(art, s)):
                wa, wb, wc = [inst.to_world(v) for v in (a, b, c)]
                sa, sb, sc_ = [to_seg(v, start, ax) for v in (wa, wb, wc)]
                if not inside2s((0, 0, 0), sa, sb, sc_):
                    continue
                n = face_normal(sa, sb, sc_)
                py = sa[1] if n[1] == 0 else sa[1] - ((0 - sa[0]) * n[0] + (0 - sa[2]) * n[2]) / n[1]  # GetPlaneY 0xd2be0
                d = 0.0 - py
                allc.append((d, i, s, k, att, sphere_ok))
                if not sphere_ok:
                    continue
                if d < (sbest[0] if sbest else 1e38) and d > -1.0 and (att[1] & MASK) == 0:
                    sbest = (d, k, (wa, wb, wc), att)
            if sbest:
                f = sbest[0] + 0.5
                if f > -1.0 and (ibest is None or f < ibest[0]):
                    ibest = (f, s) + sbest[1:]
        if ibest and (best is None or ibest[0] < best[0]):
            best = (ibest[0], i) + ibest[1:]
    return best, allc, L


def world_hit(start, end, clist):
    """WCollisionMgr::GetWorldNormal 0xc0e00: closest face along the line, then IntersectSegPlane 0xd2c20 (0<=t<1)."""
    best, allc, L = closest_face(start, end, clist)
    if best is None:
        return None, best, allc, L
    f, i, s, k, (wa, wb, wc), att = best
    n = face_normal(wa, wb, wc)
    den = dot(n, sub(end, start))
    if den == 0:
        return None, best, allc, L
    t = dot(n, sub(wa, start)) / den
    return (t if 0 <= t < 1 else None), best, allc, L
