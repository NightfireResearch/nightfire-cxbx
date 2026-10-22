#ifndef DESTROY_H_
#define DESTROY_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

// A destroyable's extra data (OBJECTTYPE_DESTROY, 0x18 bytes): shot, blown up or switched on, it changes to its
// broken model, can explode, and switches a channel on, at once or after a delay
typedef struct DESTROY_tag {
    celglist_tag *smashedGlist; // 0x00 - the broken model: the model's lod or the next entity's (none with effect
                                //        flag 0x10)
    float health;               // 0x04 - key 3
    float blastRadius;          // 0x08 - key 5 / 100: the explosion's radius
    int delay;                  // 0x0c - key 6 in frames, negative until smashed; counted down after
    ushort channelIn;           // 0x10 - key 1: smashes it when switched on
    ushort channelOut;          // 0x12 - key 0: switched on once smashed
    ushort mask;                // 0x14 - the damage classes it ignores: the complement of key 4 (0 for none)
    char explosion;             // 0x16 - key 2: 0 none, 1, 2 or another kind
    uchar smashed;              // 0x17
} DESTROY_tag;

XBOX_SIZE(DESTROY_tag, 0x18);

// A destroyable's placement keys (level_tag)
typedef struct DestroyPlacement {
    char _unknown00[0x2c];
    uint channelOut;            // 0x2c
    uint channelIn;             // 0x30
    uint explosion;             // 0x34
    uint health;                // 0x38
    uint mask;                  // 0x3c - the damage classes that hurt it, 0 for all
    uint blast;                 // 0x40 - hundredths
    uint delay;                 // 0x44 - seconds
} DestroyPlacement;

static_assert(offsetof(DestroyPlacement, delay) == 0x44, "Bad offset of DestroyPlacement.delay");

#pragma pack(pop)

obj_tag * Destroy_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl, celglist_tag *someOtherGl);
void Destroy_Update(obj_tag *obj);
void Destroy_CollisionHandler(obj_tag *obj);
void Destroy_Smash(obj_tag *obj, obj_tag *from, ushort flags);

#endif // DESTROY_H_
