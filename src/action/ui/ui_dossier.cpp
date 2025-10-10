#include "ui.h"
#include "Manager.h"
#include <stdio.h>
#include <string.h>

typedef enum {
    GADGET_GRAPPLE = 0x03,
    GADGET_CAMERA = 0x06,
    GADGET_DARTGUN = 0x0C,
    GADGET_DECODER = 0x0F,
    GADGET_TASER = 0x12,
    GADGET_LASER = 0x15,
    GADGET_NO_UPGRADES = 0x3F,
} GadgetUpgradeabilityType;

// This array is not constant - it is modified for the custom branded shaver, and potentially for upgraded gadgets too?
M_ITEM ds_gadgets[14] = {
    {
        .iconHashcode = ICON_DS_GADGET_TASER,
        .title = GADGET_TASER_NAME,
        .description = GADGET_TASER_DESC,
        .identifier = GADGET_TASER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = ICON_DS_GADGET_LASER,
        .title = GADGET_LASER_NAME,
        .description = GADGET_LASER_DESC,
        .identifier = GADGET_LASER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = ICON_DS_GADGET_GRAPPLE,
        .title = GADGET_GRAPPLE_NAME,
        .description = GADGET_GRAPPLE_DESC,   
        .identifier = GADGET_GRAPPLE,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_CAMERA,
        .title = GADGET_CAMERA_NAME,
        .description = GADGET_CAMERA_DESC,   
        .identifier = GADGET_CAMERA,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_DECODER,
        .title = GADGET_DECODER_NAME,
        .description = GADGET_DECODER_DESC,   
        .identifier = GADGET_DECODER,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_QWORM,
        .title = GADGET_QWORM_NAME,
        .description = GADGET_QWORM_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_SHAVER_BRANDED, // default value never seen, this is always overwritten as soon as the page is started
        .title = GADGET_SHAVER_NAME,
        .description = GADGET_SHAVER_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_SENTRY,
        .title = GADGET_SENTRY_NAME,
        .description = GADGET_SENTRY_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_DARTGUN,
        .title = GADGET_DARTGUN_NAME,
        .description = GADGET_DARTGUN_DESC,   
        .identifier = GADGET_DARTGUN,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_NIGHTVISION,
        .title = GADGET_NIGHTVISION_NAME,
        .description = GADGET_NIGHTVISION_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_SMOKESCREEN,
        .title = GADGET_SMOKESCREEN_NAME,
        .description = GADGET_SMOKESCREEN_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_TURBO,
        .title = GADGET_TURBO_NAME,
        .description = GADGET_TURBO_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_QWEDGE,
        .title = GADGET_QWEDGE_NAME,
        .description = GADGET_QWEDGE_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    },
    {
        .iconHashcode = ICON_DS_GADGET_EMP,
        .title = GADGET_EMP_NAME,
        .description = GADGET_EMP_DESC,   
        .identifier = GADGET_NO_UPGRADES,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL 
    }
};

// ds_options: 002e0af8 (PS2 EU), 0017cf70 (Xbox)
const M_ITEM ds_options[4] = {
    {
        .iconHashcode = ICON_DOSSIER_RECORDS,
        .title = DOSSIER_RECORDS_NAME,
        .description = DOSSIER_RECORDS_DESC,
        .identifier = 0,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = ICON_DOSSIER_REWARDS,
        .title = DOSSIER_REWARDS_NAME,
        .description = DOSSIER_REWARDS_DESC,
        .identifier = 1,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = ICON_DOSSIER_GADGETS,
        .title = DOSSIER_GADGETS_NAME,
        .description = DOSSIER_GADGETS_DESC,
        .identifier = 2,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    },
    {
        .iconHashcode = ICON_DOSSIER_WEAPONS,
        .title = MP_CFG_OR_DOSSIER_WEAPONS,
        .description = DOSSIER_WEAPONS_DESC,
        .identifier = 3,
        .enabled = 1,
        .descriptionWhenDisabled = TXT_NULL
    }
};

// This should produce an array of 4 0x18-byte structs, representing the menu layout for the Dossier screen
static_assert(sizeof(ds_options) == 0x18 * 4, "Size of ds_options is not as expected");

// AUTOINJECT
bool C_SBDOSSIER_Handler(uchar param_1, M_CONTROL *param_2, uint control, uint eventType, int param_5, int param_6) {

    MessageType event = (MessageType)eventType;

    printf("In C_SBDOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", param_1, control, eventType, param_5, param_6);
    // Seems that param 3 is always C_SBDOSSIER, 4 is the action/event type, 5 is some unknown value (pointer?), 6 is 0
    switch (event) {
    
    case MessageType_Select: {
            
            int lVar1 = WHEEL_GET_VALUE(param_2);

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
        case MessageType_Enter:
            Menu_UpdateWheel(param_1, param_2, (M_ITEM*)ds_options, (HASHCODE)0x1000010d, (HASHCODE)0x1000010a, (HASHCODE)0x100001ed, SUB_C_SBDOSSIER_IRIS, event == MessageType_Scroll);
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
bool P_DOSSIER_Handler(uchar param_1, M_CONTROL* param_2, uint param_3, uint messageType, int param_5, int param_6) {

    //printf("In P_DOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", param_1, param_3, param_4, param_5, param_6);

    switch (messageType) {
        case MessageType_MaybeEnterPage:
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
bool P_DSGADGETS_Handler(uchar managerNum, M_CONTROL *param_2, uint param_3, uint messageType, int param_5, int param_6) {
    switch(messageType) {
        case MessageType_MaybeEnterPage: {
            
            Menu_StartIris(0, managerNum, SUB_C_DSGADGETS_IRIS);
            __Menu_Send(managerNum, C_SBDSGTSCROLL, 0x2e, 0, 0);

            // Modify the shaver icon depending on the current territory - special licensing deal
            ds_gadgets[6].iconHashcode = ((VIDEO_FRAME_RATE != 50) ? ICON_DS_GADGET_SHAVER_GENERIC : ICON_DS_GADGET_SHAVER_BRANDED);

            break;
        }
        case 0x50: {
            Menu_PlayIris(1, managerNum, SUB_C_DSGADGETS_IRIS);
            break;
        }
    }
    return true;
}

#define SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT ((HASHCODE)0x1000016b)


// AUTOGEN
undefined4 __cdecl Menu_GetObjectUpgradeLevel(uint param_1,byte param_2);

// AUTOINJECT
bool C_SBDSGTSCROLL_Handler(uchar param_1, M_CONTROL *param_2, uint control, uint eventType, int param_5, int param_6) {

    MessageType event = (MessageType)eventType;

    switch(event) {
        case MessageType_Enter:
        case MessageType_Scroll: {
            // TODO: Is this taking label_upper, label_middle, label_lower?
            Menu_UpdateWheel(param_1, param_2, ds_gadgets, (HASHCODE)0x1000016a, (HASHCODE)0x1000016c, (HASHCODE)0x1000016b, (HASHCODE)0x1000016d, event == MessageType_Scroll);

            int gadgetNum = WHEEL_GET_VALUE(param_2);
            uint gadgetId = ds_gadgets[gadgetNum].identifier;
            int upgradeLevel = Menu_GetObjectUpgradeLevel(gadgetId, 0);
            const char* description = Txt_BindLabel(ds_gadgets[gadgetNum].description, 0);
            static char ug_buf[512]; // upgrade_buffer: contains the strings for each gadget, with upgrade if relevant
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
                    case GADGET_NO_UPGRADES:
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
            __Menu_Send(param_1, SUB_C_SBDSGTSCROLL_DESCRIPTION_TEXT, MessageType_SetText, (int)ug_buf, 0);

            break;
        }
        case MessageType_Unknown_0x51: {
            __Menu_SendMessage(param_2,0x27,0,0xd);
            __Menu_SendMessage(param_2,0x2e,0,0);
            break;
        }
    }

        return true;

}