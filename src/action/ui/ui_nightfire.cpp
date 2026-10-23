#include "ui.h"

#include "Manager.h"
#include "Menu.h"
#include "MenuManager.h"
#include "../engine/Text.h"
#include "../game.h"
#include "../sound/SFX.h"
#include "../game/mp/multiplayer.h"
#include "../input.h"
#include "../util/Stack.h"
#include "MenuCommon.h"
#include "../game/sp/Mission.h"
#include "../game/sp/PlayerStats.h"

#include <stdio.h>
#include <string.h>

// The single player campaign's pages: the mission wheel (P_NFMAP / C_SBNFMAP) and the difficulty wheel
// (P_NFDFCTY / C_SBNFDFCTY).

// The missions (sp_level, ui.h; these are its shipped contents). .enabled is the campaign's progress: set from the codename's save (Menu_SetNightfireStatus), by
// finishing the mission before (Menu_SetLevelBonus) and by secret codes (Menu_SpecialCodenameCheck).
// XBE_GLOBAL(0x0017c580, 0x120)
M_ITEM sp_level[12] = {
    {HT_Texture_prev_paris, SPMAP_PARISPRELUDE_NAME, SPMAP_PARISPRELUDE_DESC, HT_Level_Driving_Paris, 1,
        TXT_NULL},
    {HT_Texture_prev_castle, SPMAP_EXCHANGE_NAME, SPMAP_EXCHANGE_DESC, HT_Level_CastleExterior, 1, TXT_NULL},
    {HT_Texture_prev_snowmobile, SPMAP_ALPINEESCAPE_NAME, SPMAP_ALPINEESCAPE_DESC, HT_Level_Driving_SnowMobile,
        0, SPMAP_LOCKED},
    {HT_Texture_prev_vanquish, SPMAP_ENEMIESVANQUISHED_NAME, SPMAP_ENEMIESVANQUISHED_DESC,
        HT_Level_Driving_Alps, 0, SPMAP_LOCKED},
    {HT_Texture_prev_henderson, SPMAP_DOUBLECROSS_NAME, SPMAP_DOUBLECROSS_DESC, HT_Level_HendersonA, 0,
        SPMAP_LOCKED},
    {HT_Texture_prev_tower1, SPMAP_NIGHTSHIFT_NAME, SPMAP_NIGHTSHIFT_DESC, HT_Level_TowerA, 0, SPMAP_LOCKED},
    {HT_Texture_prev_powerstation, SPMAP_CHAINREACTION_NAME, SPMAP_CHAINREACTION_DESC, HT_Level_PowerStationA1, 0,
        SPMAP_LOCKED},
    {HT_Texture_prev_tower2, SPMAP_PHOENIXFIRE_NAME, SPMAP_PHOENIXFIRE_DESC, HT_Level_Tower2A, 0,
        SPMAP_LOCKED},
    {HT_Texture_prev_infiltration, SPMAP_DEEPDESCENT_NAME, SPMAP_DEEPDESCENT_DESC, HT_Level_Driving_Underwater, 0,
        SPMAP_LOCKED},
    {HT_Texture_prev_jungle, SPMAP_ISLANDINFILTRATION_NAME, SPMAP_ISLANDINFILTRATION_DESC,
        HT_Level_Driving_JungleA, 0, SPMAP_LOCKED},
    {HT_Texture_prev_countdown, SPMAP_COUNTDOWN_NAME, SPMAP_COUNTDOWN_DESC, HT_Level_EvilBase, 0, SPMAP_LOCKED},
    {HT_Texture_prev_equinox, SPMAP_EQUINOX_NAME, SPMAP_EQUINOX_DESC, HT_Level_SpaceStationD, 0, SPMAP_LOCKED},
};

// difficulty (ui.h): the identifier is GameState.difficultyModifier.
// XBE_GLOBAL(0x0017c760, 0x48)
M_ITEM difficulty[3] = {
    {HT_Texture_dt_operative, DIFFICULTY_OPERATIVE, DIFFICULTY_OPERATIVE_DESC, 1, 1, TXT_NULL},
    {HT_Texture_dt_agent, DIFFICULTY_AGENT, DIFFICULTY_AGENT_DESC, 2, 1, TXT_NULL},
    {HT_Texture_dt_00agent, DIFFICULTY_00AGENT, DIFFICULTY_00AGENT_DESC, 3, 1, TXT_NULL},
};

extern char menu_unlock_everything;
// Counts up a few frames once a mission is picked (0 = none), so the loading panel shows before the load starts.
// XBE_GLOBAL(0x0025d7dc, 0x1)
static uint8_t nf_mission_countdown;

// AUTOINJECT
bool P_NFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_PageEnter) {
        if ((HASHCODE)arg2 == P_NFSELECT) {
            Menu_StartIris(0, managerNum, SUB_C_SBNFDFCTY_IRIS);
            __Menu_Send(managerNum, C_SBNFDFCTY, MessageType_SetValue, 0, 0);
        } else {
            Menu_StartIris(4, managerNum, SUB_C_SBNFDFCTY_IRIS);
        }
    } else if (message == MessageType_PageUpdate) {
        Menu_PlayIris(1, managerNum, SUB_C_SBNFDFCTY_IRIS);
    }
    return true;
}

// AUTOINJECT
bool C_SBNFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        Menu_UpdateWheel(managerNum, control, difficulty, SUB_C_SBNFDFCTY_WHEEL_TEXT, SUB_C_SBNFDFCTY_ICON,
                         SUB_C_SBNFDFCTY_DESCRIPTION_TEXT, SUB_C_SBNFDFCTY_IRIS, message == MessageType_Scroll);
        break;
    case MessageType_Select: {
        uchar idx = (uchar)SCROLL_GET_VALUE(control);
        if (menu_unlock_everything || difficulty[idx].enabled) {
            GameState.difficultyModifier = difficulty[idx].identifier;
            Menu_ChangePageCloseIris(P_NFMAP, managerNum, SUB_C_SBNFMAP_IRIS);
        }
        break;
    }
    case MessageType_ControlCreated:
        __Menu_SendMessage(control, MessageType_SetRange, 0, ARRAY_SIZE(difficulty) - 1);
        break;
    }
    return true;
}

// AUTOINJECT
bool P_NFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_PageEnter) {
        for (int i = 0; i < 4; i++)
            PlayerInputs[i].controllerPort = (undefined1)i;
        Menu_RestartFrontEndLoop();
        Menu_StartIris((HASHCODE)arg2 == P_NFDFCTY ? 0 : 4, managerNum, SUB_C_SBNFMAP_IRIS);
        // B goes back to the main menu when this is the first page (the menu was opened straight onto it)
        Action_TranslatedText hint = Stack_IsEmpty(&manager[managerNum].stack) ? NFMAP_HINT_BACK_TO_MAIN_MENU : NFMAP_HINT_BACK;
        LABEL_SET_TEXT(managerNum, SUB_P_NFMAP_BUTTON_HINT_TEXT, Txt_BindLabel(hint, 0));
        // the wheel starts on the latest open mission
        M_CONTROL *wheel = CONTROL_GET(managerNum, C_SBNFMAP);
        __Menu_SendMessage(wheel, MessageType_SetValue, 0, 0);
        int latest = ARRAY_SIZE(sp_level) - 1;
        while (!*(uchar *)&sp_level[latest].enabled)
            latest--;
        __Menu_SendMessage(wheel, MessageType_SetValue, latest, 0);
    } else if (message == MessageType_PageUpdate) {
        Menu_PlayIris(1, managerNum, SUB_C_SBNFMAP_IRIS);
        if (nf_mission_countdown != 0 && ++nf_mission_countdown != 0) {
            if (nf_mission_countdown <= 3) {
                __Menu_Send(managerNum, SUB_P_NFMAP_LOADING_PANEL, MessageType_SetState, CONTROL_STATE_INERT, 0);
            } else if (nf_mission_countdown == 4) {
                ResetMap_LevelToLoad(GameState.NextLevelHashcode, false, false);
                GameFlow_PushState(0xb, 80.0f, 0);
                MenuManager_Delete(managerNum);
                SFXFadeDown(1);
            }
        }
    } else if (message == MessageType_BackAtRoot) {
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_MAIN, 1);
    }
    return true;
}

// AUTOINJECT
bool C_SBNFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        Menu_UpdateWheel(managerNum, control, sp_level, SUB_C_SBNFMAP_WHEEL_TEXT, SUB_C_SBNFMAP_ICON,
                         SUB_C_SBNFMAP_DESCRIPTION_TEXT, SUB_C_SBNFMAP_IRIS, message == MessageType_Scroll);
        break;
    case MessageType_Select: {
        uchar idx = (uchar)SCROLL_GET_VALUE(control);
        if ((menu_unlock_everything || sp_level[idx].enabled) && idx < ARRAY_SIZE(sp_level)) {
            GameState.NextLevelHashcode = (HASHCODE)sp_level[idx].identifier;
            MPSettings.isMultiplayer = 0;
            MPSettings.numPlayers = 1;
            MPSettings.numPlayersAndBots = 1;
            MPSettings.isTeamGame = 0;
            GameState.BaseMapHashCode = 0;
            PlayerInputs[0].controllerPort = (undefined1)Menu_GetLastController();
            ResetMap_LevelToLoad(GameState.NextLevelHashcode, false, false);
            GameFlow_PushState(7, 80.0f, 0xff);
            MenuManager_Delete(managerNum);
            SFXFadeDown(1);
        }
        break;
    }
    case MessageType_ControlCreated:
        __Menu_SendMessage(control, MessageType_SetRange, 0, ARRAY_SIZE(sp_level) - 1);
        __Menu_SendMessage(control, MessageType_SetValue, 0, 0);
        break;
    case MessageType_AltSelect1:   // Y: the dossier
        Menu_ChangePageCloseIris(P_DOSSIER, managerNum, SUB_C_SBNFMAP_IRIS);
        break;
    }
    return true;
}

// ---- after a mission: its results (P_NFRESULTS), the rewards won (P_NFBONUS), its statistics (P_NFSTATS)

// The mission's score, as P_NFRESULTS and P_NFSTATS last read it
// XBE_GLOBAL(0x0025d7d0, 0x4)
SCORETABLE *results_score;
// Menu_SetLevelBonus's answer: the last mission was finished, so the end of the game follows
// XBE_GLOBAL(0x0025d7d4, 0x1)
uint8_t results_last_mission;
// The results' texts, which the labels keep
// XBE_GLOBAL(0x0025dc50, 0x80)
char results_title_text[0x80];
// XBE_GLOBAL(0x0025dcd0, 0x40)
char results_target_text[0x40];
// XBE_GLOBAL(0x0025dd10, 0x40)
char results_score_text[0x40];
// A better score is to be saved with the codename
// XBE_GLOBAL(0x0025dd50, 0x1)
uint8_t results_saving;

extern uint8_t menu_level_bonus_earned;  // MenuUnlocks.cpp

#define TXT_PLATINUM_WON    ((Action_TranslatedText)0x2c7)
#define TXT_GOLD_WON        ((Action_TranslatedText)0x2c8)
#define TXT_SILVER_WON      ((Action_TranslatedText)0x2c9)
#define TXT_BRONZE_WON      ((Action_TranslatedText)0x2ca)
#define TXT_PLATINUM_TARGET ((Action_TranslatedText)0x2cc)
#define TXT_GOLD_TARGET     ((Action_TranslatedText)0x2cd)
#define TXT_SILVER_TARGET   ((Action_TranslatedText)0x2ce)
#define TXT_BRONZE_TARGET   ((Action_TranslatedText)0x2cf)
#define TXT_MISSION_FAILED  ((Action_TranslatedText)0x2fd)
#define TXT_KILLED_IN_ACTION ((Action_TranslatedText)0x2fe)
#define TXT_NICE_WORK       ((Action_TranslatedText)0x10001fd)  // "Nice work %s. You completed the mission."
#define TXT_AGENT           ((Action_TranslatedText)0x10001fe)

#define MEDAL_NONE 5    // Menu_SetLevelBonus's medal for none
#define TXT_NFMAP_HINT_MAIN_MENU ((Action_TranslatedText)0x283)  // "~A Select  ~Y Dossier  ~V Scroll  ~B Main Menu"
// The format of the results' and the rewards' memos (the game's string, which a memo keeps)
#define RESULTS_FORMAT FontFormat3

// On from the results: to the rewards won, the end of the game, or the missions
static void Results_GoOn(uchar managerNum) {
    if (menu_level_bonus_earned) {
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_NFBONUS, 1);
    } else if (results_last_mission) {
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_WINGAME, 1);
    } else {
        LABEL_SET_TEXT(managerNum, SUB_P_NFMAP_BUTTON_HINT_TEXT, Txt_BindLabel(TXT_NFMAP_HINT_MAIN_MENU, 0));
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_NFMAP, 1);
    }
}

// The result: the score, the medal won and the next medal's target, and the mission's rewards given (by
// Menu_SetLevelBonus, with the medal; a failed mission or one killed in action passes 4, as a platinum). Coming
// back from the statistics or the dossier changes nothing. A then saves a better score (with a saved codename), and
// goes on to the rewards won, the end of the game, or the missions.
// AUTOINJECT
bool P_NFRESULTS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_PageEnter: {
        uint medal = MEDAL_NONE;
        Action_TranslatedText title = (Action_TranslatedText)0, targetName = (Action_TranslatedText)0;
        bool titleIsLabel = true;
        HASHCODE icon = (HASHCODE)0;
        for (int i = 0; i < 4; i++)
            PlayerInputs[i].controllerPort = (undefined1)i;
        Menu_RestartFrontEndLoop();
        ls.done = 0;
        results_saving = 0;
        Manager_SendMessage(&manager[managerNum], MessageType_SetCursor, SUB_P_NFRESULTS_FIRST, 0);
        if ((HASHCODE)arg2 == P_NFSTATS || (HASHCODE)arg2 == P_DOSSIER)
            return true;
        results_score = (SCORETABLE *)PlrStat_GetScore(0);
        if (results_score != NULL) {
            sprintf(results_score_text, "%d", results_score->score);
            switch (Mission_Status()) {
            case 2:
                title = TXT_MISSION_FAILED;
                sprintf(results_target_text, "%d", results_score->thresholdScoreBronze);
                medal = 4;
                targetName = TXT_BRONZE_TARGET;
                break;
            case 3:
                title = TXT_KILLED_IN_ACTION;
                sprintf(results_target_text, "%d", results_score->thresholdScoreBronze);
                medal = 4;
                targetName = TXT_BRONZE_TARGET;
                break;
            default: {
                SeparateNumber(results_score->score, results_score_text);
                LABEL_SET_TEXT(managerNum, SUB_P_NFRESULTS_SCORE_TEXT, results_score_text);
                uint score = results_score->score;
                if (score == 0)
                    break;
                if (score >= results_score->thresholdScorePlatinum) {
                    medal = 4;
                    title = TXT_PLATINUM_WON;
                    targetName = TXT_PLATINUM_TARGET;
                    icon = HT_Texture_medal_platinum;
                    SeparateNumber(results_score->thresholdScorePlatinum, results_target_text);
                } else if (score >= results_score->thresholdScoreGold) {
                    medal = 3;
                    title = TXT_GOLD_WON;
                    targetName = TXT_PLATINUM_TARGET;
                    icon = HT_Texture_medal_gold;
                    SeparateNumber(results_score->thresholdScorePlatinum, results_target_text);
                } else if (score >= results_score->thresholdScoreSilver) {
                    medal = 2;
                    title = TXT_SILVER_WON;
                    targetName = TXT_GOLD_TARGET;
                    icon = HT_Texture_medal_silver;
                    SeparateNumber(results_score->thresholdScoreGold, results_target_text);
                } else if (score >= results_score->thresholdScoreBronze) {
                    medal = 1;
                    title = TXT_BRONZE_WON;
                    targetName = TXT_SILVER_TARGET;
                    icon = HT_Texture_medal_bronze;
                    SeparateNumber(results_score->thresholdScoreSilver, results_target_text);
                } else {
                    // no medal: the title names the codename instead
                    SeparateNumber(results_score->thresholdScoreBronze, results_target_text);
                    medal = MEDAL_NONE;
                    targetName = TXT_BRONZE_TARGET;
                    icon = HT_Texture_medal_none;
                    if (ls.slot < CODENAME_SLOTS_DEFAULT) {
                        const char *agent = Txt_BindLabel(TXT_AGENT, 0);
                        sprintf(results_title_text, Txt_BindLabel(TXT_NICE_WORK, 0), agent);
                    } else {
                        sprintf(results_title_text, Txt_BindLabel(TXT_NICE_WORK, 0), ls.codename);
                    }
                    titleIsLabel = false;
                }
                break;
            }
            }
            if (titleIsLabel)
                strcpy(results_title_text, Txt_BindLabel(title, 0));
            __Menu_Send(managerNum, SUB_P_NFRESULTS_TITLE_TEXT, MessageType_SetFont, (intptr_t)RESULTS_FORMAT, 0);
            __Menu_Send(managerNum, SUB_P_NFRESULTS_TITLE_TEXT, MessageType_MemoSetOption280, 1, 0);
            LABEL_SET_TEXT(managerNum, SUB_P_NFRESULTS_TITLE_TEXT, results_title_text);
            LABEL_SET_TEXT(managerNum, SUB_P_NFRESULTS_SCORE_TEXT, results_score_text);
            LABEL_SET_TEXT(managerNum, SUB_P_NFRESULTS_TARGET_NAME, Txt_BindLabel(targetName, 0));
            LABEL_SET_TEXT(managerNum, SUB_P_NFRESULTS_TARGET_TEXT, results_target_text);
            __Menu_Send(managerNum, SUB_P_NFRESULTS_MEDAL_ICON, MessageType_SetState, CONTROL_STATE_INERT, 0);
            __Menu_Send(managerNum, SUB_P_NFRESULTS_MEDAL_ICON, MessageType_SetIcon, icon, 0);
        }
        results_last_mission = Menu_SetLevelBonus(GameState.BaseMapHashCode, medal, 0);
        break;
    }
    case MessageType_Select:
        if (PlrStats_DoneBetter() && ls.slot >= CODENAME_SLOTS_DEFAULT) {
            results_saving = 1;
            break;
        }
        if (PlrStats_DoneBetter())
            Menu_UpdateDefaultCodename(0, (uchar)ls.slot);
        Results_GoOn(managerNum);
        break;
    case MessageType_PageUpdate: {
        uint answer = Menu_UpdateOptionBox(NULL);
        if (answer == 1 || results_saving) {
            ls.busy = 1;
            ls.slot = LS_SLOT_NEW;
            ls.returnPage = (HASHCODE)0;
            ls.returnPageArg = 0;
            ls.operation = LS_OPERATION_SAVE;
            ls.done = 0;
            ls.control = NULL;
            ls.field17_0x42 = 0;
            Menu_UpdateMessageBox(managerNum, 0xff, 0);
        } else if (answer == 2) {
            ls.done = 1;
        }
        if (ls.slot >= CODENAME_SLOTS_DEFAULT)
            Menu_UpdateMessageBox(managerNum, 0xff, 0);
        if (ls.done) {
            Results_GoOn(managerNum);
            ls.done = 0;
        }
        break;
    }
    case MessageType_AltSelect1:   // Y: the dossier
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_DOSSIER, 0);
        break;
    case MessageType_AltSelect2:   // the statistics
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_NFSTATS, 0);
        break;
    }
    return true;
}

// XBE_GLOBAL(0x0025d708, 0x84)
char bonus_text[0x84];
// XBE_GLOBAL(0x0025dd58, 0x80)
char bonus_text_before[0x80];

// The rewards just won, the first medal's pictured, the others' names after it from the best down (blank with none)
// AUTOINJECT
bool P_NFBONUS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_PageEnter: {
        LEVEL_BONUSES *bonuses = Menu_GetLevelBonuses((HASHCODE)GameState.BaseMapHashCode);
        __Menu_Send(managerNum, SUB_P_NFBONUS_TEXT, MessageType_SetFont, (intptr_t)RESULTS_FORMAT, 0);
        if (!menu_level_bonus_earned) {
            LABEL_SET_TEXT(managerNum, SUB_P_NFBONUS_TEXT, NULL);
            break;
        }
        bonus_text[0] = 0;
        if (bonuses->medals[0].earned) {
            __Menu_Send(managerNum, SUB_P_NFBONUS_ICON, MessageType_SetIcon, bonuses->medals[0].icon, 0);
            sprintf(bonus_text, "%s\n", Txt_BindLabel(bonuses->medals[0].name, 0));
        }
        for (int medal = 3; medal > 0; medal--) {
            if (!bonuses->medals[medal].earned)
                continue;
            strcpy(bonus_text_before, bonus_text);
            sprintf(bonus_text, "%s\n%s", bonus_text_before, Txt_BindLabel(bonuses->medals[medal].name, 0));
        }
        LABEL_SET_TEXT(managerNum, SUB_P_NFBONUS_TEXT, bonus_text);
        break;
    }
    case MessageType_Select:
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, results_last_mission ? P_WINGAME : P_NFMAP, 1);
        break;
    case MessageType_AltSelect1:
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_DOSSIER, 0);
        break;
    case MessageType_AltSelect2:
        Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_NFSTATS, 0);
        break;
    }
    return true;
}

// Splits "a|b|c|d" into a row's first four cells
static void Stats_SplitRow(LIST_ROW *row, char *text) {
    for (int cell = 0; text != NULL && cell < 4; cell++) {
        char *bar = strstr(text, "|");
        if (bar != NULL)
            *bar = 0;
        strcpy(row->cells[cell].text, text);
        text = bar != NULL ? bar + 1 : NULL;
    }
}

// A row of the statistics: label, then two numbers (or two times) and the points, in the list's four columns
// AUTOINJECT
void Menu_AddRow(uchar managerNum, Action_TranslatedText label, int achieved, int target, uint points, char indent,
                 char isTime) {
    LIST_ROW row = {};
    char *text = Txt_GetStringFromHeap(0);
    char pointsText[0x40];
    SeparateNumber(points, pointsText);
    if (isTime) {
        const char *achievedText = Timer_Seconds2String(achieved, 0);
        const char *targetText = Timer_Seconds2String(target, 0);
        sprintf(text, "%s|%s|%s|%s", Txt_BindLabel(label, 0), achievedText, targetText, pointsText);
    } else if (indent) {
        sprintf(text, "  %s|%d|%d|%s", Txt_BindLabel(label, 0), achieved, target, pointsText);
    } else {
        sprintf(text, "%s|%d|%d|%s", Txt_BindLabel(label, 0), achieved, target, pointsText);
    }
    Stats_SplitRow(&row, text);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListAddRow, (intptr_t)&row, 0);
}

// The same with a percentage, and a second number or "----" (extra -1)
// AUTOINJECT
void Menu_AddRowPercentage(uchar managerNum, Action_TranslatedText label, uint percent, int extra, uint points,
                           char indent) {
    LIST_ROW row = {};
    char *text = Txt_GetStringFromHeap(0);
    char pointsText[0x40];
    char extraText[0x40];
    SeparateNumber(points, pointsText);
    if (extra != -1)
        sprintf(extraText, "%d%%", extra);
    else
        sprintf(extraText, "----");
    if (indent)
        sprintf(text, "  %s|%d%%|%s|%s", Txt_BindLabel(label, 0), percent, extraText, pointsText);
    else
        sprintf(text, "%s|%d%%|%s|%s", Txt_BindLabel(label, 0), percent, extraText, pointsText);
    Stats_SplitRow(&row, text);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListAddRow, (intptr_t)&row, 0);
}

#define TXT_STATS_CATEGORY      ((Action_TranslatedText)0x1000204)
#define TXT_STATS_CURRENT       ((Action_TranslatedText)0x10001e1)
#define TXT_STATS_TARGET        ((Action_TranslatedText)0x10001e2)
#define TXT_STATS_TOTAL         ((Action_TranslatedText)0x163)
#define TXT_STATS_BOND_MOVES    ((Action_TranslatedText)0x165)
#define TXT_STATS_OPPONENTS     ((Action_TranslatedText)0x15a)
#define TXT_STATS_DISPATCHED    ((Action_TranslatedText)0x15b)
#define TXT_STATS_SUBDUED       ((Action_TranslatedText)0x15c)
#define TXT_STATS_SURRENDERED   ((Action_TranslatedText)0x16e)
#define TXT_STATS_STEALTH       ((Action_TranslatedText)0x15d)
#define TXT_STATS_ACCURACY      ((Action_TranslatedText)0x15e)
#define TXT_STATS_HEALTH        ((Action_TranslatedText)0x15f)
#define TXT_STATS_TIME          ((Action_TranslatedText)0x160)
#define TXT_STATS_007_BONUS     ((Action_TranslatedText)0x161)
#define TXT_STATS_SUBTOTAL      ((Action_TranslatedText)0x162)
#define TXT_STATS_DIFFICULTY    ((Action_TranslatedText)0x164)

// A rating of 0 to 1 as a percentage, rounded
static uint Stats_Percent(float rating) {
    return Ftol((double)rating * 100.0f + 0.5f);
}

// The statistics: each category's figures and points (the opponents subdued, surrendered and the stealth rating only
// on a level on foot), then the subtotal, the difficulty bonus and the total
// AUTOINJECT
bool P_NFSTATS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message != MessageType_PageEnter)
        return true;
    uchar footRows = 0;
    results_score = (SCORETABLE *)PlrStat_GetScore(0);
    if (results_score == NULL)
        return true;
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ClearItems, 0, 0);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetSelectionColour, 0x0f050580, 0);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetColumnWidth, 0, 35);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListAddColumn, 21, ALIGN_RIGHT);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListAddColumn, 21, ALIGN_RIGHT);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListAddColumn, 24, ALIGN_RIGHT);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_AddItem, (intptr_t)Txt_BindLabel(TXT_STATS_CATEGORY, 0), 0);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetCellText, 1, (intptr_t)Txt_BindLabel(TXT_STATS_CURRENT, 0));
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetCellText, 2, (intptr_t)Txt_BindLabel(TXT_STATS_TARGET, 0));
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetCellText, 3, (intptr_t)Txt_BindLabel(TXT_STATS_TOTAL, 0));

    ScoreCategory *cats = results_score->statsTable;
    Menu_AddRow(managerNum, TXT_STATS_BOND_MOVES, cats[0].achieved, cats[0].target, Ftol(cats[0].points), 0, 0);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_AddItem, (intptr_t)Txt_BindLabel(TXT_STATS_OPPONENTS, 0), 0);
    Menu_AddRow(managerNum, TXT_STATS_DISPATCHED, cats[1].achieved, cats[1].target, Ftol(cats[1].points), 1, 0);
    if (results_score->isAction) {
        Menu_AddRow(managerNum, TXT_STATS_SUBDUED, cats[2].achieved, cats[2].target, Ftol(cats[2].points), 1, 0);
        Menu_AddRow(managerNum, TXT_STATS_SURRENDERED, cats[3].achieved, cats[3].target, Ftol(cats[3].points), 1, 0);
        Menu_AddRowPercentage(managerNum, TXT_STATS_STEALTH, Stats_Percent(cats[4].rating), -1, Ftol(cats[4].points), 0);
        footRows = 3;
    }
    Menu_AddRowPercentage(managerNum, TXT_STATS_ACCURACY, Stats_Percent(cats[5].rating), cats[5].target,
                          Ftol(cats[5].points), 0);
    Menu_AddRowPercentage(managerNum, TXT_STATS_HEALTH, Stats_Percent(cats[6].rating), 100, Ftol(cats[6].points), 0);
    Menu_AddRow(managerNum, TXT_STATS_TIME, results_score->timeTaken, results_score->parTime * 100, Ftol(cats[7].points),
                0, 1);
    Menu_AddRow(managerNum, TXT_STATS_007_BONUS, cats[8].achieved, cats[8].target, Ftol(cats[8].points), 0, 0);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_AddItem, (intptr_t)MenuEmptyString, 0);

    char text[0x100];
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_AddItem, (intptr_t)Txt_BindLabel(TXT_STATS_SUBTOTAL, 0), 0);
    SeparateNumber(results_score->baseScore, text);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetCellText, (footRows + 9) << 16 | 3, (intptr_t)text);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_AddItem, (intptr_t)Txt_BindLabel(TXT_STATS_DIFFICULTY, 0), 0);
    SeparateNumber(results_score->difficultyBonus, text);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetCellText, (footRows + 10) << 16 | 3, (intptr_t)text);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_AddItem, (intptr_t)Txt_BindLabel(TXT_STATS_TOTAL, 0), 0);
    SeparateNumber(results_score->score, text);
    __Menu_Send(managerNum, SUB_P_NFSTATS_LIST, MessageType_ListSetCellText, (footRows + 11) << 16 | 3, (intptr_t)text);
    return true;
}
