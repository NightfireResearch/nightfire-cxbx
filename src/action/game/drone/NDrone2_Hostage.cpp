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

#define NUM_HOSTAGES_SAVED U32_AT(0x001e57cc)

#include <stdio.h>

// This is called perhaps with an argument on the stack depending on whether hostage count was initially 0 (ie is this the first freed)
// but this parameter doesn't appear to be used on either Xbox or PS2
// As such we can simplify the flow
// UNINJECTABLE - custom calling convention and only used from DroneFunc_HostageSaved
void DroneFunc_NotifyHostageSaved(uint numHostagesSaved) {
    char* str = (char*)Txt_BindLabel(numHostagesSaved == 1 ? NOTIF_ONE_HOSTAGE_RELEASED : NOTIF_ALL_HOSTAGES_RELEASED, 0);
    Text_AddMsg(NULL, NULL, 1, str, 0, 180);
}

// AUTOINJECT
void DroneFunc_HostageSaved(DCVars_tag *dcVars) {

    const int switchChannel = dcVars->drone->associatedSwitchChannel;

    // Already activated?
    if(switch_channels[switchChannel])
        return;

    NUM_HOSTAGES_SAVED++;
    
    if(switchChannel) {
        switch_channels[switchChannel] = true;
        switch_channels_time[switchChannel] = GameState.NumFramesUnpaused;
    }

    if(GameState.CurrentLevelHashcode == HT_Level_HendersonB || GameState.CurrentLevelHashcode == HT_Level_HendersonC) {
        DroneFunc_NotifyHostageSaved(NUM_HOSTAGES_SAVED);
    }
    
}