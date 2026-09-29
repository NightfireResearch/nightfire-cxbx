#include "ui.h"

#include "Manager.h"
#include "Menu.h"
#include "../game/mp/multiplayer.h"
#include "../game/drone/BOT.h"
#include "../engine/Text.h"
#include "../game.h"
#include "../util/Random.h"

#include "../assets.h"

#include <stdio.h>
#include <string.h>

const M_ITEM mp_level_shipped[8] = {
    {ICON_MPMAP_SKYRAIL,        MPMAP_SKYRAIL_NAME,        MPMAP_SKYRAIL_DESC,        HT_Level_SkyRail,       true, TXT_NULL},
    {ICON_MPMAP_FORTKNOX,       MPMAP_FORTKNOX_NAME,       MPMAP_FORTKNOX_DESC,       HT_Level_FortKnox,      true, TXT_NULL},
    {ICON_MPMAP_SNOWBLIND,      MPMAP_SNOWBLIND_NAME,      MPMAP_SNOWBLIND_DESC,      HT_Level_SnowBlind,     true, TXT_NULL},
    {ICON_MPMAP_PHOENIXBASE,    MPMAP_PHOENIXBASE_NAME,    MPMAP_PHOENIXBASE_DESC,    HT_Level_StealthShip,   true, TXT_NULL},
    {ICON_MPMAP_ATLANTIS,       MPMAP_ATLANTIS_NAME,       MPMAP_ATLANTIS_DESC,       HT_Level_Atlantis,      true, TXT_NULL},
    {ICON_MPMAP_SILO,           MPMAP_SILO_NAME,           MPMAP_SILO_DESC,           HT_Level_MissileSilo,   true, TXT_NULL},
    {ICON_MPMAP_SUBPEN,         MPMAP_SUBPEN_NAME,         MPMAP_SUBPEN_DESC,         HT_Level_SubPen,        true, TXT_NULL},
    {ICON_MPMAP_RAVINE,         MPMAP_RAVINE_NAME,         MPMAP_RAVINE_DESC,         HT_Level_Ravine,        true, TXT_NULL}
};

const M_ITEM mp_scenario_shipped[13] = {
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
const M_ITEM mp_characters_shipped[29] = {
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
const M_ITEM mp_characters_small_shipped[29] = {
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
const M_ITEM mp_options_shipped[5] = {
    {ICON_MPSCENARIO_QUICKGAME, MENU_CONTINUE, MP_START_DESC, 0, 1, TXT_NULL}, // Continue
    {ICON_MPOPTIONS_AIBOTS, MP_AIBOTS, MP_CFG_BOTS_DESC, 1, 1, NO_BOTS_ON_RAVINE}, // AI Bots
    {ICON_MPOPTIONS_RULES, MP_CFG_RULES, MP_GM_RULES_DESC, 2, 1, TXT_NULL}, // Game Rules
    {ICON_MPOPTIONS_PLAYERMODS, MP_CFG_PLAYERS, MP_CFG_PLAYER_DESC, 3, 1, TXT_NULL}, // Player Mods
    {ICON_MPOPTIONS_ENVIROMODS, MP_CFG_ENVIRONMENT, MP_CFG_ENVIRONMENT_DESC, 4, 1, TXT_NULL}, // Enviro-Mods
};

// The bot wheel: Continue, then one item per bot slot; C_SBBOTS sets each icon to the bot's character.
const M_ITEM mp_bots_shipped[17] = {
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

#define menu_unlock_everything U8_AT(0x0025d79e)

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
#define enviromods_explosive_scenery_unlocked U8_AT(0x002456a8)

// AUTOINJECT
bool P_MPENVIROMODS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint message, int param_5, int param_6) {
  
    switch(message) {
        case MessageType_PageEnter: {
            
            RADIO_CLEAR(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE, MP_RESPAWN_NEAR, 0);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE, MP_RESPAWN_FAR, 1);
            RADIO_ADD_ITEM(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE, MP_RANDOM, 2);
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
            MPSettings.RespawnSelectionMode = RADIO_GET_VALUE(managerNum, SUB_C_MPENVIROMODS_RESPAWNMODE);
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

#define mp_editing_bot U8_AT(0x002456b2)                // the bot P_MPBOTCHOOSE / P_MPBOTSETUP are editing
#define mp_bot_playing_text ((char *)0x0025e960)        // "Playing : Yes"

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
#define mp_stuff (*(M_ITEM(*)[29])0x00245338)
// Who has the one-per-game characters: the editing bot's index + 10 (0 = nobody). In a game without teams only one
// bot may be on MI6's side, and only one bot may be a Bond.
#define mp_good_bot_taken U8_AT(0x002456b0)
#define mp_bond_bot_taken U8_AT(0x002456b1)
#define mp_bot_default_stats (*(BOT_stats_t **)0x002456ac)   // the last stats copied into a bot

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
    memcpy(mpbots.bot[mp_editing_bot].stats, stats, sizeof(BOT_stats_t));
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
                strcpy(MPSettings.Player[mp_editing_bot + 4].Name, Txt_BindLabel(character->title, 0));   // the bots follow the 4 players
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

#define mp_option_box_text ((char *)0x0025e3d8)
#define mp_bots_initialised U8_AT(0x0025e4d7)

// A bot, ready to play as a character, with its default stats and the character's name as its player name.
static void SetUpBot(int bot, uchar skin) {
    MPBOT *b = &mpbots.bot[bot];
    b->isPlaying = 1;
    b->statsEdited = 0;
    b->isGood = 0;
    b->SkinNum = skin;
    BOT_stats_t *stats = BOT_getDefaultStats(skin);
    mp_bot_default_stats = stats;
    memcpy(b->stats, stats, sizeof(BOT_stats_t));
    M_ITEM *character = Menu_GetItemFromHash(mp_characters, skin, ARRAY_SIZE(mp_characters));
    if (character != NULL)
        strcpy(MPSettings.Player[4 + bot].Name, Txt_BindLabel(character->title, 0));
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
        for (int i = 0; i < 4; i++) {
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
            for (int i = 0; i < 4; i++)
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
                Menu_CreateOptionBox(managerNum, (int **)Txt_BindLabel(MP_GOOD_GUYS_WRONG_TEAM, 0), 0, 1, 0);
                break;
            }
            if (teams && (phoenix == 0 || mi6 == 0)) {
                sprintf(mp_option_box_text, Txt_BindLabel(MP_NO_PLAYERS_ON_TEAM, 0),
                        Txt_BindLabel(phoenix == 0 ? MP_TEAM_PHOENIX : MP_TEAM_MI6, 0));
                Menu_CreateOptionBox(managerNum, (int **)mp_option_box_text, 8, 1, 0);
                break;
            }
            if (MPSettings.numBots + MPSettings.numPlayers < 2) {
                strcpy(mp_option_box_text, Txt_BindLabel(MP_NOT_ENOUGH_PLAYERS, 0));
                Menu_CreateOptionBox(managerNum, (int **)mp_option_box_text, 7, 1, 0);
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
        // the first time: the six bots as the next six characters
        if (!mp_bots_initialised) {
            for (int b = 0; b < 6; b++)
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
    JOIN_CHOOSE_TEAM = 2,       // team games only
    JOIN_CHOOSE_CHARACTER = 3,
    JOIN_CHOOSE_HANDICAP = 4,
    JOIN_READY = 5,
} JoinStep;
#define mp_join_step (*(int(*)[4])0x00245698)

// AUTOGEN
ulonglong Menu_GetMPSkins(int managerNum, byte agent, char param_3);
// AUTOGEN
undefined4 __stdcall Menu_AllJoinedPlayersReady(void);

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
    for (uint i = 0; i < 4; i++) {
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
                RefreshCharacterChoices(managerNum, 4, false);
            } else if (teams && mp_bond_bot_taken == agent + 1) {
                mp_bond_bot_taken = 0;
                RefreshCharacterChoices(managerNum, 4, false);
            }
            *(int *)arg2 = -2;
        }
        break;
    }
    return true;
}
