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

// AUTOGEN
void Menu_UpdateWheel(uchar param_1, M_CONTROL *param_2, M_ITEM *param_3, HASHCODE param_4, HASHCODE param_5, HASHCODE param_6, HASHCODE param_7, bool param_8);

// AUTOGEN
int __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);

// AUTOGEN
uint __Menu_Send(uchar param_1, HASHCODE param_2, uint param_3, int param_4, int param_5);

// AUTOGEN
bool Menu_SelectItemInControl(M_CONTROL* control, M_ITEM *list, ushort size, int idx);