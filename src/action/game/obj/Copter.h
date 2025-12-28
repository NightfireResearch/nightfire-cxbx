#ifndef COPTER_H_
#define COPTER_H_

#include "../../actionhelpers.h"


#pragma pack(push, 1)

typedef struct {
    LLNODE_tag node;
    char pad[0x24];
    obj_tag* scriptPlayerObj; // 0x2c
    char pad2[0x38-0x2c-4];
    obj_tag* body; // 0x38
    char pad22[0x70-0x38-4];
    ushort switchChannelToShowCopterBody; // 0x70
    char pad3[0x7c-0x70-2];
} COPTER;

static_assert(sizeof(COPTER) == 0x7c, "Copter size incorrect");
static_assert(offsetof(COPTER, scriptPlayerObj) == 0x2c, "Offset of scriptPlayerObj incorrect");
static_assert(offsetof(COPTER, body) == 0x38, "Offset of body incorrect");
static_assert(offsetof(COPTER, switchChannelToShowCopterBody) == 0x70, "Offset of switchChannelToShowCopterBody incorrect");

#pragma pack(pop)

void Copter_Delete(obj_tag *obj);
obj_tag * Copter_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);
obj_tag* Copter_GetBody(COPTER* copter);

#define CopterList (*(LLISTINFO_tag*)0x001fe6a8)

#endif // COPTER_H_
