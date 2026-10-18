#include "DynamicObject.h"

#include "build.h"
#include "control.h"
#include "../../engine/celglist.h"

// A dynamic object, linked to its room. Its model is used unchecked.
// AUTOINJECT
obj_tag* DynamicObject_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(sizeof(DYNAMICOBJECT_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    DynamicObjectPlacement *keys = (DynamicObjectPlacement *)lvl;
    DYNAMICOBJECT_tag *data = (DYNAMICOBJECT_tag *)obj->extraObjectData;
    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_DYNAMICOBJECT;
    data->unknown0 = (short)keys->unknown2c;
    data->unknown2 = (short)keys->unknown30;
    data->effectFlags = obj->effectFlags;
    data->unknown8 = (short)keys->unknown34;
    data->unknownA = (uchar)keys->unknown38;
    if (data->unknown0 != 0)
        obj->effectFlags |= 0x30;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}
