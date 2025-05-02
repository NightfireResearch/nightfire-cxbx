#include "Door.h"


// Only used for drone navigation - the player's interaction with locked doors is independent of this?
// AUTOINJECT
bool Door_IsLocked(obj_tag* obj) {
    if(obj == NULL)
        return false;
    
    DOORINFO* doorInfo = (DOORINFO*)(obj->extraObjectData);

    ushort swChannel = doorInfo->unlockSwitchChannel;

    // Not lockable
    if(swChannel == 0)
        return false;

    // Lockable - when the switch is activated, the door is unlocked
    return !switch_channels[swChannel];
    
}