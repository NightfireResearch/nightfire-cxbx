#ifndef CREEPWALL_H_
#define CREEPWALL_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A creep wall's obj_tag::extraObjectData (8 bytes): the placement's four parameters, the second the third-person
// camera that watches it (0 for none) and the fourth the switch channel set while the player is on it.
typedef struct CREEPWALL {
    short field_0;      // 0x0 - the placement's first parameter
    ushort thirdCam;    // 0x2 - the second
    short field_4;      // 0x4 - the third
    short channel;      // 0x6 - the fourth
} CREEPWALL;
#pragma pack(pop)
static_assert(sizeof(CREEPWALL) == 8, "Bad size for CREEPWALL");

obj_tag * CreepWall_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);

#endif // CREEPWALL_H_
