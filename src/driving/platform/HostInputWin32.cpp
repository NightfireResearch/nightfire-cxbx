#include "HostInput.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/renderWindow.h"

// Controllers from Win32's XInput, whose state is already HostPadState's shape, and the keyboard through
// GetAsyncKeyState. XInput's own header is not included: it would also declare functions whose names collide with
// the Xbox ones XboxInput.cpp replaces.

struct Win32Vibration {
    uint16_t wLeftMotorSpeed;
    uint16_t wRightMotorSpeed;
};

static_assert(sizeof(HostPadState) == 16, "HostPadState is XINPUT_STATE");

extern "C" __declspec(dllimport) unsigned long __stdcall XInputGetState(unsigned long index, HostPadState *state);
extern "C" __declspec(dllimport) unsigned long __stdcall XInputSetState(unsigned long index,
                                                                        Win32Vibration *vibration);
// MSVC acts on this; other linkers get the library from the CMake target instead. Guarded because
// clang emits the directive into .drectve regardless, and GNU ld then warns that it cannot read it.
#ifdef _MSC_VER
#pragma comment(lib, "xinput9_1_0.lib")
#endif

bool HostInput_ReadPad(int port, HostPadState *state) {
    return XInputGetState((unsigned long)port, state) == ERROR_SUCCESS;
}

void HostInput_SetRumble(int port, uint16_t leftMotor, uint16_t rightMotor) {
    Win32Vibration vibration;
    vibration.wLeftMotorSpeed = leftMotor;
    vibration.wRightMotorSpeed = rightMotor;
    XInputSetState((unsigned long)port, &vibration);
}

bool HostInput_KeyHeld(HostKey key) {
    static const int kVirtualKeys[] = {
        'A', 'D', 'S', 'W',
        VK_LEFT, VK_RIGHT, VK_UP, VK_DOWN,
        VK_SPACE, VK_SHIFT, VK_RETURN, VK_ESCAPE,
        VK_F8, VK_F9,
        'Q', 'E', 'R',
        'C', 'Z', 'X', 'P', 'N', '1', '2',
        VK_BACK, VK_LCONTROL, VK_F6,
    };
    return (GetAsyncKeyState(kVirtualKeys[key]) & 0x8000) != 0;
}

// The render window is found by its class (src/common/renderWindow.h). If it cannot be found the keys are accepted
// anyway: better stray input than input that silently does nothing.
bool HostInput_WindowHasFocus(void) {
    HWND render = FindOwnRenderWindow();
    if (render == NULL)
        return true;
    HWND root = GetAncestor(render, GA_ROOT);
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == root);
}

// ---------------------------------------------------------------------------------------------------------------
// The mouse.
//
// RAW INPUT, NOT CURSOR POSITION. The position Windows reports has already been through the pointer acceleration
// curve that "Enhance pointer precision" applies, so the same physical movement produces a different angle
// depending on how fast it was made. Raw input reports the device's own counts, untouched. If the registration
// fails, cursor deltas are used instead and say so, because being slightly wrong is much better than having no
// mouse.
//
// Raw input is delivered as WM_INPUT to a window, and the window's messages are pumped by the thread that created
// it. The game's thread pumps nothing, and the render window belongs to another thread, so it is no use here.
// Hence the message-only window below, created on and pumped from the thread that reads the mouse, registered with
// RIDEV_INPUTSINK so that it receives movement without ever being in the foreground.
//
// CONFINING THE POINTER takes both re-centring every read and a ClipCursor to the client area. Re-centring stops
// the pointer wandering off the window, but only once per frame: a quick flick carries it outside the window well
// inside the 20ms between two frames, wearing someone else's cursor while it is out there. The clip stops it
// leaving at all; re-centring keeps it away from the edges and, without raw input, gives a fixed point to measure
// from. Windows drops the clip when the application stops being the foreground one or exits.
//
// HIDING THE POINTER is done by giving the window class a fully transparent cursor. ShowCursor keeps its counter
// per thread input queue, and SetCursor is ignored unless the calling thread is the one receiving mouse input;
// the window belongs to another thread, so both quietly do nothing. A NULL class cursor means "leave the cursor
// alone" to DefWindowProc. SetClassLong is process-wide, and the window's own thread applies it.
// ---------------------------------------------------------------------------------------------------------------

#define MOUSE_MESSAGE_WINDOW_CLASS "NightfireMouseLookSink"

static bool MouseCaptured;
static HWND RawWindow;
static bool RawWindowTried;
static bool RawRegistered;
static bool CursorHidden;
static HCURSOR SavedClassCursor;
static POINT CursorBeforeCapture;
static int32_t AccumX, AccumY, AccumWheel;

static HWND FindRenderWindow(void) {
    return FindOwnRenderWindow();
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

static void EnsureRawInput(void) {
    if (RawWindowTried)
        return;
    RawWindowTried = true;

    WNDCLASSA windowClass;
    memset(&windowClass, 0, sizeof(windowClass));
    windowClass.lpfnWndProc = DefWindowProcA;
    windowClass.hInstance = GetModuleHandleA(NULL);
    windowClass.lpszClassName = MOUSE_MESSAGE_WINDOW_CLASS;
    RegisterClassA(&windowClass); // a failure here is either "already registered" or caught by the next call

    RawWindow = CreateWindowExA(0, MOUSE_MESSAGE_WINDOW_CLASS, "", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL,
                                windowClass.hInstance, NULL);
    if (RawWindow == NULL) {
        printf("[mouse] could not create the raw input window (error %lu)\n", GetLastError());
        return;
    }

    RAWINPUTDEVICE device;
    device.usUsagePage = 0x01; // generic desktop
    device.usUsage = 0x02;     // mouse
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = RawWindow;
    RawRegistered = RegisterRawInputDevices(&device, 1, sizeof(device)) != FALSE;
    if (!RawRegistered)
        printf("[mouse] raw input unavailable (error %lu) - using cursor movement instead, which the "
               "\"enhance pointer precision\" setting will affect\n", GetLastError());
}

// Drained every read whether or not the mouse is captured, so the queue cannot grow while nothing listens
static void PumpRawInput(void) {
    if (RawWindow == NULL)
        return;
    MSG message;
    while (PeekMessageA(&message, RawWindow, 0, 0, PM_REMOVE)) {
        if (message.message == WM_INPUT) {
            RAWINPUT raw;
            UINT size = sizeof(raw);
            if (GetRawInputData((HRAWINPUT)message.lParam, RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 &&
                raw.header.dwType == RIM_TYPEMOUSE &&
                (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
                // Absolute reports come from tablets, touchscreens and some remote desktop setups, and are a
                // position rather than a movement. Ignoring them loses mouse look on those, which is a good deal
                // better than the view snapping to a corner.
                AccumX += raw.data.mouse.lLastX;
                AccumY += raw.data.mouse.lLastY;
            }
            if (size != (UINT)-1 && raw.header.dwType == RIM_TYPEMOUSE &&
                (raw.data.mouse.usButtonFlags & RI_MOUSE_WHEEL) != 0) {
                // usButtonData is a signed count in WHEEL_DELTA units, in a field declared unsigned.
                AccumWheel += (int)(short)raw.data.mouse.usButtonData;
            }
        }
        DispatchMessageA(&message); // WM_INPUT has to reach DefWindowProc so the system can release it
    }
}

// A cursor whose AND mask is all ones and XOR mask all zeroes - "leave every pixel of the screen exactly as it is".
// It has to be the size the system asks for: CreateCursor refuses anything else, and that size is not always 32x32.
// Made once and kept, since it is a system resource.
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
    unsigned char *andMask = (unsigned char *)malloc(bytes);
    unsigned char *xorMask = (unsigned char *)malloc(bytes);
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

static bool HideCursorOverWindow(HWND window) {
    if (CursorHidden)
        return true;
    HCURSOR blank = BlankCursor();
    if (blank == NULL)
        return false;
    SetLastError(0);
    HCURSOR previous = (HCURSOR)SetClassLongPtrA(window, GCLP_HCURSOR, (LONG_PTR)blank);
    if (previous == NULL && GetLastError() != 0) {
        printf("[mouse] could not set the window's cursor (error %lu)\n", GetLastError());
        return false;
    }
    SavedClassCursor = previous;
    CursorHidden = true;
    return true;
}

static void RestoreCursorOverWindow(HWND window) {
    if (!CursorHidden)
        return;
    if (window != NULL && SavedClassCursor != NULL)
        SetClassLongPtrA(window, GCLP_HCURSOR, (LONG_PTR)SavedClassCursor);
    CursorHidden = false;
    SavedClassCursor = NULL;
}

// Re-applied every read: the window can be moved or resized underneath us, and the clip is dropped by the system
// on any foreground change
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

void HostMouse_Read(HostMouseState *state) {
    EnsureRawInput();
    PumpRawInput();
    HWND window = FindRenderWindow();
    if (MouseCaptured && window != NULL) {
        ClipCursorToWindow(window);
        POINT centre;
        bool haveCentre = GetClientCentre(window, &centre);
        if (!RawRegistered && haveCentre) {
            POINT cursor;
            if (GetCursorPos(&cursor)) {
                AccumX += cursor.x - centre.x;
                AccumY += cursor.y - centre.y;
            }
        }
        if (haveCentre)
            SetCursorPos(centre.x, centre.y);
    }
    state->dx = AccumX;
    state->dy = AccumY;
    state->wheel = AccumWheel;
    AccumX = AccumY = AccumWheel = 0;
    state->left = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    state->right = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
}

bool HostMouse_HasWindow(void) {
    return FindRenderWindow() != NULL;
}

bool HostMouse_InWindow(void) {
    HWND window = FindRenderWindow();
    POINT cursor;
    RECT client;
    if (window == NULL || !GetCursorPos(&cursor) || !GetClientRect(window, &client) || !ScreenToClient(window, &cursor))
        return false;
    return cursor.x >= 0 && cursor.y >= 0 && cursor.x < client.right && cursor.y < client.bottom;
}

bool HostMouse_Capture(bool capture) {
    HWND window = FindRenderWindow();
    if (capture) {
        if (!GetCursorPos(&CursorBeforeCapture)) {
            CursorBeforeCapture.x = 0;
            CursorBeforeCapture.y = 0;
        }
        bool hidden = false;
        if (window != NULL) {
            POINT centre;
            if (GetClientCentre(window, &centre))
                SetCursorPos(centre.x, centre.y);
            ClipCursorToWindow(window);
            hidden = HideCursorOverWindow(window);
        }
        AccumX = AccumY = 0;
        MouseCaptured = true;
        return hidden;
    }
    if (!MouseCaptured)
        return false;
    ClipCursor(NULL);
    RestoreCursorOverWindow(window);
    if (CursorBeforeCapture.x != 0 || CursorBeforeCapture.y != 0)
        SetCursorPos(CursorBeforeCapture.x, CursorBeforeCapture.y);
    AccumX = AccumY = AccumWheel = 0;
    MouseCaptured = false;
    return false;
}
