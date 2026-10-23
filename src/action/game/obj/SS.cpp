#include "SS.h"
#include "../../../common/xbeClass.h"
#include "build.h"
#include "control.h"
#include "../Spline.h"
#include "../sp/SwitchChannels.h"
#include "../../engine/Collide.h"
#include "../../engine/viewer.h"
#include "../../sound/Sound.h"

#include <math.h>

#pragma fp_contract(off)

// A simple script: an object moved along a path of keys, once or over and over, when something sets it off.

// SIMPLESCRIPT::mode
enum SSMode {
    SSMode_Stopped = 0,
    SSMode_Moving = 1,   // along the path, then back to waiting (or stopped, with once)
    SSMode_Looping = 8,  // round the path for good
    SSMode_Waiting = 0x40,
};

// What sets it off (SIMPLESCRIPT::triggers), and what SS_Activate needs
enum SSTrigger {
    SSTrigger_KeepChannel = 0x1, // SS_Update leaves the switch channel alone
    SSTrigger_Bullet = 0x2,
    SSTrigger_Activate = 0x4,    // SS_Activate (the player using it) starts it
    SSTrigger_Player = 0x8,
    SSTrigger_Drone = 0x10,
};

#pragma pack(push, 1)

typedef struct SIMPLESCRIPT {
    float frame;            // 0x00 - where on the path, in keys
    float speed;            // 0x04 - keys a frame
    float stopFrame;        // 0x08 - a stop on the way: the object waits there for another trigger
    void *path;             // 0x0c - the placement's keys
    uint switchChannel;     // 0x10 - sets it off, and is set while it moves
    Action_SFX sound;       // 0x14 - played when it sets off
    DYNAMICSOUNDS *soundHandle; // 0x18
    ushort lastKey;         // 0x1c
    ushort triggers;        // 0x1e - SSTrigger
    ushort startMode;       // 0x20
    ushort mode;            // 0x22 - SSMode
    ushort once;            // 0x24 - moving ends in SSMode_Stopped rather than SSMode_Waiting
    ushort spline;          // 0x26 - the path is a spline, not keys moved between linearly
    ushort moved;           // 0x28 - set once it has moved: the object is moved every frame from then on
    ushort water;           // 0x2a - its room's shear height follows the object
} SIMPLESCRIPT;

typedef struct {
    PlacementHeader header;     // 0x00
    uint mode;                  // 0x2c
    uint once;                  // 0x30
    uint triggers;              // 0x34
    uint switchChannel;         // 0x38
    uint sound;                 // 0x3c - 0 for none
    uint stopFrame;             // 0x40
    uint spline;                // 0x44
    uint water;                 // 0x48
} Create_SS_Params;

#pragma pack(pop)

LAYOUT_32(sizeof(SIMPLESCRIPT) == 0x2c, "Bad size for SIMPLESCRIPT");
LAYOUT_32(offsetof(SIMPLESCRIPT, lastKey) == 0x1c, "Bad offset of lastKey");
LAYOUT_32(offsetof(SIMPLESCRIPT, water) == 0x2a, "Bad offset of water");
static_assert(offsetof(Create_SS_Params, mode) == 0x2c, "Bad offset of mode");
static_assert(offsetof(Create_SS_Params, water) == 0x48, "Bad offset of water");

// How close past the stop frame counts as at it
#define SS_STOP_TOLERANCE 0.0002f

// The originals, called by address: Ghidra's prototypes have untyped arguments

// AUTOINJECT
obj_tag * SS_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_SS_Params *params = (Create_SS_Params *)lvl;
    if (params->header.splinePoints == 0)
        return NULL;

    obj_tag *obj = control_create_object(sizeof(SIMPLESCRIPT), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    SIMPLESCRIPT *ss = (SIMPLESCRIPT *)obj->extraObjectData;
    if (params->header.splineLength == 0)
        params->header.splineLength = 75;   // written back into the placement, as in the original
    ss->frame = 0.0f;
    ss->path = params->header.spline;
    ss->lastKey = params->header.splinePoints - 1;
    ss->mode = params->mode;
    ss->startMode = params->mode;
    ss->once = params->once;
    ss->triggers = params->triggers;
    ss->switchChannel = params->switchChannel;
    ss->sound = (Action_SFX)(params->sound != 0 ? params->sound : 0xffff);
    ss->stopFrame = (float)params->stopFrame;
    ss->spline = params->spline;
    ss->water = params->water;
    ss->soundHandle = NULL;
    ss->speed = (float)ss->lastKey / params->header.splineLength;
    obj->objectType = OBJECTTYPE_SIMPLESCRIPT;
    Control_SetGList(obj, celgl);
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX;
    build_LinkToRoom(obj, 1, glb_world);

    // The stop moves to a frame the steps land on
    ss->stopFrame = (int)((double)ss->stopFrame / ss->speed) * ss->speed;
    return obj;
}

// Whether it has been set off: touched by a kind of object its triggers name, or its switch channel set.
ushort SS_Operate(obj_tag *obj, SIMPLESCRIPT *ss) {
    ushort touched = 0;
    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        if (hit->hitObj == NULL)
            continue;
        switch (hit->hitObj->objectType) {
        case OBJECTTYPE_DRONE:
            touched |= SSTrigger_Drone;
            break;
        case OBJECTTYPE_PLAYER:
            touched |= SSTrigger_Player;
            break;
        case OBJECTTYPE_BULLET:
            touched |= SSTrigger_Bullet;
            break;
        }
    }

    if (ss->triggers & touched)
        return 1;
    if (ss->switchChannel != 0 && switch_channels[ss->switchChannel] != 0)
        return 1;
    return 0;
}

// The player using it starts it, if its triggers allow and it is not stopped for good.
// AUTOINJECT
void SS_Activate(obj_tag *obj) {
    SIMPLESCRIPT *ss = (SIMPLESCRIPT *)obj->extraObjectData;
    if (ss->mode != SSMode_Stopped && (ss->triggers & SSTrigger_Activate)) {
        ss->mode = SSMode_Moving;
        if (ss->switchChannel != 0)
            switch_channels[ss->switchChannel] = 1;
    }
}

// Moves the object along its path to the current frame, and the frame on by a step.
static void SS_Step(obj_tag *obj, SIMPLESCRIPT *ss) {
    if (ss->spline) {
        Spline_Interp(ss->path, 0, ss->lastKey, ss->frame, &obj->transformMatrix, NULL, 2);
    } else {
        KeyFrame_Interp(ss->path, 0, ss->lastKey, ss->frame, &obj->transformMatrix);
    }
    obj->transformFlags |= TRANSFORM_MOVED;
    if (ss->lastKey == 1)
        ss->frame = ss->speed + ss->frame;
    else
        ss->frame = (float)((double)FRAME_RATE_MUL * ss->speed + ss->frame);
}

// AUTOINJECT
void SS_Update(obj_tag *obj) {
    if (obj == NULL)
        return;

    SIMPLESCRIPT *ss = (SIMPLESCRIPT *)obj->extraObjectData;
    ushort setOff = SS_Operate(obj, ss);
    if (!(ss->triggers & SSTrigger_KeepChannel) && ss->switchChannel != 0 && switch_channels[ss->switchChannel] == 0)
        switch_channels[ss->switchChannel] = (char)setOff;
    if (ss->moved)
        obj->transformFlags |= TRANSFORM_MOVED;
    if (ss->soundHandle != NULL && Sound_IsFinnished(ss->soundHandle))
        ss->soundHandle = NULL;

    switch (ss->mode) {
    case SSMode_Waiting:
        if (!setOff)
            break;
        ss->soundHandle = Sound_Play3D(ss->sound, &obj->centrePoint, 100.0f, -1.0f, -1.0f, 0, 0, 0);
        ss->mode = SSMode_Moving;
        // fall through
    case SSMode_Moving:
        SS_Step(obj, ss);
        if (ss->frame > ss->stopFrame && fabs((double)ss->frame - ss->stopFrame) < SS_STOP_TOLERANCE) {
            ss->mode = SSMode_Waiting;
            ss->soundHandle = NULL;
        }
        if ((int)ss->frame > ss->lastKey) {
            ss->frame = 0.0f;
            ss->soundHandle = NULL;
            ss->mode = ss->once ? SSMode_Stopped : SSMode_Waiting;
        }
        ss->moved = 1;
        break;

    case SSMode_Looping:
        SS_Step(obj, ss);
        if (ss->lastKey < ss->frame)
            ss->frame = 0.0f;
        break;
    }

    if (ss->water && obj->inCel != NULL)
        obj->inCel->shearHeight = obj->centrePoint.y;
}
