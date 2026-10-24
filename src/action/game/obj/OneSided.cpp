#include "OneSided.h"

#include "../../engine/viewer.h"
#include "build.h"
#include "control.h"

#pragma fp_contract(off)

// AUTOINJECT
obj_tag* OneSided_Create(_VECTOR *pos, _VECTOR *rot, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(0, pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    obj->objectType = OBJECTTYPE_ONESIDED;
    obj->objGraphics = celgl;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Hidden from behind: when viewer 0 is on the far side of the way it faces
// AUTOINJECT
void OneSided_Update(obj_tag *obj) {
    _VECTOR toObj, facing;
    Vec_Subtract(&obj->position, &glb_viewer[0]->pos, &toObj);
    Mat_GetDir(&facing, &obj->transformMatrix);
    if (Vec_Dot(&facing, &toObj) < 0.0)
        obj->effectFlags |= EFFECT_HIDDEN;
    else
        obj->effectFlags &= ~EFFECT_HIDDEN;
}
