#ifndef FLICKER_H_
#define FLICKER_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A flickering light's extra data: it follows the platform's light, and when that is off it goes dark, sets its
// channel and gives off sparks
typedef struct FLICKER_tag {
    uint light;                 // 0x00 - psiLight_Create's
    short sparks;               // 0x04
    char _pad06[2];
    _VECTOR up;                 // 0x08 - the sparks fall along it
    uint channel;               // 0x14
} FLICKER_tag;

static_assert(sizeof(FLICKER_tag) == 0x18, "FLICKER_tag is 0x18 bytes");

// The placement's keys (level_tag)
typedef struct FlickerPlacement {
    PlacementHeader header;     // 0x00
    uint sparks;                // 0x2c
    uint channel;               // 0x30
} FlickerPlacement;

static_assert(offsetof(FlickerPlacement, channel) == 0x30, "Bad offset of FlickerPlacement.channel");

#pragma pack(pop)

obj_tag * Flicker_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Flicker_Update(obj_tag *obj);

#endif // FLICKER_H_
