#include "ui.h"

#include "Menu.h"
#include "../game/mp/multiplayer.h"

#include "../assets.h"

#include <stdio.h>


M_ITEM mp_level[8] = {
    {ICON_MPMAP_SKYRAIL, MP_MAP_SKYRAIL_NAME, MP_MAP_SKYRAIL_DESC, HT_Level_SkyRail, true, TXT_NULL},
    {ICON_MPMAP_FORTKNOX, MP_MAP_FORTKNOX_NAME, MP_MAP_FORTKNOX_DESC, HT_Level_FortKnox, true, TXT_NULL},
    {ICON_MPMAP_SNOWBLIND, MP_MAP_SNOWBLIND_NAME, MP_MAP_SNOWBLIND_DESC, HT_Level_SnowBlind, true, TXT_NULL},
    {ICON_MPMAP_PHOENIXBASE, MP_MAP_PHOENIXBASE_NAME, MP_MAP_PHOENIXBASE_DESC, HT_Level_StealthShip, true, TXT_NULL},
    {ICON_MPMAP_ATLANTIS, MP_MAP_ATLANTIS_NAME, MP_MAP_ATLANTIS_DESC, HT_Level_Atlantis, true, TXT_NULL},
    {ICON_MPMAP_SILO, MP_MAP_SILO_NAME, MP_MAP_SILO_DESC, HT_Level_MissileSilo, true, TXT_NULL},
    {ICON_MPMAP_SUBPEN, MP_MAP_SUBPEN_NAME, MP_MAP_SUBPEN_DESC, HT_Level_SubPen, true, TXT_NULL},
    {ICON_MPMAP_RAVINE, MP_MAP_RAVINE_NAME, MP_MAP_RAVINE_DESC, HT_Level_Ravine, true, TXT_NULL}
};

M_ITEM dummy1[999]; // Dummy array to prevent crash in Menu_UpdateWheel

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

#define menu_unlock_everything U8_AT(0x0025d79e)

// AUTOINJECT
bool C_SBMPMAP_Handler(uchar param_1,M_CONTROL *param_2,uint param_3,uint param_4,int param_5,int param_6) {
  uint uVar1;
  
  switch(param_4) {
  case 0x49:
  case 0x54:
    Menu_UpdateWheel(param_1,param_2,mp_level,(HASHCODE)0x100000e9,(HASHCODE)0x1000000a,(HASHCODE)0x1000000b,(HASHCODE)0x10000105,param_4 == 0x49);
    return true;
  case 0x4b:
    uVar1 = __Menu_SendMessage(param_2,0x40,0,0);
    if ((menu_unlock_everything != '\0') || (mp_level[uVar1 & 0xff].enabled != false)) {
      GameState.NextLevelHashcode = (HASHCODE)mp_level[uVar1 & 0xff].identifier;
      MPSettings.multiplayerLevelHashcode = GameState.NextLevelHashcode;
      Menu_ChangePageCloseIris(P_MPSETUP,param_1,0x10000105);
    }
    break;
  case 0x51:
    __Menu_SendMessage(param_2,0x27,0,7);
    return true;
  }
  return true;
}
