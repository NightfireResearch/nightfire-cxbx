#ifndef PICKUP_H_
#define PICKUP_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A pickup's extraObjectData (Ghidra's PICKUPINFO). Only the fields the bot code reads are laid out.
typedef struct PICKUPINFO {
    char _pad00[0x18];
    ushort unknown18;       // 0x18 - Pickup_Create writes 0, or 1 when placed with its last flag set
    ushort kind;            // 0x1a - the placement's pickup kind: 0 weapon, 1 ammo, 3 health or armour
    ushort weaponId;        // 0x1c
} PICKUPINFO;
#pragma pack(pop)

static_assert(offsetof(PICKUPINFO, kind) == 0x1a, "Bad offset of PICKUPINFO.kind");
static_assert(offsetof(PICKUPINFO, weaponId) == 0x1c, "Bad offset of PICKUPINFO.weaponId");

obj_tag * Pickup_Create(_VECTOR *pos,_VECTOR *rot,_MATRIX *mtx,celglist_tag *param_4,ushort maybePickupType,ushort param_6,uint param_7,uint param_8,uint param_9,char param_10,ushort param_11,ushort param_12,uint param_13);

#endif // PICKUP_H_