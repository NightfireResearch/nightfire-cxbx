#ifndef LOCK_H_
#define LOCK_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

// A lock's extra data (OBJECTTYPE_LOCK, 0x20 bytes). Its states (obj_tag::curState) are LOCKSTATE_; the decryptor
// counts its shots in obj_tag::subState, and the laser its frames of cooling.
typedef struct LOCK_tag {
    ushort kind;                // 0x00 - LOCK_KEYPAD, LOCK_LASER or LOCK_DECRYPT
    ushort key;                 // 0x02 - the key that opens it (BLData::keys), and its code (KeyCodes)
    ushort openSound;           // 0x04 - 0xffff for none
    ushort lockedSound;         // 0x06 - used without the key; 0xffff for none
    ushort channel;             // 0x08 - set while it is open
    ushort menu;                // 0x0a - the keypad's menu manager
    uint unknownC;              // 0x0c - key 2
    void *path;                 // 0x10 - the spline it moves along as it opens
    float pathPos;              // 0x14 - frames along it
    float pathSpeed;            // 0x18 - frames a tick at 60 Hz
    uint pathFrames;            // 0x1c - the spline's points less one; 0 for none
} LOCK_tag;

LAYOUT_32(sizeof(LOCK_tag) == 0x20, "LOCK_tag is 0x20 bytes");

// A lock's placement keys (level_tag)
typedef struct LockPlacement {
    char _unknown00[2];
    ushort splinePoints;        // 0x02
    ushort splineLength;        // 0x04 - frames
    char _unknown06[2];
    void *spline;               // 0x08
    char _unknown0c[0x2c - 0x0c];
    uint kind;                  // 0x2c
    uint key;                   // 0x30
    uint unknown34;             // 0x34
    uint openSound;             // 0x38 - 0 for none
    uint lockedSound;           // 0x3c
    uint channel;               // 0x40
    uint easy;                  // 0x44 - unlocked from the start at difficulty 1
} LockPlacement;

LAYOUT_32(offsetof(LockPlacement, easy) == 0x44, "Bad offset of LockPlacement.easy");

#pragma pack(pop)

// LOCK_tag::kind
enum {
    LOCK_KEYPAD = 0,            // its code entered on the keypad
    LOCK_LASER = 1,             // cut by the laser
    LOCK_DECRYPT = 2,           // its code found by the decryptor
};

// The lock's state (obj_tag::curState)
enum {
    LOCKSTATE_LOCKED = 0,
    LOCKSTATE_OPENING = 1,      // moving along its spline
    LOCKSTATE_OPEN = 2,
    LOCKSTATE_KEYPAD = 3,       // used without the key: the keypad is to open
    LOCKSTATE_KEYPAD_OPEN = 4,
    LOCKSTATE_DECRYPTING = 5,
};

obj_tag * Lock_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
void Lock_Update(obj_tag *obj);
void Lock_Activate(obj_tag *lock, obj_tag *user);
void Lock_Unlock(obj_tag *user, obj_tag *lock, char silent);
void Lock_ReLock(obj_tag *user, obj_tag *lock);

#endif // LOCK_H_
