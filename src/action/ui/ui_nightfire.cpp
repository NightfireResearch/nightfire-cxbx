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

// The single player campaign's pages: the mission wheel (P_NFMAP / C_SBNFMAP) and the difficulty wheel
// (P_NFDFCTY / C_SBNFDFCTY).

// The missions (sp_level, ui.h; these are its shipped contents). .enabled is the campaign's progress: set from the codename's save (Menu_SetNightfireStatus), by
// finishing the mission before (Menu_SetLevelBonus) and by secret codes (Menu_SpecialCodenameCheck).
// XBE_GLOBAL(0x0017c580, 0x120)
M_ITEM sp_level[12] = {
    {ICON_SPMAP_PARISPRELUDE, SPMAP_PARISPRELUDE_NAME, SPMAP_PARISPRELUDE_DESC, HT_Level_Driving_Paris, 1, TXT_NULL},
    {ICON_SPMAP_EXCHANGE, SPMAP_EXCHANGE_NAME, SPMAP_EXCHANGE_DESC, HT_Level_CastleExterior, 1, TXT_NULL},
    {ICON_SPMAP_ALPINEESCAPE, SPMAP_ALPINEESCAPE_NAME, SPMAP_ALPINEESCAPE_DESC, HT_Level_Driving_SnowMobile, 0, SPMAP_LOCKED},
    {ICON_SPMAP_ENEMIESVANQUISHED, SPMAP_ENEMIESVANQUISHED_NAME, SPMAP_ENEMIESVANQUISHED_DESC, HT_Level_Driving_Alps, 0, SPMAP_LOCKED},
    {ICON_SPMAP_DOUBLECROSS, SPMAP_DOUBLECROSS_NAME, SPMAP_DOUBLECROSS_DESC, HT_Level_HendersonA, 0, SPMAP_LOCKED},
    {ICON_SPMAP_NIGHTSHIFT, SPMAP_NIGHTSHIFT_NAME, SPMAP_NIGHTSHIFT_DESC, HT_Level_TowerA, 0, SPMAP_LOCKED},
    {ICON_SPMAP_CHAINREACTION, SPMAP_CHAINREACTION_NAME, SPMAP_CHAINREACTION_DESC, HT_Level_PowerStationA1, 0, SPMAP_LOCKED},
    {ICON_SPMAP_PHOENIXFIRE, SPMAP_PHOENIXFIRE_NAME, SPMAP_PHOENIXFIRE_DESC, HT_Level_Tower2A, 0, SPMAP_LOCKED},
    {ICON_SPMAP_DEEPDESCENT, SPMAP_DEEPDESCENT_NAME, SPMAP_DEEPDESCENT_DESC, HT_Level_Driving_Underwater, 0, SPMAP_LOCKED},
    {ICON_SPMAP_ISLANDINFILTRATION, SPMAP_ISLANDINFILTRATION_NAME, SPMAP_ISLANDINFILTRATION_DESC, HT_Level_Driving_JungleA, 0, SPMAP_LOCKED},
    {ICON_SPMAP_COUNTDOWN, SPMAP_COUNTDOWN_NAME, SPMAP_COUNTDOWN_DESC, HT_Level_EvilBase, 0, SPMAP_LOCKED},
    {ICON_SPMAP_EQUINOX, SPMAP_EQUINOX_NAME, SPMAP_EQUINOX_DESC, HT_Level_SpaceStationD, 0, SPMAP_LOCKED},
};

// difficulty (ui.h): the identifier is GameState.difficultyModifier.
// XBE_GLOBAL(0x0017c760, 0x48)
M_ITEM difficulty[3] = {
    {ICON_DIFFICULTY_OPERATIVE, DIFFICULTY_OPERATIVE, DIFFICULTY_OPERATIVE_DESC, 1, 1, TXT_NULL},
    {ICON_DIFFICULTY_AGENT, DIFFICULTY_AGENT, DIFFICULTY_AGENT_DESC, 2, 1, TXT_NULL},
    {ICON_DIFFICULTY_00AGENT, DIFFICULTY_00AGENT, DIFFICULTY_00AGENT_DESC, 3, 1, TXT_NULL},
};

#define menu_unlock_everything U8_AT(0x0025d79e)
// Counts up a few frames once a mission is picked (0 = none), so the loading panel shows before the load starts.
#define nf_mission_countdown U8_AT(0x0025d7dc)

// AUTOINJECT
bool P_NFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
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
bool C_SBNFDFCTY_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
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
bool P_NFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
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
bool C_SBNFMAP_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
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
            MPSettings.maybeIsTeamGame = 0;
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
