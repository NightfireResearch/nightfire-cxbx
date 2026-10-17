#include "SimpleZone.h"

#include "../platform/X87.h"

// ---------------------------------------------------------------------------------------------------------------
// WSimpleZone (0x000cc840-0x000cc8e5), ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

namespace {

constexpr float kZoneScale = 0.2f;   // 0x3e4ccccd: a step is 5 units

}  // namespace

// Each difference offset by 2^shift has to be in [0, 2^(shift+1)), tested together as one value with no bits from
// shift + 1 up.
// FUNC_AT(0x000cc840)
bool WSimpleZone::IsZoneInRange(const WSimpleZone *other, int shift) {
    int range = 1 << shift;
    return ((x - other->x + range) | (y - other->y + range) | (z - other->z + range)) >> (shift + 1) == 0;
}

// FUNC_AT(0x000cc880)
void WSimpleZone::SetPosition(const Coord3 *position) {
    x = RoundToInt(position->x * kZoneScale);
    y = RoundToInt(position->y * kZoneScale);
    z = RoundToInt(position->z * kZoneScale);
}
