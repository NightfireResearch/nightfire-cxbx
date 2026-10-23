// The menus' managers: making a menu from the menu data, its frame, closing it, and the message shown when a pad is
// pulled out (PS2: menu_manager.cpp)

#include "MenuManager.h"
#include "Manager.h"
#include "MenuCommon.h"
#include "Component.h"
#include "Page.h"

#include "../game.h"
#include "../input.h"
#include "../engine/Script.h"
#include "../engine/Text.h"
#include "../game/mp/multiplayer.h"
#include "../util/bin.h"

#include <stdio.h>
#include <string.h>
#include "../sound/SFX.h"

#pragma fp_contract(off)

// Empty on the Xbox, where the linker shares one empty function among several
static void Menu_StringInit(int size) {}
static void Menu_StringKill(void) {}

// The menu data (MenuManager_Load)
// XBE_GLOBAL(0x0025fb08, 0x4)
uchar *menu_file;
// A player has chosen to quit or restart from a menu (Player_HandleDeath)
// XBE_GLOBAL(0x0025fb10, 0x1)
uint8_t user_restarted_or_quit;
// What a manager's cursor sprite is made from
// XBE_GLOBAL(0x0017d60c, 0x2c)
SpriteInfo menu_cursor = {
    .colour = 0x7f7f7f7f,
    .unknown1 = 0x7f7f7f7f,
    .depth = 0xffff,
    .flags = 2048,
    .onscreenWidth = 128,
    .onscreenHeight = 32,
    .spritesheetX = 31,
    .spritesheetY = 36,
    .spritesheetWidth = 1,
    .spritesheetHeight = 1,
    .textRef = (Action_TranslatedText)0xffffffff,
    .textureHashcode = (HASHCODE)0x3000042,
};

// The message shown while a pad is out (MenuManager_Monitor): its state, the player whose pad it is, whether the
// mission's end menu was showing, the frames it waits, its text
// XBE_GLOBAL(0x0025fb0c, 0x4)
uint32_t controller_state;
// XBE_GLOBAL(0x0017d638, 0x4)
int32_t controller_player = -1;
extern uint8_t controller_mission_end;
// XBE_GLOBAL(0x0017d63c, 0x4)
int32_t controller_wait = 10;
// XBE_GLOBAL(0x0025fb18, 0x100)
static char controller_text[256];

#define CONTROLLER_WAIT 10
#define CONTROLLER_REMOVED_TEXT ((Action_TranslatedText)0x10001ec)
#define CONTROLLER_MENU 0x80000002
#define CONTROLLER_PAGE ((HASHCODE)0x4000002f)
#define CONTROLLER_BOX ((HASHCODE)0x1000011e)
#define CONTROLLER_MEMO ((HASHCODE)0x1000011f)
#define CONTROLLER_OPTIONS ((HASHCODE)0x10000120)

// The pause menu and the mission's end menu, made again once the pad is back
#define PAUSE_MENU 0x80000002
#define PAUSE_PAGE ((HASHCODE)0x4000004b)
#define MISSION_END_MENU 0x80000004
#define MISSION_END_PAGE ((HASHCODE)0x40000042)
#define MENU_STATUS_PAUSE 3
#define MENU_STATUS_MISSION_END 4

// The menu data's records
#define RECORD_PAGE             0xfffffff0  // a page of a menu, for some platforms
#define RECORD_END              0xfffffff2
#define RECORD_MANAGER_MESSAGE  0xfffffff3
#define RECORD_KEY_MESSAGE      0xfffffff4  // a message the last keyframe sends
#define RECORD_KEYFRAME         0xfffffff5  // of the last script
#define RECORD_MESSAGE          0xfffffff6  // to the last control
#define RECORD_SCRIPT           0xfffffff7  // of the last control
#define RECORD_FONT             0xfffffff8  // of the last control
#define RECORD_CONTROL          0xfffffff9
#define RECORD_PIECE            0xfffffffb  // of the last skin
#define RECORD_SKIN             0xfffffffc  // of the last set
#define RECORD_SKIN_SET         0xfffffffd
#define RECORD_SKIP             0xfffffffe

// A record's platforms: all, or the Xbox
#define PLATFORM_ALL  0
#define PLATFORM_XBOX 1

// The colour the menu data gives some texts, and the one the Xbox shows instead
#define MENU_DATA_COLOUR 0x7d6d59ff
#define XBOX_COLOUR      0x645a49ff

// Script_PlayDefault's event for back
#define SCRIPT_EVENT_BACK 0x5b

// A control the Xbox puts in a place of its own
#define MOVED_CONTROL ((HASHCODE)0x10000224)

// The status shown: the highest of the managers' (inline in the original's frame and watch, as here)
static uint MenuManager_HighestStatus(void) {
    uint status = 0;
    if (manager[0].status > 0)
        status = manager[0].status;
    for (int m = 1; m < 5; m++) {
        if (manager[m].status > status)
            status = manager[m].status;
    }
    return status;
}

// AUTOINJECT
void MenuManager_Init(void) {
    memset(manager, 0, sizeof(M_MANAGER) * 5);
    for (uchar i = 0; i < 5; i++) {
        M_MANAGER *mgr = &manager[i];
        mgr->index = i;
        mgr->whichPlayer = 0;
        mgr->currentPage = NULL;
        mgr->underPage = NULL;
        mgr->sidePage = NULL;
        mgr->exists = false;
        mgr->overlayMode = false;
        mgr->inputLocked = false;
        mgr->closeAction = ACTION_PAUSE;
    }
    controller_state = 0;
    controller_player = -1;
    user_restarted_or_quit = 0;
    ls.listStale = 1;
    Component_InitComponents();
}

// A menu from the menu data in a free manager: its pages, the controls of the platform's and their messages, fonts and
// scripts, the skins; then the cursor, the start page and the focus on its first control. The manager's number, or
// MENU_NONE.
// AUTOINJECT
uint MenuManager_Create(uint menu, uint startPage, short player, uchar managerNum, uint closeAction, char pauseAudio,
                        uint status) {
    if (menu_file == NULL)
        return MENU_NONE;
    M_MANAGER *mgr = &manager[managerNum];
    if (mgr->exists)
        return MENU_NONE;
    Menu_StringInit(32000);
    if (pauseAudio) {
        SFXPause();
        SFXPauseAllStreams();
    }
    menu_music_playing = 0;
    Input_RumbleStop(0);
    Input_RumbleStop(1);
    Input_RumbleStop(2);
    Input_RumbleStop(3);
    LinkedList_Init(&mgr->pages, offsetof(M_PAGE, pageLink) / 4);
    mgr->closeAction = closeAction;
    mgr->index = managerNum;
    mgr->control.type = ControlType_Manager;
    mgr->exists = true;
    mgr->startPlayer = (uchar)player;
    mgr->whichPlayer = player;
    mgr->status = status;
    mgr->currentMovie = (HASHCODE)0;
    mgr->overlayMode = false;
    mgr->inputLocked = false;
    mgr->didPauseAudio = pauseAudio;
    mgr->startPage = (HASHCODE)startPage;
    user_restarted_or_quit = 0;
    if (player == -1)
        mgr->whichPlayer = -1;
    mgr->currentPage = NULL;
    mgr->underPage = NULL;
    mgr->sidePage = NULL;
    Page_FindHelpControl(NULL);
    Stack_Init(&mgr->stack, 64, mgr->stackMem);

    uint result = MENU_NONE;
    MENU_COMPONENTSET *set = NULL;
    MENU_COMPONENTPIECE *pieces = NULL;
    M_CONTROL *control = NULL;
    M_SCRIPT *script = NULL;
    M_KEYFRAME *key = NULL;
    uchar skin = 0;
    uchar piece = 0;
    uchar *data = menu_file;
    bool skipping = true;
    // The records are read into these, which keep what a record does not set
    NEW_CONTROL create = {};
    M_MESSAGE message = {};
    M_KEYFRAME keyframe = {};
    BIN_GetDWord(&data);
    for (bool reading = true; reading;) {
        switch (BIN_GetDWord(&data)) {
            case RECORD_SKIP:
                BIN_GetDWord(&data);
                break;
            case RECORD_SKIN_SET:
                set = Component_AddSet(managerNum, BIN_GetWord(&data));
                skin = 0;
                break;
            case RECORD_SKIN: {
                short count = BIN_GetWord(&data);
                HASHCODE texture = (HASHCODE)BIN_GetDWord(&data);
                short left = BIN_GetWord(&data);
                short top = BIN_GetWord(&data);
                short right = BIN_GetWord(&data);
                short bottom = BIN_GetWord(&data);
                pieces = Component_AddComponent(&set->components[skin], left, top, right, bottom, texture, count);
                skin++;
                piece = 0;
                break;
            }
            case RECORD_PIECE: {
                short x = BIN_GetWord(&data);
                short y = BIN_GetWord(&data);
                short width = BIN_GetWord(&data);
                short height = BIN_GetWord(&data);
                short u = BIN_GetWord(&data);
                short v = BIN_GetWord(&data);
                short uw = BIN_GetWord(&data);
                short vh = BIN_GetWord(&data);
                uint colour = BIN_GetDWord(&data);
                uint flags = BIN_GetDWord(&data);
                float stretchWidth = (float)BIN_GetDWord(&data);
                float stretchHeight = (float)BIN_GetDWord(&data);
                Component_InitInstance(&pieces[piece], x, y, width, height, u, v, uw, vh, flags, colour, stretchWidth,
                                       stretchHeight);
                piece++;
                break;
            }
            case RECORD_PAGE: {
                HASHCODE hashcode = (HASHCODE)BIN_GetDWord(&data);
                uint owner = BIN_GetDWord(&data);
                HASHCODE movie = (HASHCODE)BIN_GetDWord(&data);
                uchar platform = BIN_GetByte(&data);
                int size = BIN_GetDWord(&data);
                if (owner == menu && (platform == PLATFORM_ALL || platform == PLATFORM_XBOX)) {
                    M_PAGE *page = (M_PAGE *)Manager_SendMessage(mgr, MessageType_CreatePage, hashcode, owner);
                    if (page != NULL)
                        page->backgroundMovie = movie;
                } else {
                    data += size;
                }
                break;
            }
            case RECORD_CONTROL: {
                if (control != NULL) {
                    Manager_SendMessage(mgr, MessageType_ControlCreated, (intptr_t)control, 0);
                    Page_SendMessage(mgr->currentPage, MessageType_ControlCreated, (intptr_t)control, 1);
                }
                create.hashcode = (HASHCODE)BIN_GetDWord(&data);
                create.type = BIN_GetByte(&data);
                uchar platform = BIN_GetByte(&data);
                create.x = BIN_GetWord(&data);
                create.y = BIN_GetWord(&data);
                create.width = BIN_GetWord(&data);
                create.height = BIN_GetWord(&data);
                create.depth = BIN_GetWord(&data);
                create.id = BIN_GetDWord(&data);
                create.skin = BIN_GetWord(&data);
                create.value = BIN_GetDWord(&data);
                create.max = BIN_GetDWord(&data);
                create.hasBar = create.type == ControlType_Scroll;
                // A label at the screen's corner as large as the screen or larger fills it
                if (create.type == ControlType_Label && create.x <= 0 && create.y <= 0 && create.width >= SCREEN_WIDTH &&
                    create.height >= SCREEN_HEIGHT) {
                    create.width = SCREEN_WIDTH;
                    create.height = SCREEN_HEIGHT;
                } else if (create.hashcode == MOVED_CONTROL) {
                    create.x = 0x181;
                    create.y = 0xe2;
                }
                if (platform == PLATFORM_XBOX || platform == PLATFORM_ALL) {
                    control = (M_CONTROL *)Manager_SendMessage(mgr, MessageType_CreateObject, (intptr_t)&create, 0);
                    if (control != NULL) {
                        memset(((M_BASECONTROL *)control)->defaultScripts, 0, sizeof(M_BASECONTROL::defaultScripts));
                        skipping = false;
                    }
                } else {
                    skipping = true;
                }
                break;
            }
            case RECORD_MESSAGE: {
                uint type = BIN_GetByte(&data);
                int arg1 = BIN_GetDWord(&data);
                int arg2 = BIN_GetDWord(&data);
                if ((type == MessageType_SetHighlightColour || type == MessageType_SetNormalColour ||
                     type == MessageType_SetColour) &&
                    arg1 == (int)MENU_DATA_COLOUR)
                    arg1 = XBOX_COLOUR;
                if (!skipping && type != MessageType_SelectIndex)
                    __Menu_SendMessage(control, type, arg1, arg2);
                break;
            }
            case RECORD_FONT:
                if (!skipping) {
                    // Of 16-bit characters the first time, made 8-bit where it lies
                    if (data[1] == '\0') {
                        for (int i = 0; i < 0x1f; i++)
                            data[i] = data[i * 2];
                        data[0x1f] = '\0';
                    }
                    __Menu_SendMessage(control, MessageType_SetFont, (intptr_t)data, 0);
                }
                data += 0x20;
                break;
            case RECORD_SCRIPT:
                if (!skipping) {
                    int id = BIN_GetDWord(&data);
                    script = Script_AddScript(control, id);
                    script->flags = BIN_GetDWord(&data);
                } else {
                    BIN_GetDWord(&data);
                    BIN_GetDWord(&data);
                }
                break;
            case RECORD_KEYFRAME:
                keyframe.frame = BIN_GetWord(&data);
                keyframe.x = BIN_GetWord(&data);
                keyframe.y = BIN_GetWord(&data);
                keyframe.width = BIN_GetWord(&data);
                keyframe.height = BIN_GetWord(&data);
                keyframe.flags = BIN_GetDWord(&data);
                if (!skipping)
                    key = Script_AddKeyFrame(script, &keyframe);
                break;
            case RECORD_KEY_MESSAGE:
                message.target = BIN_GetDWord(&data);
                message.type = BIN_GetByte(&data);
                message.arg1 = BIN_GetDWord(&data);
                message.arg2 = BIN_GetDWord(&data);
                if (!skipping)
                    Script_AddMessage(key, &message);
                break;
            case RECORD_MANAGER_MESSAGE: {
                uchar type = BIN_GetByte(&data);
                int arg1 = BIN_GetDWord(&data);
                int arg2 = BIN_GetDWord(&data);
                Manager_SendMessage(mgr, (MessageType)type, arg1, arg2);
                break;
            }
            case RECORD_END:
                if (control != NULL) {
                    Manager_SendMessage(mgr, MessageType_ControlCreated, (intptr_t)control, 0);
                    Page_SendMessage(mgr->currentPage, MessageType_ControlCreated, (intptr_t)control, 1);
                }
                result = managerNum;
                reading = false;
                break;
            default:
                reading = false;
                break;
        }
    }

    mgr->field_0x1cf = 7;
    menu_cursor.textureHashcode = (HASHCODE)0;
    menu_cursor.textRef = (Action_TranslatedText)(uintptr_t)MenuEmptyString;
    mgr->cursorSprite = Menu_CreateSprite(&menu_cursor);
    Manager_SendMessage(mgr, MessageType_GoPage, startPage, 1);
    Manager_SendMessage(mgr, MessageType_SetCursor, 0x10000002, 0);
    return result;
}

// The sounds paused for the menu go on, its close action is handed back (and the game unpaused if it was START), its
// page leaves and it is freed
// AUTOINJECT
void MenuManager_Delete(uchar managerNum) {
    M_MANAGER *mgr = &manager[managerNum];
    if (!mgr->exists)
        return;
    if (mgr->didPauseAudio) {
        SFXUnPause();
        SFXUnPauseAllStreams();
        menu_music_playing = 0;
    }
    if (mgr->closeAction != MENU_NO_CLOSE_ACTION) {
        Menu_SetAction(mgr->whichPlayer, mgr->closeAction, 1);
        if (mgr->closeAction == ACTION_PAUSE)
            GS_PauseGame(false);
    }
    Manager_SendMessage(mgr, MessageType_PageLeave, (intptr_t)mgr->currentPage, 0);
    Manager_SendMessage(mgr, MessageType_Destroy, 1, 0);
    memset(mgr, 0, sizeof(*mgr));
    psiStopBackgroundMovie();
    Menu_StringKill();
    Input_ClearAllActions(-1);
}

// A frame of the menus: the pulse, the text's throb, the delayed messages, a movie's skip; then for each manager (one
// that comes after another that exists waits) back (from its player, or any), the d-pad's moves of the cursor while no
// player has a focus of his own, and its pages' frames
// AUTOINJECT
void MenuManager_Update(void) {
    if (MenuManager_HighestStatus() == 0)
        return;
    Menu_AlphaUpdate();
    Menu_UpdateTextFmt();
    Menu_ProcessDelayedMessages();
    if (!movieFinished()) {
        Menu_UpdateMovieCounter();
        if (Menu_InputAction(-1, ACTION_SKIP_CUTSCENE, 4))
            Menu_StopMovie(0);
    }

    for (int m = 0; m < 5; m++) {
        M_MANAGER *mgr = &manager[m];
        if (!mgr->exists || (m > 0 && manager[m - 1].exists))
            continue;
        M_PAGE *page = mgr->currentPage;
        if (!mgr->inputLocked) {
            uchar player = 0;
            do {
                if (mgr->whichPlayer != -1)
                    player = (uchar)mgr->whichPlayer;
                if (Menu_InputAction(player, ACTION_MENU_BACK, 4)) {
                    // The control with the player's focus, or the focused one, may take back with its script
                    if (mgr->currentPage != NULL) {
                        M_CONTROL *c = mgr->currentPage->playerFocus[player];
                        if (c == NULL)
                            c = mgr->currentPage->focused;
                        if (Script_PlayDefault(c, SCRIPT_EVENT_BACK))
                            break;
                    }
                    if (!mgr->overlayMode) {
                        Menu_PlaySound(MENU_SFX_BACK);
                        Manager_SendMessage(mgr, MessageType_Back, 0, player);
                        return;
                    }
                }
                if (mgr->whichPlayer != -1)
                    break;
                player++;
            } while (player <= 3);
        }

        bool shared = true;
        if (mgr->currentPage != NULL) {
            for (int i = 0; i < 4; i++) {
                if (mgr->currentPage->playerFocus[i] != NULL)
                    shared = false;
            }
        }
        mgr->cursorSprite->colourTint = (Menu_AlphaGet() + 0x7f) | 0x201f3400;
        if (shared && !mgr->inputLocked) {
            switch (page->navigationMode) {
                case 3:
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_LEFT, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_LEFT);
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_RIGHT, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_RIGHT);
                    break;
                case 2:
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_UP, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_UP);
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_DOWN, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_DOWN);
                    break;
                case 1:
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_LEFT, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_LEFT);
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_RIGHT, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_RIGHT);
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_UP, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_UP);
                    if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_DOWN, 0xc))
                        Menu_FindControl(mgr, MENU_DIR_DOWN);
                    break;
            }
        }
        if (mgr->currentPage != NULL)
            Page_Update(mgr, mgr->currentPage, 1);
        if (mgr->underPage != NULL)
            Page_Update(mgr, mgr->underPage, 1);
        if (mgr->sidePage != NULL)
            Page_Update(mgr, mgr->sidePage, 0);
    }
}

// The menus' watch over the pads: a manager's close action closes it; and in play, a player's pad taken out puts up a
// message (in place of the menus) until every player's pad is back, when the pause menu, or the mission's end menu if
// that was showing, is put up again
// AUTOINJECT
void MenuManager_Monitor(void) {
    for (uchar m = 0; m < 5; m++) {
        M_MANAGER *mgr = &manager[m];
        if (mgr->exists && (m == 0 || !manager[m - 1].exists) && mgr->closeAction != MENU_NO_CLOSE_ACTION &&
            Menu_InputAction(mgr->whichPlayer, mgr->closeAction, 4) && mgr->control.type == ControlType_Manager)
            MenuManager_Delete(m);
    }

    switch (GameFlow_GetState()) {
        case 7:
        case 8:
        case 0xc:
        case 0xd:
        case 0xe:
            controller_wait = CONTROLLER_WAIT;
            return;
    }
    if (GameState.CurrentLevelHashcode == HT_Level_Menu_Pre)
        return;
    if (!movieFinished() || (MenuManager_HighestStatus() == 0 && ScriptCam != 0)) {
        controller_wait = CONTROLLER_WAIT;
        return;
    }
    if (--controller_wait > 0)
        return;

    for (uint i = 0; i < MPSettings.numPlayers; i++) {
        PlayerInput *input = &PlayerInputs[i];
        switch (controller_state) {
            case 0:
                if (input->controllerIsActive)
                    break;
                controller_player = i;
                controller_mission_end = MenuManager_GetStatus() == MENU_STATUS_MISSION_END;
                if (!controller_mission_end && GameState.LevelEnding)
                    break;
                sprintf(controller_text, Txt_BindLabel(CONTROLLER_REMOVED_TEXT, 0), input->controllerPort + 1);
                controller_state = 1;
                // fall through
            case 1:
                MenuManager_Delete(0);
                MenuManager_Delete(1);
                MenuManager_Delete(2);
                MenuManager_Delete(3);
                GS_PauseGame(true);
                MenuManager_Create(CONTROLLER_MENU, CONTROLLER_PAGE, 0, 0, MENU_NO_CLOSE_ACTION, 1, 2);
                Manager_SendMessage(&manager[0], MessageType_LockInput, 1, 0);
                __Menu_Send(0, CONTROLLER_MEMO, MessageType_SetText, (intptr_t)controller_text, 0);
                controller_state = 2;
                break;
            case 2: {
                // The box fitted round the text and the options
                sprintf(controller_text, Txt_BindLabel(CONTROLLER_REMOVED_TEXT, 0),
                        PlayerInputs[controller_player].controllerPort + 1);
                __Menu_Send(0, CONTROLLER_MEMO, MessageType_SetText, (intptr_t)controller_text, 0);
                M_CONTROL *box = (M_CONTROL *)__Menu_Send(0, CONTROLLER_BOX, MessageType_GetControl, 0, 0);
                M_MEMO *memo = (M_MEMO *)__Menu_Send(0, CONTROLLER_MEMO, MessageType_GetControl, 0, 0);
                __Menu_SendMessage(&memo->control, MessageType_MemoSetOption282, 0, 0);
                __Menu_SendMessage(&memo->control, MessageType_SetLineSpacing, 100, 0);
                uchar lines = __Menu_SendMessage(&memo->control, MessageType_MemoGetNumLines, 0, 0);
                uchar options = __Menu_Send(0, CONTROLLER_OPTIONS, MessageType_GetNumItems, 0, 0);
                short height = (uchar)Ftol(memo->lineHeight) * lines + (options != 0 ? 0x1a : 0) + options * 0x15 + 0x1c;
                box->height = height;
                box->y = Ftol(((double)SCREEN_HEIGHT - height) * 0.5);
                memo->control.y = box->y + 10;
                if (PlayerInputs[controller_player].controllerIsActive) {
                    for (uint j = 0; j < MPSettings.numPlayers; j++) {
                        if (!PlayerInputs[j].controllerIsActive) {
                            controller_wait = 1;
                            controller_player = j;
                            return;
                        }
                    }
                    controller_state = 3;
                }
                break;
            }
            case 3:
            case 4:
                GS_PauseGame(true);
                MenuManager_Delete(0);
                if (controller_player == -1)
                    controller_player = 0;
                if (controller_mission_end)
                    MenuManager_Create(MISSION_END_MENU, MISSION_END_PAGE, controller_player, 0, MENU_NO_CLOSE_ACTION,
                                       1, MENU_STATUS_MISSION_END);
                else
                    MenuManager_Create(PAUSE_MENU, PAUSE_PAGE, controller_player, 0, ACTION_PAUSE, 1,
                                       MENU_STATUS_PAUSE);
                controller_state = 0;
                controller_player = -1;
                break;
        }
    }
}

// AUTOINJECT
void MenuManager_Load(uint hashcode, uint *data) {
    menu_file = (uchar *)data;
}

// AUTOINJECT
uchar MenuManager_Exists(uchar managerNum) {
    return manager[managerNum].exists;
}

// The highest status of the managers
uint MenuManager_GetStatus(void) {
    return MenuManager_HighestStatus();
}

// AUTOINJECT
void MenuManager_Enable(bool enable, uchar managerNum) {
    manager[managerNum].exists = enable;
}

// AUTOINJECT
uchar Menu_PlayerHasQuitOrRestarted(void) {
    return user_restarted_or_quit;
}
