#include "ui.h"

#include "Manager.h"
#include "Menu.h"
#include "../game/mp/multiplayer.h"
#include "../game/drone/BOT.h"
#include "../engine/Text.h"
#include "../input.h"
#include "MenuManager.h"
#include "Process.h"
#include "../sound/SFX.h"
#include "../game.h"
#include "../util/Random.h"

#include "../assets.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

// XBE_GLOBAL(0x0017c6a0, 0xc0)
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

// XBE_GLOBAL(0x0017c7a8, 0x138)
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

// The multiplayer characters (identifier = skin number). Items 12-28 are unlocked by rewards (Menu_UnlockMPSkins).
// XBE_GLOBAL(0x0017c8e0, 0x2b8)
M_ITEM mp_characters[29] = {
    {ICON_MPCHAR_BOND, CHAR_BOND_FULLNAME, CHAR_BOND_DESC, 0, 1, CHARACTER_LOCKED}, // Bond
    {ICON_MPCHAR_DRAKE, CHAR_DRAKE_FULLNAME, CHAR_DRAKE_DESC, 1, 1, CHARACTER_LOCKED}, // Drake
    {ICON_MPCHAR_ROOK, CHAR_ROOK_FULLNAME, CHAR_ROOK_DESC, 2, 1, CHARACTER_LOCKED}, // Rook
    {ICON_MPCHAR_KIKO, CHAR_KIKO_FULLNAME, CHAR_KIKO_DESC, 3, 1, CHARACTER_LOCKED}, // Kiko
    {ICON_MPCHAR_ALURA, CHAR_ALURA_FULLNAME, CHAR_ALURA_DESC, 4, 1, CHARACTER_LOCKED}, // Alura
    {ICON_MPCHAR_DOMINIQUE, CHAR_DOMINIQUE_FULLNAME, CHAR_DOMINIQUE_DESC, 5, 1, CHARACTER_LOCKED}, // Dominique
    {ICON_MPCHAR_SNOWGUARD, CHAR_GUARD_FULLNAME, CHAR_GUARD_DESC, 6, 1, CHARACTER_LOCKED}, // Snow Guard
    {ICON_MPCHAR_BLACKOPS, CHAR_BLACKOPS_FULLNAME, CHAR_BLACKOPS_DESC, 7, 1, CHARACTER_LOCKED}, // Black Ops
    {ICON_MPCHAR_YAKUZA, CHAR_YAKUZA_FULLNAME, CHAR_YAKUZA_DESC, 8, 1, CHARACTER_LOCKED}, // Yakuza
    {ICON_MPCHAR_PHOENIXCOMMANDO, CHAR_COMMANDO_FULLNAME, CHAR_COMMANDO_DESC, 9, 1, CHARACTER_LOCKED}, // Phoenix Commando
    {ICON_MPCHAR_PHOENIXSOLDIER, CHAR_SOLDIER_FULLNAME, CHAR_SOLDIER_DESC, 0xa, 1, CHARACTER_LOCKED}, // Phoenix Soldier
    {ICON_MPCHAR_NINJA, CHAR_NINJA_FULLNAME, CHAR_NINJA_DESC, 0xb, 1, CHARACTER_LOCKED}, // Ninja
    {ICON_MPCHAR_BONDTUX, CHAR_BONDTUX_FULLNAME, CHAR_BONDTUX_DESC, 0xc, 0, CHARACTER_LOCKED}, // Bond Tux
    {ICON_MPCHAR_DRAKESUIT, CHAR_DRAKESUIT_FULLNAME, CHAR_DRAKESUIT_DESC, 0xd, 0, CHARACTER_LOCKED}, // Drake Suit
    {ICON_MPCHAR_BONDSPACESUIT, CHAR_BONDSPACE_FULLNAME, CHAR_BONDSPACE_DESC, 0xe, 0, CHARACTER_LOCKED}, // Bond Spacesuit
    {ICON_MPCHAR_GOLDFINGER, CHAR_GOLDFINGER_FULLNAME, CHAR_GOLDFINGER_DESC, 0xf, 0, CHARACTER_LOCKED}, // Goldfinger
    {ICON_MPCHAR_RENARD, CHAR_RENARD_FULLNAME, CHAR_RENARD_DESC, 0x10, 0, CHARACTER_LOCKED}, // Renard
    {ICON_MPCHAR_SCARAMANGA, CHAR_SCARAMANGA_FULLNAME, CHAR_SCARAMANGA_DESC, 0x11, 0, CHARACTER_LOCKED}, // Scaramanga
    {ICON_MPCHAR_PUSSYGALORE, CHAR_GALORE_FULLNAME, CHAR_GALORE_DESC, 0x12, 0, CHARACTER_LOCKED}, // Pussy Galore
    {ICON_MPCHAR_CHRISTMASJONES, CHAR_XMASJONES_FULLNAME, CHAR_XMASJONES_DESC, 0x13, 0, CHARACTER_LOCKED}, // Christmas Jones
    {ICON_MPCHAR_WAILIN, CHAR_WAILIN_FULLNAME, CHAR_WAILIN_DESC, 0x14, 0, CHARACTER_LOCKED}, // Wai Lin
    {ICON_MPCHAR_XENIAONATOPP, CHAR_XENIA_FULLNAME, CHAR_XENIA_DESC, 0x15, 0, CHARACTER_LOCKED}, // Xenia Onatopp
    {ICON_MPCHAR_MAYDAY, CHAR_MAYDAY_FULLNAME, CHAR_MAYDAY_DESC, 0x16, 0, CHARACTER_LOCKED}, // May Day
    {ICON_MPCHAR_ELEKTRAKING, CHAR_ELEKTRA_FULLNAME, CHAR_ELEKTRA_DESC, 0x17, 0, CHARACTER_LOCKED}, // Elektra King
    {ICON_MPCHAR_JAWS, CHAR_JAWS_FULLNAME, CHAR_JAWS_DESC, 0x18, 0, CHARACTER_LOCKED}, // Jaws
    {ICON_MPCHAR_BARONSAMEDI, CHAR_SAMEDI_FULLNAME, CHAR_SAMEDI_DESC, 0x19, 0, CHARACTER_LOCKED}, // Baron Samedi
    {ICON_MPCHAR_ODDJOB, CHAR_ODDJOB_FULLNAME, CHAR_ODDJOB_DESC, 0x1a, 0, CHARACTER_LOCKED}, // Oddjob
    {ICON_MPCHAR_NICKNACK, CHAR_NICKNACK_FULLNAME, CHAR_NICKNACK_DESC, 0x1b, 0, CHARACTER_LOCKED}, // Nick Nack
    {ICON_MPCHAR_MAXZORIN, CHAR_ZORIN_FULLNAME, CHAR_ZORIN_DESC, 0x1c, 0, CHARACTER_LOCKED}, // Max Zorin
};

// The same characters with small icons, for the bot and player setup pages; unlocked alongside mp_characters.
// XBE_GLOBAL(0x0017cb98, 0x2b8)
M_ITEM mp_characters_small[29] = {
    {ICON_MPCHAR_SMALL_BOND, CHAR_BOND_FULLNAME, CHAR_BOND_DESC, 0, 1, CHARACTER_LOCKED}, // Bond
    {ICON_MPCHAR_SMALL_DRAKE, CHAR_DRAKE_FULLNAME, CHAR_DRAKE_DESC, 1, 1, CHARACTER_LOCKED}, // Drake
    {ICON_MPCHAR_SMALL_ROOK, CHAR_ROOK_FULLNAME, CHAR_ROOK_DESC, 2, 1, CHARACTER_LOCKED}, // Rook
    {ICON_MPCHAR_SMALL_KIKO, CHAR_KIKO_FULLNAME, CHAR_KIKO_DESC, 3, 1, CHARACTER_LOCKED}, // Kiko
    {ICON_MPCHAR_SMALL_ALURA, CHAR_ALURA_FULLNAME, CHAR_ALURA_DESC, 4, 1, CHARACTER_LOCKED}, // Alura
    {ICON_MPCHAR_SMALL_DOMINIQUE, CHAR_DOMINIQUE_FULLNAME, CHAR_DOMINIQUE_DESC, 5, 1, CHARACTER_LOCKED}, // Dominique
    {ICON_MPCHAR_SMALL_SNOWGUARD, CHAR_GUARD_FULLNAME, CHAR_GUARD_DESC, 6, 1, CHARACTER_LOCKED}, // Snow Guard
    {ICON_MPCHAR_SMALL_BLACKOPS, CHAR_BLACKOPS_FULLNAME, CHAR_BLACKOPS_DESC, 7, 1, CHARACTER_LOCKED}, // Black Ops
    {ICON_MPCHAR_SMALL_YAKUZA, CHAR_YAKUZA_FULLNAME, CHAR_YAKUZA_DESC, 8, 1, CHARACTER_LOCKED}, // Yakuza
    {ICON_MPCHAR_SMALL_PHOENIXCOMMANDO, CHAR_COMMANDO_FULLNAME, CHAR_COMMANDO_DESC, 9, 1, CHARACTER_LOCKED}, // Phoenix Commando
    {ICON_MPCHAR_SMALL_PHOENIXSOLDIER, CHAR_SOLDIER_FULLNAME, CHAR_SOLDIER_DESC, 0xa, 1, CHARACTER_LOCKED}, // Phoenix Soldier
    {ICON_MPCHAR_SMALL_NINJA, CHAR_NINJA_FULLNAME, CHAR_NINJA_DESC, 0xb, 1, CHARACTER_LOCKED}, // Ninja
    {ICON_MPCHAR_SMALL_BONDTUX, CHAR_BONDTUX_FULLNAME, CHAR_BONDTUX_DESC, 0xc, 0, CHARACTER_LOCKED}, // Bond Tux
    {ICON_MPCHAR_SMALL_DRAKESUIT, CHAR_DRAKESUIT_FULLNAME, CHAR_DRAKESUIT_DESC, 0xd, 0, CHARACTER_LOCKED}, // Drake Suit
    {ICON_MPCHAR_SMALL_BONDSPACESUIT, CHAR_BONDSPACE_FULLNAME, CHAR_BONDSPACE_DESC, 0xe, 0, CHARACTER_LOCKED}, // Bond Spacesuit
    {ICON_MPCHAR_SMALL_GOLDFINGER, CHAR_GOLDFINGER_FULLNAME, CHAR_GOLDFINGER_DESC, 0xf, 0, CHARACTER_LOCKED}, // Goldfinger
    {ICON_MPCHAR_SMALL_RENARD, CHAR_RENARD_FULLNAME, CHAR_RENARD_DESC, 0x10, 0, CHARACTER_LOCKED}, // Renard
    {ICON_MPCHAR_SMALL_SCARAMANGA, CHAR_SCARAMANGA_FULLNAME, CHAR_SCARAMANGA_DESC, 0x11, 0, CHARACTER_LOCKED}, // Scaramanga
    {ICON_MPCHAR_SMALL_PUSSYGALORE, CHAR_GALORE_FULLNAME, CHAR_GALORE_DESC, 0x12, 0, CHARACTER_LOCKED}, // Pussy Galore
    {ICON_MPCHAR_SMALL_CHRISTMASJONES, CHAR_XMASJONES_FULLNAME, CHAR_XMASJONES_DESC, 0x13, 0, CHARACTER_LOCKED}, // Christmas Jones
    {ICON_MPCHAR_SMALL_WAILIN, CHAR_WAILIN_FULLNAME, CHAR_WAILIN_DESC, 0x14, 0, CHARACTER_LOCKED}, // Wai Lin
    {ICON_MPCHAR_SMALL_XENIAONATOPP, CHAR_XENIA_FULLNAME, CHAR_XENIA_DESC, 0x15, 0, CHARACTER_LOCKED}, // Xenia Onatopp
    {ICON_MPCHAR_SMALL_MAYDAY, CHAR_MAYDAY_FULLNAME, CHAR_MAYDAY_DESC, 0x16, 0, CHARACTER_LOCKED}, // May Day
    {ICON_MPCHAR_SMALL_ELEKTRAKING, CHAR_ELEKTRA_FULLNAME, CHAR_ELEKTRA_DESC, 0x17, 0, CHARACTER_LOCKED}, // Elektra King
    {ICON_MPCHAR_SMALL_JAWS, CHAR_JAWS_FULLNAME, CHAR_JAWS_DESC, 0x18, 0, CHARACTER_LOCKED}, // Jaws
    {ICON_MPCHAR_SMALL_BARONSAMEDI, CHAR_SAMEDI_FULLNAME, CHAR_SAMEDI_DESC, 0x19, 0, CHARACTER_LOCKED}, // Baron Samedi
    {ICON_MPCHAR_SMALL_ODDJOB, CHAR_ODDJOB_FULLNAME, CHAR_ODDJOB_DESC, 0x1a, 0, CHARACTER_LOCKED}, // Oddjob
    {ICON_MPCHAR_SMALL_NICKNACK, CHAR_NICKNACK_FULLNAME, CHAR_NICKNACK_DESC, 0x1b, 0, CHARACTER_LOCKED}, // Nick Nack
    {ICON_MPCHAR_SMALL_MAXZORIN, CHAR_ZORIN_FULLNAME, CHAR_ZORIN_DESC, 0x1c, 0, CHARACTER_LOCKED}, // Max Zorin
};

// The multiplayer setup wheel. P_MPOPTIONS disables AI Bots on Ravine.
// XBE_GLOBAL(0x0017ce50, 0x78)
M_ITEM mp_options[5] = {
    {ICON_MPSCENARIO_QUICKGAME, MENU_CONTINUE, MP_START_DESC, 0, 1, TXT_NULL}, // Continue
    {ICON_MPOPTIONS_AIBOTS, MP_AIBOTS, MP_CFG_BOTS_DESC, 1, 1, NO_BOTS_ON_RAVINE}, // AI Bots
    {ICON_MPOPTIONS_RULES, MP_CFG_RULES, MP_GM_RULES_DESC, 2, 1, TXT_NULL}, // Game Rules
    {ICON_MPOPTIONS_PLAYERMODS, MP_CFG_PLAYERS, MP_CFG_PLAYER_DESC, 3, 1, TXT_NULL}, // Player Mods
    {ICON_MPOPTIONS_ENVIROMODS, MP_CFG_ENVIRONMENT, MP_CFG_ENVIRONMENT_DESC, 4, 1, TXT_NULL}, // Enviro-Mods
};

// The bot wheel: Continue, then one item per bot slot; C_SBBOTS sets each icon to the bot's character.
// XBE_GLOBAL(0x0017cfd0, 0x198)
M_ITEM mp_bots[17] = {
    {ICON_MPSCENARIO_QUICKGAME, MENU_CONTINUE, TXT_NULL, 0, 1, TXT_NULL}, // Continue
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_1, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 1
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_2, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 2
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_3, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 3
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_4, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 4
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_5, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 5
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_6, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 6
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_7, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 7
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_8, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 8
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_9, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 9
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_10, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 10
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_11, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 11
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_12, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 12
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_13, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 13
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_14, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 14
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_15, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 15
    {ICON_MPSCENARIO_ARENA, MP_CFG_BOT_16, TXT_NULL, 0, 1, TXT_NULL}, // Setup Bot 16
};

// AUTOINJECT
bool P_MPMAP_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {

    switch((MessageType)message) {
        case MessageType_PageEnter: {
            Menu_StartIris(((param_6 == P_MPSCENARIO) ? 0 : 4), param_1, SUB_C_MP_IRIS);
            M_CONTROL* ctrl = (M_CONTROL*)__Menu_Send(param_1, C_SBMPMAP, 0x39, 0, 0);
            Menu_SelectItemInControl(ctrl, mp_level, ARRAY_SIZE(mp_level), MPSettings.multiplayerLevelHashcode);
            break;
        }
        case 0x50: {
            Menu_PlayIris(1, param_1, SUB_C_MP_IRIS);
            break;
        }
    }
    return true;  
}

extern char menu_unlock_everything;

// AUTOINJECT
bool C_SBMPMAP_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {
  
    MessageType event = (MessageType)message;

    switch(event) {
        case MessageType_Scroll:
        case MessageType_ValueSet: {
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
        case MessageType_PageEnter: {
            Menu_StartIris((param_6 == P_MAIN) ? 0 : 4, managerNum, SUB_C_MPSCENARIO_IRIS);
            Menu_UnlockMPSettings();
            M_CONTROL *pMVar1 = (M_CONTROL *)__Menu_Send(managerNum, C_SBMPSCEN, 0x39, 0, 0);
            Menu_SelectItemInControl(pMVar1, mp_scenario, ARRAY_SIZE(mp_scenario), MPSettings.GameMode); // Start on the previously chosen game mode
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


// AUTOINJECT
bool P_MPPLAYERMODS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {
  
    switch(message) {
        case MessageType_PageEnter: {
            
            RADIO_CLEAR(managerNum, SUB_C_MPPLAYERMODS_FRIENDLYFIRE);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_FRIENDLYFIRE, MP_ON, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_FRIENDLYFIRE, MP_OFF, 0);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPPLAYERMODS_FRIENDLYFIRE, MPSettings.FriendlyFire);
            
            RADIO_CLEAR(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_NORMAL, WEAPSET_NORMAL);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_PISTOLS, WEAPSET_PISTOLS);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_AUTOMATIC, WEAPSET_AUTOMATIC);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_SNIPERS, WEAPSET_SNIPERS);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_EXPLOSIVES, WEAPSET_EXPLOSIVES);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_EXPLOSIVES2, WEAPSET_EXPLOSIVES2);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_MI6, WEAPSET_MI6);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_PHOENIX, WEAPSET_PHOENIX);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_MODERN, WEAPSET_MODERN);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_WEAPSET_STEALTHY, WEAPSET_STEALTHY);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MP_RANDOM, WEAPSET_RANDOM);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET, MPSettings.weaponSet);
            
            RADIO_CLEAR(managerNum, SUB_C_MPPLAYERMODS_PROFESSIONALMODE);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_PROFESSIONALMODE, MP_ON, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_PROFESSIONALMODE, MP_OFF, 0);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPPLAYERMODS_PROFESSIONALMODE, MPSettings.TripleDamageModifierProfessionalMode);
            
            RADIO_CLEAR(managerNum, SUB_C_MPPLAYERMODS_LOCATIONDAMAGE);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_LOCATIONDAMAGE, MP_ON, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_LOCATIONDAMAGE, MP_OFF, 0);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPPLAYERMODS_LOCATIONDAMAGE, MPSettings.LocationDamageEnabled);

            RADIO_CLEAR(managerNum, SUB_C_MPPLAYERMODS_TEAMID);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_TEAMID, MP_ON, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPPLAYERMODS_TEAMID, MP_OFF, 0);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPPLAYERMODS_TEAMID, MPSettings.ShowTeamAndNameOverhead);

            break;
        }
        case MessageType_Select: {

            // Commit the settings
            MPSettings.FriendlyFire = RADIO_GET_VALUE(managerNum, SUB_C_MPPLAYERMODS_FRIENDLYFIRE);
            MPSettings.weaponSet = (WeaponSet) RADIO_GET_VALUE(managerNum, SUB_C_MPPLAYERMODS_WEAPONSET);
            MPSettings.TripleDamageModifierProfessionalMode = RADIO_GET_VALUE(managerNum, SUB_C_MPPLAYERMODS_PROFESSIONALMODE);
            MPSettings.LocationDamageEnabled = RADIO_GET_VALUE(managerNum, SUB_C_MPPLAYERMODS_LOCATIONDAMAGE);
            MPSettings.ShowTeamAndNameOverhead = RADIO_GET_VALUE(managerNum, SUB_C_MPPLAYERMODS_TEAMID);
            
            // Notify manager of a change to settings? / Page change in general?
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0); 
            break;
        }
    }

  return true;
}

// FIXME: this might be part of mp_stuff?
#define enviromods_explosive_scenery_unlocked mp_explosive_scenery_unlocked

// AUTOINJECT
bool P_MPENVIROMODS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {
  
    switch(message) {
        case MessageType_PageEnter: {
            
            RADIO_CLEAR(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE, MP_RESPAWN_NEAR, RESPAWN_NEAR);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE, MP_RESPAWN_FAR, RESPAWN_FAR);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE, MP_RANDOM, RESPAWN_RANDOM);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE, MPSettings.RespawnSelectionMode);
            
            RADIO_CLEAR(managerNum, SUB_C_MPENVIROMODS_GUNEMPLACEMENTS);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_GUNEMPLACEMENTS, MP_ON, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_GUNEMPLACEMENTS, MP_OFF, 0);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPENVIROMODS_GUNEMPLACEMENTS, MPSettings.GunEmplacementsEnabled);
            
            RADIO_CLEAR(managerNum, SUB_C_MPENVIROMODS_EXPLOSIVESCENERY);
            if(enviromods_explosive_scenery_unlocked) {
                RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_EXPLOSIVESCENERY, MP_ON, 1);
                RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_EXPLOSIVESCENERY, MP_OFF, 0);
            } else {
                RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_EXPLOSIVESCENERY, LOCKED, 0x10);
            }
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPENVIROMODS_EXPLOSIVESCENERY, MPSettings.ExplosiveSceneryEnabled);
            
            RADIO_CLEAR(managerNum, SUB_C_MPENVIROMODS_GRAPPLE);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_GRAPPLE, MP_ON, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_GRAPPLE, MP_OFF, 0);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPENVIROMODS_GRAPPLE, MPSettings.GrappleEnabled);

            RADIO_CLEAR(managerNum, SUB_C_MPENVIROMODS_MINIVEHICLES);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_MINIVEHICLES, MP_OFF, 0);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_MINIVEHICLES, MP_RC_TANK, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_MINIVEHICLES, MP_RC_HELI, 2);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_MINIVEHICLES, MP_RANDOM, 3);
            RADIO_SELECT_ITEM(managerNum, SUB_C_MPENVIROMODS_MINIVEHICLES, MPSettings.MiniVehiclesEnabled);

            break;
        }
        case MessageType_Select: {

            // Commit the settings
            MPSettings.RespawnSelectionMode = (RespawnMode) RADIO_GET_VALUE(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE);
            MPSettings.GunEmplacementsEnabled = (WeaponSet) RADIO_GET_VALUE(managerNum, SUB_C_MPENVIROMODS_GUNEMPLACEMENTS);
            MPSettings.ExplosiveSceneryEnabled = RADIO_GET_VALUE(managerNum, SUB_C_MPENVIROMODS_EXPLOSIVESCENERY);
            MPSettings.GrappleEnabled = RADIO_GET_VALUE(managerNum, SUB_C_MPENVIROMODS_GRAPPLE);
            MPSettings.MiniVehiclesEnabled = RADIO_GET_VALUE(managerNum, SUB_C_MPENVIROMODS_MINIVEHICLES);
            
            // Notify manager of a change to settings? / Page change in general?
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0); 
            break;
        }
    }

  return true;
}

// XBE_GLOBAL(0x002456b2, 0x1)
uint8_t mp_editing_bot;  // the bot P_MPBOTCHOOSE / P_MPBOTSETUP are editing
// XBE_GLOBAL(0x0025e960, 0x40)
static char mp_bot_playing_text[0x40]; // "Playing : Yes"

// AUTOINJECT
bool P_MPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        M_CONTROL *wheel = CONTROL_GET(managerNum, C_SBMPOPTIONS);
        if ((HASHCODE)arg2 == P_MPSETUP) {
            Menu_StartIris(0, managerNum, SUB_C_SBMPOPTIONS_IRIS);
            Menu_SelectItemInControl(wheel, mp_options, ARRAY_SIZE(mp_options), 0);
        } else {
            Menu_StartIris(4, managerNum, SUB_C_SBMPOPTIONS_IRIS);
        }
        mp_options[1].enabled = MPSettings.multiplayerLevelHashcode != HT_Level_Ravine;   // no bots on Ravine
    } else if (message == MessageType_PageUpdate) {
        Menu_PlayIris(1, managerNum, SUB_C_SBMPOPTIONS_IRIS);
        Menu_UpdateOptionBox(NULL);
    }
    return true;
}

// AUTOINJECT
bool P_MPBOTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter || message == MessageType_PageResume) {
        Menu_StartIris((HASHCODE)arg2 == P_MPOPTIONS ? 0 : 4, managerNum, SUB_C_SBBOTS_IRIS);
        __Menu_Send(managerNum, C_SBBOTS, MessageType_SetValue, mp_editing_bot + 1, 0);
    } else if (message == MessageType_PageUpdate) {
        Menu_PlayIris(1, managerNum, SUB_C_SBBOTS_IRIS);
    }
    return true;
}

// AUTOINJECT
bool C_SBBOTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet: {
        int item = SCROLL_GET_VALUE(control);
        if (item == 0) {
            mp_bots[0].iconHashcode = ICON_MPSCENARIO_QUICKGAME;
            __Menu_Send(managerNum, SUB_C_SBBOTS_PLAYING_TEXT, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        } else {
            // "Playing : Yes/No" for the bot, and its character's icon on the wheel
            const MPBOT *bot = &mpbots.bot[item - 1];
            sprintf(mp_bot_playing_text, "%s : %s", Txt_BindLabel(MP_BOT_PLAYING, 0), Txt_BindLabel(bot->isPlaying ? TXT_YES : TXT_NO, 0));
            __Menu_Send(managerNum, SUB_C_SBBOTS_PLAYING_TEXT, MessageType_SetState, CONTROL_STATE_INERT, 0);
            __Menu_Send(managerNum, SUB_C_SBBOTS_PLAYING_TEXT, MessageType_SetText, (int)mp_bot_playing_text, 0);
            mp_bots[item].iconHashcode = Menu_GetItemFromHash(mp_characters, (uchar)bot->SkinNum, ARRAY_SIZE(mp_characters))->iconHashcode;
        }
        Menu_UpdateWheel(managerNum, control, mp_bots, SUB_C_SBBOTS_WHEEL_TEXT, SUB_C_SBBOTS_ICON, (HASHCODE)0,
                         SUB_C_SBBOTS_IRIS, message == MessageType_Scroll);
        break;
    }
    case MessageType_Select: {
        int item = SCROLL_GET_VALUE(control);
        if (item == 0) {
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
            break;
        }
        mp_editing_bot = (uchar)(item - 1);
        Menu_ChangePageCloseIris(P_MPBOTCHOOSE, managerNum, SUB_C_SBBOTS_IRIS);
        break;
    }
    case MessageType_ControlCreated:
        __Menu_SendMessage(control, MessageType_SetRange, 0, ARRAY_SIZE(mpbots.bot));
        __Menu_SendMessage(control, MessageType_SetValue, 1, 0);
        break;
    }
    return true;
}

// P_MPBOTCHOOSE's own copy of mp_characters, with the characters this bot cannot have greyed out.
// XBE_GLOBAL(0x00245338, 0x2b8)
static M_ITEM mp_stuff[29];
// Who has the one-per-game characters: the editing bot's index + 10 (0 = nobody). In a game without teams only one
// bot may be on MI6's side, and only one bot may be a Bond.
extern uint8_t mp_good_bot_taken;
extern uint8_t mp_bond_bot_taken;
// XBE_GLOBAL(0x002456ac, 0x4)
static BOT_stats_t * mp_bot_default_stats; // the last stats copied into a bot

static bool IsBondSkin(int skin) {
    return skin == 0 || skin == 0xc || skin == 0xe;
}

// AUTOINJECT
bool P_MPBOTCHOOSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        Menu_UnlockMPSkins(0xff);
        Menu_StartIris((HASHCODE)arg2 == P_MPBOTS ? 0 : 4, managerNum, SUB_C_SBMPBTCHOOSE_IRIS);
        for (int i = 0; i < ARRAY_SIZE(mp_stuff); i++) {
            mp_stuff[i] = mp_characters[i];
            bool goodTaken = mp_good_bot_taken != 0 && mp_editing_bot + 10 != mp_good_bot_taken;
            if ((MPSettings.GameMode & TEAMGAME) == GM_QUICK && goodTaken && Menu_IsBotGood(mp_characters[i].identifier))
                *(uchar *)&mp_stuff[i].enabled = 0;
            if (mp_bond_bot_taken != 0 && mp_editing_bot + 10 != mp_bond_bot_taken && IsBondSkin((int)mp_characters[i].identifier))
                *(uchar *)&mp_stuff[i].enabled = 0;
        }
        __Menu_Send(managerNum, C_SBMPBTCHOOSE, MessageType_SetRange, 0, ARRAY_SIZE(mp_stuff) - 1);
        __Menu_Send(managerNum, C_SBMPBTCHOOSE, MessageType_SetValue, (uchar)mpbots.bot[mp_editing_bot].SkinNum, 0);
    } else if (message == MessageType_PageUpdate) {
        Menu_PlayIris(1, managerNum, SUB_C_SBMPBTCHOOSE_IRIS);
    }
    return true;
}

// The bot takes the character's default stats as the wheel moves.
static void TakeDefaultStats(M_CONTROL *wheel) {
    BOT_stats_t *stats = BOT_getDefaultStats(SCROLL_GET_VALUE(wheel));
    mp_bot_default_stats = stats;
    mpbots.bot[mp_editing_bot].stats = *stats;
}

// AUTOINJECT
bool C_SBMPBTCHOOSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Scroll) {
        mpbots.bot[mp_editing_bot].statsEdited = 0;
        TakeDefaultStats(control);
        Menu_UpdateWheel(managerNum, control, mp_stuff, SUB_C_SBMPBTCHOOSE_WHEEL_TEXT, SUB_C_SBMPBTCHOOSE_ICON,
                         SUB_C_SBMPBTCHOOSE_DESCRIPTION_TEXT, SUB_C_SBMPBTCHOOSE_IRIS, true);
    } else if (message == MessageType_Select) {
        uchar skin = (uchar)SCROLL_GET_VALUE(control);
        uchar bot = mp_editing_bot;
        if (menu_unlock_everything || *(uchar *)&mp_stuff[skin].enabled) {
            mpbots.bot[mp_editing_bot].SkinNum = skin;
            mpbots.bot[bot].isGood = Menu_IsBotGood(skin);
            if ((MPSettings.GameMode & TEAMGAME) == GM_QUICK && Menu_IsBotGood((uchar)mpbots.bot[bot].SkinNum))
                mp_good_bot_taken = mp_editing_bot + 10;
            if (IsBondSkin((uchar)mpbots.bot[mp_editing_bot].SkinNum))
                mp_bond_bot_taken = mp_editing_bot + 10;
            M_ITEM *character = Menu_GetItemFromHash(mp_characters, skin, ARRAY_SIZE(mp_characters));
            if (character != NULL)
                strcpy(MPSettings.Player[NUM_PLAYERS + mp_editing_bot].Name, Txt_BindLabel(character->title, 0));   // the bots follow the players
            mpbots.bot[mp_editing_bot].isPlaying = 1;
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MPBOTSETUP, 0);
        }
    } else if (message == MessageType_ValueSet) {
        if (!mpbots.bot[mp_editing_bot].statsEdited)
            TakeDefaultStats(control);
        Menu_UpdateWheel(managerNum, control, mp_stuff, SUB_C_SBMPBTCHOOSE_WHEEL_TEXT, SUB_C_SBMPBTCHOOSE_ICON,
                         SUB_C_SBMPBTCHOOSE_DESCRIPTION_TEXT, SUB_C_SBMPBTCHOOSE_IRIS, false);
    }
    return true;
}

// The bot setup page's health, reaction and recover choices (reaction and recover stop at 200).
static const struct { const char *text; int value; } bot_stat_values[] = {
    { "50", 50 }, { "75", 75 }, { "100", 100 }, { "125", 125 }, { "150", 150 }, { "175", 175 }, { "200", 200 },
    { "250", 250 }, { "300", 300 },
};

static void AddStatValues(uchar managerNum, HASHCODE radio, int count) {
    RADIO_CLEAR(managerNum, radio);
    for (int i = 0; i < count; i++)
        __Menu_Send(managerNum, radio, MessageType_AddItem, (int)bot_stat_values[i].text, bot_stat_values[i].value);
}

// The editing bot's setup: whether it plays and, for the characters below 15, its stats (the others' are fixed).
// A keeps them and goes back to the bot wheel.
// AUTOINJECT
bool P_MPBOTSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    static const HASHCODE stat_radios[] = { SUB_P_MPBOTSETUP_SPEED_SCROLL, SUB_P_MPBOTSETUP_ACCURACY_SCROLL,
        SUB_P_MPBOTSETUP_AGGRESSION_SCROLL, SUB_P_MPBOTSETUP_HEALTH_SCROLL, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL,
        SUB_P_MPBOTSETUP_REACTION_SCROLL, SUB_P_MPBOTSETUP_RECOVER_SCROLL };

    if (message == MessageType_Select) {
        mpbots.bot[mp_editing_bot].isPlaying = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_PLAYING_SCROLL);
        mpbots.bot[mp_editing_bot].stats.accuracy = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_ACCURACY_SCROLL);
        // aggression is stored 16 bits wide, over the byte after it too
        uint aggression = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_AGGRESSION_SCROLL);
        mpbots.bot[mp_editing_bot].stats.baseAggression = (uchar)aggression;
        mpbots.bot[mp_editing_bot].stats.unused3 = (uchar)(aggression >> 8);
        mpbots.bot[mp_editing_bot].stats.health = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_HEALTH_SCROLL);
        mpbots.bot[mp_editing_bot].stats.speed = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_SPEED_SCROLL);
        mpbots.bot[mp_editing_bot].stats.personality = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL);
        mpbots.bot[mp_editing_bot].stats.reaction = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_REACTION_SCROLL);
        MPBOT *bot = &mpbots.bot[mp_editing_bot];
        bot->stats.recover = RADIO_GET_VALUE(managerNum, SUB_P_MPBOTSETUP_RECOVER_SCROLL);
        bot->statsEdited = 1;
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);

        // give back the one-per-game characters if the bot no longer has one or is not playing
        bot = &mpbots.bot[mp_editing_bot];
        if ((MPSettings.GameMode & TEAMGAME) == GM_QUICK && mp_good_bot_taken == mp_editing_bot + 10)
            if (!Menu_IsBotGood((uchar)bot->SkinNum) || !bot->isPlaying)
                mp_good_bot_taken = 0;
        if (mp_bond_bot_taken == mp_editing_bot + 10)
            if (!IsBondSkin((uchar)bot->SkinNum) || !bot->isPlaying)
                mp_bond_bot_taken = 0;
    } else if (message == MessageType_PageEnter) {
        RADIO_CLEAR(managerNum, SUB_P_MPBOTSETUP_PLAYING_SCROLL);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PLAYING_SCROLL, TXT_YES, 1);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PLAYING_SCROLL, TXT_NO, 0);
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_PLAYING_SCROLL, (uchar)mpbots.bot[mp_editing_bot].isPlaying);

        if ((uchar)mpbots.bot[mp_editing_bot].SkinNum < 15) {
            __Menu_SendEx(managerNum, SUB_P_MPBOTSETUP_PLAYING_SCROLL, 0, MessageType_SetAlwaysHighlighted, 1, 0);
            for (HASHCODE radio : stat_radios) {
                __Menu_SendEx(managerNum, radio, 0, MessageType_SetAlwaysHighlighted, 1, 0);
                __Menu_SendEx(managerNum, radio, 1, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
            }
        } else {
            for (HASHCODE radio : stat_radios)
                __Menu_Send(managerNum, radio, MessageType_SetState, CONTROL_STATE_INERT, 0);
        }

        RADIO_CLEAR(managerNum, SUB_P_MPBOTSETUP_ACCURACY_SCROLL);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_ACCURACY_SCROLL, BOT_ACCURACY_POOR, 8);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_ACCURACY_SCROLL, BOT_ACCURACY_AVERAGE, 5);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_ACCURACY_SCROLL, BOT_ACCURACY_GOOD, 3);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_ACCURACY_SCROLL, BOT_ACCURACY_VERYGOOD, 1);
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_ACCURACY_SCROLL, mpbots.bot[mp_editing_bot].stats.accuracy);

        RADIO_CLEAR(managerNum, SUB_P_MPBOTSETUP_AGGRESSION_SCROLL);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_AGGRESSION_SCROLL, CONTROL_NORMAL, 2);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_AGGRESSION_SCROLL, BOT_AGGRESSION_HIGH, 3);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_AGGRESSION_SCROLL, BOT_AGGRESSION_VERYHIGH, 4);
        const BOT_stats_t *stats = &mpbots.bot[mp_editing_bot].stats;
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_AGGRESSION_SCROLL, stats->baseAggression | stats->unused3 << 8);

        AddStatValues(managerNum, SUB_P_MPBOTSETUP_HEALTH_SCROLL, ARRAY_SIZE(bot_stat_values));
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_HEALTH_SCROLL, mpbots.bot[mp_editing_bot].stats.health);

        RADIO_CLEAR(managerNum, SUB_P_MPBOTSETUP_SPEED_SCROLL);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_SPEED_SCROLL, BOT_SPEED_SLOW, 0);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_SPEED_SCROLL, BOT_SPEED_NORMAL, 1);
        RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_SPEED_SCROLL, BOT_SPEED_FAST, 2);
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_SPEED_SCROLL, mpbots.bot[mp_editing_bot].stats.speed);

        // the personalities of the bot's side
        RADIO_CLEAR(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL);
        if (Menu_IsBotGood((uchar)mpbots.bot[mp_editing_bot].SkinNum)) {
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_NONE, BOT_PERSONALITY_NONE);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_JUDGE, BOT_PERSONALITY_JUDGE);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_COLLECTOR, BOT_PERSONALITY_COLLECTOR);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_GUARDIAN, BOT_PERSONALITY_GUARDIAN);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_TEAMPLAYER, BOT_PERSONALITY_TEAM_PLAYER);
        } else {
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_NONE, BOT_PERSONALITY_NONE);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_BERSERKER, BOT_PERSONALITY_BERSERKER);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_GREEDY, BOT_PERSONALITY_GREEDY);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_VENGEFUL, BOT_PERSONALITY_VENGEFUL);
            RADIO_ADD_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, MP_PERSONALITY_ASSASSIN, BOT_PERSONALITY_ASSASSIN);
        }
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_PERSONALITY_SCROLL, mpbots.bot[mp_editing_bot].stats.personality);

        AddStatValues(managerNum, SUB_P_MPBOTSETUP_REACTION_SCROLL, 7);
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_REACTION_SCROLL, mpbots.bot[mp_editing_bot].stats.reaction);
        AddStatValues(managerNum, SUB_P_MPBOTSETUP_RECOVER_SCROLL, 7);
        RADIO_SELECT_ITEM(managerNum, SUB_P_MPBOTSETUP_RECOVER_SCROLL, mpbots.bot[mp_editing_bot].stats.recover);
    } else if (message == MessageType_PageUpdate && (uchar)mpbots.bot[mp_editing_bot].SkinNum >= 15) {
        __Menu_Send(managerNum, SUB_P_MPBOTSETUP_INFO_TEXT, MessageType_SetText, (int)Txt_BindLabel(BOT_STATS_FIXED, 0), 0);
    }
    return true;
}

// XBE_GLOBAL(0x0025e3d8, 0xff)
static char mp_option_box_text[0xff];
// XBE_GLOBAL(0x0025e4d7, 0x1)
static uint8_t mp_bots_initialised;

// A bot, ready to play as a character, with its default stats and the character's name as its player name.
static void SetUpBot(int bot, uchar skin) {
    MPBOT *b = &mpbots.bot[bot];
    b->isPlaying = 1;
    b->statsEdited = 0;
    b->isGood = 0;
    b->SkinNum = skin;
    BOT_stats_t *stats = BOT_getDefaultStats(skin);
    mp_bot_default_stats = stats;
    b->stats = *stats;
    M_ITEM *character = Menu_GetItemFromHash(mp_characters, skin, ARRAY_SIZE(mp_characters));
    if (character != NULL)
        strcpy(MPSettings.Player[NUM_PLAYERS + bot].Name, Txt_BindLabel(character->title, 0));
}

// The playing bots move to the front, in order, and are counted (none on Ravine). A moved bot's name is copied with it,
// but the bot it swaps places with is not given its own name back (as the original).
// AUTOINJECT
void __stdcall Menu_PrepareBots(void) {
    uchar playing = 0;
    for (uchar i = 0; i < NUM_BOTS; i++) {
        if (!mpbots.bot[i].isPlaying)
            continue;
        if (i != playing) {
            char name[sizeof(MPSettings.Player[0].Name)];
            strcpy(name, MPSettings.Player[NUM_PLAYERS + i].Name);
            MPBOT bot = mpbots.bot[playing];
            mpbots.bot[playing] = mpbots.bot[i];
            mpbots.bot[i] = bot;
            strcpy(MPSettings.Player[NUM_PLAYERS + playing].Name, name);
        }
        playing++;
    }
    mpbots.NumBots = playing;
    mpbots.Enabled = 1;
    MPSettings.numBots = playing;
    if (MPSettings.multiplayerLevelHashcode == HT_Level_Ravine) {
        mpbots.NumBots = 0;
        MPSettings.numBots = 0;
    }
}

// AUTOINJECT
bool C_SBMPSCEN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        Menu_UpdateWheel(managerNum, control, mp_scenario, SUB_C_SBMPSCEN_WHEEL_TEXT, SUB_C_SBMPSCEN_ICON,
                         SUB_C_SBMPSCEN_DESCRIPTION_TEXT, SUB_C_MPSCENARIO_IRIS, message == MessageType_Scroll);
        break;
    case MessageType_Select: {
        uchar item = (uchar)SCROLL_GET_VALUE(control);
        if (!menu_unlock_everything && !mp_scenario[item].enabled)
            break;
        if (item != 0) {
            MPSettings.GameMode = (MultiplayerGameMode)mp_scenario[item].identifier;
            Menu_ChangePageCloseIris(P_MPMAP, managerNum, SUB_C_MPSCENARIO_IRIS);
            break;
        }
        // Quick Game: an arena match on a random map (of the first seven), first to 10 or 10 minutes, the joined
        // players against three bots (Drake, Kiko and Rook) - player 1 as Bond for MI6, the others for Phoenix.
        Menu_PrepareBots();
        MPSettings.GameMode = GM_ARENA;
        MPSettings.MaxPoints = 10;
        MPSettings.MaxDuration = 10;
        MPSettings.field53_0x190 = 0;
        MPSettings.ExplosiveSceneryEnabled = 0;
        MPSettings.maybeIsTeamGame = 0;
        MPSettings.ShowTeamAndNameOverhead = 1;
        MPSettings.FriendlyFire = 0;
        MPSettings.isMultiplayer = 1;
        MPSettings.GrappleEnabled = 0;
        MPSettings.weaponSet = WEAPSET_NORMAL;
        MPSettings.MiniVehiclesEnabled = 2;
        GameState.NextLevelHashcode = (HASHCODE)mp_level[Rand_Random() % 7].identifier;
        MPSettings.multiplayerLevelHashcode = GameState.NextLevelHashcode;
        static const uint quick_skins[4] = { 0, 6, 7, 8 };
        uchar players = 0;
        for (int i = 0; i < NUM_PLAYERS; i++) {
            if (!mp_join_slots[i].joined)
                continue;
            players++;
            mp_join_slots[i].skin = quick_skins[i];
            mp_join_slots[i].team = i == 0 ? MI6 : PHOENIX;
        }
        MPSettings.numPlayers = players;
        mpbots.NumBots = 3;
        MPSettings.numBots = 3;
        static const uchar bot_skins[3] = { 1, 3, 2 };
        for (int b = 0; b < 3; b++)
            SetUpBot(b, bot_skins[b]);
        Menu_ChangePageCloseIris(P_MPCONFIRM, managerNum, SUB_C_MPSCENARIO_IRIS);
        break;
    }
    case MessageType_ControlCreated:
        __Menu_SendMessage(control, MessageType_SetRange, 0, ARRAY_SIZE(mp_scenario) - 1);
        break;
    }
    return true;
}

// AUTOINJECT
bool C_SBMPOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        Menu_UpdateWheel(managerNum, control, mp_options, SUB_C_SBMPOPTIONS_WHEEL_TEXT, SUB_C_SBMPOPTIONS_ICON,
                         SUB_C_SBMPOPTIONS_DESCRIPTION_TEXT, SUB_C_SBMPOPTIONS_IRIS, message == MessageType_Scroll);
        break;
    case MessageType_Select: {
        uchar item = (uchar)SCROLL_GET_VALUE(control);
        if (!mp_options[item].enabled)
            break;
        switch (item) {
        case 0: {
            // Continue: count each side - players by the team they joined, bots by their character - and refuse a
            // match that could not be played.
            Menu_PrepareBots();
            uchar phoenix = 0, mi6 = 0;
            for (int i = 0; i < NUM_PLAYERS; i++)
                if (mp_join_slots[i].joined) {
                    if (mp_join_slots[i].team == PHOENIX)
                        phoenix++;
                    else
                        mi6++;
                }
            MPSettings.numPlayers = mi6 + phoenix;
            for (int b = 0; b < (uchar)mpbots.NumBots; b++) {
                if (mpbots.bot[b].isGood)
                    mi6++;
                else
                    phoenix++;
            }
            bool teams = (MPSettings.GameMode & TEAMGAME) != GM_QUICK;
            if (!teams && mi6 > 1) {
                Menu_CreateOptionBox(managerNum, Txt_BindLabel(MP_GOOD_GUYS_WRONG_TEAM, 0), 0, 1, 0);
                break;
            }
            if (teams && (phoenix == 0 || mi6 == 0)) {
                sprintf(mp_option_box_text, Txt_BindLabel(MP_NO_PLAYERS_ON_TEAM, 0),
                        Txt_BindLabel(phoenix == 0 ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0));
                Menu_CreateOptionBox(managerNum, mp_option_box_text, 8, 1, 0);
                break;
            }
            if (MPSettings.numBots + MPSettings.numPlayers < 2) {
                strcpy(mp_option_box_text, Txt_BindLabel(MP_NOT_ENOUGH_PLAYERS, 0));
                Menu_CreateOptionBox(managerNum, mp_option_box_text, 7, 1, 0);
                break;
            }
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MPCONFIRM, 0);
            break;
        }
        case 1: Menu_ChangePageCloseIris(P_MPBOTS, managerNum, SUB_C_SBMPOPTIONS_IRIS); break;
        case 2: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MPRULES, 0); break;
        case 3: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MPPLAYERMODS, 0); break;
        case 4: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MPENVIROMODS, 0); break;
        }
        break;
    }
    case MessageType_ControlCreated:
        // the first time: the bots as the characters from 6 on
        if (!mp_bots_initialised) {
            for (int b = 0; b < NUM_BOTS; b++)
                mpbots.bot[b].SkinNum = (char)(6 + b);
            mp_bots_initialised = 1;
        }
        __Menu_SendMessage(control, MessageType_SetRange, 0, ARRAY_SIZE(mp_options) - 1);
        break;
    }
    return true;
}

// Each agent's box on the join page (P_MPSETUP) is a radio, C_RBMPSETUP with the agent's number as its id, that
// steps through these; B steps back.
typedef enum {
    JOIN_OUT = 0,               // the join page: not joined
    JOIN_CHOOSE_CODENAME = 1,   // the join page
    JOIN_CHOOSE_TEAM = 2,       // team games only
    JOIN_CHOOSE_CHARACTER = 3,
    JOIN_CHOOSE_HANDICAP = 4,
    JOIN_READY = 5,
} JoinStep;
// XBE_GLOBAL(0x00245698, 0x10)
int mp_join_step[NUM_PLAYERS];

// Fills an agent's character radio on the join page with the characters it may have: unlocked, and - in a game
// without teams - only a good one if no other agent has one (taken = true: never), or in a team game one of its
// team's side, and a Bond only if no other agent is one. Selects the first; returns what that returns.
// AUTOINJECT
ulonglong Menu_GetMPSkins(int managerNum, byte agent, char taken) {
    M_CONTROL *radio = (M_CONTROL *)__Menu_SendEx((byte)managerNum, C_RBMPSETUP, agent, MessageType_GetControl, 0, 0);
    __Menu_SendMessage(radio, MessageType_ClearItems, 0, 0);
    Menu_UnlockMPSkins(agent);
    for (int i = 0; i < ARRAY_SIZE(mp_characters_small); i++) {
        if (!menu_unlock_everything && !ITEM_ENABLED(mp_characters[i]))
            continue;
        uint skin = mp_characters_small[i].identifier;
        bool good = BOT_getDefaultStats(skin)->isBad == 0;
        if ((MPSettings.GameMode & TEAMGAME) == GM_QUICK) {
            if (good) {
                if (taken)
                    continue;
                if (mp_good_bot_taken != 0 && mp_good_bot_taken != agent + 1)
                    continue;
            }
        } else {
            if (good ? mp_join_slots[agent].team != MI6 : mp_join_slots[agent].team != PHOENIX)
                continue;
            if (IsBondSkin((int)mp_characters_small[i].identifier) && mp_bond_bot_taken != 0 && mp_bond_bot_taken != agent + 1)
                continue;
        }
        __Menu_SendMessage(radio, MessageType_AddItem, (int)Txt_BindLabel(mp_characters_small[i].title, 0), mp_characters_small[i].identifier);
    }
    return (uint)__Menu_SendMessage(radio, MessageType_SelectIndex, 0, 0);
}
// Every joined player has finished setting up, and one has joined at least. Each slot takes its number as its
// controller's port on the way.
// AUTOINJECT
bool __stdcall Menu_AllJoinedPlayersReady(void) {
    uchar ready = 1;    // 2 all so far, 4 one is not
    for (uchar i = 0; i < NUM_PLAYERS; i++) {
        MPJoinSlot *slot = &mp_join_slots[i];
        slot->controllerPort = i;
        if (!slot->joined)
            continue;
        if (!slot->ready)
            ready = 4;
        else if (ready != 4)
            ready = 2;
    }
    return ready == 2;
}

// The start button's fade, once everyone is ready
// AUTOINJECT
void Menu_MPFadeWhenReady(M_CONTROL *control) {
    M_MESSAGE fade = {};
    fade.type = MESSAGE_PROCESS;
    fade.arg1 = 0x60606080;
    fade.arg2 = 0;
    Process_Create(control, &fade, 30, 0);
}

static const struct { const char *text; int value; } handicaps[] = {
    { "-75", -75 }, { "-50", -50 }, { "-25", -25 }, { "0", 0 }, { "+25", 25 }, { "+50", 50 }, { "+75", 75 }, { "+100", 100 },
};

static void ShowTeamIcon(uchar managerNum, M_CONTROL *radio, uint agent) {
    int team = RADIO_GET_VALUE_OF(radio);
    __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetIcon, team == MI6 ? ICON_MP_TEAM_MI6 : ICON_MP_TEAM_PHOENIX, 0);
}

// The other agents still choosing a character get their lists again, with the one-per-game characters taken
// (taken = true) or given back.
static void RefreshCharacterChoices(uchar managerNum, uint except, bool taken) {
    for (uint i = 0; i < NUM_PLAYERS; i++) {
        if (i == except || mp_join_step[i] != JOIN_CHOOSE_CHARACTER)
            continue;
        int current = __Menu_SendEx(managerNum, C_RBMPSETUP, i, MessageType_GetSelectedItemValue, 0, 0);
        if (taken) {
            if (mp_good_bot_taken != 0)
                Menu_GetMPSkins(managerNum, (byte)i, 1);
            if (mp_bond_bot_taken != 0)
                Menu_GetMPSkins(managerNum, (byte)i, 1);
        } else {
            Menu_GetMPSkins(managerNum, (byte)i, 0);
        }
        __Menu_SendEx(managerNum, C_RBMPSETUP, i, MessageType_SelectItemByValue, current, 0);
    }
}

// AUTOINJECT
bool C_RBMPSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    uint agent = (uchar)control->id;
    MPJoinSlot *slot = &mp_join_slots[agent];
    bool teams = (MPSettings.GameMode & TEAMGAME) != GM_QUICK;
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        if (mp_join_step[agent] == JOIN_CHOOSE_TEAM) {
            ShowTeamIcon(managerNum, control, agent);
        } else if (mp_join_step[agent] == JOIN_CHOOSE_CHARACTER) {
            M_ITEM *character = Menu_GetItemFromHash(mp_characters_small, RADIO_GET_VALUE_OF(control), ARRAY_SIZE(mp_characters_small));
            if (character != NULL)
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetIcon, character->iconHashcode, 0);
        }
        break;

    case MessageType_Select:
        if (mp_join_step[agent] == JOIN_CHOOSE_TEAM) {
            mp_join_step[agent] = JOIN_CHOOSE_CHARACTER;
            if (!teams)
                Menu_IsBotGood(slot->skin);   // as the original: called, and the answer not used
            else
                slot->team = (MPTeam)RADIO_GET_VALUE_OF(control);
            Menu_GetMPSkins(managerNum, (byte)agent, 0);
            __Menu_SendMessage(control, MessageType_SelectItemByValue, slot->skin, 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetText, (int)Txt_BindLabel(CFG_CHOOSE_CHARACTER, 0), 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetState, CONTROL_STATE_INERT, 0);
        } else if (mp_join_step[agent] == JOIN_CHOOSE_CHARACTER) {
            mp_join_step[agent] = JOIN_CHOOSE_HANDICAP;
            slot->skin = RADIO_GET_VALUE_OF(control);
            slot->team = (MPTeam)Menu_IsBotGood(slot->skin);
            if (!teams && Menu_IsBotGood(slot->skin))
                mp_good_bot_taken = (uchar)(agent + 1);
            if (IsBondSkin((int)slot->skin))
                mp_bond_bot_taken = (uchar)(agent + 1);
            __Menu_SendMessage(control, MessageType_ClearItems, 0, 0);
            for (int i = 0; i < ARRAY_SIZE(handicaps); i++)
                __Menu_SendMessage(control, MessageType_AddItem, (int)handicaps[i].text, handicaps[i].value);
            __Menu_SendMessage(control, MessageType_SelectItemByValue, MPSettings.Player[agent].HealthModifier, 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetText, (int)Txt_BindLabel(CFG_HEALTH_HANDICAP, 0), 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetIcon, ICON_MP_HANDICAP, 0);
            RefreshCharacterChoices(managerNum, agent, true);
        } else if (mp_join_step[agent] == JOIN_CHOOSE_HANDICAP) {
            MPSettings.Player[agent].HealthModifier = RADIO_GET_VALUE_OF(control);
            mp_join_step[agent] = JOIN_READY;
            Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPFINISH, agent);
            M_CONTROL *finish = (M_CONTROL *)__Menu_SendEx(managerNum, C_RBMPFINISH, agent, MessageType_GetControl, 0, 0);
            __Menu_SendMessage(finish, MessageType_SetText, (int)Txt_BindLabel(CFG_PLAYER_READY, 0), 0);
            slot->ready = 1;
            if ((uchar)Menu_AllJoinedPlayersReady())
                __Menu_SendMessage((M_CONTROL *)&manager[managerNum], MessageType_GoPage, P_MPOPTIONS, 0);
        }
        break;

    case MessageType_PlayerFocusGained:
        __Menu_SendEx(managerNum, C_RBMPFINISH, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_SendMessage(control, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        break;

    case MessageType_QueryBack:
        // B goes back a step (or, from the first, off the page)
        if (mp_join_step[agent] == JOIN_CHOOSE_CHARACTER) {
            if (!teams)
                break;
            __Menu_SendMessage(control, MessageType_ClearItems, 0, 0);
            __Menu_SendMessage(control, MessageType_AddItem, (int)Txt_BindLabel(MP_TEAM_PHOENIX, 0), PHOENIX);
            __Menu_SendMessage(control, MessageType_AddItem, (int)Txt_BindLabel(MP_TEAM_MI6, 0), MI6);
            __Menu_SendMessage(control, MessageType_SelectItemByValue, slot->team, 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetText, (int)Txt_BindLabel(CFG_CHOOSE_TEAM, 0), 0);
            ShowTeamIcon(managerNum, control, agent);
            mp_join_step[agent] = JOIN_CHOOSE_TEAM;
            *(int *)arg2 = -2;
        } else if (mp_join_step[agent] == JOIN_CHOOSE_HANDICAP) {
            mp_join_step[agent] = JOIN_CHOOSE_CHARACTER;
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetText, (int)Txt_BindLabel(CFG_CHOOSE_CHARACTER, 0), 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetState, CONTROL_STATE_INERT, 0);
            Menu_GetMPSkins(managerNum, (byte)agent, 0);
            __Menu_SendMessage(control, MessageType_SelectItemByValue, slot->skin, 0);
            // give back the one-per-game character this agent had taken (the Bond one only in a team game, as
            // the original)
            if (!teams && mp_good_bot_taken == agent + 1) {
                mp_good_bot_taken = 0;
                RefreshCharacterChoices(managerNum, NUM_PLAYERS, false);
            } else if (teams && mp_bond_bot_taken == agent + 1) {
                mp_bond_bot_taken = 0;
                RefreshCharacterChoices(managerNum, NUM_PLAYERS, false);
            }
            *(int *)arg2 = -2;
        }
        break;
    }
    return true;
}

// ---- the join page (P_MPJOIN), then each agent's setup (P_MPSETUP)

extern ulong menu_bonus[8];   // MenuUnlocks.cpp

// A missing controller's message, the player's number in it (names ours): for one that had joined, one that had not
#define MP_CONTROLLER_MISSING_JOINED ((Action_TranslatedText)0x10002d5)
#define MP_CONTROLLER_MISSING        ((Action_TranslatedText)0x10002a2)
// The bots' short names, one per mp_characters identifier from the first
#define MP_BOT_SHORT_NAME_FIRST 0x10002e2
#define MP_BOT_SHORT_NAMES      29
// XBE_GLOBAL(0x0015d3c0, 0x4) readonly relocate
char BotString[] = "Bot";

// The parts of a codename's save a player loads; part 8 too when only one player has joined
#define LS_MASK_ALONE  0x2d
#define LS_MASK_SHARED 0x25

// The colours the join page fades from and to
#define FADE_BLACK 0xff
#define FADE_CLEAR 0

// XBE_GLOBAL(0x0022507c, 0x4)
uint8_t mp_controller_missing[NUM_PLAYERS];         // the last update found the player's controller missing
// XBE_GLOBAL(0x00225078, 0x4)
uint8_t mp_controller_missing_joined[NUM_PLAYERS];  // ... and the player had joined; never read
// XBE_GLOBAL(0x00224f78, 0x100)
char mp_controller_text[0x100];

// A player's boxes as the controller comes and goes: without it, the message and every box but the first two
// hidden (on the join page, the player leaves); once it is back, the join prompt again on the join page, else the
// setup's boxes shown again.
// AUTOINJECT
void Menu_UpdateMPControllers(uchar managerNum, uchar player, char joinPage) {
    if (PlayerInputs[player].controllerIsActive) {
        if (mp_controller_missing[player]) {
            if (joinPage) {
                mp_join_step[player] = JOIN_OUT;
                Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPSTART, player);
                __Menu_SendEx(managerNum, C_RBMPSTART, player, MessageType_SetText,
                              (int)Txt_BindLabel(PRESS_A_TO_JOIN, 0), 0);
                __Menu_SendEx(managerNum, C_RBMPFINISH, player, MessageType_SetText,
                              (int)Txt_BindLabel(PRESS_A_TO_JOIN, 0), 0);
            } else {
                __Menu_SendEx(managerNum, C_RBMPFINISH, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
                __Menu_SendEx(managerNum, C_RBMPSETUP, player, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
            }
        }
        mp_controller_missing[player] = 0;
        mp_controller_missing_joined[player] = 0;
        return;
    }
    sprintf(mp_controller_text,
            Txt_BindLabel(mp_join_slots[player].joined ? MP_CONTROLLER_MISSING_JOINED : MP_CONTROLLER_MISSING, 0),
            player + 1);
    __Menu_SendEx(managerNum, C_RBMPSTART, player, MessageType_SetText, (int)mp_controller_text, 0);
    __Menu_SendEx(managerNum, C_RBMPFINISH, player, MessageType_SetText, (int)mp_controller_text, 0);
    __Menu_SendEx(managerNum, C_RBMPSTART, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
    __Menu_SendEx(managerNum, C_RBMPFINISH, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
    __Menu_SendEx(managerNum, C_RBMPSETUP, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    __Menu_SendEx(managerNum, C_RBMPCNAME, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    if (joinPage) {
        mp_join_slots[player].joined = 0;
        mp_join_slots[player].ready = 0;
    }
    mp_controller_missing[player] = 1;
    mp_controller_missing_joined[player] = mp_join_step[player] != JOIN_OUT;
}

// A character's short name, as the bots are called ("Bot" past the last)
// AUTOINJECT
char* Menu_GetBotShortName(BotNum skin) {
    if (skin >= MP_BOT_SHORT_NAMES)
        return BotString;
    return (char *)Txt_BindLabel((Action_TranslatedText)(MP_BOT_SHORT_NAME_FIRST + skin), 0);
}

// The codenames to be listed again
static void RelistCodenames(uchar managerNum) {
    ls.busy = 1;
    ls.slot = LS_SLOT_NEW;
    ls.returnPage = (HASHCODE)0;
    ls.returnPageArg = 0;
    ls.operation = LS_OPERATION_ENUM;
    ls.done = 0;
    ls.control = NULL;
    ls.field17_0x42 = 0;
    Menu_UpdateMessageBox(managerNum, 0, 0);
}

static void FadeJoinPage(uchar managerNum, uint colour) {
    M_MESSAGE fade = {};
    fade.type = MESSAGE_PROCESS;
    fade.arg1 = colour;
    fade.arg2 = 0;
    Process_Create(CONTROL_GET(managerNum, SUB_P_MPJOIN_FADE), &fade, 15, 0);
}

// Each player presses A to join (C_RBMPSTART), picks a codename (C_RBMPCNAME), whose settings are loaded, and says
// it is ready; once everyone joined is, the scenarios. Meanwhile the codenames are listed and each player's loaded,
// ls.mpPlayer saying whose; ls.returnPageArg is used as a flag: the codenames lists are to be filled again once the
// listing is over.
// AUTOINJECT
bool P_MPJOIN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
    case MessageType_PageEnter:
        ls.returnPageArg = 0;
        for (int i = 0; i < 2 * NUM_PLAYERS; i++)
            menu_bonus[i] = 0;
        ls.operation = LS_OPERATION_ENUM;
        if (arg2 == P_MAIN) {
            LABEL_SET_COLOUR(managerNum, SUB_P_MPJOIN_FADE, FADE_BLACK);
            __Menu_SendMessage(&manager[managerNum].control, MessageType_LockInput, 1, 0);
            __Menu_SendDelayedMessage(15, &manager[managerNum].control, MessageType_LockInput, 0, 0);
            FadeJoinPage(managerNum, FADE_CLEAR);
        } else {
            LABEL_SET_COLOUR(managerNum, SUB_P_MPJOIN_FADE, FADE_CLEAR);
        }
        if (!ls.listStale)
            ls.done = 1;
        else
            RelistCodenames(managerNum);
        if (ls.operation != LS_OPERATION_LOAD) {
            memset(mp_join_slots, 0, sizeof(mp_join_slots));
            for (int i = 0; i < NUM_PLAYERS; i++)
                mp_join_slots[i].skin = i;
            ls.mpPlayer = 0;
            for (int i = 0; i < NUM_PLAYERS; i++) {
                mp_join_step[i] = JOIN_OUT;
                mp_join_slots[i].joined = 0;
                Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPSTART, i);
            }
            for (int i = 0; i < NUM_PLAYERS; i++)
                PlayerInputs[i].controllerPort = (char)i;
            __Menu_Send(managerNum, SUB_C_RBMPSETUP_TITLE, MessageType_SetText,
                        (int)Txt_BindLabel(MP_PROMPT_START_GAME, 0), 0);
        }
        mp_join_slots[0].team = MI6;
        mp_join_slots[2].team = MI6;
        mp_join_slots[1].team = PHOENIX;
        mp_join_slots[3].team = PHOENIX;
        return true;

    case MessageType_PageUpdate: {
        uchar loading = ls.mpPlayer - 1;
        int joined = mp_join_slots[2].joined + mp_join_slots[3].joined + mp_join_slots[1].joined +
                     mp_join_slots[0].joined;
        Menu_UpdateMessageBox(managerNum, joined == 1 ? LS_MASK_ALONE : LS_MASK_SHARED, loading);
        if (ls.done && Menu_AllJoinedPlayersReady())
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MPSCENARIO, 0);
        if (!ls.busy && ls.done && ls.returnPageArg) {
            for (uint i = 0; i < NUM_PLAYERS; i++) {
                M_CONTROL *codenames =
                    (M_CONTROL *)__Menu_SendEx(managerNum, C_RBMPCNAME, i, MessageType_GetControl, 0, 0);
                __Menu_SendMessage(codenames, MessageType_ClearItems, 0, 0);
                Menu_PutCodenamesIntoControl(codenames);
            }
            ls.returnPageArg = 0;
        }
        for (uchar i = 0; i < NUM_PLAYERS; i++)
            Menu_UpdateMPControllers(managerNum, i, 1);
        break;
    }

    case MessageType_SaveFlowDone: {
        // A listing over (arg2 8), or a player's codename loaded: that player is ready
        uchar player = ls.mpPlayer - 1;
        if (ls.operation != LS_OPERATION_LOAD)
            break;
        if (arg2 == 8) {
            if (!ls.listStale)
                ls.done = 1;
            else
                RelistCodenames(managerNum);
            ls.returnPageArg = 1;
            return true;
        }
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, C_RBMPSETUP, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        mp_join_step[player] = JOIN_READY;
        ls.operation = LS_OPERATION_NONE;
        if (mp_join_step[ls.mpPlayer - 1] == JOIN_READY)
            Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPSTART, ls.mpPlayer - 1);
        break;
    }

    case MessageType_QueryBack:
        // Back to the main menu after the fade, the page kept meanwhile
        __Menu_SendMessage(&manager[managerNum].control, MessageType_LockInput, 1, 0);
        __Menu_SendDelayedMessage(19, &manager[managerNum].control, MessageType_Back, 1, 0);
        __Menu_SendDelayedMessage(19, &manager[managerNum].control, MessageType_LockInput, 0, 0);
        FadeJoinPage(managerNum, FADE_BLACK);
        *(int *)arg2 = -2;
        return true;
    }
    return true;
}

// The colour of a player's box while it has the player's focus
#define MP_BOX_COLOUR 0xfa6e5aff
#define ICON_MP_CODENAME ((HASHCODE)0x030001a0)

// A player's first box on the join page: A joins (a codename is chosen next) or, once the codename is loaded, says
// the player is ready. B on a ready player goes back to the codenames.
// AUTOINJECT
bool C_RBMPSTART_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    uchar player = (uchar)control->id;
    MPJoinSlot *slot = &mp_join_slots[player];
    switch (message) {
    case MessageType_PlayerFocusGained:
        Process_Destroy(control);
        __Menu_SendMessage(control, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        __Menu_SendMessage(control, MessageType_SetColour, MP_BOX_COLOUR, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, C_RBMPCNAME, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        if (mp_join_step[player] == JOIN_OUT) {
            slot->joined = 0;
            __Menu_SendMessage(control, MessageType_SetText, (int)Txt_BindLabel(PRESS_A_TO_JOIN, 0), player);
        } else if (mp_join_step[player] == JOIN_READY) {
            __Menu_SendMessage(control, MessageType_SetText, (int)Txt_BindLabel(PRESS_A_WHEN_READY, 0), player);
        }
        break;

    case MessageType_PlayerFocusLost:
        slot->ready = 0;
        break;

    case MessageType_Select:
        if (mp_join_step[player] == JOIN_OUT) {
            slot->joined = 1;
            slot->ready = 0;
            mp_join_step[player] = JOIN_CHOOSE_CODENAME;
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, player, MessageType_SetText,
                          (int)Txt_BindLabel(CFG_CHOOSE_CODENAME, 0), 0);
            Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPCNAME, player);
            M_CONTROL *codenames =
                (M_CONTROL *)__Menu_SendEx(managerNum, C_RBMPCNAME, player, MessageType_GetControl, 0, 0);
            __Menu_SendMessage(codenames, MessageType_ClearItems, 0, 0);
            Menu_PutCodenamesIntoControl(codenames);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, player, MessageType_SetIcon, ICON_MP_CODENAME, 0);
            __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, player, MessageType_SetIconRect, 0, 0x7f003f);
        } else if (mp_join_step[player] == JOIN_READY) {
            __Menu_SendMessage(control, MessageType_SetText, (int)Txt_BindLabel(CFG_PLAYER_READY, 0), 0);
            Menu_MPFadeWhenReady(control);
            slot->ready = 1;
        }
        break;

    case MessageType_QueryBack:
        if (mp_join_step[player] != JOIN_READY)
            break;
        Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPCNAME, player);
        mp_join_step[player] = JOIN_CHOOSE_CODENAME;
        __Menu_SendMessage(control, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_SendEx(managerNum, C_RBMPCNAME, player, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        *(int *)arg2 = -2;
        break;
    }
    return true;
}

// A player's codenames on the join page: a default one is applied at once, a saved one loaded (P_MPJOIN hears when
// it is). The player is named after the codename, or "Player <n>" for a default.
// AUTOINJECT
bool C_RBMPCNAME_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    uchar player = (uchar)control->id;
    switch (message) {
    case MessageType_QueryBack:
        if (mp_join_step[player] != JOIN_CHOOSE_CODENAME)
            break;
        mp_join_step[player] = JOIN_OUT;
        Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPSTART, player);
        *(int *)arg2 = -2;
        break;

    case MessageType_PlayerFocusGained:
        __Menu_SendEx(managerNum, C_RBMPSTART, player, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, player, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_SendMessage(control, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        break;

    case MessageType_Select: {
        if (mp_join_step[player] != JOIN_CHOOSE_CODENAME)
            break;
        uchar joined = mp_join_slots[3].joined + mp_join_slots[2].joined + mp_join_slots[1].joined +
                       mp_join_slots[0].joined;
        ushort mask = joined == 1 ? LS_MASK_ALONE : LS_MASK_SHARED;
        ls.mpPlayer = player + 1;
        int slot = __Menu_SendMessage(control, MessageType_GetSelectedIndex, 0, 0) + 1;
        if (slot < CODENAME_SLOTS_DEFAULT) {
            byte loading = ls.mpPlayer - 1;
            ls.busy = 0;
            ls.done = 1;
            ls.slot = slot;
            Menu_MapDefaultCodename(loading, (byte)slot, mask);
            mp_join_step[loading] = JOIN_READY;
            Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPSTART, loading);
        } else {
            char *codename = (char *)__Menu_SendMessage(control, MessageType_GetText, 0, 0);
            _Menu_StartLS(codename, LS_OPERATION_LOAD, 0, managerNum, ls.mpPlayer - 1, mask, slot, (HASHCODE)0, 0,
                          NULL, 0);
        }
        if (ls.slot >= CODENAME_SLOTS_DEFAULT)
            strcpy(MPSettings.Player[player].Name, ls.codename);
        else
            sprintf(MPSettings.Player[player].Name, "%s %d", Txt_BindLabel(PLAYER, 0), player + 1);
        break;
    }
    }
    return true;
}

// The agents' setup: each joined agent's radio (C_RBMPSETUP) starts at the team (team games) or the character. The
// players' claims on the one-per-game characters are dropped, the bots' (10 and up) kept.
// AUTOINJECT
bool P_MPSETUP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        if (mp_good_bot_taken < 10)
            mp_good_bot_taken = 0;
        if (mp_bond_bot_taken < 10)
            mp_bond_bot_taken = 0;
        for (int i = 0; i < NUM_PLAYERS; i++)
            PlayerInputs[i].controllerPort = (char)i;
        for (uint agent = 0; agent < NUM_PLAYERS; agent++) {
            MPJoinSlot *slot = &mp_join_slots[agent];
            slot->ready = 0;
            M_CONTROL *radio = (M_CONTROL *)__Menu_SendEx(managerNum, C_RBMPSETUP, agent, MessageType_GetControl, 0, 0);
            if ((MPSettings.GameMode & TEAMGAME) != GM_QUICK) {
                mp_join_step[agent] = JOIN_CHOOSE_TEAM;
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetText,
                              (int)Txt_BindLabel(CFG_CHOOSE_TEAM, 0), 0);
                __Menu_SendMessage(radio, MessageType_ClearItems, 0, 0);
                __Menu_SendMessage(radio, MessageType_AddItem, (int)Txt_BindLabel(MP_TEAM_PHOENIX, 0), PHOENIX);
                __Menu_SendMessage(radio, MessageType_AddItem, (int)Txt_BindLabel(MP_TEAM_MI6, 0), MI6);
                __Menu_SendMessage(radio, MessageType_SelectItemByValue, slot->team, 0);
            } else {
                mp_join_step[agent] = JOIN_CHOOSE_CHARACTER;
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetText,
                              (int)Txt_BindLabel(CFG_CHOOSE_CHARACTER, 0), 0);
                if ((MPSettings.GameMode & TEAMGAME) == GM_QUICK)
                    slot->team = MI6;
                else
                    slot->team = (MPTeam)RADIO_GET_VALUE_OF(radio);
                Menu_GetMPSkins(managerNum, (byte)agent, 0);
                __Menu_SendMessage(radio, MessageType_SelectItemByValue, slot->skin, 0);
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetText,
                              (int)Txt_BindLabel(CFG_CHOOSE_CHARACTER, 0), 0);
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetState, CONTROL_STATE_INERT, 0);
            }
            if (!slot->joined) {
                __Menu_SendEx(managerNum, C_RBMPFINISH, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
                __Menu_SendEx(managerNum, C_RBMPSETUP, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
                __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            } else {
                Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPSETUP, agent);
            }
        }
    } else if (message == MessageType_PageUpdate) {
        for (uint agent = 0; agent < NUM_PLAYERS; agent++)
            if (mp_join_slots[agent].joined)
                Menu_UpdateMPControllers(managerNum, (uchar)agent, 0);
    }
    return true;
}

// A ready agent's box on the setup page. B goes back to the handicap, whose choices go to the title's control
// rather than the radio.
// AUTOINJECT
bool C_RBMPFINISH_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    uchar agent = (uchar)control->id;
    switch (message) {
    case MessageType_PlayerFocusGained:
        Process_Destroy(control);
        __Menu_SendMessage(control, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        __Menu_SendMessage(control, MessageType_SetColour, MP_BOX_COLOUR, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, C_RBMPSETUP, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        if (mp_join_step[agent] == JOIN_READY)
            __Menu_SendMessage(control, MessageType_SetText, (int)Txt_BindLabel(PRESS_A_WHEN_READY, 0), agent);
        break;

    case MessageType_PlayerFocusLost:
        mp_join_slots[agent].ready = 0;
        break;

    case MessageType_QueryBack: {
        if (mp_join_step[agent] != JOIN_READY)
            break;
        Manager_SendMessage(&manager[managerNum], MessageType_SetPlayerFocus, C_RBMPSETUP, agent);
        mp_join_step[agent] = JOIN_CHOOSE_HANDICAP;
        M_CONTROL *title =
            (M_CONTROL *)__Menu_SendEx(managerNum, SUB_C_RBMPSETUP_TITLE, agent, MessageType_GetControl, 0, 0);
        __Menu_SendMessage(title, MessageType_ClearItems, 0, 0);
        for (int i = 0; i < ARRAY_SIZE(handicaps); i++)
            __Menu_SendMessage(title, MessageType_AddItem, (int)handicaps[i].text, handicaps[i].value);
        __Menu_SendMessage(title, MessageType_SelectItemByValue, MPSettings.Player[agent].HealthModifier, 0);
        __Menu_SendEx(managerNum, SUB_C_RBMPSETUP_ICON, agent, MessageType_SetState, CONTROL_STATE_INERT, 0);
        *(int *)arg2 = -2;
        break;
    }
    }
    return true;
}

// The match's length in minutes (or unlimited, -1) and the score to win, or in Top Agent the lives
static const struct { const char *text; int value; } mp_rule_durations[] = {
    {"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6}, {"7", 7}, {"8", 8}, {"9", 9}, {"10", 10},
    {"15", 15}, {"20", 20}, {"25", 25}, {"30", 30}, {"35", 35}, {"40", 40}, {"45", 45}, {"50", 50}, {"55", 55},
    {"60", 60},
};
static const struct { const char *text; int value; } mp_rule_points[] = {
    {"1", 1}, {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5}, {"6", 6}, {"7", 7}, {"8", 8}, {"9", 9}, {"10", 10},
    {"15", 15}, {"20", 20}, {"25", 25}, {"30", 30}, {"35", 35}, {"40", 40}, {"45", 45}, {"50", 50}, {"55", 55},
    {"60", 60}, {"65", 65}, {"70", 70}, {"75", 75}, {"80", 80}, {"85", 85}, {"90", 90}, {"95", 95}, {"100", 100},
    {"105", 105}, {"110", 110}, {"115", 115}, {"120", 120},
};

// AUTOINJECT
bool P_MPRULES_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_PageEnter) {
        __Menu_Send(managerNum, SUB_P_MPRULES_DURATION, MessageType_ClearItems, 0, 0);
        for (int i = 0; i < ARRAY_SIZE(mp_rule_durations); i++)
            __Menu_Send(managerNum, SUB_P_MPRULES_DURATION, MessageType_AddItem, (int)mp_rule_durations[i].text,
                        mp_rule_durations[i].value);
        __Menu_Send(managerNum, SUB_P_MPRULES_DURATION, MessageType_AddItem, (int)Txt_BindLabel(CFG_UNLIMITED, 0), -1);
        __Menu_Send(managerNum, SUB_P_MPRULES_DURATION, MessageType_SelectItemByValue, MPSettings.MaxDuration, 0);
        __Menu_Send(managerNum, SUB_P_MPRULES_POINTS, MessageType_ClearItems, 0, 0);
        for (int i = 0; i < ARRAY_SIZE(mp_rule_points); i++)
            __Menu_Send(managerNum, SUB_P_MPRULES_POINTS, MessageType_AddItem, (int)mp_rule_points[i].text,
                        mp_rule_points[i].value);
        __Menu_Send(managerNum, SUB_P_MPRULES_POINTS, MessageType_SelectItemByValue, MPSettings.MaxPoints, 0);
        Action_TranslatedText title = MPSettings.GameMode == GM_TOPAGENT ? MP_RULES_LIVES : MP_RULES_POINTS;
        __Menu_Send(managerNum, SUB_P_MPRULES_POINTS, MessageType_SetText, (int)Txt_BindLabel(title, 0), -1);
        __Menu_Send(managerNum, SUB_P_MPRULES_POINTS, MessageType_SelectItemByValue, MPSettings.MaxPoints, 0);
    } else if (message == MessageType_Select) {
        MPSettings.MaxDuration = __Menu_Send(managerNum, SUB_P_MPRULES_DURATION, MessageType_GetSelectedItemValue, 0, 0);
        MPSettings.MaxPoints = __Menu_Send(managerNum, SUB_P_MPRULES_POINTS, MessageType_GetSelectedItemValue, 0, 0);
        Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
    }
    return true;
}


// XBE_GLOBAL(0x0025ed20, 0x1)
static uint8_t mp_debrief_text_count; // the next of mp_debrief_text's strings
// XBE_GLOBAL(0x0025eaa0, 0x300)
char mp_debrief_text[24][32];  // the numbers on the debriefing
// XBE_GLOBAL(0x0025e9a0, 0x100)
static char mp_debrief_result[0x100]; // "<name> wins the match."

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

static_assert(offsetof(MPSettings_t, GameMode) == 0x204, "Offset of MPSettings.GameMode wrong");
static_assert(offsetof(MPSettings_t, multiplayerLevelHashcode) == 0x208, "Offset of multiplayerLevelHashcode wrong");

// A participant's score in the match's mode: victories in Arena and Team Arena, points in the scored modes, none in
// the rest
// AUTOINJECT
int Menu_GetMPScore(byte participant) {
    int points = Ftol(MPGame.players[participant].points);
    int victories = (int)MPGame.players[participant].victories;
    switch (MPSettings.GameMode) {
        case GM_ARENA:
        case GM_TEAMARENA:
            return victories;
        case GM_TOPAGENT:
        case GM_ASSASSIN:
        case GM_CTF:
        case GM_DEMOLITION:
        case GM_PROTECTION:
        case GM_BLUEPRINT:
        case GM_GOLDENEYE:
        case GM_KOTH:
        case GM_UPLINK:
        case GM_TEAMKOTH:
            return points;
        default:
            return 0;
    }
}

static const char *DebriefNumber(int value) {
    char *text = mp_debrief_text[mp_debrief_text_count];
    sprintf(text, "%d", value);
    return text;
}

// The debriefing after a match: the players and bots by score, four rows, and who won.
// AUTOINJECT
bool P_MPDEBRIEFING_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message != MessageType_PageEnter)
        return true;

    // As the original lays them out: the four place labels, then {score, participant} per player and bot - the
    // labels are read by place index, and a fifth place or later reads on into the scores.
    int table[4 + 2 * NUM_AGENTS] = { PLACE_1ST, PLACE_2ND, PLACE_3RD, PLACE_4TH };
    int *place = &table[0];
    int *entry = &table[4];   // entry[2k] score, entry[2k + 1] participant (players, then bots)

    mp_debrief_text_count = 0;
    Menu_RestartFrontEndLoop();
    uchar count = 0;
    for (int p = 0; p < MPSettings.numPlayers; p++, count++) {
        entry[count * 2 + 1] = p;
        entry[count * 2] = Menu_GetMPScore((byte)p);
    }
    for (int p = NUM_PLAYERS; p < MPSettings.numBots + NUM_PLAYERS; p++, count++) {
        entry[count * 2 + 1] = p;
        entry[count * 2] = Menu_GetMPScore((byte)p);
    }
    // highest score first
    bool swapped;
    do {
        swapped = false;
        for (int i = 0; i < MPSettings.numPlayersAndBots - 1; i++)
            if (entry[i * 2] < entry[i * 2 + 2]) {
                int score = entry[i * 2], who = entry[i * 2 + 1];
                entry[i * 2] = entry[i * 2 + 2];
                entry[i * 2 + 1] = entry[i * 2 + 3];
                entry[i * 2 + 2] = score;
                entry[i * 2 + 3] = who;
                swapped = true;
            }
    } while (swapped && MPSettings.numPlayersAndBots - 1 > 0);

    // places: equal scores share one
    int shown = 0;
    for (int i = 0; i < MPSettings.numPlayersAndBots; i++) {
        if (i > 0 && entry[i * 2] != entry[i * 2 - 2])
            shown = i;
        __Menu_SendEx(managerNum, SUB_P_MPDEBRIEFING_PLACE, i, MessageType_SetText, (int)Txt_BindLabel((Action_TranslatedText)place[shown], 0), 0);
    }

    static const HASHCODE row_controls[] = { SUB_P_MPDEBRIEFING_PLACE, SUB_P_MPDEBRIEFING_ICON, SUB_P_MPDEBRIEFING_NAME,
        SUB_P_MPDEBRIEFING_POINTS, SUB_P_MPDEBRIEFING_VICTORIES, SUB_P_MPDEBRIEFING_DEATHS, SUB_P_MPDEBRIEFING_ROW_15B,
        SUB_P_MPDEBRIEFING_TOTAL };
    for (int row = 0; row < 4; row++) {
        if (row >= MPSettings.numPlayersAndBots) {
            __Menu_SendEx(managerNum, C_MPDBG, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            for (HASHCODE c : row_controls)
                __Menu_SendEx(managerNum, c, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            continue;
        }
        __Menu_SendEx(managerNum, C_MPDBG, row, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        for (HASHCODE c : row_controls)
            __Menu_SendEx(managerNum, c, row, MessageType_SetState, CONTROL_STATE_INERT, 0);
        int who = entry[row * 2 + 1];
        M_ITEM *character = Menu_GetItemFromHash(mp_characters_small, MPSettings.Player[who].SkinNum, ARRAY_SIZE(mp_characters_small));
        if (character != NULL)
            __Menu_SendEx(managerNum, SUB_P_MPDEBRIEFING_ICON, row, MessageType_SetIcon, character->iconHashcode, 0);
        const char *name = who < NUM_PLAYERS ? MPSettings.Player[who].Name : Menu_GetBotShortName((BotNum)MPSettings.Player[who].SkinNum);
        __Menu_SendEx(managerNum, SUB_P_MPDEBRIEFING_NAME, row, MessageType_SetText, (int)name, 0);
        __Menu_SendEx(managerNum, SUB_P_MPDEBRIEFING_POINTS, row, MessageType_SetText, (int)DebriefNumber((int)MPGame.players[who].points), 0);
        mp_debrief_text_count++;
        __Menu_SendEx(managerNum, SUB_P_MPDEBRIEFING_VICTORIES, row, MessageType_SetText, (int)DebriefNumber((int)MPGame.players[who].victories), 0);
        mp_debrief_text_count++;
        __Menu_SendEx(managerNum, SUB_P_MPDEBRIEFING_DEATHS, row, MessageType_SetText, (int)DebriefNumber((int)MPGame.players[who].deaths), 0);
        mp_debrief_text_count++;
        __Menu_SendEx(managerNum, SUB_P_MPDEBRIEFING_TOTAL, row, MessageType_SetText, (int)DebriefNumber((int)Menu_GetMPScore((byte)entry[row * 2 + 1])), 0);
        mp_debrief_text_count++;
    }

    __Menu_Send(managerNum, SUB_P_MPDEBRIEFING_RESULT, MessageType_MemoSetOption282, 1, 0);
    const char *result;
    if ((MPSettings.GameMode & TEAMGAME) != GM_QUICK) {
        float phoenix = MPGame.teamScore[PHOENIX], mi6 = MPGame.teamScore[MI6];
        if (phoenix == mi6) {
            result = Txt_BindLabel(MP_MATCH_DRAW, 0);
        } else {
            sprintf(mp_debrief_result, Txt_BindLabel(MP_X_WINS_THE_MATCH, 0), Txt_BindLabel(!(mi6 > phoenix) ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0));
            result = mp_debrief_result;
        }
    } else {
        if (entry[0] == entry[2])
            sprintf(mp_debrief_result, Txt_BindLabel(MP_MATCH_DRAW, 0));   // as the original: the label is the format
        else
            sprintf(mp_debrief_result, Txt_BindLabel(MP_X_WINS_THE_MATCH, 0), MPSettings.Player[entry[1]].Name);
        result = mp_debrief_result;
    }
    __Menu_Send(managerNum, SUB_P_MPDEBRIEFING_RESULT, MessageType_SetText, (int)result, 0);
    return true;
}

// The confirm page's counts of rows used: MI6 (or everyone, without teams) and Phoenix, players and bots
// XBE_GLOBAL(0x0025e95b, 0x1)
static uint8_t mp_confirm_mi6_players;
// XBE_GLOBAL(0x0025e95a, 0x1)
static uint8_t mp_confirm_phoenix_players;
// XBE_GLOBAL(0x0025e959, 0x1)
static uint8_t mp_confirm_mi6_bots;
// XBE_GLOBAL(0x0025e958, 0x1)
static uint8_t mp_confirm_phoenix_bots;
// XBE_GLOBAL(0x0025e8d8, 0x80)
char mp_confirm_handicap_text[NUM_PLAYERS][32];
// XBE_GLOBAL(0x0025e4d8, 0x40)
static char mp_confirm_map_text[0x40];
// XBE_GLOBAL(0x0025e518, 0x40)
static char mp_confirm_scenario_text[0x40];
// XBE_GLOBAL(0x0025e558, 0x40)
static char mp_confirm_weapons_text[0x40];
// XBE_GLOBAL(0x0025e598, 0x40)
static char mp_confirm_points_text[0x40];
// XBE_GLOBAL(0x0025e5d8, 0x40)
static char mp_confirm_duration_text[0x40];
// XBE_GLOBAL(0x0025e618, 0x40)
static char mp_confirm_friendly_text[0x40];
#define MP_UNLIMITED ((int)-1)     // MaxPoints / MaxDuration

// The settings and controls a match was started with (P_MPCONFIRM), for C_MPDBG to go back to.
// XBE_GLOBAL(0x002245f8, 0x23c)
MPSettings_t mp_settings_backup;
// XBE_GLOBAL(0x00224838, 0x560)
PlayerInput mp_inputs_backup[NUM_PLAYERS];

// AUTOINJECT
void __stdcall Menu_StoreMPSettings(void) {
    mp_settings_backup = MPSettings;
    memcpy(mp_inputs_backup, PlayerInputs, sizeof(mp_inputs_backup));
}

// Each player keeps the controller style it has now (its low byte).
// AUTOINJECT
void __stdcall Menu_RestoreMPSettings(void) {
    MPSettings = mp_settings_backup;
    for (int i = 0; i < NUM_PLAYERS; i++) {
        uchar style = (uchar)PlayerInputs[i].controlStyle;
        PlayerInputs[i] = mp_inputs_backup[i];
        PlayerInputs[i].controlStyle = style;
    }
}

// The debriefing's player rows: Select goes back to the scenario page with the match's settings; AltSelect1 plays
// the same match again
// AUTOINJECT
bool C_MPDBG_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select) {
        Menu_RestoreMPSettings();
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MPSCENARIO, 1);
    } else if (message == MessageType_AltSelect1) {
        ResetMap_LevelToLoad((HASHCODE)MPSettings.multiplayerLevelHashcode, false, false);
        GameFlow_PushState(7, 80.0f, 0xff);
        MenuManager_Delete(managerNum);
    }
    return true;
}

// "<label> : <value>", the value looked up first, as the original does (a TXT_NULL value takes a heap string).
static void ConfirmLine(char *out, Action_TranslatedText label, Action_TranslatedText value) {
    const char *v = Txt_BindLabel(value, 0);
    sprintf(out, "%s : %s", Txt_BindLabel(label, 0), v);
}

static Action_TranslatedText WeaponSetName(WeaponSet set) {
    switch (set) {
    case WEAPSET_NORMAL:      return MP_WEAPSET_NORMAL;
    case WEAPSET_PISTOLS:     return MP_WEAPSET_PISTOLS;
    case WEAPSET_AUTOMATIC:   return MP_WEAPSET_AUTOMATIC;
    case WEAPSET_SNIPERS:     return MP_WEAPSET_SNIPERS;
    case WEAPSET_EXPLOSIVES:  return MP_WEAPSET_EXPLOSIVES;
    case WEAPSET_EXPLOSIVES2: return MP_WEAPSET_EXPLOSIVES2;
    case WEAPSET_MI6:         return MP_WEAPSET_MI6;
    case WEAPSET_PHOENIX:     return MP_WEAPSET_PHOENIX;
    case WEAPSET_MODERN:      return MP_WEAPSET_MODERN;
    case WEAPSET_STEALTHY:    return MP_WEAPSET_STEALTHY;
    case WEAPSET_RANDOM:      return MP_RANDOM;
    default:                  return TXT_NULL;
    }
}

// The last page before a multiplayer match: who is playing, on which side, and the rules. A starts it.
// AUTOINJECT
bool P_MPCONFIRM_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    if (message == MessageType_Select) {
        // The joined controllers become players 0, 1... in order: each one's controls setup and name move down to
        // its player number, and its join slot is copied beside it. (As the original, a moved controller's port
        // byte takes the old value of the slot it moves into, and player 3's the last one moved.)
        Menu_StoreMPSettings();
        MPJoinSlot slots[NUM_PLAYERS];
        memset(slots, 0, sizeof(slots));
        for (int k = 0; k < NUM_PLAYERS; k++)
            slots[k].controllerPort = (uchar)k;
        uchar players = 0;
        if (mp_join_slots[0].joined) {
            slots[0] = mp_join_slots[0];
            slots[0].controllerPort = 0;
            players = 1;
        }
        uchar port3 = 0;
        for (int k = 1; k < NUM_PLAYERS; k++) {
            if (!mp_join_slots[k].joined) {
                if (k == 3)
                    port3 = slots[3].controllerPort;
                continue;
            }
            memcpy(&PlayerInputs[players], &PlayerInputs[k], sizeof(PlayerInput));
            memcpy(&MPSettings.Player[players], &MPSettings.Player[k], sizeof(MPSettings_PerPlayer));
            uchar port = slots[players].controllerPort;
            slots[players] = mp_join_slots[k];
            if (k < 3)
                slots[k].controllerPort = port;
            else
                port3 = port;
            players++;
        }
        for (int p = 0; p < 3; p++) {
            PlayerInputs[p].controllerPort = slots[p].controllerPort;
            MPSettings.Player[p].SkinNum = slots[p].skin;
            MPSettings.Player[p].TeamId = slots[p].team;
        }
        MPSettings.Player[3].TeamId = slots[3].team;
        PlayerInputs[3].controllerPort = port3;
        MPSettings.Player[3].SkinNum = slots[3].skin;
        if ((int)MPSettings.MaxDuration != MP_UNLIMITED)
            MPSettings.MaxDuration *= 60;   // minutes to seconds
        MPSettings.ExplosiveSceneryEnabled &= 1;
        MPSettings.numPlayers = players;
        GameState.NextLevelHashcode = (HASHCODE)MPSettings.multiplayerLevelHashcode;
        GameState.difficultyModifier = 1;
        MPSettings.isMultiplayer = 1;
        ResetMap_LevelToLoad((HASHCODE)MPSettings.multiplayerLevelHashcode, false, false);
        GameFlow_PushState(7, 80.0f, 0xff);
        SFXFadeDown(1);
        MenuManager_Delete(managerNum);
        return true;
    }
    if (message != MessageType_PageEnter)
        return true;

    mp_confirm_phoenix_bots = 0;
    mp_confirm_mi6_bots = 0;
    mp_confirm_phoenix_players = 0;
    mp_confirm_mi6_players = 0;
    for (uint row = 0; row < NUM_PLAYERS; row++) {
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_MI6_NAME, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_PHOENIX_NAME, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_MI6_ICON, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_PHOENIX_ICON, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_MI6_HANDICAP, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_PHOENIX_HANDICAP, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    }
    bool teams = (MPSettings.GameMode & TEAMGAME) != GM_QUICK;
    // column headings: "MI6" and (shown) "Phoenix" from the menu data, or one column of everyone "Playing"
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_HEADING_1, MessageType_SetText, (int)Txt_BindLabel(teams ? MP_TEAM_MI6 : MP_BOT_PLAYING, 0), 0);
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_HEADING_2, MessageType_SetState, teams ? CONTROL_STATE_INERT : CONTROL_STATE_HIDDEN, 0);

    for (int i = 0; i < NUM_PLAYERS; i++) {
        const MPJoinSlot *slot = &mp_join_slots[i];
        if (!slot->joined)
            continue;
        HASHCODE name, icon, handicap;
        uchar row;
        if (slot->team == MI6 || !teams) {
            name = SUB_P_MPCONFIRM_MI6_NAME; icon = SUB_P_MPCONFIRM_MI6_ICON; handicap = SUB_P_MPCONFIRM_MI6_HANDICAP;
            row = mp_confirm_mi6_players++;
        } else {
            name = SUB_P_MPCONFIRM_PHOENIX_NAME; icon = SUB_P_MPCONFIRM_PHOENIX_ICON; handicap = SUB_P_MPCONFIRM_PHOENIX_HANDICAP;
            row = mp_confirm_phoenix_players++;
        }
        __Menu_SendEx(managerNum, name, row, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_SendEx(managerNum, name, row, MessageType_SetText, (int)MPSettings.Player[i].Name, 0);
        __Menu_SendEx(managerNum, handicap, row, MessageType_SetState, CONTROL_STATE_INERT, 0);
        int h = MPSettings.Player[i].HealthModifier;
        sprintf(mp_confirm_handicap_text[i], "%s%d", h >= 0 ? "+" : "", h);
        __Menu_SendEx(managerNum, handicap, row, MessageType_SetText, (int)mp_confirm_handicap_text[i], 0);
        if (Menu_GetItemFromHash(mp_characters_small, slot->skin, ARRAY_SIZE(mp_characters_small)) != NULL) {
            __Menu_SendEx(managerNum, icon, row, MessageType_SetState, CONTROL_STATE_INERT, 0);
            __Menu_SendEx(managerNum, icon, row, MessageType_SetIcon, mp_characters_small[slot->skin].iconHashcode, 0);
        }
    }

    M_ITEM *map = Menu_GetItemFromHash(mp_level, MPSettings.multiplayerLevelHashcode, ARRAY_SIZE(mp_level));
    ConfirmLine(mp_confirm_map_text, CFG_MAP, map != NULL ? map->title : TXT_NULL);
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_MAP, MessageType_SetText, (int)mp_confirm_map_text, 0);
    M_ITEM *scenario = Menu_GetItemFromHash(mp_scenario, MPSettings.GameMode, ARRAY_SIZE(mp_scenario));
    ConfirmLine(mp_confirm_scenario_text, CFG_SCENARIO, scenario != NULL ? scenario->title : TXT_NULL);
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_SCENARIO, MessageType_SetText, (int)mp_confirm_scenario_text, 0);
    ConfirmLine(mp_confirm_weapons_text, CFG_WEAPON_SET, WeaponSetName(MPSettings.weaponSet));
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_WEAPONS, MessageType_SetText, (int)mp_confirm_weapons_text, 0);

    // Points (lives for Top Agent, minutes held for King of the Hill)
    if ((int)MPSettings.MaxPoints == MP_UNLIMITED) {
        if (MPSettings.GameMode == GM_TOPAGENT)
            ConfirmLine(mp_confirm_duration_text, CFG_LIVES, CFG_UNLIMITED);   // as the original: into the next line's buffer
        else if (MPSettings.GameMode == GM_KOTH || MPSettings.GameMode == GM_TEAMKOTH)
            ConfirmLine(mp_confirm_points_text, CFG_MINUTES, CFG_UNLIMITED);
        else
            ConfirmLine(mp_confirm_points_text, CFG_POINTS, CFG_UNLIMITED);
    } else {
        sprintf(mp_confirm_points_text, "%s : %d", Txt_BindLabel(MPSettings.GameMode == GM_TOPAGENT ? CFG_LIVES : CFG_POINTS, 0),
                (int)MPSettings.MaxPoints);
    }
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_POINTS, MessageType_SetText, (int)mp_confirm_points_text, 0);
    if ((int)MPSettings.MaxDuration == MP_UNLIMITED)
        ConfirmLine(mp_confirm_duration_text, CFG_DURATION, CFG_UNLIMITED);
    else
        sprintf(mp_confirm_duration_text, "%s : %d", Txt_BindLabel(CFG_DURATION, 0), (int)MPSettings.MaxDuration);
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_DURATION, MessageType_SetText, (int)mp_confirm_duration_text, 0);
    ConfirmLine(mp_confirm_friendly_text, CFG_FRIENDLY_FIRE, MPSettings.FriendlyFire ? MP_ON : MP_OFF);
    __Menu_Send(managerNum, SUB_P_MPCONFIRM_FRIENDLY_FIRE, MessageType_SetText, (int)mp_confirm_friendly_text, 0);

    for (uint row = 0; row < 10; row++) {
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_MI6_BOT, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_SendEx(managerNum, SUB_P_MPCONFIRM_PHOENIX_BOT, row, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    }
    for (uint b = 0; b < (uchar)mpbots.NumBots; b++) {
        const MPBOT *bot = &mpbots.bot[b];
        HASHCODE icon = mp_characters_small[(uchar)bot->SkinNum].iconHashcode;
        HASHCODE column = (HASHCODE)0;   // (a bot neither good nor bad: row 0 of nothing, as the original)
        uint row = 0;
        if (!teams || bot->isGood == 1) {
            row = mp_confirm_mi6_bots++;
            column = SUB_P_MPCONFIRM_MI6_BOT;
        } else if (bot->isGood == 0) {
            row = mp_confirm_phoenix_bots++;
            column = SUB_P_MPCONFIRM_PHOENIX_BOT;
        }
        __Menu_SendEx(managerNum, column, row, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_SendEx(managerNum, column, row, MessageType_SetIcon, icon, 0);
    }
    return true;
}
