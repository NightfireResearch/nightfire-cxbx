#include "Menu.h"
#include "Manager.h"

#include "../game/drone/BOT.h"


#include <stdio.h>
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
void __Menu_SendDelayed(int param_1,byte param_2,HASHCODE param_3,undefined4 param_4,undefined4 param_5,undefined4 param_6);

// AUTOGEN
int __Menu_SendEx(byte param_1,HASHCODE param_2,uint itemNum, uint param_4,int **param_5,int **param_6);

// Exists on PS2 at 00202fd8, inlined on Xbox
bool Menu_IsBotGood(uint idx) {
    BOT_stats_t *stats = BOT_getDefaultStats(idx);
    return stats->isBad == 0;
}

// Size unclear
#define buf_171 (*(char*)0x002250b8)

// AUTOINJECT
void Menu_UpdateWheel(uchar managerNum, M_CONTROL *ctrl, M_ITEM *itemList, HASHCODE param_4, HASHCODE param_5, HASHCODE param_6, HASHCODE param_7, bool maybeDoAnimation) {

  int iVar2;
  const char *pcVar4;
  const char *pcVar5;
  const char *pcVar6;
  Action_TranslatedText AVar7;

  // The index of the item in the list that is currently selected, and its 2 surrounding items
  int idxUp2;
  int idxUp1;
  const int idxMid = __Menu_SendMessage(ctrl,0x40,0,0);
  int idxDown1;
  int idxDown2;

  // 5 strings, representing the middle item, and its 2 surrounding items
  const char* strUp2;
  const char* strUp1;
  const char* strMid;
  const char* strDown1;
  const char* strDown2;
  
  if (ctrl == NULL) 
    return;

  if (ctrl->type == ControlType_Scroll) {
    ctrl->field_0x144 = 0;
  }

  const int last_item_idx = __Menu_SendMessage(ctrl,0x38,0,0);

  if (param_5 != 0) {
    if (maybeDoAnimation) {
      __Menu_SendDelayed(10,managerNum, param_5, 0x24,itemList[idxMid].iconHashcode,0);
      __Menu_SendDelayed(10,managerNum, param_5, 0x1a,
                   (-(uint)(itemList[idxMid].enabled != false) & 0x2020209f) + 0x60606060,0);
    }
    else {
      __Menu_Send(managerNum, param_5, 0x24, itemList[idxMid].iconHashcode, 0);
      __Menu_Send(managerNum, param_5, 0x1a,
                  (-(uint)(itemList[idxMid].enabled != false) & 0x2020209f) + 0x60606060, 0);
    }
  }


  if (param_6 == 0) goto LAB_0007f86e;


  if (*(HASHCODE *)(manager[managerNum].field158_0x1bc + 0x18) == P_MPBOTCHOOSE) {
    if (itemList[idxMid].enabled == false) {
      AVar7 = itemList[idxMid].descriptionWhenDisabled;
LAB_0007f83e:
      idxDown1 = 0;
      pcVar4 = Txt_BindLabel(AVar7,0);
    }
    else {
      pcVar4 = Txt_BindLabel(Menu_IsBotGood(itemList[idxMid].identifier) ? MP_TEAM_MI6 : MP_TEAM_PHOENIX, 0);
      pcVar5 = Txt_BindLabel(MP_TEAM, 0);
      pcVar6 = Txt_BindLabel(itemList[idxMid].description, 0);
      sprintf(&buf_171, "%s\n%s : %s", pcVar6, pcVar5, pcVar4);
      idxDown1 = 0;
      pcVar4 = &buf_171;
    }
  }
  else {
    idxDown1 = 0;
    if (itemList[idxMid].enabled != false) {
      AVar7 = itemList[idxMid].description;
      goto LAB_0007f83e;
    }
    pcVar4 = Txt_BindLabel(itemList[idxMid].descriptionWhenDisabled,0);
  }


  __Menu_Send(managerNum, param_6, MessageType_SetText, (int)pcVar4, idxDown1);


  LAB_0007f86e:

  // Work out the indices of the items to display in the wheel, managing the ends of the array appropriately
  idxUp1 = idxMid - 1;
  idxUp2 = idxMid - 2;
  idxDown1 = idxMid + 1;
  idxDown2 = idxMid + 2;
  if(idxUp1 < 0) {
    idxUp1 = 999;
  }
  if(idxUp2 < 0) {
    idxUp2 = 999;
  }
  if(idxDown1 > last_item_idx) {
    idxDown1 = 999;
  }
  if(idxDown2 > last_item_idx) {
    idxDown2 = 999;
  }
  
  // Obtain and set the strings up
  strUp2 = (idxUp2 == 999) ? " " : Txt_BindLabel(itemList[idxUp2].title, 0);
  strUp1 = (idxUp1 == 999) ? " " : Txt_BindLabel(itemList[idxUp1].title, 0);
  strMid = (idxMid == 999) ? " " : Txt_BindLabel(itemList[idxMid].title, 0);
  strDown1 = (idxDown1 == 999) ? " " : Txt_BindLabel(itemList[idxDown1].title, 0);
  strDown2 = (idxDown2 == 999) ? " " : Txt_BindLabel(itemList[idxDown2].title, 0);

  __Menu_SendEx(managerNum, param_4, 0, MessageType_SetText, (int)strUp2, NULL);
  __Menu_SendEx(managerNum, param_4, 1, MessageType_SetText, (int)strUp1, NULL);
  __Menu_SendEx(managerNum, param_4, 2, MessageType_SetText, (int)strMid, NULL);
  __Menu_SendEx(managerNum, param_4, 3, MessageType_SetText, (int)strDown1, NULL);
  __Menu_SendEx(managerNum, param_4, 4, MessageType_SetText, (int)strDown2, NULL);
  
  // Handle greyed-out disabled items
  const int maybeColourDisabled = 0x48484880;
  const int maybeColourEnabled = 0x7d6d5aff;

  // Previously, the game wouldn't check for 999 resulting in an out-of-bounds read. Now fixed.
  if(idxUp2 != 999)
    __Menu_SendEx(managerNum, param_4, 0, MessageType_SetColour, itemList[idxUp2].enabled ? maybeColourEnabled : maybeColourDisabled, NULL);      
  if(idxUp1 != 999)
    __Menu_SendEx(managerNum, param_4, 1, MessageType_SetColour, itemList[idxUp1].enabled ? maybeColourEnabled : maybeColourDisabled, NULL);
  if(idxDown1 != 999)
    __Menu_SendEx(managerNum, param_4, 3, MessageType_SetColour, itemList[idxDown1].enabled ? maybeColourEnabled : maybeColourDisabled, NULL);
  if(idxDown2 != 999)
    __Menu_SendEx(managerNum, param_4, 4, MessageType_SetColour, itemList[idxDown2].enabled ? maybeColourEnabled : maybeColourDisabled, NULL);

  // The middle item can't be offscreen, no need to check
  __Menu_SendEx(managerNum, param_4, 2, MessageType_SetColour, itemList[idxMid].enabled ? maybeColourEnabled : maybeColourDisabled, NULL);


  if (maybeDoAnimation) {
    // Inlined Menu_StartIris(2, param_1, param_7)
    // DAT_0025d7b9 = '\0';
    // DAT_0025d7b8 = 0;
    // DAT_0025d7b0 = 1;
    // DAT_0025d7ac = 0;
    // Menu_PlayIris('\0',managerNum,param_7);
    // DAT_0025d7ac = 0xffffffff;
    Menu_StartIris(2, managerNum, param_7);
  }

}

// AUTOGEN
int __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);

// AUTOGEN
uint __Menu_Send(uchar param_1, HASHCODE param_2, uint param_3, int param_4, int param_5);

// AUTOGEN
bool Menu_SelectItemInControl(M_CONTROL* control, M_ITEM *list, ushort size, int idx);

// AUTOGEN
void Menu_UnlockMPSettings(void);