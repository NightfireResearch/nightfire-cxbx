#ifndef DOOR_H
#define DOOR_H

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

// A door's state (obj_tag::curState)
enum DoorState {
    DoorState_Closed = 0x2,
    DoorState_Open = 0x4,
    DoorState_Opening = 0x8,
    DoorState_Closing = 0x10,
};

// A door's kind (obj_tag::subState): a swing door is closed halfway along its path and swings open either way; a
// sliding one runs from closed (0) to open (1).
enum DoorKind {
    DoorKind_Swing = 0,
    DoorKind_Slide1 = 1,   // never pushed back by what it hits
    DoorKind_Slide2 = 2,
};

// DOORINFO::flags: the placement's two flag words, and one bit of the frame's own
enum DoorFlags {
    DoorFlag_Spline = 0x1,      // the path is a spline, not keys moved between linearly
    DoorFlag_StartOpen = 0x4,
    DoorFlag_Moved = 0x10,      // already moved this frame (Doors_Calc clears it)
    DoorFlag_StayOpen = 0x20,   // never closes by itself
};

#pragma pack(push, 1)
typedef struct {
    LLNODE_tag node;        // 0x00 - in TheDoors
    obj_tag *obj;           // 0x08
    void *path;             // 0x0c - the placement's keys
    _MATRIX hinge;          // 0x10 - swing: the inverse of the closed matrix; sliding: the end of the path
    float pathPos;          // 0x4c - 0..1
    float speed;            // 0x50 - of the path, a frame
    float direction;        // 0x54 - 1 or -1
    float side;             // 0x58 - swing: which way along its path it opens, 1 or -1
    ushort openSound;       // 0x5c - Action_SFX, 0xffff for none
    ushort closeSound;      // 0x5e
    ushort lockedSound;     // 0x60
    ushort channel;         // 0x62 - set while open or opening; set or cleared, it opens or closes the door
    ushort unlockSwitchChannel; // 0x64 - 0: not lockable, any other number: the switch channel that unlocks the door
    ushort group;           // 0x66 - the doors of a group move together
    ushort cooldown;        // 0x68 - frames until a hit can push it again
    ushort lastKey;         // 0x6a
    ushort flags;           // 0x6c - DoorFlags
    ushort openTime;        // 0x6e - frames open
    ushort msgShown;        // 0x70 - Door_DoMsg's message has been shown
    uchar oneWay;           // 0x72 - 1 or 2: opens from one side only
    uchar noTouch;          // 0x73 - does not open by being touched
} DOORINFO;

LAYOUT_32(sizeof(DOORINFO) == 0x74, "Size of DOOR_INFO not correct");
LAYOUT_32(offsetof(DOORINFO, pathPos) == 0x4c, "Bad offset of pathPos");
LAYOUT_32(offsetof(DOORINFO, openSound) == 0x5c, "Bad offset of openSound");
LAYOUT_32(offsetof(DOORINFO, unlockSwitchChannel) == 0x64, "Bad offset of unlockSwitchChannel");
LAYOUT_32(offsetof(DOORINFO, flags) == 0x6c, "Bad offset of flags");
LAYOUT_32(offsetof(DOORINFO, oneWay) == 0x72, "Bad offset of oneWay");
#pragma pack(pop)

void Door_Init(void);
obj_tag * Door_Create(_VECTOR *pos, _VECTOR *rot, level_tag *level, celglist_tag *celgl);
void _Door_SetupSwing(obj_tag *obj);
void _Door_SetupSlide(obj_tag *obj);
void _Door_SetState(obj_tag *obj, ushort state);
void _Door_Interp(obj_tag *obj, DOORINFO *door, char advance);
void Door_ReactToHit(obj_tag *obj);
bool Door_Activate(obj_tag *obj, obj_tag *user, char touched);
bool Door_IsLocked(obj_tag* obj);
bool Door_IsClosed(obj_tag *obj);
bool Door_IsOpen(obj_tag *obj);
uchar Door_Link2Portal(obj_tag *obj);
char * Door_DoMsg(obj_tag *obj);
void _Door_MoveGroup(obj_tag *leadObj, DOORINFO *lead, char active);
void _Door_CloseIfClear(obj_tag *obj);
void Doors_Calc(void);
void Door_Update(obj_tag *obj);

// The register-argument entries (see Door.cpp)
void Door_SetupSwing(void);
void Door_SetupSlide(void);
void Door_SetState(void);
void Door_Interp(void);
void Door_MoveGroup(void);
void Door_CloseIfClear(void);

#endif // DOOR_H
