#include "Copter.h"

#include <string.h>

#include "ScriptPlayer.h"


// AUTOINJECT
void Copter_Delete(obj_tag *obj) {
 
    COPTER *copter = (COPTER*)obj->extraObjectData;

    SP_Delete(copter->scriptPlayerObj);

    LList_Remove(&CopterList, (LLNODE_tag*)copter);

    memset(copter, 0, sizeof(COPTER));
    
    // We don't free the object?
}

// AUTOGEN
obj_tag * Copter_Create(_VECTOR *pos, _VECTOR *rot, level_tag *lvl);

// AUTOINJECT
obj_tag* Copter_GetBody(COPTER* copter) { 

    if(!SwitchChannel_IsActive(copter->switchChannelToShowCopterBody))
        return NULL;

    return copter->body;
}