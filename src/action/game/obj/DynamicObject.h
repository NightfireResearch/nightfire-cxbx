#ifndef DYNAMICOBJECT_H_
#define DYNAMICOBJECT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A dynamic object's extra data (OBJECTTYPE_DYNAMICOBJECT, 0xc bytes): keys 0 to 3 and its model's effect flags
typedef struct DYNAMICOBJECT_tag {
    short showChannel;          // 0x00 - key 0: a switch channel; until it is on the object is hidden (flags 0x30)
                                //        and not updated
    short hideChannel;          // 0x02 - key 1: a switch channel that hides it (hideOnly) or deletes it
    uint effectFlags;           // 0x04 - its model's
    short fadeDistance;         // 0x08 - key 2: from the first view, its alpha rises over the second half of this
    uchar hideOnly;             // 0x0a - key 3
    uchar padB;
} DYNAMICOBJECT_tag;

static_assert(sizeof(DYNAMICOBJECT_tag) == 0xc, "DYNAMICOBJECT_tag is 0xc bytes");

// Its placement keys (level_tag)
typedef struct DynamicObjectPlacement {
    PlacementHeader header;     // 0x00
    uint showChannel;           // 0x2c
    uint hideChannel;           // 0x30
    uint fadeDistance;          // 0x34
    uint hideOnly;              // 0x38
} DynamicObjectPlacement;
#pragma pack(pop)

obj_tag* DynamicObject_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void DynamicObject_Update(obj_tag *obj);

#endif // DYNAMICOBJECT_H_
