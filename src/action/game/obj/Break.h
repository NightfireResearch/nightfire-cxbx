#ifndef BREAK_H
#define BREAK_H

#include "../../actionhelpers.h"

typedef enum {
    BreakType_Window = 2,
    BreakType_FORCE_U32 = 0x7FFFFFFF
} BreakType;

typedef struct {
    BreakType breakType;
    float health;
    ushort flags;
    ushort unknown_maybe_pad;
} ObjData_Break;

static_assert(sizeof(ObjData_Break) == 0xc, "Size of ObjData_Break is wrong");

void Break_Update(obj_tag *obj);
void Break_Kill(obj_tag *gameObject, HITDATA_tag *hitData);
obj_tag* Break_Create(_VECTOR *position,_VECTOR *rotation,celglist_tag* celgl,void *pData);

#endif // BREAK_H