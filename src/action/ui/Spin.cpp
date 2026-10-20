// The menus' spin boxes: a number in a label, moved by a thin scroll at its right (PS2: menu_controls.cpp). The
// number is the scroll's value turned over its range, max + min - value, so that up raises it.

#include "Spin.h"
#include "Label.h"
#include "MenuCommon.h"
#include "Component.h"
#include "Scroll.h"

#include "../gfx/Font.h"

#include <stdio.h>

#pragma fp_contract(off)

// The game's "0", a spin's first text
// XBE_GLOBAL(0x00161884, 0x2) readonly relocate
char SpinZero[] = "0";

// The scroll's width
#define SPIN_SCROLL_WIDTH 8

// A spin from the menu data: its skin's component 9, a line of the plain format high; a label within the skin's
// insets (a depth nearer) and the scroll at its right, at the range's minimum
// AUTOINJECT
void Spin_Init(M_SPIN *spin, NEW_CONTROL *create, M_CONTROL *parent) {
    spin->control.type = ControlType_Spin;
    spin->control.state = CONTROL_STATE_SHOWN;
    spin->control.skin = (uint)create->skin << 16 | 9;
    spin->control.extraControlHash = (HASHCODE)0;
    spin->control.parent = parent;
    spin->control.hashcode = create->hashcode;
    spin->control.sprites = NULL;
    spin->control.id = create->id;
    spin->control.drawState = 0;
    spin->control.depth = create->depth;
    GPOINT extent;
    Font_GetTextExtent(MenuAlphabet, PlainFormat, &extent, NULL, -1);
    double labelHeight = extent.y + 2.0;
    spin->labelHeight = (float)labelHeight;
    spin->control.x = create->x;
    spin->control.y = create->y;
    spin->control.width = create->width;
    // From the sum before it is rounded to a float
    spin->control.height = Ftol(labelHeight) + 4;

    NEW_CONTROL label = *create;
    label.x += Component_GetHandle(&spin->control)->left;
    label.width = label.width - Component_GetHandle(&spin->control)->right - Component_GetHandle(&spin->control)->left;
    label.font = PlainFormat;
    label.text = SpinZero;
    label.depth--;
    label.height = Ftol(spin->labelHeight) + 2;
    label.keepState = 0;
    Label_Init(&spin->label, &label, &spin->control);

    NEW_CONTROL scroll = *create;
    scroll.x += create->width - SPIN_SCROLL_WIDTH;
    scroll.width = SPIN_SCROLL_WIDTH;
    scroll.height = Ftol(spin->labelHeight) + 4;
    scroll.hasBar = 1;
    scroll.keepState = 0;
    Scroll_Init(&spin->scroll, &scroll, &spin->control);
    Scroll_SendMessage(&spin->scroll, MessageType_SetValue, create->max, 0);
    Script_PrepareScripting(&spin->control);
}

// Values in and out are the number shown; what a spin does not handle itself goes to its label
// AUTOINJECT
int Spin_SendMessage(M_SPIN *spin, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&spin->control);
        case MessageType_ScriptPlay:
            return Script_Play(&spin->control, arg1, arg2);
        case MessageType_SetScript:
            return Control_SetScript(&spin->control, arg1, arg2);
        case MessageType_ScrollSetMax:
        case MessageType_ScrollSetMin:
        case MessageType_SetRange:
        case MessageType_GetMax:
        case MessageType_GetMin:
            return Scroll_SendMessage(&spin->scroll, message, arg1, arg2);
        case MessageType_SetState:
            spin->control.state = (uchar)arg1;
            if (spin->control.state & CONTROL_STATE_HIDDEN)
                Component_SetupInstance(&spin->control, 0, -1, -1, -1, -1);
            Label_SendMessage(&spin->label, MessageType_SetState, arg1, arg2);
            Scroll_SendMessage(&spin->scroll, MessageType_SetState, arg1, arg2);
            return 1;
        case MessageType_SetValue:
            return Scroll_SendMessage(&spin->scroll, MessageType_SetValue,
                                      spin->scroll.max + spin->scroll.min - arg1, arg2);
        case MessageType_GetControl:
            return (int)spin;
        case MessageType_GetId:
            return spin->control.id;
        case MessageType_GetValue:
            return spin->scroll.max - spin->scroll.value + spin->scroll.min;
        case MessageType_Destroy:
            Label_SendMessage(&spin->label, MessageType_Destroy, 0, 0);
            Scroll_SendMessage(&spin->scroll, MessageType_Destroy, 0, 0);
            Component_Free(&spin->control);
            Script_DelAllScripts(&spin->control);
            Menu_Free((void **)&spin, 3);
            return 0;
        case MessageType_Scroll:
        case MessageType_Select:
        case MessageType_MemoBack:
            return __Menu_SendMessage(spin->control.parent, message, (int)spin, 0);
        default:
            return Label_SendMessage(&spin->label, message, arg1, arg2);
    }
}

// The scroll at the right, the label within the skin's insets showing the number
// AUTOINJECT
void Spin_Update(M_MANAGER *mgr, M_PAGE *page, M_SPIN *spin) {
    if ((spin->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (spin->control.state & CONTROL_STATE_HIDDEN)) {
        spin->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&spin->control);
        Scroll_Update(mgr, page, &spin->scroll);
        Label_Update(mgr, page, &spin->label, Menu_CursorOverMe(mgr, &spin->control));
        return;
    }
    spin->control.state &= ~CONTROL_STATE_UNDRAWN;
    Menu_ResizeControl(&spin->scroll.control, spin->control.x + spin->control.width - SPIN_SCROLL_WIDTH,
                       spin->control.y, SPIN_SCROLL_WIDTH, spin->control.height);
    short labelHeight = Ftol(spin->labelHeight) + 2;
    short width = spin->control.width - Component_GetHandle(&spin->control)->right -
                  Component_GetHandle(&spin->control)->left;
    Menu_ResizeControl(&spin->label.control, Component_GetHandle(&spin->control)->left + spin->control.x,
                       spin->control.y, width, labelHeight);
    spin->control.drawState = page->focused->hashcode == spin->control.hashcode ? 0x20 : 0x10;
    sprintf(spin->text, "%d", spin->scroll.max - spin->scroll.value + spin->scroll.min);
    Label_SendMessage(&spin->label, MessageType_SetText, (int)spin->text, 0);
    Label_Update(mgr, page, &spin->label, Menu_CursorOverMe(mgr, &spin->control));
    Component_SetupInstance(&spin->control, spin->control.drawState, -1, -1,
                            spin->control.width - SPIN_SCROLL_WIDTH, -1);
    Scroll_Update(mgr, page, &spin->scroll);
}
