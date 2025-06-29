#include "NDrone2.h"


// NOAUTOINJECT
bool NDrone2_DSTATE_HostageDead(DCVars_tag* dcVars, Drone_tag *drone, obj_tag *gameObj, MsgObject *msg) {

    switch(msg->msgType) {
    case 0:
        return true;
    
    case 1:
        // TODO: This
        return true;
    
    case 3:
        // TODO: This
        return true;

    case 12:
        // TODO: This
        return true;
    
    default:
        return false;
    
    }

    // Not needed, unreachable as all cases above return
    return false;
}