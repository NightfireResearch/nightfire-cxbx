#include "Process.h"
#include "ui.h"

#include "Manager.h"
#include "Menu.h"
#include "MenuCommon.h"
#include "Button.h"
#include "List.h"
#include "Memo.h"
#include "../engine/Text.h"
#include "../engine/LoadSave.h"
#include "../engine/psiSave.h"
#include "../engine/XboxSystem.h"
#include "../game.h"
#include "../input.h"
#include "../game/mp/multiplayer.h"
#include "../game/sp/PlayerStats.h"
#include "../sound/SFX.h"
#include "../util/Random.h"
#include "../util/Sort.h"

#include <stdio.h>
#include <string.h>

// The codename menu (P_CNMENU / C_SBCNOPTIONS): Secret Unlocks, the options pages, and saving the codename.

// cn_options (ui.h). The last item's title is "Codenames" for a default codename, which cannot be saved, else "Save Codename".
// XBE_GLOBAL(0x0017cec8, 0xa8)
M_ITEM cn_options[7] = {
    {HT_Texture_icon_newcodename, SECRET_UNLOCKS, SECRET_UNLOCKS_DESC, 0, 1, TXT_NULL}, // Secret Unlocks
    {HT_Texture_icon_controllerxbox, CONTROLLER_SETUP, CONTROL_SCHEME_ACTION_DESC, 1, 1, TXT_NULL}, // Controller Setup
    {HT_Texture_icon_controllerxbox, CN_DRIVING_CONTROLLER, CONTROL_SCHEME_DRIVING_DESC, 2, 1, TXT_NULL}, // Driving Controller
    {HT_Texture_icon_options, CN_ADVANCED_OPTIONS, ADV_SETTINGS_DESC, 3, 1, TXT_NULL}, // Advanced Options
    {HT_Texture_icon_mpoptions, CN_MULTIPLAYER_OPTIONS, MULTIPLAYER_SETTINGS_DESC, 4, 1, TXT_NULL}, // Multiplayer Options
    {HT_Texture_icon_avoptions, CN_AV_OPTIONS, AV_SETTINGS_DESC, 5, 1, TXT_NULL}, // AV Options
    {HT_Texture_icon_savecodename, SAVE_CODENAME, ACCEPT_CHANGES_DESC, 6, 1, TXT_NULL}, // Save Codename
};

// XBE_GLOBAL(0x0025ed28, 0x40)
static char cn_menu_title_text[0x40]; // "Edit <codename>"

// AUTOINJECT
bool P_CNMENU_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_PageEnter:
        // Coming back from one of its own pages (Secret Unlocks among them) keeps the changes made so far; arriving
        // with a codename just picked, or just named, starts afresh.
        if ((HASHCODE)arg2 != P_CNSELECT && ((HASHCODE)arg2 != P_CNNAME || cn_secret_mode)) {
            Menu_StartIris(IRIS_LAST_FRAME, managerNum, SUB_C_SBCNOPTIONS_IRIS);
            break;
        }
        cn_modified = 0;
        Menu_SelectItemInControl(CONTROL_GET(managerNum, C_SBCNOPTIONS), cn_options, ARRAY_SIZE(cn_options), 1);
        Menu_StartIris(IRIS_FROM_MIDDLE, managerNum, SUB_C_SBCNOPTIONS_IRIS);
        sprintf(cn_menu_title_text, Txt_BindLabel(CODENAME_EDIT_TITLE, 0), ls.codename);
        LABEL_SET_TEXT(managerNum, SUB_P_CNMENU_TITLE_TEXT, cn_menu_title_text);
        cn_options[6].title = ls.slot < 2 ? CODENAMES : SAVE_CODENAME;
        // a codename just named goes straight on to its controller setup
        if ((HASHCODE)arg2 == P_CNNAME && !cn_secret_mode)
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNCONTROLS, 0);
        break;
    case MessageType_PageUpdate:
        Menu_PlayIris(1, managerNum, SUB_C_SBCNOPTIONS_IRIS);
        Menu_UpdateMessageBox(managerNum, SAVE_ALL, 0);
        if (Menu_UpdateOptionBox(NULL) == OPTIONBOX_ANSWER_A)   // "lose your changes?" answered yes
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 1, 0);
        break;
    case MessageType_SaveFlowDone:
        if (ls.returnPage != 0)
            Menu_ChangePageCloseIris(ls.returnPage, managerNum, SUB_C_SBCNSELECT_IRIS);
        break;
    case MessageType_QueryBack:
        if (cn_modified) {
            Menu_CreateOptionBoxLabel(managerNum, EXIT_LOSING_CHANGES_CONFIRM, OPTIONBOX_LOSE_CHANGES, 1, 1);
            *(int *)arg2 = BACK_REFUSED;   // stay on the page
        }
        break;
    }
    return true;
}

// AUTOINJECT
bool C_SBCNOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        Menu_UpdateWheel(managerNum, control, cn_options, SUB_C_SBCNOPTIONS_WHEEL_TEXT, SUB_C_SBCNOPTIONS_ICON,
                         SUB_C_SBCNOPTIONS_DESCRIPTION_TEXT, SUB_C_SBCNOPTIONS_IRIS, message == MessageType_Scroll);
        break;
    case MessageType_ControlCreated:
        __Menu_SendMessage(control, MessageType_SetRange, 0, ARRAY_SIZE(cn_options) - 1);
        break;
    case MessageType_Select:
        switch ((uchar)SCROLL_GET_VALUE(control)) {
        case 0: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNNAME, 0); break;
        case 1: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNCONTROLS, 0); break;
        case 2: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNDRIVINGCONTROLS, 0); break;
        case 3: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNOPTIONS, 0); break;
        case 4: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNMPOPTIONS, 0); break;
        case 5: Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNAVOPTIONS, 0); break;
        case 6:
            // A default codename (slots 0 and 1) is not saved, only brought up to date; the page to go to next
            // comes from the wheel's id.
            if (ls.slot < 2) {
                Menu_UpdateDefaultCodename(0, (byte)ls.slot);
                ls.returnPage = (HASHCODE)control->id;
                Menu_ChangePageCloseIris(ls.returnPage, managerNum, SUB_C_SBCNOPTIONS_IRIS);
                break;
            }
            ls.returnPage = (HASHCODE)control->id;
            ls.busy = 1;
            ls.returnPageArg = 1;
            ls.operation = LS_OPERATION_SAVE;
            ls.slot = LS_SLOT_NEW;
            ls.done = 0;
            ls.control = control;
            ls.field17_0x42 = 0;
            Menu_UpdateMessageBox(managerNum, SAVE_ALL, 0);
            break;
        }
        break;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Loading, saving and listing codenames: a page sets up ls (or has Menu_StartLS do it) and runs
// Menu_UpdateMessageBox from its update until XBox_DoSaveFlow, which shows the message boxes, says it is over.
// ---------------------------------------------------------------------------------------------------------------

extern uint32_t SubtitlesEnabled;
extern uint8_t menu_bonus_replace;  // MenuUnlocks.cpp
extern int32_t SaveEnum_EntriesPerFrame;  // psiSave.cpp

// XBox_DoSaveFlow's state, which its error box's list (C_LBERROPTIONS) winds up
// XBE_GLOBAL(0x0025eda0, 0x4)
MENU_LS *saveflow_ls;
// XBE_GLOBAL(0x0025eda4, 0x4)
M_MANAGER *saveflow_manager;
// XBE_GLOBAL(0x0025eda8, 0x1)
uint8_t saveflow_box_open;
// XBE_GLOBAL(0x0025edac, 0x4)
char *saveflow_text;
// XBE_GLOBAL(0x0025edb0, 0x4)
uint32_t saveflow_frames;
// XBE_GLOBAL(0x0025edb4, 0x4)
uint32_t saveflow_result;  // the state it was last given
// XBE_GLOBAL(0x0025edb8, 0x1)
uint8_t saveflow_cancel;  // clear the delayed messages instead of going on
extern uint8_t saveflow_dashboard;  // B on the box leaves for the dashboard
// XBE_GLOBAL(0x0025edb9, 0x1)
uint8_t saveflow_relist;  // a codenames page lists the codenames as it enters or resumes

// ---- the message box of a load, save or listing (XBox_DoSaveFlow)

// The box: a page shown over the one under it, a button behind a memo and a list
#define SAVEFLOW_PAGE          P_AFTERRESULTS

// The tips shown under TXT_GETTING_CODENAMES, one picked at random every 300 frames
static const Action_TranslatedText saveflow_tips[] = {
    (Action_TranslatedText)0x1000111, (Action_TranslatedText)0x1000112, (Action_TranslatedText)0x1000113,
    (Action_TranslatedText)0x1000114, (Action_TranslatedText)0x1000115, (Action_TranslatedText)0x1000116,
    (Action_TranslatedText)0x1000117, (Action_TranslatedText)0x1000118, (Action_TranslatedText)0x1000119,
    (Action_TranslatedText)0x100011a, (Action_TranslatedText)0x100011b, (Action_TranslatedText)0x100011c,
    (Action_TranslatedText)0x100011d, (Action_TranslatedText)0x100011e, (Action_TranslatedText)0x100011f,
    (Action_TranslatedText)0x1000120, (Action_TranslatedText)0x1000121, (Action_TranslatedText)0x1000122,
    (Action_TranslatedText)0x1000123, (Action_TranslatedText)0x1000124, (Action_TranslatedText)0x1000125,
    (Action_TranslatedText)0x1000126, (Action_TranslatedText)0x1000127, (Action_TranslatedText)0x1000128,
    (Action_TranslatedText)0x100012a, (Action_TranslatedText)0x100012b, (Action_TranslatedText)0x100012c,
    (Action_TranslatedText)0x100012d, (Action_TranslatedText)0x100012e, (Action_TranslatedText)0x100012f,
    (Action_TranslatedText)0x1000130, (Action_TranslatedText)0x1000131, (Action_TranslatedText)0x1000132,
    (Action_TranslatedText)0x1000133, (Action_TranslatedText)0x1000134, (Action_TranslatedText)0x1000135,
    (Action_TranslatedText)0x1000136, (Action_TranslatedText)0x1000137, (Action_TranslatedText)0x1000138,
    (Action_TranslatedText)0x1000139, (Action_TranslatedText)0x100013a, (Action_TranslatedText)0x100013b,
    (Action_TranslatedText)0x100013c,
};
#define SAVEFLOW_TIP_FRAMES 300

// XBE_GLOBAL(0x0025edc0, 0x1)
uint8_t saveflow_dashboard_offered;     // once only
// XBE_GLOBAL(0x0025edc4, 0x4)
uint saveflow_hold_until;               // the frame count the box stays up to, or 0
// XBE_GLOBAL(0x0025edc8, 0x200)
char saveflow_text_buffer[0x200];
// XBE_GLOBAL(0x0025efc8, 0x4)
const char *saveflow_tip;

// One frame of a load, save or listing's message box: the operation's state (Menu_UpdateMessageBox's) and the save
// layer's (psiGetLoadingDataState) pick the text; an operation over (4, 6, or 7 and 8 once the box has been up long
// enough) closes the box, if it has no choices, and tells the page (SaveFlowDone). The box is laid out around its
// text and drawn here, without input.
// AUTOINJECT
void XBox_DoSaveFlow(M_MANAGER *mgr, uint state, MENU_LS *operation) {
    saveflow_ls = operation;
    saveflow_manager = mgr;
    int result = psiGetLoadingDataState();
    saveflow_frames++;
    bool over = false;
    switch (state) {
    case 0:
    case 1:
    case 2:
    case 3:
        switch (result) {
        case PSISAVE_LOADED:
        case PSISAVE_DELETED:
        case PSISAVE_ENUM_ENDED:
            saveflow_cancel = 0;
            break;
        case PSISAVE_LOAD_BAD_ARGS:
        case PSISAVE_LOAD_FAILED:
            saveflow_text = (char *)Txt_BindLabel(TXT_UNABLE_TO_LOAD_CODENAME, 0);
            operation->returnPage = (HASHCODE)0;
            saveflow_cancel = 1;
            break;
        case PSISAVE_SAVED:
            Manager_SendMessage(mgr, MessageType_LockInput, 1, 0);
            saveflow_text = (char *)Txt_BindLabel(TXT_SAVING, 0);
            saveflow_cancel = 0;
            break;
        case PSISAVE_SAVE_FAILED:
        case PSISAVE_6:
        case PSISAVE_7:
            saveflow_text = (char *)Txt_BindLabel(TXT_UNABLE_TO_SAVE_GAME, 0);
            operation->returnPage = (HASHCODE)0;
            saveflow_cancel = 1;
            break;
        case PSISAVE_ENUM_RUNNING:
            if (saveflow_frames == 1) {
                Manager_SendMessage(mgr, MessageType_LockInput, 1, 0);
                saveflow_tip = NULL;
            }
            if (saveflow_frames > 10) {
                if (saveflow_frames % SAVEFLOW_TIP_FRAMES == 0 || saveflow_tip == NULL)
                    saveflow_tip = Txt_BindLabel(saveflow_tips[Rand_Random() % ARRAY_SIZE(saveflow_tips)], 0);
                sprintf(saveflow_text_buffer, "%s\n%s", Txt_BindLabel(TXT_GETTING_CODENAMES, 0), saveflow_tip);
                saveflow_text = saveflow_text_buffer;
            }
            saveflow_cancel = 0;
            break;
        case PSISAVE_12:
            saveflow_cancel = 1;
            break;
        }
        break;
    case 4:
    case 6:
        over = true;
        break;
    case 7:
    case 8:
        // Held a little (or to the tip's end, while the box is up) before it is over
        if (saveflow_hold_until == 0) {
            if (result == PSISAVE_SAVED) {
                saveflow_hold_until = 80;
                saveflow_ls->listStale = 1;
            } else if (result == PSISAVE_ENUM_ENDED) {
                if (saveflow_tip != NULL)
                    sprintf(saveflow_text_buffer, "%s\n%s", Txt_BindLabel(TXT_GETTING_CODENAMES, 0), saveflow_tip);
                else
                    sprintf(saveflow_text_buffer, "%s", Txt_BindLabel(TXT_GETTING_CODENAMES, 0));
                saveflow_text = saveflow_text_buffer;
                if (saveflow_frames % SAVEFLOW_TIP_FRAMES != 0 && saveflow_box_open)
                    saveflow_hold_until = saveflow_frames - saveflow_frames % SAVEFLOW_TIP_FRAMES + SAVEFLOW_TIP_FRAMES;
            }
        }
        if (saveflow_frames < saveflow_hold_until)
            break;
        saveflow_hold_until = 0;
        over = true;
        break;
    }

    if (over) {
        saveflow_result = state;
        operation->busy = 0;
        Manager_SendMessage(mgr, MessageType_LockInput, 1, 0);
        if (__Menu_Send(mgr->index, C_LBERROPTIONS, MessageType_GetNumItems, 0, 0) == 0) {
            if (saveflow_box_open) {
                Manager_SendMessage(mgr, MessageType_Back, 0, 0);
                if (operation->returnPage != 0)
                    __Menu_SendMessage(&mgr->control, MessageType_GoPage, operation->returnPage,
                                       operation->returnPageArg);
            }
            saveflow_box_open = 0;
        }
        operation->done = 1;
        saveflow_text = NULL;
        saveflow_frames = 0;
        Manager_SendMessage(mgr, MessageType_SaveFlowDone, (intptr_t)mgr->currentPage, 0);
        Manager_SendMessage(mgr, MessageType_LockInput, 0, 0);
        LS_FlushStates();
        Menu_FlushEnum();
        if (saveflow_dashboard && !saveflow_dashboard_offered && saveflow_ls->operation != LS_OPERATION_ENUM_FIRST) {
            saveflow_text = (char *)Txt_BindLabel(TXT_DAMAGED_SAVES_FOUND, 0);
            operation->returnPage = (HASHCODE)0;
            saveflow_dashboard_offered = 1;
            saveflow_cancel = 1;
        }
    }

    if (saveflow_text != NULL) {
        if (!saveflow_box_open) {
            Manager_SendMessage(mgr, MessageType_GoPage, SAVEFLOW_PAGE, GOPAGE_OVERLAY);
            __Menu_Send(mgr->index, SUB_P_AFTERRESULTS_TEXT, MessageType_SetText, (intptr_t)saveflow_text, 0);
            __Menu_Send(mgr->index, C_LBERROPTIONS, MessageType_ClearItems, 0, 0);
            __Menu_Send(mgr->index, C_LBERROPTIONS, MessageType_ListSetColumnAlign, 0, ALIGN_CENTRE);
            __Menu_Send(mgr->index, C_LBERROPTIONS, MessageType_SetPulse, 0, 0);
            switch (result) {
            case PSISAVE_LOAD_BAD_ARGS:
            case PSISAVE_LOAD_FAILED:
            case PSISAVE_SAVE_FAILED:
            case PSISAVE_6:
            case PSISAVE_7:
            case PSISAVE_12:
                // An error: a choice to take, and the codenames listed again after
                Manager_SendMessage(mgr, MessageType_LockInput, 0, 0);
                __Menu_Send(mgr->index, C_LBERROPTIONS, MessageType_AddItem,
                            (intptr_t)Txt_BindLabel(TXT_PRESS_A_TO_CONTINUE, 0), 0);
                __Menu_Send(mgr->index, SUB_P_AFTERRESULTS_HINT_TEXT, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
                saveflow_relist = 1;
                break;
            default:
                __Menu_Send(mgr->index, SUB_P_AFTERRESULTS_HINT_TEXT, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);
                saveflow_relist = 0;
                break;
            }
            saveflow_box_open = 1;
            saveflow_frames = 0;
        } else {
            __Menu_Send(mgr->index, SUB_P_AFTERRESULTS_TEXT, MessageType_SetText, (intptr_t)saveflow_text, 0);
        }
    }

    // The button as tall as the text and the choices, centred on the screen, the memo and the list on it
    M_BUTTON *button = (M_BUTTON *)__Menu_Send(mgr->index, SUB_P_AFTERRESULTS_BOX, MessageType_GetControl, 0, 0);
    M_MEMO *memo = (M_MEMO *)__Menu_Send(mgr->index, SUB_P_AFTERRESULTS_TEXT, MessageType_GetControl, 0, 0);
    M_LIST *list = (M_LIST *)__Menu_Send(mgr->index, C_LBERROPTIONS, MessageType_GetControl, 0, 0);
    __Menu_SendMessage(&memo->control, MessageType_MemoSetOption282, 0, 0);
    uchar lines = __Menu_SendMessage(&memo->control, MessageType_MemoGetNumLines, 0, 0);
    uchar choices = __Menu_Send(mgr->index, C_LBERROPTIONS, MessageType_GetNumItems, 0, 0);
    short height = (choices != 0 ? 24 : 0) + 30 + lines * 31 + choices * 21;
    button->control.height = height;
    short y = Ftol((SCREEN_HEIGHT - (double)height) * 0.5);
    button->control.y = y;
    memo->control.y = y + 10;
    list->control.y = lines * 31 + y + 30;
    manager[mgr->index].takingInput = 0;
    Memo_Update(&manager[mgr->index], (M_PAGE *)memo->control.parent, memo);
    List_Update(&manager[mgr->index], (M_PAGE *)list->control.parent, list);
    Button_Update(&manager[mgr->index], (M_PAGE *)button->control.parent, button);
    manager[mgr->index].takingInput = 1;
}

// ---- room on the save drive for a codename (names ours)

#define MAX_SAVES             0xffd

enum {
    SAVE_SPACE_OK = 0,
    SAVE_SPACE_BLOCKS = 1,      // the drive has fewer free blocks than a save takes
    SAVE_SPACE_SAVES = 2,       // too many saves on it
};

// XBE_GLOBAL(0x0025edbc, 0x4)
uint save_space_problem;
// XBE_GLOBAL(0x0025efd0, 0x200)
char save_space_text[0x200];

// The size of a codename's whole save
// AUTOINJECT
int LS_GetSaveSize(void) {
    return LS_GetSaveStuff(0xff, 0);
}

// Room for a new codename: true if there is, else false with a box saying why (OPTIONBOX_SAVE_SPACE)
// AUTOINJECT
bool Menu_CheckSaveSpace(uchar managerNum, MENU_LS *unused) {
    save_space_problem = SAVE_SPACE_OK;
    int size = LS_GetSaveSize();
    int needed = SaveDrive_BlocksFor_Thunk(size);
    bool short_of_blocks = needed > (int)SaveDrive_FreeBlocks_Thunk();
    bool too_many = (int)SavesListed >= MAX_SAVES;
    if (short_of_blocks)
        save_space_problem = SAVE_SPACE_BLOCKS;
    if (too_many)
        save_space_problem = SAVE_SPACE_SAVES;
    switch (save_space_problem) {
        case SAVE_SPACE_BLOCKS: {
            int blocks = SaveDrive_BlocksFor_Thunk(size);
            blocks -= (int)SaveDrive_FreeBlocks_Thunk();
            sprintf(save_space_text, Txt_BindLabel(TXT_SAVE_BLOCKS_SHORT, 0), blocks);
            break;
        }
        case SAVE_SPACE_SAVES:
            sprintf(save_space_text, Txt_BindLabel(TXT_TOO_MANY_SAVES, 0));
            break;
        default:
            return true;
    }
    Menu_CreateOptionBox(managerNum, save_space_text, OPTIONBOX_SAVE_SPACE, 0, 0);
    return false;
}

// To the dashboard to make the room Menu_CheckSaveSpace asked for: the blocks it needs, or none
// AUTOINJECT
void Menu_GoToDashboard(void) {
    switch (save_space_problem) {
        case SAVE_SPACE_BLOCKS:
            WriteStateFileAndLaunch(SaveDrive_BlocksFor_Thunk(LS_GetSaveSize()), NULL, 0);
            return;
        case SAVE_SPACE_SAVES:
            WriteStateFileAndLaunch(0, NULL, 0);
            return;
    }
}

// A codename among those listed
// AUTOINJECT
bool Menu_CodenameExists(const char *name) {
    for (uint i = 0; i < ls.numCodenames; i++) {
        if (strcmp(codename_buf[i], name) == 0)
            return true;
    }
    return false;
}

#define ENUM_STATE_START   0
#define ENUM_STATE_LISTING 1
#define ENUM_STATE_RESTART 4
#define ENUM_STATE_DONE    7
#define CODENAME_FIRST_MAX 8          // the longest name a first-only enumeration takes
#define CODENAME_MAX       0x20


// XBE_GLOBAL(0x00224f70, 0x4)
uint ls_state;                 // what the operation last returned, for XBox_DoSaveFlow
// XBE_GLOBAL(0x00224f6c, 0x4)
uint ls_delete_state;
// XBE_GLOBAL(0x00224f74, 0x1)
bool ls_updating;
// XBE_GLOBAL(0x00224f58, 0x4)
uint enum_state;

// One step of the load, save, delete or listing set up in ls, and its message box. A save refused (saving not
// allowed, or no codename) returns with ls_updating still set, so from then on this does nothing at all.
// AUTOINJECT
void Menu_UpdateMessageBox(uchar managerNum, ushort mask, byte slot) {
    if (ls.busy == 0 || ls_updating)
        return;
    ls_updating = true;
    switch (ls.operation) {
    case LS_OPERATION_LOAD:
        ls_state = LS_Load(ls.codename, slot, mask);
        break;
    case LS_OPERATION_SAVE:
        if (!ls.canSave || strlen(ls.codename) == 0) {
            ls.done = 1;
            ls.busy = 0;
            LS_FlushStates();
            return;
        }
        ls_state = LS_Save(ls.codename, slot, mask);
        ls.listStale = 1;
        break;
    case LS_OPERATION_ENUM:
        ls_state = Menu_EnumSaves(managerNum, 0);
        break;
    case LS_OPERATION_DELETE:
        // psiDeleteData(ls.codename) is the empty debug hook in the Xbox build: nothing is deleted here
        if (ls_delete_state == 0 || ls_delete_state == 4)
            ls_delete_state = 5;
        else if (ls_delete_state != 5)
            break;
        ls_state = psiDeletingData();
        if (ls_state == PSI_NOT_DONE)
            ls_delete_state = 0;
        break;
    case LS_OPERATION_ENUM_FIRST:
        ls_state = Menu_EnumSaves(managerNum, 1);
        break;
    }
    XBox_DoSaveFlow(&manager[managerNum], ls_state, &ls);
    ls_updating = false;
}

// Lists the codenames into codename_buf - the two defaults, then a few saved ones a frame - and adds the saved ones
// to ls.control. firstOnly stops at the first short enough. Nothing is done for a listing while the list is not
// stale. Returns the state: 1 listing, 7 done.
// AUTOINJECT
uint Menu_EnumSaves(uchar managerNum, char firstOnly) {
    if (ls.operation == LS_OPERATION_ENUM && !ls.listStale) {
        enum_state = ENUM_STATE_DONE;
        return enum_state;
    }
    switch (enum_state) {
    case ENUM_STATE_START:
    case ENUM_STATE_RESTART:
        ls.numCodenames = 0;
        for (int i = 0; i < CODENAME_SLOTS_DEFAULT; i++) {
            strcpy(codename_buf[ls.numCodenames], Txt_BindLabel(def_codename[i].name, 0));
            ls.numCodenames++;
        }
        psiStartSaveEnum();
        enum_state = ENUM_STATE_LISTING;
        break;
    case ENUM_STATE_LISTING:
        for (ushort i = 0; i < SaveEnum_EntriesPerFrame; i++) {
            char *name = psiGetNextSave();
            if (name == (char *)-1)   // not ready yet
                return enum_state;
            if (name == NULL) {
                enum_state = ENUM_STATE_DONE;
                __Menu_SendMessage(ls.control, MessageType_SetRange, 0, ls.numCodenames - 1);
                psiEndSaveEnum();
                ls.listStale = 0;
                QuickSort(codename_buf[CODENAME_SLOTS_DEFAULT], ls.numCodenames - CODENAME_SLOTS_DEFAULT, CODENAME_MAX,
                          Compare_Strings, NULL);
                return enum_state;
            }
            if (firstOnly) {
                if (strlen(name) <= CODENAME_FIRST_MAX) {
                    strcpy(codename_buf[ls.numCodenames], name);
                    __Menu_SendMessage(ls.control, MessageType_AddItem, (intptr_t)codename_buf[ls.numCodenames], 0);
                    ls.numCodenames++;
                    enum_state = ENUM_STATE_DONE;
                    psiEndSaveEnum();
                    return enum_state;
                }
            } else if (strlen(name) <= CODENAME_MAX) {
                strcpy(codename_buf[ls.numCodenames], name);
                __Menu_SendMessage(ls.control, MessageType_AddItem, (intptr_t)codename_buf[ls.numCodenames], 0);
                ls.numCodenames++;
            }
        }
        break;
    }
    return enum_state;
}

// AUTOINJECT
void Menu_FlushEnum(void) {
    enum_state = ENUM_STATE_START;
}

// The codenames into a control: a wheel gets their range, a list or radio the names after the first. The second
// default is "Play without saving" on P_NFSELECT, "Default" elsewhere.
// AUTOINJECT
void Menu_PutCodenamesIntoControl(M_CONTROL *control) {
    if (control == NULL)
        return;
    M_PAGE *page = manager[0].currentPage;
    if (page != NULL && page->control.hashcode == P_NFSELECT)
        strcpy(codename_buf[1], Txt_BindLabel(PLAY_WITHOUT_SAVING, 0));
    else
        strcpy(codename_buf[1], Txt_BindLabel(CODENAME_DEFAULT, 0));
    switch (control->type) {
    case ControlType_List:
    case ControlType_Radio:
        for (ushort i = 1; i < ls.numCodenames; i++)
            __Menu_SendMessage(control, MessageType_AddItem, (intptr_t)codename_buf[i], 0);
        __Menu_SendMessage(control, MessageType_SelectIndex, __Menu_SendMessage(control, MessageType_GetSelectedIndex, 0, 0), 0);
        break;
    case ControlType_Scroll: {
        int last = ls.numCodenames - 1;
        if (last < 1)
            last = 1;
        __Menu_SendMessage(control, MessageType_SetRange, 0, last);
        __Menu_SendMessage(control, MessageType_SetValue, SCROLL_GET_VALUE(control), 0);
        break;
    }
    }
}

// Applies a default codename to a player: parts 1 the controls and other options, 2 the missions open (and the
// mission statistics reset), 4 the multiplayer handicap, 8 the sound and split-screen layout; any part also its
// bonus, with the cheats off. Always: it becomes ls's codename, with its missions and bonus.
// AUTOINJECT
void Menu_MapDefaultCodename(byte player, byte slot, uint parts) {
    const DEFAULT_CODENAME *codename = &def_codename[slot];
    if (parts & 1) {
        PlayerInput *input = &PlayerInputs[player];
        input->crouchToggle = codename->crouchToggle;
        input->autoAim = codename->autoAim;
        input->autoSwitchBetterWeapon = codename->autoSwitchBetterWeapon;
        input->controlStyle = codename->controlStyle;
        input->crosshairsEnabled = codename->crosshairsEnabled;
        input->manualAimToggle = codename->manualAimToggle;
        input->controlStyleDriving = codename->controlStyleDriving;
        input->inverted = codename->inverted;
        MPSettings.TripleDamageModifierProfessionalMode = codename->tripleDamage;
        MPSettings.Player[player].radar = codename->mpRadar;
        MPSettings.RespawnSelectionMode = (RespawnMode)codename->respawnMode;
        SubtitlesEnabled = codename->subtitles;
        input->vibrationEnabled = codename->vibrationEnabled;
        input->flashingObjects = codename->input_0c;
        input->hudVisibility = codename->input_0b;
    }
    if (parts & 2) {
        Menu_SetNightfireStatus(codename->nightfireStatus);
        PlrStat_ResetForMission();
        GameState.WeaponUpgradeRelated = codename->weaponUpgrade;
    }
    if (parts & 4) {
        MPSettings.Player[player].HealthModifier = codename->healthModifier;
        PlayerInputs[player].autoAimMultiplayer = codename->autoAimMultiplayer;
    }
    if (parts & 8) {
        MultiplayerLayout_LeftRightOrTopBtm = codename->multiplayerLayout;
        SFXMusicSetVolume(codename->musicVolume);
        SFXSetVolume(codename->sfxVolume);
    }
    if (parts != 0) {
        Menu_SetBonus(codename->bonusLo, codename->bonusHi, player, 0);
        CheatInfo.Immortal = 0;
        CheatInfo.AllWeapons = 0;
        CheatInfo.UnlimitedAmmo = 0;
    }
    ls.slot = slot;
    strcpy(ls.codename, Txt_BindLabel(codename->name, 0));
    Menu_SetNightfireStatus(codename->nightfireStatus);
    Menu_SetBonus(codename->bonusLo, codename->bonusHi, player, 0);
}

extern uint32_t menu_bonus[8];   // MenuUnlocks.cpp

// A default codename takes the player's settings, volumes, progress and rewards as they are now (the reverse of
// Menu_MapDefaultCodename)
// AUTOINJECT
void Menu_UpdateDefaultCodename(uchar player, uchar slot) {
    DEFAULT_CODENAME *codename = &def_codename[slot];
    PlayerInput *input = &PlayerInputs[player];
    codename->manualAimToggle = input->manualAimToggle;
    codename->autoAim = input->autoAim;
    codename->autoSwitchBetterWeapon = input->autoSwitchBetterWeapon;
    codename->controlStyle = input->controlStyle;
    codename->crosshairsEnabled = input->crosshairsEnabled;
    codename->crouchToggle = input->crouchToggle;
    codename->controlStyleDriving = input->controlStyleDriving;
    codename->healthModifier = (short)MPSettings.Player[player].HealthModifier;
    codename->inverted = input->inverted;
    codename->autoAimMultiplayer = input->autoAimMultiplayer;
    codename->musicVolume = SFXMusicGetVolume();
    codename->tripleDamage = (uchar)MPSettings.TripleDamageModifierProfessionalMode;
    codename->mpRadar = (uchar)MPSettings.Player[player].radar;
    codename->respawnMode = (uchar)MPSettings.RespawnSelectionMode;
    codename->multiplayerLayout = (uchar)MultiplayerLayout_LeftRightOrTopBtm;
    codename->sfxVolume = SFXGetVolume();
    codename->subtitles = (uchar)SubtitlesEnabled;
    codename->showNames = (uchar)MPSettings.ShowTeamAndNameOverhead;
    codename->vibrationEnabled = input->vibrationEnabled;
    codename->nightfireStatus = Menu_GetNightfireStatus();
    codename->bonusLo = menu_bonus[player * 2];
    codename->bonusHi = menu_bonus[player * 2 + 1];
    codename->input_0c = input->flashingObjects;
    codename->input_0b = input->hudVisibility;
    codename->weaponUpgrade = GameState.WeaponUpgradeRelated;
}

// A codenames wheel's turn: the description and icon of a default codename or a saved one, the codenames each side of
// the selected one (none past either end, or with no codenames listed), and the iris as it turns
// AUTOINJECT
void Menu_UpdateCodenameWheel(uchar managerNum, M_CONTROL *wheel, HASHCODE text, HASHCODE icon, HASHCODE description,
                              HASHCODE iris, char turning) {
    if (wheel == NULL)
        return;
    if (wheel->type == ControlType_Scroll)
        ((M_SCROLL *)wheel)->wrap = 0;
    int selected = __Menu_SendMessage(wheel, MessageType_GetValue, 0, 0);
    int last = __Menu_SendMessage(wheel, MessageType_GetMax, 0, 0);

    Action_TranslatedText about;
    HASHCODE picture;
    switch (selected) {
        case 0:
            about = CODENAME_NEW_DESC;
            picture = HT_Texture_icon_newcodename;
            break;
        case 1:
            about = CODENAME_DEFAULT_DESC;
            picture = HT_Texture_icon_codename;
            break;
        default:
            about = CODENAME_SAVED_DESC;
            picture = HT_Texture_icon_codename;
            break;
    }
    if (icon != 0) {
        if (turning)
            __Menu_SendDelayed(10, managerNum, icon, MessageType_SetIcon, picture, 0);
        else
            __Menu_Send(managerNum, icon, MessageType_SetIcon, picture, 0);
    }
    if (description != 0)
        LABEL_SET_TEXT(managerNum, description, Txt_BindLabel(about, 0));

    int slots[5] = {selected - 2, selected - 1, selected, selected + 1, selected + 2};
    if (slots[1] < 0)
        slots[0] = slots[1] = LS_SLOT_NEW;
    else if (slots[0] < 0)
        slots[0] = LS_SLOT_NEW;
    if (slots[3] > last)
        slots[3] = slots[4] = LS_SLOT_NEW;
    else if (slots[4] > last)
        slots[4] = LS_SLOT_NEW;
    if (ls.numCodenames == 0)
        slots[0] = slots[1] = slots[2] = slots[3] = slots[4] = LS_SLOT_NEW;
    for (int i = 0; i < 5; i++)
        __Menu_SendEx(managerNum, text, i, MessageType_SetText,
                      (intptr_t)(slots[i] == LS_SLOT_NEW ? MenuSpaceString : codename_buf[slots[i]]), 0);

    if (turning)
        Menu_StartIris(IRIS_WHEEL, managerNum, iris);
}

// Puts a codenames wheel on the codename, or on the first entry if it is not in the list.
// AUTOINJECT
void Menu_SelectCodenameInControl(byte managerNum, char *codename, HASHCODE control) {
    if (codename == NULL || strlen(codename) == 0) {
        __Menu_Send(managerNum, control, MessageType_SetValue, 0, 0);
        return;
    }
    for (uint i = 0; i < ls.numCodenames; i++)
        if (strcmp(codename_buf[i], codename) == 0) {
            __Menu_Send(managerNum, control, MessageType_SetValue, i, 0);
            return;
        }
    __Menu_Send(managerNum, control, MessageType_SetValue, 0, 0);
}

// Sets up ls for an operation and runs its first step (name ours). stale marks the list to be enumerated again;
// a listing with nothing stale only marks ls done.
void Menu_StartLS(char *codename, int operation, char stale, uchar managerNum, byte slot, ushort mask, uint lsSlot,
                  HASHCODE returnPage, int returnPageArg, M_CONTROL *control, uchar done) {
    if (operation == LS_OPERATION_ENUM && (ls.listStale | stale) == 0) {
        ls.done = 1;
        return;
    }
    ls.operation = operation;
    ls.slot = lsSlot;
    ls.returnPage = returnPage;
    ls.listStale |= stale;
    ls.busy = 1;
    ls.returnPageArg = returnPageArg;
    ls.done = done;
    ls.control = control;
    ls.field17_0x42 = 0;
    if (codename != NULL)
        strcpy(ls.codename, codename);
    Menu_UpdateMessageBox(managerNum, mask, slot);
}

// ---- the Codenames page (P_CNSELECT) and its wheel

// XBE_GLOBAL(0x0025ed21, 0x1)
uint8_t cnselect_leaving;           // a codename was loaded, or the wheel redrawn, after a save flow
// XBE_GLOBAL(0x0017d60a, 0x1)
uint8_t cnselect_from_main_menu = 1; // the wheel starts on "Default"

// Sets up a listing of the codenames into the wheel.
static void ListCodenames(uchar managerNum, M_CONTROL *wheel) {
    ls.busy = 1;
    ls.slot = LS_SLOT_NEW;
    ls.returnPage = (HASHCODE)0;
    ls.returnPageArg = 0;
    ls.operation = LS_OPERATION_ENUM;
    ls.control = wheel;
    ls.field17_0x42 = 0;
    Menu_UpdateMessageBox(managerNum, 0, 0);
}

static void FadeMenuGroup(uchar managerNum, HASHCODE group, int arg1) {
    M_MESSAGE fade = {};
    fade.type = MESSAGE_PROCESS;
    fade.arg1 = arg1;
    fade.arg2 = 0;
    Process_Create(CONTROL_GET(managerNum, group), &fade, 0xf, 0);
}

// AUTOINJECT
bool P_CNSELECT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_PageEnter:
        ls.returnPage = (HASHCODE)0;
        Menu_SelectCodenameInControl(managerNum, ls.codename, C_SBCNSELECT);
        Menu_StartIris((HASHCODE)arg2 == P_MAIN ? IRIS_FROM_MIDDLE : IRIS_LAST_FRAME,
                       managerNum, SUB_C_SBCNSELECT_IRIS);
        saveflow_relist = 1;
        LABEL_SET_TEXT(managerNum, SUB_C_SBCNSELECT_WHEEL_TEXT, " ");
        // and on as when resuming
    case MessageType_PageResume: {
        menu_bonus_replace = 0;
        if (saveflow_relist) {
            M_CONTROL *wheel = CONTROL_GET(managerNum, C_SBCNSELECT);
            ls.done = 1;
            if (ls.listStale)
                ListCodenames(managerNum, wheel);
            saveflow_relist = 0;
        }
        if ((HASHCODE)arg2 == P_MAIN) {
            Menu_StartIris(IRIS_FROM_MIDDLE, managerNum, SUB_C_SBCNSELECT_IRIS);
            LABEL_SET_COLOUR(managerNum, SUB_P_CNSELECT_MENU_GROUP, 0xff);
            M_MANAGER *mgr = &manager[managerNum];
            __Menu_SendMessage(&mgr->control, MessageType_LockInput, 1, 0);
            __Menu_SendDelayedMessage(0xf, &mgr->control, MessageType_LockInput, 0, 0);
            FadeMenuGroup(managerNum, SUB_P_CNSELECT_MENU_GROUP, 0);
            cnselect_from_main_menu = 1;
        } else {
            LABEL_SET_COLOUR(managerNum, SUB_P_CNSELECT_MENU_GROUP, 0);
            cnselect_from_main_menu = 0;
        }
        cnselect_leaving = 0;
        for (int player = 0; player < 4; player++)
            PlayerInputs[player].controllerPort = player;
        break;
    }
    case MessageType_PageUpdate: {
        Menu_PlayIris(1, managerNum, SUB_C_SBCNSELECT_IRIS);
        if (ls.done && !cnselect_leaving) {
            Menu_PutCodenamesIntoControl(CONTROL_GET(managerNum, C_SBCNSELECT));
            ls.done = 0;
            if (cnselect_from_main_menu)
                __Menu_Send(managerNum, C_SBCNSELECT, MessageType_SetValue, 1, 0);
        }
        // a default codename picked on the wheel needs no load
        if (ls.busy != 0 && ls.slot < CODENAME_SLOTS_DEFAULT) {
            ls.busy = 0;
            Menu_MapDefaultCodename(0, ls.slot, 0xff);
            if (ls.returnPage != 0)
                Menu_ChangePageCloseIris(ls.returnPage, managerNum, SUB_C_SBCNSELECT_IRIS);
        } else {
            Menu_UpdateMessageBox(managerNum, SAVE_ALL, 0);
        }
        undefined4 box = 0;
        undefined4 answer = Menu_UpdateOptionBox(&box);
        switch (box) {
        case OPTIONBOX_DELETE_CODENAME:
            if (answer == OPTIONBOX_A) {
                if (psiDeleteSave(codename_buf[__Menu_Send(managerNum, C_SBCNSELECT, MessageType_GetValue, 0, 0)])) {
                    M_CONTROL *wheel = CONTROL_GET(managerNum, C_SBCNSELECT);
                    ls.listStale |= 1;
                    ls.done = 0;
                    ListCodenames(managerNum, wheel);
                } else {
                    Menu_CreateOptionBoxLabel(managerNum, CODENAME_DELETE_FAILED, OPTIONBOX_DELETE_CODENAME, 1, 0);
                }
            }
            break;
        case OPTIONBOX_3:
            if (answer == OPTIONBOX_A) {
                ls.done = 1;
                ls.canSave = 0;
                if (ls.slot > 0 && ls.slot < CODENAME_SLOTS_DEFAULT)
                    Menu_ChangePageCloseIris(P_CNMENU, managerNum, SUB_C_SBCNSELECT_IRIS);
                break;
            }
            // and on, as for the space box
        case OPTIONBOX_SAVE_SPACE:
            if (answer == OPTIONBOX_B)
                Menu_GoToDashboard();
            break;
        }
        break;
    }
    case MessageType_SaveFlowDone:
        if (ls.operation == LS_OPERATION_LOAD)
            Menu_ChangePageCloseIris(P_CNMENU, managerNum, SUB_C_SBCNSELECT_IRIS);
        else
            Menu_UpdateCodenameWheel(managerNum, CONTROL_GET(managerNum, C_SBCNSELECT), SUB_C_SBCNSELECT_WHEEL_TEXT,
                                     SUB_C_SBCNSELECT_ICON, SUB_C_SBCNSELECT_DESCRIPTION_TEXT, SUB_C_SBCNSELECT_IRIS, 0);
        cnselect_leaving = 1;
        break;
    case MessageType_QueryBack: {
        // back to the main menu after a fade, with the input locked meanwhile
        M_MANAGER *mgr = &manager[managerNum];
        __Menu_SendMessage(&mgr->control, MessageType_LockInput, 1, 0);
        __Menu_SendDelayedMessage(0x13, &mgr->control, MessageType_GoPage, P_MAIN, 0);
        __Menu_SendDelayedMessage(0x13, &mgr->control, MessageType_LockInput, 0, 0);
        FadeMenuGroup(managerNum, SUB_P_CNSELECT_MENU_GROUP, 0xff);
        *(int *)arg2 = BACK_REFUSED;   // stay on the page meanwhile
        break;
    }
    }
    return true;
}

// The codenames wheel: 0 is a new codename, 1 the default one, the rest are loaded. Y makes a new one, X deletes
// the one showing (after a box asking).
// AUTOINJECT
bool C_SBCNSELECT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        Menu_UpdateCodenameWheel(managerNum, control, SUB_C_SBCNSELECT_WHEEL_TEXT, SUB_C_SBCNSELECT_ICON,
                                 SUB_C_SBCNSELECT_DESCRIPTION_TEXT, SUB_C_SBCNSELECT_IRIS, message == MessageType_Scroll);
        LABEL_SET_TEXT(managerNum, SUB_C_SBCNSELECT_BUTTON_HINT_TEXT,
                       Txt_BindLabel(SCROLL_GET_VALUE(control) < CODENAME_SLOTS_DEFAULT ? CNSELECT_HINT : CNSELECT_HINT_DELETE, 0));
        break;
    case MessageType_Select: {
        uint value = SCROLL_GET_VALUE(control);
        if (value == 0) {
            ls.slot = value;
            if (Menu_CheckSaveSpace(managerNum, &ls)) {
                Menu_MapDefaultCodename(0, 0, 0xff);
                Menu_ChangePageCloseIris(P_CNNAME, managerNum, SUB_C_SBCNSELECT_IRIS);
            }
        } else if (value < CODENAME_SLOTS_DEFAULT) {
            ls.slot = value;
            Menu_MapDefaultCodename(0, value, 0xff);
            Menu_ChangePageCloseIris(P_CNMENU, managerNum, SUB_C_SBCNSELECT_IRIS);
        } else {
            Menu_StartLS(codename_buf[SCROLL_GET_VALUE(control)], LS_OPERATION_LOAD, 0, managerNum, 0, 0xff, value,
                         (HASHCODE)0, 0, control, 0);
        }
        // the codename menu comes back here
        __Menu_Send(managerNum, C_SBCNOPTIONS, MessageType_SetId, P_CNSELECT, 0);
        break;
    }
    case MessageType_AltSelect1:
        ls.slot = 0;
        if (Menu_CheckSaveSpace(managerNum, &ls)) {
            Menu_MapDefaultCodename(0, 0, 0xff);
            __Menu_SendMessage(&manager[managerNum].control, MessageType_GoPage, P_CNNAME, 0);
            __Menu_Send(managerNum, C_SBCNOPTIONS, MessageType_SetId, P_CNSELECT, 0);
        }
        break;
    case MessageType_AltSelect2:
        if (SCROLL_GET_VALUE(control) >= CODENAME_SLOTS_DEFAULT) {
            ls.slot = LS_SLOT_NEW;
            Menu_CreateOptionBoxLabel(managerNum, CODENAME_DELETE_CONFIRM, OPTIONBOX_DELETE_CODENAME, 0, 0);
        }
        break;
    }
    return true;
}

// The list on XBox_DoSaveFlow's error box: any choice ends the flow - the page under the box is told, the box
// closed, and the page to go to gone to (unless the flow was cancelled). B leaves for the dashboard when the box
// offers it.
// AUTOINJECT
bool C_LBERROPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    if (message == MessageType_Select) {
        if (saveflow_manager->underPage != NULL)
            Manager_SendMessage(saveflow_manager, MessageType_SaveFlowDone, (intptr_t)saveflow_manager->underPage,
                                saveflow_result);
        if (saveflow_box_open)
            Manager_SendMessage(saveflow_manager, MessageType_Back, 0, 0);
        if (saveflow_cancel) {
            Menu_ClearDelayedMessages();
            saveflow_ls->returnPage = (HASHCODE)0;
        } else if (saveflow_ls->returnPage != 0) {
            Manager_SendMessage(saveflow_manager, MessageType_GoPage, saveflow_ls->returnPage, saveflow_ls->returnPageArg);
        }
        saveflow_ls->busy = 0;
        saveflow_ls->done = 1;
        saveflow_box_open = 0;
        saveflow_text = NULL;
        saveflow_frames = 0;
        saveflow_dashboard = 0;
        Manager_SendMessage(saveflow_manager, MessageType_LockInput, 0, 0);
        LS_FlushStates();
    } else if (message == MessageType_BackOnControl) {
        if (saveflow_dashboard)
            WriteStateFileAndLaunch(0, NULL, 0);
    }
    return true;
}

// ---- the codename select before the campaign (P_NFSELECT) and its wheel

// XBE_GLOBAL(0x0025df70, 0x1)
uint8_t nfselect_leaving;         // a codename was loaded, or the wheel redrawn, after a save flow
// XBE_GLOBAL(0x0017d609, 0x1)
uint8_t nfselect_from_main_menu = 1; // the wheel starts on "Default"


// As P_CNSELECT, for the campaign: a codename is picked, then the difficulty. Entering puts the Codenames page's
// wheel (not this page's) on the codename in hand, as the original does.
// AUTOINJECT
bool P_NFSELECT_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_PageEnter:
        ls.returnPage = (HASHCODE)0;
        Menu_SelectCodenameInControl(managerNum, ls.codename, C_SBCNSELECT);
        Menu_StartIris((HASHCODE)arg2 == P_MAIN ? IRIS_FROM_MIDDLE : IRIS_LAST_FRAME, managerNum, SUB_C_SBNFCN_IRIS);
        saveflow_relist = 1;
        LABEL_SET_TEXT(managerNum, SUB_C_SBNFCN_WHEEL_TEXT, MenuSpaceString);
        // and on as when resuming
    case MessageType_PageResume:
        menu_bonus_replace = 0;
        if (saveflow_relist) {
            M_CONTROL *wheel = CONTROL_GET(managerNum, C_SBNFCN);
            ls.done = 1;
            if (ls.listStale)
                ListCodenames(managerNum, wheel);
            saveflow_relist = 0;
        }
        if ((HASHCODE)arg2 == P_MAIN) {
            Menu_StartIris(IRIS_FROM_MIDDLE, managerNum, SUB_C_SBNFCN_IRIS);
            LABEL_SET_COLOUR(managerNum, SUB_P_NFSELECT_MENU_GROUP, 0xff);
            M_MANAGER *mgr = &manager[managerNum];
            __Menu_SendMessage(&mgr->control, MessageType_LockInput, 1, 0);
            __Menu_SendDelayedMessage(0xf, &mgr->control, MessageType_LockInput, 0, 0);
            FadeMenuGroup(managerNum, SUB_P_NFSELECT_MENU_GROUP, 0);
            nfselect_from_main_menu = 1;
        } else {
            LABEL_SET_COLOUR(managerNum, SUB_P_NFSELECT_MENU_GROUP, 0);
            nfselect_from_main_menu = 0;
        }
        nfselect_leaving = 0;
        for (int player = 0; player < 4; player++)
            PlayerInputs[player].controllerPort = player;
        break;
    case MessageType_PageUpdate: {
        Menu_PlayIris(1, managerNum, SUB_C_SBNFCN_IRIS);
        if (ls.done && !nfselect_leaving) {
            Menu_PutCodenamesIntoControl(CONTROL_GET(managerNum, C_SBNFCN));
            ls.done = 0;
            if (nfselect_from_main_menu)
                __Menu_Send(managerNum, C_SBNFCN, MessageType_SetValue, 1, 0);
        }
        uint box = 0;
        uint answer = Menu_UpdateOptionBox(&box);
        if (box == OPTIONBOX_SAVE_SPACE) {
            if (answer == OPTIONBOX_A) {
                ls.done = 1;
                ls.canSave = 0;
                // The original hands the iris P_NFDFCTY's low byte as the manager (0x23), not managerNum
                if (ls.slot > 0 && ls.slot < CODENAME_SLOTS_DEFAULT)
                    Menu_ChangePageCloseIris(ls.returnPage, (uchar)P_NFDFCTY, SUB_C_SBNFCN_IRIS);
            } else if (answer == OPTIONBOX_B) {
                Menu_GoToDashboard();
            }
        } else if (box == OPTIONBOX_DEFAULT_CODENAME && answer == OPTIONBOX_ANSWER_A) {
            Menu_ChangePageCloseIris(P_NFDFCTY, managerNum, SUB_C_SBNFCN_IRIS);
        }
        // a default codename picked on the wheel needs no load
        if (ls.busy != 0 && ls.slot < CODENAME_SLOTS_DEFAULT) {
            ls.busy = 0;
            Menu_MapDefaultCodename(0, ls.slot, 0xff);
            Menu_ChangePageCloseIris(ls.returnPage, managerNum, SUB_C_SBNFCN_IRIS);
        } else {
            Menu_UpdateMessageBox(managerNum, SAVE_ALL, 0);
        }
        break;
    }
    case MessageType_SaveFlowDone:
        if (ls.operation == LS_OPERATION_LOAD)
            Menu_ChangePageCloseIris(P_NFDFCTY, managerNum, SUB_C_SBNFCN_IRIS);
        else
            Menu_UpdateCodenameWheel(managerNum, CONTROL_GET(managerNum, C_SBNFCN), SUB_C_SBNFCN_WHEEL_TEXT,
                                     SUB_C_SBNFCN_ICON, SUB_C_SBNFCN_DESCRIPTION_TEXT, SUB_C_SBNFCN_IRIS, 0);
        nfselect_leaving = 1;
        break;
    case MessageType_QueryBack: {
        // back to the main menu after a fade, with the input locked meanwhile
        M_MANAGER *mgr = &manager[managerNum];
        __Menu_SendMessage(&mgr->control, MessageType_LockInput, 1, 0);
        __Menu_SendDelayedMessage(0x13, &mgr->control, MessageType_GoPage, P_MAIN, 0);
        __Menu_SendDelayedMessage(0x13, &mgr->control, MessageType_LockInput, 0, 0);
        FadeMenuGroup(managerNum, SUB_P_NFSELECT_MENU_GROUP, 0xff);
        *(int *)arg2 = BACK_REFUSED;   // stay on the page meanwhile
        break;
    }
    }
    return true;
}

// The wheel: a new codename (room for it checked first), a default one (with a warning box: the page goes on to the
// difficulty on its answer), or a saved one, loaded
// AUTOINJECT
bool C_SBNFCN_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
    case MessageType_Scroll:
    case MessageType_ValueSet:
        Menu_UpdateCodenameWheel(managerNum, control, SUB_C_SBNFCN_WHEEL_TEXT, SUB_C_SBNFCN_ICON,
                                 SUB_C_SBNFCN_DESCRIPTION_TEXT, SUB_C_SBNFCN_IRIS, message == MessageType_Scroll);
        break;
    case MessageType_Select: {
        LABEL_SET_TEXT(managerNum, SUB_P_NFMAP_BUTTON_HINT_TEXT, Txt_BindLabel(NFMAP_HINT_BACK, 0));
        int value = SCROLL_GET_VALUE(control);
        if (value == 0) {
            ls.slot = 0;
            if (Menu_CheckSaveSpace(managerNum, &ls) && ls.canSave) {
                Menu_MapDefaultCodename(0, 0, 0xff);
                Menu_ChangePageCloseIris(P_CNNAME, managerNum, SUB_C_SBNFCN_IRIS);
                // the codename menu comes back here
                __Menu_Send(managerNum, C_SBCNOPTIONS, MessageType_SetId, P_NFSELECT, 0);
            }
        } else if (value < CODENAME_SLOTS_DEFAULT) {
            ls.slot = value;
            Menu_CreateOptionBoxLabel(managerNum, DEFAULT_CODENAME_WARNING, OPTIONBOX_DEFAULT_CODENAME, 1, 1);
            Menu_MapDefaultCodename(0, value, 0xff);
        } else {
            char *codename = codename_buf[SCROLL_GET_VALUE(control)];
            uint slot = SCROLL_GET_VALUE(control);
            Menu_StartLS(codename, LS_OPERATION_LOAD, 0, managerNum, 0, 0xff, slot, (HASHCODE)0, 0, control, 0);
        }
        break;
    }
    case MessageType_AltSelect1:
        ls.slot = 0;
        if (Menu_CheckSaveSpace(managerNum, &ls) && ls.canSave) {
            __Menu_SendMessage(&manager[managerNum].control, MessageType_GoPage, P_CNNAME, 0);
            __Menu_Send(managerNum, C_SBCNOPTIONS, MessageType_SetId, P_NFSELECT, 0);
        }
        break;
    }
    return true;
}
