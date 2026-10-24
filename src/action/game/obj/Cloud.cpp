#include "Cloud.h"

#include <math.h>

#include "../view.h"
#include "control.h"

#pragma fp_contract(off)

// Not checked: the object
// AUTOINJECT
obj_tag* Cloud_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(sizeof(CLOUD_tag), pos, rot, NULL);
    CloudPlacement *keys = (CloudPlacement *)lvl;
    CLOUD_tag *cloud = (CLOUD_tag *)obj->extraObjectData;
    obj->objGraphics = celgl;
    obj->objectType = OBJECTTYPE_CLOUD;
    cloud->height = keys->height;
    cloud->speedX = (float)((double)keys->speedX * 0.001f);
    cloud->speedZ = (float)((double)keys->speedZ * 0.001f);
    cloud->wrap = keys->wrap;
    cloud->offsetX = 0.0f;
    cloud->offsetZ = 0.0f;
    View_SetDrawInNoViews(obj);
    View_AddSkyObj((ushort)keys->skyObj, NULL, NULL, NULL, obj, 0, 0, 0, 1);
    return obj;
}

// The XBE's fmodf is FPREM, which the C runtime's fmod matches for finite values (the XBE's raises its error for
// the others)
// AUTOINJECT
void cloud_update(obj_tag *obj) {
    CLOUD_tag *cloud = (CLOUD_tag *)obj->extraObjectData;
    if (glb_players[0] == NULL)
        return;
    float wrap = (float)cloud->wrap;
    cloud->offsetX = (float)fmod((double)FRAME_RATE_MUL * cloud->speedX + cloud->offsetX, (double)wrap);
    cloud->offsetZ = (float)fmod((double)FRAME_RATE_MUL * cloud->speedZ + cloud->offsetZ, (double)wrap);
    obj->position.x = glb_players[0]->position.x + cloud->offsetX;
    obj->position.z = glb_players[0]->position.z + cloud->offsetZ;
    obj->position.y = (float)((double)cloud->height + glb_players[0]->position.y);
}
