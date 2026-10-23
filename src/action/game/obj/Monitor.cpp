#include "Monitor.h"
#include "../../../common/xbeClass.h"
#include "build.h"
#include "control.h"
#include "Explode.h"
#include "Player.h"
#include "Sensor.h"
#include "../sp/SwitchChannels.h"
#include "../../engine/Camera.h"
#include "../../engine/Collide.h"
#include "../../engine/viewer.h"
#include "../../input.h"
#include "../../ui/HUD.h"

#pragma fp_contract(off)

// A security monitor: the player using it watches the cameras of its group through it, one after another, until
// none is left or it is broken.

#pragma pack(push, 1)
typedef struct {
    uchar header[0x2c];
    uint group;             // 0x2c - the cameras' SENSOR::monitorGroup
    uint unbreakable;       // 0x30
} Create_Monitor_Params;
#pragma pack(pop)

static_assert(offsetof(Create_Monitor_Params, unbreakable) == 0x30, "Bad offset of unbreakable");
LAYOUT_32(offsetof(viewer_tag, someCel) == 0x4, "Bad offset of viewer_tag::someCel");
LAYOUT_32(offsetof(viewer_tag, viewMatrix) == 0x108, "Bad offset of viewer_tag::viewMatrix");

// AUTOINJECT
obj_tag * Monitor_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_Monitor_Params *params = (Create_Monitor_Params *)lvl;
    obj_tag *obj = control_create_object(sizeof(MONITOR), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    MONITOR *monitor = (MONITOR *)obj->extraObjectData;
    monitor->health = params->unbreakable != 0 ? 0.0f : 1.0f;
    obj->curState = MonitorState_Off;
    obj->subState = params->group;
    obj->objectType = OBJECTTYPE_MONITOR;
    Control_SetGList(obj, celgl);
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// The first camera of the group from node on that is not disabled. It is shown from then on.
static obj_tag *Monitor_FindCamera(LLNODE_tag *node, ushort group) {
    for (; node != NULL; node = node->next) {
        SENSOR *sensor = (SENSOR *)node;
        if (sensor->owner->curState == SENSOR_STATE_DISABLED)
            continue;
        if (sensor->monitorGroup == group) {
            sensor->owner->displayMask = 0;
            return sensor->owner;
        }
    }
    return NULL;
}

// The player hands the viewer back to their own camera.
static void Monitor_GiveBack(MONITOR *monitor) {
    obj_tag *user = monitor->user;
    BLData *bloke = (BLData *)user->extraObjectData;
    Camera_SetUpdator(monitor->viewerIndex, user, (cameraUpdateFunc)Player_PositionCamera);
    Player_Enable(user, NULL, 0);
    HUD_Enable(bloke->hudInfo, SecCam, 0, 0);
    HUD_Enable(bloke->hudInfo, Ammo, 1, 0);
    HUD_Enable(bloke->hudInfo, Health, 1, 0);
}

// The player starts watching the first camera of the monitor's group, if there is one: their camera stops following
// them, they stop, and the HUD shows the security camera pane alone.
// AUTOINJECT
void Monitor_Activate(obj_tag *obj, obj_tag *user) {
    if (obj->curState != MonitorState_Off || user->objectType != OBJECTTYPE_PLAYER)
        return;

    BLData *bloke = (BLData *)user->extraObjectData;
    MONITOR *monitor = (MONITOR *)obj->extraObjectData;
    monitor->viewer = glb_viewer[bloke->playerNum];
    monitor->camera = Monitor_FindCamera(CameraList.head, obj->subState);
    if (monitor->camera == NULL)
        return;

    obj->curState = MonitorState_Showing;
    monitor->user = user;
    monitor->viewerIndex = bloke->playerNum;
    monitor->viewer->someCel = NULL;
    Camera_SetUpdator(bloke->playerNum, user, NULL);
    Input_SetAction(bloke->playerNum, ACTION_RELOAD, 1);
    Player_Disable(user, 0);
    HUD_Enable(bloke->hudInfo, SecCam, 1, 0);
    HUD_Enable(bloke->hudInfo, Ammo, 0, 0);
    HUD_Enable(bloke->hudInfo, Health, 0, 0);
}

// Shots break a monitor that can be broken. While showing, a camera that has been disabled gives way to the next of
// the group (from the start of the list again if need be), and the player pressing the button moves on to the next
// (but not round again); with no camera left the player gets their camera back. The camera shown has its monitored
// channel set and gives the viewer its matrix.
// AUTOINJECT
void Monitor_Update(obj_tag *obj) {
    MONITOR *monitor = (MONITOR *)obj->extraObjectData;

    if (monitor->health != 0.0f) {
        double health = monitor->health - (double)Collide_GetDamageNObjects(obj->hitList, NULL, NULL, 0);
        monitor->health = (float)health;
        if (health < 0.0 && obj->curState != MonitorState_Broken) {
            monitor->health = 0.0f;
            Control_NextLOD(obj);
            obj->curState = MonitorState_Broken;
            if (monitor->user != NULL)
                Monitor_GiveBack(monitor);
            Explode_Create(obj, &obj->centrePoint, &CONST_UP_VECTOR, obj->radius * 6.0f, obj->radius,
                           HT_Script_DestroyExplosion1, 0.5f, 200, 200, 0, NULL, 0);
        }
    }

    switch ((short)obj->curState) {
    case MonitorState_Off:
    case MonitorState_Broken:
        return;
    case MonitorState_Showing:
        break;
    case 4:
        obj->curState = MonitorState_Off;
        return;
    default:
        obj->curState--;
        return;
    }

    ushort group = obj->subState;
    obj_tag *camera = monitor->camera;
    if (camera != NULL && camera->curState == SENSOR_STATE_DISABLED) {
        camera->displayMask = 1;
        SENSOR *sensor = (SENSOR *)camera->extraObjectData;
        obj_tag *next = NULL;
        if (sensor != NULL)
            next = Monitor_FindCamera(sensor->cameraListNode.next, group);
        if (next == NULL)
            next = Monitor_FindCamera(CameraList.head, group);
        monitor->camera = next;
    }

    if (Input_Action(monitor->viewerIndex, ACTION_RELOAD, 4)) {
        obj_tag *next = NULL;
        camera = monitor->camera;
        if (camera != NULL) {
            camera->displayMask = 1;
            SENSOR *sensor = (SENSOR *)camera->extraObjectData;
            if (sensor != NULL)
                next = Monitor_FindCamera(sensor->cameraListNode.next, group);
        }
        monitor->camera = next;
        monitor->viewer->someCel = NULL;
    }

    if (monitor->camera == NULL) {
        Monitor_GiveBack(monitor);
        monitor->user = NULL;
        obj->curState = MonitorState_Done;
        return;
    }

    SENSOR *sensor = (SENSOR *)monitor->camera->extraObjectData;
    if (sensor->monitoredChannel != 0)
        switch_channels[sensor->monitoredChannel] = 1;
    Mat_Copy(&monitor->camera->transformMatrix, &monitor->viewer->viewMatrix);
    sensor->childObjectVisibilityState = 1;
}
