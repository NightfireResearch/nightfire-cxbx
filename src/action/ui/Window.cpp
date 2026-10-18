// A page's window: its first control, which holds the scripts played as the page comes and goes (PS2: menu_manager.cpp)

#include "Window.h"
#include "MenuCommon.h"

// Owned by the manager, on the page it shows when made
// AUTOINJECT
void Window_Init(M_WINDOW *window, NEW_CONTROL *create, M_MANAGER *mgr) {
    window->control.state = CONTROL_STATE_SHOWN;
    window->control.type = ControlType_Window;
    window->control.skin = 0;
    window->control.parent = &mgr->control;
    window->control.hashcode = create->hashcode;
    window->control.id = create->id;
    window->control.depth = create->depth;
    window->control.sprites = NULL;
    window->control.drawState = 0;
    window->page = mgr->currentPage;
    window->control.x = create->x;
    window->control.y = create->y;
    window->control.width = create->width;
    window->control.height = create->height;
    Script_PrepareScripting(&window->control);
}

// AUTOINJECT
int Window_SendMessage(M_WINDOW *window, uint message, int arg1, int arg2) {
    switch (message) {
        case MessageType_ScriptExitLoop:
            return Script_ExitLoop(&window->control);
        case MessageType_ScriptPlay:
            return Script_Play(&window->control, arg1, (char)arg2);
        case MessageType_SetScript:
            return Control_SetScript(&window->control, arg1, arg2);
        case MessageType_GetControl:
            return (int)window;
        case MessageType_Destroy:
            Script_DelAllScripts(&window->control);
            Menu_Free((void **)&window, 3);
            return 0;
    }
    return 0;
}

// The page's window: its first control
// AUTOINJECT
M_CONTROL* Window_Get(M_PAGE *page) {
    return (M_CONTROL *)LinkedList_First(&page->controls);
}
