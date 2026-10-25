#include "Ladder.h"

#pragma pack(push, 1)

typedef struct {
    PlacementHeader header; // 0x00
    char unknown[4];
    short b;
} Create_Ladder_Params;

#pragma pack(pop)

// AUTOINJECT
obj_tag * Ladder_Create(_VECTOR *pos,_VECTOR *rot,level_tag *lvl,celglist_tag *celgl) {

    Create_Ladder_Params *params = (Create_Ladder_Params*)lvl;

    obj_tag *obj = control_create_object(sizeof(ObjData_Ladder), pos, rot, NULL);
    if(obj == NULL)
        return NULL;

    ObjData_Ladder * ladder = (ObjData_Ladder*)obj->extraObjectData;

    ladder->unknown0 = 0;
    ladder->param1 = params->b;
    obj->objGraphics = celgl;
    obj->objectType = OBJECTTYPE_LADDER;
    
    build_LinkToRoom(obj, '\0', glb_world);
    
    Controls_StraddleTest(obj);
    
    return obj;
}

