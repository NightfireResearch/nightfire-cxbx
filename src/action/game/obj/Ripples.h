#ifndef RIPPLES_H_
#define RIPPLES_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// Ripples' extra data: rings of smoke on the water within radius of it, one in rate frames on average
typedef struct RIPPLES_tag {
    short radius;               // 0x00 - metres
    short rate;                 // 0x02
} RIPPLES_tag;

static_assert(sizeof(RIPPLES_tag) == 4, "RIPPLES_tag is 4 bytes");

// The placement's keys (level_tag)
typedef struct RipplesPlacement {
    PlacementHeader header;     // 0x00
    uint radius;                // 0x2c
    uint rate;                  // 0x30
} RipplesPlacement;

static_assert(offsetof(RipplesPlacement, rate) == 0x30, "Bad offset of RipplesPlacement.rate");

// A bubble's extra data (object type 6)
typedef struct BUBBLE_tag {
    uint _unknown00;
    _VECTOR velocity;           // 0x04
    char _unknown10[0x1c - 0x10];
    short framesLeft;           // 0x1c
} BUBBLE_tag;

static_assert(offsetof(BUBBLE_tag, framesLeft) == 0x1c, "Bad offset of BUBBLE_tag.framesLeft");

#pragma pack(pop)

obj_tag * Ripples_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Ripples_Update(obj_tag *obj);
void Bubble_Update(obj_tag *obj);

#endif // RIPPLES_H_
