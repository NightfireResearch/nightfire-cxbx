#ifndef PLACEMENT_H_
#define PLACEMENT_H_

#include "../../helpers.h"
#include "../../driving/platform/Ptr32.h"

#pragma pack(push, 1)

// The part of every placement before its keys. The map loader fills in only the path, when it has one.
typedef struct PlacementHeader {
    char _unknown00[2];
    ushort splinePoints;        // 0x02
    ushort splineLength;        // 0x04 - frames to cover the path
    char _unknown06[2];
    Ptr32<void> spline;         // 0x08 - the map's path (m_paths)
    char _unknown0c[0x2c - 0x0c];
} PlacementHeader;
static_assert(sizeof(PlacementHeader) == 0x2c, "PlacementHeader is 0x2c bytes");

#pragma pack(pop)

#endif // PLACEMENT_H_
