#include "CreepWall.h"
#include "control.h"
#include "build.h"

// The placement's parameters as CreepWall_Create reads them
#pragma pack(push, 1)
typedef struct {
    uchar _unknown00[0x2c];
    uint param[4];      // 0x2c
} Create_CreepWall_Params;
#pragma pack(pop)
static_assert(offsetof(Create_CreepWall_Params, param) == 0x2c, "Bad offset of param");

// A wall to creep along: its model is what the player uses (Player_Activate).
// AUTOINJECT
obj_tag * CreepWall_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_CreepWall_Params *params = (Create_CreepWall_Params *)lvl;
    obj_tag *obj = control_create_object(sizeof(CREEPWALL), pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    CREEPWALL *wall = (CREEPWALL *)obj->extraObjectData;
    wall->field_0 = (short)params->param[0];
    wall->thirdCam = (ushort)params->param[1];
    wall->field_4 = (short)params->param[2];
    wall->channel = (short)params->param[3];
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_CREEPWALL;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}
