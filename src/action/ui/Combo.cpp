// The menus' combo boxes: a label showing a list's selected row, the list opening under it when the label is pressed
// (PS2: menu_controls.cpp)

#include "Combo.h"
#include "Label.h"
#include "List.h"
#include "MenuCommon.h"
#include "Component.h"

#include "../memory.h"
#include "../engine/psiInput.h"
#include "../gfx/Font.h"

#pragma fp_contract(off)

// A combo from the menu data: its skin's component 2, a label a line high and a list under it, hidden, both a depth
// nearer. The combo is as high as the label.
// AUTOINJECT
void Combo_Init(M_COMBO *combo, NEW_CONTROL *create, M_CONTROL *parent) {
    combo->control.type = ControlType_Combo;
    combo->control.state = CONTROL_STATE_SHOWN;
    combo->control.skin = (uint)create->skin << 16 | 2;
    combo->control.parent = parent;
    combo->control.extraControlHash = (HASHCODE)0;
    combo->control.hashcode = create->hashcode;
    combo->control.id = create->id;
    combo->control.sprites = NULL;
    combo->control.depth = create->depth;
    combo->control.drawState = 0;
    // By the list's row height, which List_Init only sets below
    combo->visibleRows = Ftol(create->height / (double)combo->list.rowHeight) - 1;
    combo->list.selected = NULL;
    GPOINT extent;
    Font_GetTextExtent(MenuAlphabet, PlainFormat, &extent, NULL, -1);
    combo->labelHeight = extent.y + 2.0f;

    // As high as the combo was before its height is set below
    NEW_CONTROL label = *create;
    label.x += Component_GetHandle(&combo->control)->left;
    label.height = combo->control.height;
    label.depth = create->depth - 1;
    label.font = PlainFormat;
    label.text = MenuEmptyString;
    label.keepState = 0;
    Label_Init(&combo->label, &label, &combo->control);
    Label_SendMessage(&combo->label, MessageType_SetState, CONTROL_STATE_HIDDEN, 0);

    NEW_CONTROL list = *create;
    list.x += Component_GetHandle(&combo->control)->left;
    list.y += Component_GetHandle(&combo->control)->top + Ftol(combo->labelHeight) + 4;
    list.width += Component_GetHandle(&combo->control)->right - Component_GetHandle(&combo->control)->left;
    list.height = 1;
    list.value = LIST_HIGHLIGHT_BAR;
    list.depth = create->depth - 1;
    list.keepState = 0;
    List_Init(&combo->list, &list, &combo->control);

    combo->control.x = create->x;
    combo->control.y = create->y;
    combo->control.width = create->width;
    combo->control.height = Ftol(combo->labelHeight) + 5;
    Script_PrepareScripting(&combo->control);
}

// AUTOINJECT
int Combo_SendMessage(M_COMBO *combo, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&combo->control);
        case MessageType_GetNumItems:
            return List_SendMessage(&combo->list, MessageType_GetNumItems, arg1, arg2);
        case MessageType_GetControl:
            return (int)combo;
        case MessageType_GetVisibleRows:
            return combo->visibleRows;
        case MessageType_Destroy:
            Label_SendMessage(&combo->label, MessageType_Destroy, 0, 0);
            List_SendMessage(&combo->list, MessageType_Destroy, 0, 0);
            Component_Free(&combo->control);
            Script_DelAllScripts(&combo->control);
            Mem_Free((void **)&combo);
            return 0;
        case MessageType_Scroll:
        case MessageType_Select:
            return __Menu_SendMessage(combo->control.parent, message, (int)combo, 0);
        default:
            return 0;
    }
}

// The list under the label, the label within the skin's left inset. Pressing select (player 1's) with the cursor on
// the label opens the list; pressing it again while it is open plays the select sound.
// AUTOINJECT
void Combo_Update(M_MANAGER *mgr, M_PAGE *page, M_COMBO *combo) {
    bool pressedOpen = false;
    if ((combo->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (combo->control.state & CONTROL_STATE_HIDDEN)) {
        combo->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&combo->control);
        Label_Update(mgr, page, &combo->label, false);
        return;
    }
    combo->control.state &= ~CONTROL_STATE_UNDRAWN;
    combo->visibleRows = Ftol(combo->list.control.height / (double)combo->list.rowHeight);
    short width = combo->control.width - Component_GetHandle(&combo->control)->right -
                  Component_GetHandle(&combo->control)->left;
    short y = Component_GetHandle(&combo->control)->top + Ftol(combo->labelHeight) + combo->control.y + 4;
    Menu_ResizeControl(&combo->list.control, Component_GetHandle(&combo->control)->left + combo->control.x, y, width,
                       combo->list.control.height);
    short labelHeight = Ftol(combo->labelHeight) + 1;
    Menu_ResizeControl(&combo->label.control, Component_GetHandle(&combo->control)->left + combo->control.x,
                       combo->control.y, combo->control.width - Component_GetHandle(&combo->control)->right,
                       labelHeight);
    Component_SetupInstance(&combo->control, combo->control.drawState, -1, -1, -1, -1);

    if (page->focused->hashcode == combo->control.hashcode) {
        combo->control.drawState |= 0x20;
        if (Menu_InputAction(0, ACTION_MENU_SELECT, 4) && mgr->cursorSprite->positionX >= combo->control.x &&
            mgr->cursorSprite->positionX <= combo->control.x + combo->control.width &&
            mgr->cursorSprite->positionY >= combo->control.y &&
            mgr->cursorSprite->positionY <= combo->control.y + (double)combo->labelHeight + 5.0) {
            if (combo->control.drawState & COMBO_OPEN) {
                combo->control.drawState |= 0x40;
                pressedOpen = true;
            } else {
                // Grown over the list, which is shown
                combo->control.drawState |= COMBO_OPEN | 0x40;
                combo->control.height += Component_GetHandle(&combo->control)->bottom + combo->list.control.height;
                List_SendMessage(&combo->list, MessageType_SetState, CONTROL_STATE_SHOWN, 0);
            }
        } else {
            combo->control.drawState &= ~0x40;
        }
        if (combo->control.drawState & COMBO_OPEN)
            List_Update(mgr, page, &combo->list);
    } else {
        combo->control.drawState = 0x10;
    }

    // A list does not answer GetSelectedItemText: its 0 blanks the label
    Label_SendMessage(&combo->label, MessageType_SetText,
                      List_SendMessage(&combo->list, MessageType_GetSelectedItemText, 0, 0), 0);
    Label_Update(mgr, page, &combo->label, Menu_CursorOverMe(mgr, &combo->control));
    Component_SetupInstance(&combo->control, combo->control.drawState, -1, -1, -1, -1);
    if (pressedOpen)
        Menu_PlaySound(MENU_SFX_SELECT);
}
