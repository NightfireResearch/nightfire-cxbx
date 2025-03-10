#include "control.h"

// Pointer to first game object
#define DynamicObjList (*(obj_tag**)0x001df34c)

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

obj_tag* control_first_object(void) {

    return DynamicObjList;

}