// The debug page's tools (page 0x40000007, which nothing on a retail disc opens): the level list, the tunables of
// P_TWEAKS and P_TWEAKS2, the scripted scenes of C_NIS, and the medal of P_CHEATMEDAL (PS2: menu_tweaks.cpp)

#include "ui.h"
#include "Manager.h"
#include "Menu.h"
#include "MenuManager.h"
#include "MenuCommon.h"

#include "../game.h"
#include "../engine/Camera.h"
#include "../engine/Loader.h"
#include "../engine/Script.h"
#include "../game/drone/DroneTuning.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Player.h"
#include "../game/obj/control.h"
#include "../game/sp/SwitchChannels.h"
#include "../sound/SFX.h"
#include "../util/hashtable.h"

#include <stdio.h>

// Not reimplemented, called by address

// The player's damage modifiers (Player.cpp)
extern float Plr_DMod_Multi;
extern float Plr_DMod_Head;
extern float Plr_DMod_LowerLimb;
extern float Plr_DMod_UpperLimb;
extern float Plr_DMod_Easy;
extern float Plr_DMod_Normal;
extern float Plr_DMod_Hard;

// ---- the level list

// Moving the cursor picks the level, selecting it loads it; the list starts on the level being played
// AUTOINJECT
bool C_LBPMMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_Scroll:
            GameState.ReloadMenupage = P_START;
            GameState.NextLevelHashcode = (HASHCODE)RADIO_GET_VALUE_OF(control);
            break;
        case MessageType_Select:
            MPSettings.numPlayers = 1;
            ResetMap_LevelToLoad((HASHCODE)RADIO_GET_VALUE_OF(control), false, false);
            GameFlow_PushState(7, 80.0f, 0xff);
            SFXFadeDown(1);
            MenuManager_Delete(managerNum);
            break;
        case MessageType_ControlCreated:
            __Menu_SendMessage(control, MessageType_AddItem, (intptr_t)"Main Menu", HT_Level_Menu_Pre);
            __Menu_SendMessage(control, MessageType_SelectItemByValue, GameState.CurrentLevelHashcode, 0);
            break;
    }
    return true;
}

// ---- the tunables

// A tunable on a scroll: the scroll shows it times sliderScale (truncated), and its value times scale is what is
// stored back and printed beside it
typedef struct {
    HASHCODE scroll;
    const char *name;
    float *tunable;
    float sliderScale;
    float scale;
    HASHCODE valueText;
} TWEAK;

static const TWEAK tweaks[18] = {
    {SUB_P_TWEAKS_DRONE_EASY, "Drone Easy", &DroneDamage_Easy, 2.0f, 0.5f, SUB_P_TWEAKS_DRONE_EASY_VALUE},
    {SUB_P_TWEAKS_DRONE_NORMAL, "Drone Normal", &DroneDamage_Normal, 2.0f, 0.5f, SUB_P_TWEAKS_DRONE_NORMAL_VALUE},
    {SUB_P_TWEAKS_DRONE_HARD, "Drone Hard", &DroneDamage_Hard, 2.0f, 0.5f, SUB_P_TWEAKS_DRONE_HARD_VALUE},
    {SUB_P_TWEAKS_DRONE_HEAD, "Drone Head", &DroneDamage_Head, 4.0f, 0.25f, SUB_P_TWEAKS_DRONE_HEAD_VALUE},
    {SUB_P_TWEAKS_DRONE_ARMS, "Drone Arms", &DroneDamage_Arms, 4.0f, 0.25f, SUB_P_TWEAKS_DRONE_ARMS_VALUE},
    {SUB_P_TWEAKS_DRONE_LEGS, "Drone Legs", &DroneDamage_Legs, 4.0f, 0.25f, SUB_P_TWEAKS_DRONE_LEGS_VALUE},
    {SUB_P_TWEAKS_DRONE_TORSO, "Drone Torso", &DroneDamage_Torso, 4.0f, 0.25f, SUB_P_TWEAKS_DRONE_TORSO_VALUE},
    {SUB_P_TWEAKS_ARMOUR_HELMET, "Armour Helmet", &DroneArmour_Helmet, 5.0f, 0.2f, SUB_P_TWEAKS_ARMOUR_HELMET_VALUE},
    {SUB_P_TWEAKS_ARMOUR_COMBAT, "Armour Combat", &DroneArmour_Combat, 5.0f, 0.2f, SUB_P_TWEAKS_ARMOUR_COMBAT_VALUE},
    {SUB_P_TWEAKS_ARMOUR_JACKET, "Armour Jacket", &DroneArmour_Jacket, 5.0f, 0.2f, SUB_P_TWEAKS_ARMOUR_JACKET_VALUE},
    {SUB_P_TWEAKS_ARMOUR_VEST, "Armour Vest", &DroneArmour_Vest, 5.0f, 0.2f, SUB_P_TWEAKS_ARMOUR_VEST_VALUE},
    {SUB_P_TWEAKS_MULTIPLAYER, "Multiplayer", &Plr_DMod_Multi, 10.0f, 0.1f, SUB_P_TWEAKS_MULTIPLAYER_VALUE},
    {SUB_P_TWEAKS_MP_HEAD, "MP Head", &Plr_DMod_Head, 10.0f, 0.1f, SUB_P_TWEAKS_MP_HEAD_VALUE},
    {SUB_P_TWEAKS_MP_L_LIMB, "MP L Limb", &Plr_DMod_LowerLimb, 10.0f, 0.1f, SUB_P_TWEAKS_MP_L_LIMB_VALUE},
    {SUB_P_TWEAKS_MP_U_LIMB, "MP U Limb", &Plr_DMod_UpperLimb, 10.0f, 0.1f, SUB_P_TWEAKS_MP_U_LIMB_VALUE},
    {SUB_P_TWEAKS_DIFF_EASY, "Diff Easy", &Plr_DMod_Easy, 20.0f, 0.05f, SUB_P_TWEAKS_DIFF_EASY_VALUE},
    {SUB_P_TWEAKS_DIFF_NORMAL, "Diff Normal", &Plr_DMod_Normal, 20.0f, 0.05f, SUB_P_TWEAKS_DIFF_NORMAL_VALUE},
    {SUB_P_TWEAKS_DIFF_HARD, "Diff Hard", &Plr_DMod_Hard, 20.0f, 0.05f, SUB_P_TWEAKS_DIFF_HARD_VALUE},
};

// The order P_TWEAKS sets its scrolls in as it enters: Drone Legs before Drone Arms
static const uchar tweaks_enter_order[18] = {0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17};

static const TWEAK tweaks2[14] = {
    {SUB_P_TWEAKS2_DRONE_DELAY_MIN, "Drone Delay Min", &DroneFiring_BurstDelay_Min, 1.0f, 1.0f,
        SUB_P_TWEAKS2_DRONE_DELAY_MIN_VALUE},
    {SUB_P_TWEAKS2_DRONE_DELAY_NORM, "Drone Delay Norm", &DroneFiring_BurstDelay_Normal, 1.0f, 1.0f,
        SUB_P_TWEAKS2_DRONE_DELAY_NORM_VALUE},
    {SUB_P_TWEAKS2_DRONE_DELAY_MAX, "Drone Delay Max", &DroneFiring_BurstDelay_Max, 1.0f, 1.0f,
        SUB_P_TWEAKS2_DRONE_DELAY_MAX_VALUE},
    {SUB_P_TWEAKS2_DRONE_DIST_MIN, "Drone Dist Min", &DroneFiring_BurstDelay_MinDist, 1.0f, 1.0f,
        SUB_P_TWEAKS2_DRONE_DIST_MIN_VALUE},
    {SUB_P_TWEAKS2_DRONE_DIST_MAX, "Drone Dist Max", &DroneFiring_BurstDelay_MaxDist, 1.0f, 1.0f,
        SUB_P_TWEAKS2_DRONE_DIST_MAX_VALUE},
    {SUB_P_TWEAKS2_FIRING_ACC_EASY, "Firing acc easy", &DroneFiring_Accuracy_Easy, 4.0f, 0.25f,
        SUB_P_TWEAKS2_FIRING_ACC_EASY_VALUE},
    {SUB_P_TWEAKS2_FIRING_ACC_NORM, "Firing acc norm", &DroneFiring_Accuracy_Normal, 4.0f, 0.25f,
        SUB_P_TWEAKS2_FIRING_ACC_NORM_VALUE},
    {SUB_P_TWEAKS2_FIRING_ACC_HARD, "Firing acc hard", &DroneFiring_Accuracy_Hard, 4.0f, 0.25f,
        SUB_P_TWEAKS2_FIRING_ACC_HARD_VALUE},
    {SUB_P_TWEAKS2_NEW_SIGHT_TIME_TO_ACC, "New Sight time to acc", &DroneFiring_NewSighting_TimeToHit, 4.0f, 0.25f,
        SUB_P_TWEAKS2_NEW_SIGHT_TIME_TO_ACC_VALUE},
    {SUB_P_TWEAKS2_MOVE_TARGET_TIME_TO_ACC, "Move target time to acc", &DroneFiring_TargetFirstMoved_TimeToHit, 4.0f,
        0.25f,
        SUB_P_TWEAKS2_MOVE_TARGET_TIME_TO_ACC_VALUE},
    {SUB_P_TWEAKS2_MOVE_TARGET_ACCURACY, "Move target accuracy", &DroneFiring_TargetFirstMoved_Accuracy, 4.0f, 0.25f,
        SUB_P_TWEAKS2_MOVE_TARGET_ACCURACY_VALUE},
    {SUB_P_TWEAKS2_MOVE_TARGET_ACC_MOVE, "Move target acc move", &DroneFiring_TargetMoving_Accuracy, 4.0f, 0.25f,
        SUB_P_TWEAKS2_MOVE_TARGET_ACC_MOVE_VALUE},
    {SUB_P_TWEAKS2_TOO_CLOSE_DISTANCE, "Too close distance", &DroneFiring_TooClose_Distance, 4.0f, 0.25f,
        SUB_P_TWEAKS2_TOO_CLOSE_DISTANCE_VALUE},
    {SUB_P_TWEAKS2_TOO_CLOSE_ACCURACY, "Too close accuracy", &DroneFiring_TooClose_Accuracy, 4.0f, 0.25f,
        SUB_P_TWEAKS2_TOO_CLOSE_ACCURACY_VALUE},
};

// The values as text, which the labels keep. P_TWEAKS2 shows a fifteenth that nothing writes.
// XBE_GLOBAL(0x0025d828, 0x240)
char tweaks_text[18][0x20];
// XBE_GLOBAL(0x0025da68, 0x1e0)
char tweaks2_text[15][0x20];

static void Tweak_SetScroll(uchar managerNum, const TWEAK *tweak) {
    __Menu_Send(managerNum, tweak->scroll, MessageType_SetValue, Ftol((double)*tweak->tunable * tweak->sliderScale), 0);
}

static void Tweak_Print(uchar managerNum, const TWEAK *tweak, char *text) {
    int value = __Menu_Send(managerNum, tweak->scroll, MessageType_GetValue, 0, 0);
    sprintf(text, "%f", (double)value * tweak->scale);
}

static void Tweak_Store(uchar managerNum, const TWEAK *tweak) {
    int value = __Menu_Send(managerNum, tweak->scroll, MessageType_GetValue, 0, 0);
    *tweak->tunable = (float)((double)value * tweak->scale);
}

// The drones' damage and armour, and the player's damage modifiers. A saves them, Y goes on to P_TWEAKS2.
// AUTOINJECT
bool P_TWEAKS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_PageEnter:
            LABEL_SET_TEXT(managerNum, SUB_P_TWEAKS_HINT_TEXT, "~A Save changes ~Y More tweaks ~B Back");
            for (int i = 0; i < ARRAY_SIZE(tweaks); i++)
                LABEL_SET_TEXT(managerNum, tweaks[i].scroll, tweaks[i].name);
            for (int i = 0; i < ARRAY_SIZE(tweaks); i++)
                Tweak_SetScroll(managerNum, &tweaks[tweaks_enter_order[i]]);
            break;
        case MessageType_PageUpdate:
            for (int i = 0; i < ARRAY_SIZE(tweaks); i++)
                Tweak_Print(managerNum, &tweaks[i], tweaks_text[i]);
            for (int i = 0; i < ARRAY_SIZE(tweaks); i++)
                LABEL_SET_TEXT(managerNum, tweaks[i].valueText, tweaks_text[i]);
            break;
        case MessageType_Select:
            for (int i = 0; i < ARRAY_SIZE(tweaks); i++)
                Tweak_Store(managerNum, &tweaks[i]);
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
            break;
        case MessageType_AltSelect1:
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_TWEAKS2, 0);
            break;
    }
    return true;
}

// The drones' firing
// AUTOINJECT
bool P_TWEAKS2_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_PageEnter:
            LABEL_SET_TEXT(managerNum, SUB_P_TWEAKS2_HINT_TEXT, "~A Save changes  ~B Back");
            for (int i = 0; i < ARRAY_SIZE(tweaks2); i++)
                LABEL_SET_TEXT(managerNum, tweaks2[i].scroll, tweaks2[i].name);
            __Menu_Send(managerNum, SUB_P_TWEAKS2_UNUSED_SCROLL, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            for (int i = 0; i < ARRAY_SIZE(tweaks2); i++)
                Tweak_SetScroll(managerNum, &tweaks2[i]);
            break;
        case MessageType_PageUpdate:
            for (int i = 0; i < ARRAY_SIZE(tweaks2); i++)
                Tweak_Print(managerNum, &tweaks2[i], tweaks2_text[i]);
            for (int i = 0; i < ARRAY_SIZE(tweaks2); i++)
                LABEL_SET_TEXT(managerNum, tweaks2[i].valueText, tweaks2_text[i]);
            LABEL_SET_TEXT(managerNum, SUB_P_TWEAKS2_UNUSED_VALUE, tweaks2_text[14]);
            break;
        case MessageType_Select:
            for (int i = 0; i < ARRAY_SIZE(tweaks2); i++)
                Tweak_Store(managerNum, &tweaks2[i]);
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 0, 0);
            break;
    }
    return true;
}

// ---- the scripted scenes

// The scene C_NIS plays, run by P_NIS's update until it ends
// XBE_GLOBAL(0x002451f8, 0x4)
SCRIPTINFO *nis_script;
// Set by A, cleared by Y; nothing reads it
// XBE_GLOBAL(0x0025d7c8, 0x1)
uint8_t nis_by_select;

// AUTOINJECT
bool P_NIS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_PageUpdate && nis_script != NULL) {
        Script_Update(nis_script);
        control_movement_object_handler(0);
        if (!Script_IsPlaying(nis_script)) {
            nis_script = NULL;
            Player_Enable(glb_players[0], NULL, 0);
            Camera_PopStates();
            Camera_Enable(7, 0, 1, NULL);
            ScriptCam = HASHCODE_NONE;
        }
    }
    return true;
}

// The scenes of each level, by script; a level's own line has none
static const struct { const char *name; HASHCODE script; } nis_scenes[] = {
    {"Castle Exterior", (HASHCODE)0},
    {"  Halo Jump", HT_Script_NIS_HaloJump},
    {"  Van Jump", HT_Script_NIS_VanJump},
    {"  Cliff Fall", HT_Script_NIS_CliffFall},
    {"Castle Courtyards", (HASHCODE)0},
    {"  Castle_tux_change", HT_Script_NIS_Castle_tux_change},
    {"Castle Indoors 1", (HASHCODE)0},
    {"  M1_library", HT_Script_NIS_M1_library},
    {"  Engagement", HT_Script_NIS_Engagement},
    {"Castle Indoors 2", (HASHCODE)0},
    {"  Castle Meeting", HT_Script_NIS_CastleMeeting},
    {"  Castle Rook", HT_Script_NIS_CastleRook},
    {"  Gondola", HT_Script_NIS_Gondola},
    {"  Cliff Fall 2", HT_Script_NIS_CliffFall2},
    {"Estate A", (HASHCODE)0},
    {"  Meet Mayhew", HT_Script_NIS_MeetMayhew},
    {"  Yakuza Ambush", HT_Script_NIS_YakuzaAmbush},
    {"  Meet Mayhew Intro", HT_Script_NIS_MeetMayhewIntro},
    {"Estate B", (HASHCODE)0},
    {"  Bath House", HT_Script_NIS_BathHouse},
    {"  Bunker Objectives", HT_Script_NIS_BunkerObjectives},
    {"Estate C", (HASHCODE)0},
    {"  Yakuza Sniper", HT_Script_NIS_YakuzaSniper},
    {"Estate D", (HASHCODE)0},
    {"  Mayhew Death", HT_Script_NIS_MayhewDeath},
    {"  Ninja Attack", HT_Script_NIS_NinjaAttack},
    {"  Second Bunker", HT_Script_NIS_SecondBunker},
    {"  Meet Mayhew Intro", HT_Script_NIS_MeetMayhewIntro},
    {"Tower A", (HASHCODE)0},
    {"  Infiltrate Intro", HT_Script_NIS_InfiltrateIntro},
    {"  Infiltrate Noname", (HASHCODE)0},
    {"Tower B", (HASHCODE)0},
    {"Tower C", (HASHCODE)0},
    {"  Infiltrate Fall", HT_Script_NIS_InfiltrateFall},
    {"  Infiltrate Intro", HT_Script_NIS_InfiltrateIntro},
    {"  Infiltrate Stair", HT_Script_NIS_InfiltrateStair},
    {"  Infiltrate Buzz", HT_Script_NIS_InfiltrateBuzz},
    {"  Infiltrate Outro", HT_Script_NIS_InfiltrateOutro},
    {"Powerstation A1", (HASHCODE)0},
    {"  Drake Interview", HT_Script_NIS_DrakeInterview},
    {"Powerstation A2", (HASHCODE)0},
    {"  Rook Retreat", HT_Script_NIS_RookRetreat},
    {"  Enter Vent", HT_Script_NIS_EnterVent},
    {"Tower2 A", (HASHCODE)0},
    {"  Infiltrate Fall", HT_Script_NIS_InfiltrateFall},
    {"  Dominique Death", HT_Script_NIS_DominiqueDeath},
    {"Tower2 B", (HASHCODE)0},
    {"  Infiltrate Fall", HT_Script_NIS_InfiltrateFall},
    {"  Suitcase Gun", (HASHCODE)0},
    {"  Enter Shaft", (HASHCODE)0},
    {"Tower2 C", (HASHCODE)0},
    {"  Lobby Rescue", HT_Script_NIS_LobbyRescue},
    {"  Elevator Exit", HT_Script_NIS_ElevatorExit},
    {"Tower2 Elevator", (HASHCODE)0},
    {"  Shaft Attack", HT_Script_NIS_ShaftAttack},
    {"  Elevator Plummet", HT_Script_NIS_ElevatorPlummet},
    {"Evil Base", (HASHCODE)0},
    {"  Kiko Command Centre", HT_Script_NIS_KikoCommandCentre},
    {"  Kiko Arrival", HT_Script_NIS_KikoArrival},
    {"  Rook Reveal", HT_Script_NIS_RookReveal},
    {"Evil Silo", (HASHCODE)0},
    {"Evil Base C", (HASHCODE)0},
    {"  Bond Dropped", HT_Script_NIS_BondDropped},
    {"  Kiko Flees", HT_Script_NIS_KikoFlees},
    {"  Kiko Death", HT_Script_NIS_KikoDeath},
    {"Space Station D", (HASHCODE)0},
    {"  Laser Taunt", HT_Script_NIS_LaserTaunt},
    {"  Drake Explosion", HT_Script_NIS_DrakeExplosion},
    {"  Drake Killed", HT_Script_NIS_DrakeKilled},
    {"Unknown", (HASHCODE)0},
};

// The list of scenes. A or Y plays the one picked, when none is playing: its file is loaded if it must be, and the
// player and the camera are handed to it.
// AUTOINJECT
bool C_NIS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_ControlCreated:
            for (int i = 0; i < ARRAY_SIZE(nis_scenes); i++)
                __Menu_SendMessage(control, MessageType_AddItem, (intptr_t)nis_scenes[i].name, nis_scenes[i].script);
            nis_script = NULL;
            return true;
        case MessageType_Select:
            nis_by_select = 1;
            break;
        case MessageType_AltSelect1:
            nis_by_select = 0;
            break;
        default:
            return true;
    }
    if (Script_IsPlaying(nis_script))
        return true;
    HASHCODE script = (HASHCODE)RADIO_GET_VALUE_OF(control);
    if (script == 0)
        return true;
    uint loadable = isLoadable(script);
    if (loadable != 0) {
        SFXPauseAllStreams();
        LoadableLoad(script, loadable);
        SFXUnPauseAllStreams();
    }
    if (hashtable_getitem(script) == NULL)
        return true;
    nis_script = Script_Load(script, &CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, NULL, NULL, NULL, NULL);
    Player_Disable(glb_players[0], 0);
    Script_Play(nis_script, 0);
    Camera_PushStates();
    Camera_Enable(7, 0, 0, NULL);
    return true;
}

// ---- the medal cheat

// XBE_GLOBAL(0x002790a8, 0x4)
uint cheat_medal;    // nothing reads it

// The list's choice: SWITCH_MISSION_FAILED (item 0), or SWITCH_MISSION_WON with a medal (items 1 to 5: 999, then 1
// to 4), each with the frame it was set; the menu closes
// AUTOINJECT
bool P_CHEATMEDAL_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_PageEnter) {
        __Menu_SendMessage(CONTROL_GET(managerNum, SUB_P_CHEATMEDAL_LIST), MessageType_ClearItems, 0, 0);
    }
    else if (message == MessageType_Select) {
        uchar choice = (uchar)RADIO_GET_VALUE(managerNum, SUB_P_CHEATMEDAL_LIST);
        MenuManager_Delete(managerNum);
        switch (choice) {
            case 0:
                switch_channels[SWITCH_MISSION_FAILED] = 1;
                switch_channels_time[SWITCH_MISSION_FAILED] = GameState.NumFramesUnpaused;
                break;
            case 1: case 2: case 3: case 4: case 5:
                switch_channels_time[SWITCH_MISSION_WON] = GameState.NumFramesUnpaused;
                cheat_medal = choice == 1 ? 999 : choice - 1;
                switch_channels[SWITCH_MISSION_WON] = 1;
                break;
        }
    }
    return true;
}
