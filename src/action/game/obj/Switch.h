#ifndef SWITCH_H_
#define SWITCH_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A simple two-position switch: flips its own visual state (curState) and switch_channels[channelNum] to match
// each other, driven either remotely (something else changes the channel, see Switch_Update's first block) or
// locally (Switch_Activate, eg. the player using it). scriptPlayer plays the physical flip animation.
typedef struct ObjData_Switch {
    LLNODE_tag listNode;         // 0x00 - SwitchList link
    obj_tag *objectPtr;          // 0x08 - back-pointer to this switch's own obj_tag
    obj_tag *scriptPlayer;       // 0x0c - plays the flip animation; driven purely via SP_UnPause (triggerMask is
                                  //        set to SP_Trigger_Unpause only, see SP_Create in Switch_Create)
    ushort autoResetFrames;      // 0x10 - if nonzero, Switch_Update automatically flips the channel back to
                                  //        restState this many frames after it was moved away from it (0 = never)
    ushort channelNum;           // 0x12 - which switch_channels[]/switch_channels_time[] slot this switch drives
    ushort gateSwitchChannel;    // 0x14 - if nonzero, Switch_Activate refuses to fire while switch_channels[this]
                                  //        is set (eg. a lockout channel)
    ushort restState;            // 0x16 - the channel/curState value this switch starts at, and that the
                                  //        auto-reset timer (autoResetFrames) restores it to
    uchar triggerCount;          // 0x18 - incremented every time this switch actually flips; combined with
                                  //        oneShot below to permanently disable it after its first use
    uchar oneShot;               // 0x19 - if set, this switch stops reacting once triggerCount is nonzero
    uchar unknown_0x1a[2];
} ObjData_Switch;

static_assert(sizeof(ObjData_Switch) == 0x1c, "Bad size for ObjData_Switch");
static_assert(offsetof(ObjData_Switch, objectPtr) == 0x8, "Bad offset of objectPtr");
static_assert(offsetof(ObjData_Switch, scriptPlayer) == 0xc, "Bad offset of scriptPlayer");
static_assert(offsetof(ObjData_Switch, autoResetFrames) == 0x10, "Bad offset of autoResetFrames");
static_assert(offsetof(ObjData_Switch, channelNum) == 0x12, "Bad offset of channelNum");
static_assert(offsetof(ObjData_Switch, gateSwitchChannel) == 0x14, "Bad offset of gateSwitchChannel");
static_assert(offsetof(ObjData_Switch, restState) == 0x16, "Bad offset of restState");
static_assert(offsetof(ObjData_Switch, triggerCount) == 0x18, "Bad offset of triggerCount");
static_assert(offsetof(ObjData_Switch, oneShot) == 0x19, "Bad offset of oneShot");

#pragma pack(pop)

obj_tag* Switch_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Switch_Update(obj_tag *obj);
void Switch_Activate(obj_tag *switchObj, obj_tag *activatorObj);
obj_tag* Switch_GetNearest(_VECTOR *pos);
ushort Switch_GetSwitchChannel(obj_tag *obj);

#endif // SWITCH_H_