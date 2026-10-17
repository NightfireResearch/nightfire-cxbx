#include "Door.h"
#include "build.h"
#include "control.h"
#include "../Spline.h"
#include "../mp/multiplayer.h"
#include "../sp/SwitchChannels.h"
#include "../../engine/Collide.h"
#include "../../engine/Text.h"
#include "../../game.h"
#include "../../sound/Sound.h"
#include "../../util/LList.h"

#include <bit>
#include <math.h>
#include <string.h>

#pragma fp_contract(off)

// Doors move along their placement's path, position 0 to 1. A swing door is closed halfway along (0.5) and swings
// open either way, away from whoever opens it; a sliding door runs from closed (0) to open (1). Doors of a group
// move together.

#pragma pack(push, 1)

typedef struct {
    ushort unknown00;           // 0x00
    ushort pathPointCount;      // 0x02
    ushort pathDuration;        // 0x04 - frames to cover the path; 0 is taken as one second
    uchar padding06[2];
    void *path;                 // 0x08
    uchar padding0c[0x20];
    uint kind;                  // 0x2c - DoorKind
    uint group;                 // 0x30
    uint channel;               // 0x34
    uint lockChannel;           // 0x38
    uint linkToPortals;         // 0x3c
    uint openSound;             // 0x40 - 0 for none
    uint closeSound;            // 0x44
    uint lockedSound;           // 0x48
    uint flags[2];              // 0x4c - DoorFlags, the two words or'd together
    uint oneWay;                // 0x54
    uint noTouch;               // 0x58
} Create_Door_Params;

#pragma pack(pop)

static_assert(offsetof(Create_Door_Params, kind) == 0x2c, "Bad offset of kind");
static_assert(offsetof(Create_Door_Params, noTouch) == 0x58, "Bad offset of noTouch");

// XBE_GLOBAL(0x001df98c, 0xc)
#define TheDoors (*(LLISTINFO_tag *)0x001df98c)
// The states of each group's doors or'd together, built again every frame by Door_Update
// XBE_GLOBAL(0x001df8c0, 0xca)
#define DoorGroupsStates (*(ushort(*)[101])0x001df8c0)

// obj_tag::unknown_0xd9: build_LinkDoors2Portals links the door to the portals it covers
#define DOOR_LINK_TO_PORTALS 0x8

// How far through a swing door the one pushing it must be before it swings back
static constexpr float DOOR_PUSH_TOLERANCE = 0.0002f;
static_assert(std::bit_cast<uint32_t>(DOOR_PUSH_TOLERANCE) == 0x3951b717, "not the original's constant");
// How far in front of the hinge's plane the user counts as in a closing swing door's way
static constexpr float DOOR_CLEARANCE = 0.3f;
static_assert(std::bit_cast<uint32_t>(DOOR_CLEARANCE) == 0x3e99999a, "not the original's constant");

// The texts shown here
#define TXT_DOOR_MSG ((Action_TranslatedText)0x0200000b)
#define TXT_DOOR_UNLOCKED ((Action_TranslatedText)0x02000003)

// The originals, called by address
#define KeyFrame_Interp ((void (__cdecl *)(void *path, uint firstKey, uint lastKey, float frame, _MATRIX *mtx))0x000d8ac0)
#define Control_BuildWorldSph ((void (__cdecl *)(obj_tag *obj))0x0002db20)
#define MPDrone_MaybeIsDyingOrDead ((bool (__cdecl *)(obj_tag *drone))0x0001a190)

// AUTOINJECT
void Door_Init(void) {
    LList_Init(&TheDoors, sizeof(DOORINFO), 0);
    memset(DoorGroupsStates, 0, sizeof(DoorGroupsStates));
}

// The original does not check that the object was made.
// AUTOINJECT
obj_tag * Door_Create(_VECTOR *pos, _VECTOR *rot, level_tag *level, celglist_tag *celgl) {
    Create_Door_Params *params = (Create_Door_Params *)level;

    obj_tag *obj = control_create_object(sizeof(DOORINFO), pos, rot, NULL);
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    door->group = params->group;
    door->channel = params->channel;
    door->unlockSwitchChannel = params->lockChannel;
    door->openSound = params->openSound != 0 ? params->openSound : 0xffff;
    door->closeSound = params->closeSound != 0 ? params->closeSound : 0xffff;
    door->lockedSound = params->lockedSound != 0 ? params->lockedSound : 0xffff;
    door->path = params->path;
    door->lastKey = params->pathPointCount - 1;
    double frames = params->pathDuration != 0 ? (double)params->pathDuration : (double)_FRAME_RATE;
    door->speed = (float)(1.0 / frames);
    door->obj = obj;
    door->flags = params->flags[0] | params->flags[1];
    bool startOpen = ((params->flags[0] | params->flags[1]) & DoorFlag_StartOpen) != 0;

    obj->curState = startOpen ? DoorState_Open : DoorState_Closed;
    obj->subState = params->kind;
    obj->transformFlags |= TRANSFORM_MATRIX_PLACED | TRANSFORM_POSITION_FROM_MATRIX | 0x80;
    obj->objectType = OBJECTTYPE_DOOR;
    if (params->linkToPortals != 0)
        obj->unknown_0xd9 |= DOOR_LINK_TO_PORTALS;
    door->oneWay = params->oneWay;
    door->noTouch = params->noTouch;
    Control_SetGList(obj, celgl);
    if (obj->subState == DoorKind_Swing)
        _Door_SetupSwing(obj);
    else
        _Door_SetupSlide(obj);

    build_LinkToRoom(obj, 1, glb_world);
    LList_Add(&TheDoors, &door->node);
    Control_BuildWorldSph(obj);
    if (startOpen && door->channel != 0) {
        switch_channels[door->channel] = 1;
        switch_channels_time[door->channel] = GameState.NumFramesUnpaused;
    }
    Controls_StraddleTest(obj);
    return obj;
}

// A swing door: closed halfway along its path, with its speed rounded to take a whole number of frames to get there.
// The hinge is the inverse of the closed matrix; side is which way along the path it swings open.
void _Door_SetupSwing(obj_tag *obj) {
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    int framesToHalf = (int)(int64_t)(0.5 / ((double)door->speed * 2.0));
    door->speed = (float)(0.5 / framesToHalf);
    door->pathPos = (obj->curState & DoorState_Open) ? 1.0f : 0.5f;
    Spline_Interp(door->path, 0, door->lastKey, door->lastKey * 0.5f, &door->hinge, NULL, 2);
    Spline_Interp(door->path, 0, door->lastKey, door->lastKey * door->pathPos, &obj->transformMatrix, NULL, 2);
    Mat_InverseSelf(&door->hinge);

    _MATRIX start, closed;
    Spline_Interp(door->path, 0, door->lastKey, 0.0f, &start, NULL, 2);
    Spline_Interp(door->path, 0, door->lastKey, door->lastKey * 0.5f, &closed, NULL, 2);
    _VECTOR dir, norm;
    Mat_GetDir(&dir, &start);
    Mat_GetNorm(&norm, &closed);
    double dot = (double)norm.z * dir.z + (double)norm.y * dir.y + (double)norm.x * dir.x;
    door->side = dot < 0.0 ? 1.0f : -1.0f;

    celglist_tag *celgl = obj->objGraphics;
    _VECTOR size;
    Vec_Subtract(&celgl->extentMax, &celgl->extentMin, &size);
    NF_WARN_IF(fabsf(size.x) < fabsf(size.z), "ERROR : Swing door is not modelled on the xy axis : %s",
               celgl->name); // GC check (0x80082a74)
}

// The original takes the door in EAX, and the placement on the stack, unread.
// AUTOLTCG
void __declspec(naked) Door_SetupSwing(void) {
    _asm {
        push eax
        call _Door_SetupSwing
        add esp, 4
        ret
    }
}

// A sliding door. The hinge is the inverse of the path's first matrix, and then the path's last one in its place.
void _Door_SetupSlide(obj_tag *obj) {
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    door->pathPos = (obj->curState & DoorState_Open) ? 1.0f : 0.0f;
    Spline_Interp(door->path, 0, door->lastKey, 0.0f, &door->hinge, NULL, 2);
    Mat_InverseSelf(&door->hinge);
    Spline_Interp(door->path, 0, door->lastKey, (float)door->lastKey, &door->hinge, NULL, 2);
    Spline_Interp(door->path, 0, door->lastKey, door->lastKey * door->pathPos, &obj->transformMatrix, NULL, 2);
}

// FUN_0002eb80 in the original, which takes the door in EAX, and the placement on the stack, unread. The name is ours.
// AUTOLTCG
void __declspec(naked) Door_SetupSlide(void) {
    _asm {
        push eax
        call _Door_SetupSlide
        add esp, 4
        ret
    }
}

// Starting to open or close plays its sound. The door's channel follows whether it was open or opening.
void _Door_SetState(obj_tag *obj, ushort state) {
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    if ((state & DoorState_Opening) && !(obj->curState & DoorState_Opening))
        Sound_Play3D((Action_SFX)door->openSound, Mat_Position(obj->transformMatrix), 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if ((state & DoorState_Closing) && !(obj->curState & DoorState_Closing))
        Sound_Play3D((Action_SFX)door->closeSound, Mat_Position(obj->transformMatrix), 100.0f, -1.0f, -1.0f, 0, 0, 0);
    if (door->channel != 0) {
        bool open = (obj->curState & (DoorState_Open | DoorState_Opening)) != 0;
        switch_channels[door->channel] = open;
        if (open)
            switch_channels_time[door->channel] = GameState.NumFramesUnpaused;
    }
    obj->curState = state;
    door->openTime = 0;
}

// The original takes the door in ESI and the state in BX.
// AUTOLTCG
void __declspec(naked) Door_SetState(void) {
    _asm {
        movzx eax, bx
        push eax
        push esi
        call _Door_SetState
        add esp, 8
        ret
    }
}

// Puts the door where it is on its path and, with advance, moves it on a frame, stopping it at the end. Stopping
// always moves it with Spline_Interp, even on a door that moves between keys.
void _Door_Interp(obj_tag *obj, DOORINFO *door, char advance) {
    if (door->flags & DoorFlag_Spline)
        Spline_Interp(door->path, 0, door->lastKey, door->lastKey * door->pathPos, &obj->transformMatrix, NULL, 2);
    else
        KeyFrame_Interp(door->path, 0, door->lastKey, door->lastKey * door->pathPos, &obj->transformMatrix);
    obj->transformFlags |= TRANSFORM_MOVED;
    if (!advance)
        return;

    float pathPos = (float)((double)door->direction * door->speed * FRAME_RATE_MUL + door->pathPos);
    door->pathPos = pathPos;
    float step = door->speed * FRAME_RATE_MUL;
    bool stopped = false;

    short kind = obj->subState;
    if (kind == DoorKind_Swing) {
        ushort state = obj->curState;
        if ((state & DoorState_Opening) && (pathPos > 1.0f || pathPos < 0.0f)) {
            // Swung fully open, one way or the other
            door->pathPos = pathPos < 0.0f ? 0.0f : (pathPos > 1.0f ? 1.0f : pathPos);
            _Door_SetState(obj, DoorState_Open);
            stopped = true;
        } else if (state & DoorState_Closing) {
            double fromClosed = 0.5 - pathPos;
            if (fromClosed < 0.0)
                fromClosed = -fromClosed;
            if (!(fromClosed <= step))
                return;
            door->pathPos = 0.5f;
            _Door_SetState(obj, DoorState_Closed);
            stopped = true;
        } else {
            return;
        }
    } else if (kind > 0 && kind <= DoorKind_Slide2) {
        if (pathPos < 0.0f) {
            door->pathPos = 0.0f;
            _Door_SetState(obj, DoorState_Closed);
            stopped = true;
        }
        if (door->pathPos > 1.0f) {
            door->pathPos = 1.0f;
            _Door_SetState(obj, DoorState_Open);
            stopped = true;
        }
    }

    if (stopped)
        Spline_Interp(door->path, 0, door->lastKey, door->lastKey * door->pathPos, &obj->transformMatrix, NULL, 2);
}

// The original takes the door in EAX and its data in EDI, then advance on the stack.
// AUTOLTCG
void __declspec(naked) Door_Interp(void) {
    _asm {
        push dword ptr [esp + 4]
        push edi
        push eax
        call _Door_Interp
        add esp, 12
        ret
    }
}

// A player, or a drone that is up and moving, hitting a moving door: a swing door turns back when it was swinging
// into them, and a closing door of the second sliding kind opens again.
// AUTOINJECT
void Door_ReactToHit(obj_tag *obj) {
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    if (door->cooldown != 0)
        return;

    obj_tag *pusher = NULL;
    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        obj_tag *other = hit->hitObj;
        if (other == NULL)
            continue;
        if ((other->objectType == OBJECTTYPE_DRONE && !MPDrone_MaybeIsDyingOrDead(other)) ||
            other->objectType == OBJECTTYPE_PLAYER) {
            pusher = other;
            break;
        }
    }
    if (pusher == NULL)
        return;

    switch ((short)obj->subState) {
    case DoorKind_Swing:
        if (obj->curState & (DoorState_Opening | DoorState_Closing)) {
            // Which side of the door the pusher is, as the door is now, against the way it opens
            _MATRIX inverse;
            Mat_Copy(&obj->transformMatrix, &inverse);
            Mat_InverseSelf(&inverse);
            _VECTOR local;
            RotPreTransVec(&inverse, &pusher->centrePoint, &local);
            double side = (double)local.z * door->side;
            if (door->pathPos > 0.5f)
                side = -side;

            ushort state = obj->curState;
            ushort reverse;
            if ((state & DoorState_Closing) && side < -DOOR_PUSH_TOLERANCE)
                reverse = DoorState_Opening;
            else if ((state & DoorState_Opening) && side > DOOR_PUSH_TOLERANCE)
                reverse = DoorState_Closing;
            else
                return;
            door->direction = -door->direction;
            _Door_SetState(obj, reverse);
        }
        break;

    case DoorKind_Slide1:
        return;

    case DoorKind_Slide2:
        if (!(obj->curState & DoorState_Closing))
            return;
        door->direction = -door->direction;
        _Door_SetState(obj, DoorState_Opening);
        _Door_Interp(obj, door, 0);
        break;
    }

    _Door_Interp(obj, door, 0);
    door->flags |= DoorFlag_Moved;
    door->cooldown = 2;
}

static void Door_PlayLocked(obj_tag *obj, DOORINFO *door) {
    Sound_Play3D((Action_SFX)door->lockedSound, Mat_Position(obj->transformMatrix), 100.0f, -1.0f, -1.0f, 0, 0, 0);
}

// Opens or closes the door for user (the player, a drone, or the door itself through its channel). A locked door only
// plays its sound, as does a one-way door from the wrong side; a swing door opens away from the user and does not
// close on them. Returns whether it started to move.
// AUTOINJECT
bool Door_Activate(obj_tag *obj, obj_tag *user, char touched) {
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    if (touched && door->noTouch)
        return false;
    if (door->unlockSwitchChannel != 0 && !switch_channels[door->unlockSwitchChannel] &&
        (obj->curState & (DoorState_Closed | DoorState_Closing))) {
        Door_PlayLocked(obj, door);
        return false;
    }
    if (door->flags & DoorFlag_Moved)
        return false;

    if (door->oneWay) {
        _VECTOR toDoor, dir;
        Vec_Subtract(Mat_Position(obj->transformMatrix), Mat_Position(user->transformMatrix), &toDoor);
        Mat_GetDir(&dir, &obj->transformMatrix);
        double dot = (double)dir.z * toDoor.z + (double)dir.y * toDoor.y + (double)dir.x * toDoor.x;
        if ((door->oneWay == 1 && dot < 0.0) || (door->oneWay == 2 && dot > 0.0)) {
            Door_PlayLocked(obj, door);
            return false;
        }
    }
    door->oneWay = 0;

    bool started = false;
    short kind = obj->subState;
    if (kind == DoorKind_Swing) {
        if (obj->curState & (DoorState_Open | DoorState_Opening)) {
            _VECTOR local;
            RotPreTransVec(&door->hinge, &user->centrePoint, &local);
            double closingTowards = (double)door->direction * door->side;
            double inWay = closingTowards < 0.0 ? (double)local.z + DOOR_CLEARANCE : (double)local.z - DOOR_CLEARANCE;
            inWay = inWay * door->direction * door->side;
            if (inWay < 0.0 && -inWay < obj->radius) {
                door->flags |= DoorFlag_Moved;
                return false;
            }
        }

        ushort state = obj->curState;
        if (state & (DoorState_Closed | DoorState_Closing)) {
            _VECTOR local;
            RotPreTransVec(&door->hinge, &user->centrePoint, &local);
            door->direction = (local.z <= 0.0f ? -1.0f : 1.0f) * door->side;
            _Door_SetState(obj, DoorState_Opening);
            started = true;
        } else if (state & (DoorState_Open | DoorState_Opening)) {
            door->direction = door->pathPos > 0.5f ? -1.0f : 1.0f;
            _Door_SetState(obj, DoorState_Closing);
            started = true;
        }
    } else if (kind > 0 && kind <= DoorKind_Slide2) {
        started = true;
        if (obj->curState & (DoorState_Closed | DoorState_Closing)) {
            door->direction = 1.0f;
            _Door_SetState(obj, DoorState_Opening);
        } else if (obj->curState & (DoorState_Open | DoorState_Opening)) {
            door->direction = -1.0f;
            _Door_SetState(obj, DoorState_Closing);
        }
    }

    door->flags |= DoorFlag_Moved;
    door->cooldown = 2;
    return started;
}

// Only used for drone navigation and Door_DoMsg - the player's interaction with locked doors is independent of this?
// AUTOINJECT
bool Door_IsLocked(obj_tag* obj) {
    if(obj == NULL)
        return false;

    DOORINFO* doorInfo = (DOORINFO*)(obj->extraObjectData);

    ushort swChannel = doorInfo->unlockSwitchChannel;

    // Not lockable
    if(swChannel == 0)
        return false;

    // Lockable - when the switch is activated, the door is unlocked
    return !switch_channels[swChannel];

}

// For Vision_Recurse: a door of the first sliding kind always counts as closed; any other by its group's state, if it
// has a group.
// AUTOINJECT
bool Door_IsClosed(obj_tag *obj) {
    if (obj == NULL)
        return false;
    if (obj->subState == DoorKind_Slide1)
        return true;
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    if (door->group != 0)
        return DoorGroupsStates[door->group] == DoorState_Closed;
    return obj->curState == DoorState_Closed;
}

// FUN_0002f4e0 in the original; the name is ours.
// AUTOINJECT
bool Door_IsOpen(obj_tag *obj) {
    if (obj == NULL)
        return false;
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    if (door->group != 0)
        return DoorGroupsStates[door->group] == DoorState_Open;
    return obj->curState == DoorState_Open;
}

// AUTOINJECT
uchar Door_Link2Portal(obj_tag *obj) {
    if (obj == NULL)
        return 0;
    return (obj->unknown_0xd9 & DOOR_LINK_TO_PORTALS) != 0;
}

// The door's message, once for each door, unless it is locked or open. None while MPSettings+0x1e4 is set.
// AUTOINJECT
char * Door_DoMsg(obj_tag *obj) {
    if (obj == NULL || MPSettings.maybeDroneAIEnabled)
        return NULL;
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    if (door->msgShown)
        return NULL;
    door->msgShown = 1;
    if (Door_IsLocked(obj) || Door_IsOpen(obj))
        return NULL;
    return (char *)Txt_BindLabel(TXT_DOOR_MSG, 0);
}

// The other doors of the lead door's group, not yet moved this frame, take its movement and state. Doors_Calc always
// passes active as 1.
void _Door_MoveGroup(obj_tag *leadObj, DOORINFO *lead, char active) {
    for (LLNODE_tag *node = TheDoors.head; node != NULL && active; node = node->next) {
        DOORINFO *door = (DOORINFO *)node;
        if (door == lead || door->group != lead->group || (door->flags & DoorFlag_Moved))
            continue;
        door->direction = lead->direction;
        door->pathPos = lead->pathPos;
        door->flags |= DoorFlag_Moved;
        door->openTime = lead->openTime;
        door->obj->curState = leadObj->curState;
        _Door_Interp(door->obj, door, 0);
    }
}

// FUN_0002f7e0 in the original, which takes the lead door's data in ESI and active in BL, then the lead door on the
// stack. The name is ours.
// AUTOLTCG
void __declspec(naked) Door_MoveGroup(void) {
    _asm {
        movzx eax, bl
        push eax
        push esi
        push dword ptr [esp + 12]
        call _Door_MoveGroup
        add esp, 12
        ret
    }
}

// Closes the door unless a player, a dead player or a drone is near it: within twice the square of their radii
// added together.
void _Door_CloseIfClear(obj_tag *obj) {
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    for (obj_tag *other = control_first_object(); other != NULL; other = other->nextObject) {
        if (other->effectFlags & FLAG_HIDDEN)
            continue;
        if (other->objectType != OBJECTTYPE_DEAD_PLAYER && other->objectType != OBJECTTYPE_PLAYER &&
            other->objectType != OBJECTTYPE_DRONE)
            continue;
        float reach = other->radius + obj->radius;
        double distance = Vec_SqDist3D(&obj->centrePoint, &other->centrePoint);
        if ((double)reach * reach * 2.0 > distance) {
            door->openTime = 0;
            return;
        }
    }
    Door_Activate(obj, glb_players[0], 0);
    door->openTime = 0;
}

// FUN_0002f850 in the original, which takes the door in EDI. The name is ours.
// AUTOLTCG
void __declspec(naked) Door_CloseIfClear(void) {
    _asm {
        push edi
        call _Door_CloseIfClear
        add esp, 4
        ret
    }
}

// Once a frame, before the doors' updates: the doors of each group follow its first door.
// AUTOINJECT
void Doors_Calc(void) {
    memset(DoorGroupsStates, 0, sizeof(DoorGroupsStates));
    for (LLNODE_tag *node = TheDoors.head; node != NULL; node = node->next) {
        DOORINFO *door = (DOORINFO *)node;
        if (door->group != 0)
            _Door_MoveGroup(door->obj, door, 1);
    }
    for (LLNODE_tag *node = TheDoors.head; node != NULL; node = node->next)
        ((DOORINFO *)node)->flags &= ~DoorFlag_Moved;
}

// A door follows its channel, opens when unlocked (saying so, except in two levels), closes itself after six seconds
// open unless kept open or someone is near, and moves on along its path.
// AUTOINJECT
void Door_Update(obj_tag *obj) {
    DOORINFO *door = (DOORINFO *)obj->extraObjectData;
    if (door->cooldown != 0)
        door->cooldown--;
    if (door->flags & DoorFlag_Moved)
        return;

    ushort state = obj->curState;
    if ((state & (DoorState_Closed | DoorState_Open)) && door->channel != 0) {
        if (switch_channels[door->channel]) {
            if (!(state & DoorState_Open))
                Door_Activate(obj, glb_players[0], 0);
        } else if (!(state & DoorState_Closed)) {
            _Door_CloseIfClear(obj);
        }
    }

    ushort lock = door->unlockSwitchChannel;
    if (lock != 0 && !switch_channels_prev[lock] && switch_channels[lock] && !(door->flags & DoorFlag_Moved)) {
        if (GameState.CurrentLevelHashcode != HT_Level_EvilBase &&
            GameState.CurrentLevelHashcode != HT_Level_Tower2Elevator)
            Text_AddMsg(0, 0, 1, (char *)Txt_BindLabel(TXT_DOOR_UNLOCKED, 0), 0, 180);
        Door_Activate(obj, glb_players[0], 0);
        door->flags |= DoorFlag_Moved;
        door->unlockSwitchChannel = 0;
    }

    if ((obj->curState & DoorState_Open) && !(door->flags & DoorFlag_StayOpen)) {
        if (door->openTime++ >= FRAME_RATE_INT * 6)
            _Door_CloseIfClear(obj);
    }

    if (obj->curState & (DoorState_Opening | DoorState_Closing))
        _Door_Interp(obj, door, 1);
    else
        Vec_Copy(&obj->position, &obj->lastPosition);

    if (door->group != 0)
        DoorGroupsStates[door->group] |= obj->curState;
}
