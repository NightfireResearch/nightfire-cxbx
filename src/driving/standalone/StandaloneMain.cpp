// The standalone build's entry point (docs/driving/standalone.md): no Driving.xbe mapped, no injection DLL. It does
// what the loader and the XBE's entry did for the injected build - make the render window, start the game's thread
// at mainXapiStartup (platform/XboxStartup.cpp), and pump the window's messages.
//
// Not yet done here, and listed in the doc: the kernel the loader provided (src/loader/kernel.cpp, which
// platform/KernelImports.cpp reaches through the import table), and the seams' installation without patching.

#include <windows.h>
#include <stdio.h>

#include "../../common/renderWindow.h"
#include "../platform/XboxStartup.h"

static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_CLOSE) {
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(window, message, wParam, lParam);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    WNDCLASSA windowClass = {};
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.lpszClassName = NIGHTFIRE_RENDER_WINDOW_CLASS;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    RegisterClassA(&windowClass);
    HWND window = CreateWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, "Nightfire (driving)", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                CW_USEDEFAULT, CW_USEDEFAULT, 640, 480, NULL, NULL, windowClass.hInstance, NULL);
    if (window == NULL)
        return 1;

    HANDLE thread = CreateThread(NULL, 0x10000, mainXapiStartup, NULL, 0, NULL);
    if (thread == NULL) {
        printf("[standalone] the game's thread could not be created (error %lu)\n", GetLastError());
        return 1;
    }
    CloseHandle(thread);

    MSG message;
    while (GetMessageA(&message, NULL, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageA(&message);
    }
    return 0;
}
