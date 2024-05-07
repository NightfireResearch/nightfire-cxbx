#include "../helpers.h"

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

// ds_options: 002e0af8 (PS2 EU), ?? (Xbox)
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

#define manager ((M_MANAGER *)0x0025f1d0)

#include <stdio.h>

// AUTOINJECT
undefined4 C_SBDOSSIER_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint param_4, int param_5, int param_6) {

    printf("In C_SBDOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", param_1, param_3, param_4, param_5, param_6);
    /*
    switch (param_4) {
        case 0x4b: {
            
            int lVar1 = __Menu_SendMessage(param_2, 0x40, 0, 0);

            switch(lVar1) {
                case 0:
                    Manager_SendMessage(manager[param_1], 0x44, 0x4000003a, 0);
                    return 1;
                case 1:
                    Manager_SendMessage(manager[param_1], 0x44, 0x4000003b, 0);
                    return 1;
                case 2:
                    Menu_ChangePageCloseIris(MENU_DSGADGETS, param_1, 0x1000010b);
                    return 1;
                case 3:
                    Menu_ChangePageCloseIris(MENU_DSWEAPONS, param_1, 0x1000010b);
                    return 1;
            }
        }

    case 0x49:
    case 0x54:
        Menu_UpdateWheel(param_1, param_2, ds_options, 0x1000010d, 0x1000010a, 0x100001ed, 0x1000010b, param_4 == 0x49);
        return 1;

    case 0x51:
        __Menu_SendMessage(param_2, 0x27, 0, 3);
        return 1;

    default:
        return 1;
    }
    */
}