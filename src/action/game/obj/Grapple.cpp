#include "Grapple.h"
#include "control.h"
#include "build.h"

// A point the grapple catches on: only its model (Bullet_Delete sets the grapple point on it).
// AUTOINJECT
obj_tag * Grapple_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(0, pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_GRAPPLE;
    build_LinkToRoom(obj, 0, glb_world);
    Controls_StraddleTest(obj);
    return obj;
}
