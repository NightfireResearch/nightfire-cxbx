#include "Menu.h"

// Menu (Send, SendEx, SendMessage), Iris (Start, Play), Wheel etc

#define sp_level (*(M_ITEM(*)[12])0x0017c580)

// AUTOINJECT
int Menu_GetLevelIndex(HASHCODE level) {

    for(int i = 0; i < ARRAY_SIZE(sp_level); i++) {
        if(sp_level[i].identifier == level) {
            return i;
        }
    }
    return -1;
}

// AUTOGEN
void Menu_StartIris(MENU_IRISOPS param_1, uchar param_2, uint param_3);

// AUTOGEN
void Menu_PlayIris(char param_1, uchar param_2, uint param_3);

// AUTOGEN
void Menu_ChangePageCloseIris(HASHCODE param_1, uchar param_2, uint param_3);



// NOTE: Original game has a bug in Menu_UpdateWheel - when running under Windows with modern memory protection, the game crashes when the item 
// array is in injected code/memory, but NOT if it is in the original game code

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

// AUTOGEN
void Menu_UpdateWheel(uchar param_1, M_CONTROL *param_2, M_ITEM *param_3, HASHCODE param_4, HASHCODE param_5, HASHCODE param_6, HASHCODE param_7, bool param_8);

// AUTOGEN
int __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);

// AUTOGEN
uint __Menu_Send(uchar param_1, HASHCODE param_2, uint param_3, int param_4, int param_5);

// AUTOGEN
bool Menu_SelectItemInControl(M_CONTROL* control, M_ITEM *list, ushort size, int idx);

// AUTOGEN
void Menu_UnlockMPSettings(void);