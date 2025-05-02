#ifndef DOOR_H
#define DOOR_H

#include "../../actionhelpers.h"


#pragma pack(push, 1)
typedef struct {
    LLNODE_tag *llPrev;
    LLNODE_tag *llNext;
    char unknown[92];
    ushort unlockSwitchChannel; // 0: not lockable, any other number: the switch channel that unlocks the door
    char pad[14];
} DOORINFO;

static_assert(sizeof(DOORINFO) == 0x74, "Size of DOOR_INFO not correct");
#pragma pack(pop)


bool Door_IsLocked(obj_tag* obj);

#endif // DOOR_H