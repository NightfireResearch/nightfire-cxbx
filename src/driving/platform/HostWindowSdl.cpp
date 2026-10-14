#include "HostWindow.h"
#include "HostSdl.h"

#include <SDL3/SDL.h>
#ifdef _WIN32
#define SDL_MAIN_HANDLED   // SDL_RegisterApp only: the executable has a main of its own
#include <SDL3/SDL_main.h>
#endif
#include <stdio.h>

#include "../../common/renderWindow.h"

// The window on SDL3 (NIGHTFIRE_SDL). SDL is started here, on the thread that will run the window's events - the
// loader's main thread, or the standalone executable's - which is the thread SDL takes for its main one. The game's
// thread reads the keyboard and pads (HostInputSdl.cpp) and plays sound (sound/sdlAudioDriving.cpp) through SDL's
// thread-safe calls; anything that must happen on the main thread is handed to it with SDL_RunOnMainThread.

static SDL_Window *Window;

bool HostWindow_Open(int width, int height, const char *title) {
#ifdef _WIN32
    // The window class the D3D9 backend's checks, the test tools (tools/drive_game.ps1) and the devtools find the
    // render window by.
    SDL_RegisterApp(NIGHTFIRE_RENDER_WINDOW_CLASS, 0, NULL);
#endif
    // Pads are read whether or not the window is in front, as XInput reads them.
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        printf("[sdl] SDL could not start: %s\n", SDL_GetError());
        fflush(stdout);
        return false;
    }
    Window = SDL_CreateWindow(title, width, height, SDL_WINDOW_RESIZABLE);
    if (Window == NULL) {
        printf("[sdl] no window: %s\n", SDL_GetError());
        fflush(stdout);
        return false;
    }
    int version = SDL_GetVersion();
    printf("[sdl] SDL %d.%d.%d: window %dx%d, video driver %s, audio driver %s\n", SDL_VERSIONNUM_MAJOR(version),
           SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version), width, height, SDL_GetCurrentVideoDriver(),
           SDL_GetCurrentAudioDriver());
    fflush(stdout);
    return true;
}

void HostWindow_Run(void) {
    SDL_Event event;
    while (SDL_WaitEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
            return;
        HostInputSdl_Event(&event);
    }
    printf("[sdl] waiting for events failed: %s\n", SDL_GetError());
    fflush(stdout);
}

void *HostWindow_Native(void) {
    if (Window == NULL)
        return NULL;
#ifdef _WIN32
    return SDL_GetPointerProperty(SDL_GetWindowProperties(Window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#else
    return Window;
#endif
}

struct ClientSize {
    int width, height;
};

static void SDLCALL ApplyClientSize(void *data) {
    ClientSize *size = static_cast<ClientSize *>(data);
    SDL_SetWindowSize(Window, size->width, size->height);
}

// Waits for the window's thread to do it, so that a swap chain made next is made against the new size.
void HostWindow_SetClientSize(uint32_t width, uint32_t height) {
    if (Window == NULL)
        return;
    ClientSize size = { (int)width, (int)height };
    SDL_RunOnMainThread(ApplyClientSize, &size, true);
}

bool HostWindowSdl_HasKeyboard(void) {
    return Window == NULL || SDL_GetKeyboardFocus() == Window;
}
