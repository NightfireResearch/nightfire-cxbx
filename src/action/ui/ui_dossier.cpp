#include "ui.h"
#include "Manager.h"
#include "Menu.h"
#include "../engine/Text.h"
#include "../game/sp/PlayerStats.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    WEAPON_PISTOL = 0x00,
    GADGET_GRAPPLE = 0x03,
    GADGET_CAMERA = 0x06,
    WEAPON_SNIPER = 0x09,
    GADGET_DARTGUN = 0x0C,
    GADGET_DECODER = 0x0F,
    GADGET_TASER = 0x12,
    GADGET_LASER = 0x15,
    NO_UPGRADES = 0x3F,
} UpgradeabilityType;

// This array is not constant - it is modified for the custom branded shaver, and potentially for upgraded gadgets too?
// XBE_GLOBAL(0x0017d3f0, 0x150)
M_ITEM ds_gadgets[14] = {
    {
        .iconHashcode = HT_Texture_IconDsGadgetTaser,
        .title = GADGET_TASER_NAME,
        .description = GADGET_TASER_DESC,
        .identifier = GADGET_TASER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDossierGadgets,
        .title = GADGET_LASER_NAME,
        .description = GADGET_LASER_DESC,
        .identifier = GADGET_LASER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetGrapple,
        .title = GADGET_GRAPPLE_NAME,
        .description = GADGET_GRAPPLE_DESC,   
        .identifier = GADGET_GRAPPLE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetCamera,
        .title = GADGET_CAMERA_NAME,
        .description = GADGET_CAMERA_DESC,   
        .identifier = GADGET_CAMERA,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetDecoder,
        .title = GADGET_DECODER_NAME,
        .description = GADGET_DECODER_DESC,   
        .identifier = GADGET_DECODER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetQworm,
        .title = GADGET_QWORM_NAME,
        .description = GADGET_QWORM_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetShaverBranded, // default value never seen, this is always overwritten as soon as the page is started
        .title = GADGET_SHAVER_NAME,
        .description = GADGET_SHAVER_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetSentry,
        .title = GADGET_SENTRY_NAME,
        .description = GADGET_SENTRY_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetDartgun,
        .title = GADGET_DARTGUN_NAME,
        .description = GADGET_DARTGUN_DESC,   
        .identifier = GADGET_DARTGUN,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetNightvision,
        .title = GADGET_NIGHTVISION_NAME,
        .description = GADGET_NIGHTVISION_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetSmokescreen,
        .title = GADGET_SMOKESCREEN_NAME,
        .description = GADGET_SMOKESCREEN_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetTurbo,
        .title = GADGET_TURBO_NAME,
        .description = GADGET_TURBO_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetQwedge,
        .title = GADGET_QWEDGE_NAME,
        .description = GADGET_QWEDGE_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_IconDsGadgetEmp,
        .title = GADGET_EMP_NAME,
        .description = GADGET_EMP_DESC,   
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    }
};

// XBE_GLOBAL(0x0017cf70, 0x60) - PS2 EU 0x002e0af8
const M_ITEM ds_options[4] = {
    {
        .iconHashcode = HT_Texture_IconDossierRecords,
        .title = DOSSIER_RECORDS_NAME,
        .description = DOSSIER_RECORDS_DESC,
        .identifier = 0,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDossierRewards,
        .title = DOSSIER_REWARDS_NAME,
        .description = DOSSIER_REWARDS_DESC,
        .identifier = 1,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDossierGadgets,
        .title = DOSSIER_GADGETS_NAME,
        .description = DOSSIER_GADGETS_DESC,
        .identifier = 2,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDossierWeapons,
        .title = MP_CFG_OR_DOSSIER_WEAPONS,
        .description = DOSSIER_WEAPONS_DESC,
        .identifier = 3,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    }
};

// This should produce an array of 4 0x18-byte structs, representing the menu layout for the Dossier screen
static_assert(sizeof(ds_options) == 0x18 * 4, "Size of ds_options is not as expected");

// This array is not constant - it is modified for upgraded pistol
// XBE_GLOBAL(0x0017d168, 0x288)
M_ITEM ds_weapons[27] = {
    {
        .iconHashcode = HT_Texture_IconDsWeaponPP7,
        .title = WEAPON_PP7_NAME,
        .description = WEAPON_PP7_DESC,
        .identifier = WEAPON_PISTOL,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponGoldengun,
        .title = WEAPON_GOLDENGUN_NAME,
        .description = WEAPON_GOLDENGUN_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDossierWeapons,
        .title = WEAPON_KOWLOON_NAME,
        .description = WEAPON_KOWLOON_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponRaptormagnum,
        .title = WEAPON_RAPTORMAGNUM_NAME,
        .description = WEAPON_RAPTORMAGNUM_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponDEUTSCHEM9K,
        .title = WEAPON_DEUTSCHEM9K_NAME,
        .description = WEAPON_DEUTSCHEM9K_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSTORMM32,
        .title = WEAPON_STORMM32_NAME,
        .description = WEAPON_STORMM32_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSG5COMMANDO,
        .title = WEAPON_SG5COMMANDO_NAME,
        .description = WEAPON_SG5COMMANDO_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponAIMS20,
        .title = WEAPON_AIMS20_NAME,
        .description = WEAPON_AIMS20_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponFrinesi,
        .title = WEAPON_FRINESI_NAME,
        .description = WEAPON_FRINESI_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponCovertsniper,
        .title = WEAPON_COVERTSNIPER_NAME,
        .description = WEAPON_COVERTSNIPER_DESC,
        .identifier = WEAPON_SNIPER, // Upgradeable description
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponTacticalsniper,
        .title = WEAPON_TACTICALSNIPER_NAME,
        .description = WEAPON_TACTICALSNIPER_DESC,
        .identifier = WEAPON_SNIPER, // Upgradeable description
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponMiliteklauncher,
        .title = WEAPON_MILITEKLAUNCHER_NAME,
        .description = WEAPON_MILITEKLAUNCHER_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSentinel,
        .title = WEAPON_SENTINEL_NAME,
        .description = WEAPON_SENTINEL_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponScorpion,
        .title = WEAPON_SCORPION_NAME,
        .description = WEAPON_SCORPION_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSamurai,
        .title = WEAPON_SAMURAI_NAME,
        .description = WEAPON_SAMURAI_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponFraggrenade,
        .title = WEAPON_FRAGGRENADE_NAME,
        .description = WEAPON_FRAGGRENADE_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSmokegrenade,
        .title = WEAPON_SMOKEGRENADE_NAME,
        .description = WEAPON_SMOKEGRENADE_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponStungrenade,
        .title = WEAPON_STUNGRENADE_NAME,
        .description = WEAPON_STUNGRENADE_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSatchelcharge,
        .title = WEAPON_SATCHELCHARGE_NAME,
        .description = WEAPON_SATCHELCHARGE_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponRemotemine,
        .title = WEAPON_REMOTEMINE_NAME,
        .description = WEAPON_REMOTEMINE_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponTripbomb,
        .title = WEAPON_TRIPBOMB_NAME,
        .description = WEAPON_TRIPBOMB_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSnowmobile,
        .title = WEAPON_SNOWMOBILE_NAME,
        .description = WEAPON_SNOWMOBILE_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponCarmissiles,
        .title = WEAPON_CARMISSILES_NAME,
        .description = WEAPON_CARMISSILES_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSubtorpedoes,
        .title = WEAPON_SUBTORPEDOES_NAME,
        .description = WEAPON_SUBTORPEDOES_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponSubmines,
        .title = WEAPON_SUBMINES_NAME,
        .description = WEAPON_SUBMINES_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponJunglecar,
        .title = WEAPON_JUNGLECAR_NAME,
        .description = WEAPON_JUNGLECAR_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_IconDsWeaponJungleplane,
        .title = WEAPON_JUNGLEPLANE_NAME,
        .description = WEAPON_JUNGLEPLANE_DESC,
        .identifier = NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    }
};

// AUTOINJECT
bool C_SBDOSSIER_Handler(uchar param_1, M_CONTROL *param_2, uint control, uint eventType, intptr_t param_5, intptr_t param_6) {

    MessageType event = (MessageType)eventType;

    printf("In C_SBDOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", param_1, control, eventType, param_5, param_6);
    // Seems that param 3 is always C_SBDOSSIER, 4 is the action/event type, 5 is some unknown value (pointer?), 6 is 0
    switch (event) {
    
    case MessageType_Select: {
            
            int lVar1 = SCROLL_GET_VALUE(param_2);

            switch(lVar1) {
                case 0:
                    // Option 0: Records
                    Manager_SendMessage(&manager[param_1], MessageType_GoPage, P_DSRECORDS, 0);
                    return 1;
                case 1:
                    // Option 1: Rewards
                    Manager_SendMessage(&manager[param_1], MessageType_GoPage, P_DSREWARDS, 0);
                    return 1;
                case 2:
                    // Option 2: Dossier - Gadgets submenu
                    Menu_ChangePageCloseIris(P_DSGADGETS, param_1, SUB_C_SBDOSSIER_IRIS);
                    return 1;
                case 3:
                    // Option 3: Dossier - Weapons submenu
                    Menu_ChangePageCloseIris(P_DSWEAPONS, param_1, SUB_C_SBDOSSIER_IRIS);
                    return 1;
            }
        }

        case MessageType_Scroll:
        case MessageType_ValueSet:
            Menu_UpdateWheel(param_1, param_2, (M_ITEM*)ds_options, HT_MenuControl_00010d, HT_MenuControl_00010a,
                             HT_MenuControl_0001ed, SUB_C_SBDOSSIER_IRIS, event == MessageType_Scroll);
            return 1;

        case 0x51:
            __Menu_SendMessage(param_2, 0x27, 0, 3);
            return 1;

        default:
            // X button on Xbox controller fires 0x5e
            // Y button on Xbox controller fires 0x5d
            // Right analog, trigger buttons have no effect that I can see
            return 1;
        }
    
}

// AUTOINJECT
bool P_DOSSIER_Handler(uchar param_1, M_CONTROL* param_2, uint param_3, uint messageType, intptr_t param_5, intptr_t param_6) {

    //printf("In P_DOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", param_1, param_3, eventType, param_5, param_6);

    switch (messageType) {
        case MessageType_PageEnter:
            if (param_6 != P_NFMAP && param_6 != P_NFRESULTS && param_6 != P_NFBONUS) {
                Menu_StartIris(4, param_1, SUB_C_SBDOSSIER_IRIS);
                return true;
            }
            Menu_StartIris(0, param_1, SUB_C_SBDOSSIER_IRIS);
            __Menu_Send(param_1, C_SBDOSSIER, MessageType_SetValue, 0x0, 0x0);
            break;

        case 0x50: // Every frame
            Menu_PlayIris(1, param_1, SUB_C_SBDOSSIER_IRIS);
            return true;

        default:
            return true;
    }

    return true;
}

// AUTOINJECT
bool P_DSGADGETS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint messageType, intptr_t param_5, intptr_t param_6) {
    switch(messageType) {
        case MessageType_PageEnter: {
            
            Menu_StartIris(0, managerNum, SUB_C_DSGADGETS_IRIS);
            __Menu_Send(managerNum, C_SBDSGTSCROLL, 0x2e, 0, 0);

            // Modify the shaver icon depending on the current territory - special licensing deal
            ds_gadgets[6].iconHashcode = ((VIDEO_FRAME_RATE != 50) ? HT_Texture_IconDsGadgetShaverGeneric
                                          : HT_Texture_IconDsGadgetShaverBranded);

            break;
        }
        case 0x50: {
            Menu_PlayIris(1, managerNum, SUB_C_DSGADGETS_IRIS);
            break;
        }
    }
    return true;
}

// AUTOINJECT
bool C_SBDSGTSCROLL_Handler(uchar param_1, M_CONTROL *param_2, uint control, uint eventType, intptr_t param_5, intptr_t param_6) {

    MessageType event = (MessageType)eventType;

    switch(event) {
        case MessageType_ValueSet:
        case MessageType_Scroll: {
            // TODO: Is this taking label_upper, label_middle, label_lower?
            Menu_UpdateWheel(param_1, param_2, ds_gadgets, HT_MenuControl_00016a, HT_MenuControl_00016c,
                             SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT, SUB_C_DSGADGETS_IRIS, event == MessageType_Scroll);

            int gadgetNum = SCROLL_GET_VALUE(param_2);
            uint gadgetId = ds_gadgets[gadgetNum].identifier;
            int upgradeLevel = Menu_GetObjectUpgradeLevel(gadgetId, 0);
            const char* description = Txt_BindLabel(ds_gadgets[gadgetNum].description, 0);
            static char ug_buf[512]; // upgrade_buffer: contains the strings for the current weapon description, with upgrade if relevant
            strcpy(ug_buf, description);

            if(upgradeLevel == 0) {
                // No need to modify the string
            } else {
                char tmp[256];
                strcpy(tmp, ug_buf);

                // If a gadget has been upgraded, we append a second part to the description
                Action_TranslatedText modifier = TXT_NULL;
                switch(gadgetId) {
                    case GADGET_GRAPPLE:
                        modifier = REWARD_UPGRADE_RANGE;
                        break;
                    case GADGET_CAMERA:
                        modifier = REWARD_UPGRADE_MAGNIFICATION_AND_BIOTARGET;
                        break;
                    case GADGET_DARTGUN:
                        modifier = REWARD_UPGRADE_DARTGUN_STRONGER_SEDATIVE;
                        break;
                    case GADGET_DECODER:
                        modifier = REWARD_UPGRADE_PDA_SPEED;
                        break;
                    case GADGET_TASER:
                        modifier = REWARD_UPGRADE_RANGE_AND_CHARGE;
                        break;
                    case GADGET_LASER:
                        modifier = REWARD_UPGRADE_LASER_SPEED;
                        break;
                    case NO_UPGRADES:
                    default:
                        modifier = TXT_NULL;
                        break;
                }

                if (modifier != TXT_NULL) {
                    const char* modifierText = Txt_BindLabel(modifier, 0);
                    sprintf(ug_buf, "%s\n%s", tmp, modifierText);
                }

            }

            // Update the description
            LABEL_SET_TEXT(param_1, SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT, ug_buf);

            break;
        }
        case MessageType_ControlCreated: {
            __Menu_SendMessage(param_2, 0x27, 0, ARRAY_SIZE(ds_gadgets) - 1);
            __Menu_SendMessage(param_2, 0x2e, 0, 0);
            break;
        }
    }

    return true;

}

// AUTOINJECT
bool P_DSWEAPONS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint eventType, intptr_t param_5, intptr_t param_6) {
  switch((MessageType)eventType) {
    case MessageType_PageEnter: {
        switch(Menu_GetObjectUpgradeLevel(0, 0)) {
        case 0:
        ds_weapons[0].iconHashcode = HT_Texture_IconDsWeaponPP7;
        ds_weapons[0].title = WEAPON_PP7_NAME;
        ds_weapons[0].description = WEAPON_PP7_DESC;
        break;
        case 1:
        ds_weapons[0].iconHashcode = HT_Texture_IconDsWeaponPP7Gold;
        ds_weapons[0].title = WEAPON_PP7_GOLD_NAME;
        ds_weapons[0].description = WEAPON_PP7_GOLD_DESC;
        break;
        case 2:
        ds_weapons[0].iconHashcode = HT_Texture_IconDsWeaponP2K;
        ds_weapons[0].title = WEAPON_P2K_NAME;
        ds_weapons[0].description = WEAPON_P2K_DESC;
        break;
        case 3:
        ds_weapons[0].iconHashcode = HT_Texture_IconDsWeaponP2KGold;
        ds_weapons[0].title = WEAPON_P2K_GOLD_NAME;
        ds_weapons[0].description = WEAPON_P2K_GOLD_DESC;
        }
        Menu_StartIris(0, param_1, HT_MenuControl_00016e);
        __Menu_Send(param_1, C_SBDSWPSCROLL, 0x2e, 0, 0);
        break;
    }
    case 0x50: {
        Menu_PlayIris(1, param_1, HT_MenuControl_00016e);
        break;
    }
  }

  return true;
}

// AUTOINJECT
bool C_SBDSWPSCROLL_Handler(uchar param_1, M_CONTROL *param_2, uint control, uint eventType, intptr_t param_5, intptr_t param_6) {

    MessageType event = (MessageType)eventType;

    switch(event) {
        case MessageType_ValueSet:
        case MessageType_Scroll: {
            // TODO: Is this taking label_upper, label_middle, label_lower?
            Menu_UpdateWheel(param_1, param_2, ds_weapons, HT_MenuControl_000171, HT_MenuControl_000170,
                             SUB_C_SBDSWPSCROLL_DESCRIPTION_TEXT, HT_MenuControl_00016e, event == MessageType_Scroll);

            int weaponNum = SCROLL_GET_VALUE(param_2);
            uint weaponId = ds_weapons[weaponNum].identifier;
            int upgradeLevel = Menu_GetObjectUpgradeLevel(weaponId, 0);
            const char* description = Txt_BindLabel(ds_weapons[weaponNum].description, 0);
            static char wpn_upg_buf[512]; // upgrade_buffer: contains the strings for the current weapon description, with upgrade if relevant
            strcpy(wpn_upg_buf, description);

            if(upgradeLevel == 0) {
                // No need to modify the string
            } else {
                char tmp[256];
                strcpy(tmp, wpn_upg_buf);

                // If a weapon has been upgraded, we append a second part to the description
                Action_TranslatedText modifier = TXT_NULL;
                switch(weaponId) {
                    case WEAPON_SNIPER:
                        modifier = (upgradeLevel == 1 ? REWARD_UPGRADE_MAGNIFICATION :
                                    upgradeLevel == 2 ? REWARD_UPGRADE_MAGNIFICATION_AND_CLIP :
                                    TXT_NULL);
                        break;
                    default:
                        modifier = TXT_NULL;
                        break;
                }

                if (modifier != TXT_NULL) {
                    const char* modifierText = Txt_BindLabel(modifier, 0);
                    sprintf(wpn_upg_buf, "%s\n%s", tmp, modifierText);
                }

            }

            // Update the description
            LABEL_SET_TEXT(param_1, SUB_C_SBDSWPSCROLL_DESCRIPTION_TEXT, wpn_upg_buf);

            break;
        }
        case MessageType_ControlCreated: {
            __Menu_SendMessage(param_2, 0x27, 0, ARRAY_SIZE(ds_weapons) - 1);
            __Menu_SendMessage(param_2, 0x2e, 0, 0);
            break;
        }
    }

    return true;
}

// AUTOINJECT
bool P_DSREWARDS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint eventType, intptr_t param_5, intptr_t param_6) {
    switch((MessageType)eventType) {
        case MessageType_PageEnter: {
            M_CONTROL* rewardsControl = CONTROL_GET(param_1, C_RBDSREWARDS);
            Menu_AddItemsToControl(rewardsControl, sp_level, ARRAY_SIZE(sp_level), 0, 1);

            // Unclear why we use __Menu_SendMessage here rather than RADIO_SELECT_ITEM / __Menu_Send. Immediate dispatch vs next frame vs delayed?
            __Menu_SendMessage(rewardsControl, MessageType_SelectItemByValue, GameState.BaseMapHashCode, 0);
            break;
        }
    }

    return true;
}

// AUTOINJECT
bool P_DSRECORDS_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint eventType, intptr_t param_5, intptr_t param_6) {
    switch((MessageType)eventType) {
        case MessageType_PageEnter: {
            M_CONTROL* rewardsControl = CONTROL_GET(param_1, C_RBDSRECORDS);
            Menu_AddItemsToControl(rewardsControl, sp_level, ARRAY_SIZE(sp_level), 0, 1);

            // Unclear why we use __Menu_SendMessage here rather than RADIO_SELECT_ITEM / __Menu_Send. Immediate dispatch vs next frame vs delayed?
            __Menu_SendMessage(rewardsControl, MessageType_SelectItemByValue, GameState.BaseMapHashCode, 0);
            break;
        }
    }

    return true;
}

// ---- the records and rewards rows (radios over the missions)

// The records' texts, which the labels keep
// XBE_GLOBAL(0x0025e378, 0x20)
char records_best_text[0x20];
// XBE_GLOBAL(0x0025e398, 0x20)
char records_bond_moves_text[0x20];
// XBE_GLOBAL(0x0025e3b8, 0x20)
char records_upgrades_text[0x20];

extern uint32_t menu_bonus[8];   // MenuUnlocks.cpp

// The best medal held, a medal counting only with every one below it
static const struct { Action_TranslatedText name; HASHCODE icon; } records_medals[4] = {
    {(Action_TranslatedText)0x309, HT_Texture_IconMedalBronze},   // "BRONZE MEDAL"
    {(Action_TranslatedText)0x308, HT_Texture_IconMedalSilver},
    {(Action_TranslatedText)0x307, HT_Texture_IconMedalGold},
    {(Action_TranslatedText)0x306, HT_Texture_IconMedalPlatinum},   // "PLATINUM MEDAL"
};

// A mission's records: its best score, the Bond moves of its first scoring category, the upgrades its medals have
// given the first player, and the best medal held
// AUTOINJECT
bool C_RBDSRECORDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message != MessageType_Scroll)
        return true;
    HASHCODE level = (HASHCODE)RADIO_GET_VALUE_OF(control);
    SCORETABLE *totals = PlrStats_GetLevelTotals(level);
    LEVEL_BONUSES *bonuses = Menu_GetLevelBonuses(level);
    if (totals != NULL) {
        sprintf(records_best_text, "%d", totals->bestScore);
        sprintf(records_bond_moves_text, "%d", totals->statsTable[0].achieved);
    } else {
        sprintf(records_best_text, "%d", 0);
        sprintf(records_bond_moves_text, "%d", 0);
    }
    LABEL_SET_TEXT(managerNum, SUB_C_RBDSRECORDS_BEST_TEXT, records_best_text);
    LABEL_SET_TEXT(managerNum, SUB_C_RBDSRECORDS_BOND_MOVES_TEXT, records_bond_moves_text);

    uchar upgrades = 0;
    for (uint medal = 1; medal < 5; medal++) {
        REWARDINFO_tag info = {};
        PlrStarts_ProcessRewardCounter(menu_bonus, level, medal, 0, &info);
        if (info.hasMedal && info.objType == REWARD_UPGRADE)
            upgrades++;
    }
    sprintf(records_upgrades_text, "%d", upgrades);
    LABEL_SET_TEXT(managerNum, SUB_C_RBDSRECORDS_UPGRADES_TEXT, records_upgrades_text);

    int best = 0;
    while (best < 4 && bonuses->medals[best].earned)
        best++;
    if (best > 0) {
        __Menu_Send(managerNum, SUB_C_RBDSRECORDS_MEDAL_TEXT, MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_Send(managerNum, SUB_C_RBDSRECORDS_MEDAL_ICON, MessageType_SetState, CONTROL_STATE_INERT, 0);
        LABEL_SET_TEXT(managerNum, SUB_C_RBDSRECORDS_MEDAL_TEXT, Txt_BindLabel(records_medals[best - 1].name, 0));
        __Menu_Send(managerNum, SUB_C_RBDSRECORDS_MEDAL_ICON, MessageType_SetIcon, records_medals[best - 1].icon, 0);
    } else {
        __Menu_Send(managerNum, SUB_C_RBDSRECORDS_MEDAL_TEXT, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_Send(managerNum, SUB_C_RBDSRECORDS_MEDAL_ICON, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    }
    return true;
}

// Each medal's reward, shown when the first player has it and every medal below it
static const HASHCODE rewards_icons[4] = {
    HT_MenuControl_0001d6, HT_MenuControl_0001d7, HT_MenuControl_0001d8, HT_MenuControl_0001d9,
};
static const HASHCODE rewards_names[4] = {
    HT_MenuControl_0001d2, HT_MenuControl_0001d3, HT_MenuControl_0001d4, HT_MenuControl_0001d5,
};

// AUTOINJECT
bool C_RBDSREWARDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message != MessageType_Scroll && message != MessageType_ValueSet)
        return true;
    LEVEL_BONUSES *bonuses = Menu_GetLevelBonuses((HASHCODE)RADIO_GET_VALUE_OF(control));
    for (int medal = 0; medal < 4; medal++) {
        __Menu_Send(managerNum, rewards_icons[medal], MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        __Menu_Send(managerNum, rewards_names[medal], MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    }
    for (int medal = 0; medal < 4 && bonuses->medals[medal].earned; medal++) {
        __Menu_Send(managerNum, rewards_icons[medal], MessageType_SetState, CONTROL_STATE_INERT, 0);
        __Menu_Send(managerNum, rewards_icons[medal], MessageType_SetIcon, bonuses->medals[medal].icon, 0);
        __Menu_Send(managerNum, rewards_names[medal], MessageType_SetState, CONTROL_STATE_INERT, 0);
        LABEL_SET_TEXT(managerNum, rewards_names[medal], Txt_BindLabel(bonuses->medals[medal].name, 0));
    }
    return true;
}
