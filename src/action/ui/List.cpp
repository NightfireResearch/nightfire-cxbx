// The menus' lists: rows of up to five columns, a scroll at the right that selects a row, and a bar under the selected
// row (PS2: menu_controls.cpp)

#include "List.h"
#include "MenuCommon.h"
#include "Component.h"
#include "Scroll.h"

#include "../engine/Text.h"
#include "../gfx/Font.h"

#include <string.h>

#pragma fp_contract(off)

// The bar at the right
#define LIST_BAR_WIDTH 16
// The texture and rectangle of a row's stripe
#define STRIPE_TEXTURE ((HASHCODE)0x3000042)

// AUTOINJECT
void List_Init(M_LIST *list, NEW_CONTROL *create, M_CONTROL *parent) {
    list->format = ListFormat1;
    strcpy(list->selectedFormat, ListFormat1);
    GPOINT extent;
    Font_GetTextExtent(MenuAlphabet, list->format, &extent, NULL, -1);
    list->control.type = ControlType_List;
    list->control.state = CONTROL_STATE_SHOWN;
    list->control.skin = (uint)create->skin << 16 | 3;
    list->control.parent = parent;
    list->control.extraControlHash = (HASHCODE)0;
    list->topRow = 0;
    list->selected = NULL;
    list->control.hashcode = create->hashcode;
    list->control.id = create->id;
    list->control.sprites = NULL;
    list->control.depth = create->depth;
    // The visible rows are the height over the row's height as computed, not as stored
    double rowHeight = extent.y * (double)1.1f;
    list->rowHeight = (float)rowHeight;
    list->highlight.control.depth = create->depth - 1;
    list->visibleRows = Ftol(create->height / rowHeight);
    list->numColumns = 1;
    list->highlightMode = (uchar)create->value;
    list->highlightColour = 0xff;
    list->normalColour = 0x404040ff;
    list->pulseColour = 0x7f7f7fff;
    list->alpha = 0x80;
    list->pulse = 0;
    list->stripeColour = 0;
    list->wrap = 0;
    list->continuedRows = 0;
    LinkedList_Init(&list->rows, 0);
    memset(list->cells, 0, sizeof(list->cells));
    memset(list->columnWidth, 0, sizeof(list->columnWidth));
    list->columnAlign[0] = 1;
    list->columnWidth[0] = 100;

    // The bar along the right, as high as the list (whose height is only set below)
    NEW_CONTROL bar = *create;
    bar.x += bar.width - LIST_BAR_WIDTH;
    bar.width = LIST_BAR_WIDTH;
    bar.height = list->control.height;
    bar.min = 0;
    bar.max = 0;
    bar.hasBar = 1;
    bar.depth = create->depth - 1;
    bar.keepState = 0;
    Scroll_Init(&list->scroll, &bar, &list->control);

    memset(&list->highlight, 0, sizeof(list->highlight));
    list->highlight.control.type = ControlType_72;
    list->highlight.control.parent = &list->control;
    list->highlight.control.sprites = NULL;
    list->highlight.control.skin = (uint)create->skin << 16 | 4;
    Component_SetupInstance(&list->highlight.control, 0, -1, -1, -1, -1);
    list->control.x = create->x;
    list->control.y = create->y;
    list->control.width = create->width;
    list->control.height = create->height;
    Script_PrepareScripting(&list->control);
}

// A row's index
int List_GetIndex(M_LIST *list, LIST_ROW *row) {
    int index = 0;
    for (LIST_ROW *r = (LIST_ROW *)LinkedList_First(&list->rows); r != NULL; r = r->next) {
        if (r == row)
            return index;
        index++;
    }
    return -1;
}

// The row at an index
LIST_ROW* List_SetIndex(M_LIST *list, int index) {
    LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows);
    for (int i = 0; i < index; i++) {
        if (row == NULL)
            return NULL;
        row = row->next;
    }
    return row;
}

// The index of a row among those a text began on (wrapped rows not counted); the first row is not tested for a
// continuation, and a row not in the list reads past the last
// AUTOINJECT
int List_GetIndex_RealItem(M_LIST *list, LINKEDLIST *rows, LIST_ROW *row) {
    LIST_ROW *r = (LIST_ROW *)LinkedList_First(&list->rows);
    if (r == NULL)
        return -1;
    int index = 0;
    while (r != row) {
        r = r->next;
        if (!r->continued)
            index++;
    }
    return index;
}

// The row a text began on at an index (wrapped rows not counted); the list must have a row
// AUTOINJECT
LIST_ROW* List_SetIndex_RealItem(M_LIST *list, LINKEDLIST *rows, int index) {
    int i = -1;
    for (LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows);; row = row->next) {
        if (!row->continued)
            i++;
        if (i == index)
            return row;
        if (row->next == NULL)
            return NULL;
    }
}

// Scrolls so that a wrapped row after the selection shows, and puts the scroll on the selection
// FUNC_AT(0x00090f20)
void List_ShowSelected(M_LIST *list) {
    LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows);
    int index = list->selectedIndex;
    for (int i = 0; i < index + 1; i++) {
        if (row == NULL)
            goto scroll;
        row = row->next;
    }
    if (row != NULL && row->continued && index - list->topRow + 2 > list->visibleRows)
        List_SendMessage(list, MessageType_ListSetTopRow, index - list->visibleRows + 2, 0);
scroll:
    Scroll_SendMessage(&list->scroll, MessageType_SetValue, list->selectedIndex, 0);
}

// A row, its cells cleared
static LIST_ROW* List_NewRow(void) {
    return (LIST_ROW *)Menu_Malloc(sizeof(LIST_ROW));
}

// A row added at the end: the first selected, and the scroll's maximum raised for each after the first
static void List_AddRow(M_LIST *list, LIST_ROW *row) {
    LinkedList_Add(&list->rows, &row->prev, row);
    if (list->selected == NULL)
        list->selected = row;
    if (list->rows.count > 1)
        Scroll_SendMessage(&list->scroll, MessageType_ScrollAddToMax, 1, 0);
}

// Brought up to date at once, without input, while its page has a focused control
static void List_Refresh(M_LIST *list) {
    M_PAGE *page = Menu_GetPage(&list->control);
    M_MANAGER *mgr = (M_MANAGER *)page->control.parent;
    if (page->focused == NULL)
        return;
    mgr->takingInput = false;
    List_Update(mgr, page, list);
    mgr->takingInput = true;
}

// AUTOINJECT
intptr_t List_SendMessage(M_LIST *list, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_ListAddColumn:
            if (list->numColumns >= LIST_COLUMNS)
                return 0;
            list->columnWidth[list->numColumns] = arg1;
            list->columnAlign[list->numColumns] = arg2;
            list->numColumns++;
            return 1;
        case MessageType_ListAddRow: {
            // A copy of the cells' texts. (The original's alternative for a cell without one, its icon, tests the
            // address of the text, which is never NULL.)
            LIST_ROW *from = (LIST_ROW *)arg1;
            LIST_ROW *row = List_NewRow();
            for (int i = 0; i < LIST_COLUMNS; i++) {
                row->cells[i].text[0] = '\0';
                row->cells[i].icon = (HASHCODE)0;
                row->cells[i].u = 0;
                row->cells[i].v = 0;
                row->cells[i].uw = 0;
                row->cells[i].vh = 0;
            }
            row->value = 0;
            row->stripe = NULL;
            for (uchar i = 0; i < list->numColumns; i++)
                strcpy(row->cells[i].text, from->cells[i].text);
            row->value = arg2;
            List_AddRow(list, row);
            List_Refresh(list);
            return 1;
        }
        case MessageType_AddItem:
        case MessageType_AddItemLabel: {
            char *text = message == MessageType_AddItemLabel ? (char *)Txt_BindLabel((Action_TranslatedText)arg1, 0)
                                                              : (char *)arg1;
            LIST_ROW *row = List_NewRow();
            for (int i = 0; i < LIST_COLUMNS; i++) {
                row->cells[i].text[0] = '\0';
                row->cells[i].icon = (HASHCODE)0;
            }
            row->value = arg2;
            row->stripe = NULL;
            row->continued = 0;
            List_AddRow(list, row);
            if (text != NULL)
                List_SendMessage(list, MessageType_ListSetCellText, (list->rows.count - 1) << 16, (intptr_t)text);
            List_Refresh(list);
            return list->rows.count - 1;
        }
        case MessageType_ListRemoveItem: {
            LIST_ROW *row = List_SetIndex(list, arg1);
            if (row == NULL)
                return 0;
            if (row == list->selected)
                list->selected = NULL;
            LinkedList_Remove(&list->rows, &row->prev, row);
            Menu_Free((void **)&row, 2);
            Scroll_SendMessage(&list->scroll, MessageType_SetValue, 0, 0);
            Scroll_SendMessage(&list->scroll, MessageType_ScrollAddToMax, -1, 0);
            return 1;
        }
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&list->control);
        case MessageType_ScriptPlay:
            return Script_Play(&list->control, arg1, (char)arg2);
        case MessageType_ClearItems: {
            for (LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows); row != NULL; row = row->next) {
                if (row->stripe != NULL)
                    Menu_DeleteSprite(row->stripe);
                row->stripe = NULL;
            }
            for (ushort i = 0; i < ((ushort)list->visibleRows < (ushort)list->rows.count ? (ushort)list->visibleRows
                                                                                          : (ushort)list->rows.count);
                 i++) {
                for (int j = 0; j < LIST_COLUMNS; j++) {
                    if (list->cells[i][j] != NULL)
                        Menu_DeleteSprite(list->cells[i][j]);
                }
            }
            memset(list->cells, 0, sizeof(list->cells));
            if (list->rows.count != 0) {
                LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows);
                while (row != NULL) {
                    LIST_ROW *next = row->next;
                    LinkedList_Remove(&list->rows, &row->prev, row);
                    Menu_Free((void **)&row, 2);
                    row = next;
                }
            }
            memset(&list->rows, 0, sizeof(list->rows));
            list->selected = NULL;
            list->topRow = 0;
            list->visibleRows = 0;
            list->numColumns = 1;
            list->columnWidth[0] = 100;
            list->columnAlign[0] = 1;
            list->continuedRows = 0;
            list->selectedIndex = 0;
            Scroll_SendMessage(&list->scroll, MessageType_SetValue, 0, 0);
            Scroll_SendMessage(&list->scroll, MessageType_ScrollSetMax, 0, 0);
            return 1;
        }
        case MessageType_ListSetCellText: {
            // Of the row and the column in arg1, counting rows from those a text began on
            uint rowIndex = (uint)arg1 >> 16;
            LIST_ROW *row = List_SetIndex(list, list->continuedRows + rowIndex);
            if (row == NULL)
                return 0;
            int column = arg1 & 0xffff;
            if (column >= list->numColumns)
                return 0;
            char *text = (char *)arg2;
            GPOINT scale;
            scale.x = 1.0f;
            scale.y = 1.0f;
            int width = Ftol(list->columnWidth[column] * (double)0.01f * (list->control.width * 0.9));
            uint fits = Font_GetTextCountExtent(text, list->format, &scale, (float)width);
            uint length = strlen(text);
            if (strstr(text, "~") != NULL)
                length--;
            if (fits < length && list->wrap) {
                // Cut at the last space before the end of what fits, and the rest onto a row of its own
                uint cut = fits;
                for (uint i = fits; i != 0; i--) {
                    if (text[i] == ' ') {
                        cut = i;
                        break;
                    }
                }
                if (length == 0)
                    return 1;
                memcpy(row->cells[column].text, text, cut);
                row->cells[column].text[cut] = '\0';
                List_SendMessage(list, MessageType_AddItem, (intptr_t)MenuSpaceString, 0);
                List_SetIndex(list, list->continuedRows + rowIndex + 1)->continued = 1;
                List_SendMessage(list, MessageType_ListSetCellText, ((arg1 >> 16) + 1) << 16 | column,
                                 (intptr_t)(text + cut + 1));
                list->continuedRows++;
            } else if (length != 0) {
                strcpy(row->cells[column].text, text);
            }
            return 1;
        }
        case MessageType_SetHighlightColour:
            list->highlightColour = arg1;
            return 1;
        case MessageType_SetNormalColour:
            list->normalColour = arg1;
            return 1;
        case MessageType_SelectIndex:
            // The scroll goes to the row selected before, then the row a text began on at the index is selected
            if (!Scroll_SendMessage(&list->scroll, MessageType_SetValue, List_GetIndex(list, list->selected), 0))
                return 0;
            list->selected = List_SetIndex_RealItem(list, &list->rows, arg1);
            list->selectedIndex = (uchar)List_GetIndex(list, list->selected);
            List_ShowSelected(list);
            return __Menu_SendMessage(list->control.parent, MessageType_Scroll, (intptr_t)list, 0);
        case MessageType_SelectItemByValue:
            for (LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows); row != NULL; row = row->next) {
                if (row->value == arg1) {
                    list->selected = row;
                    Scroll_SendMessage(&list->scroll, MessageType_SetValue, List_GetIndex(list, list->selected), 0);
                    if (arg2 != 0)
                        return 1;
                    return __Menu_SendMessage(list->control.parent, MessageType_Scroll, (intptr_t)list, 0);
                }
            }
            return 0;
        case MessageType_ListSetCurrentItemValue:
            if (list->selected == NULL)
                return 0;
            list->selected->value = arg1;
            return 1;
        case MessageType_SetScript:
            return Control_SetScript(&list->control, arg1, arg2);
        case MessageType_SetFont: {
            if (arg1 == 1)
                list->format = ListFormat1;
            else if (arg1 == 3)
                list->format = ListFormat3;
            else if (arg1 == 2)
                list->format = ListFormat2;
            GPOINT extent;
            Font_GetTextExtent(MenuAlphabet, list->format, &extent, NULL, -1);
            list->rowHeight = extent.y * (double)1.1f;
            strcpy(list->selectedFormat, list->format);
            return 1;
        }
        case MessageType_ListSetColumnWidth:
            if (arg1 >= list->numColumns)
                return 0;
            list->columnWidth[arg1] = arg2 > 100 ? 100 : arg2;
            return 1;
        case MessageType_SetPulseColour:
            list->pulseColour = arg1;
            return 1;
        case MessageType_ListSetOption564:
            list->highlightMode = (uchar)arg1;
            return 0;
        case MessageType_SetState: {
            list->control.state = (uchar)arg1;
            Scroll_SendMessage(&list->scroll, MessageType_SetState, arg1, arg2);
            if (list->control.state & CONTROL_STATE_HIDDEN) {
                Component_SetupInstance(&list->control, 0, -1, -1, -1, -1);
                Component_SetupInstance(&list->highlight.control, 0, -1, -1, -1, -1);
            }
            List_Refresh(list);
            return 1;
        }
        case MessageType_ListSetTopRow:
            if ((uint)arg1 <= list->rows.count - list->visibleRows && arg1 >= 0)
                list->topRow = arg1;
            return 1;
        case MessageType_GetHighlightColour:
            return list->highlightColour;
        case MessageType_GetNormalColour:
            return list->normalColour;
        case MessageType_GetNumItems:
            return list->rows.count;
        case MessageType_GetSelectedIndex:
            return List_GetIndex_RealItem(list, &list->rows, list->selected);
        case MessageType_GetSelectedItemValue:
            if (list->selected == NULL)
                return 0;
            return list->selected->value;
        case MessageType_GetControl:
            return (intptr_t)list;
        case MessageType_ListGetTopRow:
            return list->topRow;
        case MessageType_GetId:
            return list->control.id;
        case MessageType_GetVisibleRows:
            return list->visibleRows;
        case MessageType_Destroy:
            Component_Free(&list->control);
            Component_Free(&list->highlight.control);
            Scroll_SendMessage(&list->scroll, MessageType_Destroy, 0, arg2);
            List_SendMessage(list, MessageType_ClearItems, 0, 0);
            Script_DelAllScripts(&list->control);
            if (list->control.parent->type == ControlType_Page)
                Menu_Free((void **)&list, 3);
            return 0;
        case MessageType_Scroll:
        case MessageType_MemoBack: {
            // The scroll's value selects a row; on a wrapped row it moves on, the way it was going (back from the
            // end), to one a text began on
            int value = Scroll_SendMessage(&list->scroll, MessageType_GetValue, 0, 0);
            int max = Scroll_SendMessage(&list->scroll, MessageType_GetMax, 0, 0);
            list->selected = NULL;
            if (list->rows.count != 0) {
                int step = list->selectedIndex < value ? 1 : -1;
                list->selected = List_SetIndex(list, value);
                while (list->selected->continued) {
                    if (step + value > max)
                        step = -(step < 0 ? -step : step);
                    value += step;
                    list->selected = List_SetIndex(list, value);
                }
            }
            list->selectedIndex = (uchar)value;
            List_ShowSelected(list);
            __Menu_SendMessage(list->control.parent, message, (intptr_t)list, 0);
            return 1;
        }
        case MessageType_Select:
        case MessageType_AltSelect1:
        case MessageType_AltSelect2:
        case MessageType_BackOnControl:
            return __Menu_SendMessage(list->control.parent, message, (intptr_t)list, 0);
        case MessageType_ValueSet: {
            int value = Scroll_SendMessage(&list->scroll, MessageType_GetValue, 0, 0);
            if (value >= 0)
                List_SetIndex(list, value);
            return 1;
        }
        case MessageType_SetAlpha:
            list->alpha = arg1;
            return 1;
        case MessageType_ListSetColumnAlign:
            if (arg1 >= list->numColumns)
                return 0;
            list->columnAlign[arg1] = arg2;
            return 1;
        case MessageType_SetTextFlag2000:
            list->textFlag2000 = (uchar)arg1;
            return 1;
        case MessageType_SetPulse:
            list->pulse = (uchar)arg1;
            return 0;
        case MessageType_ListSetSelectionColour:
            list->stripeColour = arg1;
            return 0;
        case MessageType_SetLineSpacing: {
            GPOINT extent;
            Font_GetTextExtent(MenuAlphabet, list->format, &extent, NULL, -1);
            list->rowHeight = (float)(arg1 * (double)0.01f * extent.y);
            // The row's height over the list's, the other way round from List_Update's
            list->visibleRows = Ftol(list->rowHeight / (double)list->control.height);
            return 0;
        }
    }
    return 0;
}

// The rows with sprites
static ushort List_SpriteRows(M_LIST *list) {
    ushort visible = (ushort)list->visibleRows;
    ushort count = (ushort)list->rows.count;
    return visible < count ? visible : count;
}

// A visible row's sprites: its stripe (every other row) and its cells, each in its column's share of the list less the
// bar, by its alignment; the selected row (and the rows its text wraps onto) in the pulse colour, with the bar under it
static void List_DrawRow(M_PAGE *page, M_PAGE *own, M_LIST *list, LIST_ROW *row, int rowNum, LIST_ROW *lastBegun,
                         sprite **slot) {
    if (list->stripeColour != 0 && rowNum % 2 == 0) {
        if (row->stripe == NULL)
            row->stripe = Menu_CreateSprite(NULL);
        short y = Ftol(Component_GetHandle(&list->control)->top + (rowNum - list->topRow) * (double)list->rowHeight +
                       list->control.y + 3.0);
        Menu_InitializeSprite(row->stripe, 0x28, own->control.x + list->control.x, y + own->control.y,
                              list->control.width, Ftol(list->rowHeight), NULL, STRIPE_TEXTURE, list->stripeColour,
                              list->stripeColour, 0x26, 0x13, 1, 1, 0);
    }

    for (short column = 0; column < list->numColumns; column++) {
        int left = 0;
        if (column > 0) {
            double space = list->control.width - LIST_BAR_WIDTH;
            for (int i = 0; i < column; i++)
                left = Ftol(list->columnWidth[i] * (double)0.01f * space + (ushort)left);
        }
        sprite **cell = &slot[column];
        if (*cell == NULL)
            *cell = Menu_CreateSprite(NULL);
        // The original has an icon for a cell without text, but tests the text's address, which is never NULL
        char *text = row->cells[column].text;
        short textY = Ftol(list->rowHeight);
        GPOINT extent = {0.0f, 0.0f};
        GPOINT scale = {0.0f, 0.0f};
        Font_GetTextExtent(text, list->format, &extent, &scale, strlen(text));
        short height = Ftol(extent.y);

        (*cell)->maybeClippedString = list->format;
        Menu_ClipString(*cell, Ftol(list->columnWidth[column] * (double)0.01f * list->control.width - 16.0),
                        list->format);
        ushort flags = list->textFlag2000 ? 0x2000 : 0;
        uint colour = page->focused->hashcode == list->control.hashcode ? list->highlightColour : list->normalColour;
        short width = Ftol(list->columnWidth[column] * (double)0.01f * list->control.width);
        short y = Ftol(Component_GetHandle(&list->control)->top + (rowNum - list->topRow) * (double)list->rowHeight +
                       list->control.y + textY) +
                  own->control.y;
        short x = Component_GetHandle(&list->control)->left + own->control.x + list->control.x + left + 2;
        Menu_InitializeSprite(*cell, (uchar)(list->control.depth - 7), x, y, width, height, text, (HASHCODE)0, colour,
                              list->alpha, 0, 0, 0, 0, flags);

        sprite *spr = *cell;
        spr->maybeClippedString = list->format;
        short columnWidth = Ftol(list->columnWidth[column] * (double)0.01f * list->control.width);
        GPOINT size;
        if (row == list->selected && !(list->control.state & CONTROL_STATE_INERT))
            Font_GetTextExtent(text, list->selectedFormat, &size, &spr->scale, -1);
        else
            Font_GetTextExtent(text, list->format, &size, &spr->scale, -1);
        spr->onscreenWidth = Ftol(size.x);
        if (list->columnAlign[column] == ALIGN_RIGHT)
            spr->positionX = Ftol((double)(spr->positionX + columnWidth) - size.x);
        else if (list->columnAlign[column] == ALIGN_CENTRE)
            spr->positionX = Ftol(((double)columnWidth - size.x) * (double)0.5f + spr->positionX);
    }

    if ((row == list->selected && !(list->control.state & CONTROL_STATE_INERT)) ||
        (lastBegun == list->selected && row->continued)) {
        if (list->highlightMode == LIST_HIGHLIGHT_COLOUR || list->highlightMode == LIST_HIGHLIGHT_BAR) {
            for (int i = 0; i < list->numColumns; i++) {
                if (list->pulse)
                    slot[i]->colourTint = PulsedColour(list->pulseColour, Menu_AlphaGet());
                else
                    slot[i]->colourTint = list->pulseColour;
            }
        }
        if (list->highlightMode == LIST_HIGHLIGHT_BAR && !row->continued) {
            M_CONTROL *bar = &list->highlight.control;
            int height = Ftol(Component_GetHandle(bar)->bottom + (double)list->rowHeight);
            int width = list->control.width - (list->visibleRows > 1 ? LIST_BAR_WIDTH : 0) -
                        Component_GetHandle(bar)->right - Component_GetHandle(bar)->left;
            int y = Ftol(Component_GetHandle(bar)->top + (rowNum - list->topRow) * (double)list->rowHeight +
                         list->control.y);
            int x = Component_GetHandle(bar)->left + list->control.x;
            Component_SetupInstance(bar, list->control.state & CONTROL_STATE_HIDDEN ? 0 : list->control.drawState, x,
                                    y, width, height);
        }
    }
}

// Hidden (or its owner), its sprites are hidden. Else the bar at the right is shown only while more than one row is
// visible, the rows visible follow the selection, the visible rows are drawn and the others' sprites hidden.
// AUTOINJECT
void List_Update(M_MANAGER *mgr, M_PAGE *page, M_LIST *list) {
    M_PAGE *own = Menu_GetPage(&list->control);
    if ((list->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (list->control.state & CONTROL_STATE_HIDDEN)) {
        list->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&list->control);
        Component_Free(&list->highlight.control);
        Scroll_Update(mgr, page, &list->scroll);
        for (LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows); row != NULL; row = row->next) {
            if (row->stripe != NULL)
                row->stripe->maybeEnabled = MENU_SPRITE_HIDDEN;
        }
        for (ushort i = 0; i < List_SpriteRows(list); i++) {
            for (int j = 0; j < LIST_COLUMNS; j++) {
                if (list->cells[i][j] != NULL)
                    list->cells[i][j]->maybeEnabled = MENU_SPRITE_HIDDEN;
            }
        }
        return;
    }
    list->control.state &= ~CONTROL_STATE_UNDRAWN;
    if (list->control.width <= LIST_BAR_WIDTH)
        Component_SetupInstance(&list->highlight.control, 0, -1, -1, -1, -1);
    Menu_ResizeControl(&list->scroll.control, list->control.width + list->control.x - LIST_BAR_WIDTH, list->control.y,
                       LIST_BAR_WIDTH, list->control.height);
    int height = list->control.height;
    list->visibleRows = Ftol(height / (double)list->rowHeight);
    if (list->topRow + list->visibleRows > (int)list->rows.count && list->rows.count > (uint)list->visibleRows)
        list->topRow = list->rows.count - list->visibleRows;
    list->control.drawState = page->focused->hashcode == list->control.hashcode ? 0x20 : 0x10;

    uchar inert = list->control.state & CONTROL_STATE_INERT;
    if (!inert && list->visibleRows > 1 && (list->scroll.control.state & CONTROL_STATE_HIDDEN)) {
        list->scroll.control.width = LIST_BAR_WIDTH;
        Component_SetupInstance(&list->control, list->control.drawState, -1, -1, list->control.width - LIST_BAR_WIDTH,
                                height);
    } else if ((inert || list->visibleRows == 1) && !(list->scroll.control.state & CONTROL_STATE_HIDDEN)) {
        list->scroll.control.width = -1;
        Component_SetupInstance(&list->control, list->control.drawState, -1, -1, list->control.width, height);
    }

    if (list->selected != NULL) {
        int index = List_GetIndex(list, list->selected);
        if (index >= list->topRow + list->visibleRows)
            list->topRow = index + 1 - list->visibleRows;
        else if (index < list->topRow)
            list->topRow = index;
    }

    if (list->rows.count != 0) {
        int rowNum = 0;
        LIST_ROW *lastBegun = NULL;
        sprite **slot = list->cells[0];
        for (LIST_ROW *row = (LIST_ROW *)LinkedList_First(&list->rows); row != NULL; row = row->next) {
            if (rowNum >= list->topRow && rowNum < list->topRow + list->visibleRows) {
                List_DrawRow(page, own, list, row, rowNum, lastBegun, slot);
                slot += LIST_COLUMNS;
            } else {
                // The sprites of the next visible row's place
                for (int i = 0; i < list->numColumns; i++) {
                    if (slot[i] != NULL)
                        slot[i]->maybeEnabled = MENU_SPRITE_HIDDEN;
                }
            }
            rowNum++;
            if (!row->continued)
                lastBegun = row;
        }
    }

    int bar = list->visibleRows > 1 && !(list->control.state & CONTROL_STATE_INERT) ? LIST_BAR_WIDTH : 0;
    Component_SetupInstance(&list->control, list->control.drawState, -1, -1, list->control.width - bar,
                            list->control.height);
    Scroll_Update(mgr, page, &list->scroll);
}
