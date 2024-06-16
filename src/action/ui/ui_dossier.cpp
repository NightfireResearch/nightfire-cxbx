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

// ds_options: 002e0af8 (PS2 EU), 0017cf70 (Xbox)

// If we reconstruct ds_options in code ourselves, we get a crash!
// The crash occurs at 0x7fa01 which is the Menu_UpdateWheel function - error code 0xC0000005: Access violation
// The offending code looks like (param_3[local_28].enabled != false)

// At the time of crash:
// ds_options located at: 0x6bf95830
// [0x1F38] MAIN: Received Exception (Code := 0xC0000005)
//  EIP := 0x0007FA01(=D3DDevice__m_VerticalBlankEvent_OFFSET+0x7d4ad)
//  EFL := 0x00010246
//  EAX := 0x00000300 EBX := 0x00000000 ECX := 0x00000000 EDX := 0x00000BB5
//  ESI := 0x1000010D EDI := 0x6BF95830 ESP := 0x0D94F9CC EBP := 0x00000000
//  CR2 := 0x00000000

// The assembly is: AL,byte ptr [EDI + EDX*0x8 + 0x10]

// EDI contains the address of ds_options as expected
// EDX should contain the offset (in units of 0x8 bytes) into the ds_options array
// 0x10 is the offset of the enabled field in the M_ITEM struct

// EDX is actually 0x00000BB5, which is 2997 in decimal. 2997 * 8 = 23976. Divide by 0x18, this is the 999th element.

// There is some logic in the Menu_UpdateWheel function that hardcodes 999 as a special value, but this is out of the bounds of the ds_options array!
// This doesn't cause a crash in the original game because the Xbox lacks any memory protection and the game just reads whatever is there
// but in our case, it crashes because we're trying to read memory that isn't allocated?

// This could also lead to weird behaviour that changes depending on where the ds_options array is located in memory and what is in the memory after it

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
// Guard against the game trying to read past the end of the array by allocating a bunch of extra space
// This is a hacky workaround but seems to work just fine
M_ITEM dummy[999];

// This should produce an array of 4 0x18-byte structs, representing the menu layout for the Dossier screen
static_assert(sizeof(ds_options) == 0x18 * 4, "Size of ds_options is not as expected");


#define manager ((M_MANAGER *)0x0025f1d0)

#include <stdio.h>

// AUTOGEN
int __cdecl __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);

// AUTOGEN
int __cdecl Manager_SendMessage(M_MANAGER *param_1, uint msgType, int param_3, int param_4);

// AUTOGEN
void __cdecl Menu_ChangePageCloseIris(uint param_1, uchar param_2, uint param_3);

// AUTOGEN
void __cdecl Menu_UpdateWheel(uchar param_1, M_CONTROL *param_2, M_ITEM *param_3, uint param_4, uint param_5, uint param_6, uint param_7, char param_8);


// AUTOINJECT
undefined4 C_SBDOSSIER_Handler(uchar param_1, M_CONTROL *param_2, uint param_3, uint param_4, int param_5, int param_6) {

    printf("In C_SBDOSSIER_Handler, params 0x%08x, 0x%08x, 0x%08x, 0x%08x, 0x%08x\n", param_1, param_3, param_4, param_5, param_6);

    switch (param_4) {
        case 0x4b: {
            
            int lVar1 = __Menu_SendMessage(param_2, 0x40, 0, 0);

            switch(lVar1) {
                case 0:
                    Manager_SendMessage(&manager[param_1], 0x44, 0x4000003a, 0);
                    return 1;
                case 1:
                    Manager_SendMessage(&manager[param_1], 0x44, 0x4000003b, 0);
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
        // Note - there is a bug in Menu_UpdateWheel that causes a crash if the M_ITEM array has fewer than 999 elements
        // We can work around this by allocating a dummy array immediately after the ds_options array
        Menu_UpdateWheel(param_1, param_2, (M_ITEM*)ds_options, 0x1000010d, 0x1000010a, 0x100001ed, 0x1000010b, param_4 == 0x49);
        return 1;

    case 0x51:
        __Menu_SendMessage(param_2, 0x27, 0, 3);
        return 1;

    default:
        return 1;
    }
    
}