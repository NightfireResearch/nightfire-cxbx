// The menus' pages: the controls they hold, their frame, and the help line a page may show (PS2: menu_manager.cpp)

#include "Page.h"
#include "MenuCommon.h"

#include "../engine/Text.h"

// A help line: the text the help memo shows while a control has the focus (with a value of its, or any)
#pragma pack(push, 1)
typedef struct HELP_TEXT {
    HASHCODE control;           // 0x00
    int value;                  // 0x04 MessageType_GetSelectedItemValue's, or HELP_ANY_VALUE
    Action_TranslatedText text; // 0x08
} HELP_TEXT;
#pragma pack(pop)
static_assert(sizeof(HELP_TEXT) == 0xc, "HELP_TEXT is 0xc bytes");

#define help_texts ((HELP_TEXT *)0x0017d640)
#define HELP_TEXTS 130
#define HELP_ANY_VALUE ((int)0xf0f0f0f0)

// The memo of the page shown that the help lines go to
#define help_control (*(M_CONTROL **)0x0025fc18)
#define HELP_MEMO ((HASHCODE)0x10000225)

// AUTOINJECT
void Page_Init(M_PAGE *page, HASHCODE hashcode, M_MANAGER *mgr) {
    page->control.hashcode = hashcode;
    page->control.type = ControlType_Page;
    page->control.depth = 0;
    page->control.id = 0;
    page->control.parent = &mgr->control;
    page->control.x = 0;
    page->control.y = 0;
    page->control.width = SCREEN_WIDTH;
    page->control.height = SCREEN_HEIGHT;
    page->control.state = CONTROL_STATE_SHOWN;
    page->focused = NULL;
    page->navigationMode = 1;
    page->backgroundMovie = (HASHCODE)0;
    page->framesShown = 0;
    page->inputDelay = 0;
    for (int i = 0; i < 4; i++)
        page->playerFocus[i] = NULL;
    LinkedList_Init(&page->controls, 0);
    Script_PrepareScripting(&page->control);
}

// AUTOINJECT
int Page_SendMessage(M_PAGE *page, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_CreateObject: {
            M_CONTROL *control = (M_CONTROL *)arg1;
            LinkedList_Add(&page->controls, &control->prev, control);
            return 1;
        }
        // A control's selection goes to the page's handler first; then, as for the rest, the control plays its default
        // script, and (unless arg2 says it has been) the control's handler hears of it
        case MessageType_Select:
        case MessageType_AltSelect1:
        case MessageType_AltSelect2:
        case MessageType_BackOnControl:
            if (arg2 == 0)
                __Menu_SendMessage(page->control.parent, message, (int)page, arg1);
            // fall through
        case MessageType_Scroll:
        case MessageType_GainFocus:
        case MessageType_LoseFocus:
        case MessageType_ControlCreated:
            Script_PlayDefault((M_CONTROL *)arg1, message);
            // fall through
        case MessageType_TextChanged:
        case MessageType_MemoBack:
        case MessageType_TextEndEntry:
        case MessageType_ValueSet:
        case MessageType_SelectRepeat:
            if (arg2 == 0)
                return __Menu_SendMessage(page->control.parent, message, arg1, 0);
            return 0;
        case MessageType_Destroy:
            if (page->controls.count != 0) {
                M_CONTROL *control = (M_CONTROL *)LinkedList_First(&page->controls);
                while (control != NULL) {
                    M_CONTROL *next = control->next;
                    __Menu_SendMessage(control, MessageType_Destroy, 0, 0);
                    control = next;
                }
            }
            Menu_Free((void **)&page, 4);
            return 1;
        case MessageType_IsCursorOverFocused:
            return (char)Menu_CursorOverMe((M_MANAGER *)page->control.parent, page->focused);
        case MessageType_SetInputDelay:
            page->inputDelay = arg1;
            return 0;
    }
    return 0;
}

// A frame of the page: the focus follows the cursor, then each control from the last runs its script and (unless
// hidden) its update - as the player whose focus it has, if any, and without input until the page's input delay has
// passed. The cursor goes to the focused control, the page's extra control is updated as if focused, and the page's
// handler gets its frame.
// AUTOINJECT
void Page_Update(M_MANAGER *mgr, M_PAGE *page, int takingInput) {
    if (page == NULL || mgr->cursorSprite == NULL)
        return;
    mgr->takingInput = takingInput;
    page->framesShown++;
    M_CONTROL *under = Menu_GetControl(mgr);
    M_CONTROL *was = page->focused;
    if (under != was) {
        page->focused = under;
        under->framesFocused = 0;
        if (mgr->takingInput) {
            Menu_AlphaReset();
            __Menu_SendMessage(page->control.parent, MessageType_LoseFocus, (int)was, 0);
            __Menu_SendMessage(page->control.parent, MessageType_GainFocus, (int)under, 0);
            if (Menu_GetPage(was) == Menu_GetPage(under))
                Menu_PlaySound(MENU_SFX_FOCUS);
        }
    }
    if (page->framesShown == 1) {
        if (mgr->overlayMode)
            help_control = NULL;
        else
            Page_FindHelpControl(page);
    }
    Page_SetHelpText(page);

    for (M_CONTROL *c = (M_CONTROL *)LinkedList_Last(&page->controls); c != NULL && mgr->exists; c = c->prev) {
        if (c->hashcode == page->focused->hashcode)
            c->framesFocused = page->focused->framesFocused;
        Script_RunFrame(mgr, c);
        if (c->state & CONTROL_STATE_HIDDEN)
            continue;
        M_CONTROL *focused = page->focused;
        bool input = mgr->takingInput;
        short player = mgr->whichPlayer;
        for (uchar i = 0; i < 4; i++) {
            if (page->playerFocus[i] == c && page == mgr->currentPage) {
                mgr->whichPlayer = i;
                mgr->cursorSprite->positionX = c->x + 1;
                mgr->cursorSprite->positionY = c->y + 1;
                mgr->cursorSprite->backupOnscreenHeight = 1;
                mgr->cursorSprite->backupOnscreenWidth = 1;
                page->focused = c;
                break;
            }
        }
        c->framesFocused++;
        if ((uint)page->framesShown < (uint)page->inputDelay)
            mgr->takingInput = false;
        Menu_Update(mgr, page, c);
        mgr->whichPlayer = player;
        page->focused = focused;
        mgr->takingInput = input;
    }

    if (page->focused != NULL && mgr->cursorSprite != NULL && mgr->currentPage == page) {
        sprite *cursor = mgr->cursorSprite;
        cursor->positionX = (page->focused->width >> 1) + mgr->currentPage->control.x + page->focused->x;
        cursor->positionY = (page->focused->height >> 1) + page->focused->y + mgr->currentPage->control.y;
        cursor->onscreenWidth = page->focused->width;
        cursor->onscreenHeight = page->focused->height;
        cursor->maybeEnabled = (uchar)page->focused->depth + 1;
    }
    if (page->control.extraControlHash != 0) {
        M_CONTROL *focused = page->focused;
        page->focused = (M_CONTROL *)__Menu_Send(mgr->index, page->control.extraControlHash, MessageType_GetControl, 0, 0);
        if (page->focused != NULL)
            Menu_Update(mgr, page, page->focused);
        page->focused = focused;
    }
    if (mgr->overlayMode && mgr->takingInput) {
        if (mgr->underPage != NULL)
            __Menu_SendMessage(mgr->underPage->control.parent, MessageType_PageUpdate, (int)page, 0);
    } else if (mgr->currentPage == page && mgr->takingInput) {
        __Menu_SendMessage(page->control.parent, MessageType_PageUpdate, (int)page, 0);
    }
}

// The page's help memo, if it has one
// AUTOINJECT
void Page_FindHelpControl(M_PAGE *page) {
    if (page == NULL) {
        help_control = NULL;
        return;
    }
    for (M_CONTROL *c = (M_CONTROL *)LinkedList_First(&page->controls); c != NULL; c = c->next) {
        if (c->type == ControlType_Memo && c->hashcode == HELP_MEMO) {
            help_control = c;
            return;
        }
    }
    help_control = NULL;
}

// The help memo shows the focused control's help line, or nothing
// AUTOINJECT
void Page_SetHelpText(M_PAGE *page) {
    if (help_control == NULL)
        return;
    M_CONTROL *focused = page->focused;
    if (focused == NULL)
        return;
    int value = __Menu_SendMessage(focused, MessageType_GetSelectedItemValue, 0, 0);
    for (int i = 0; i < HELP_TEXTS; i++) {
        if (help_texts[i].control == focused->hashcode &&
            (help_texts[i].value == value || help_texts[i].value == HELP_ANY_VALUE)) {
            __Menu_SendMessage(help_control, MessageType_SetText, (int)Txt_BindLabel(help_texts[i].text, 0), 0);
            return;
        }
    }
    __Menu_SendMessage(help_control, MessageType_SetText, 0, 0);
}
