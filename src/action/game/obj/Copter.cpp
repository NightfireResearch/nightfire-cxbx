#include "Copter.h"

#include <string.h>

#include "ScriptPlayer.h"

#define CopterList (*(LLISTINFO_tag*)0x001fe6a8)

// AUTOINJECT
void Copter_Delete(obj_tag *obj) {
 
    COPTER *copter = (COPTER*)obj->extraObjectData;

    SP_Delete(copter->scriptPlayerObj);

    LList_Remove(&CopterList, (LLNODE_tag*)copter);

    memset(copter, 0, sizeof(COPTER));
    
    // We don't free the object?
}