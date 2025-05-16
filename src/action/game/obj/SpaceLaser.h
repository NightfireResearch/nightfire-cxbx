#ifndef SPACELASER_H_
#define SPACELASER_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    _VECTOR a;
    _VECTOR b;
    HASHCODE someScripts[2];
    HASHCODE someScript;
    SCRIPTINFO *scriptInfo;
    float unknown1;
    float unknown2;
    float someTimerBeforeRunningScript;
    DYNAMICSOUNDS *soundHandle;
    ushort unknown3;
    ushort otherSwitchChannel;
    ushort someSwitchChannel;
    char unknown4[2];
} ObjData_SpaceLaser;

static_assert(sizeof(ObjData_SpaceLaser) == 0x40, "ObjData_SpaceLaser size incorrect");

#pragma pack(pop)

void SpaceLaser_Update(obj_tag *obj);
void Create_SpaceLaser(_VECTOR* pos, _VECTOR* rot, level_tag* level);

#endif // SPACELASER_H_