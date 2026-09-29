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
void Menu_AddItemsToControl(M_CONTROL *control, M_ITEM *itemList, ushort numItems, ushort firstItemIdx, uchar unlockEverything);
void Menu_ProcessDelayedMessages(void);
void* Menu_Malloc(int size);
M_ITEM* Menu_GetItemFromHash(M_ITEM *list, int hashcodeToMatch, uint numItems);
undefined4 __stdcall Menu_GetLastController(void);
void __stdcall Menu_RestartFrontEndLoop(void);
void __stdcall Menu_StopFrontEndMusic(void);
void Menu_UpdateMessageBox(uint managerNum, ushort param_2, byte param_3);
undefined4 Menu_UpdateOptionBox(undefined4 *type);
void Menu_CreateOptionBoxLabel(byte managerNum, Action_TranslatedText text, undefined4 type, char param_4, char param_5);

int __Menu_SendEx(byte param_1,HASHCODE param_2,uint itemNum,uint param_4, int param_5, int param_6);
int __Menu_SendMessage(M_CONTROL *param_1, uint param_2, int param_3, int param_4);
uint __Menu_Send(uchar param_1, HASHCODE param_2, uint param_3, int param_4, int param_5);
void __Menu_SendDelayed(int delayDuration, byte managerNum, HASHCODE controlHashcode, undefined4 arg1, undefined4 arg2, undefined4 arg3);
undefined4 __Menu_SendDelayedMessage(uint duration,M_CONTROL *control,uint arg1,int arg2,int arg3);

bool Menu_IsBotGood(uint idx);
void __stdcall Menu_PrepareBots(void);
void Menu_CreateOptionBox(byte managerNum, int **text, undefined4 type, char param_4, char param_5);
void Menu_UnlockMPSkins(byte param_1);
bool Menu_HasMedal(HASHCODE hc, uint level, uchar maybePlayerNum);


// The codename load/save in progress, which Menu_UpdateMessageBox runs from a page's update.
#pragma pack(push, 1)
typedef struct MENU_LS {
    uint busy;                  // 0x00 a load or save is set up (Menu_UpdateMessageBox does nothing otherwise)
    uint numCodenames;          // 0x04 saved codenames (codename_buf)
    HASHCODE returnPage;        // 0x08 the page to go to when it is done
    undefined4 field3_0xc;      // 0x0c
    uint slot;                  // 0x10 the save slot; 999 = a new codename
    undefined4 field5_0x14;     // 0x14
    char codename[32];          // 0x18
    char pad_38[1];
    uchar operation;            // 0x39 LS_OPERATION_*
    uchar field9_0x3a;          // 0x3a
    uchar field10_0x3b;         // 0x3b
    char pad_3c[1];
    uchar field12_0x3d;         // 0x3d
    char pad_3e[2];
    uchar doMiniMission;        // 0x40
    uchar relatedToMainMenuSound; // 0x41
    uchar field17_0x42;         // 0x42
    char pad_43[5];
} MENU_LS;
#pragma pack(pop)
static_assert(sizeof(MENU_LS) == 0x48, "MENU_LS is 0x48 bytes");
#define ls (*(MENU_LS *)0x0017d540)

// The codename being edited
#define cn_modified            U8_AT(0x0025d7dd)       // it has changes not yet saved
#define cn_secret_mode         U8_AT(0x0025d7de)       // P_CNNAME was entered from the codename menu (Secret Unlocks)

#define LS_OPERATION_LOAD 0
#define LS_OPERATION_SAVE 1

// Option boxes (Menu_CreateOptionBox): the type a page's update gets back from Menu_UpdateOptionBox, to tell
// its boxes apart.
#define OPTIONBOX_OVERWRITE_CODENAME 4   // "This codename already exists..."
#define OPTIONBOX_BAD_CODENAME       6   // Menu_ValidateCodename's complaint
#define OPTIONBOX_UNLOCK_SUCCESS     0xc // "Unlock successful"
#define OPTIONBOX_LOSE_CHANGES       0xd // "Are you sure you want to exit and lose your changes?"

#endif // MENU_H