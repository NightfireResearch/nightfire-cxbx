// The pause menu (P_PAUSE and the tabs and lists of its C_GCPAUSE instances) and the menu at the end of a failed
// mission (P_ENDMISSION) (PS2: menu_handlers.cpp)

#include "ui.h"
#include "MenuCommon.h"
#include "Manager.h"
#include "Menu.h"
#include "MenuManager.h"

#include "../game.h"
#include "../input.h"
#include "../engine/Text.h"
#include "../game/mp/multiplayer.h"
#include "../game/obj/Player.h"
#include "../game/obj/PlayerWeapon.h"
#include "../game/sp/Mission.h"
#include "../game/sp/PlayerStats.h"
#include "../sound/SFX.h"

#include <stdio.h>

// Not reimplemented, called by address

extern uint8_t user_restarted_or_quit;  // MenuManager.cpp

// The pause menu's controls: instances of C_GCPAUSE told apart by their ids
#define PAUSE_TAB_OBJECTIVES_TEXT   0   // the tabs: GainFocus shows their panel
#define PAUSE_TAB_OBJECTIVES        1   //   A on it opens the hint of the objective picked in the list
#define PAUSE_TAB_CONTROLS          3
#define PAUSE_TAB_SCORES            4
#define PAUSE_RESUME_LIST           6   // resume, restart, quit
#define PAUSE_PANEL_LIST            8   // the objectives, or the scores
#define PAUSE_STYLE_RADIO           9   // the controller style
#define PAUSE_STYLE_LIST            10  // its buttons (Menu_DisplayControllerStyleList)
#define PAUSE_CONFIRM_TEXT          12
#define PAUSE_CONFIRM_LIST          13  // yes, no
#define PAUSE_INVERTED_TEXT         14
#define SUB_P_PAUSE_HINT_TEXT       ((HASHCODE)0x1000006f)
#define SUB_P_PAUSE_INVERTED        ((HASHCODE)0x100000c9)
#define SUB_P_PAUSE_ICON_184        ((HASHCODE)0x10000184)
#define SUB_P_PAUSE_BUILD_TEXT      ((HASHCODE)0x100000d6)

#define TXT_PAUSE_HINT          ((Action_TranslatedText)0x1000217)  // "~A Select  ~V Scroll  ~H Next  ~S Continue"
#define TXT_PAUSE_HINT_OBJECTIVES ((Action_TranslatedText)0x1000218)
#define TXT_PAUSE_HINT_CONTROLS ((Action_TranslatedText)0x100021a)
#define TXT_PAUSE_HINT_SCORES   ((Action_TranslatedText)0x100021b)
#define TXT_PAUSE_HINT_CONFIRM  ((Action_TranslatedText)0x10002d6)
#define TXT_RESTART_CONFIRM     ((Action_TranslatedText)0x1ff)      // "RESTART: ARE YOU SURE?"
#define TXT_QUIT_CONFIRM        ((Action_TranslatedText)0x200)
#define TXT_NO_HINT             ((Action_TranslatedText)0x31d)      // "HINT:\nSample hint text can go here..."
#define TXT_INVERTED            ((Action_TranslatedText)0x206)
#define TXT_NORMAL              ((Action_TranslatedText)0x3bd)
#define TXT_TOTAL               ((Action_TranslatedText)0x163)

#define OPTIONBOX_OBJECTIVE_HINT 10

static void Pause_SetState(uchar managerNum, int id, int state) {
    __Menu_SendEx(managerNum, C_GCPAUSE, id, MessageType_SetState, state, 0);
}

// ---- the pause page

// XBE_GLOBAL(0x0025d7e0, 0x40)
char pause_build_text[0x40];

// The game's build string: Menu_SetLevelBonus, given a level not in sp_level, writes 1 over the space after the time
// XBE_GLOBAL(0x0017c560, 0x1d)
char PauseBuildString[] = "<!> Oct 20 2002 04:26:28 <!>";

// The styles' names, for the radio (as P_CNCONTROLS)
static const Action_TranslatedText pause_style_names[8] = {
    (Action_TranslatedText)0x28, (Action_TranslatedText)0x37, (Action_TranslatedText)0x46, (Action_TranslatedText)0x55,
    (Action_TranslatedText)0x64, (Action_TranslatedText)0x73, (Action_TranslatedText)0x100016d,
    (Action_TranslatedText)0x100017c,
};

// Entering: in multiplayer the objectives tab is hidden and the two others are centred on the screen; the controller
// style radio is filled, every panel hidden. B with the confirmation showing goes back to the resume list instead of
// closing the menu; an option box answered shows the panel list again.
// AUTOINJECT
bool P_PAUSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    uchar player = manager[managerNum].startPlayer;
    switch (message) {
    case MessageType_QueryBack: {
        M_CONTROL *confirm = (M_CONTROL *)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_CONFIRM_LIST, MessageType_GetControl, 0, 0);
        if (MenuManager_GetStatus() != 3 || confirm->state != CONTROL_STATE_SHOWN)
            break;
        LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT, 0));
        Pause_SetState(managerNum, PAUSE_CONFIRM_TEXT, CONTROL_STATE_HIDDEN);
        Pause_SetState(managerNum, PAUSE_CONFIRM_LIST, CONTROL_STATE_HIDDEN);
        Pause_SetState(managerNum, PAUSE_RESUME_LIST, CONTROL_STATE_SHOWN);
        Pause_SetState(managerNum, 15, CONTROL_STATE_INERT);
        break;
    }
    case MessageType_PageUpdate:
        if (Menu_UpdateOptionBox(NULL) != 0)
            Pause_SetState(managerNum, PAUSE_PANEL_LIST, CONTROL_STATE_SHOWN);
        break;
    case MessageType_PageEnter: {
        if (MPSettings.isMultiplayer) {
            Pause_SetState(managerNum, PAUSE_TAB_OBJECTIVES, CONTROL_STATE_HIDDEN);
            M_CONTROL *text = (M_CONTROL *)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_TAB_OBJECTIVES_TEXT, MessageType_GetControl, 0, 0);
            M_CONTROL *controls = (M_CONTROL *)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_TAB_CONTROLS, MessageType_GetControl, 0, 0);
            M_CONTROL *scores = (M_CONTROL *)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_TAB_SCORES, MessageType_GetControl, 0, 0);
            if (text != NULL && controls != NULL) {
                short width = controls->width;
                controls->x = (short)Ftol(320.0f - (double)(width / 2));
                scores->x = controls->x + width + 10;
                text->x = controls->x - text->width - 10;
            }
        }
        for (int style = 0; style < 8; style++)
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_STYLE_RADIO, MessageType_AddItem,
                          (intptr_t)Txt_BindLabel(pause_style_names[style], 0), style);
        __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_RESUME_LIST, MessageType_SelectIndex, 0, 0);
        __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_STYLE_RADIO, MessageType_SelectItemByValue,
                      PlayerInputs[player].controlStyle, 0);
        for (int id = PAUSE_PANEL_LIST; id <= PAUSE_CONFIRM_LIST; id++)
            Pause_SetState(managerNum, id, CONTROL_STATE_HIDDEN);
        __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_CONFIRM_LIST, MessageType_SetLineSpacing, 175, 0);
        Pause_SetState(managerNum, PAUSE_INVERTED_TEXT, CONTROL_STATE_HIDDEN);
        Pause_SetState(managerNum, 16, CONTROL_STATE_HIDDEN);
        __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_RESUME_LIST, MessageType_ListSetColumnAlign, 0, ALIGN_CENTRE);
        __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_RESUME_LIST, MessageType_SetLineSpacing, 175, 0);
        __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_CONFIRM_LIST, MessageType_ListSetColumnAlign, 0, ALIGN_CENTRE);
        __Menu_Send(managerNum, C_GCPAUSE, MessageType_SetPulseColour, 0x806d59ff, 0);
        __Menu_Send(managerNum, C_GCPAUSE, MessageType_SetTextFlag2000, 0, 0);
        __Menu_Send(managerNum, SUB_P_PAUSE_HINT_TEXT, MessageType_SetTextFlag2000, 0, 0);
        __Menu_Send(managerNum, SUB_P_PAUSE_INVERTED, MessageType_SetTextFlag2000, 0, 0);
        __Menu_Send(managerNum, SUB_P_PAUSE_INVERTED, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
        LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT, 0));
        if (PlayerInputs[player].inverted) {
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetColour, 0x645a49d2, 0);
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetText,
                          (intptr_t)Txt_BindLabel(TXT_INVERTED, 0), 0);
        } else {
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetColour, 0x694646d2, 0);
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetText,
                          (intptr_t)Txt_BindLabel(TXT_NORMAL, 0), 0);
        }
        CheatInfo.pauseCleared = 0;
        __Menu_Send(managerNum, SUB_P_PAUSE_ICON_184, MessageType_SetIconSpriteFlags, 0x100, 0);
        Manager_SendMessage(&manager[managerNum], MessageType_SetCursor, C_GCPAUSE, 0);
        sprintf(pause_build_text, "%s", PauseBuildString);
        LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_BUILD_TEXT, pause_build_text);
        break;
    }
    }
    return true;
}

// ---- the panels a tab shows

// The objectives so far, a tick for each completed
static void Pause_ShowObjectives(uchar managerNum) {
    LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT_OBJECTIVES, 0));
    M_LIST *list = (M_LIST *)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_PANEL_LIST, MessageType_GetControl, 0, 0);
    list->wrap = 1;
    __Menu_SendMessage(&list->control, MessageType_SetLineSpacing, 100, 0);
    __Menu_SendMessage(&list->control, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
    __Menu_SendMessage(&list->control, MessageType_ClearItems, 0, 0);
    __Menu_SendMessage(&list->control, MessageType_ListSetColumnWidth, 0, 6);
    __Menu_SendMessage(&list->control, MessageType_ListAddColumn, 6, 0);
    __Menu_SendMessage(&list->control, MessageType_ListSetColumnAlign, 1, ALIGN_CENTRE);
    __Menu_SendMessage(&list->control, MessageType_ListAddColumn, 88, 0);
    list->scroll.wrap = 0;
    short numObjectives = (short)Mission_NumVisObjectives();
    for (short i = 0; i < numObjectives; i++) {
        OBJ_STATE state;
        Mission_ObjectiveState(&state, i);
        __Menu_SendMessage(&list->control, MessageType_AddItem, (intptr_t)MenuEmptyString, 0);
        __Menu_SendMessage(&list->control, MessageType_ListSetCellText, i << 16 | 1, (intptr_t)(state.status == 4 ? "~J" : "~I"));
        __Menu_SendMessage(&list->control, MessageType_ListSetCellText, i << 16 | 2,
                           (intptr_t)Txt_BindLabel((Action_TranslatedText)state.name, 0));
    }
    __Menu_SendMessage(&list->control, MessageType_SelectIndex, numObjectives - 1, 0);
}

// The controller style and the look's inversion
static void Pause_ShowControls(uchar managerNum) {
    LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT_CONTROLS, 0));
    uchar player = manager[managerNum].startPlayer;
    Pause_SetState(managerNum, PAUSE_STYLE_RADIO, CONTROL_STATE_SHOWN);
    Pause_SetState(managerNum, 11, CONTROL_STATE_INERT);
    Pause_SetState(managerNum, PAUSE_STYLE_LIST, CONTROL_STATE_INERT);
    Pause_SetState(managerNum, PAUSE_INVERTED_TEXT, CONTROL_STATE_INERT);
    Pause_SetState(managerNum, 16, CONTROL_STATE_INERT);
    Menu_DisplayControllerStyleList(managerNum, PlayerInputs[player].controlStyle);
    __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_STYLE_RADIO, MessageType_SelectItemByValue,
                  PlayerInputs[player].controlStyle, 0);
    __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetColour,
                  PlayerInputs[player].inverted ? 0x645a49d2 : 0x694646d2, 0);
    __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetText,
                  (intptr_t)Txt_BindLabel(PlayerInputs[player].inverted ? TXT_INVERTED : TXT_NORMAL, 0), 0);
}

static void Pause_PanelSend(uchar managerNum, uint message, intptr_t arg1, intptr_t arg2) {
    __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_PANEL_LIST, message, arg1, arg2);
}

// A player's name and score in the left column (MI6, or everyone without teams) or the right (Phoenix), and its
// team's total
static void Pause_AddScore(uchar managerNum, uint participant, uchar *mi6Row, uchar *phoenixRow, int *mi6Total,
                           int *phoenixTotal) {
    char text[0x20];
    sprintf(text, "%d", Menu_GetMPScore(participant));
    bool teams = (MPSettings.GameMode & TEAMGAME) != GM_QUICK;
    MPSettings_PerPlayer *player = &MPSettings.Player[participant];
    if (player->TeamId == MI6 || !teams) {
        Pause_PanelSend(managerNum, MessageType_ListSetCellText, *mi6Row << 16, (intptr_t)player->Name);
        Pause_PanelSend(managerNum, MessageType_ListSetCellText, *mi6Row << 16 | 1, (intptr_t)text);
        (*mi6Row)++;
    } else {
        Pause_PanelSend(managerNum, MessageType_ListSetCellText, *phoenixRow << 16 | 3, (intptr_t)player->Name);
        Pause_PanelSend(managerNum, MessageType_ListSetCellText, *phoenixRow << 16 | 4, (intptr_t)text);
        (*phoenixRow)++;
    }
    if ((MPSettings.GameMode & TEAMGAME) != GM_QUICK) {
        if (player->TeamId == MI6)
            *mi6Total += Menu_GetMPScore(participant);
        else
            *phoenixTotal += Menu_GetMPScore(participant);
    }
}

// The scores: in multiplayer every player's and bot's (and the teams' totals), in a mission its statistics so far
static void Pause_ShowScores(uchar managerNum) {
    LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT_SCORES, 0));
    if (MPSettings.isMultiplayer) {
        Pause_PanelSend(managerNum, MessageType_SetLineSpacing, 100, 0);
        Pause_PanelSend(managerNum, MessageType_SetState, CONTROL_STATE_INERT, 0);
        Pause_PanelSend(managerNum, MessageType_ClearItems, 0, 0);
        Pause_PanelSend(managerNum, MessageType_ListSetColumnWidth, 0, 20);
        for (int column = 1; column < 5; column++)
            Pause_PanelSend(managerNum, MessageType_ListAddColumn, 20, 0);
        static const uchar align[5] = {1, ALIGN_RIGHT, 1, 1, ALIGN_RIGHT};
        for (int column = 0; column < 5; column++)
            Pause_PanelSend(managerNum, MessageType_ListSetColumnAlign, column, align[column]);
        for (int row = 0; row < 10; row++)
            Pause_PanelSend(managerNum, MessageType_AddItem, 0, 0);
        uchar mi6Row = 1, phoenixRow = 1;
        int mi6Total = 0, phoenixTotal = 0;
        if ((MPSettings.GameMode & TEAMGAME) != GM_QUICK) {
            Pause_PanelSend(managerNum, MessageType_ListSetCellText, 0, (intptr_t)Txt_BindLabel(MP_TEAM_MI6, 0));
            Pause_PanelSend(managerNum, MessageType_ListSetCellText, 3, (intptr_t)Txt_BindLabel(MP_TEAM_PHOENIX, 0));
            mi6Row = 2;
            phoenixRow = 2;
        }
        for (int i = 0; i < (int)MPSettings.numPlayers; i++)
            Pause_AddScore(managerNum, i, &mi6Row, &phoenixRow, &mi6Total, &phoenixTotal);
        for (int i = NUM_PLAYERS; i < (int)MPSettings.numBots + NUM_PLAYERS; i++)
            Pause_AddScore(managerNum, i, &mi6Row, &phoenixRow, &mi6Total, &phoenixTotal);
        if ((MPSettings.GameMode & TEAMGAME) != GM_QUICK) {
            char text[0x20];
            sprintf(text, "%d", mi6Total);
            Pause_PanelSend(managerNum, MessageType_ListSetCellText, 9 << 16, (intptr_t)Txt_BindLabel(TXT_TOTAL, 0));
            Pause_PanelSend(managerNum, MessageType_ListSetCellText, 9 << 16 | 1, (intptr_t)text);
            sprintf(text, "%d", phoenixTotal);
            Pause_PanelSend(managerNum, MessageType_ListSetCellText, 9 << 16 | 3, (intptr_t)Txt_BindLabel(TXT_TOTAL, 0));
            Pause_PanelSend(managerNum, MessageType_ListSetCellText, 9 << 16 | 4, (intptr_t)text);
        }
        return;
    }

    // The original asks for player 1's score here (P_NFSTATS asks for player 0's)
    SCORETABLE *score = (SCORETABLE *)PlrStat_GetScore(1);
    if (score == NULL)
        return;
    ScoreCategory *cats = score->statsTable;
    char numbers[16][0x10];
    sprintf(numbers[0], "%d", cats[0].achieved);
    sprintf(numbers[1], "%d", cats[0].target);
    sprintf(numbers[2], "%d", cats[1].achieved);
    sprintf(numbers[3], "%d", cats[1].target);
    sprintf(numbers[4], "%d", cats[2].achieved);
    sprintf(numbers[5], "%d", cats[2].target);
    sprintf(numbers[6], "%d", cats[3].achieved);
    sprintf(numbers[7], "%d", cats[3].target);
    sprintf(numbers[8], "%d", cats[5].achieved);
    sprintf(numbers[9], "%d", cats[5].target);
    sprintf(numbers[10], "%d", cats[6].achieved);
    sprintf(numbers[11], "%d", 100);
    sprintf(numbers[12], "%s", Timer_Seconds2String(cats[7].achieved, 0));
    sprintf(numbers[13], "%s", Timer_Seconds2String(cats[7].target * 100, 0));
    sprintf(numbers[14], "%d", cats[8].achieved);
    sprintf(numbers[15], "%d", cats[8].target);

    Pause_PanelSend(managerNum, MessageType_SetLineSpacing, 87, 0);
    Pause_PanelSend(managerNum, MessageType_SetState, CONTROL_STATE_INERT, 0);
    Pause_PanelSend(managerNum, MessageType_ClearItems, 0, 0);
    Pause_PanelSend(managerNum, MessageType_ListSetColumnWidth, 0, 45);
    Pause_PanelSend(managerNum, MessageType_ListAddColumn, 30, 0);
    Pause_PanelSend(managerNum, MessageType_ListAddColumn, 30, 0);
    Pause_PanelSend(managerNum, MessageType_ListSetColumnAlign, 1, ALIGN_RIGHT);
    Pause_PanelSend(managerNum, MessageType_ListSetColumnAlign, 2, ALIGN_RIGHT);
    Pause_PanelSend(managerNum, MessageType_AddItem, (intptr_t)Txt_BindLabel((Action_TranslatedText)0x1000204, 0), 0); // "Category"
    Pause_PanelSend(managerNum, MessageType_ListSetCellText, 1, (intptr_t)Txt_BindLabel((Action_TranslatedText)0x10001e1, 0));
    Pause_PanelSend(managerNum, MessageType_ListSetCellText, 2, (intptr_t)Txt_BindLabel((Action_TranslatedText)0x10001e2, 0));
    Pause_PanelSend(managerNum, MessageType_AddItem, (intptr_t)MenuEmptyString, 0);

    // The rows from row 2: a label (indented by two spaces for the opponents' kinds) and two numbers
    static const struct { Action_TranslatedText label; uchar indent; uchar row; } rows[9] = {
        {(Action_TranslatedText)0x165, 0, 2},   // "Bond Moves"
        {(Action_TranslatedText)0x15a, 0, 0},   // "Opponents": a heading
        {(Action_TranslatedText)0x15b, 1, 4},
        {(Action_TranslatedText)0x15c, 1, 5},
        {(Action_TranslatedText)0x16e, 1, 6},
        {(Action_TranslatedText)0x15e, 0, 7},   // "Accuracy Rating"
        {(Action_TranslatedText)0x15f, 0, 8},
        {(Action_TranslatedText)0x160, 0, 9},   // "Time"
        {(Action_TranslatedText)0x161, 0, 10},
    };
    int number = 0;
    for (int i = 0; i < ARRAY_SIZE(rows); i++) {
        if (rows[i].indent) {
            char text[0x100];
            sprintf(text, "  %s", Txt_BindLabel(rows[i].label, 0));
            Pause_PanelSend(managerNum, MessageType_AddItem, (intptr_t)text, 0);
        } else {
            Pause_PanelSend(managerNum, MessageType_AddItem, (intptr_t)Txt_BindLabel(rows[i].label, 0), 0);
        }
        if (rows[i].row == 0)
            continue;
        Pause_PanelSend(managerNum, MessageType_ListSetCellText, rows[i].row << 16 | 1, (intptr_t)numbers[number]);
        Pause_PanelSend(managerNum, MessageType_ListSetCellText, rows[i].row << 16 | 2, (intptr_t)numbers[number + 1]);
        number += 2;
    }
}

// The objective picked: its hint in a box (a stand-in text when it has none)
static void Pause_ShowHint(uchar managerNum) {
    OBJ_STATE state;
    Mission_ObjectiveState(&state, (short)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_PANEL_LIST,
                                                        MessageType_GetSelectedIndex, 0, 0));
    if (state.description == Action_TranslatedText_NULLVALUE)
        state.description = TXT_NO_HINT;
    Pause_SetState(managerNum, PAUSE_PANEL_LIST, CONTROL_STATE_HIDDEN);
    Menu_CreateOptionBoxLabel(managerNum, (Action_TranslatedText)state.description, OPTIONBOX_OBJECTIVE_HINT, 1, 0);
    __Menu_Send(managerNum, C_LBMSGOPTIONS, MessageType_SetPulseColour, 0x645a4cff, 0);
}

// ---- restart and quit

// The last choice of the resume list: quit, else restart
// XBE_GLOBAL(0x0025d820, 0x1)
uint8_t pause_quitting;
// The confirmation was just answered no (the same A reaches the resume list, which then does nothing)
// XBE_GLOBAL(0x0025d821, 0x1)
uint8_t pause_confirm_closed;

static void Pause_AskConfirm(uchar managerNum, Action_TranslatedText question) {
    LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT_CONFIRM, 0));
    Pause_SetState(managerNum, PAUSE_RESUME_LIST, CONTROL_STATE_HIDDEN);
    Pause_SetState(managerNum, PAUSE_CONFIRM_TEXT, CONTROL_STATE_INERT);
    __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_CONFIRM_TEXT, MessageType_SetText, (intptr_t)Txt_BindLabel(question, 0), 0);
    __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_CONFIRM_LIST, MessageType_SelectIndex, 1, 0);
    Pause_SetState(managerNum, PAUSE_CONFIRM_LIST, CONTROL_STATE_SHOWN);
}

// The pause menu's tabs, lists and radio. A tab gaining the focus hides every panel and shows its own; the resume list
// resumes, or asks before restarting the mission or quitting to the front end; the style radio changes the style and
// X inverts the look.
// AUTOINJECT
bool C_GCPAUSE_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_GainFocus:
        Pause_SetState(managerNum, PAUSE_RESUME_LIST, CONTROL_STATE_HIDDEN);
        for (int id = PAUSE_PANEL_LIST; id <= PAUSE_INVERTED_TEXT; id++)
            Pause_SetState(managerNum, id, CONTROL_STATE_HIDDEN);
        Pause_SetState(managerNum, 16, CONTROL_STATE_HIDDEN);
        Pause_SetState(managerNum, 15, CONTROL_STATE_HIDDEN);
        __Menu_Send(managerNum, C_GCPAUSE, MessageType_SetPulseColour, 0x806d59ff, 0);
        switch (control->id) {
        case PAUSE_TAB_OBJECTIVES_TEXT:
            LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT, 0));
            Pause_SetState(managerNum, PAUSE_RESUME_LIST, CONTROL_STATE_SHOWN);
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_RESUME_LIST, MessageType_SetLineSpacing, 175, 0);
            Pause_SetState(managerNum, 15, CONTROL_STATE_INERT);
            break;
        case PAUSE_TAB_OBJECTIVES:
            Pause_ShowObjectives(managerNum);
            break;
        case PAUSE_TAB_CONTROLS:
            Pause_ShowControls(managerNum);
            break;
        case PAUSE_TAB_SCORES:
            Pause_ShowScores(managerNum);
            break;
        }
        break;

    case MessageType_Select:
        switch (control->id) {
        case PAUSE_TAB_OBJECTIVES:
            Pause_ShowHint(managerNum);
            break;
        case PAUSE_RESUME_LIST:
            if (pause_confirm_closed) {
                pause_confirm_closed = 0;
                break;
            }
            switch ((uchar)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_RESUME_LIST, MessageType_GetSelectedIndex, 0, 0)) {
            case 0:
                MenuManager_Delete(managerNum);
                break;
            case 1:
                pause_quitting = 0;
                Pause_AskConfirm(managerNum, TXT_RESTART_CONFIRM);
                break;
            case 2:
                pause_quitting = 1;
                Pause_AskConfirm(managerNum, TXT_QUIT_CONFIRM);
                break;
            }
            break;
        case PAUSE_CONFIRM_LIST:
            switch ((uchar)__Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_CONFIRM_LIST, MessageType_GetSelectedIndex, 0, 0)) {
            case 0:     // yes
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
                GameState.LevelEnding = 1;
                user_restarted_or_quit = 1;
                break;
            case 1:     // no
                LABEL_SET_TEXT(managerNum, SUB_P_PAUSE_HINT_TEXT, Txt_BindLabel(TXT_PAUSE_HINT, 0));
                Pause_SetState(managerNum, PAUSE_CONFIRM_TEXT, CONTROL_STATE_HIDDEN);
                Pause_SetState(managerNum, PAUSE_CONFIRM_LIST, CONTROL_STATE_HIDDEN);
                Pause_SetState(managerNum, PAUSE_RESUME_LIST, CONTROL_STATE_SHOWN);
                pause_confirm_closed = 1;
                break;
            }
            break;
        }
        break;

    case MessageType_Scroll:
        if (control->id == PAUSE_STYLE_RADIO) {
            uchar player = manager[managerNum].startPlayer;
            int style = __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_STYLE_RADIO, MessageType_GetSelectedItemValue, 0, 0);
            Menu_ChangeControllerStyle(player, style);
            Menu_DisplayControllerStyleList(managerNum, style);
        }
        break;

    case MessageType_AltSelect2:
        if (control->id == PAUSE_STYLE_RADIO) {
            PlayerInput *input = &PlayerInputs[manager[managerNum].startPlayer];
            input->inverted = input->inverted == 0;
            // the colours a shade apart from those the tab sets
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetColour,
                          input->inverted ? 0x645a49c0 : 0x644646c0, 0);
            __Menu_SendEx(managerNum, C_GCPAUSE, PAUSE_INVERTED_TEXT, MessageType_SetText,
                          (intptr_t)Txt_BindLabel(input->inverted ? TXT_INVERTED : TXT_NORMAL, 0), 0);
        }
        break;
    }
    return true;
}

// ---- the end of a failed mission

#define SUB_P_ENDMISSION_TITLE   ((HASHCODE)0x10000001)
#define SUB_P_ENDMISSION_LIST    ((HASHCODE)0x100001a4)   // restart from the checkpoint, restart, quit
#define SUB_P_ENDMISSION_ICON    ((HASHCODE)0x10000184)

// AUTOINJECT
bool P_ENDMISSION_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_PageEnter) {
        __Menu_Send(managerNum, SUB_P_ENDMISSION_TITLE, MessageType_SetTextFlag2000, 0, 0);
        __Menu_Send(managerNum, SUB_P_PAUSE_HINT_TEXT, MessageType_SetTextFlag2000, 0, 0);
        __Menu_Send(managerNum, SUB_P_ENDMISSION_LIST, MessageType_SetTextFlag2000, 0, 0);
        __Menu_Send(managerNum, SUB_P_ENDMISSION_LIST, MessageType_ListSetColumnAlign, 0, ALIGN_CENTRE);
        __Menu_Send(managerNum, SUB_P_ENDMISSION_LIST, MessageType_SetFont, 1, 0);
        __Menu_Send(managerNum, SUB_P_ENDMISSION_LIST, MessageType_SetLineSpacing, 200, 0);
        __Menu_Send(managerNum, SUB_P_ENDMISSION_ICON, MessageType_SetIconSpriteFlags, 0x100, 0);
    }
    else if (message == MessageType_Select) {
        GameState.VibrationEnabled = 0;
        switch (RADIO_GET_VALUE(managerNum, SUB_P_ENDMISSION_LIST)) {
        case 0:     // from the checkpoint
            ResetMap_LevelToLoad((HASHCODE)GameState.CurrentLevelHashcode, true, true);
            Player_RamLoad((BLData *)glb_players[0]->extraObjectData, 1);
            break;
        case 1:
            ResetMap_LevelToLoad(Mission_BaseMapHCode(), false, true);
            break;
        case 2:
            GameState.ReloadMenupage = P_NFMAP;
            ResetMap_LevelToLoad(HT_Level_Menu_Pre, false, false);
            break;
        }
        SFXFadeDown(1);
        GameFlow_PushState(7, 80.0f, 0xff);
        MenuManager_Delete(managerNum);
    }
    return true;
}
