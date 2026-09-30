#include "ui.h"

#include "Manager.h"
#include "Menu.h"
#include "../engine/Text.h"

#include <stdio.h>

// The codename menu (P_CNMENU / C_SBCNOPTIONS): Secret Unlocks, the options pages, and saving the codename.

// cn_options (ui.h). The last item's title is "Codenames" for a default codename, which cannot be saved, else "Save Codename".
// XBE_GLOBAL(0x0017cec8, 0xa8)
M_ITEM cn_options[7] = {
    {ICON_CNOPTIONS_SECRETUNLOCKS, SECRET_UNLOCKS, SECRET_UNLOCKS_DESC, 0, 1, TXT_NULL}, // Secret Unlocks
    {ICON_CNOPTIONS_CONTROLLER, CONTROLLER_SETUP, CONTROL_SCHEME_ACTION_DESC, 1, 1, TXT_NULL}, // Controller Setup
    {ICON_CNOPTIONS_CONTROLLER, CN_DRIVING_CONTROLLER, CONTROL_SCHEME_DRIVING_DESC, 2, 1, TXT_NULL}, // Driving Controller
    {ICON_CNOPTIONS_ADVANCED, CN_ADVANCED_OPTIONS, ADV_SETTINGS_DESC, 3, 1, TXT_NULL}, // Advanced Options
    {ICON_CNOPTIONS_MULTIPLAYER, CN_MULTIPLAYER_OPTIONS, MULTIPLAYER_SETTINGS_DESC, 4, 1, TXT_NULL}, // Multiplayer Options
    {ICON_CNOPTIONS_AV, CN_AV_OPTIONS, AV_SETTINGS_DESC, 5, 1, TXT_NULL}, // AV Options
    {ICON_CNOPTIONS_SAVE, SAVE_CODENAME, ACCEPT_CHANGES_DESC, 6, 1, TXT_NULL}, // Save Codename
};

// XBE_GLOBAL(0x0025ed28, 0x48)
#define cn_menu_title_text ((char *)0x0025ed28)  // "Edit <codename>"

// AUTOGEN
void Menu_UpdateDefaultCodename(byte param_1, byte slot);

// AUTOINJECT
bool P_CNMENU_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
    switch (message) {
    case MessageType_PageEnter:
        // Coming back from one of its own pages (Secret Unlocks among them) keeps the changes made so far; arriving
        // with a codename just picked, or just named, starts afresh.
        if ((HASHCODE)arg2 != P_CNSELECT && ((HASHCODE)arg2 != P_CNNAME || cn_secret_mode)) {
            Menu_StartIris(4, managerNum, SUB_C_SBCNOPTIONS_IRIS);
            break;
        }
        cn_modified = 0;
        Menu_SelectItemInControl(CONTROL_GET(managerNum, C_SBCNOPTIONS), cn_options, ARRAY_SIZE(cn_options), 1);
        Menu_StartIris(0, managerNum, SUB_C_SBCNOPTIONS_IRIS);
        sprintf(cn_menu_title_text, Txt_BindLabel(CODENAME_EDIT_TITLE, 0), ls.codename);
        LABEL_SET_TEXT(managerNum, SUB_P_CNMENU_TITLE_TEXT, cn_menu_title_text);
        cn_options[6].title = ls.slot < 2 ? CODENAMES : SAVE_CODENAME;
        // a codename just named goes straight on to its controller setup
        if ((HASHCODE)arg2 == P_CNNAME && !cn_secret_mode)
            Manager_SendMessage(&manager[managerNum], MessageType_GoPage, P_CNCONTROLS, 0);
        break;
    case MessageType_PageUpdate:
        Menu_PlayIris(1, managerNum, SUB_C_SBCNOPTIONS_IRIS);
        Menu_UpdateMessageBox(managerNum, 0xff, 0);
        if (Menu_UpdateOptionBox(NULL) == 3)   // "lose your changes?" answered yes
            Manager_SendMessage(&manager[managerNum], MessageType_Back, 1, 0);
        break;
    case MessageType_SaveFlowDone:
        if (ls.returnPage != 0)
            Menu_ChangePageCloseIris(ls.returnPage, managerNum, SUB_C_SBCNSELECT_IRIS);
        break;
    case MessageType_QueryBack:
        if (cn_modified) {
            Menu_CreateOptionBoxLabel(managerNum, EXIT_LOSING_CHANGES_CONFIRM, OPTIONBOX_LOSE_CHANGES, 1, 1);
            *(int *)arg2 = -2;   // stay on the page
        }
        break;
    }
    return true;
}

// AUTOINJECT
bool C_SBCNOPTIONS_Handler(uchar managerNum, M_CONTROL *control, uint hashcode, uint message, int arg1, int arg2) {
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
            ls.field3_0xc = 1;
            ls.operation = LS_OPERATION_SAVE;
            ls.slot = 999;
            ls.field9_0x3a = 0;
            ls.field5_0x14 = (undefined4)control;
            ls.field17_0x42 = 0;
            Menu_UpdateMessageBox(managerNum, 0xff, 0);
            break;
        }
        break;
    }
    return true;
}
