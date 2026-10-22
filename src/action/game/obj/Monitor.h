#ifndef MONITOR_H_
#define MONITOR_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

// A monitor's state (obj_tag::curState); counting down from any other number back to MonitorState_Off
enum MonitorState {
    MonitorState_Off = 0,
    MonitorState_Showing = 2,
    MonitorState_Broken = 3,
    MonitorState_Done = 9,      // a player has stopped watching: counts down to 4, then 0
};

#pragma pack(push, 1)
typedef struct {
    viewer_tag *viewer;     // 0x00 - the watching player's viewer
    obj_tag *camera;        // 0x04 - the camera shown; NULL for none
    obj_tag *user;          // 0x08 - the player watching
    float health;           // 0x0c - 0 for a monitor that cannot be broken
    ushort viewerIndex;     // 0x10
    uchar padding12[2];
} MONITOR;
#pragma pack(pop)

XBOX_SIZE(MONITOR, 0x14);

obj_tag * Monitor_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Monitor_Activate(obj_tag *obj, obj_tag *user);
void Monitor_Update(obj_tag *obj);

#endif // MONITOR_H_
