"""Step-by-step replay of the uw_mis11 cave-wall clip: the car moves along the recorded heading from B (constant
speed, or from rest with constant acceleration), and every 50 Hz step does what RigidBody::CollideWithWorld (0xb1420)
does: WCollider::Refresh (region), then 11 segments centre -> test point + v*dt through WCollider::GetWorldNormal.
No collision response is modelled: the question is only whether ANY segment reports the wall before the car centre
is through it.

    python clip_sim.py                # the table of speeds, as-is and with the two fixes
"""
import replay as rp
from replay import *

B = (1338.976, -304.836, 1293.299)
M = (1343.889, -309.236, 1292.302)
A = (1346.820, -311.517, 1291.707)
H = norm((0.7524, -0.6408, -0.1527))

INST = T.instances[28]
WALL = [INST.to_world(v) for v in tri_list(INST.article(), 22)[8][:3]]
WN = norm(cross(sub(WALL[1], WALL[0]), sub(WALL[2], WALL[0])))


def side(p):
    """distance of p from the wall plane, positive on B's side"""
    return -dot(sub(p, WALL[0]), WN)


def octagonal(dx, dz):
    return max(dx, dz) + 0.25 * min(dx, dz)


def euclid(dx, dz):
    return math.hypot(dx, dz)


def world_xz_radius(inst):
    art = inst.article()
    c = inst.position()
    return max(math.hypot(w[0] - c[0], w[2] - c[2]) for _, verts, _ in art.strips for w in map(inst.to_world, verts))


def patched_list(metric, radius_of):
    """instance_strip_list with a different broad-phase metric / radius (for the what-if runs)."""
    def f(p, R, only=None):
        seen = set(); out = []; cands = []
        for c in cells_box(p, R):
            cands += T.node_list(c, 0)
        for i in cands:
            if i in seen:
                continue
            seen.add(i)
            inst = T.instances[i]; art = inst.article()
            if art is None:
                continue
            c = inst.position()
            if metric(abs(c[0] - p[0]), abs(c[2] - p[2])) >= R + radius_of(inst):
                continue
            lp = inst.to_local(p); strips = []
            for s, (sc, sr, _) in enumerate(art.spheres):
                d = sub(sc, lp)
                if dot(d, d) >= (sr + R) ** 2:
                    continue
                for a, b, c_, att in tri_list(art, s):
                    cen = mul(add(add(a, b), c_), 1 / 3.0); rr = att[2] / 16.0 + R; dd = sub(cen, lp)
                    if dot(dd, dd) < rr * rr:
                        strips.append(s); break
            if strips:
                out.append((i, strips))
        return out
    return f


def run(v0, acc=0.0, start_back=0.0, variant='as-is', verbose=False, fwd=None, roll=0.0):
    if variant == 'as-is':
        lister = instance_strip_list
    elif variant == 'euclid':
        lister = patched_list(euclid, lambda i: i.radius)
    elif variant == 'world-radius':
        cache = {}
        lister = patched_list(octagonal, lambda i: cache.setdefault(i.index, max(i.radius, world_xz_radius(i))))
    rp.instance_strip_list = lister           # Collider.refresh looks it up by module global
    ax = car_axes(fwd or H, roll)
    col = Collider()
    pos = add(B, mul(H, -start_back))
    v = v0
    step = 0
    first_hit = None
    listed_28 = 0
    while side(pos) > 0 and step < 2000:     # until the centre is through the wall plane
        vel = mul(H, v)
        col.refresh(pos, RADIUS)
        has28 = any(i == 28 for i, _ in col.list)
        listed_28 += has28
        hits = []
        for n, e in zip(POINT_NAMES, test_points(pos, ax, vel)):
            t, best, allc, L = world_hit(pos, e, col.list)
            if t is not None:
                hits.append((n, round(t, 2), best[1]))
        if verbose:
            print('step %3d v=%5.2f dist-to-wall %.2f region %s R=%.3f %s hits %s' % (
                step, v, side(pos), 'rebuilt' if col.rebuilt else 'kept   ', col.R,
                'ci0028 listed' if has28 else 'ci0028 NOT listed', hits))
        if hits and first_hit is None:
            first_hit = (step, side(pos), v, hits)
        pos = add(pos, mul(vel, DT))
        v += acc * DT
        step += 1
    rp.instance_strip_list = instance_strip_list
    return first_hit, step, v, listed_28


if __name__ == '__main__':
    print('wall triangle ci0028 strip 22 tri 8, world normal', tuple(round(x, 3) for x in WN),
          'B is %.2f m from its plane along the normal' % side(B))
    print('ci0028 +0x3c radius %.3f, world-XZ extent of its geometry %.3f' % (INST.radius, world_xz_radius(INST)))
    for variant in ('as-is', 'euclid', 'world-radius'):
        print('\n== %s ==' % variant)
        for v in (2, 5, 10, 15, 18, 20, 21, 22, 23, 24, 25, 30, 40, 50):
            fh, steps, vend, n28 = run(v, 0.0, 3.0, variant)
            print(' const %4.1f m/s: %s  (%d steps to the plane, ci0028 in the collider list on %d of them)' % (
                v, ('first wall report at step %d, centre %.2f m from the plane, by %s' % (fh[0], fh[1], fh[3]))
                if fh else 'NO segment ever reports the wall: the centre crosses it', steps, n28))
        for acc in (2.75, 9.81 * 2.75):
            fh, steps, vend, n28 = run(0.0, acc, 0.0, variant)
            print(' from rest at B, a=%.2f m/s^2: %s  (reaches %.1f m/s at the plane, %d steps, ci0028 listed on %d)' % (
                acc, ('first wall report at step %d, %.2f m from the plane, %s' % (fh[0], fh[1], fh[3])) if fh
                else 'NO segment ever reports the wall', vend, steps, n28))
