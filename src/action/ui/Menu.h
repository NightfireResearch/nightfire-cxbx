#ifndef MENU_H
#define MENU_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

typedef uint MENU_IRISOPS;

int Menu_GetLevelIndex(HASHCODE level);
bool Menu_IsDrivingLevel(HASHCODE level);
void Menu_StartIris(MENU_IRISOPS op, uchar managerNum, uint label);
void Menu_PlayIris(char tick, uchar managerNum, uint label);
void Menu_ChangePageCloseIris(HASHCODE page, uchar managerNum, uint label);
void Menu_UpdateWheel(uchar managerNum, M_CONTROL *ctrl, M_ITEM *itemList, HASHCODE wheel, HASHCODE icon,
                      HASHCODE descriptionLabel, HASHCODE iris, bool doAnimation);
bool Menu_SelectItemInControl(M_CONTROL *control, M_ITEM *list, ushort size, int value);
void Menu_DeleteSprite(sprite* spr);
void Menu_ClearStack(M_MANAGER *mgr);
void Menu_ChangeControllerStyle(ushort playerNum, int controllerStyle);
void Menu_AddItemsToControl(M_CONTROL *control, M_ITEM *itemList, ushort numItems, ushort firstItemIdx, uchar unlockEverything);
void Menu_ProcessDelayedMessages(void);
void __stdcall Menu_ClearDelayedMessages(void);
M_ITEM* Menu_GetItemFromHash(M_ITEM *list, int value, uint numItems);
void __stdcall Menu_RestartFrontEndLoop(void);
void __stdcall Menu_StopFrontEndMusic(void);
void Menu_UpdateMessageBox(uchar managerNum, ushort mask, byte slot);
uint Menu_UpdateOptionBox(uint *type);
void Menu_CreateOptionBox(uchar managerNum, const char *text, uint type, char hasAnswer, char canCancel);
void Menu_CreateOptionBoxLabel(uchar managerNum, Action_TranslatedText text, uint type, char hasAnswer, char canCancel);

intptr_t __Menu_SendEx(byte managerNum, HASHCODE hashcode, uint id, uint message, intptr_t arg1, intptr_t arg2);
intptr_t __Menu_SendEx2(byte managerNum, HASHCODE hashcode, uint bit, uint message, intptr_t arg1, intptr_t arg2);
intptr_t __Menu_SendMessage(M_CONTROL *control, uint message, intptr_t arg1, intptr_t arg2);
uintptr_t __Menu_Send(uchar managerNum, HASHCODE hashcode, uint message, intptr_t arg1, intptr_t arg2);
void __Menu_SendDelayed(int delayDuration, byte managerNum, HASHCODE controlHashcode, undefined4 arg1, intptr_t arg2, intptr_t arg3);
undefined4 __Menu_SendDelayedMessage(uint duration,M_CONTROL *control,uint arg1,intptr_t arg2,intptr_t arg3);

bool Menu_IsBotGood(uint idx);
void __stdcall Menu_PrepareBots(void);
void __stdcall Menu_StoreMPSettings(void);
void __stdcall Menu_RestoreMPSettings(void);
bool __stdcall Menu_AllJoinedPlayersReady(void);
void Menu_MPFadeWhenReady(M_CONTROL *control);
int Menu_GetMPScore(byte participant);
bool Menu_HasMedal(HASHCODE level, uint medal, uchar player);
uint64_t Menu_GetBonus(byte player);

// The front end (ui_frontend.cpp): the movies a page plays, the language, the mini mission
void Menu_PlayMovie(HASHCODE movie, uchar skippable, int looping, uchar stopMusic, char lockInput, uint skipDelay);
void Menu_StopMovie(char force);
void Menu_UpdateMovieCounter(void);
bool psiMovieLoop(void);
void Menu_SetSoundLanguage(int language);
int Menu_GetLanguageButton(int language);
uchar Menu_WillRunMiniMission(void);
uchar Menu_RunMiniMission(uchar managerNum);
// What Menu_PlayMovie keeps of the movie it started (psiMovieLoop and Menu_RestartFrontEndLoop still read them)
extern uint8_t menu_music_playing;  // the front end's music has been started
extern uint8_t menu_movie_skippable;  // Menu_StopMovie may stop it
extern uint8_t menu_movie_stopped_music;  // the front end's music was stopped for it
extern uint32_t menu_movie_skip_delay;  // frames before it can be skipped

// The codename pages (ui_codenames.cpp)
void Menu_UpdateCodenameWheel(uchar managerNum, M_CONTROL *wheel, HASHCODE text, HASHCODE icon, HASHCODE description,
                              HASHCODE iris, char turning);
void Menu_UpdateDefaultCodename(uchar player, uchar slot);
bool Menu_CodenameExists(const char *name);
int LS_GetSaveSize(void);
bool Menu_CheckSaveSpace(uchar managerNum, struct MENU_LS *unused);     // false, with a box, without room for a codename
void Menu_GoToDashboard(void);                                   // to make the room it asked for

// The controller styles (ui_options.cpp)
void Menu_DisplayControllerStyle(uchar managerNum, uint style);
void Menu_DisplayControllerStyleList(uchar managerNum, uint style);
void Menu_UpdateControllerLabelString(uchar managerNum, const char *button, Action_TranslatedText line,
                                      const char *button2, Action_TranslatedText line2);

// The results after a mission (ui_nightfire.cpp)
void Menu_AddRow(uchar managerNum, Action_TranslatedText label, int achieved, int target, uint points, char indent,
                 char isTime);
void Menu_AddRowPercentage(uchar managerNum, Action_TranslatedText label, uint percent, int extra, uint points,
                           char indent);

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

// The rewards of each mission: a medal (1-4) gives the reward of its slot, named and pictured on the results pages
typedef union LEVEL_REWARD {
    uint value;
    struct {
        ushort bit;             // its bit in a bonus (an upgrade's first)
        ushort type;            // REWARD_*; 0 none
    };
} LEVEL_REWARD;
static_assert(sizeof(LEVEL_REWARD) == 4, "LEVEL_REWARD is 4 bytes");

typedef struct LEVEL_REWARDS {
    HASHCODE level;                 // 0x00
    LEVEL_REWARD rewards[5];        // 0x04 by medal; slot 0 is none
    uint unknown18;
    Action_TranslatedText names[4]; // 0x1c by medal less one
    uint unknown2c;
    HASHCODE icons[4];              // 0x30
} LEVEL_REWARDS;
static_assert(sizeof(LEVEL_REWARDS) == 0x40, "LEVEL_REWARDS is 0x40 bytes");

#define REWARDS_TABLE_SIZE 12
extern const LEVEL_REWARDS rewards_table[REWARDS_TABLE_SIZE];

// What Menu_GetLevelBonuses hands back: each medal's reward, and whether the first player has it
typedef struct LEVEL_BONUSES {
    uint unknown00;
    struct {
        uchar earned;
        char pad[3];
        Action_TranslatedText name;
        HASHCODE icon;
    } medals[4];
} LEVEL_BONUSES;
static_assert(sizeof(LEVEL_BONUSES) == 0x34, "LEVEL_BONUSES is 0x34 bytes");

uint PlrStarts_GetRewardsTableSize(void);
LEVEL_BONUSES* Menu_GetLevelBonuses(HASHCODE level);
int Menu_GetBestMedal(HASHCODE level, uchar player);

void PlrStarts_ProcessRewardCounter(uint32_t *bonus, HASHCODE hashcode, uint upgradeLevel, char set, REWARDINFO_tag *op);
uint __stdcall Menu_GetNightfireStatus(void);
void Menu_SetNightfireStatus(uint status);
void Menu_SetBonus(uint lo, uint hi, byte player, char orIn);
bool Menu_SetLevelBonus(int level, uint medal, byte player);
undefined4 Menu_GetObjectUpgradeLevel(uint objId, byte player);
void Menu_UnlockMPSkins(byte player);
void Menu_UnlockMPSettings(void);
ulonglong Menu_GetMPSkins(int managerNum, byte agent, char taken);
void Menu_UpdateMPControllers(uchar managerNum, uchar player, char joinPage);
char* Menu_GetBotShortName(BotNum skin);

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
    uchar mpPlayer;             // 0x38 P_MPJOIN: the player (from 1) whose codename is loading
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
LAYOUT_32(sizeof(MENU_LS) == 0x48, "MENU_LS is 0x48 bytes");
void XBox_DoSaveFlow(M_MANAGER *mgr, uint state, MENU_LS *operation);
extern MENU_LS ls;

#pragma pack(push, 1)
// A control Menu_FindControl could move to, and how far it is (QuickSort's Compare_MenuCtrl sorts by the cost)
typedef struct MENU_NEIGHBOUR {
    M_CONTROL *control;
    short cost;
    char pad[2];
} MENU_NEIGHBOUR;
#pragma pack(pop)
LAYOUT_32(sizeof(MENU_NEIGHBOUR) == 8, "MENU_NEIGHBOUR is 8 bytes");

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
    uchar showNames;                // 0x30 MPSettings.ShowTeamAndNameOverhead
    uchar unknown31[7];             // 0x31
} DEFAULT_CODENAME;
#pragma pack(pop)
static_assert(sizeof(DEFAULT_CODENAME) == 0x38, "DEFAULT_CODENAME is 0x38 bytes");
extern DEFAULT_CODENAME def_codename[2];
#define CODENAME_SLOTS_DEFAULT 2

// The codenames list: the two defaults, then the saved ones as Menu_EnumSaves found them (ls.numCodenames in all).
// The original has no fixed count: the room before the next global holds 4098.
extern char codename_buf[CODENAME_SLOTS_DEFAULT + 4096][0x20];   // Menu.cpp

uint Menu_EnumSaves(uchar managerNum, char firstOnly);
void Menu_FlushEnum(void);
void Menu_PutCodenamesIntoControl(M_CONTROL *control);
void Menu_MapDefaultCodename(byte player, byte slot, uint parts);
void Menu_SelectCodenameInControl(byte managerNum, char *codename, HASHCODE control);
Action_TranslatedText Menu_IsValidCodename(char *name);
void Menu_StartLS(char *codename, int operation, char stale, uchar managerNum, byte slot, ushort mask, uint lsSlot,
                  HASHCODE returnPage, int returnPageArg, M_CONTROL *control, uchar done);

// The codename being edited
extern uint8_t cn_modified;  // it has changes not yet saved
extern uint8_t cn_secret_mode; // P_CNNAME was entered from the codename menu (Secret Unlocks)

#define LS_OPERATION_LOAD 0
#define LS_OPERATION_SAVE 1
#define LS_OPERATION_ENUM 2         // list the saved codenames into ls.control
#define LS_OPERATION_DELETE 3
#define LS_OPERATION_ENUM_FIRST 4   // only the first short enough
#define LS_OPERATION_NONE 5         // P_MPJOIN, once a player's codename is loaded

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
ushort Menu_GetAction(short player, int action, uchar flags);
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
