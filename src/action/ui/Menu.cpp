#include "Menu.h"
#include "Manager.h"

#include "../game/drone/BOT.h"
#include "../input.h"


#include <stdio.h>
// Menu (Send, SendEx, SendMessage), Iris (Start, Play), Wheel etc


// AUTOINJECT
int Menu_GetLevelIndex(HASHCODE level) {

    for(int i = 0; i < ARRAY_SIZE(sp_level); i++) {
        if(sp_level[i].identifier == level) {
            return i;
        }
    }
    return -1;
}

// AUTOINJECT
bool Menu_IsDrivingLevel(HASHCODE level) {
  switch(level) {
      case HT_Level_Driving_Paris:
      case HT_Level_Driving_Underwater:
      case HT_Level_Driving_JungleA:
      case HT_Level_Driving_SnowMobile:
      case HT_Level_Driving_Alps:
          return true;
  }
  return false;
}

// AUTOGEN
void Menu_StartIris(MENU_IRISOPS param_1, uchar param_2, uint param_3);

// AUTOGEN
void Menu_PlayIris(char param_1, uchar param_2, uint param_3);

// AUTOGEN
void Menu_ChangePageCloseIris(HASHCODE param_1, uchar param_2, uint param_3);

typedef struct {
  M_CONTROL* control;
  uint dispatchOnFrameNum;
  uint param1;
  uint param2;
  uint param3;
} DelayedMessage;

#define menu_delay_frame U32_AT(0x00224540)
#define menu_delay_msg (*(DelayedMessage(*)[128])(0x00223b40))

// AUTOINJECT
undefined4 __Menu_SendDelayedMessage(uint duration,M_CONTROL *control,uint arg1,int arg2,int arg3) {

  for(int i = 0; i < ARRAY_SIZE(menu_delay_msg); i++) {

    // Is the item at this index expired?
    if(menu_delay_msg[i].dispatchOnFrameNum < menu_delay_frame) {

      // Insert this item and return
      menu_delay_msg[i].control = control;
      menu_delay_msg[i].dispatchOnFrameNum = menu_delay_frame + duration;
      menu_delay_msg[i].param1 = arg1;
      menu_delay_msg[i].param2 = arg2;
      menu_delay_msg[i].param3 = arg3;

      return true;

    }

  }

  return false;
}

// AUTOINJECT
void __Menu_SendDelayed(int delayDuration, byte managerNum, HASHCODE controlHashcode, undefined4 arg1, undefined4 arg2, undefined4 arg3) {

  M_CONTROL* control = CONTROL_GET(managerNum, controlHashcode);
  
  if(control == NULL)
    return;

  __Menu_SendDelayedMessage(delayDuration, control, arg1, arg2, arg3);

}

// AUTOINJECT
void Menu_ProcessDelayedMessages(void) {

  menu_delay_frame++;

  for(int i = 0; i < ARRAY_SIZE(menu_delay_msg); i++) {
    DelayedMessage msg = menu_delay_msg[i];
    if(msg.dispatchOnFrameNum == menu_delay_frame) {
      __Menu_SendMessage(msg.control, msg.param1, msg.param2, msg.param3);
    }
  }

}

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
void Menu_UpdateWheel(uchar managerNum, M_CONTROL *ctrl, M_ITEM *itemList, HASHCODE param_4, HASHCODE param_5, HASHCODE descriptionLabel, HASHCODE param_7, bool maybeDoAnimation) {

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
      __Menu_SendDelayed(10,managerNum, param_5, MessageType_SetIcon, itemList[idxMid].iconHashcode,0);
      __Menu_SendDelayed(10,managerNum, param_5, MessageType_SetColour, itemList[idxMid].enabled ? 0x808080ff : 0x60606060,0);
    }
    else {
      __Menu_Send(managerNum, param_5, MessageType_SetIcon, itemList[idxMid].iconHashcode, 0);
      LABEL_SET_COLOUR(managerNum, param_5, itemList[idxMid].enabled ? 0x808080ff : 0x60606060);
    }
  }


  if (descriptionLabel != 0) { 

    if (*(HASHCODE *)(manager[managerNum].field158_0x1bc + 0x18) == P_MPBOTCHOOSE) {

      // Special case when on the MP bot selection menu

      if (!itemList[idxMid].enabled) {
        // Use the disabled description
        pcVar4 = Txt_BindLabel(itemList[idxMid].descriptionWhenDisabled,0);
      }
      else {
        // Merge their team name into the description
        pcVar4 = Txt_BindLabel(Menu_IsBotGood(itemList[idxMid].identifier) ? MP_TEAM_MI6 : MP_TEAM_PHOENIX, 0);
        pcVar5 = Txt_BindLabel(MP_TEAM, 0);
        pcVar6 = Txt_BindLabel(itemList[idxMid].description, 0);
        sprintf(&buf_171, "%s\n%s : %s", pcVar6, pcVar5, pcVar4);
        pcVar4 = &buf_171;
      }
    }

    // Every other menu uses the item list directly - either the normal or disabled description
    else {
      pcVar4 = Txt_BindLabel(itemList[idxMid].enabled ? itemList[idxMid].description : itemList[idxMid].descriptionWhenDisabled, 0);
    }

    LABEL_SET_TEXT(managerNum, descriptionLabel, pcVar4);

  }

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
  
  // Handle greyed-out disabled items. 3 entries representing the distance from the middle (greys out gradually to the edge)
  const int colourDisabled[3] = {0x48484880, 0x48484840, 0x48484820};
  const int colourEnabled[3] = {0x7d6d5aff, 0x7d6d5a80, 0x7d6d5a40};

  // Previously, the game wouldn't check for 999 resulting in an out-of-bounds read. Now fixed.
  if(idxUp2 != 999)
    __Menu_SendEx(managerNum, param_4, 0, MessageType_SetColour, itemList[idxUp2].enabled ? colourEnabled[2] : colourDisabled[2], NULL);
  if(idxUp1 != 999)
    __Menu_SendEx(managerNum, param_4, 1, MessageType_SetColour, itemList[idxUp1].enabled ? colourEnabled[1] : colourDisabled[1], NULL);
  if(idxDown1 != 999)
    __Menu_SendEx(managerNum, param_4, 3, MessageType_SetColour, itemList[idxDown1].enabled ? colourEnabled[1] : colourDisabled[1], NULL);
  if(idxDown2 != 999)
    __Menu_SendEx(managerNum, param_4, 4, MessageType_SetColour, itemList[idxDown2].enabled ? colourEnabled[2] : colourDisabled[2], NULL);

  // The middle item can't be offscreen, no need to check
  __Menu_SendEx(managerNum, param_4, 2, MessageType_SetColour, itemList[idxMid].enabled ? colourEnabled[0] : colourDisabled[0], NULL);


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

// AUTOINJECT
void Menu_DeleteSprite(sprite* spr) {
    if(spr == NULL)
        return;

    Sprite_Delete(spr);
    
}

// AUTOGEN
int __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);

// AUTOGEN
uint __Menu_Send(uchar param_1, HASHCODE param_2, uint param_3, int param_4, int param_5);

// AUTOGEN
bool Menu_SelectItemInControl(M_CONTROL* control, M_ITEM *list, ushort size, int idx);

// AUTOGEN
void Menu_UnlockMPSettings(void);

// AUTOGEN
void Menu_Free(void **data, undefined4 mallocFlags);

// AUTOGEN
undefined4 Menu_GetObjectUpgradeLevel(uint param_1,byte param_2);

#define menu_unlock_everything U8_AT(0x0025d79e)

// AUTOINJECT
void Menu_AddItemsToControl(M_CONTROL *control, M_ITEM *itemList, ushort numItems, ushort firstItemIdx, uchar unlockEverything) { 

  if(itemList == NULL)
    return;

  __Menu_SendMessage(control, MessageType_MaybeInitScroll, 0, 0);

  for(int i = firstItemIdx; i < numItems; i++) {
    M_ITEM* item = &itemList[i];
    if(unlockEverything || menu_unlock_everything || item->enabled) {
      __Menu_SendMessage(control, MessageType_AddTextToScroll, (int)Txt_BindLabel(item->title, 0), (int)item->identifier);
    }
  }

}

// AUTOINJECT
void Menu_ClearStack(M_MANAGER *mgr) {

    while (!Stack_IsEmpty(&mgr->stack)) {
        // The Xbox code takes Stack_Top, then calls Stack_Pop and discards the return value.
        // No need to do that here, we can just pop it which should have the same end result
        // but does so in a much more idiomatic way.
        Menu_Free((void **)Stack_Pop(&mgr->stack), 8);
    }

}

// AUTOINJECT
void Menu_ChangeControllerStyle(ushort playerNum, int controllerStyle) {
  if (playerNum == 0xffff) {
    Input_ChangeControllerStyle(0, controllerStyle);
    Input_ChangeControllerStyle(1, controllerStyle);
    Input_ChangeControllerStyle(2, controllerStyle);
    Input_ChangeControllerStyle(3, controllerStyle);
  } else {
    Input_ChangeControllerStyle(playerNum, controllerStyle);
  }
}