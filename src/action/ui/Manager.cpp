// A menu's manager: its pages, the page shown and the page history, and the messages no control handles (PS2:
// menu_manager.cpp)

#include "Manager.h"
#include "../../common/xbeClass.h"
#include "MenuCommon.h"
#include "Button.h"
#include "Checkbox.h"
#include "Combo.h"
#include "Label.h"
#include "List.h"
#include "Memo.h"
#include "Page.h"
#include "Radio.h"
#include "Scroll.h"
#include "Spin.h"
#include "TextBox.h"
#include "Window.h"
#include "Component.h"

#include "../game.h"
#include "../input.h"
#include "Menu.h"
#include "../devtools/MenuProbe.h"

// XBE_GLOBAL(0x0025f1d0, 0x938)
M_MANAGER manager[5];

// The history's entries: a page left and its focused control
#pragma pack(push, 1)
typedef struct PAGE_HISTORY {
    M_PAGE *page;
    M_CONTROL *focused;
} PAGE_HISTORY;
#pragma pack(pop)
LAYOUT_32(sizeof(PAGE_HISTORY) == 8, "PAGE_HISTORY is 8 bytes");

// What MessageType_QueryBack's answer is set to by a handler that keeps the page
#define BACK_REFUSED 0xfffffffe

static M_PAGE* Manager_NextPage(M_PAGE *page) {
    return (M_PAGE *)((char *)page->pageLink[0] - offsetof(M_PAGE, pageLink));
}

// A control from the menu data, put on the page shown; NULL if the page did not take it
static M_CONTROL* Manager_CreateObject(M_MANAGER *mgr, NEW_CONTROL *create) {
    M_CONTROL *control;
    switch (create->type) {
        case ControlType_Button: {
            M_BUTTON *button = (M_BUTTON *)Menu_Malloc(sizeof(M_BUTTON));
            Button_Init(button, create, &mgr->currentPage->control);
            control = &button->control;
            break;
        }
        case ControlType_Checkbox: {
            M_CHECKBOX *checkbox = (M_CHECKBOX *)Menu_Malloc(sizeof(M_CHECKBOX));
            Checkbox_Init(checkbox, create, &mgr->currentPage->control);
            control = &checkbox->control;
            break;
        }
        case ControlType_Combo: {
            M_COMBO *combo = (M_COMBO *)Menu_Malloc(sizeof(M_COMBO));
            Combo_Init(combo, create, &mgr->currentPage->control);
            control = &combo->control;
            break;
        }
        case ControlType_Label: {
            M_LABEL *label = (M_LABEL *)Menu_Malloc(sizeof(M_LABEL));
            Label_Init(label, create, &mgr->currentPage->control);
            control = &label->control;
            break;
        }
        case ControlType_List: {
            M_LIST *list = (M_LIST *)Menu_Malloc(sizeof(M_LIST));
            List_Init(list, create, &mgr->currentPage->control);
            control = &list->control;
            break;
        }
        case ControlType_Memo: {
            M_MEMO *memo = (M_MEMO *)Menu_Malloc(sizeof(M_MEMO));
            Memo_Init(memo, create, &mgr->currentPage->control);
            control = &memo->control;
            break;
        }
        case ControlType_Radio: {
            M_RADIO *radio = (M_RADIO *)Menu_Malloc(sizeof(M_RADIO));
            Radio_Init(radio, create, &mgr->currentPage->control);
            control = &radio->control;
            break;
        }
        case ControlType_Scroll: {
            M_SCROLL *scroll = (M_SCROLL *)Menu_Malloc(sizeof(M_SCROLL));
            Scroll_Init(scroll, create, &mgr->currentPage->control);
            control = &scroll->control;
            break;
        }
        case ControlType_Spin: {
            M_SPIN *spin = (M_SPIN *)Menu_Malloc(sizeof(M_SPIN));
            Spin_Init(spin, create, &mgr->currentPage->control);
            control = &spin->control;
            break;
        }
        case ControlType_Text: {
            M_TEXTBOX *box = (M_TEXTBOX *)Menu_Malloc(sizeof(M_TEXTBOX));
            Text_Init(box, create, &mgr->currentPage->control);
            control = &box->control;
            break;
        }
        case ControlType_Window: {
            M_WINDOW *window = (M_WINDOW *)Menu_Malloc(sizeof(M_WINDOW));
            Window_Init(window, create, mgr);
            control = &window->control;
            break;
        }
        default:
            return NULL;
    }
    if (!__Menu_SendMessage(&mgr->currentPage->control, MessageType_CreateObject, (intptr_t)control, 0))
        return NULL;
    return control;
}

// To the page of the hashcode, leaving the page shown (onto the history, or under the new one as an overlay); the
// other pages, hidden, lose their sprites. The new page's background movie starts, and its window plays its entry
// script, or else is put in place.
static void Manager_GoPage(M_MANAGER *mgr, HASHCODE hashcode, int flags) {
    M_PAGE *page = (M_PAGE *)LinkedList_First(&mgr->pages);
    HASHCODE previous = (HASHCODE)0;
    Input_ClearAllActions(-1);
    while (page != NULL) {
        if (page->control.hashcode == hashcode) {
            Menu_EnableSounds(page->control.hashcode);
            if (!(flags & GOPAGE_NO_HISTORY)) {
                PAGE_HISTORY *entry = (PAGE_HISTORY *)Menu_Malloc(sizeof(PAGE_HISTORY));
                entry->page = mgr->currentPage;
                entry->focused = mgr->currentPage->focused;
                Stack_Push(&mgr->stack, entry);
            }
            if (hashcode == P_MAIN && !(flags & GOPAGE_KEEP_HISTORY))
                Menu_ClearStack(mgr);
            if (mgr->underPage != NULL) {
                mgr->underPage->control.state = CONTROL_STATE_HIDDEN;
                Page_Update(mgr, mgr->underPage, 0);
            }
            if (mgr->currentPage != NULL) {
                previous = mgr->currentPage->control.hashcode;
                mgr->currentPage->control.state = CONTROL_STATE_HIDDEN;
                Page_Update(mgr, mgr->currentPage, 0);
                if (flags & GOPAGE_OVERLAY) {
                    mgr->underPage = mgr->currentPage;
                    mgr->overlayMode = true;
                    Menu_ClearDelayedMessages();
                } else {
                    if (!(flags & GOPAGE_QUIET))
                        Manager_SendMessage(mgr, MessageType_PageLeave, (intptr_t)mgr->currentPage, 0);
                    mgr->underPage = NULL;
                    mgr->overlayMode = false;
                    Manager_SendMessage(mgr, MessageType_LockInput, 0, 0);
                }
            }
            mgr->currentPage = page;
            page->control.depth = 5;
            mgr->currentPage->control.state = CONTROL_STATE_SHOWN;
            mgr->currentPage->framesShown = 0;
            if (mgr->currentMovie != mgr->currentPage->backgroundMovie && !(flags & GOPAGE_OVERLAY)) {
                if (mgr->currentPage->backgroundMovie == 0)
                    psiStopBackgroundMovie();
                else
                    Menu_PlayMovie(mgr->currentPage->backgroundMovie, 0, 1, 0, 0, 0);
                mgr->currentMovie = mgr->currentPage->backgroundMovie;
            }
            if (!(flags & GOPAGE_QUIET)) {
                Manager_SendMessage(mgr, MessageType_PageEnter, (intptr_t)mgr->currentPage, previous);
                if (!Script_PlayDefault(&mgr->currentPage->control, MessageType_PageEnter)) {
                    M_CONTROL *window = Window_Get(mgr->currentPage);
                    Script_Stop(window);
                    window->x = 0;
                    window->y = 0;
                    Page_Update(mgr, mgr->currentPage, 0);
                }
            } else if (!(flags & GOPAGE_NO_RESUME)) {
                Manager_SendMessage(mgr, MessageType_PageResume, (intptr_t)mgr->currentPage, previous);
            }
            if (mgr->underPage != NULL) {
                if (flags & GOPAGE_OVERLAY) {
                    mgr->underPage->control.depth = 0xfffb;
                    mgr->underPage->control.state = CONTROL_STATE_INERT;
                    Page_Update(mgr, mgr->underPage, 0);
                } else if (!(flags & GOPAGE_QUIET)) {
                    Manager_SendMessage(mgr, MessageType_PageLeave, (intptr_t)mgr->underPage, flags);
                    if (!Script_PlayDefault(&mgr->underPage->control, MessageType_PageLeave)) {
                        M_CONTROL *window = Window_Get(mgr->underPage);
                        Script_Stop(window);
                        window->y = 2000;
                        window->x = 2000;
                        Page_Update(mgr, mgr->underPage, 0);
                    }
                }
            }
        } else if (page != mgr->currentPage && page != mgr->underPage && page != mgr->sidePage) {
            page->control.state = CONTROL_STATE_HIDDEN;
            page->control.depth = 0;
            if (!(flags & GOPAGE_QUIET))
                Page_Update(mgr, page, 0);
            for (M_CONTROL *c = (M_CONTROL *)LinkedList_Last(&page->controls); c != NULL; c = c->prev) {
                Component_Free(c);
                if (c->type == ControlType_Label && ((M_LABEL *)c)->drawn != NULL) {
                    Menu_DeleteSprite(((M_LABEL *)c)->drawn);
                    ((M_LABEL *)c)->drawn = NULL;
                }
            }
        }
        if (page->pageLink[0] == NULL)
            break;
        page = Manager_NextPage(page);
    }
    Page_Update(mgr, mgr->currentPage, 0);
}

// The page beside the one shown (none for 0): the one before leaves, the new one enters
static void Manager_SetSidePage(M_MANAGER *mgr, HASHCODE hashcode) {
    if (hashcode == 0) {
        mgr->sidePage = NULL;
        return;
    }
    M_PAGE *page = (M_PAGE *)LinkedList_First(&mgr->pages);
    if (page == NULL)
        return;
    while (page->control.hashcode != hashcode) {
        if (page->pageLink[0] == NULL)
            return;
        page = Manager_NextPage(page);
    }
    // The page entering is told it leaves, and plays its leaving script
    if (mgr->sidePage != NULL) {
        mgr->sidePage->control.state = CONTROL_STATE_HIDDEN;
        mgr->sidePage->control.depth = 0;
        Manager_SendMessage(mgr, MessageType_PageLeave, (intptr_t)page, 0);
        Script_PlayDefault(&page->control, MessageType_PageLeave);
        Page_Update(mgr, mgr->sidePage, 1);
    }
    mgr->sidePage = page;
    page->control.state = CONTROL_STATE_SHOWN;
    mgr->sidePage->control.depth = 0xfff6;
    Manager_SendMessage(mgr, MessageType_PageEnter, (intptr_t)page, 0);
    Script_PlayDefault(&page->control, MessageType_PageEnter);
}

// A player's focus, on the control of the hashcode and id (the player) on the page shown or the one under it
static int Manager_SetPlayerFocus(M_MANAGER *mgr, HASHCODE hashcode, int player) {
    Input_ClearAllActions(-1);
    M_PAGE *pages[2] = {mgr->currentPage, mgr->underPage};
    for (uint i = 0; i < 2; i++) {
        M_PAGE *page = pages[i];
        if (page == NULL)
            continue;
        for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
            if (hashcode != 0 && c->hashcode == hashcode && c->id == player) {
                if (page->playerFocus[player] != NULL)
                    Manager_SendMessage(mgr, MessageType_PlayerFocusLost, (intptr_t)page->playerFocus[player], 0);
                page->playerFocus[player] = c;
                Manager_SendMessage(mgr, MessageType_PlayerFocusGained, (intptr_t)c, 0);
                return 1;
            }
        }
    }
    return 0;
}

// Back to the page before (as an overlay if the page shown is one), unless a handler refuses; with none, the page's
// handler hears it is at the root
static int Manager_Back(M_MANAGER *mgr, int forced, int player) {
    M_PAGE *page = mgr->currentPage;
    if (!forced) {
        int answer = player;
        if (page->playerFocus[player] != NULL) {
            Manager_SendMessage(mgr, MessageType_QueryBack, (intptr_t)page->playerFocus[player], (intptr_t)&answer);
            if (answer == (int)BACK_REFUSED)
                return 1;
        }
        Manager_SendMessage(mgr, MessageType_QueryBack, (intptr_t)page, (intptr_t)&answer);
        if (answer == (int)BACK_REFUSED)
            return 1;
    }
    if (Stack_IsEmpty(&mgr->stack)) {
        Manager_SendMessage(mgr, MessageType_BackAtRoot, (intptr_t)page, page->control.hashcode);
        return 1;
    }
    PAGE_HISTORY *entry = (PAGE_HISTORY *)Stack_Top(&mgr->stack);
    if (page == NULL || !Script_PlayDefault(&page->control, MessageType_BackAtRoot)) {
        __Menu_SendMessage(&mgr->control, MessageType_GoPage, entry->page->control.hashcode,
                           GOPAGE_NO_HISTORY | GOPAGE_KEEP_HISTORY | (mgr->overlayMode ? GOPAGE_QUIET : 0));
        __Menu_SendMessage(&mgr->control, MessageType_SetCursor, 0, (intptr_t)entry->focused);
    }
    Stack_Pop(&mgr->stack);
    Menu_Free((void **)&entry, 8);
    return 1;
}

// The cursor to the middle of a control of the page shown that can take the focus: by hashcode, or (arg1 0) the
// control itself
static int Manager_SetCursor(M_MANAGER *mgr, HASHCODE hashcode, M_CONTROL *control) {
    M_PAGE *page = mgr->currentPage;
    if (page == NULL)
        return 0;
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
        bool match;
        if (hashcode != 0)
            match = c->hashcode == hashcode;
        else if (control != NULL)
            match = c == control;
        else
            continue;
        if (match && c->state != CONTROL_STATE_INERT) {
            mgr->cursorSprite->positionX = (c->width >> 1) + c->x + page->control.x;
            mgr->cursorSprite->positionY = (c->height >> 1) + c->y + mgr->currentPage->control.y;
            mgr->currentPage->focused = c;
            return 1;
        }
    }
    return 0;
}

// The messages to the manager, and those no control or page handled: the handlers get them, and for some a control
// plays its default script; one the handlers leave goes on to the page shown
// AUTOINJECT
intptr_t Manager_SendMessage(M_MANAGER *mgr, MessageType message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_CreateObject:
            return (intptr_t)Manager_CreateObject(mgr, (NEW_CONTROL *)arg1);
        case MessageType_CreatePage: {
            M_PAGE *page = (M_PAGE *)Menu_Malloc(sizeof(M_PAGE));
            Page_Init(page, (HASHCODE)arg1, mgr);
            LinkedList_Add(&mgr->pages, &page->pageLink[1], page->pageLink);
            __Menu_SendMessage(&mgr->control, MessageType_GoPage, arg1,
                               GOPAGE_NO_HISTORY | GOPAGE_QUIET | GOPAGE_NO_RESUME);
            return (intptr_t)page;
        }
        case MessageType_GoPage:
            Manager_GoPage(mgr, (HASHCODE)arg1, arg2);
            return 1;
        case MessageType_SetSidePage:
            Manager_SetSidePage(mgr, (HASHCODE)arg1);
            return 1;
        case MessageType_Back:
            return Manager_Back(mgr, arg1, arg2);
        case MessageType_Destroy: {
            // Each page's next is read from the page just freed
            M_PAGE *page = (M_PAGE *)LinkedList_First(&mgr->pages);
            while (page != NULL) {
                __Menu_SendMessage(&page->control, MessageType_Destroy, 0, 0);
                if (page->pageLink[0] == NULL)
                    break;
                page = Manager_NextPage(page);
            }
            LinkedList_ReInit(&mgr->pages);
            Menu_DeleteSprite(mgr->cursorSprite);
            Component_DestroyAll(mgr->index);
            mgr->currentPage = NULL;
            mgr->underPage = NULL;
            mgr->sidePage = NULL;
            Menu_ClearStack(mgr);
            return 0;
        }
        case MessageType_GetCurrentPage:
            if (!mgr->overlayMode) {
                if (mgr->currentPage != NULL)
                    return mgr->currentPage->control.hashcode;
            } else if (mgr->underPage != NULL) {
                return mgr->underPage->control.hashcode;
            }
            return 0;
        case MessageType_SetCursor:
            return Manager_SetCursor(mgr, (HASHCODE)arg1, (M_CONTROL *)arg2);
        case MessageType_SetPlayerFocus:
            if (mgr->currentPage == NULL)
                return 0;
            return Manager_SetPlayerFocus(mgr, (HASHCODE)arg1, arg2);
        case MessageType_LockInput:
            mgr->inputLocked = (uchar)arg1;
            Input_ClearAllActions(-1);
            return 1;
        case MessageType_SetPageOption:
            if (mgr->currentPage == NULL)
                return 0;
            if (arg1 == 1)
                mgr->currentPage->navigationMode = arg2;
            return 1;
        default: {
            if (mgr->currentPage == NULL)
                return 0;
            // Handler_HandleMessage, or the menu message log in front of it
            bool handled = MenuProbe_HandleMessage(mgr->index, (M_CONTROL *)arg1, message, arg1, arg2);
            switch (message) {
                case MessageType_Scroll:
                case MessageType_TextChanged:
                case MessageType_Select:
                case MessageType_GainFocus:
                case MessageType_LoseFocus:
                case MessageType_MemoBack:
                case MessageType_TextEndEntry:
                    if (((M_CONTROL *)arg1)->type == ControlType_Page)
                        return 1;
                    return (char)Script_PlayDefault((M_CONTROL *)arg1, message);
                default:
                    if (handled)
                        return 0;
                    return __Menu_SendMessage(&mgr->currentPage->control, message, arg1, 1);
            }
        }
    }
}
