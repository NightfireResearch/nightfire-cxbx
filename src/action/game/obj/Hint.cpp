#include "Hint.h"

#include "object.h"
#include "../../game.h"

extern uint32_t HintsEnabled; // defined in game.cpp

// A hint placement's parameters, as the level data has them (after the common 0x2c-byte placement header).
typedef struct {
    char header[0x2c];
    uint data1;          // 0x2c - hint word 1 (stored second)
    uint data0;          // 0x30 - hint word 0
    uint difficulty;     // 0x34 - 0: any; 1-4: at most this difficulty; 5-7: only difficulty 1-3
    uint data2;          // 0x38
    uint data3;          // 0x3c
} HintPlacement;
static_assert(offsetof(HintPlacement, difficulty) == 0x34, "Bad offset of HintPlacement::difficulty");

// A hint object (object type 0x43): only when hints are on and the placement's difficulty rule allows it.
// AUTOINJECT
obj_tag * Hint_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    if (HintsEnabled == 0)
        return NULL;
    HintPlacement *p = (HintPlacement *)lvl;
    uint difficulty = GameState.difficultyModifier;
    if (p->difficulty != 0 && difficulty > p->difficulty)
        return NULL;
    if (p->difficulty == 5 && difficulty != 1)
        return NULL;
    if (p->difficulty == 6 && difficulty != 2)
        return NULL;
    if (p->difficulty == 7 && difficulty != 3)
        return NULL;

    obj_tag *obj = control_create_object(0x10, pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    obj->objectType = OBJECTTYPE_HINT;
    Control_SetGList(obj, celgl);
    uint *data = (uint *)obj->extraObjectData;
    data[1] = p->data1;
    data[0] = p->data0;
    data[2] = p->data2;
    data[3] = p->data3;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}
