#ifndef SPACELASER_H_
#define SPACELASER_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    _VECTOR emitterPnt; // 0x00 - 0x0c
    _VECTOR targetPnt; // 0x0c - 0x18
    HASHCODE someScripts[2];
    HASHCODE someScript;
    SCRIPTINFO *scriptInfo;
    float someCountdown; // Initial startup delay?
    float unknown2;
    float someTimerBeforeRunningScript; // Cooldown between shots?
    DYNAMICSOUNDS *soundHandle;
    ushort unknown3;
    ushort laserShutoffSwitchChannel;
    ushort laserStartupSwitchChannel;
    char unknown4[2];
} ObjData_SpaceLaser;

static_assert(sizeof(ObjData_SpaceLaser) == 0x40, "ObjData_SpaceLaser size incorrect");

#pragma pack(pop)

void SpaceLaser_Update(obj_tag *obj);
void SpaceLaser_Register(obj_tag *obj, ushort param_2, _VECTOR *param_3);
obj_tag * Create_SpaceLaser(_VECTOR* pos, _VECTOR* rot, level_tag* level);

#endif // SPACELASER_H_