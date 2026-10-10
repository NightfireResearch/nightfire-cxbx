#ifndef PICKUP_H_
#define PICKUP_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A pickup's extra data (Ghidra's PICKUPINFO; Pickup_Create allocates 0x34 bytes). Names other than Ghidra's
// are ours.
typedef struct {
    _VECTOR velocity;       // 0x00 - while it falls, gravity is added to it each frame
    _VECTOR centreOffset;   // 0x0c - its matrix's position less its centre point (Pickup_Create)
    ushort state;           // 0x18 - PickupState
    ushort kind;            // 0x1a - Pickup_Create's maybePickupType
    ushort weaponId;        // 0x1c - BOTSTATE_pickGoal reads it zero-extended
    ushort ammoType;        // 0x1e
    ushort unknown20;       // 0x20
    ushort sfxOnPickup;     // 0x22
    ushort respawnTime;     // 0x24 - tens of seconds a taken pickup stays hidden
    ushort lifetime;        // 0x26 - frames until the pickup is removed, 0 for never
    ushort pickupIdx;       // 0x28
    ushort pad2a;
    uint unknown2c;         // 0x2c
    ushort spins;           // 0x30 - turns about Y each frame
    ushort pad32;
} PICKUPINFO;

#pragma pack(pop)

static_assert(sizeof(PICKUPINFO) == 0x34, "PICKUPINFO is wrong size");
static_assert(offsetof(PICKUPINFO, state) == 0x18, "Bad offset of PICKUPINFO.state");
static_assert(offsetof(PICKUPINFO, respawnTime) == 0x24, "Bad offset of PICKUPINFO.respawnTime");
static_assert(offsetof(PICKUPINFO, spins) == 0x30, "Bad offset of PICKUPINFO.spins");

// PICKUPINFO.state (names ours)
enum PickupState : ushort {
    PICKUP_FALLING = 0,     // dropping to the floor under gravity
    PICKUP_READY = 1,       // can be picked up
    PICKUP_RESPAWNING = 2,  // taken; hidden until respawnTime has passed
};

obj_tag * Pickup_Create(_VECTOR *pos,_VECTOR *rot,_MATRIX *mtx,celglist_tag *param_4,ushort maybePickupType,ushort param_6,uint param_7,uint param_8,uint param_9,char param_10,ushort param_11,ushort param_12,uint param_13);
void Pickup_Update(obj_tag *obj);

#endif // PICKUP_H_
