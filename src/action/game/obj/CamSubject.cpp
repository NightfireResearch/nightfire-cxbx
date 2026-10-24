#include "CamSubject.h"
#include "bullet.h"
#include "control.h"
#include "build.h"
#include "../sp/SwitchChannels.h"

// The placement's parameters as CamSubject_Create reads them
#pragma pack(push, 1)
typedef struct {
    PlacementHeader header; // 0x00
    uint param[1];      // 0x2c
} Create_CamSubject_Params;
#pragma pack(pop)
static_assert(offsetof(Create_CamSubject_Params, param) == 0x2c, "Bad offset of param");

// The weapon (its base) whose shots a camera subject answers

// A thing to photograph: its model, and in its substate the switch channel its photograph sets.
// AUTOINJECT
obj_tag * CamSubject_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl, celglist_tag *celgl) {
    Create_CamSubject_Params *params = (Create_CamSubject_Params *)lvl;
    obj_tag *obj = control_create_object(0, pos, rot, NULL);
    if (obj == NULL)
        return NULL;

    obj->objGraphics = celgl;
    obj->effectFlags = celgl->applyFlagsToObject;
    obj->objectType = OBJECTTYPE_CAMSUBJECT;
    obj->subState = (ushort)params->param[0];
    build_LinkToRoom(obj, 0, glb_world);
    return obj;
}

// A shot of Weap_Camera that hits it sets its channel, and it goes
// AUTOINJECT
void CamSubject_Update(obj_tag *obj) {
    for (HITDATA_tag *hit = obj->hitList; hit != NULL; hit = hit->next) {
        obj_tag *bullet = hit->hitObj;
        if (bullet == NULL || bullet->objectType != OBJECTTYPE_BULLET)
            continue;
        if (((BU_tag *)bullet->extraObjectData)->wpnDef->weaponBaseNum != Weap_Camera)
            continue;
        switch_channels[(short)obj->subState] = 1;
        obj->flags |= OBJFLAG_DELETE;
    }
}
