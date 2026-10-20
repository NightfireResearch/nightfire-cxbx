#include "Corona.h"

#include "build.h"
#include "control.h"
#include "../../game.h"
#include "../../engine/Collide.h"
#include "../../engine/viewer.h"
#include "../../math/math.h"

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

// Every other frame (by curState, 0 or 1, against the frame count's lowest bit), while drawn lately: brightens by 16
// while in sight of the first view, flag 0x2000 set; dims by 16 out of it. Not drawn, it goes dark.
// AUTOINJECT
void Corona_Update(obj_tag *obj) {
    if ((ushort)(GameState.NumFramesUnpaused & 1) == obj->curState)
        return;
    if (!obj->drawnCountdown) {
        obj->tweakA = 0;
        return;
    }
    viewer_tag *viewer = glb_viewer[0];
    if (Collide_LineOfSight(&viewer->pos, &obj->position, viewer->someCel, obj, glb_players[0], 8)) {
        obj->effectFlags |= 0x2000;
        int alpha = obj->tweakA + 16;
        obj->tweakA = (uchar)(alpha > 0xff ? 0xff : alpha);
    }
    else {
        obj->effectFlags &= ~0x2000;
        int alpha = obj->tweakA - 16;
        obj->tweakA = (uchar)(alpha < 0 ? 0 : alpha);
    }
}

// A sun reflection: kept at its offset (the extra data) from the first view. It grows by a tenth a frame until
// past 100 (curState 0 to 1), then shrinks by the same until under 2, and is deleted.
// AUTOINJECT
void Sunreflection_Update(obj_tag *obj) {
    _VECTOR pos;
    Vec_Add2(&glb_viewer[0]->pos, (_VECTOR *)obj->extraObjectData, &pos);
    Vec_Copy(&pos, &obj->position);
    obj->transformFlags |= TRANSFORM_MOVED;
    if (obj->curState == 0) {
        double scale = obj->scale * 1.1;
        obj->scale = (float)scale;
        if (scale > 100.0f)
            obj->curState = 1;
    }
    else {
        double scale = obj->scale * (1.0 / 1.1);
        obj->scale = (float)scale;
        if (scale < 2.0f)
            obj->flags |= 1;
    }
}
