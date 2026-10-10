#ifndef DRIVING_GAME_SOFTZONE_H_
#define DRIVING_GAME_SOFTZONE_H_

// ---------------------------------------------------------------------------------------------------------------
// The soft zones (SoftZone.cpp; the name is the game's PBondCar_PosInSoftZone's): a fixed table of 19 boxes in the
// game's data, each a quadrilateral and a plane - the quad's corners, and the plane's normal with w the zone's
// depth along it, measured from the last corner. Missile::CheckForCollision asks whether a point is in one;
// PBondCar::ProcessSubmarinePhysics reads the table too.
// ---------------------------------------------------------------------------------------------------------------

#include <stdint.h>

#include "../data/CoordConvert.h"   // Coord4

struct SoftZone {
    Coord4 corners[4];          // +0x00 in order around the quad
    Coord4 plane;               // +0x40 the normal; w the depth
};
static_assert(sizeof(SoftZone) == 0x50, "a soft zone is 0x50 bytes");

constexpr int kSoftZones = 19;

#define SoftZones ((const SoftZone *)0x001c37b0)   // [kSoftZones], initialised data

// Whether the point is on the inner side of each of the quad's four edges, seen along the normal
// (Ghidra: FUN_000643b0; the name is ours)
bool PointInsideQuad(const Coord4 *point, const Coord4 *corners, const Coord4 *normal);         // 0x000643b0
// Whether the point is in any of the soft zones
bool PBondCar_PosInSoftZone(const Coord4 *point);                                              // 0x00064440

#endif // DRIVING_GAME_SOFTZONE_H_
