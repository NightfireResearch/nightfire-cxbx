#include "Ladder.h"


#pragma pack(push, 1)

typedef struct {
    short a;
    short b;
} ObjData_Ladder;

static_assert(sizeof(ObjData_Ladder) == 0x4, "Wrong size for ladder info");

typedef struct {
    char unknown[0x30];
    short b;
} Create_Ladder_Params;

#pragma pack(pop)

// AUTOINJECT
obj_tag * Ladder_Create(_VECTOR *param_1,_VECTOR *param_2,level_tag *param_3,celglist_tag *param_4) {

    Create_Ladder_Params *params = (Create_Ladder_Params*)param_3;


    obj_tag *obj = control_create_object(sizeof(ObjData_Ladder), param_1, param_2, NULL);
    if(obj == NULL)
        return NULL;

    ObjData_Ladder * ladder = (ObjData_Ladder*)obj->extraObjectData;

    ladder->a = 0;
    ladder->b = params->b;
    obj->objGraphics = param_4;
    obj->objectType = OBJECTTYPE_LADDER;
    
    build_LinkToRoom(obj, '\0', (level_tag *)glb_world);
    
    Controls_StraddleTest(obj);
    
    return obj;
}

