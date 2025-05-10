#include "ui.h"

#include "Menu.h"
#include "../game/mp/multiplayer.h"

#define mp_level ((M_ITEM*)0x0017c6a0)

// AUTOINJECT
bool P_MPMAP_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {

    MessageType event = (MessageType)message;

    switch(event) {
    case 0x4c: {
        Menu_StartIris(((param_6 == 0x4000001a) ? 0 : 4),param_1,0x10000105);
        M_CONTROL* ctrl = (M_CONTROL*)__Menu_Send(param_1,C_SBMPMAP,0x39,0,0);
        Menu_SelectItemInControl(ctrl, mp_level, 8, MPSettings.multiplayerLevelHashcode);
        return true;
        }
    case 0x50:
        Menu_PlayIris(1,param_1,0x10000105);
        return true;
    default:
        return true;       
    }
}
