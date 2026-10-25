#include "ui.h"
#include "Manager.h"
#include "Menu.h"
#include "../engine/Text.h"
#include "../game/sp/PlayerStats.h"
#include "../game/Upgrade.h"
#include <stdio.h>
#include <string.h>

// This array is not constant - it is modified for the custom branded shaver, and potentially for upgraded gadgets too?
// XBE_GLOBAL(0x0017d3f0, 0x150)
M_ITEM ds_gadgets[14] = {
    {
        .iconHashcode = HT_Texture_gadget_stunner,
        .title = GADGET_TASER_NAME,
        .description = GADGET_TASER_DESC,
        .identifier = UPGRADE_BIT_TASER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_gadget_watch,
        .title = GADGET_LASER_NAME,
        .description = GADGET_LASER_DESC,
        .identifier = UPGRADE_BIT_LASER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_gadget_grapple,
        .title = GADGET_GRAPPLE_NAME,
        .description = GADGET_GRAPPLE_DESC,   
        .identifier = UPGRADE_BIT_GRAPPLE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_microcamera,
        .title = GADGET_CAMERA_NAME,
        .description = GADGET_CAMERA_DESC,   
        .identifier = UPGRADE_BIT_CAMERA,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_decryptor,
        .title = GADGET_DECODER_NAME,
        .description = GADGET_DECODER_DESC,   
        .identifier = UPGRADE_BIT_PDA,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_qworm,
        .title = GADGET_QWORM_NAME,
        .description = GADGET_QWORM_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_shaver_logo, // default value never seen, this is always overwritten as soon as the page is started
        .title = GADGET_SHAVER_NAME,
        .description = GADGET_SHAVER_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_suitcase,
        .title = GADGET_SENTRY_NAME,
        .description = GADGET_SENTRY_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_dart,
        .title = GADGET_DARTGUN_NAME,
        .description = GADGET_DARTGUN_DESC,   
        .identifier = UPGRADE_BIT_DARTGUN,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_nightvision,
        .title = GADGET_NIGHTVISION_NAME,
        .description = GADGET_NIGHTVISION_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_vanquishsmoke,
        .title = GADGET_SMOKESCREEN_NAME,
        .description = GADGET_SMOKESCREEN_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_turbo,
        .title = GADGET_TURBO_NAME,
        .description = GADGET_TURBO_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_vanquishknifeedge,
        .title = GADGET_QWEDGE_NAME,
        .description = GADGET_QWEDGE_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = HT_Texture_gadget_emp,
        .title = GADGET_EMP_NAME,
        .description = GADGET_EMP_DESC,   
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    }
};

// XBE_GLOBAL(0x0017cf70, 0x60) - PS2 EU 0x002e0af8
const M_ITEM ds_options[4] = {
    {
        .iconHashcode = HT_Texture_icon_records,
        .title = DOSSIER_RECORDS_NAME,
        .description = DOSSIER_RECORDS_DESC,
        .identifier = 0,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_icon_rewards,
        .title = DOSSIER_REWARDS_NAME,
        .description = DOSSIER_REWARDS_DESC,
        .identifier = 1,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_gadget_watch,
        .title = DOSSIER_GADGETS_NAME,
        .description = DOSSIER_GADGETS_DESC,
        .identifier = 2,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_kowloon,
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
        .iconHashcode = HT_Texture_weapon_pp7,
        .title = WEAPON_PP7_NAME,
        .description = WEAPON_PP7_DESC,
        .identifier = UPGRADE_BIT_HANDGUN,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_goldengun,
        .title = WEAPON_GOLDENGUN_NAME,
        .description = WEAPON_GOLDENGUN_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_kowloon,
        .title = WEAPON_KOWLOON_NAME,
        .description = WEAPON_KOWLOON_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_magnum,
        .title = WEAPON_RAPTORMAGNUM_NAME,
        .description = WEAPON_RAPTORMAGNUM_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_deutsche,
        .title = WEAPON_DEUTSCHEM9K_NAME,
        .description = WEAPON_DEUTSCHEM9K_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_storm,
        .title = WEAPON_STORMM32_NAME,
        .description = WEAPON_STORMM32_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_sig,
        .title = WEAPON_SG5COMMANDO_NAME,
        .description = WEAPON_SG5COMMANDO_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_aims,
        .title = WEAPON_AIMS20_NAME,
        .description = WEAPON_AIMS20_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_frinesi,
        .title = WEAPON_FRINESI_NAME,
        .description = WEAPON_FRINESI_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_wintersniper,
        .title = WEAPON_COVERTSNIPER_NAME,
        .description = WEAPON_COVERTSNIPER_DESC,
        .identifier = UPGRADE_BIT_SNIPER, // Upgradeable description
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_wintertacticalsniper,
        .title = WEAPON_TACTICALSNIPER_NAME,
        .description = WEAPON_TACTICALSNIPER_DESC,
        .identifier = UPGRADE_BIT_SNIPER, // Upgradeable description
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_militek,
        .title = WEAPON_MILITEKLAUNCHER_NAME,
        .description = WEAPON_MILITEKLAUNCHER_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_sentinel,
        .title = WEAPON_SENTINEL_NAME,
        .description = WEAPON_SENTINEL_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_scorpion,
        .title = WEAPON_SCORPION_NAME,
        .description = WEAPON_SCORPION_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_laserrifle,
        .title = WEAPON_SAMURAI_NAME,
        .description = WEAPON_SAMURAI_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_fraggrenade,
        .title = WEAPON_FRAGGRENADE_NAME,
        .description = WEAPON_FRAGGRENADE_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_smokegrenade,
        .title = WEAPON_SMOKEGRENADE_NAME,
        .description = WEAPON_SMOKEGRENADE_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_flashbang,
        .title = WEAPON_STUNGRENADE_NAME,
        .description = WEAPON_STUNGRENADE_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_satchel,
        .title = WEAPON_SATCHELCHARGE_NAME,
        .description = WEAPON_SATCHELCHARGE_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_remotemine,
        .title = WEAPON_REMOTEMINE_NAME,
        .description = WEAPON_REMOTEMINE_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_tripbomb,
        .title = WEAPON_TRIPBOMB_NAME,
        .description = WEAPON_TRIPBOMB_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_snowmobile,
        .title = WEAPON_SNOWMOBILE_NAME,
        .description = WEAPON_SNOWMOBILE_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_vanquishmissiles,
        .title = WEAPON_CARMISSILES_NAME,
        .description = WEAPON_CARMISSILES_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_vanquishtorpedo,
        .title = WEAPON_SUBTORPEDOES_NAME,
        .description = WEAPON_SUBTORPEDOES_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_qcharge,
        .title = WEAPON_SUBMINES_NAME,
        .description = WEAPON_SUBMINES_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_urbanassault,
        .title = WEAPON_JUNGLECAR_NAME,
        .description = WEAPON_JUNGLECAR_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = HT_Texture_weapon_ultralight,
        .title = WEAPON_JUNGLEPLANE_NAME,
        .description = WEAPON_JUNGLEPLANE_DESC,
        .identifier = UPGRADE_BIT_NONE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    }
};

// AUTOINJECT
bool C_SBDOSSIER_Handler(uchar managerNum, M_CONTROL *ctrl, uint control, uint eventType, intptr_t arg1, intptr_t arg2) {

    MessageType event = (MessageType)eventType;

    printf("In C_SBDOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", managerNum, control, eventType, arg1, arg2);
    switch (event) {
    
    case MessageType_Select: {
            
            int selected = SCROLL_GET_VALUE(ctrl);

            switch(selected) {
                case 0:
                    // Option 0: Records
                    Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_DSRECORDS, 0);
                    return 1;
                case 1:
                    // Option 1: Rewards
                    Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_DSREWARDS, 0);
                    return 1;
                case 2:
                    // Option 2: Dossier - Gadgets submenu
                    Menu_ChangePageCloseIris(P_DSGADGETS, managerNum, SUB_C_SBDOSSIER_IRIS);
                    return 1;
                case 3:
                    // Option 3: Dossier - Weapons submenu
                    Menu_ChangePageCloseIris(P_DSWEAPONS, managerNum, SUB_C_SBDOSSIER_IRIS);
                    return 1;
            }
        }

        case MessageType_Scroll:
        case MessageType_ValueSet:
            Menu_UpdateWheel(managerNum, ctrl, (M_ITEM*)ds_options, SUB_C_SBDOSSIER_WHEEL_TEXT, SUB_C_SBDOSSIER_ICON,
                             SUB_C_SBDOSSIER_DESCRIPTION_TEXT, SUB_C_SBDOSSIER_IRIS, event == MessageType_Scroll);
            return 1;

        case MessageType_ControlCreated:
            __Menu_SendMessage(ctrl, MessageType_SetRange, 0, 3);
            return 1;

        default:
            // X button on Xbox controller fires 0x5e
            // Y button on Xbox controller fires 0x5d
            // Right analog, trigger buttons have no effect that I can see
            return 1;
        }
    
}

// AUTOINJECT
bool P_DOSSIER_Handler(uchar managerNum, M_CONTROL* control, uint hashcode, uint messageType, intptr_t arg1, intptr_t arg2) {

    //printf("In P_DOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", managerNum, hashcode, eventType, arg1, arg2);

    switch (messageType) {
        case MessageType_PageEnter:
            if (arg2 != P_NFMAP && arg2 != P_NFRESULTS && arg2 != P_NFBONUS) {
                Menu_StartIris(IRIS_LAST_FRAME, managerNum, SUB_C_SBDOSSIER_IRIS);
                return true;
            }
            Menu_StartIris(IRIS_FROM_MIDDLE, managerNum, SUB_C_SBDOSSIER_IRIS);
            __Menu_Send(managerNum, C_SBDOSSIER, MessageType_SetValue, 0x0, 0x0);
            break;

        case MessageType_PageUpdate:
            Menu_PlayIris(1, managerNum, SUB_C_SBDOSSIER_IRIS);
            return true;

        default:
            return true;
    }

    return true;
}

// AUTOINJECT
bool P_DSGADGETS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint messageType, intptr_t arg1, intptr_t arg2) {
    switch(messageType) {
        case MessageType_PageEnter: {
            
            Menu_StartIris(IRIS_FROM_MIDDLE, managerNum, SUB_C_SBDSGTSCROLL_IRIS);
            __Menu_Send(managerNum, C_SBDSGTSCROLL, MessageType_SetValue, 0, 0);

            // Modify the shaver icon depending on the current territory - special licensing deal
            ds_gadgets[6].iconHashcode = ((VIDEO_FRAME_RATE != 50) ? HT_Texture_gadget_shaver
                                          : HT_Texture_gadget_shaver_logo);

            break;
        }
        case MessageType_PageUpdate: {
            Menu_PlayIris(1, managerNum, SUB_C_SBDSGTSCROLL_IRIS);
            break;
        }
    }
    return true;
}

// AUTOINJECT
bool C_SBDSGTSCROLL_Handler(uchar managerNum, M_CONTROL *ctrl, uint control, uint eventType, intptr_t arg1, intptr_t arg2) {

    MessageType event = (MessageType)eventType;

    switch(event) {
        case MessageType_ValueSet:
        case MessageType_Scroll: {
            // TODO: Is this taking label_upper, label_middle, label_lower?
            Menu_UpdateWheel(managerNum, ctrl, ds_gadgets, SUB_C_SBDSGTSCROLL_WHEEL_TEXT, SUB_C_SBDSGTSCROLL_ICON,
                             SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT, SUB_C_SBDSGTSCROLL_IRIS, event == MessageType_Scroll);

            int gadgetNum = SCROLL_GET_VALUE(ctrl);
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
                    case UPGRADE_BIT_GRAPPLE:
                        modifier = REWARD_UPGRADE_RANGE;
                        break;
                    case UPGRADE_BIT_CAMERA:
                        modifier = REWARD_UPGRADE_MAGNIFICATION_AND_BIOTARGET;
                        break;
                    case UPGRADE_BIT_DARTGUN:
                        modifier = REWARD_UPGRADE_DARTGUN_STRONGER_SEDATIVE;
                        break;
                    case UPGRADE_BIT_PDA:
                        modifier = REWARD_UPGRADE_PDA_SPEED;
                        break;
                    case UPGRADE_BIT_TASER:
                        modifier = REWARD_UPGRADE_RANGE_AND_CHARGE;
                        break;
                    case UPGRADE_BIT_LASER:
                        modifier = REWARD_UPGRADE_LASER_SPEED;
                        break;
                    case UPGRADE_BIT_NONE:
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
            LABEL_SET_TEXT(managerNum, SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT, ug_buf);

            break;
        }
        case MessageType_ControlCreated: {
            __Menu_SendMessage(ctrl, MessageType_SetRange, 0, ARRAY_SIZE(ds_gadgets) - 1);
            __Menu_SendMessage(ctrl, MessageType_SetValue, 0, 0);
            break;
        }
    }

    return true;

}

// AUTOINJECT
bool P_DSWEAPONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint eventType, intptr_t arg1, intptr_t arg2) {
  switch((MessageType)eventType) {
    case MessageType_PageEnter: {
        switch(Menu_GetObjectUpgradeLevel(UPGRADE_BIT_HANDGUN, 0)) {
        case 0:
        ds_weapons[0].iconHashcode = HT_Texture_weapon_pp7;
        ds_weapons[0].title = WEAPON_PP7_NAME;
        ds_weapons[0].description = WEAPON_PP7_DESC;
        break;
        case 1:
        ds_weapons[0].iconHashcode = HT_Texture_weapon_pp7_gold;
        ds_weapons[0].title = WEAPON_PP7_GOLD_NAME;
        ds_weapons[0].description = WEAPON_PP7_GOLD_DESC;
        break;
        case 2:
        ds_weapons[0].iconHashcode = HT_Texture_weapon_p2k;
        ds_weapons[0].title = WEAPON_P2K_NAME;
        ds_weapons[0].description = WEAPON_P2K_DESC;
        break;
        case 3:
        ds_weapons[0].iconHashcode = HT_Texture_weapon_p2k_gold;
        ds_weapons[0].title = WEAPON_P2K_GOLD_NAME;
        ds_weapons[0].description = WEAPON_P2K_GOLD_DESC;
        }
        Menu_StartIris(IRIS_FROM_MIDDLE, managerNum, SUB_C_SBDSWPSCROLL_IRIS);
        __Menu_Send(managerNum, C_SBDSWPSCROLL, MessageType_SetValue, 0, 0);
        break;
    }
    case MessageType_PageUpdate: {
        Menu_PlayIris(1, managerNum, SUB_C_SBDSWPSCROLL_IRIS);
        break;
    }
  }

  return true;
}

// AUTOINJECT
bool C_SBDSWPSCROLL_Handler(uchar managerNum, M_CONTROL *ctrl, uint control, uint eventType, intptr_t arg1, intptr_t arg2) {

    MessageType event = (MessageType)eventType;

    switch(event) {
        case MessageType_ValueSet:
        case MessageType_Scroll: {
            // TODO: Is this taking label_upper, label_middle, label_lower?
            Menu_UpdateWheel(managerNum, ctrl, ds_weapons, SUB_C_SBDSWPSCROLL_WHEEL_TEXT, SUB_C_SBDSWPSCROLL_ICON,
                             SUB_C_SBDSWPSCROLL_DESCRIPTION_TEXT, SUB_C_SBDSWPSCROLL_IRIS, event == MessageType_Scroll);

            int weaponNum = SCROLL_GET_VALUE(ctrl);
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
                    case UPGRADE_BIT_SNIPER:
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
            LABEL_SET_TEXT(managerNum, SUB_C_SBDSWPSCROLL_DESCRIPTION_TEXT, wpn_upg_buf);

            break;
        }
        case MessageType_ControlCreated: {
            __Menu_SendMessage(ctrl, MessageType_SetRange, 0, ARRAY_SIZE(ds_weapons) - 1);
            __Menu_SendMessage(ctrl, MessageType_SetValue, 0, 0);
            break;
        }
    }

    return true;
}

// AUTOINJECT
bool P_DSREWARDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint eventType, intptr_t arg1, intptr_t arg2) {
    switch((MessageType)eventType) {
        case MessageType_PageEnter: {
            M_CONTROL* rewardsControl = CONTROL_GET(managerNum, C_RBDSREWARDS);
            Menu_AddItemsToControl(rewardsControl, sp_level, ARRAY_SIZE(sp_level), 0, 1);

            // Unclear why we use __Menu_SendMessage here rather than RADIO_SELECT_ITEM / __Menu_Send. Immediate dispatch vs next frame vs delayed?
            __Menu_SendMessage(rewardsControl, MessageType_SelectItemByValue, GameState.BaseMapHashCode, 0);
            break;
        }
    }

    return true;
}

// AUTOINJECT
bool P_DSRECORDS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint eventType, intptr_t arg1, intptr_t arg2) {
    switch((MessageType)eventType) {
        case MessageType_PageEnter: {
            M_CONTROL* rewardsControl = CONTROL_GET(managerNum, C_RBDSRECORDS);
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
    {(Action_TranslatedText)0x309, HT_Texture_medal_bronze},   // "BRONZE MEDAL"
    {(Action_TranslatedText)0x308, HT_Texture_medal_silver},
    {(Action_TranslatedText)0x307, HT_Texture_medal_gold},
    {(Action_TranslatedText)0x306, HT_Texture_medal_platinum},   // "PLATINUM MEDAL"
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
    for (uint medal = MEDAL_BRONZE; medal < MEDAL_NONE; medal++) {
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
    SUB_C_RBDSREWARDS_ICON_0, SUB_C_RBDSREWARDS_ICON_1, SUB_C_RBDSREWARDS_ICON_2, SUB_C_RBDSREWARDS_ICON_3,
};
static const HASHCODE rewards_names[4] = {
    SUB_C_RBDSREWARDS_NAME_0, SUB_C_RBDSREWARDS_NAME_1, SUB_C_RBDSREWARDS_NAME_2, SUB_C_RBDSREWARDS_NAME_3,
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
