#ifndef THIRDICON_H_
#define THIRDICON_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A third-person icon's obj_tag::extraObjectData (0xc bytes): the action icon the HUD shows while the player touches
// its model (BLData::thirdIcon).
typedef struct THIRDICON {
    uint icon;          // 0x0 - THIRDICON_*, the placement's first parameter
    ushort onChannel;   // 0x4 - it works once this channel is set (0: from the start)
    ushort offChannel;  // 0x6 - and is deleted once this one is
    char wait;          // 0x8 - frames before the standing icon's text may show again
    uchar _pad09[3];
} THIRDICON;
#pragma pack(pop)
static_assert(sizeof(THIRDICON) == 0xc, "Bad size for THIRDICON");

obj_tag * ThirdIcon_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void ThirdIcon_Update(obj_tag *obj);

#endif // THIRDICON_H_