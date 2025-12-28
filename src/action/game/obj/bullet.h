#ifndef BULLET_H
#define BULLET_H

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {
    char unknown0[0x38];
    weapon_definition_tag *wpnDef; // 0x38
    char unknown1[0xcc-0x38-4];
    float maybeAgeOrLifetime; // 0xcc, used by sentinel to represent age
    char unknown2[0xe0-0xcc-4];
} BU_tag;

static_assert(sizeof(BU_tag) == 0xe0, "Bad size for BU_tag");

#pragma pack(pop)


#endif // BULLET_H