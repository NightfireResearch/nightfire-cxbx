#ifndef SEARCHLIGHT_H_
#define SEARCHLIGHT_H_

#include "../../actionhelpers.h"

#include <stddef.h>

#pragma pack(push, 1)
typedef struct {
    ushort searchlightType;                // 0x00 - 0=stand/base, 1=body/master, 2=light holder
    ushort padding02;                      // 0x02
    float phase;                           // 0x04 - sweep animation phase OR lost-player counter (depending on object type)
    _VECTOR rotationAngles;                // 0x08 - initial rotation (used as sweep center)
    obj_tag* standObject;                  // 0x14 - type 0: stand/base (yaw only, no pitch)
    obj_tag* unusedLinkedObject;           // 0x18 - type 1: unused in practice
    union {                                // 0x1c
        obj_tag* projectionSpot;           //   - type 1: ground projection spot object
        light_tag* dynamicLight;           //   - type 2: dynamic light_tag
    };
    obj_tag* volumetricCone;               // 0x20 - visible light cone beam effect
    obj_tag* lensFlare;                    // 0x24 - bright flare when looking at light
    uchar enableSwitchChannel;             // 0x28 - switch channel to re-enable after being disabled
    uchar disableSwitchChannel;            // 0x29 - switch channel to disable searchlight
    uchar alertSwitchChannel;              // 0x2a - switch channel to trigger when player spotted
    uchar padding2b;                       // 0x2b
    float sweepHalfRange;                  // 0x2c - half the sweep angle range
    float sweepCenter;                     // 0x30 - center pitch angle of sweep
} ObjData_Spotlight;

static_assert(sizeof(ObjData_Spotlight) == 0x34, "Wrong size for ObjData_Spotlight");
static_assert(offsetof(ObjData_Spotlight, phase) == 0x04, "Bad offset of phase");
static_assert(offsetof(ObjData_Spotlight, rotationAngles) == 0x08, "Bad offset of rotationAngles");
static_assert(offsetof(ObjData_Spotlight, standObject) == 0x14, "Bad offset of standObject");
static_assert(offsetof(ObjData_Spotlight, unusedLinkedObject) == 0x18, "Bad offset of unusedLinkedObject");
static_assert(offsetof(ObjData_Spotlight, projectionSpot) == 0x1c, "Bad offset of projectionSpot");
static_assert(offsetof(ObjData_Spotlight, volumetricCone) == 0x20, "Bad offset of volumetricCone");
static_assert(offsetof(ObjData_Spotlight, lensFlare) == 0x24, "Bad offset of lensFlare");
static_assert(offsetof(ObjData_Spotlight, enableSwitchChannel) == 0x28, "Bad offset of enableSwitchChannel");
static_assert(offsetof(ObjData_Spotlight, sweepHalfRange) == 0x2c, "Bad offset of sweepHalfRange");
static_assert(offsetof(ObjData_Spotlight, sweepCenter) == 0x30, "Bad offset of sweepCenter");
#pragma pack(pop)

obj_tag* Searchlight_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Searchlight_Update(obj_tag *searchlight);

#endif // SEARCHLIGHT_H_