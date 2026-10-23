#ifndef MINE_H_
#define MINE_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A mine's extra data: it turns and bobs about where it was placed, and blows up when anything touches it
typedef struct MINE_tag {
    _VECTOR home;               // 0x00
    float spin;                 // 0x0c - a frame at 60 Hz
    float bobRate;              // 0x10 - a frame
    float bob;                  // 0x14 - how far
    float damage;               // 0x18
    float radius;               // 0x1c
    HASHCODE script;            // 0x20 - the explosion's
} MINE_tag;

static_assert(sizeof(MINE_tag) == 0x24, "MINE_tag is 0x24 bytes");

// The placement's keys (level_tag)
typedef struct MinePlacement {
    PlacementHeader header;     // 0x00
    uint damage;                // 0x2c
    uint radius;                // 0x30
    uint spin;                  // 0x34 - 1/5000ths
    uint bobRate;               // 0x38
    uint bob;                   // 0x3c - tenths
    HASHCODE script;            // 0x40
} MinePlacement;

static_assert(offsetof(MinePlacement, script) == 0x40, "Bad offset of MinePlacement.script");

#pragma pack(pop)

obj_tag * Mine_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Mine_Update(obj_tag *obj);

#endif // MINE_H_
