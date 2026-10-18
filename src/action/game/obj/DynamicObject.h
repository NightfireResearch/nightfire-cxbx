#ifndef DYNAMICOBJECT_H_
#define DYNAMICOBJECT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A dynamic object's extra data (OBJECTTYPE_DYNAMICOBJECT, 0xc bytes): keys 0 to 3 and its model's effect flags
typedef struct DYNAMICOBJECT_tag {
    short unknown0;             // 0x00 - key 0: non-zero adds effect flags 0x30
    short unknown2;             // 0x02 - key 1
    uint effectFlags;           // 0x04 - its model's
    short unknown8;             // 0x08 - key 2
    uchar unknownA;             // 0x0a - key 3
    uchar padB;
} DYNAMICOBJECT_tag;

static_assert(sizeof(DYNAMICOBJECT_tag) == 0xc, "DYNAMICOBJECT_tag is 0xc bytes");

// Its placement keys (level_tag)
typedef struct DynamicObjectPlacement {
    char _unknown00[0x2c];
    uint unknown2c;             // 0x2c
    uint unknown30;             // 0x30
    uint unknown34;             // 0x34
    uint unknown38;             // 0x38
} DynamicObjectPlacement;
#pragma pack(pop)

obj_tag* DynamicObject_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

#endif // DYNAMICOBJECT_H_
