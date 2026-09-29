"""Reference (Python) model of Nightfire's pose -> matrix palette -> vertex skinning pipeline, as specified in
spec-pose.md (Xbox psiBuildMatrixPalette 0xdd290, Quat_* 0xd5800/0xd59e0, MultiplyQuaternionAltOrder 0xd7470,
MatrixMultiplyVector 0xd7500, RotTransMat 0xd6b80, skinned vertex shaders #2/#3/#6).

Conventions (confirmed from the x87 code):
  quaternion = (x, y, z, w)   (w LAST, at +0xc)
  _MATRIX    = 15 floats: rows r0 = m[0..2], r1 = m[4..6], r2 = m[8..10], m[3], m[7], m[11] unused, t = m[12..14]
  row vectors: p' = p * R + t   (row i of R = image of axis i)

Self-test: for every dumped skin with a rigged mesh, pose every bone at its bind pose (identity local rotation,
local translation = skeleton default offset * skinScale) with an identity object matrix, and check that the skin
palette comes out as identity (rotation = I, translation ~ 0) for every used bone. That validates the hierarchy
walk, the quaternion/matrix conventions and the meaning of the skin's per-bone "bind" record together.

usage: python pose_ref.py
"""
import glob, math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import parse_skin, parse_skeleton


def quat_to_rows(q):                       # Quat_QuatToMat 0xd5800 (rotation part only)
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)],
            [2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (w * x + y * z)],
            [2 * (w * y + x * z), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)]]


def qmul(a, b):                            # MultiplyQuaternionAltOrder 0xd7470: Hamilton a (x) b
    ax, ay, az, aw = a; bx, by, bz, bw = b
    return (aw * bx + bw * ax + ay * bz - az * by,
            aw * by + bw * ay + az * bx - ax * bz,
            aw * bz + bw * az + ax * by - ay * bx,
            aw * bw - ax * bx - ay * by - az * bz)


def xform(R, t, v):                        # MatrixMultiplyVector 0xd7500: v*R + t
    return [v[0] * R[0][c] + v[1] * R[1][c] + v[2] * R[2][c] + t[c] for c in range(3)]


def mat_to_quat(R):                        # Quat_MatToQuat 0xd5ad0
    tr = R[0][0] + R[1][1] + R[2][2]
    if tr >= 0:
        s = math.sqrt(tr + 1.0); w = s * 0.5; s = 0.5 / s
        return ((R[1][2] - R[2][1]) * s, (R[2][0] - R[0][2]) * s, (R[0][1] - R[1][0]) * s, w)
    i = 1 if R[0][0] < R[1][1] else 0
    if R[i][i] < R[2][2]: i = 2
    j = (i + 1) % 3; k = (j + 1) % 3
    s = math.sqrt(R[i][i] - (R[j][j] + R[k][k]) + 1.0)
    q = [0, 0, 0, 0]; q[i] = s * 0.5
    if s != 0: s = 0.5 / s
    q[3] = (R[j][k] - R[k][j]) * s
    q[j] = (R[i][j] + R[j][i]) * s
    q[k] = (R[i][k] + R[k][i]) * s
    return tuple(q)


def build_palette(hier, trans, quats, obj_R, obj_t, bind_t):
    """psiBuildMatrixPalette. hier = skin bone bytes (bit7 used, low7 parent/0x7f root); trans/quats = the pose
    buffer (local, already skin-scaled); obj = object matrix (gameObj+0x70); bind_t = skin per-bone record t.
    Returns (boneMtxs, palette) as lists of (R, t) (None for unused bones - the original leaves them stale)."""
    n = len(hier)
    wq = [None] * n; bone = [None] * n; pal = [None] * n
    for b in range(n):
        if not hier[b] & 0x80: continue
        p = hier[b] & 0x7f
        if p == 0x7f:
            R = quat_to_rows(quats[b])
            R = [[sum(R[r][k] * obj_R[k][c] for k in range(3)) for c in range(3)] for r in range(3)]  # RotTransMat
            t = [trans[b][c] + obj_t[c] for c in range(3)]       # NOTE: root translation NOT rotated by obj
            bone[b] = (R, t); wq[b] = mat_to_quat(R)
        else:
            wq[b] = qmul(wq[p], quats[b])
            R = quat_to_rows(wq[b])
            t = xform(bone[p][0], bone[p][1], trans[b])
            bone[b] = (R, t)
            # (head tracking at b==5 / aiming at b==2 would modify wq[b], bone[b] here)
    for b in range(n):
        if hier[b] & 0x80 and bind_t is not None:
            R, t = bone[b]
            pal[b] = (R, xform(R, t, bind_t[b]))
    return bone, pal


def main():
    sk = parse_skeleton.load_table()
    files = sorted(glob.glob(parse_skin.DUMP + '/05*.bin'))
    ok = tested = 0; worst_all = 0
    I = [[1, 0, 0], [0, 1, 0], [0, 0, 1]]
    for f in files:
        s = parse_skin.parse(open(f, 'rb').read(), sk)
        if not s['bind']: continue
        tested += 1
        skel = sk[s['skel']]
        trans = [[skel['offsets'][b][k] * s['scale'][k] for k in range(3)] for b in range(s['numBones'])]
        quats = [(0.0, 0.0, 0.0, 1.0)] * s['numBones']
        _, pal = build_palette(s['bones'], trans, quats, I, [0, 0, 0], [bt for bt, _ in s['bind']])
        worst = 0
        for b, m in enumerate(pal):
            if m is None: continue
            R, t = m
            worst = max(worst, max(abs(R[r][c] - I[r][c]) for r in range(3) for c in range(3)), max(abs(x) for x in t))
        worst_all = max(worst_all, worst)
        ok += worst < 0.02
    print('%d/%d rigged skins give an identity palette in bind pose (worst deviation %.3g)' % (ok, tested, worst_all))


if __name__ == '__main__':
    main()
