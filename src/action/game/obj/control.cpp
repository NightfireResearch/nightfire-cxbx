#include "control.h"

#include "../../actionhelpers.h"

#include "../../util/LList.h"
#include "../../gfx/Animation.h"
#include "../scriptplayer.h"
#include "../../memory.h"
#include "../mp/multiplayer.h"
#include "../../math/math.h"
#include "../view.h"
#include "../../game.h"

#include <stdio.h>
#include <string.h>

// Pointer to first game object
#define DynamicObjList_FirstObj (*(obj_tag**)0x001df34c)
#define DynamicObjList (*(obj_tag**)0x001df338)

// Number of game objects
#define DynamicObjCount U32_AT(0x001df828)

// Game objects which span multiple cels
#define ForcedList (*(LLISTINFO_tag*)0x001df41c)

// AUTOINJECT
obj_tag* Control_ReturnNextObjectOfType(ObjectType type, obj_tag* from) {

    obj_tag* at = ((from == NULL) ? DynamicObjList_FirstObj : from);

    while(at != NULL) {

        if(at->objectType == type)
            return at;

        at = at->nextObject;
    }

    return NULL;

}

// AUTOINJECT
void Control_DeleteAllObjectsOfType(ObjectType type) {

    obj_tag *at = DynamicObjList_FirstObj;

    while (at != NULL) {

        if (at->objectType == type)
            at->flags |= 1;
        
        at = at->nextObject;
    }
    
}

// AUTOINJECT
obj_tag* control_first_object(void) {

    return DynamicObjList_FirstObj;

}

// AUTOINJECT
void control_delete_object(obj_tag* obj) {

  printf("Deleting object of type 0x%02x (%s)\n", obj->objectType, Object_GetName((ObjectType)obj->objectType));

  // First, call the object's delete function if it exists
  if(control_funcs[obj->objectType].deleteFunc != NULL)
    control_funcs[obj->objectType].deleteFunc(obj);

  // Clean up the object's hit list
  Collide_FreeHitList(&obj->hitList);

  // Remove from multiplayer game
  if (MPSettings.maybeDroneAIEnabled) {
    MP_objectBeingDeleted(obj);
  }

  obj->objectType = OBJECTTYPE_DELETED;

  // Remove from forced list (objects straddling cels)
  if (obj->specialFlags & FLAG_IN_FORCEDLIST) {
    LList_Remove(&ForcedList, (LLNODE_tag*)obj);
  }

  // Remove animation object and script player
  AnimObjectDelete(obj);
  SP_RemoveObj(obj, obj->scriptPlayer);

  DynamicObjCount--;

  // Remove from cel (doubly-linked)
  control_unlink_object(obj);

  // Remove from object list (doubly-linked)
  obj_tag* prev = obj->prevObject;
  obj_tag* next = obj->nextObject;
  if (next != NULL) {
    next->prevObject = prev;
  }
  if (prev != NULL) {
    prev->nextObject = next;
  }
  obj->prevObject = NULL;
  obj->nextObject = NULL;

  // Finally, free the memory
  Mem_Free((void**)&obj);
  return;

}

// AUTOINJECT
void control_unlink_object(obj_tag* obj) {
  obj_tag* next = obj->nextInCel;
  obj_tag* prev = obj->prevInCel;
  if (next != NULL) {
    next->prevInCel = prev;
  }
  if (prev != NULL) {
    prev->nextInCel = next;
  }
  obj->inCel = NULL;
  obj->nextInCel = NULL;
  obj->prevInCel = NULL;
}

// AUTOINJECT
void control_init_object(obj_tag* obj) {

    if(obj == NULL)
        return;
    
    obj->unknown_0xd4 = 0x001f;
    obj->flags |= 2;
    obj->unknown_0xd8 = 2;
    obj->creationTimeFrames = GameState.NumFramesUnpaused;
    obj->scale = 1.0;
    obj->tweakR = 0xff;
    obj->tweakG = 0xff;
    obj->tweakB = 0xff;
    obj->maybeBrightness = 0xff;
    obj->light_related1 = 0xff;
    obj->light_related2 = 0xff;
    obj->light_related3 = 0xff;

}

void control_add_object_to_list(obj_tag *list,obj_tag *obj) {
  obj_tag *tmp = list->nextObject;
  obj->prevObject = list;
  obj->nextObject = tmp;
  list->nextObject = obj;
  if (tmp != NULL)
    tmp->prevObject = obj;
}

// AUTOINJECT
obj_tag* control_create_object(int sizeBytes,_VECTOR *pos,_VECTOR *rot,quaternion_tag *quat) {

    int totalSize = sizeBytes + sizeof(obj_tag);
    obj_tag* obj = (obj_tag*)Mem_Malloc(totalSize, 0x0404, 0);
    
    if(obj == NULL) {
        printf("Failed to allocate object of size %d bytes\n", sizeBytes);
        return NULL;
    }

    memset(obj, 0, totalSize);

    control_init_object(obj);

    if(pos != NULL) {
        Vec_Copy2(pos, &obj->position, &obj->lastPosition);
    }

    if(rot != NULL) {
        Vec_Copy2(rot, &obj->rotation, &obj->lastRotation);
    }

    if(quat == NULL) {
      View_RotTransMatrix(&obj->rotation, &obj->position, &obj->transformMatrix);
    }
    else {
      Quat_QuatToMat(quat, &obj->transformMatrix);
      Matrix_SetTrans(&obj->position, &obj->transformMatrix);
    }

    obj->extraObjectData = (void*)(obj + 1);
    
    control_add_object_to_list((obj_tag*)&DynamicObjList, obj);
    DynamicObjCount++;

    obj->llPrev = NULL;
    obj->llNext = NULL;
    
    return obj;
}

// AUTOGEN
void control_movement_object_handler(char);

// AUTOGEN
void Control_SetGList(obj_tag *obj, celglist_tag *celgl);

// AUTOGEN
obj_tag * Control_CreateObjEx(unsigned short, _VECTOR *, _VECTOR *, _MATRIX *, celglist_tag *, obj_tag *,char,unsigned short,float,unsigned short,unsigned char,unsigned char,unsigned char);

// AUTOGEN
bool Control_NextLOD(obj_tag *);