// The menus' radios: one item of a list shown at a time in a label, picked with left and right by a scroll (PS2:
// menu_controls.cpp)

#include "Radio.h"
#include "Label.h"
#include "MenuCommon.h"
#include "Component.h"
#include "Scroll.h"

#include "../engine/Text.h"

#include <string.h>

// A radio from the menu data: its skin's component 5, a label within the skin's insets (a depth nearer, in the
// centred format) and a scroll over the whole radio, without its bar
// AUTOINJECT
void Radio_Init(M_RADIO *radio, NEW_CONTROL *create, M_CONTROL *parent) {
    radio->control.type = ControlType_Radio;
    radio->control.state = CONTROL_STATE_SHOWN;
    radio->control.skin = (uint)create->skin << 16 | 5;
    radio->control.extraControlHash = (HASHCODE)0;
    radio->control.parent = parent;
    radio->control.hashcode = create->hashcode;
    radio->control.sprites = NULL;
    radio->control.id = create->id;
    radio->selected = NULL;
    radio->control.drawState = 0;
    radio->control.depth = create->depth;
    LinkedList_Init(&radio->items, 0);
    radio->control.x = create->x;
    radio->control.y = create->y;
    radio->control.width = create->width;
    radio->control.height = create->height;

    NEW_CONTROL label = *create;
    label.x += Component_GetHandle(&radio->control)->left;
    label.width = label.width - Component_GetHandle(&radio->control)->right - Component_GetHandle(&radio->control)->left;
    label.depth -= 5;
    label.font = CentredFormat;
    label.keepState = 0;
    Label_Init(&radio->label, &label, &radio->control);

    NEW_CONTROL scroll = *create;
    scroll.min = 0;
    scroll.max = 0;
    scroll.hasBar = 0;
    scroll.keepState = 0;
    Scroll_Init(&radio->scroll, &scroll, &radio->control);
    Script_PrepareScripting(&radio->control);
}

// An item's index
int _Radio_GetIndex(M_RADIO *radio, LIST_ROW *item) {
    int index = 0;
    for (LIST_ROW *i = (LIST_ROW *)LinkedList_First(&radio->items); i != NULL; i = i->next) {
        if (i == item)
            return index;
        index++;
    }
    return -1;
}

// EAX the radio, EDX the item (kept); the original's stack argument (the items) is not read
// AUTOLTCG
__declspec(naked) void Radio_GetIndex(void) {
    __asm {
        push ecx
        push edx
        push edx
        push eax
        call _Radio_GetIndex
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// The item at an index, or the last for an index past the end; the radio must have an item
LIST_ROW* _Radio_SetIndex(M_RADIO *radio, int index) {
    LIST_ROW *item = (LIST_ROW *)LinkedList_First(&radio->items);
    for (int i = 0; i < index && item->next != NULL; i++)
        item = item->next;
    return item;
}

// ECX the radio, ESI the index; the original's stack argument (the items) is not read
// AUTOLTCG
__declspec(naked) void Radio_SetIndex(void) {
    __asm {
        push ecx
        push edx
        push esi
        push ecx
        call _Radio_SetIndex
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// What a radio does not handle itself goes to its label
// AUTOINJECT
int Radio_SendMessage(M_RADIO *radio, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_AddItem:
        case MessageType_AddItemLabel: {
            // The scroll's maximum raised for each item after the first; the first selected
            char *text = message == MessageType_AddItemLabel ? (char *)Txt_BindLabel((Action_TranslatedText)arg1, 0)
                                                              : (char *)arg1;
            LIST_ROW *item = (LIST_ROW *)Menu_Malloc(sizeof(LIST_ROW));
            strcpy(item->cells[0].text, text);
            item->value = arg2;
            LinkedList_Add(&radio->items, &item->prev, item);
            if (radio->items.count > 1)
                Scroll_SendMessage(&radio->scroll, MessageType_ScrollSetMax,
                                   Scroll_SendMessage(&radio->scroll, MessageType_GetMax, 0, 0) + 1, 0);
            if (radio->items.count == 1)
                radio->selected = (LIST_ROW *)LinkedList_First(&radio->items);
            return 1;
        }
        case MessageType_Scroll:
        case MessageType_Select:
        case MessageType_MemoBack:
        case MessageType_AltSelect1:
        case MessageType_AltSelect2:
            radio->selected = _Radio_SetIndex(radio, Scroll_SendMessage(&radio->scroll, MessageType_GetValue, 0, 0));
            __Menu_SendMessage(radio->control.parent, message, (int)radio, 0);
            return 1;
        case MessageType_ClearItems:
            if (radio->items.count != 0) {
                LIST_ROW *item = (LIST_ROW *)LinkedList_First(&radio->items);
                while (item != NULL) {
                    LIST_ROW *next = item->next;
                    LinkedList_Remove(&radio->items, &item->prev, item);
                    Menu_Free((void **)&item, 2);
                    item = next;
                }
            }
            radio->selected = NULL;
            Scroll_SendMessage(&radio->scroll, MessageType_SetValue, 0, 0);
            Scroll_SendMessage(&radio->scroll, MessageType_ScrollSetMax, 0, 0);
            return 0;
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&radio->control);
        case MessageType_Destroy:
            Radio_SendMessage(radio, MessageType_ClearItems, 0, 0);
            Label_SendMessage(&radio->label, MessageType_Destroy, 0, 0);
            Scroll_SendMessage(&radio->scroll, MessageType_Destroy, 0, 0);
            Component_Free(&radio->control);
            Script_DelAllScripts(&radio->control);
            Menu_Free((void **)&radio, 3);
            return 0;
        case MessageType_GetSelectedIndex:
        case MessageType_GetValue:
            return radio->scroll.value;
        case MessageType_GetSelectedItemValue:
            if (radio->selected == NULL)
                return 0;
            return radio->selected->value;
        case MessageType_GetNumItems:
            return radio->items.count - 1;
        case MessageType_GetControl:
            return (int)radio;
        case MessageType_GetSelectedItemText:
            if (radio->selected == NULL)
                return 0;
            return (int)radio->selected->cells[0].text;
        case MessageType_GetId:
            return radio->control.id;
        case MessageType_ScriptPlay:
            return Script_Play(&radio->control, arg1, (char)arg2);
        case MessageType_SelectIndex:
            // The scroll goes to the item selected before, then the index's item is selected and the scroll put on it
            if (!Scroll_SendMessage(&radio->scroll, MessageType_SetValue, _Radio_GetIndex(radio, radio->selected), 0))
                return 0;
            radio->selected = _Radio_SetIndex(radio, arg1);
            Scroll_SendMessage(&radio->scroll, MessageType_SetValue, arg1, arg2);
            return __Menu_SendMessage(radio->control.parent, MessageType_Scroll, (int)radio, 0);
        case MessageType_SelectItemByValue:
            for (LIST_ROW *item = (LIST_ROW *)LinkedList_First(&radio->items); item != NULL; item = item->next) {
                if (item->value == arg1) {
                    radio->selected = item;
                    Scroll_SendMessage(&radio->scroll, MessageType_SetValue, _Radio_GetIndex(radio, radio->selected),
                                       0);
                    break;
                }
            }
            if (arg2 != 0)
                return 1;
            return __Menu_SendMessage(radio->control.parent, MessageType_Scroll, (int)radio, 0);
        case MessageType_SetId:
            // The last item's value
            if (radio->selected == NULL)
                return 0;
            ((LIST_ROW *)LinkedList_Last(&radio->items))->value = arg1;
            return 1;
        case MessageType_SetScript:
            return Control_SetScript(&radio->control, arg1, arg2);
        case MessageType_SetState:
            radio->control.state = (uchar)arg1;
            Label_SendMessage(&radio->label, MessageType_SetState, arg1, arg2);
            Scroll_SendMessage(&radio->scroll, MessageType_SetState, arg1, arg2);
            return 1;
        case MessageType_ValueSet:
            radio->selected = _Radio_SetIndex(radio, Scroll_SendMessage(&radio->scroll, MessageType_GetValue, 0, 0));
            return __Menu_SendMessage(radio->control.parent, MessageType_Scroll, (int)radio, 1);
        default:
            return Label_SendMessage(&radio->label, message, arg1, arg2);
    }
}

// The scroll over the whole radio, the label within the skin's insets showing the selected item's text
// AUTOINJECT
void Radio_Update(M_MANAGER *mgr, M_PAGE *page, M_RADIO *radio) {
    if ((radio->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (radio->control.state & CONTROL_STATE_HIDDEN)) {
        radio->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&radio->control);
        Scroll_Update(mgr, page, &radio->scroll);
        Label_Update(mgr, page, &radio->label, Menu_CursorOverMe(mgr, &radio->control));
        return;
    }
    radio->control.state &= ~CONTROL_STATE_UNDRAWN;
    Menu_ResizeControl(&radio->scroll.control, &radio->control);
    short width = radio->control.width - Component_GetHandle(&radio->control)->right -
                  Component_GetHandle(&radio->control)->left;
    Menu_ResizeControl(&radio->label.control, Component_GetHandle(&radio->control)->left + radio->control.x,
                       radio->control.y - 1, width, radio->control.height);
    Scroll_Update(mgr, page, &radio->scroll);
    radio->label.text = radio->selected != NULL ? radio->selected->cells[0].text : MenuSpaceString;
    Label_Update(mgr, page, &radio->label, Menu_CursorOverMe(mgr, &radio->control));
}
