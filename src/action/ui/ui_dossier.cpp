

#include "ui.h"
#include "Manager.h"
#include <stdio.h>
// const M_ITEM ds_gadgets[14] = {

//     {
//         .iconHashcode = ICON_DS_GADGET_TASER,
//         .title = GADGET_TASER_NAME,
//         .description = GADGET_TASER_DESC,
//         .identifier = 0x12,
//         .enabled = 1,
//         .descriptionWhenDisabled = TXT_NULL
//     },
//     {
//         .iconHashcode = ICON_DS_GADGET_LASER,
//         .title = GADGET_LASER_NAME,
//         .description = GADGET_LASER_DESC,
//         .identifier = 0x15,
//         .enabled = 1,
//         .descriptionWhenDisabled = TXT_NULL
//     },
//     {
//         .iconHashcode = ICON_DS_GADGET_GRAPPLE,
//         .title = GADGET_TASER_NAME,
//         .description = GADGET_GRAPPLE_DESC,   
//         .identifier = 0x03,
//         .enabled = 1,
//         .descriptionWhenDisabled = TXT_NULL 
//     },

//     // TODO: Finish me

// };

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
            
            int lVar1 = __Menu_SendMessage(param_2, MessageType_GetValue, 0, 0); // Get the item number

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
                    Menu_ChangePageCloseIris(P_DSGADGETS, param_1, 0x1000010b);
                    return 1;
                case 3:
                    // Option 3: Dossier - Weapons submenu
                    Menu_ChangePageCloseIris(P_DSWEAPONS, param_1, 0x1000010b);
                    return 1;
            }
        }

        case MessageType_Scroll:
        case MessageType_Enter:
            // Note - there is a bug in Menu_UpdateWheel that causes a crash if the M_ITEM array has fewer than 999 elements
            // We can work around this by allocating a dummy array immediately after the ds_options array
            Menu_UpdateWheel(param_1, param_2, (M_ITEM*)ds_options, (HASHCODE)0x1000010d, (HASHCODE)0x1000010a, (HASHCODE)0x100001ed, (HASHCODE)0x1000010b, event == MessageType_Scroll);
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
bool P_DOSSIER_Handler(uchar param_1, M_CONTROL* param_2, uint param_3, uint param_4, int param_5, int param_6) {

    //printf("In P_DOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", param_1, param_3, param_4, param_5, param_6);

    switch (param_4) {
        case 0x4c:
            if (param_6 != 0x4000001c && param_6 != 0x40000036 && param_6 != 0x40000038) {
                Menu_StartIris(4, param_1, 0x1000010b);
                return true;
            }
            Menu_StartIris(0, param_1, 0x1000010b);
            __Menu_Send(param_1, (HASHCODE)0x1000010c, 0x2e, 0x0, 0x0); // Hashcode corresponds to C_SBDOSSIER_Handler?
            break;

        case 0x50: // Every frame
            Menu_PlayIris(1, param_1, 0x1000010b);
            return true;

        default:
            return true;
    }

    return true;
}