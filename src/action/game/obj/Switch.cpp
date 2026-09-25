#include "Switch.h"

#include "control.h"
#include "build.h"
#include "ScriptPlayer.h"
#include "../../math/math.h"
#include "../../util/LList.h"
#include "../../engine/celglist.h"
#include "../sp/SwitchChannels.h"
#include "../../game.h"

// Switches are kept in their own list, initialised (oddly) from Sensor_Init alongside CameraList - see Sensor.cpp
#define SwitchList (*(LLISTINFO_tag*)0x0029aab0)

// Stride confirmed via Switch_Create's raw disassembly; only the first byte (switchChannel) is understood so far.
// Note this is addressed directly (index*8 + base), NOT via a +1 like Ghidra's decompiler shows for the one place
// that touches it - that +1 is just an artifact of where Ghidra chose to anchor the symbol, not a real offset.
typedef struct {
    uchar switchChannel;
    uchar unknown[7];
} SSysItem;
static_assert(sizeof(SSysItem) == 8, "Bad size for SSysItem");
#define SSysItems ((SSysItem*)0x0029aad0)

#pragma pack(push, 1)
typedef struct {
    ObjectCreationData_Basic baseData; // 0x00-0x2b
    ushort channelNum;                 // 0x2c
    uchar pad_2e[2];
    HASHCODE scriptHashcode;           // 0x30 - the flip-animation script, passed straight to SP_Create
    ushort autoResetFrames;            // 0x34
    uchar pad_36[2];
    ushort restState;                  // 0x38
    uchar pad_3a[2];
    uchar oneShot;                     // 0x3c
    uchar pad_3d[3];
    ushort gateSwitchChannel;          // 0x40
    uchar pad_42[2];
    int sysItemIndex;                  // 0x44 - if nonzero, SSysItems[this].switchChannel is set to channelNum
} Create_Switch_Params;
#pragma pack(pop)

static_assert(offsetof(Create_Switch_Params, channelNum) == 0x2c, "Bad offset of channelNum");
static_assert(offsetof(Create_Switch_Params, scriptHashcode) == 0x30, "Bad offset of scriptHashcode");
static_assert(offsetof(Create_Switch_Params, autoResetFrames) == 0x34, "Bad offset of autoResetFrames");
static_assert(offsetof(Create_Switch_Params, restState) == 0x38, "Bad offset of restState");
static_assert(offsetof(Create_Switch_Params, oneShot) == 0x3c, "Bad offset of oneShot");
static_assert(offsetof(Create_Switch_Params, gateSwitchChannel) == 0x40, "Bad offset of gateSwitchChannel");
static_assert(offsetof(Create_Switch_Params, sysItemIndex) == 0x44, "Bad offset of sysItemIndex");

// AUTOINJECT
obj_tag* Switch_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Switch_Params *createParams = (Create_Switch_Params*)lvl;

    obj_tag *switchObj = control_create_object(sizeof(ObjData_Switch), pos, rot, NULL);
    if (switchObj == NULL)
        return NULL;

    ObjData_Switch *switchData = (ObjData_Switch*)switchObj->extraObjectData;
    switchData->objectPtr = switchObj;
    switchData->channelNum = createParams->channelNum;
    switchData->autoResetFrames = createParams->autoResetFrames;
    switchData->restState = createParams->restState;
    switchData->oneShot = createParams->oneShot;
    switchData->gateSwitchChannel = createParams->gateSwitchChannel;

    if (createParams->sysItemIndex != 0) {
        SSysItems[createParams->sysItemIndex].switchChannel = (uchar)switchData->channelNum;
    }

    switchObj->curState = switchData->restState;
    switchObj->objectType = OBJECTTYPE_SWITCH;
    switchObj->objGraphics = NULL;
    switchObj->effectFlags = celgl->applyFlagsToObject;
    build_LinkToRoom(switchObj, 0, glb_world);

    if (switchData->channelNum != 0) {
        switch_channels[switchData->channelNum] = (char)switchData->restState;
        if (switchData->restState != 0) {
            switch_channels_time[switchData->channelNum] = GameState.NumFramesUnpaused;
        }
    }

    // The flip-animation ScriptPlayer: starts armed/paused (playMode 8), only reacts to SP_UnPause
    // (SP_Trigger_Unpause) - Switch_Update/Switch_Activate drive it directly rather than any of its own triggers
    obj_tag *scriptPlayer = SP_Create(pos, rot, createParams->scriptHashcode, (HASHCODE)0, 8, 0, switchObj, NULL, NULL, SP_Trigger_Unpause);
    switchData->scriptPlayer = scriptPlayer;

    if (scriptPlayer == NULL) {
        // Matches the original exactly - it returns the object it just told control_delete_object to clean up
        control_delete_object(switchObj);
        return switchObj;
    }

    SP_Update(scriptPlayer);
    if (switchObj->curState == 1) {
        // Starting "on": run the flip animation all the way through immediately rather than playing it out over
        // real frames, so the switch renders in its final state right away
        SP_UnPause(switchData->scriptPlayer);
        while (SP_GetState(switchData->scriptPlayer) != 0) {
            SP_Update(switchData->scriptPlayer);
        }
    }

    LList_Add(&SwitchList, (LLNODE_tag*)switchData);
    return switchObj;
}

// AUTOINJECT
void Switch_Update(obj_tag *obj) {
    ObjData_Switch *switchData = (ObjData_Switch*)obj->extraObjectData;

    // A one-shot switch stops reacting entirely once it's fired
    if (switchData->oneShot != 0 && switchData->triggerCount != 0)
        return;

    // React to switch_channels having been changed by something else (another switch, a trigger, ...) - catch
    // this switch's own visual state up to match, and play the flip animation
    if (switchData->channelNum != 0 && (uchar)switch_channels[switchData->channelNum] != obj->curState) {
        obj->curState ^= 1;
        switchData->triggerCount++;
        SP_UnPause(switchData->scriptPlayer);
    }

    // Auto-reset: once the channel has sat away from restState for more than autoResetFrames frames (using
    // obj->subState, repurposed here as a plain frame counter - see object.h), flip it back automatically
    if (switchData->autoResetFrames != 0 && (uchar)switch_channels[switchData->channelNum] != switchData->restState) {
        short framesAway = obj->subState;
        obj->subState = framesAway + 1;

        if ((int)switchData->autoResetFrames < (int)framesAway) {
            switchData->triggerCount++;
            SP_UnPause(switchData->scriptPlayer);

            if (switchData->channelNum != 0) {
                switch_channels[switchData->channelNum] = (switch_channels[switchData->channelNum] == 0);
                if (switch_channels[switchData->channelNum] == 0) {
                    switch_channels_time[switchData->channelNum] = GameState.NumFramesUnpaused;
                }
            }
            obj->subState = 0; // WALK - just the "counter reset" value here, not a real movement state
        }
    }
}

// AUTOINJECT
void Switch_Activate(obj_tag *switchObj, obj_tag *activatorObj) {
    ObjData_Switch *switchData = (ObjData_Switch*)switchObj->extraObjectData;

    if (switchData->oneShot != 0 && switchData->triggerCount != 0)
        return;

    // Channel 0x61 is specifically locked out from being triggered directly by the player (some other mechanism
    // must flip it instead) - exact reason for this particular channel isn't known
    if (activatorObj->objectType == OBJECTTYPE_PLAYER && switchData->channelNum == 0x61)
        return;

    // Only activatable while visually in sync with the channel (not mid-animation) ...
    if (switchObj->curState != (uchar)switch_channels[switchData->channelNum])
        return;

    // ... and while the gate channel (if any) isn't set
    if (switchData->gateSwitchChannel != 0 && switch_channels[switchData->gateSwitchChannel] != 0)
        return;

    // ... and while the flip animation is fully idle
    if (SP_GetState(switchData->scriptPlayer) != 0)
        return;

    if (switchData->channelNum == 0)
        return;

    switch_channels[switchData->channelNum] = (switch_channels[switchData->channelNum] == 0);
    if (switch_channels[switchData->channelNum] == 0) {
        switch_channels_time[switchData->channelNum] = GameState.NumFramesUnpaused;
    }
}

// AUTOINJECT
obj_tag* Switch_GetNearest(_VECTOR *pos) {
    obj_tag *nearest = NULL;
    float bestDistSq = 9999.9f;

    for (LLNODE_tag *node = SwitchList.head; node != NULL; node = node->next) {
        ObjData_Switch *switchData = (ObjData_Switch*)node;
        float distSq = Vec_SqDist3D(pos, &switchData->objectPtr->position);
        if (distSq < bestDistSq) {
            nearest = switchData->objectPtr;
            bestDistSq = distSq;
        }
    }

    return nearest;
}

// AUTOINJECT
ushort Switch_GetSwitchChannel(obj_tag *obj) {
    if (obj == NULL)
        return 0;

    return ((ObjData_Switch*)obj->extraObjectData)->channelNum;
}
