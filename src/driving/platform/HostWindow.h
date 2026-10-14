#ifndef DRIVING_PLATFORM_HOSTWINDOW_H_
#define DRIVING_PLATFORM_HOSTWINDOW_H_

#include <stdint.h>

// The game's window. Two bodies: HostWindowWin32.cpp (a Win32 window of the render class, src/common/renderWindow.h)
// and HostWindowSdl.cpp (SDL3's, with NIGHTFIRE_SDL). The thread that opens it runs its events (HostWindow_Run); the
// game runs on a thread of its own.
//
// The standalone executable opens it (standalone/StandaloneMain.cpp). The injected build's window is the loader's:
// with NIGHTFIRE_SDL the loader asks the injection DLL to open and run this one instead (src/inject_driving.cpp).

// Creates and shows the window with a client area of this size: false if it cannot.
bool HostWindow_Open(int width, int height, const char *title);
// Runs the window's events on the thread that opened it, until the window is closed.
void HostWindow_Run(void);
// What Direct3D presents into: the HWND on Windows (with SDL3 too), the SDL_Window on other hosts (dxvk-native's
// convention). Null before the window is open.
void *HostWindow_Native(void);
// Gives the window a client area of this size, from any thread.
void HostWindow_SetClientSize(uint32_t width, uint32_t height);

#endif // DRIVING_PLATFORM_HOSTWINDOW_H_
