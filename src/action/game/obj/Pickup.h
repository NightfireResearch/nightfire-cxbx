#ifndef PICKUP_H_
#define PICKUP_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A pickup's extra data (OBJECTTYPE_PICKUP)
typedef struct PICKUPINFO {
    _VECTOR vel;                // 0x00 - while it falls
    _VECTOR offset;             // 0x0c - its matrix's translation from its bounding sphere's centre
    ushort state;               // 0x18 - PICKUP_
    ushort kind;                // 0x1a - PICKUP_KIND_
    ushort item;                // 0x1c - a weapon, an ammo's weapon or a key
    short count;                // 0x1e
    ushort channel;             // 0x20 - set when taken
    ushort sound;               // 0x22
    ushort respawn;             // 0x24 - tens of seconds; 0 never, 0xffff stays where it is
    ushort lifetime;            // 0x26 - frames; 0 for ever
    short mpIndex;              // 0x28 - -1
    char _pad2a[2];
    Action_TranslatedText label; // 0x2c - a mission item's message
    ushort spin;                // 0x30
    char _pad32[2];
} PICKUPINFO;

static_assert(sizeof(PICKUPINFO) == 0x34, "PICKUPINFO is 0x34 bytes");
static_assert(offsetof(PICKUPINFO, label) == 0x2c, "Bad offset of PICKUPINFO.label");

#pragma pack(pop)

// PICKUPINFO::state
enum {
    PICKUP_FALLING = 0,
    PICKUP_RESTING = 1,
    PICKUP_RESPAWNING = 2,
};

// PICKUPINFO::kind
enum {
    PICKUP_KIND_WEAPON = 0,
    PICKUP_KIND_AMMO = 1,
    PICKUP_KIND_KEY = 2,
    PICKUP_KIND_ARMOUR = 3,
    PICKUP_KIND_MISSION = 4,
    PICKUP_KIND_INVISIBILITY = 5,   // the player's alpha to 0; no level's map places one
    PICKUP_KIND_BONUS = 6,          // only with a gold medal
    PICKUP_KIND_SET_FIRST = 7,      // 7-11: a weapon of the multiplayer set (Pickup_CreateFromSet)
    PICKUP_KIND_SET_LAST = 11,
};

// The multiplayer weapon sets, five weapons each; set 10 is drawn at random (Pickup_MakeRandomWeaponSet) from the
// 27 weapons after them
#define PICKUP_SETS 11
extern short PickupMatrix[PICKUP_SETS][5];
extern short UseableGuns[27];

obj_tag * Pickup_Create(_VECTOR *pos, _VECTOR *rot, _MATRIX *mtx, celglist_tag *celgl, ushort kind, ushort item,
                        uint count, uint channel, uint sound, char placed, ushort respawn, ushort lifetime,
                        uint label);
void Pickup_Update(obj_tag *obj);
void Pickup_Handler(obj_tag *obj, PICKUPINFO *info, obj_tag *by);
bool Pickup_CanInheritPosition(obj_tag *pickup, obj_tag *from);
void __stdcall Pickup_MakeRandomWeaponSet(void);
obj_tag * Pickup_CreateFromSet(_VECTOR *pos, _VECTOR *rot, ushort kind);
obj_tag * Pickup_CreateSimple(_MATRIX *mtx, short weapon, short count, ushort kind);

#endif // PICKUP_H_
