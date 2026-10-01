#ifndef BULLET_H
#define BULLET_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    char unknown0a[0x24];
    obj_tag *firedByObj; // 0x24 - the object that fired this bullet (eg. read by SP_GetHitDamage)
    char unknown0b[0x38-0x24-4];
    weapon_definition_tag *wpnDef; // 0x38
    char unknown1[0xcc-0x38-4];
    float maybeAgeOrLifetime; // 0xcc, used by sentinel to represent age
    char unknown2[0xe0-0xcc-4];
} BU_tag;

static_assert(sizeof(BU_tag) == 0xe0, "Bad size for BU_tag");
static_assert(offsetof(BU_tag, firedByObj) == 0x24, "Bad offset of firedByObj");

#pragma pack(pop)

obj_tag * Bullet_init(short plyNum, obj_tag *playerObj, obj_tag *param_3, weapon_definition_tag *weaponDef, _VECTOR *pos, _VECTOR *direction, ushort weaponSound);

#endif // BULLET_H