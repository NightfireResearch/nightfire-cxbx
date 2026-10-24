#ifndef SPACELASER_H_
#define SPACELASER_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

typedef struct {
    _VECTOR emitterPnt; // 0x00 - 0x0c
    _VECTOR targetPnt; // 0x0c - 0x18
    HASHCODE shotScripts[2];        // one picked at random for each shot
    HASHCODE finalScript;
    SCRIPTINFO *scriptInfo;
    float finalDelay;               // seconds the startup channel must be on before the final script
    float doneDelay;                // seconds into the final script before doneChannel goes on
    float shotDelay;                // seconds between shots
    DYNAMICSOUNDS *soundHandle;
    ushort doneChannel;
    ushort laserShutoffSwitchChannel;
    ushort laserStartupSwitchChannel;
    char unknown4[2];
} ObjData_SpaceLaser;

LAYOUT_32(sizeof(ObjData_SpaceLaser) == 0x40, "ObjData_SpaceLaser size incorrect");

// The placement's keys (level_tag)
typedef struct SpaceLaserPlacement {
    PlacementHeader header;         // 0x00
    HASHCODE shotScript0;           // 0x2c
    HASHCODE shotScript1;           // 0x30
    HASHCODE finalScript;           // 0x34
    uint finalSeconds;              // 0x38
    uint doneSeconds;               // 0x3c
    uint doneChannel;               // 0x40
    uint laserShutoffSwitchChannel; // 0x44
    uint laserStartupSwitchChannel; // 0x48
} SpaceLaserPlacement;

// obj_tag::curState
enum SpaceLaserState {
    SPACELASER_WAITING = 0,         // between shots
    SPACELASER_CHARGING = 1,        // a shot's script
    SPACELASER_FIRING = 2,
    SPACELASER_FINAL = 3,           // the final script
    SPACELASER_OFF = 4,             // the shutoff channel came on: it is deleted
};

#pragma pack(pop)

void SpaceLaser_Update(obj_tag *obj);
void SpaceLaser_Register(obj_tag *obj, ushort unused, _VECTOR *posOut);
obj_tag * Create_SpaceLaser(_VECTOR* pos, _VECTOR* rot, level_tag* level);

#endif // SPACELASER_H_