#include "HostWindow.h"

#include <windows.h>

#include "../../common/renderWindow.h"

// A Win32 window of the render class, whose messages the opening thread pumps. The D3D9 backend finds it by its
// class (src/common/gfx/d3d9Backend.cpp), and sizes it to the back buffer itself.

static HWND Window;

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

bool HostWindow_Open(int width, int height, const char *title) {
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.lpszClassName = NIGHTFIRE_RENDER_WINDOW_CLASS;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&windowClass);
    RECT wanted = { 0, 0, width, height };
    AdjustWindowRect(&wanted, WS_OVERLAPPEDWINDOW, FALSE);
    Window = CreateWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, CW_USEDEFAULT,
                           CW_USEDEFAULT, wanted.right - wanted.left, wanted.bottom - wanted.top, NULL, NULL,
                           windowClass.hInstance, NULL);
    return Window != NULL;
}

void HostWindow_Run(void) {
    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
}

void *HostWindow_Native(void) {
    return Window;
}

void HostWindow_SetClientSize(uint32_t width, uint32_t height) {
    RECT wanted = { 0, 0, (LONG)width, (LONG)height };
    if (!AdjustWindowRect(&wanted, (DWORD)GetWindowLongA(Window, GWL_STYLE), FALSE))
        return;
    SetWindowPos(Window, NULL, 0, 0, wanted.right - wanted.left, wanted.bottom - wanted.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}
