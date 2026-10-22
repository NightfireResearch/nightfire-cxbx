// The menus' scrolls: a value between a minimum and a maximum, shown as a bar with a thumb (PS2: menu_controls.cpp)

#include "Scroll.h"
#include "MenuCommon.h"
#include "Component.h"

#include <string.h>

#pragma fp_contract(off)

// The pressed directions, as draw states of the bar
#define SCROLL_LESS 0x40
#define SCROLL_MORE 0x80

// A held direction repeats from the 16th frame, every 5th frame, and every 2nd once held for more than 505
#define SCROLL_REPEAT_AFTER 15
#define SCROLL_FAST_AFTER 0x1f9

// A scroll from the menu data, or laid out again from its own size (NEW_CONTROL_LAYOUT: its thumb, value and scripts
// kept). A horizontal bar has square arrows at its ends as high as the bar, a vertical one as wide as it; the thumb is
// the track's share of one value, at least as long as the arrows.
// AUTOINJECT
void Scroll_Init(M_SCROLL *scroll, NEW_CONTROL *create, M_CONTROL *parent) {
    scroll->control.parent = parent;
    scroll->control.type = ControlType_Scroll;
    scroll->control.state = CONTROL_STATE_SHOWN;
    scroll->control.extraControlHash = (HASHCODE)0;
    scroll->control.hashcode = create->hashcode;
    scroll->control.depth = create->depth;
    scroll->flags = 0;
    scroll->min = create->min;
    scroll->max = create->max;
    scroll->range = scroll->max - scroll->min + 1;
    scroll->control.drawState = 0;
    if (create->keepState != NEW_CONTROL_LAYOUT)
        memset(&scroll->thumb, 0, sizeof(scroll->thumb));
    M_CONTROL *thumb = &scroll->thumb.control;
    thumb->type = ControlType_72;
    thumb->depth = create->depth - 1;
    thumb->parent = &scroll->control;
    thumb->skin = (uint)create->skin << 16 | 8;
    if (!create->hasBar)
        scroll->flags |= SCROLL_NO_BAR;
    if (create->height > create->width)
        scroll->flags |= SCROLL_VERTICAL;
    if (scroll->flags & SCROLL_VERTICAL) {
        scroll->control.skin = (uint)create->skin << 16 | 7;
        scroll->startInset = Component_GetHandle(&scroll->control)->top + create->width;
        scroll->endInset = Component_GetHandle(&scroll->control)->bottom + create->width;
        thumb->x = Component_GetHandle(thumb)->left + create->x;
        thumb->y = create->y + scroll->startInset;
        thumb->width = create->width - Component_GetHandle(thumb)->right - Component_GetHandle(thumb)->left;
        int length = (create->height - scroll->startInset - scroll->endInset) / scroll->range;
        thumb->height = length > create->width ? length : create->width;
    } else {
        scroll->control.skin = (uint)create->skin << 16 | 6;
        scroll->startInset = Component_GetHandle(&scroll->control)->left + create->height;
        scroll->endInset = Component_GetHandle(&scroll->control)->right + create->height;
        thumb->x = create->x + scroll->startInset;
        thumb->y = Component_GetHandle(thumb)->top + create->y;
        int length = (create->width - scroll->startInset - scroll->endInset) / scroll->range;
        thumb->width = length > create->height ? length : create->height;
        thumb->height = create->height - Component_GetHandle(thumb)->bottom - Component_GetHandle(thumb)->top;
    }
    scroll->control.x = create->x;
    scroll->control.y = create->y;
    scroll->control.width = create->width;
    scroll->control.height = create->height;
    if (create->keepState != NEW_CONTROL_LAYOUT) {
        scroll->pressed = 0;
        scroll->wrap = 1;
        scroll->proportionalBar = 0;
        scroll->value = scroll->min;
        scroll->control.id = create->id;
        scroll->control.sprites = NULL;
        thumb->sprites = NULL;
        Script_PrepareScripting(&scroll->control);
    }
}

// AUTOINJECT
intptr_t Scroll_SendMessage(M_SCROLL *scroll, uint message, intptr_t arg1, intptr_t arg2) {
    switch (message) {
        case MessageType_ScrollAddToMax:
            scroll->max += arg1;
            return 0;
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&scroll->control);
        case MessageType_ScrollHideBar:
            scroll->flags |= SCROLL_NO_BAR;
            return 1;
        case MessageType_ScriptPlay:
            return Script_Play(&scroll->control, arg1, (char)arg2);
        case MessageType_SetId:
            scroll->control.id = arg1;
            return 1;
        case MessageType_SetScript:
            return Control_SetScript(&scroll->control, arg1, arg2);
        case MessageType_ScrollSetMax:
            if (arg1 < scroll->min)
                return 0;
            scroll->max = arg1;
            if (scroll->value > arg1)
                scroll->value = arg1;
            return 1;
        case MessageType_ScrollSetMin:
            if (arg1 > scroll->max)
                return 0;
            scroll->min = arg1;
            if (scroll->value < arg1)
                scroll->value = arg1;
            return 1;
        case MessageType_SetRange:
            if (arg1 > arg2)
                return 0;
            scroll->min = arg1;
            scroll->max = arg2;
            scroll->range = arg2 - arg1 + 1;
            if (scroll->value < arg1)
                scroll->value = arg1;
            if (scroll->value > arg2)
                scroll->value = arg2;
            return 1;
        case MessageType_SetState:
            scroll->control.state = (uchar)arg1;
            if (scroll->control.state & CONTROL_STATE_HIDDEN) {
                Component_SetupInstance(&scroll->control, 0, -1, -1, -1, -1);
                Component_SetupInstance(&scroll->thumb.control, 0, -1, -1, -1, -1);
            }
            return 1;
        case MessageType_SetValue:
            if (arg1 > scroll->max || arg1 < scroll->min)
                return 0;
            scroll->value = arg1;
            __Menu_SendMessage(scroll->control.parent, MessageType_ValueSet, (intptr_t)scroll, 0);
            return 1;
        case MessageType_GetMax:
            return scroll->max;
        case MessageType_GetControl:
            return (intptr_t)scroll;
        case MessageType_GetMin:
            return scroll->min;
        case MessageType_GetId:
            return scroll->control.id;
        case MessageType_GetValue:
            if (scroll->value < scroll->min)
                return scroll->min;
            if (scroll->value > scroll->max)
                return scroll->max;
            return scroll->value;
        case MessageType_Destroy:
            Component_Free(&scroll->control);
            Component_Free(&scroll->thumb.control);
            Script_DelAllScripts(&scroll->control);
            if (scroll->control.parent->type == ControlType_Page)
                Menu_Free((void **)&scroll, 3);
            return 0;
        case MessageType_ScrollNoWrap:
            scroll->proportionalBar = 1;
            scroll->wrap = 0;
            return 1;
    }
    return 0;
}

// One step of the value, down or up, at the press or the repeat: within the range, or round to the other end where
// it wraps
static void Scroll_Step(M_SCROLL *scroll, bool more, bool repeat) {
    if (!more) {
        if (scroll->value > scroll->min)
            scroll->value--;
        else if (scroll->wrap)
            scroll->value = scroll->max;
        else
            return;
        Menu_PlaySound(repeat ? MENU_SFX_PREV_REPEAT : MENU_SFX_PREV);
    } else {
        if (scroll->value < scroll->max)
            scroll->value++;
        else if (scroll->wrap)
            scroll->value = scroll->min;
        else
            return;
        Menu_PlaySound(repeat ? MENU_SFX_NEXT_REPEAT : MENU_SFX_NEXT);
    }
    __Menu_SendMessage(scroll->control.parent, MessageType_Scroll, (intptr_t)scroll, 0);
}

// A press of a direction steps the value at once; held, it repeats
static void Scroll_Press(M_SCROLL *scroll, ushort direction, uint held, uint rate) {
    if (scroll->pressed == 0) {
        scroll->pressed = direction;
        scroll->heldFrames = 0;
        Scroll_Step(scroll, direction == SCROLL_MORE, false);
    } else if (scroll->pressed == direction && held > SCROLL_REPEAT_AFTER) {
        scroll->heldFrames = held + 1;
        if ((held + 1) % rate == 0)
            Scroll_Step(scroll, direction == SCROLL_MORE, true);
    } else {
        scroll->heldFrames = held + 1;
    }
}

// Where the thumb starts: the value's share of the space the thumb travels, from the track's start, kept between the
// first arrow's end and the last one's start (or the first's end, whichever is further)
static short Scroll_ThumbAt(double space, double offset, int count, double start, int first, int last) {
    short at = Ftol(space / (count > 0 ? count : 1) * offset + start);
    if (at < first)
        return first;
    int limit = last > first ? last : first;
    if (at > limit)
        return limit;
    return at;
}

// Select, the alternative selects and back on the focused scroll go to its owner
static void Scroll_Selects(M_MANAGER *mgr, M_PAGE *page, M_SCROLL *scroll) {
    if (page->focused->hashcode == scroll->control.hashcode && mgr->takingInput && !mgr->inputLocked) {
        if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_SELECT, 4)) {
            Menu_PlaySound(MENU_SFX_SELECT);
            __Menu_SendMessage(scroll->control.parent, MessageType_Select, (intptr_t)scroll, 0);
        } else if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_1, 4)) {
            Menu_PlaySound(MENU_SFX_ALTSELECT_1);
            __Menu_SendMessage(scroll->control.parent, MessageType_AltSelect1, (intptr_t)scroll, 0);
        } else if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_ALTSELECT_2, 4)) {
            Menu_PlaySound(MENU_SFX_ALTSELECT_2);
            __Menu_SendMessage(scroll->control.parent, MessageType_AltSelect2, (intptr_t)scroll, 0);
        } else if (Menu_InputAction(mgr->whichPlayer, ACTION_MENU_BACK, 4)) {
            Menu_PlaySound(MENU_SFX_BACK_ON_CONTROL);
            __Menu_SendMessage(scroll->control.parent, MessageType_BackOnControl, (intptr_t)scroll, 0);
        }
    }
}

// The d-pad moves the value while the scroll has the focus (left and right for a horizontal bar, up and down for a
// vertical one, not both ways at once); then the bar and its thumb are laid out from the value, and select, the
// alternative selects and back go to its owner
// AUTOINJECT
void Scroll_Update(M_MANAGER *mgr, M_PAGE *page, M_SCROLL *scroll) {
    // Laid out again from its own place and size
    NEW_CONTROL layout;
    layout.hashcode = scroll->control.hashcode;
    layout.min = scroll->min;
    layout.max = scroll->max;
    layout.id = 0;
    layout.keepState = NEW_CONTROL_LAYOUT;
    layout.skin = scroll->control.skin >> 16;
    layout.depth = scroll->control.depth;
    layout.x = scroll->control.x;
    layout.y = scroll->control.y;
    layout.width = scroll->control.width;
    layout.height = scroll->control.height;
    layout.hasBar = ~scroll->flags & SCROLL_NO_BAR;
    layout.type = 0;
    int value = scroll->value;
    Scroll_Init(scroll, &layout, scroll->control.parent);
    scroll->value = value;

    // A key's low byte
    uchar up = (uchar)Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_UP, 1);
    uchar down = (uchar)Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_DOWN, 1);
    uchar left = (uchar)Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_LEFT, 1);
    uchar right = (uchar)Menu_InputAction(mgr->whichPlayer, ACTION_MENU_DIR_RIGHT, 1);
    bool vertically = up || down;
    bool across = left || right;
    uint held = scroll->heldFrames;
    uint rate = held > SCROLL_FAST_AFTER ? 2 : 5;

    if (page->focused != NULL && page->focused->hashcode == scroll->control.hashcode && mgr->takingInput &&
        !mgr->inputLocked) {
        bool vertical = scroll->flags & SCROLL_VERTICAL;
        if (!vertical ? left && !vertically : up && !across)
            Scroll_Press(scroll, SCROLL_LESS, held, rate);
        else if (!vertical ? right && !vertically : down && !across)
            Scroll_Press(scroll, SCROLL_MORE, held, rate);
        else if (scroll->pressed != 0)
            scroll->pressed = 0;
        if (!(scroll->control.state & CONTROL_STATE_HIDDEN) && scroll->pressed == 0)
            scroll->control.drawState = 0x20;
        else
            scroll->control.drawState = scroll->pressed;
    } else if (mgr->takingInput) {
        if (!(scroll->control.state & CONTROL_STATE_HIDDEN))
            scroll->control.drawState = 0x10;
        scroll->pressed = 0;
        scroll->heldFrames = 0;
    }

    if ((scroll->control.parent->state & (CONTROL_STATE_HIDDEN | CONTROL_STATE_UNDRAWN)) ||
        (scroll->control.state & CONTROL_STATE_HIDDEN)) {
        scroll->control.state |= CONTROL_STATE_UNDRAWN;
        Component_Free(&scroll->control);
        Component_Free(&scroll->thumb.control);
        return;
    }

    M_CONTROL *thumb = &scroll->thumb.control;
    Component_SetupInstance(&scroll->control, scroll->control.drawState, -1, -1, -1, -1);
    if (!(scroll->flags & SCROLL_NO_BAR)) {
        int count = scroll->max - scroll->min;
        int width;
        int height;
        if (scroll->flags & SCROLL_VERTICAL) {
            int track = scroll->control.height - scroll->endInset - scroll->startInset;
            float space = track - thumb->height;
            float offset = scroll->value - scroll->min;
            float start = scroll->control.y + scroll->startInset;
            thumb->y = Scroll_ThumbAt(space, offset, count, start, scroll->control.width + scroll->control.y,
                                      scroll->control.y - scroll->control.width - thumb->height + scroll->control.height);
            short length = thumb->height < track ? thumb->height : track > 0 ? track : 0;
            height = length - Component_GetHandle(thumb)->bottom - Component_GetHandle(thumb)->top;
            width = thumb->width - Component_GetHandle(thumb)->right - Component_GetHandle(thumb)->left;
        } else {
            int track = scroll->control.width - scroll->endInset - scroll->startInset;
            float space = track - thumb->width;
            int offset = scroll->value - scroll->min;
            float start = scroll->control.x + scroll->startInset;
            int first = scroll->control.x + scroll->control.height;
            int last = scroll->control.x - thumb->width - scroll->control.height + scroll->control.width;
            thumb->x = Scroll_ThumbAt(space, offset, count, start, first, last);
            if (scroll->proportionalBar) {
                // The thumb fills the track from its start up to the value: the value's share of the count as it is
                short filled = Ftol((double)offset / count * track);
                height = thumb->height - Component_GetHandle(thumb)->bottom - Component_GetHandle(thumb)->top;
                Component_SetupInstance(thumb, scroll->control.drawState,
                                        Component_GetHandle(thumb)->left + scroll->startInset + scroll->control.x,
                                        Component_GetHandle(thumb)->top + thumb->y, filled, height);
                scroll->control.state &= ~CONTROL_STATE_UNDRAWN;
                Scroll_Selects(mgr, page, scroll);
                return;
            }
            short length = thumb->width < track ? thumb->width : track > 0 ? track : 0;
            height = thumb->height - Component_GetHandle(thumb)->bottom - Component_GetHandle(thumb)->top;
            width = length - Component_GetHandle(thumb)->right - Component_GetHandle(thumb)->left;
        }
        Component_SetupInstance(thumb, scroll->control.drawState, Component_GetHandle(thumb)->left + thumb->x,
                                Component_GetHandle(thumb)->top + thumb->y, width, height);
    }
    scroll->control.state &= ~CONTROL_STATE_UNDRAWN;
    Scroll_Selects(mgr, page, scroll);
}
