#ifndef HURT_H_
#define HURT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A volume that hurts what it touches (an OBJECTTYPE_HURT's extraObjectData, 0x1c bytes, filled in by Hurt_Create
// from the placement's keys). Only the fields Hurt_Create's keys name are named.
typedef struct Hurt_tag {
    char _unknown00[0x10];          // 0x00 - the path and its timing
    float damage;                   // 0x10 - key 1
    uchar type;                     // 0x14 - key 0
    uchar oneShotPath;              // 0x15 - key 5
    ushort armMask;                 // 0x16 - key 4
    ushort enableSwitchChannel;     // 0x18 - key 2
    ushort sound;                   // 0x1a - key 3 (0xffff for none)
} Hurt_tag;

#pragma pack(pop)

static_assert(sizeof(Hurt_tag) == 0x1c, "Hurt_tag is 0x1c bytes");
static_assert(offsetof(Hurt_tag, damage) == 0x10, "Wrong offset for Hurt_tag.damage");

obj_tag * Hurt_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
float Hurt_GetDamage(obj_tag *obj);
ushort Hurt_GetType(obj_tag *obj);

#endif // HURT_H_