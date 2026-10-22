// The menus' checkboxes: a box from the skin and a label beside it (PS2: menu_controls.cpp)

#include "Checkbox.h"
#include "Label.h"
#include "MenuCommon.h"
#include "Component.h"

#include "../gfx/Font.h"

#pragma fp_contract(off)

// A checkbox from the menu data: its skin's component 1, and a label as wide as its text past the skin's left inset
// AUTOINJECT
void Checkbox_Init(M_CHECKBOX *checkbox, NEW_CONTROL *create, M_CONTROL *parent) {
    checkbox->control.type = ControlType_Checkbox;
    checkbox->control.state = CONTROL_STATE_SHOWN;
    checkbox->control.skin = (uint)create->skin << 16 | 1;
    checkbox->control.parent = parent;
    checkbox->control.extraControlHash = (HASHCODE)0;
    checkbox->control.hashcode = create->hashcode;
    checkbox->control.id = create->id;
    checkbox->control.sprites = NULL;
    checkbox->control.depth = create->depth;
    checkbox->checked = (uchar)create->value;
    checkbox->control.drawState = 0;
    float textWidth = 0.0f;
    if (create->text != NULL) {
        GPOINT extent;
        Font_GetTextExtent(create->text, PlainFormat, &extent, NULL, -1);
        textWidth = extent.x;
    }
    NEW_CONTROL label = *create;
    label.x += Component_GetHandle(&checkbox->control)->left;
    label.font = PlainFormat;
    label.width = Ftol(textWidth) + 1;
    label.keepState = 0;
    Label_Init(&checkbox->label, &label, &checkbox->control);
    checkbox->control.x = create->x;
    checkbox->control.y = create->y;
    checkbox->control.width = create->width;
    checkbox->control.height = create->height;
    Script_PrepareScripting(&checkbox->control);
}

// What a checkbox does not handle itself goes to its label
// AUTOINJECT
intptr_t Checkbox_SendMessage(M_CHECKBOX *checkbox, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&checkbox->control);
        case MessageType_ScriptPlay:
            return Script_Play(&checkbox->control, arg1, (char)arg2);
        case MessageType_SetScript:
            return Control_SetScript(&checkbox->control, arg1, arg2);
        case MessageType_SetValue:
            checkbox->checked = (uchar)arg1;
            return 1;
        case MessageType_SetState:
            checkbox->control.state = (uchar)arg1;
            if (checkbox->control.state & CONTROL_STATE_HIDDEN)
                Component_SetupInstance(&checkbox->control, 0, -1, -1, -1, -1);
            Label_SendMessage(&checkbox->label, MessageType_SetState, arg1, arg2);
            return 1;
        case MessageType_GetValue:
            return (char)checkbox->checked;
        case MessageType_GetControl:
            return (intptr_t)checkbox;
        case MessageType_GetId:
            return checkbox->control.id;
        case MessageType_Destroy:
            Label_SendMessage(&checkbox->label, MessageType_Destroy, 0, 0);
            Component_Free(&checkbox->control);
            Script_DelAllScripts(&checkbox->control);
            Menu_Free((void **)&checkbox, 3);
            return 0;
        default:
            return Label_SendMessage(&checkbox->label, message, arg1, arg2);
    }
}

// Focused (the focus has its hashcode), select toggles it and goes to its owner; its skin shows it focused or not,
// checked or not
// AUTOINJECT
void Checkbox_Update(M_MANAGER *mgr, M_PAGE *page, M_CHECKBOX *checkbox) {
    bool selected = false;
    if ((checkbox->control.parent->state & CONTROL_STATE_HIDDEN) || (checkbox->control.state & CONTROL_STATE_HIDDEN)) {
        checkbox->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&checkbox->control);
        Label_Update(mgr, page, &checkbox->label, Menu_CursorOverMe(mgr, &checkbox->control));
        return;
    }
    checkbox->control.state &= ~CONTROL_STATE_UNDRAWN;
    short width = checkbox->control.width - Component_GetHandle(&checkbox->control)->left;
    short x = Component_GetHandle(&checkbox->control)->left + checkbox->control.x;
    Menu_ResizeControl(&checkbox->label.control, x, checkbox->control.y, width, checkbox->control.height);

    if (page->focused->hashcode == checkbox->control.hashcode && checkbox->control.state == CONTROL_STATE_SHOWN &&
        mgr->takingInput) {
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_SELECT, 4)) {
            checkbox->checked = !checkbox->checked;
            selected = true;
        }
        checkbox->control.drawState = 0x20;
    } else {
        checkbox->control.drawState = 0x10;
    }
    checkbox->control.drawState |= checkbox->checked ? 0x40 : 0x80;
    Label_Update(mgr, page, &checkbox->label, Menu_CursorOverMe(mgr, &checkbox->control));
    Component_SetupInstance(&checkbox->control, checkbox->control.drawState, -1, -1, -1, -1);
    if (selected && mgr->takingInput && !mgr->inputLocked) {
        Menu_PlaySound(MENU_SFX_SELECT);
        __Menu_SendMessage(checkbox->control.parent, MessageType_Select, (intptr_t)checkbox, 0);
    }
}
