#pragma fp_contract(off)

#include "WorldMath.h"
#include "../platform/RealMath.h"

#include <math.h>
#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------
// WWorldMath (docs/driving/maths.md for the x87 rules). The originals keep differences and products on the x87
// stack across whole expressions and store some of them to floats on the way - sometimes using the stored float
// later and the unrounded value elsewhere. Each is written here in double in the original's order, with the
// stored floats named, so every later use reads the one the original reads.
// ---------------------------------------------------------------------------------------------------------------

static constexpr float kTolerance = 0.0001f;    // 0x38d1b717 (and its negative, 0xb8d1b717)

// The 2D segment tests' acceptance of a numerator against the denominator: in [0, den] when den is positive, in
// [den, 0] when it is negative, with the tolerance at 0 - as two tests in the original's order (NaN passes neither).
static bool WithinSegment(float numerator, float denominator) {
    if (numerator >= -kTolerance && numerator <= denominator)
        return true;
    return numerator <= kTolerance && numerator >= denominator;
}

// x and z differences are kept unrounded for b, stored to floats for a; the first parameter is tested and used as
// a float, the second unrounded.
// FUNC_AT(0x000d1060)
bool FasterSegmentIntersect(const Coord4 *a, const Coord4 *b, Coord4 *hit) {
    const double bz = double(b[1].z) - b[0].z;
    const float ax = a[1].x - a[0].x;
    const float az = a[1].z - a[0].z;
    const double bx = double(b[1].x) - b[0].x;
    const double denominator = ax * bz - bx * az;
    if (denominator == 0.0)
        return false;
    const float dz = a[0].z - b[0].z;
    const float dx = a[0].x - b[0].x;
    const float inverse = float(1.0 / denominator);
    const float ta = float((dz * bx - dx * bz) * inverse);
    if (!(ta >= 0.0f) || !(ta <= 1.0f))
        return false;
    const double tb = (double(dz) * ax - double(dx) * az) * inverse;
    if (!(tb >= 0.0) || !(tb <= 1.0))
        return false;
    if (hit != NULL) {
        float along[3];
        v3sub(1, &a[1], &a[0], along);
        hit->x = float(double(along[0]) * ta + a[0].x);
        hit->y = float(double(along[1]) * ta + a[0].y);
        hit->z = float(double(along[2]) * ta + a[0].z);
    }
    return true;
}

// FUNC_AT(0x000d2860)
bool WWorldMath::SegmentIntersect(const Coord4 *a, const Coord4 *b, Coord4 *hit) {
    const double dz = double(a[0].z) - b[0].z;
    const double bx = double(b[1].x) - b[0].x;
    const double bz = double(b[1].z) - b[0].z;
    const double dx = double(a[0].x) - b[0].x;
    const float numeratorA = float(bx * dz - dx * bz);
    const float ax = a[1].x - a[0].x;
    const float az = a[1].z - a[0].z;
    const float numeratorB = float(double(ax) * dz - double(az) * dx);
    const float denominator = float(double(ax) * bz - double(az) * bx);
    if (denominator == 0.0f)
        return false;
    if (!WithinSegment(numeratorA, denominator) || !WithinSegment(numeratorB, denominator))
        return false;
    if (hit != NULL) {
        const float t = numeratorA / denominator;
        float along[3];
        v3sub(1, &a[1], &a[0], along);
        hit->x = float(double(along[0]) * t + a[0].x);
        hit->y = float(double(along[1]) * t + a[0].y);
        hit->w = 1.0f;
        hit->z = float(double(along[2]) * t + a[0].z);
    }
    return true;
}

// The quadratic in the segment's parameter, |p + t * d|^2 = r^2 with p the first point relative to the centre:
// a = |d|^2, b = 2 p.d, c = |p|^2 - r^2. The original stores a, b, the discriminant and some of the differences to
// floats on the way, and goes on with b both unrounded and stored (b * b is the one times the other).
// FUNC_AT(0x000d29e0)
bool WWorldMath::IntersectCircle(float x1, float z1, float x2, float z2, float centreX, float centreZ, float radius,
                                 float *t1, float *t2) {
    const float radiusSquared = radius * radius;
    const double toCentreX = double(centreX) - x1, toCentreZ = double(centreZ) - z1;
    if (toCentreZ * toCentreZ + toCentreX * toCentreX < radiusSquared) {
        const double endX = double(centreX) - x2, endZ = double(centreZ) - z2;
        if (endZ * endZ + endX * endX < radiusSquared) {
            *t2 = 0.0f;
            *t1 = 0.0f;
            return true;
        }
    }
    const double px = double(x1) - centreX;
    const double qx = double(x2) - centreX;
    const float pz = z1 - centreZ;
    const double qz = double(z2) - centreZ;
    const float dx = float(qx - px);
    const double dz = qz - pz;
    const float a = float(dz * dz + double(dx) * dx);
    double b = double(dx) * px + dz * pz;
    b = b + b;
    const float bStored = float(b);
    const float discriminant = float(b * bStored - ((px * px + double(pz) * pz) - radiusSquared) * a * 4.0f);
    if (discriminant < 0.0f)
        return false;
    const double root = sqrt(double(discriminant));     // REAL_sqrtf's FSQRT, unrounded
    const double inverse = 1.0f / (double(a) + a);
    *t1 = float((-double(bStored) - root) * inverse);
    const double second = (root - bStored) * inverse;
    *t2 = float(second);
    if (*t1 >= 0.0f && *t1 <= 1.0f)
        return true;
    return second >= 0.0 && second <= 1.0;
}

// The direction's z difference is stored to a float and that is what the projection and the result use; its
// square, and everything of x, is unrounded.
// FUNC_AT(0x000d2b70)
void WWorldMath::NearestPointLine2D(const Coord4 *point, const Coord4 *seg, Coord4 *nearest) {
    const double dx = double(seg[1].x) - seg[0].x;
    const double dz = double(seg[1].z) - seg[0].z;
    const float dzStored = float(dz);
    const float t = float(((double(point->z) - seg[0].z) * dzStored + (double(point->x) - seg[0].x) * dx) /
                          (dz * dz + dx * dx));
    const double z = double(dzStored) * t + seg[0].z;
    const double x = dx * t + seg[0].x;
    nearest->y = 0.0f;
    nearest->x = float(x);
    nearest->z = float(z);
}

// FUNC_AT(0x000d2be0)
double WWorldMath::GetPlaneY(const Coord3 *normal, const Coord3 *pointOnPlane, const Coord3 *point) {
    if (normal->y == 0.0f)
        return pointOnPlane->y;
    return pointOnPlane->y - ((double(point->z) - pointOnPlane->z) * normal->z +
                              (double(point->x) - pointOnPlane->x) * normal->x) / normal->y;
}

// The parameter's float is stored first; x uses the unrounded quotient, y and z read the stored one back.
// FUNC_AT(0x000d2c20)
bool WWorldMath::IntersectSegPlane(const Coord4 *from, const Coord4 *to, const Coord3 *pointOnPlane,
                                   const Coord4 *normal, Coord4 *hit, float *t) {
    Coord3 toPlane, along;
    v3sub(1, pointOnPlane, from, &toPlane);
    v3sub(1, to, from, &along);
    const float distance = float(VEC3_Dot(normal, &toPlane));
    const double rate = VEC3_Dot(normal, &along);
    if (rate == 0.0)
        return false;
    const double quotient = distance / rate;
    *t = float(quotient);
    hit->x = float((double(to->x) - from->x) * quotient + from->x);
    hit->y = float((double(to->y) - from->y) * *t + from->y);
    hit->z = float((double(to->z) - from->z) * *t + from->z);
    return *t >= 0.0f && *t <= 1.0f;
}

// FUNC_AT(0x000d2cf0)
void WWorldMath::NearestPointLine3D(const Coord4 *point, const Coord4 *seg, Coord4 *nearest) {
    alignas(16) Coord4 along = {}, offset = {};
    VU0_v4sub(&seg[1], &seg[0], &along);
    VU0_v4sub(point, &seg[0], &offset);
    const float projection = v3dotprod(&offset, &along);
    const float scale = projection / v3dotprod(&along, &along);
    VU0_v4scale(&along, scale, &along);
    VU0_v3add(&along, &seg[0], nearest);
}

// Row 1's y is kept unrounded for the choice of row 0 (x when row 1 leans up, else y); row 2's normalisation skips
// the reciprocal square root for a zero length.
// FUNC_AT(0x000d2d70)
bool WWorldMath::MakeSegSpaceMatrix(const Coord4 *from, const Coord4 *to, MATRIX4 *matrix) {
    if (fabs(double(from->x) - to->x) < kTolerance && fabs(double(from->y) - to->y) < kTolerance &&
        fabs(double(from->z) - to->z) < kTolerance)
        return false;
    float *across = matrix->mtx[0], *along = matrix->mtx[1], *up = matrix->mtx[2], *origin = matrix->mtx[3];

    VU0_v4sub(from, to, along);
    along[3] = 0.0f;
    const double lengthSquared = double(along[0]) * along[0] + double(along[1]) * along[1] +
                                 double(along[2]) * along[2];
    const float scale = VU0_rsqrt(float(lengthSquared));
    along[0] = scale * along[0];
    const double alongY = double(scale) * along[1];
    along[1] = float(alongY);
    along[2] = scale * along[2];
    if (alongY > 0.5f) {
        across[0] = 1.0f;
        across[1] = 0.0f;
    } else {
        across[0] = 0.0f;
        across[1] = 1.0f;
    }
    across[2] = 0.0f;
    across[3] = 0.0f;

    VU0_v4crossprodxyz(across, along, up);
    up[3] = 0.0f;
    const float upSquared = float(double(up[0]) * up[0] + double(up[1]) * up[1] + double(up[2]) * up[2]);
    const float upScale = upSquared == 0.0f ? 0.0f : VU0_rsqrt(upSquared);
    up[0] = upScale * up[0];
    up[1] = upScale * up[1];
    up[2] = upScale * up[2];
    VU0_v4crossprodxyz(along, up, across);

    origin[0] = from->x;
    origin[1] = from->y;
    origin[2] = from->z;
    origin[3] = 1.0f;
    return true;
}
