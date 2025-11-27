#include "Rotor.h"

#pragma pack(push, 1)
typedef struct {
    _VECTOR axis;
    char switchChannel;
    char switchPolarity; // Rotates when no switch channel, or switchPolarity == switch_channels[switchChannel]
    char _pad[2];
} ObjData_Rotor;
#pragma pack(pop)

// AUTOINJECT
obj_tag * rotor_init(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl, ushort param_4, uchar param_5, uchar switchChannel) {

    obj_tag *gameObj = control_create_object(sizeof(ObjData_Rotor), pos, rot, NULL);

    if(gameObj == NULL)
        return NULL;

    ObjData_Rotor *rotor = (ObjData_Rotor*)gameObj->extraObjectData;
    gameObj->objectType = OBJECTTYPE_ROTOR;
    rotor->switchChannel = switchChannel;
    Vec_Copy(rot, &rotor->axis);

    if(celgl != NULL) {
        gameObj->objGraphics = celgl;
        gameObj->effectFlags = celgl->applyFlagsToObject;
    }

    build_LinkToRoom(gameObj, 0, glb_world);
}

// AUTOINJECT
void rotor_update(obj_tag* gameObj) { 

    ObjData_Rotor *rotor = (ObjData_Rotor*)gameObj->extraObjectData;

    if((rotor->switchChannel == 0) || (switch_channels[rotor->switchChannel] == rotor->switchPolarity)) {
        gameObj->renderType |= 0x20;
        auxVec_AddMulR32(&gameObj->rotation, &rotor->axis, FRAME_RATE_MUL, &gameObj->rotation);
    }
}