#pragma fp_contract(off)

#include "OBB.h"
#include "../platform/RealMath.h"
#include "../platform/X87.h"

#include <math.h>
#include <stddef.h>

// ---------------------------------------------------------------------------------------------------------------
// OBB: the separating-axis test (docs/driving/maths.md for the x87 rules). Two passes, the first along this box's
// three axes, the second along the other's, the axes in the order x, z, y. Along each axis the separation is the
// distance between the centres projected on it, less this box's half extent along it, less the other box's three
// extent axes projected on it; positive along any axis means the boxes are apart. Each separation is stored to a
// float after every subtraction, as the original stores it.
// ---------------------------------------------------------------------------------------------------------------

static constexpr float kPenetrationUnset = -100000.0f;

// Axis i of the pass: x, z, y (the compiled loop maps 1 to 2 and 2 to 1).
static int PassAxis(int i) {
    return i == 1 ? 2 : (i == 2 ? 1 : 0);
}

// FUNC_AT(0x00037a70)
OBB* OBB::Construct() {
    return this;
}

// FUNC_AT(0x000ac870)
OBB* OBB::Construct(const MATRIX4 *matrix, const Coord4 *position, const Coord4 *halfExtents) {
    Reset(matrix, position, halfExtents);
    return this;
}

// FUNC_AT(0x000ac970)
void OBB::Reset(const MATRIX4 *matrix, const Coord4 *position, const Coord4 *halfExtents) {
    centre = *position;
    halfExtent[0] = halfExtents->x;
    halfExtent[1] = halfExtents->y;
    halfExtent[2] = halfExtents->z;
    for (int i = 0; i < 3; i++) {
        axis[i].x = matrix->mtx[i][0];
        axis[i].y = matrix->mtx[i][1];
        axis[i].z = matrix->mtx[i][2];
    }
    extentAxis[0].x = halfExtent[0] * axis[0].x;
    normal.w = 0.0f;
    contactPoint.w = 0.0f;
    extentAxis[0].y = halfExtent[0] * axis[0].y;
    penetration = kPenetrationUnset;
    extentAxis[0].z = halfExtent[0] * axis[0].z;
    extentAxis[1].x = halfExtent[1] * axis[1].x;
    extentAxis[1].y = halfExtent[1] * axis[1].y;
    extentAxis[1].z = halfExtent[1] * axis[1].z;
    extentAxis[2].x = axis[2].x * halfExtent[2];
    extentAxis[2].y = axis[2].y * halfExtent[2];
    extentAxis[2].z = halfExtent[2] * axis[2].z;
}

// FUNC_AT(0x000aca70)
bool OBB::CheckOBBOverlap(const OBB *other) const {
    for (int pass = 0; pass < 2; pass++) {
        const OBB *box = pass == 0 ? this : other;
        const OBB *against = pass == 0 ? other : this;
        for (int i = 0; i < 3; i++) {
            const int k = PassAxis(i);
            alignas(16) Coord4 along = box->axis[k];
            alignas(16) Coord4 between = {};
            VU0_v4sub(box, against, &between);
            float separation = fabsf(v3dotprod(&between, &along)) - box->halfExtent[k];
            for (int j = 0; j < 3; j++)
                separation = separation - fabsf(v3dotprod(&along, &against->extentAxis[j]));
            if (separation > 0.0f)
                return false;
        }
    }
    return true;
}

// The axis is made to point against the difference of the centres: when the projection is not negative (or is
// NaN) the axis is negated through the x87 (FLD, FCHS, FSTP, which quiets a signalling NaN), otherwise the
// projection is. The other box's corner furthest along the axis is walked to by its extent axes. A deeper
// penetration than any so far (this box's record, whichever pass) is stored with the axis - negated back on the
// second pass, so the normal always belongs to this box's side.
// FUNC_AT(0x000acb90)
bool OBB::CheckOBBOverlapAndFindIntersection(const OBB *other) {
    for (int pass = 0; pass < 2; pass++) {
        const OBB *box = pass == 0 ? this : other;
        const OBB *against = pass == 0 ? other : this;
        for (int i = 0; i < 3; i++) {
            const int k = PassAxis(i);
            alignas(16) Coord4 along = box->axis[k];
            alignas(16) Coord4 corner = against->centre;
            alignas(16) Coord4 between = {};
            VU0_v4sub(box, against, &between);
            float projection = v3dotprod(&between, &along);
            if (!(projection < 0.0f)) {
                along.x = -QuietNaN(along.x);
                along.y = -QuietNaN(along.y);
                along.z = -QuietNaN(along.z);
            } else {
                projection = -projection;
            }
            float separation = projection - box->halfExtent[k];
            for (int j = 0; j < 3; j++) {
                const Coord4 *extent = &against->extentAxis[j];
                float reach = v3dotprod(&along, extent);
                separation = separation - fabsf(reach);
                if (reach > 0.0f)
                    VU0_v4sub(&corner, extent, &corner);
                else if (reach < 0.0f)
                    VU0_v3add(&corner, extent, &corner);
            }
            if (separation > 0.0f)
                return false;
            if (separation > penetration) {
                penetration = separation;
                contactPoint = corner;
                secondPass = uint8_t(pass);
                if (pass != 0) {
                    normal.x = -QuietNaN(along.x);
                    normal.y = -QuietNaN(along.y);
                    normal.z = -QuietNaN(along.z);
                } else {
                    normal.x = along.x;
                    normal.y = along.y;
                    normal.z = along.z;
                }
            }
        }
    }
    return true;
}
