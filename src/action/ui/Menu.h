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
void Menu_DeleteSprite(sprite* spr);
void Menu_ClearStack(M_MANAGER *mgr);
void Menu_ChangeControllerStyle(ushort playerNum, int controllerStyle);
void Menu_AddItemsToControl(M_CONTROL *control, M_ITEM *itemList, ushort numItems, ushort firstItemIdx, uchar unlockEverything);
void Menu_ProcessDelayedMessages(void);
void __stdcall Menu_ClearDelayedMessages(void);
M_ITEM* Menu_GetItemFromHash(M_ITEM *list, int hashcodeToMatch, uint numItems);
void __stdcall Menu_RestartFrontEndLoop(void);
void __stdcall Menu_StopFrontEndMusic(void);
void Menu_UpdateMessageBox(uchar managerNum, ushort mask, byte slot);
undefined4 Menu_UpdateOptionBox(undefined4 *type);
void Menu_CreateOptionBoxLabel(byte managerNum, Action_TranslatedText text, undefined4 type, char param_4, char param_5);

int __Menu_SendEx(byte managerNum, HASHCODE hashcode, uint id, uint message, int arg1, int arg2);
int __Menu_SendEx2(byte managerNum, HASHCODE hashcode, uint bit, uint message, int arg1, int arg2);
int __Menu_SendMessage(M_CONTROL *control, uint message, int arg1, int arg2);
uint __Menu_Send(uchar managerNum, HASHCODE hashcode, uint message, int arg1, int arg2);
void __Menu_SendDelayed(int delayDuration, byte managerNum, HASHCODE controlHashcode, undefined4 arg1, undefined4 arg2, undefined4 arg3);
undefined4 __Menu_SendDelayedMessage(uint duration,M_CONTROL *control,uint arg1,int arg2,int arg3);

bool Menu_IsBotGood(uint idx);
void __stdcall Menu_PrepareBots(void);
void Menu_CreateOptionBox(byte managerNum, int **text, undefined4 type, char param_4, char param_5);
bool Menu_HasMedal(HASHCODE hc, uint level, uchar maybePlayerNum);


// Rewards (MenuUnlocks.cpp; docs/ui/secrets.md)
#pragma pack(push, 1)
typedef struct REWARDINFO_tag {
    uint objType;       // REWARD_*
    uint objId;
    int upgradeLevel;   // for an upgrade: how many of that object's upgrade bits are set
    bool hasMedal;      // the reward's bit is set in the bonus
    char pad[3];
} REWARDINFO_tag;
#pragma pack(pop)
static_assert(sizeof(REWARDINFO_tag) == 0x10, "REWARDINFO_tag is 0x10 bytes");

#define REWARD_MP_CHARACTER 1
#define REWARD_MP_SCENARIO  2
#define REWARD_MP_MODIFIER  3
#define REWARD_UPGRADE      4

void PlrStarts_ProcessRewardCounter(ulong *bonus, HASHCODE hashcode, uint upgradeLevel, char set, REWARDINFO_tag *op);
uint __stdcall Menu_GetNightfireStatus(void);
void Menu_SetNightfireStatus(uint status);
void Menu_SetBonus(uint lo, uint hi, byte player, char orIn);
bool Menu_SetLevelBonus(int level, uint medal, byte player);
undefined4 Menu_GetObjectUpgradeLevel(uint objId, byte player);
void Menu_UnlockMPSkins(byte player);
void Menu_UnlockMPSettings(void);
ulonglong Menu_GetMPSkins(int managerNum, byte agent, char param_3);

// The codename load/save in progress, which Menu_UpdateMessageBox runs from a page's update.
#pragma pack(push, 1)
typedef struct MENU_LS {
    uint busy;                  // 0x00 a load or save is set up (Menu_UpdateMessageBox does nothing otherwise)
    uint numCodenames;          // 0x04 saved codenames (codename_buf)
    HASHCODE returnPage;        // 0x08 the page to go to when it is done
    int returnPageArg;          // 0x0c the GoPage flags that go with it
    uint slot;                  // 0x10 the save slot; 999 = a new codename
    M_CONTROL *control;         // 0x14 the wheel an enumeration fills
    char codename[32];          // 0x18
    char pad_38[1];
    uchar operation;            // 0x39 LS_OPERATION_*
    uchar done;                 // 0x3a the operation is over (or, for a page, its list is ready)
    uchar listStale;            // 0x3b the codenames list must be enumerated again
    char pad_3c[1];
    uchar canSave;              // 0x3d saving is allowed (Menu_UpdateMessageBox refuses otherwise)
    char pad_3e[2];
    uchar doMiniMission;        // 0x40
    uchar relatedToMainMenuSound; // 0x41
    uchar field17_0x42;         // 0x42
    char pad_43[5];
} MENU_LS;
#pragma pack(pop)
static_assert(sizeof(MENU_LS) == 0x48, "MENU_LS is 0x48 bytes");
#define ls (*(MENU_LS *)0x0017d540)

// The two default codenames, slots 0 and 1 of the codenames list (Menu_MapDefaultCodename applies one)
#pragma pack(push, 1)
typedef struct DEFAULT_CODENAME {
    Action_TranslatedText name;     // 0x00 CODENAME_NEW, CODENAME_DEFAULT
    uint nightfireStatus;           // 0x04 the missions open
    uint bonusLo;                   // 0x08
    uint bonusHi;                   // 0x0c
    int musicVolume;                // 0x10
    int sfxVolume;                  // 0x14
    uchar subtitles;                // 0x18
    uchar weaponUpgrade;            // 0x19 GameState.WeaponUpgradeRelated
    uchar mpSomeField2;             // 0x1a MPSettings_PerPlayer.SomeField2
    char pad_1b;
    short healthModifier;           // 0x1c
    uchar inverted;                 // 0x1e
    char pad_1f;
    short controlStyle;             // 0x20
    short controlStyleDriving;      // 0x22
    uchar autoAim;                  // 0x24
    uchar autoAimMultiplayer;       // 0x25
    uchar manualAimToggle;          // 0x26
    uchar crouchToggle;             // 0x27
    uchar vibrationEnabled;         // 0x28
    uchar crosshairsEnabled;        // 0x29
    uchar autoSwitchBetterWeapon;   // 0x2a
    uchar input_0c;                 // 0x2b PlayerInput +0x0c
    uchar input_0b;                 // 0x2c PlayerInput +0x0b
    uchar multiplayerLayout;        // 0x2d
    uchar tripleDamage;             // 0x2e MPSettings.TripleDamageModifierProfessionalMode
    uchar respawnMode;              // 0x2f MPSettings.RespawnSelectionMode
    uchar unknown30[8];             // 0x30
} DEFAULT_CODENAME;
#pragma pack(pop)
static_assert(sizeof(DEFAULT_CODENAME) == 0x38, "DEFAULT_CODENAME is 0x38 bytes");
#define def_codename ((DEFAULT_CODENAME *)0x0017d588)  // [2]
#define CODENAME_SLOTS_DEFAULT 2

// The codenames list: the two defaults, then the saved ones as Menu_EnumSaves found them (ls.numCodenames in all)
#define codename_buf ((char (*)[0x20])0x002251b8)

uint Menu_EnumSaves(uchar managerNum, char firstOnly);
void Menu_FlushEnum(void);
void Menu_PutCodenamesIntoControl(M_CONTROL *control);
void Menu_MapDefaultCodename(byte player, byte slot, uint parts);
void Menu_SelectCodenameInControl(byte managerNum, char *codename, HASHCODE control);
Action_TranslatedText Menu_IsValidCodename(char *name);
void _Menu_StartLS(char *codename, int operation, char stale, uchar managerNum, byte slot, ushort mask, uint lsSlot,
                   HASHCODE returnPage, int returnPageArg, M_CONTROL *control, uchar done);
void Menu_StartLS(void);   // the original's register arguments, for _Menu_StartLS

// The codename being edited
#define cn_modified            U8_AT(0x0025d7dd)       // it has changes not yet saved
extern uint8_t cn_secret_mode; // P_CNNAME was entered from the codename menu (Secret Unlocks)

#define LS_OPERATION_LOAD 0
#define LS_OPERATION_SAVE 1
#define LS_OPERATION_ENUM 2         // list the saved codenames into ls.control
#define LS_OPERATION_DELETE 3
#define LS_OPERATION_ENUM_FIRST 4   // only the first short enough

#define LS_SLOT_NEW 999             // ls.slot: a codename not saved yet

// Option boxes (Menu_CreateOptionBox): the type a page's update gets back from Menu_UpdateOptionBox, to tell
// its boxes apart.
#define OPTIONBOX_OVERWRITE_CODENAME 4   // "This codename already exists..."
#define OPTIONBOX_BAD_CODENAME       6   // Menu_IsValidCodename's complaint
#define OPTIONBOX_UNLOCK_SUCCESS     0xc // "Unlock successful"
#define OPTIONBOX_LOSE_CHANGES       0xd // "Are you sure you want to exit and lose your changes?"

extern uint8_t mp_explosive_scenery_unlocked; // defined in MenuUnlocks.cpp

// The framework (Menu.cpp): skins, finding controls, dispatch, input, sprites, sounds
MENU_COMPONENTSET* Component_AddSet(uchar managerNum, ushort id);
void Component_InitComponents(void);
void Component_DestroyAll(uchar managerNum);
MENU_COMPONENTPIECE* Component_AddComponent(MENU_COMPONENT *component, short left, short top, short right, short bottom,
                                            HASHCODE texture, short count);
void Component_InitInstance(MENU_COMPONENTPIECE *piece, short x, short y, short width, short height, short u, short v,
                            short uw, short vh, uint flags, uint colour, float stretchWidth, float stretchHeight);
MENU_COMPONENT* Component_GetHandle(M_CONTROL *control);
void Component_SetupInstance(M_CONTROL *control, uint state, int x, int y, int width, int height);
void Component_Free(M_CONTROL *control);

// Menu_FindControl's directions
#define MENU_DIR_UP     1
#define MENU_DIR_DOWN   2
#define MENU_DIR_LEFT   4
#define MENU_DIR_RIGHT  8

int Menu_CursorOverMe(M_MANAGER *mgr, M_CONTROL *control);
M_CONTROL* Menu_GetControl(M_MANAGER *mgr);
M_CONTROL* Menu_FindControl(M_MANAGER *mgr, uchar direction);
M_CONTROL* Menu_GetFirstControl(M_MANAGER *mgr);
M_PAGE* Menu_GetPage(M_CONTROL *control);
void Menu_Update(M_MANAGER *mgr, M_PAGE *page, M_CONTROL *control);
ushort _Menu_GetAction(short player, int action, uchar flags);
void Menu_GetAction(void);     // the original's register arguments, for _Menu_GetAction
ushort Menu_InputAction(short player, int action, int flags);
void Menu_InitializeSprite(sprite *spr, uchar priority, short x, short y, short width, short height, char *text,
                           HASHCODE texture, uint colour, uint colour2, ushort u, ushort v, ushort uw, ushort vh,
                           ushort flags);
void Menu_ResizeControl(M_CONTROL *control, short x, short y, short width, short height);
void Menu_ResizeControl(M_CONTROL *control, M_CONTROL *from);
int Menu_ClipString(sprite *spr, int width, char *format);
sprite* Menu_CreateSprite(SpriteInfo *info);
void* Menu_Malloc(int size);
void Menu_Free(void **data, undefined4 type);
void Menu_SetAction(short player, int action, uchar state);
uint Menu_GetLastController(void);
uint Menu_GetNoInputCount(void);
void Menu_ResetNoInputCount(void);
void Menu_AlphaUpdate(void);
void Menu_AlphaReset(void);
short Menu_AlphaGet(void);
void Menu_EnableSounds(HASHCODE page);
void Menu_PlaySound(uint sound);
void Menu_UpdateTextFmt(void);
void Menu_GetNormalTextFmt(sprite *spr);

#endif // MENU_H