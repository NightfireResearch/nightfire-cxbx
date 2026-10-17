#ifndef SENSOR_H_
#define SENSOR_H_

#include "../../actionhelpers.h"

// Sensors are kept in CameraList, so the list node has to remain first.
#pragma pack(push, 1)
typedef struct SENSOR {
    LLNODE_tag cameraListNode;             // 0x00
    obj_tag* owner;                        // 0x08
    celglist_tag* enabledGraphics;         // 0x0c
    uint enabledGraphicsLod;               // 0x10
    quaternion_tag trackedRotation;        // 0x14
    void* sweepSpline;                     // 0x24
    float sweepPosition;                   // 0x28
    float sweepStep;                       // 0x2c
    uint sweepLastPoint;                   // 0x30
    uint viewConeHalfAngle;                // 0x34, in half-degrees
    float viewConeCos;                     // 0x38
    float viewConeSin;                     // 0x3c
    float beamLength;                      // 0x40
    float alertTimer;                      // 0x44
    float alertBeepTimer;                  // 0x48
    obj_tag* beamObject;                   // 0x4c
    obj_tag* bodyObject;                   // 0x50
    DYNAMICSOUNDS* loopSound;              // 0x54
    ushort alertSwitchChannel;             // 0x58
    ushort disableSwitchChannel;           // 0x5a
    ushort monitoredChannel;               // 0x5c - set while a monitor shows this camera
    uchar monitorGroup;                    // 0x5e - the monitors of this group show it
    uchar dontAddToCameraList;             // 0x5f
    uchar childObjectVisibilityState;      // 0x60 - 1 from Monitor_Update while a monitor shows it
    uchar padding[3];
} SENSOR;

static_assert(sizeof(SENSOR) == 0x64, "Wrong size for SENSOR");
static_assert(offsetof(SENSOR, beamObject) == 0x4c, "Bad offset of beamObject");
static_assert(offsetof(SENSOR, bodyObject) == 0x50, "Bad offset of bodyObject");
#pragma pack(pop)

// XBE_GLOBAL(0x0029aabc, 0xc)
#define CameraList (*(LLISTINFO_tag*)0x0029aabc)

// SENSOR::owner->curState of a camera its disable channel has turned off
#define SENSOR_STATE_DISABLED 3

void Sensor_Init(void);
obj_tag* Sensor_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
short Sensor_SetBeam(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor);
bool Sensor_InCone(obj_tag *targetObj, obj_tag *sensorObj, SENSOR *sensor);
void Sensor_Update(obj_tag *sensorObj);


#endif // SENSOR_H_