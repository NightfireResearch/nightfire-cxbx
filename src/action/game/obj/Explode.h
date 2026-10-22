#ifndef EXPLODE_H_
#define EXPLODE_H_

#include "../../actionhelpers.h"
#include "../../../common/xbeClass.h"

#pragma pack(push, 1)

// An explosion's extra data (OBJECTTYPE_EXPLODE, 0x34 bytes): its effect script, and the damage it spreads once
typedef struct EXPLODE_tag {
    _VECTOR up;                 // 0x00 - the script's up, once turned to face the nearest player: the debris's way
    _VECTOR spread;             // 0x0c - two directions square to the player's, added: the debris's spread
    cel_tag *cel;               // 0x18 - the cel of the object that made it
    obj_tag *from;              // 0x1c - who set it off (referenced while it lasts)
    float damage;               // 0x20 - at the centre (0 when the radius is under 0.5)
    float radius;               // 0x24
    uint frames;                // 0x28 - the script's (15 without one): debris comes in the first quarter
    SCRIPTINFO *script;         // 0x2c
    ushort material;            // 0x30 - what was hit: 5 and 6 throw metal debris, 8, 10 and 11 stone
    uchar propagated;           // 0x32
    uchar _pad33;
} EXPLODE_tag;

XBOX_SIZE(EXPLODE_tag, 0x34);

#pragma pack(pop)

void Explode_Create(obj_tag *obj, _VECTOR *pos, _VECTOR *unusedDir, float scriptScale, float radius, HASHCODE script,
                    float damage, uchar unused8, uchar unused9, uchar unused10, obj_tag *from, ushort material);
void Explode_Update(obj_tag *obj);
void Explode_Propagate(_VECTOR *pos, float radius, cel_tag *cel, float damage, obj_tag *from);
void Explode_Delete(obj_tag *obj);
void Explode_CollisionHandler(obj_tag *obj);

extern HASHCODE MetalDebris[4], StoneDebris[4];

#endif // EXPLODE_H_
