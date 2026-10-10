// The pause menu's control (C_GCPAUSE): the tabs of P_PAUSE and the panels they show.
#include "ui.h"

#include "Manager.h"
#include "Menu.h"
#include "MenuManager.h"
#include "../engine/Text.h"
#include "../game.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/Mission.h"
#include "../game/sp/PlayerStats.h"
#include "../input.h"
#include "../sound/SFX.h"

#include "../assets.h"

#include <stdio.h>

#pragma pack(push, 1)

// A List control (0x56c bytes), as far as the pause menu writes it directly.
typedef struct M_LIST {
    M_WIDGET widget;
    char pad_98[0x128 - 0x98];
    M_SCROLL scroll;            // 0x128 scrolls the rows
    char pad_270[0x569 - 0x270];
    uchar unknown569;           // 0x569
    char pad_56a[2];
} M_LIST;
static_assert(sizeof(M_LIST) == 0x56c, "M_LIST is 0x56c bytes");
static_assert(offsetof(M_LIST, scroll) == 0x128, "M_LIST scroll offset incorrect");
static_assert(offsetof(M_LIST, scroll.wrap) == 0x26c, "M_LIST scroll.wrap offset incorrect");

#pragma pack(pop)

// The C_GCPAUSE instances on P_PAUSE, by id (names invented).
enum {
    PAUSE_TAB_OPTIONS = 0,      // tab: shows PAUSE_OPTIONS
    PAUSE_TAB_OBJECTIVES = 1,   // tab: the objectives in PAUSE_PANEL; Select opens the chosen one's description
    PAUSE_TAB_CONTROLS = 3,     // tab: controller style and inversion
    PAUSE_TAB_STATS = 4,        // tab: the level's statistics (single player) or the scores (multiplayer) in PAUSE_PANEL
    PAUSE_OPTIONS = 6,          // list: resume, restart, quit
    PAUSE_PANEL = 8,            // list the objectives and statistics tabs fill
    PAUSE_CONTROL_STYLE = 9,    // radio: the controller style
    PAUSE_CONTROL_LIST = 10,    // list Menu_DisplayControllerStyleList fills
    PAUSE_CONTROLS_11 = 11,     // shown with the controls tab
    PAUSE_CONFIRM_TEXT = 12,    // "restart/quit: are you sure?"
    PAUSE_CONFIRM = 13,         // list: yes, no
    PAUSE_INVERT_TEXT = 14,     // normal / inverted
    PAUSE_OPTIONS_15 = 15,      // shown with the options tab
    PAUSE_CONTROLS_16 = 16,     // shown with the controls tab
};

// PAUSE_OPTIONS' items
enum { PAUSE_RESUME = 0, PAUSE_RESTART = 1, PAUSE_QUIT = 2 };
// PAUSE_CONFIRM's items
enum { PAUSE_CONFIRM_YES = 0, PAUSE_CONFIRM_NO = 1 };

#define OPTIONBOX_OBJECTIVE 10      // (invented name) the box with an objective's description
#define OBJECTIVE_COMPLETED 4       // Objective.status once Mission_MonitorObjectives finds it met
#define PAUSE_SCORE_ROWS 10         // the scores list's rows
#define PAUSE_SCORE_TOTAL_ROW 9     // the teams' totals

// Read by Menu_PlayerHasQuitOrRestarted.
#define user_restarted_or_quit U8_AT(0x0025fb10)

// XBE_GLOBAL(0x0025d820, 0x1)
static bool pause_quitting;         // the confirmation is for quit, not restart
// XBE_GLOBAL(0x0025d821, 0x1)
static bool pause_skip_select;      // the confirmation was answered no: the options list ignores the next Select

// AUTOGEN
void Menu_DisplayControllerStyleList(undefined4 managerNum, Action_TranslatedText style);
// AUTOGEN
char* Timer_Seconds2String(int timeInHundredths, undefined4 timeFormat);
// (AUTOGEN in ui_mp.cpp)
undefined4 Menu_GetMPScore(byte participant);

static int SendToPause(uchar managerNum, int id, uint message, int arg1, int arg2) {
    return __Menu_SendEx(managerNum, C_GCPAUSE, id, message, arg1, arg2);
}

static void SetButtonHint(uchar managerNum, Action_TranslatedText text) {
    LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_BUTTON_HINT_TEXT, Txt_BindLabel(text, 0));
}

static void ShowInversion(uchar managerNum, bool inverted, uint invertedColour, uint normalColour) {
    SendToPause(managerNum, PAUSE_INVERT_TEXT, MessageType_SetColour, inverted ? invertedColour : normalColour, 0);
    SendToPause(managerNum, PAUSE_INVERT_TEXT, MessageType_SetText, (int)Txt_BindLabel(inverted ? CONTROL_INVERTED : CONTROL_NORMAL, 0), 0);
}

static void ShowObjectives(uchar managerNum) {
    SetButtonHint(managerNum, BUTTONS_HINT_SCROLL_NEXT_CONTINUE);
    M_LIST *list = (M_LIST *)SendToPause(managerNum, PAUSE_PANEL, MessageType_GetControl, 0, 0);
    M_CONTROL *control = &list->widget.control;
    list->unknown569 = 1;
    __Menu_SendMessage(control, MessageType_SetLineSpacing, 100, 0);
    __Menu_SendMessage(control, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
    __Menu_SendMessage(control, MessageType_ClearItems, 0, 0);
    __Menu_SendMessage(control, MessageType_ListSetColumnWidth, 0, 6);
    __Menu_SendMessage(control, MessageType_ListAddColumn, 6, 0);
    __Menu_SendMessage(control, MessageType_ListSetColumnAlign, 1, 3);
    __Menu_SendMessage(control, MessageType_ListAddColumn, 0x58, 0);
    list->scroll.wrap = 0;

    short count = Mission_NumVisObjectives();
    for (short i = 0; i < count; i++) {
        OBJ_STATE state;
        Mission_ObjectiveState(&state, i);
        __Menu_SendMessage(control, MessageType_AddItem, (int)"", 0);
        __Menu_SendMessage(control, MessageType_ListSetCellText, i << 16 | 1, (int)(state.status == OBJECTIVE_COMPLETED ? "~J" : "~I"));
        __Menu_SendMessage(control, MessageType_ListSetCellText, i << 16 | 2, (int)Txt_BindLabel((Action_TranslatedText)state.name, 0));
    }
    __Menu_SendMessage(control, MessageType_SelectIndex, count - 1, 0);
}

static void ShowControls(uchar managerNum) {
    SetButtonHint(managerNum, BUTTONS_INVERT_SCROLL_NEXT_CONTINUE);
    PlayerInput *input = &PlayerInputs[manager[managerNum].startPlayer];
    SendToPause(managerNum, PAUSE_CONTROL_STYLE, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
    SendToPause(managerNum, PAUSE_CONTROLS_11, MessageType_SetState, CONTROL_STATE_INERT, 0);
    SendToPause(managerNum, PAUSE_CONTROL_LIST, MessageType_SetState, CONTROL_STATE_INERT, 0);
    SendToPause(managerNum, PAUSE_INVERT_TEXT, MessageType_SetState, CONTROL_STATE_INERT, 0);
    SendToPause(managerNum, PAUSE_CONTROLS_16, MessageType_SetState, CONTROL_STATE_INERT, 0);
    Menu_DisplayControllerStyleList(managerNum, (Action_TranslatedText)input->controlStyle);
    SendToPause(managerNum, PAUSE_CONTROL_STYLE, MessageType_SelectItemByValue, input->controlStyle, 0);
    ShowInversion(managerNum, input->inverted, 0x645a49d2, 0x694646d2);
}

// One row of the statistics: a category's name, what the player did and the target.
static void AddStatsRow(uchar managerNum, int row, const char *name, const char *current, const char *target) {
    SendToPause(managerNum, PAUSE_PANEL, MessageType_AddItem, (int)name, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, row << 16 | 1, (int)current);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, row << 16 | 2, (int)target);
}

// The level's statistics so far, from player 1's score.
static void ShowStats(uchar managerNum) {
    SCORETABLE *score = (SCORETABLE *)PlrStat_GetScore(1);
    if (score == NULL)
        return;
    ScoreCategory *stats = score->statsTable;

    char current[SCORE_NUM_CATEGORIES][16], target[SCORE_NUM_CATEGORIES][16];
    for (int c = SCORE_BOND_MOMENTS; c <= SCORE_ENEMIES_SURRENDERED; c++) {
        sprintf(current[c], "%d", stats[c].achieved);
        sprintf(target[c], "%d", stats[c].target);
    }
    sprintf(current[SCORE_ACCURACY], "%d", stats[SCORE_ACCURACY].achieved);
    sprintf(target[SCORE_ACCURACY], "%d", stats[SCORE_ACCURACY].target);
    sprintf(current[SCORE_HEALTH], "%d", stats[SCORE_HEALTH].achieved);
    sprintf(target[SCORE_HEALTH], "%d", 100);
    sprintf(current[SCORE_TIME], "%s", Timer_Seconds2String(stats[SCORE_TIME].achieved, 0));
    sprintf(target[SCORE_TIME], "%s", Timer_Seconds2String(stats[SCORE_TIME].target * 100, 0));
    sprintf(current[SCORE_BOND_BONUSES], "%d", stats[SCORE_BOND_BONUSES].achieved);
    sprintf(target[SCORE_BOND_BONUSES], "%d", stats[SCORE_BOND_BONUSES].target);

    SendToPause(managerNum, PAUSE_PANEL, MessageType_SetLineSpacing, 0x57, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_SetState, CONTROL_STATE_INERT, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ClearItems, 0, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnWidth, 0, 0x2d);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListAddColumn, 0x1e, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListAddColumn, 0x1e, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnAlign, 1, 2);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnAlign, 2, 2);

    SendToPause(managerNum, PAUSE_PANEL, MessageType_AddItem, (int)Txt_BindLabel(STATS_CATEGORY, 0), 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, 0 << 16 | 1, (int)Txt_BindLabel(STATS_CURRENT, 0));
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, 0 << 16 | 2, (int)Txt_BindLabel(STATS_TARGET, 0));
    SendToPause(managerNum, PAUSE_PANEL, MessageType_AddItem, (int)"", 0);
    AddStatsRow(managerNum, 2, Txt_BindLabel(STATS_BOND_MOVES, 0), current[SCORE_BOND_MOMENTS], target[SCORE_BOND_MOMENTS]);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_AddItem, (int)Txt_BindLabel(STATS_OPPONENTS, 0), 0);
    // the three kinds of opponent, indented under it
    char name[256];
    sprintf(name, "  %s", Txt_BindLabel(STATS_DISPATCHED, 0));
    AddStatsRow(managerNum, 4, name, current[SCORE_ENEMIES_DISPATCHED], target[SCORE_ENEMIES_DISPATCHED]);
    sprintf(name, "  %s", Txt_BindLabel(STATS_SUBDUED, 0));
    AddStatsRow(managerNum, 5, name, current[SCORE_ENEMIES_DISABLED], target[SCORE_ENEMIES_DISABLED]);
    sprintf(name, "  %s", Txt_BindLabel(STATS_SURRENDERED, 0));
    AddStatsRow(managerNum, 6, name, current[SCORE_ENEMIES_SURRENDERED], target[SCORE_ENEMIES_SURRENDERED]);
    AddStatsRow(managerNum, 7, Txt_BindLabel(STATS_ACCURACY_RATING, 0), current[SCORE_ACCURACY], target[SCORE_ACCURACY]);
    AddStatsRow(managerNum, 8, Txt_BindLabel(STATS_HEALTH_REMAINING, 0), current[SCORE_HEALTH], target[SCORE_HEALTH]);
    AddStatsRow(managerNum, 9, Txt_BindLabel(STATS_TIME, 0), current[SCORE_TIME], target[SCORE_TIME]);
    AddStatsRow(managerNum, 10, Txt_BindLabel(STATS_007_BONUS, 0), current[SCORE_BOND_BONUSES], target[SCORE_BOND_BONUSES]);
}

// A column of the scores list: MI6's on the left (everyone's, without teams), Phoenix's on the right.
struct ScoreColumn {
    int nameColumn;
    uchar row;      // the next free row
    int total;      // the team's score
};

static void AddScore(uchar managerNum, int agent, bool teams, ScoreColumn *mi6, ScoreColumn *phoenix) {
    char score[32];
    sprintf(score, "%d", Menu_GetMPScore(agent));
    MPSettings_PerPlayer *player = &MPSettings.Player[agent];
    ScoreColumn *column = player->TeamId == MI6 || !teams ? mi6 : phoenix;
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, column->row << 16 | column->nameColumn, (int)player->Name);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, column->row << 16 | (column->nameColumn + 1), (int)score);
    column->row++;
    if (teams) {
        if (player->TeamId == MI6)
            mi6->total += Menu_GetMPScore(agent);
        else
            phoenix->total += Menu_GetMPScore(agent);
    }
}

// Every player's and bot's score, by team in a team game, and the teams' totals.
static void ShowScores(uchar managerNum) {
    SendToPause(managerNum, PAUSE_PANEL, MessageType_SetLineSpacing, 100, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_SetState, CONTROL_STATE_INERT, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ClearItems, 0, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnWidth, 0, 0x14);
    for (int i = 0; i < 4; i++)
        SendToPause(managerNum, PAUSE_PANEL, MessageType_ListAddColumn, 0x14, 0);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnAlign, 0, 1);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnAlign, 1, 2);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnAlign, 2, 1);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnAlign, 3, 1);
    SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetColumnAlign, 4, 2);
    for (int i = 0; i < PAUSE_SCORE_ROWS; i++)
        SendToPause(managerNum, PAUSE_PANEL, MessageType_AddItem, 0, 0);

    ScoreColumn mi6 = { 0, 1, 0 }, phoenix = { 3, 1, 0 };
    bool teams = (MPSettings.GameMode & TEAMGAME) != GM_QUICK;
    if (teams) {
        // the teams' names head the columns
        SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, 0 << 16 | 0, (int)Txt_BindLabel(MP_TEAM_MI6, 0));
        SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, 0 << 16 | 3, (int)Txt_BindLabel(MP_TEAM_PHOENIX, 0));
        mi6.row = 2;
        phoenix.row = 2;
    }
    // the counts compared signed, as the original
    for (int p = 0; p < (int)MPSettings.numPlayers; p++)
        AddScore(managerNum, p, teams, &mi6, &phoenix);
    for (int p = NUM_PLAYERS; p < (int)MPSettings.numBots + NUM_PLAYERS; p++)
        AddScore(managerNum, p, teams, &mi6, &phoenix);

    if (teams) {
        char total[32];
        sprintf(total, "%d", mi6.total);
        SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, PAUSE_SCORE_TOTAL_ROW << 16 | 0, (int)Txt_BindLabel(STATS_TOTAL, 0));
        SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, PAUSE_SCORE_TOTAL_ROW << 16 | 1, (int)total);
        sprintf(total, "%d", phoenix.total);
        SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, PAUSE_SCORE_TOTAL_ROW << 16 | 3, (int)Txt_BindLabel(STATS_TOTAL, 0));
        SendToPause(managerNum, PAUSE_PANEL, MessageType_ListSetCellText, PAUSE_SCORE_TOTAL_ROW << 16 | 4, (int)total);
    }
}

// Focus on a tab shows its panel and hides the others.
static void GainFocus(uchar managerNum, M_CONTROL *control) {
    static const int panels[] = { PAUSE_OPTIONS, PAUSE_PANEL, PAUSE_CONTROL_STYLE, PAUSE_CONTROL_LIST, PAUSE_CONTROLS_11,
        PAUSE_CONFIRM_TEXT, PAUSE_CONFIRM, PAUSE_INVERT_TEXT, PAUSE_CONTROLS_16, PAUSE_OPTIONS_15 };
    for (int id : panels)
        SendToPause(managerNum, id, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    __Menu_Send(managerNum, C_GCPAUSE, MessageType_SetPulseColour, 0x806d59ff, 0);

    switch (control->id) {
    case PAUSE_TAB_OPTIONS:
        SetButtonHint(managerNum, BUTTONS_SELECT_SCROLL_NEXT_CONTINUE);
        SendToPause(managerNum, PAUSE_OPTIONS, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
        SendToPause(managerNum, PAUSE_OPTIONS, MessageType_SetLineSpacing, 0xaf, 0);
        SendToPause(managerNum, PAUSE_OPTIONS_15, MessageType_SetState, CONTROL_STATE_INERT, 0);
        break;
    case PAUSE_TAB_OBJECTIVES:
        ShowObjectives(managerNum);
        break;
    case PAUSE_TAB_CONTROLS:
        ShowControls(managerNum);
        break;
    case PAUSE_TAB_STATS:
        SetButtonHint(managerNum, BUTTONS_NEXT_CONTINUE);
        if (MPSettings.isMultiplayer)
            ShowScores(managerNum);
        else
            ShowStats(managerNum);
        break;
    }
}

// Restart and quit ask first: the options list hides and the question shows, with "no" selected.
static void AskToConfirm(uchar managerNum, Action_TranslatedText question) {
    SetButtonHint(managerNum, BUTTONS_SELECT_BACK_SCROLL_CONTINUE);
    SendToPause(managerNum, PAUSE_OPTIONS, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
    SendToPause(managerNum, PAUSE_CONFIRM_TEXT, MessageType_SetState, CONTROL_STATE_INERT, 0);
    SendToPause(managerNum, PAUSE_CONFIRM_TEXT, MessageType_SetText, (int)Txt_BindLabel(question, 0), 0);
    SendToPause(managerNum, PAUSE_CONFIRM, MessageType_SelectIndex, PAUSE_CONFIRM_NO, 0);
    SendToPause(managerNum, PAUSE_CONFIRM, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
}

static void Select(uchar managerNum, M_CONTROL *control) {
    switch (control->id) {
    case PAUSE_TAB_OBJECTIVES: {
        OBJ_STATE state;
        Mission_ObjectiveState(&state, SendToPause(managerNum, PAUSE_PANEL, MessageType_GetSelectedIndex, 0, 0));
        if (state.description == Action_TranslatedText_NULLVALUE)
            state.description = OBJECTIVE_NO_DESCRIPTION;
        SendToPause(managerNum, PAUSE_PANEL, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        Menu_CreateOptionBoxLabel(managerNum, (Action_TranslatedText)state.description, OPTIONBOX_OBJECTIVE, 1, 0);
        __Menu_Send(managerNum, C_LBMSGOPTIONS, MessageType_SetPulseColour, 0x645a4cff, 0);
        break;
    }
    case PAUSE_OPTIONS: {
        if (pause_skip_select) {
            pause_skip_select = false;
            break;
        }
        uchar choice = SendToPause(managerNum, PAUSE_OPTIONS, MessageType_GetSelectedIndex, 0, 0);
        if (choice == PAUSE_RESUME) {
            MenuManager_Delete(managerNum);
        } else if (choice == PAUSE_RESTART) {
            pause_quitting = false;
            AskToConfirm(managerNum, RESTART_CONFIRMATION);
        } else if (choice == PAUSE_QUIT) {
            pause_quitting = true;
            AskToConfirm(managerNum, QUIT_CONFIRMATION);
        }
        break;
    }
    case PAUSE_CONFIRM: {
        uchar choice = SendToPause(managerNum, PAUSE_CONFIRM, MessageType_GetSelectedIndex, 0, 0);
        if (choice == PAUSE_CONFIRM_YES) {
            GameState.VibrationEnabled = 0;
            if (pause_quitting) {
                GameState.ReloadMenupage = MPSettings.isMultiplayer ? P_MPDEBRIEFING : P_NFMAP;
                ResetMap_LevelToLoad(HT_Level_Menu_Pre, false, false);
            } else {
                ResetMap_LevelToLoad(Mission_BaseMapHCode(), false, true);
            }
            SFXFadeDown(1);
            GameFlow_PushState(7, 80.0f, 0xff);
            MenuManager_Delete(managerNum);
            GameState.maybePaused = 1;
            user_restarted_or_quit = 1;
        } else if (choice == PAUSE_CONFIRM_NO) {
            SetButtonHint(managerNum, BUTTONS_SELECT_SCROLL_NEXT_CONTINUE);
            SendToPause(managerNum, PAUSE_CONFIRM_TEXT, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            SendToPause(managerNum, PAUSE_CONFIRM, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
            SendToPause(managerNum, PAUSE_OPTIONS, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
            pause_skip_select = true;
        }
        break;
    }
    }
}

// The pause menu: fourteen instances on P_PAUSE, told apart by id. Focus on a tab switches the panel; the
// options list resumes, or restarts or quits the level once confirmed; the controller-style radio changes the
// player's style as it scrolls, and X inverts.
// AUTOINJECT
bool C_GCPAUSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
    case MessageType_Scroll: {
        uchar player = manager[managerNum].startPlayer;
        if (control->id == PAUSE_CONTROL_STYLE) {
            int style = SendToPause(managerNum, PAUSE_CONTROL_STYLE, MessageType_GetSelectedItemValue, 0, 0);
            Menu_ChangeControllerStyle(player, style);
            Menu_DisplayControllerStyleList(managerNum, (Action_TranslatedText)style);
        }
        break;
    }
    case MessageType_Select:
        Select(managerNum, control);
        break;
    case MessageType_GainFocus:
        GainFocus(managerNum, control);
        break;
    case MessageType_AltSelect2:
        if (control->id == PAUSE_CONTROL_STYLE) {
            PlayerInput *input = &PlayerInputs[manager[managerNum].startPlayer];
            input->inverted = !input->inverted;
            ShowInversion(managerNum, input->inverted, 0x645a49c0, 0x644646c0);
        }
        break;
    }
    return true;
}
