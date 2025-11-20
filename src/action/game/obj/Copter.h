#ifndef COPTER_H_
#define COPTER_H_

#include "../../actionhelpers.h"


#pragma pack(push, 1)

typedef struct {
    LLNODE_tag node;
    char pad[0x24];
    obj_tag* scriptPlayerObj;
    char pad2[0x7c-12-0x24];
} COPTER;

static_assert(sizeof(COPTER) == 0x7c, "Copter size incorrect");
static_assert(offsetof(COPTER, scriptPlayerObj) == 0x2c, "Script player object offset incorrect");

#pragma pack(pop)

void Copter_Delete(obj_tag *obj);

obj_tag * Copter_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);

#endif // COPTER_H_
