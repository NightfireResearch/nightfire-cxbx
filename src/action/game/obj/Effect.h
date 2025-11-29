#ifndef EFFECT_H_
#define EFFECT_H_

#include "../../actionhelpers.h"

#pragma pack(push, 1)

typedef struct {

    char unknown[0x1c];
    ushort someThing;
    uchar someOther;
    char unknown1;
} Effect_tag;

static_assert(sizeof(Effect_tag) == 0x20, "Bad size for Effect_tag");

#pragma pack(pop)

#endif // EFFECT_H_