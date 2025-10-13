#include "Break.h"

#include "../../game.h"
#include "../../util/Random.h"
#include "../../sound/Sound.h"

#include "build.h"

#include <stdio.h>

// NOAUTOINJECT
void Break_Update(obj_tag *obj) {

    if(obj == NULL)
        return;
    
    if(obj->flags & 1)
        return;

    ObjData_Break *breakData = (ObjData_Break*)obj->extraObjectData;

    Collide_FilterBullets(&obj->hitList, breakData->flags | 0x0106);

    HITDATA_tag *hitData = obj->hitList;

    // Iterate over the hitList, pull out the hitter object for each, and process damage
    for(; hitData != NULL; hitData = hitData->next) {

        obj_tag *hitter = hitData->hitObj;

        if(hitter == NULL)
            continue;

        if(hitter->objectType == OBJECTTYPE_DRONE) {

            // If in HendersonA and a Shatterable object, colliding with a drone causes damage proportional to the speed
            if(GameState.CurrentLevelHashcode == HT_Level_HendersonA && breakData->breakType == 2) {
                float speed = Vec_SqDist3D(&hitter->position, &hitter->lastPosition);
                float damage = speed * 20.0f;
                breakData->health -= damage;
            }

            continue;
        }

        if(hitter->objectType == OBJECTTYPE_BULLET) {
            
            breakData->health -= hitData->dmgAmt;

            continue;
        }


    }

    if(breakData->health < 0.0f) {
        Break_Kill(obj, hitData);
    }
}

// NOAUTOINJECT
void Break_ApplyDamage(obj_tag *obj, HITDATA_tag *hitData) {

    ObjData_Break *breakData = (ObjData_Break*)obj->extraObjectData;

    breakData->health -= hitData->dmgAmt;

    if(breakData->health < 0.0f) {
        Break_Kill(obj, hitData);
    }
}

// NOAUTOINJECT
void Break_DoBreak(_VECTOR* position) {

    // Iterate over all objects in the control list, find breakables within the radius
    for(obj_tag *obj = control_first_object(); obj != NULL; obj = obj->nextObject) {

        if(obj->objectType != OBJECTTYPE_BREAK)
            continue;

        float dist = Vec_SqDist3D(&obj->position, position);
        float radius = obj->radius;

        if(radius * radius < dist)
            continue;

        _MATRIX mtx;
        HITDATA_tag hit;

        RotMatrix(&obj->rotation, &mtx);
        Mat_GetDir(&hit.someDirection, &mtx);
        Vec_Copy(&obj->centrePoint, &hit.hitPosition);
        hit.maybehitDirection.x = 0.0f;
        hit.maybehitDirection.y = 0.0f;
        hit.maybehitDirection.z = -0.5f;
        
        Break_Kill(obj, &hit);
        return;
    }
}

// Cannot be injected because it has custom register usage.
// 3 call sites: Break_Update, Break_ApplyDamage, Break_DoBreak
void Break_Kill(obj_tag *gameObject, HITDATA_tag *hitData) {
    // COLLDATA_tag* pCVar1 = obj->objGraphics->collData;
    // obj->flags = obj->flags | 1;

    // if (pCVar1 == NULL)
    //     return;

    // ObjData_Break *objBreak = (ObjData_Break *)obj->extraObjectData;

    // Action_SFX sfx;
    // if (objBreak->breakType == BreakType_Window) {
    //     sfx = SFX_ENV_WINDOW_SMASH_01;
    // }
    // else {
    //     sfx = (Rand_Rand(100) < 41) ? SFX_ENV_BREAK_CERAMIC_SMALL : SFX_ENV_BREAK_CERAMIC_LARGE;
    // }
    // Sound_Play3D(sfx, &obj->centrePoint, 100.0, -1.0, -1.0, 0, 0, 0);

    // if (objBreak->breakType == BreakType_Window) {
    //     // Special path for windows
    //     Break_ShatterWindow(obj, hitData);
    //     return;
    // }

    // // Normal path for any other breakable object
    // Drone_BrokenObject(obj, hitData);
    // if ((hitData->materialAndFlags & 0x3f) == 0) {
    //     hitData->materialAndFlags = 0x10;
    // }
    // Vec_Zero(hitData->someVec);
    // Debris_Create_Loop((int *)pCVar1, hitData, obj);

}

#pragma pack(push, 1)
typedef struct {
    char unknown[0x2c];
    int breakType;
    int health;
    int flags;
} Create_Break_Params;
#pragma pack(pop)



// FIXME: Not implemented properly. Causes a crash in 3rd part of The Exchange right now...
// NOINJECT
obj_tag* Break_Create(_VECTOR *position, _VECTOR *rotation, celglist_tag* celgl, void *pData) {

  Create_Break_Params *params = (Create_Break_Params*)pData;
    
  obj_tag *obj = control_create_object(sizeof(ObjData_Break), position, rotation, NULL);
  if (obj == NULL)
    return NULL;

  ObjData_Break *objBreak = (ObjData_Break *)obj->extraObjectData;
  obj->objectType = OBJECTTYPE_BREAK;
  Control_SetGList(obj, celgl);
  build_LinkToRoom(obj, '\0', (level_tag*)glb_world);
  objBreak->breakType = (BreakType)params->breakType;
  objBreak->health = (float)params->health;

  objBreak->flags = 0;
  if (params->flags) {
    objBreak->flags = ~((ushort)params->flags);

  }

  printf("Created breakable object at %f, %f, %f, with flags %i and health %d\n", position->x, position->y, position->z, params->flags, params->health);
  return obj;
}