#ifndef MENU_H
#define MENU_H

#include "../actionhelpers.h"

typedef uint MENU_IRISOPS;

int Menu_GetLevelIndex(HASHCODE level);
void Menu_StartIris(MENU_IRISOPS param_1, uchar param_2, uint param_3);
void Menu_PlayIris(char param_1, uchar param_2, uint param_3);
void Menu_ChangePageCloseIris(HASHCODE param_1, uchar param_2, uint param_3);
void Menu_UpdateWheel(uchar param_1, M_CONTROL *param_2, M_ITEM *param_3, uint param_4, uint param_5, HASHCODE param_6, uint param_7, char param_8);

int __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);
uint __Menu_Send(uchar param_1, HASHCODE param_2, uint param_3, int param_4, int param_5);

#endif // MENU_H