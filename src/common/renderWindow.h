#ifndef COMMON_RENDERWINDOW_H_
#define COMMON_RENDERWINDOW_H_

// The window class the loader creates its render window with.
//
// Shared because two sides have to agree on it and neither owns it: the loader creates the window and pumps its
// messages (src/loader/loadermain.cpp), while the injected DLL finds it - the D3D9 backend to present into,
// and the input layer to decide whether the game has focus.
#define NIGHTFIRE_RENDER_WINDOW_CLASS "NightfireRender"

#ifdef _WIN32
#include <string.h>
#include <windows.h>

// This process's render window. Found by class, but only among the windows this process owns: a system-wide
// FindWindow takes another running game's window when two are open, and both then present into it.
static inline HWND FindOwnRenderWindow(void) {
    struct Search {
        DWORD process;
        HWND window;
    } search = { GetCurrentProcessId(), NULL };
    EnumWindows([](HWND window, LPARAM param) -> BOOL {
        Search *s = (Search *)param;
        DWORD owner = 0;
        char name[32];
        GetWindowThreadProcessId(window, &owner);
        if (owner != s->process || GetClassNameA(window, name, sizeof(name)) == 0 ||
            strcmp(name, NIGHTFIRE_RENDER_WINDOW_CLASS) != 0)
            return TRUE;
        s->window = window;
        return FALSE;
    }, (LPARAM)&search);
    return search.window;
}
#endif

#endif // COMMON_RENDERWINDOW_H_
