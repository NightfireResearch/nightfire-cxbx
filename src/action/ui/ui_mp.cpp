#include "ui.h"

#include "Manager.h"
#include "Menu.h"
#include "../game/mp/multiplayer.h"

#include "../assets.h"

#include <stdio.h>

M_ITEM mp_level[8] = {
    {ICON_MPMAP_SKYRAIL,        MPMAP_SKYRAIL_NAME,        MPMAP_SKYRAIL_DESC,        HT_Level_SkyRail,       true, TXT_NULL},
    {ICON_MPMAP_FORTKNOX,       MPMAP_FORTKNOX_NAME,       MPMAP_FORTKNOX_DESC,       HT_Level_FortKnox,      true, TXT_NULL},
    {ICON_MPMAP_SNOWBLIND,      MPMAP_SNOWBLIND_NAME,      MPMAP_SNOWBLIND_DESC,      HT_Level_SnowBlind,     true, TXT_NULL},
    {ICON_MPMAP_PHOENIXBASE,    MPMAP_PHOENIXBASE_NAME,    MPMAP_PHOENIXBASE_DESC,    HT_Level_StealthShip,   true, TXT_NULL},
    {ICON_MPMAP_ATLANTIS,       MPMAP_ATLANTIS_NAME,       MPMAP_ATLANTIS_DESC,       HT_Level_Atlantis,      true, TXT_NULL},
    {ICON_MPMAP_SILO,           MPMAP_SILO_NAME,           MPMAP_SILO_DESC,           HT_Level_MissileSilo,   true, TXT_NULL},
    {ICON_MPMAP_SUBPEN,         MPMAP_SUBPEN_NAME,         MPMAP_SUBPEN_DESC,         HT_Level_SubPen,        true, TXT_NULL},
    {ICON_MPMAP_RAVINE,         MPMAP_RAVINE_NAME,         MPMAP_RAVINE_DESC,         HT_Level_Ravine,        true, TXT_NULL}
};

M_ITEM mp_scenario[13] = {
    {ICON_MPSCENARIO_QUICKGAME,     MPSCENARIO_QUICKGAME_NAME,  MPSCENARIO_QUICKGAME_DESC,  0x00000000, true,   MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_ARENA,         MPSCENARIO_ARENA_NAME,      MPSCENARIO_ARENA_DESC,      0x00000001, true,   MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_TEAMARENA,     MPSCENARIO_TEAMARENA_NAME,  MPSCENARIO_TEAMARENA_DESC,  0x20000002, true,   MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_CTF,           MPSCENARIO_CTF_NAME,        MPSCENARIO_CTF_DESC,        0x20000004, true,   MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_UPLINK,        MPSCENARIO_UPLINK_NAME,     MPSCENARIO_UPLINK_DESC,     0x60000008, false,  MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_TOPAGENT,      MPSCENARIO_TOPAGENT_NAME,   MPSCENARIO_TOPAGENT_DESC,   0x00000010, true,   MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_DEMOLITION,    MPSCENARIO_DEMOLITION_NAME, MPSCENARIO_DEMOLITION_DESC, 0x20000040, false,  MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_PROTECTION,    MPSCENARIO_PROTECTION_NAME, MPSCENARIO_PROTECTION_DESC, 0x20000080, false,  MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_ESPIONAGE,     MPSCENARIO_ESPIONAGE_NAME,  MPSCENARIO_ESPIONAGE_DESC,  0x20000100, true,   MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_GOLDENEYE,     MPSCENARIO_GOLDENEYE_NAME,  MPSCENARIO_GOLDENEYE_DESC,  0x20000200, false,  MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_ASSASSIN,       MPSCENARIO_ASSASSIN_NAME,  MPSCENARIO_ASSASSIN_DESC,   0x00000400, false,  MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_KOTH,          MPSCENARIO_KOTH_NAME,       MPSCENARIO_KOTH_DESC,       0x40000800, true,   MP_SCENARIO_LOCKED},
    {ICON_MPSCENARIO_TEAMKOTH,      MPSCENARIO_TEAMKOTH_NAME,   MPSCENARIO_TEAMKOTH_DESC,   0x60001000, true,   MP_SCENARIO_LOCKED}
};

// AUTOINJECT
bool P_MPMAP_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {

    switch((MessageType)message) {
        case 0x4c: {
            Menu_StartIris(((param_6 == P_MPSCENARIO) ? 0 : 4), param_1, SUB_C_MP_IRIS);
            M_CONTROL* ctrl = (M_CONTROL*)__Menu_Send(param_1, C_SBMPMAP, 0x39, 0, 0);
            Menu_SelectItemInControl(ctrl, mp_level, 8, MPSettings.multiplayerLevelHashcode);
            break;
        }
        case 0x50: {
            Menu_PlayIris(1,param_1,SUB_C_MP_IRIS);
            break;
        }
    }
    return true;  
}

#define menu_unlock_everything U8_AT(0x0025d79e)

// AUTOINJECT
bool C_SBMPMAP_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {
  
    MessageType event = (MessageType)message;

    switch(event) {
        case MessageType_Scroll:
        case MessageType_Enter: {
            Menu_UpdateWheel(param_1, param_2, mp_level, (HASHCODE)0x100000e9, (HASHCODE)0x1000000a, (HASHCODE)0x1000000b, SUB_C_MP_IRIS, event == MessageType_Scroll);
            break;
        }
        case MessageType_Select: {
            uchar idx = __Menu_SendMessage(param_2, MessageType_GetValue, 0, 0);
            if (menu_unlock_everything || mp_level[idx].enabled) {
                GameState.NextLevelHashcode = (HASHCODE)mp_level[idx].identifier;
                MPSettings.multiplayerLevelHashcode = GameState.NextLevelHashcode;
                Menu_ChangePageCloseIris(P_MPSETUP, param_1, SUB_C_MP_IRIS);
            }
            break;
        }
        case 0x51: {
            __Menu_SendMessage(param_2, 0x27, 0, 7);
            break;
        }
    }

    return true;
}

// AUTOINJECT
bool P_MPSCENARIO_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {
  
    switch(message) {
        case 0x4c: {
            Menu_StartIris((param_6 == P_MAIN) ? 0 : 4, managerNum, SUB_C_MPSCENARIO_IRIS);
            Menu_UnlockMPSettings();
            M_CONTROL *pMVar1 = (M_CONTROL *)__Menu_Send(managerNum, C_SBMPSCEN, 0x39, 0, 0);
            Menu_SelectItemInControl(pMVar1, mp_scenario, 0xd, MPSettings.GameMode);
            break;
        }
        case 0x50: {
            Menu_PlayIris(1, managerNum, SUB_C_MPSCENARIO_IRIS);
            break;
        }
        case 0x63: {
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MAIN, 0);
            break;
        }
    }

  return true;
}