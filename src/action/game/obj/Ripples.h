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
    char _unknown00[0x2c];
    uint radius;                // 0x2c
    uint rate;                  // 0x30
} RipplesPlacement;

static_assert(offsetof(RipplesPlacement, rate) == 0x30, "Bad offset of RipplesPlacement.rate");

#pragma pack(pop)

obj_tag * Ripples_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Ripples_Update(obj_tag *obj);

#endif // RIPPLES_H_
