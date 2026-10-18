#include "Mine.h"

#include "../../math/X87Ops.h"
#include "build.h"
#include "control.h"
#include "Explode.h"

#pragma fp_contract(off)

#define Control_ObjectReferenced ((void (__cdecl *)(obj_tag *obj, char referenced))0x0002db00)

// Others may point at it until it goes
// AUTOINJECT
obj_tag* Mine_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    obj_tag *obj = control_create_object(sizeof(MINE_tag), pos, rot, NULL);
    if (obj == NULL)
        return NULL;
    MINE_tag *mine = (MINE_tag *)obj->extraObjectData;
    MinePlacement *keys = (MinePlacement *)lvl;
    Vec_Copy(pos, &mine->home);
    mine->damage = (float)keys->damage;
    mine->radius = (float)keys->radius;
    mine->spin = (float)((double)keys->spin * 0.0002f);
    mine->bobRate = (float)((double)keys->bobRate * 0.0002f);
    mine->bob = (float)((double)keys->bob * 0.1f);
    mine->script = keys->script;
    obj->objectType = OBJECTTYPE_MINE;
    obj->objGraphics = celgl;
    build_LinkToRoom(obj, 0, glb_world);
    Control_ObjectReferenced(obj, 1);
    return obj;
}

// The first hit with an object sets it off
// AUTOINJECT
void Mine_Update(obj_tag *obj) {
    MINE_tag *mine = (MINE_tag *)obj->extraObjectData;
    obj->rotation.y = (float)((double)FRAME_RATE_MUL * mine->spin + obj->rotation.y);
    Extended bob = FSin((double)GameState.NumFramesUnpaused * mine->bobRate);
    obj->transformFlags |= TRANSFORM_MOVED;
    obj->position.y = (float)(bob * Extended(mine->bob) + Extended(mine->home.y));

    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        if (hit->hitObj == NULL)
            continue;
        Explode_Create(obj, &obj->position, &CONST_UP_VECTOR, mine->radius, mine->radius, mine->script, mine->damage,
                       0xc8, 0xc8, 0, obj, 0);
        obj->flags |= 1;
        return;
    }
}
