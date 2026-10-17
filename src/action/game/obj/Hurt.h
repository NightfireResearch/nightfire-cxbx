#ifndef HURT_H_
#define HURT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

// A volume that hurts what it touches (an OBJECTTYPE_HURT's extraObjectData, 0x1c bytes, filled in by Hurt_Create
// from the placement's keys). Once set off it moves along its path, once or over and over.
typedef struct Hurt_tag {
    float pathTime;                 // 0x00 - frames along the path
    float pathSpeed;                // 0x04 - frames a game frame: the path's frames over its duration
    void *path;                     // 0x08 - the placement's spline
    ushort pathFrames;              // 0x0c - the path's points less one
    ushort _pad0e;
    float damage;                   // 0x10 - key 1
    uchar type;                     // 0x14 - key 0
    uchar oneShotPath;              // 0x15 - key 5
    ushort armMask;                 // 0x16 - key 4
    ushort enableSwitchChannel;     // 0x18 - key 2
    ushort sound;                   // 0x1a - key 3 (0xffff for none)
} Hurt_tag;

#pragma pack(pop)

static_assert(sizeof(Hurt_tag) == 0x1c, "Hurt_tag is 0x1c bytes");
static_assert(offsetof(Hurt_tag, damage) == 0x10, "Wrong offset for Hurt_tag.damage");

// A hurt volume's placement (level_tag)
typedef struct HurtPlacement {
    ushort _unknown00;
    ushort pathCount;               // 0x02
    ushort pathDuration;            // 0x04
    ushort _unknown06;
    void *path;                     // 0x08
    char _unknown0c[0x2c - 0x0c];
    uint type;                      // 0x2c - key 0
    uint damage;                    // 0x30 - key 1
    uint enableSwitchChannel;       // 0x34 - key 2
    uint sound;                     // 0x38 - key 3, 0 for none
    uint armMask;                   // 0x3c - key 4
    uint oneShotPath;               // 0x40 - key 5
} HurtPlacement;

static_assert(offsetof(HurtPlacement, oneShotPath) == 0x40, "Bad offset of HurtPlacement.oneShotPath");

obj_tag * Hurt_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl);
float Hurt_GetDamage(obj_tag *obj);
ushort Hurt_GetType(obj_tag *obj);
void Hurt_Update(obj_tag *obj);
ushort _Hurt_IsTriggered(obj_tag *obj, Hurt_tag *hurt);
void Hurt_IsTriggered(void);
obj_tag* Occlude_Create(_VECTOR *pos, ushort life, ushort kind);
void Occlude_Update(obj_tag *obj);

#endif // HURT_H_