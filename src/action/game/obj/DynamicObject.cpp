#include "DynamicObject.h"

#include "build.h"
#include "control.h"
#include "../../engine/celglist.h"
#include "../../engine/viewer.h"
#include "../sp/SwitchChannels.h"
#include "../../math/math.h"
#include "../../math/X87Ops.h"

#include <math.h>

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
    data->showChannel = (short)keys->showChannel;
    data->hideChannel = (short)keys->hideChannel;
    data->effectFlags = obj->effectFlags;
    data->fadeDistance = (short)keys->fadeDistance;
    data->hideOnly = (uchar)keys->hideOnly;
    if (data->showChannel != 0)
        obj->effectFlags |= EFFECT_HIDDEN | EFFECT_NO_COLLISION;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// Waits for showChannel, then takes its model's flags back. hideChannel hides it (hideOnly) or deletes it. Nearer the
// first view than fadeDistance, its alpha is 0 over the first half and rises to 255 over the second.
// AUTOINJECT
void DynamicObject_Update(obj_tag *obj) {
    DYNAMICOBJECT_tag *data = (DYNAMICOBJECT_tag *)obj->extraObjectData;
    if (data->showChannel != 0) {
        if (!switch_channels[data->showChannel])
            return;
        obj->effectFlags = data->effectFlags;
    }
    bool hide = data->hideChannel != 0 && switch_channels[data->hideChannel];
    if (data->hideOnly) {
        if (hide)
            obj->effectFlags |= EFFECT_HIDDEN | EFFECT_NO_COLLISION;
        else
            obj->effectFlags = data->effectFlags;
    }
    else if (hide) {
        obj->flags |= OBJFLAG_DELETE;
    }
    if (data->fadeDistance == 0)
        return;

    float distance = Vec_Dist3D(&obj->centrePoint, &glb_viewer[0]->pos);
    if (!(distance < data->fadeDistance))
        return;
    double fade = (double)distance / data->fadeDistance - 0.5f;
    if (fade < 0.0f)
        fade = 0.0f;
    obj->tweakA = (uchar)Ftol((fade + fade) * 255.0f);
}
