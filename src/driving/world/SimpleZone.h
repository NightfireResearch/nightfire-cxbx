#ifndef DRIVING_WORLD_SIMPLEZONE_H_
#define DRIVING_WORLD_SIMPLEZONE_H_

#include <stdint.h>

#include "CollisionTypes.h"   // Coord3

// ---------------------------------------------------------------------------------------------------------------
// WSimpleZone: a position quantised to whole steps of 5 units (a WTargetable's), for a quick test of whether two
// are near each other. See SimpleZone.cpp.
// ---------------------------------------------------------------------------------------------------------------

class WSimpleZone {
public:
    int32_t x;                  // +0x00
    int32_t y;                  // +0x04
    int32_t z;                  // +0x08

    // Whether every coordinate of the other zone is within 2^shift steps of this one's (the exact edge is out
    // on one side, as the original's bit test has it)
    bool IsZoneInRange(const WSimpleZone *other, int shift);    // 0x000cc840
    void SetPosition(const Coord3 *position);                   // 0x000cc880
};
static_assert(sizeof(WSimpleZone) == 0xc, "a simple zone is 12 bytes");

#endif // DRIVING_WORLD_SIMPLEZONE_H_
