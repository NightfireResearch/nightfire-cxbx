#include "mouseLook.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "XboxSettings.h"
#include "../../common/renderWindow.h"

// ---------------------------------------------------------------------------------------------------------------
// See mouseLook.h for what this is for. What follows is how it gets the movement and how capture works.
//
// RAW INPUT, NOT CURSOR POSITION. Reading the cursor and subtracting the window centre is the obvious way to
// get a delta, and it is wrong for aiming: the position Windows reports has already been through the pointer
// acceleration curve that "Enhance pointer precision" applies, so the same physical movement produces a
// different angle depending on how fast it was made. Raw input reports the device's own counts, untouched.
// Since the whole point of this file is to avoid acceleration curves, going to the trouble is warranted -
// but if the registration fails for any reason, cursor deltas are used instead and say so, because being
// slightly wrong is much better than having no mouse.
//
// Raw input is delivered as WM_INPUT to a window, and the window's messages are pumped by the thread that
// created it. The game's thread pumps nothing - the render window belongs to the loader (or, under CXBX, to
// the launcher in another process entirely), so neither is any use here. Hence the message-only window below,
// created on and pumped from the game thread, registered with RIDEV_INPUTSINK so that it receives movement
// without ever being in the foreground. Whether the game should actually be listening is a separate question,
// answered by the focus check rather than by who has the keyboard.
//
// CONFINING THE POINTER takes both re-centring every frame and a ClipCursor to the client area, and it is
// worth being clear why neither alone is enough. Re-centring stops the pointer wandering off the window or
// clicking on anything behind it, but only once per frame: a quick flick carries it outside the window well
// inside the 20ms between two frames, and while it is out there it is over someone else's window, wearing
// someone else's cursor - which is exactly the flicker you get without the clip. The clip stops it leaving at
// all, so the blank cursor below always applies; re-centring is still wanted so that the pointer sits away
// from the edges and, when raw input is unavailable, so that there is a fixed point to measure from.
//
// ClipCursor is desktop-wide state, which is a reason for care rather than avoidance: Windows drops the clip
// when the clipping application stops being the foreground one or exits, and the capture is released on focus
// loss anyway, so there is no way for it to outlive the game and leave a pointer trapped.
//
// HIDING THE POINTER is done by giving the window class a fully transparent cursor, which is a roundabout way
// of saying it and is the only one that works from here. The two direct ways both belong to a thread this code
// is not: ShowCursor keeps its counter per thread input queue, and SetCursor is ignored unless the calling
// thread is the one currently receiving mouse input. The window belongs to the loader's thread (under CXBX, to
// another process entirely), and the game's thread owns nothing the pointer is ever over, so both quietly do
// nothing. Setting the class cursor to NULL does not work either, for a different reason: DefWindowProc reads
// that as "leave the cursor alone" rather than as "no cursor", so the arrow just stays as it was.
//
// A transparent cursor sidesteps all of it. SetClassLong is process-wide rather than queue-bound, and the
// window's own thread then applies it through DefWindowProc exactly as it would any other cursor. Under CXBX
// the window belongs to another process so the call fails, and the pointer stays visible, parked in the middle
// of the window - which is worth a line in the log rather than silence, since it looks like a bug.
// ---------------------------------------------------------------------------------------------------------------

#define MOUSELOOK_MESSAGE_WINDOW_CLASS "NightfireMouseLookSink"

// How many frames the aim hook may go missing before the capture is dropped. It runs every frame of normal
// play, so this only needs to cover a frame the game skips for its own reasons rather than because a menu
// opened - one would probably do, and five costs a tenth of a second of holding on too long.
#define IN_LEVEL_GRACE_FRAMES 5

// Radians per raw count at MouseSensitivity=1. About 0.086 degrees, so a 360 degree turn is roughly 4200
// counts - a little over five inches on an 800 DPI mouse, which is in the usual range for a shooter and is
// what the setting exists to move.
#define RADIANS_PER_COUNT 0.0015f

#define TWO_OVER_PI 0.63661977f

// Multipliers on the configured sensitivity, chosen so that scoped aiming is a quarter of the speed
// of hip fire rather than the same.
#define HIPFIRE_SENSITIVITY 2.0f
#define SCOPED_SENSITIVITY 0.5f

static bool    g_captured = false;
static HWND    g_rawWindow = NULL;        // message-only window that WM_INPUT is delivered to
static bool    g_rawWindowTried = false;
static bool    g_rawRegistered = false;
static bool    g_cursorHidden = false;
static HCURSOR g_savedClassCursor = NULL;
static POINT   g_cursorBeforeCapture;

static long     g_accumX = 0, g_accumY = 0; // raw counts the game has not consumed yet
static unsigned g_frame = 0;                // incremented once per MouseLook_Update
static unsigned g_lastAimFrame = 0;         // g_frame the last time the aim hook ran; 0 = never
static bool     g_prevButtonDown = false, g_prevEscapeDown = false;
static bool     g_leftDown = false, g_rightDown = false; // as of the last MouseLook_Update
static bool     g_swallowLeftUntilRelease = false;       // see Capture
static int      g_wheelAccum = 0;                        // raw wheel movement not yet turned into notches
static int      g_wheelStep = 0;                         // -1, 0 or +1, for this frame only
static bool     g_scoped = false;                        // as of the last Player_ViewClamping

// Either host's render window: CXBX's, or the one the loader creates when running standalone. Same order as
// psiInput.cpp's own lookup, and for the same reason - nothing changes for a CXBX-hosted run.
static HWND FindRenderWindow(void) {
    HWND window = FindWindowA("CxbxRender", NULL);
    if (window == NULL)
        window = FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
    return window;
}

static bool WindowHasFocus(HWND window) {
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == window || foreground == GetAncestor(window, GA_ROOT));
}

static bool GetClientCentre(HWND window, POINT *centre) {
    RECT client;
    if (!GetClientRect(window, &client))
        return false;

    POINT point;
    point.x = (client.right - client.left) / 2;
    point.y = (client.bottom - client.top) / 2;
    if (!ClientToScreen(window, &point))
        return false;

    *centre = point;
    return true;
}

static bool CursorIsInsideClient(HWND window) {
    POINT cursor;
    RECT client;
    if (!GetCursorPos(&cursor) || !GetClientRect(window, &client) || !ScreenToClient(window, &cursor))
        return false;
    return cursor.x >= 0 && cursor.y >= 0 && cursor.x < client.right && cursor.y < client.bottom;
}

static void EnsureRawInput(void) {
    if (g_rawWindowTried)
        return;
    g_rawWindowTried = true;

    WNDCLASSA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.lpszClassName = MOUSELOOK_MESSAGE_WINDOW_CLASS;
    RegisterClassA(&windowClass); // a failure here is either "already registered" or caught by the next call

    g_rawWindow = CreateWindowExA(0, MOUSELOOK_MESSAGE_WINDOW_CLASS, "", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, NULL, windowClass.hInstance, NULL);
    if (g_rawWindow == NULL) {
        printf("[mouse] could not create the raw input window (error %lu)\n", GetLastError());
        return;
    }

    RAWINPUTDEVICE device;
    device.usUsagePage = 0x01; // generic desktop
    device.usUsage = 0x02;     // mouse
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = g_rawWindow;
    g_rawRegistered = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
    if (!g_rawRegistered)
        printf("[mouse] raw input unavailable (error %lu) - using cursor movement instead, which the "
               "\"enhance pointer precision\" setting will affect\n", GetLastError());
}

static void PumpRawInput(void) {
    if (g_rawWindow == NULL)
        return;

    // Drained every frame whether or not the mouse is captured, so the queue cannot grow while the game sits
    // in a menu. Movement collected while not captured is thrown away in MouseLook_Update.
    MSG message;
    while (PeekMessageA(&message, g_rawWindow, 0, 0, PM_REMOVE)) {
        if (message.message == WM_INPUT) {
            RAWINPUT raw;
            UINT size = sizeof(raw);
            if (GetRawInputData((HRAWINPUT)message.lParam, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                raw.header.dwType == RIM_TYPEMOUSE &&
                (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                // Absolute reports come from tablets, touchscreens and some remote desktop setups, and are a
                // position rather than a movement. Ignoring them loses mouse look on those, which is a good
                // deal better than the view snapping to a corner.
                g_accumX += raw.data.mouse.lLastX;
                g_accumY += raw.data.mouse.lLastY;
            }
            if (size != (UINT)-1 && raw.header.dwType == RIM_TYPEMOUSE &&
                (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0) {
                // usButtonData is a signed count in WHEEL_DELTA units, in a field declared unsigned.
                g_wheelAccum += (int)(short)raw.data.mouse.usButtonData;
            }
        }
        DispatchMessageA(&message); // WM_INPUT has to reach DefWindowProc so the system can release it
    }
}

// A cursor whose AND mask is all ones and XOR mask all zeroes - "leave every pixel of the screen exactly as it
// is", which is the standard way of spelling an invisible cursor. It has to be the size the system asks for:
// CreateCursor refuses anything else, and that size is not always 32x32 - a machine set to large pointers
// wants 48 or 64, which is precisely the kind of thing that works here and not on someone else's desk. Made
// once and kept, since it is a system resource and the size cannot change while the process runs.
static HCURSOR BlankCursor(void) {
    static HCURSOR blank = NULL;
    static bool tried = false;
    if (tried)
        return blank;
    tried = true;

    int width = GetSystemMetrics(SM_CXCURSOR), height = GetSystemMetrics(SM_CYCURSOR);
    if (width <= 0 || height <= 0) {
        width = 32;
        height = 32;
    }

    int bytes = ((width + 7) / 8) * height;
    unsigned char *andMask = (unsigned char*)malloc(bytes);
    unsigned char *xorMask = (unsigned char*)malloc(bytes);
    if (andMask != NULL && xorMask != NULL) {
        memset(andMask, 0xFF, bytes);
        memset(xorMask, 0x00, bytes);
        blank = CreateCursor(GetModuleHandleA(NULL), 0, 0, width, height, andMask, xorMask);
        if (blank == NULL)
            printf("[mouse] could not make a %dx%d blank cursor (error %lu)\n", width, height, GetLastError());
    }
    free(andMask);
    free(xorMask);
    return blank;
}

// Returns whether the pointer is actually hidden, which the caller reports - "captured" and "you cannot see
// the pointer any more" are separate things, and a run where they disagree should say so rather than leave
// it to be noticed on screen.
static bool HideCursorOverWindow(HWND window) {
    if (g_cursorHidden)
        return true;

    HCURSOR blank = BlankCursor();
    if (blank == NULL)
        return false;

    SetLastError(0);
    HCURSOR previous = (HCURSOR)(LONG_PTR)SetClassLongA(window, GCL_HCURSOR, (LONG)(LONG_PTR)blank);
    if (previous == NULL && GetLastError() != 0) {
        printf("[mouse] could not set the window's cursor (error %lu)\n", GetLastError());
        return false;
    }

    g_savedClassCursor = previous;
    g_cursorHidden = true;
    return true;
}

static void RestoreCursorOverWindow(HWND window) {
    if (!g_cursorHidden)
        return;
    if (window != NULL && g_savedClassCursor != NULL)
        SetClassLongA(window, GCL_HCURSOR, (LONG)(LONG_PTR)g_savedClassCursor);
    g_cursorHidden = false;
    g_savedClassCursor = NULL;
}

// Shuts the pointer inside the window's client area, so that it is never briefly over another window wearing
// another window's cursor. Re-applied every frame: the window can be moved or resized underneath us, and the
// clip is dropped by the system on any foreground change.
static void ClipCursorToWindow(HWND window) {
    RECT client;
    if (!GetClientRect(window, &client))
        return;

    POINT topLeft = { client.left, client.top };
    POINT bottomRight = { client.right, client.bottom };
    if (!ClientToScreen(window, &topLeft) || !ClientToScreen(window, &bottomRight))
        return;

    RECT screenRect = { topLeft.x, topLeft.y, bottomRight.x, bottomRight.y };
    ClipCursor(&screenRect);
}

static void Capture(HWND window) {
    if (!GetCursorPos(&g_cursorBeforeCapture)) {
        g_cursorBeforeCapture.x = 0;
        g_cursorBeforeCapture.y = 0;
    }

    POINT centre;
    if (GetClientCentre(window, &centre))
        SetCursorPos(centre.x, centre.y);
    ClipCursorToWindow(window);
    bool hidden = HideCursorOverWindow(window);

    g_accumX = 0;
    g_accumY = 0;
    g_captured = true;

    // The click that takes control must not also pull the trigger, so the fire button stays suppressed until
    // it is let go of. Without this, every single time the pointer is captured the player fires a round.
    g_swallowLeftUntilRelease = true;

    printf("[mouse] captured - Escape releases it%s\n", hidden ? "" : " (the pointer could not be hidden)");
}

static void Release(HWND window) {
    if (!g_captured)
        return;

    ClipCursor(NULL);
    RestoreCursorOverWindow(window);
    if (g_cursorBeforeCapture.x != 0 || g_cursorBeforeCapture.y != 0)
        SetCursorPos(g_cursorBeforeCapture.x, g_cursorBeforeCapture.y);

    g_accumX = 0;
    g_accumY = 0;
    g_wheelAccum = 0;
    g_wheelStep = 0;
    g_scoped = false;
    g_captured = false;
    printf("[mouse] released\n");
}

// Whether the game is actually playing a level, which is the only time it makes sense to hold the pointer.
// See the comment on MouseLook_TakeAimDelta in the header for why this is the signal rather than a flag.
static bool InLevel(void) {
    return g_lastAimFrame != 0 && (g_frame - g_lastAimFrame) <= IN_LEVEL_GRACE_FRAMES;
}

void MouseLook_Update(void) {
    g_frame++;

    HWND window = FindRenderWindow();

    if (!Settings_GetMouseLook()) {
        Release(window);
        return;
    }

    EnsureRawInput();
    PumpRawInput();

    if (window == NULL) {
        Release(NULL);
        return;
    }

    // GetAsyncKeyState for the same reason psiInput.cpp uses it: this thread has no message queue of its own,
    // so GetKeyState would report a state that never changes. Both are edge-triggered - holding Escape must
    // not keep the capture off, and holding the button must not re-take it the instant it is released.
    bool escapeDown = (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0;
    bool buttonDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

    // Polled here rather than from psiInput.cpp so that everything the mouse reports is read at one point in
    // the frame, and so that the capture rules are the only thing deciding whether any of it counts.
    g_leftDown = buttonDown;
    g_rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    if (!buttonDown)
        g_swallowLeftUntilRelease = false;

    bool escapePressed = escapeDown && !g_prevEscapeDown;
    bool buttonPressed = buttonDown && !g_prevButtonDown;
    g_prevEscapeDown = escapeDown;
    g_prevButtonDown = buttonDown;

    if (!g_captured) {
        g_accumX = 0;
        g_accumY = 0;
        g_wheelAccum = 0;
        g_wheelStep = 0;
        if (buttonPressed && InLevel() && WindowHasFocus(window) && CursorIsInsideClient(window))
            Capture(window);
        return;
    }

    // Escape is also the keyboard's START button, so it opens the pause menu at the same time - which would
    // release the capture a frame later anyway, through InLevel. Handling it directly as well means the
    // pointer comes back immediately, and that it still comes back if the game declines to pause.
    if (escapePressed || !InLevel() || !WindowHasFocus(window)) {
        Release(window);
        return;
    }

    // One notch per frame, so that a flick of the wheel walks through the weapons one at a time instead of
    // jumping several at once - the game changes weapon on a press, and several presses in one frame would
    // be indistinguishable from one.
    g_wheelStep = 0;
    if (g_wheelAccum >= WHEEL_DELTA) {
        g_wheelStep = 1;
        g_wheelAccum -= WHEEL_DELTA;
    }
    else if (g_wheelAccum <= -WHEEL_DELTA) {
        g_wheelStep = -1;
        g_wheelAccum += WHEEL_DELTA;
    }

    ClipCursorToWindow(window);

    POINT centre;
    bool haveCentre = GetClientCentre(window, &centre);

    if (!g_rawRegistered && haveCentre) {
        POINT cursor;
        if (GetCursorPos(&cursor)) {
            g_accumX += cursor.x - centre.x;
            g_accumY += cursor.y - centre.y;
        }
    }

    if (haveCentre)
        SetCursorPos(centre.x, centre.y);
}

bool MouseLook_FireHeld(void) {
    return g_captured && g_leftDown && !g_swallowLeftUntilRelease;
}

bool MouseLook_ZoomHeld(void) {
    return g_captured && g_rightDown;
}

void MouseLook_SetScoped(bool scoped) {
    g_scoped = scoped;
}

bool MouseLook_NextWeapon(void) {
    return g_captured && !g_scoped && g_wheelStep > 0;
}

bool MouseLook_PrevWeapon(void) {
    return g_captured && !g_scoped && g_wheelStep < 0;
}

bool MouseLook_ZoomIn(void) {
    return g_captured && g_scoped && g_wheelStep > 0;
}

bool MouseLook_ZoomOut(void) {
    return g_captured && g_scoped && g_wheelStep < 0;
}

bool MouseLook_TakeAimDelta(float *yawRadians, float *pitchFraction) {
    g_lastAimFrame = g_frame;

    if (!g_captured)
        return false;

    long dx = g_accumX, dy = g_accumY;
    g_accumX = 0;
    g_accumY = 0;
    if (dx == 0 && dy == 0)
        return false;

    // No frame-time term anywhere in here, on purpose. These are counts the mouse has already moved, so the
    // angle they are worth is the same whether they arrived over one long frame or several short ones - and
    // scaling them by frame time would make the same movement turn a different amount depending on load.
    // Down a scope the view covers a much smaller angle, so the same hand movement should turn the player
    // less; everywhere else the original default was on the slow side for a mouse. The setting keeps meaning
    // hip-fire speed, and these two multipliers hang off it.
    float radiansPerCount = RADIANS_PER_COUNT * Settings_GetMouseSensitivity()
                          * (g_scoped ? SCOPED_SENSITIVITY : HIPFIRE_SENSITIVITY);

    // Screen axes against game axes: x grows to the right while the object's rotation grows turning left, and
    // y grows downwards while pitch grows looking up. Both are therefore negated.
    float yaw = -(float)dx * radiansPerCount;
    float pitch = -(float)dy * radiansPerCount;
    if (Settings_GetMouseInvertY())
        pitch = -pitch;

    *yawRadians = yaw;
    *pitchFraction = pitch * TWO_OVER_PI; // the game stores pitch as a fraction of a right angle, not radians
    return true;
}
