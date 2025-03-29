#include "control.h"

#include "../../actionhelpers.h"

#include "../../util/LList.h"
#include "../../gfx/Animation.h"
#include "../scriptplayer.h"
#include "../../memory.h"
#include "../mp/multiplayer.h"
#include <stdio.h>

// Pointer to first game object
#define DynamicObjList (*(obj_tag**)0x001df34c)

// Number of game objects
#define DynamicObjCount U32_AT(0x001df828)

// Game objects which span multiple cels
#define ForcedList (*(LLISTINFO_tag*)0x001df41c)

// AUTOINJECT
obj_tag* Control_ReturnNextObjectOfType(ObjectType type, obj_tag* from) {

    obj_tag* at = ((from == NULL) ? DynamicObjList : from);

    while(at != NULL) {

        if(at->objectType == type)
            return at;

        at = at->nextObject;
    }

    return NULL;

}

// AUTOINJECT
void Control_DeleteAllObjectsOfType(ObjectType type) {

    obj_tag *at = DynamicObjList;

    while (at != NULL) {

        if (at->objectType == type)
            at->flags |= 1;
        
        at = at->nextObject;
    }
    
}

// AUTOINJECT
obj_tag* control_first_object(void) {

    return DynamicObjList;

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

  // Remove from object list (doubly-linked)
  prev = obj->prevObject;
  next = obj->nextObject;
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