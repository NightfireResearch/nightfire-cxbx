#ifndef BULLET_H
#define BULLET_H

#include "../../actionhelpers.h"

typedef struct {
    char unknown0[0x38];
    weapon_definition_tag *wpnDef; // 0x38
    char unknown1[0x1234]; // no idea
} BU_tag;


#endif // BULLET_H