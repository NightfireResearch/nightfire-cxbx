#ifndef MENU_H
#define MENU_H

#include "../actionhelpers.h"

typedef uint MENU_IRISOPS;

int Menu_GetLevelIndex(HASHCODE level);
bool Menu_IsDrivingLevel(HASHCODE level);
void Menu_StartIris(MENU_IRISOPS param_1, uchar param_2, uint param_3);
void Menu_PlayIris(char param_1, uchar param_2, uint param_3);
void Menu_ChangePageCloseIris(HASHCODE param_1, uchar param_2, uint param_3);
void Menu_UpdateWheel(uchar param_1, M_CONTROL *param_2, M_ITEM *param_3, HASHCODE param_4, HASHCODE param_5, HASHCODE param_6, HASHCODE param_7, bool param_8);
bool Menu_SelectItemInControl(M_CONTROL* control, M_ITEM *list, ushort size, int idx);
void Menu_UnlockMPSettings(void);
void Menu_DeleteSprite(sprite* spr);
void Menu_ClearStack(M_MANAGER *mgr);
void Menu_ChangeControllerStyle(ushort playerNum, int controllerStyle);
undefined4 Menu_GetObjectUpgradeLevel(uint param_1,byte param_2);


int __Menu_SendEx(byte param_1,HASHCODE param_2,uint itemNum,uint param_4, int param_5, int param_6);
int __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);
uint __Menu_Send(uchar param_1, HASHCODE param_2, uint param_3, int param_4, int param_5);
void __Menu_SendDelayed(int param_1,byte param_2,HASHCODE param_3,undefined4 param_4,undefined4 param_5,undefined4 param_6);

#endif // MENU_H