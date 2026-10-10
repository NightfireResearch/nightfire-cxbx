#include "SoftZone.h"

#include "../platform/RealMath.h"

// ---------------------------------------------------------------------------------------------------------------
// The soft zones (0x000643b0-0x000644c0), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// FUNC_AT(0x000643b0)
bool PointInsideQuad(const Coord4 *point, const Coord4 *corners, const Coord4 *normal) {
    for (int corner = 0; corner < 4; corner++) {
        Coord4 edge, side, offset;
        VU0_v4sub(&corners[(corner + 1) % 4], &corners[corner], &edge);
        VU0_v4crossprodxyz(normal, &edge, &side);
        VU0_v4sub(point, &corners[corner], &offset);
        if (v3dotprod(&offset, &side) < 0.0f)
            return false;
    }
    return true;
}

// FUNC_AT(0x00064440)
bool PBondCar_PosInSoftZone(const Coord4 *point) {
    for (int zone = 0; zone < kSoftZones; zone++) {
        const SoftZone &softZone = SoftZones[zone];
        Coord4 offset;
        VU0_v4sub(point, &softZone.corners[3], &offset);
        float depth = v3dotprod(&offset, &softZone.plane);
        if (depth >= 0.0f && depth < softZone.plane.w && PointInsideQuad(point, softZone.corners, &softZone.plane))
            return true;
    }
    return false;
}
