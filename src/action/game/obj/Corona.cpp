#include "Corona.h"

#include "build.h"
#include "control.h"

// Half of them start in state 1
// AUTOINJECT
obj_tag* Corona_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(0, pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_CORONA;
    obj->curState = Rand_Rand(100) > 50;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}
