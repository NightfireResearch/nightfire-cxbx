#ifndef CLOUD_H_
#define CLOUD_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A cloud's extra data: drawn as a sky model, it keeps over player 0 at its height, drifting by its speeds and
// wrapping round at wrap
typedef struct CLOUD_tag {
    float speedX;               // 0x00 - a frame at 60 Hz
    float speedZ;               // 0x04
    float offsetX;              // 0x08 - from player 0
    float offsetZ;              // 0x0c
    int height;                 // 0x10 - above player 0
    int wrap;                   // 0x14
} CLOUD_tag;

static_assert(sizeof(CLOUD_tag) == 0x18, "CLOUD_tag is 0x18 bytes");

// The placement's keys (level_tag)
typedef struct CloudPlacement {
    PlacementHeader header;     // 0x00
    int height;                 // 0x2c
    uint speedX;                // 0x30 - thousandths
    uint speedZ;                // 0x34
    int wrap;                   // 0x38
    uint skyObj;                // 0x3c - its slot of the sky's models
} CloudPlacement;

static_assert(offsetof(CloudPlacement, skyObj) == 0x3c, "Bad offset of CloudPlacement.skyObj");

#pragma pack(pop)

obj_tag * Cloud_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void cloud_update(obj_tag *obj);

#endif // CLOUD_H_
