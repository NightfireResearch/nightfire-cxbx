#include "DynamicObject.h"

#include "build.h"
#include "control.h"
#include "../../engine/celglist.h"
#include "../../engine/viewer.h"
#include "../sp/SwitchChannels.h"
#include "../../math/math.h"

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
    data->showChannel = (short)keys->unknown2c;
    data->hideChannel = (short)keys->unknown30;
    data->effectFlags = obj->effectFlags;
    data->fadeDistance = (short)keys->unknown34;
    data->hideOnly = (uchar)keys->unknown38;
    if (data->showChannel != 0)
        obj->effectFlags |= 0x30;
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
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
            obj->effectFlags |= 0x30;
        else
            obj->effectFlags = data->effectFlags;
    }
    else if (hide) {
        obj->flags |= 1;
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
