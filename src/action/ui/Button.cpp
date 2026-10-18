// The menus' buttons: a skin and a label within it (PS2: menu_controls.cpp)

#include "Button.h"
#include "Label.h"
#include "MenuCommon.h"
#include "Component.h"

// A button from the menu data: its skin's component 0, and a label in the button's format a depth nearer
// AUTOINJECT
void Button_Init(M_BUTTON *button, NEW_CONTROL *create, M_CONTROL *parent) {
    button->control.state = CONTROL_STATE_SHOWN;
    button->control.type = ControlType_Button;
    button->control.skin = (uint)create->skin << 16;
    button->control.extraControlHash = (HASHCODE)0;
    button->control.parent = parent;
    button->control.hashcode = create->hashcode;
    button->control.id = create->id;
    button->control.depth = create->depth;
    button->control.sprites = NULL;
    button->unknown1aa = 0;
    button->control.drawState = 0;
    button->unknown1a8 = (short)create->value;
    NEW_CONTROL label = *create;
    label.depth--;
    label.keepState = 0;
    label.font = CentredFormat;
    Label_Init(&button->label, &label, &button->control);
    button->control.x = create->x;
    button->control.y = create->y;
    button->control.width = create->width;
    button->control.height = create->height;
    Script_PrepareScripting(&button->control);
}

// What a button does not handle itself goes to its label
// AUTOINJECT
int Button_SendMessage(M_BUTTON *button, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&button->control);
        case MessageType_ScriptPlay:
            return Script_Play(&button->control, arg1, (char)arg2);
        case MessageType_SetText:
            return Label_SendMessage(&button->label, MessageType_SetText, arg1, arg2);
        case MessageType_SetId:
            button->control.id = arg1;
            return 1;
        case MessageType_SetScript:
            return Control_SetScript(&button->control, arg1, arg2);
        case MessageType_SetState:
            button->control.state = (uchar)arg1;
            if (button->control.state & CONTROL_STATE_HIDDEN)
                Component_SetupInstance(&button->control, 0, -1, -1, -1, -1);
            Label_SendMessage(&button->label, MessageType_SetState, arg1, arg2);
            return 1;
        case MessageType_GetText:
            return (int)button->label.drawn->text;
        case MessageType_GetControl:
            return (int)button;
        case MessageType_GetId:
            return button->control.id;
        case MessageType_Destroy:
            Label_SendMessage(&button->label, MessageType_Destroy, 0, 0);
            Component_Free(&button->control);
            Script_DelAllScripts(&button->control);
            Menu_Free((void **)&button, 3);
            return 0;
        default:
            return Label_SendMessage(&button->label, message, arg1, arg2);
    }
}

// The label within the skin's insets; the button focused when the focus has its hashcode, pressed while select is held
// on it; select (held: repeat) and the alternative selects go to its owner
// AUTOINJECT
void Button_Update(M_MANAGER *mgr, M_PAGE *page, M_BUTTON *button) {
    char pressed = 0;
    if ((button->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (button->control.state & CONTROL_STATE_HIDDEN)) {
        button->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&button->control);
        Label_Update(mgr, page, &button->label, Menu_CursorOverMe(mgr, &button->control));
        return;
    }
    button->control.state &= ~CONTROL_STATE_UNDRAWN;
    short height = button->control.height - Component_GetHandle(&button->control)->bottom -
                   Component_GetHandle(&button->control)->top;
    short width = button->control.width - Component_GetHandle(&button->control)->right -
                  Component_GetHandle(&button->control)->left;
    short y = Component_GetHandle(&button->control)->top + button->control.y;
    short x = Component_GetHandle(&button->control)->left + button->control.x;
    Menu_ResizeControl(&button->label.control, x, y, width, height);

    if (page->focused->hashcode == button->control.hashcode) {
        button->control.drawState = 0x20;
        if (Menu_CursorOverMe(mgr, &button->control) ||
            (button->control.hashcode == page->focused->hashcode && button->control.id == page->focused->id)) {
            if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_SELECT, 4))
                pressed = 1;
            if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_SELECT, 8))
                pressed = 2;
            if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_1, 4))
                pressed = 3;
            if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_2, 4))
                pressed = 4;
            button->control.drawState = 0x80;
            if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_SELECT, 1))
                button->control.drawState = 0x40;
        }
    } else {
        button->control.drawState = 0x10;
    }
    Component_SetupInstance(&button->control, button->control.drawState, -1, -1, -1, -1);
    Label_Update(mgr, page, &button->label, Menu_CursorOverMe(mgr, &button->control));
    if (!mgr->takingInput || mgr->inputLocked)
        return;
    if (pressed == 1) {
        Menu_PlaySound(MENU_SFX_SELECT);
        __Menu_SendMessage(button->control.parent, MessageType_Select, (int)button, 0);
    } else if (pressed == 2) {
        Menu_PlaySound(MENU_SFX_SELECT);
        __Menu_SendMessage(button->control.parent, MessageType_SelectRepeat, (int)button, 0);
    } else if (pressed == 3) {
        Menu_PlaySound(MENU_SFX_ALTSELECT_1);
        __Menu_SendMessage(button->control.parent, MessageType_AltSelect1, (int)button, 0);
    } else if (pressed == 4) {
        Menu_PlaySound(MENU_SFX_ALTSELECT_2);
        __Menu_SendMessage(button->control.parent, MessageType_AltSelect2, (int)button, 0);
    }
}
