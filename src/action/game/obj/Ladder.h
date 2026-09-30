#ifndef LADDER_H_
#define LADDER_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)
// A ladder's obj_tag::extraObjectData (Ghidra's ObjData_Ladder, 4 bytes).
typedef struct ObjData_Ladder {
    // Ladder_Create zeroes it. While it is non-zero, Player_Weapon skips the scope handling for a player
    // climbing this ladder. Who sets it is not known yet.
    short unknown0; // 0x0
    short param1;   // 0x2 - level_tag param[1], copied by Ladder_Create
} ObjData_Ladder;
#pragma pack(pop)
static_assert(sizeof(ObjData_Ladder) == 4, "Bad size for ObjData_Ladder");

obj_tag * Ladder_Create(_VECTOR *param_1,_VECTOR *param_2,level_tag *param_3,celglist_tag *param_4);



#endif // LADDER_H_