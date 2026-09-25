#include "XboxInput.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "../../common/renderWindow.h"

// ---------------------------------------------------------------------------------------------------------------
// Controllers, from Win32's XInput instead of the Xbox's USB stack.
//
// The seam is XAPI's input API, for the same reason the graphics seam is at the D3D8 entry points: the layer
// above it is the game's, and the layer below it is a USB host controller that is not there. XInitDevices,
// which starts that stack, is replaced by nothing (see XboxStartup.cpp), so the seven functions here are all
// that stand between IOModule and the hardware.
//
// The two APIs are the same API twice, which is what makes this short. Xbox XINPUT became Win32 XInput almost
// unchanged: the digital button bits are identical (d-pad, start, back, thumbs), the sticks are the same
// signed 16-bit pairs, and the packet number means the same thing. The differences are exactly two:
//
//  - the Xbox pad's face buttons are analogue. A, B, X, Y, black and white each report 0..255, and the game
//    reads them that way (IOModule::Update copies all eight bytes and thresholds them at 1). Win32 reports
//    them as bits, so a pressed button becomes 255 here;
//  - the Xbox has black and white shoulder buttons where Win32 has two digital shoulders, so those map onto
//    the analogue black and white slots.
//
// The action engine does the same translation one layer higher up, inside the game's own polling function
// (src/action/engine/psiInput.cpp), because its input code was being reimplemented anyway. That file is worth
// reading for the deadzone and mapping decisions; none of them belong here, where the job is only to answer
// the questions XAPI would have answered.
//
// Not implemented: XInputGetCapabilities, which the game calls into a structure it has already zeroed - and
// zero is a truthful answer to "what extra features does this pad have".
//
// There is a keyboard fallback, synthesised as a pad on port 0 whenever nothing real is plugged into it. It
// is the same idea as the action engine's and for the same reason: without it the game stops at "please
// reconnect the controller to controller port 1" and there is no way past that screen. See the bindings
// below.
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)

// Win32's shapes, declared here rather than by including xinput.h, which would also declare functions whose
// names collide with the Xbox ones being replaced.
struct Win32Gamepad {
    uint16_t wButtons;
    uint8_t  bLeftTrigger;
    uint8_t  bRightTrigger;
    int16_t  sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};

struct Win32State {
    uint32_t     dwPacketNumber;
    Win32Gamepad Gamepad;
};

struct Win32Vibration {
    uint16_t wLeftMotorSpeed;
    uint16_t wRightMotorSpeed;
};

// The Xbox's, as the game reads them. XINPUT_STATE is the packet number, the digital buttons, the eight
// analogue buttons and the four stick axes - the layout IOModule::Update unpacks byte for byte.
struct XboxGamepad {
    uint16_t wButtons;
    uint8_t  bAnalogButtons[8];
    int16_t  sThumbLX, sThumbLY, sThumbRX, sThumbRY;
};

struct XboxState {
    uint32_t    dwPacketNumber;
    XboxGamepad Gamepad;
};

// XINPUT_FEEDBACK: a header the kernel used for the asynchronous USB write, then the motor speeds. The
// offsets are the ones the game writes to (0x42 and 0x44 from the start of its feedback block), which is what
// says the header is 66 bytes.
struct XboxFeedback {
    uint32_t dwStatus;
    void    *hEvent;
    uint8_t  Reserved[58];
    uint16_t wLeftMotorSpeed;
    uint16_t wRightMotorSpeed;
};

#pragma pack(pop)

static_assert(sizeof(XboxState) == 22, "the game unpacks XINPUT_STATE by offset");
static_assert(offsetof(XboxFeedback, wLeftMotorSpeed) == 0x42, "the game writes the motor speeds at 0x42");

extern "C" __declspec(dllimport) unsigned long __stdcall XInputGetState(unsigned long index, Win32State *state);
extern "C" __declspec(dllimport) unsigned long __stdcall XInputSetState(unsigned long index,
                                                                        Win32Vibration *vibration);
#pragma comment(lib, "xinput9_1_0.lib")

#define WIN32_ERROR_SUCCESS          0
#define XBOX_ERROR_DEVICE_NOT_CONNECTED 0x48F

// Win32 button bits for the face and shoulder buttons, which the Xbox reports as pressure instead.
#define WIN32_GAMEPAD_A              0x1000
#define WIN32_GAMEPAD_B              0x2000
#define WIN32_GAMEPAD_X              0x4000
#define WIN32_GAMEPAD_Y              0x8000
#define WIN32_GAMEPAD_LEFT_SHOULDER  0x0100
#define WIN32_GAMEPAD_RIGHT_SHOULDER 0x0200
#define WIN32_DIGITAL_ONLY_MASK      (WIN32_GAMEPAD_A | WIN32_GAMEPAD_B | WIN32_GAMEPAD_X | WIN32_GAMEPAD_Y | \
                                      WIN32_GAMEPAD_LEFT_SHOULDER | WIN32_GAMEPAD_RIGHT_SHOULDER)

// Which analogue button is which, as the Xbox numbers them.
enum { ANALOG_A = 0, ANALOG_B = 1, ANALOG_X = 2, ANALOG_Y = 3,
       ANALOG_BLACK = 4, ANALOG_WHITE = 5, ANALOG_LEFT_TRIGGER = 6, ANALOG_RIGHT_TRIGGER = 7 };

#define MAX_PORTS 4

// ---------------------------------------------------------------------------------------------------------------
// The keyboard, as a pad on port 0.
//
// A driving game needs four things - steer, accelerate, brake, handbrake - and a menu needs a few more. The
// bindings follow the conventions the game's own prompts assume (START to continue, A to accept, B to go
// back), and the analogue controls are all-or-nothing, which is what a keyboard can offer:
//
//   A / D  or  left / right      steer            (the left stick)
//   W  or  up arrow              accelerate       (the right trigger)
//   S  or  down arrow            brake and reverse (the left trigger)
//   Space                        handbrake        (A)
//   Left Shift                   B
//   Enter                        START - which is what the "press START to continue" prompts want
//   Escape                       BACK
//   arrow keys                   the d-pad as well as the stick, for menus
//
// Only while the game's window is in front, so that typing elsewhere does not drive the car. If the window
// cannot be found the keys are accepted anyway: better stray input than input that silently does nothing.
// ---------------------------------------------------------------------------------------------------------------

#define VK_BACK_        0x08
#define VK_RETURN_      0x0D
#define VK_SHIFT_       0x10
#define VK_ESCAPE_      0x1B
#define VK_SPACE_       0x20
#define VK_LEFT_        0x25
#define VK_UP_          0x26
#define VK_RIGHT_       0x27
#define VK_DOWN_        0x28
#define GA_ROOT_        2

#define WIN32_GAMEPAD_DPAD_UP    0x0001
#define WIN32_GAMEPAD_DPAD_DOWN  0x0002
#define WIN32_GAMEPAD_DPAD_LEFT  0x0004
#define WIN32_GAMEPAD_DPAD_RIGHT 0x0008
#define WIN32_GAMEPAD_START      0x0010
#define WIN32_GAMEPAD_BACK       0x0020

static bool KeyHeld(int virtualKey) {
    return (GetAsyncKeyState(virtualKey) & 0x8000) != 0;
}

static bool GameWindowHasFocus(void) {
    HWND render = FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
    if (render == NULL)
        render = FindWindowA("CxbxRender", NULL);
    if (render == NULL)
        return true;
    HWND root = GetAncestor(render, GA_ROOT_);
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == root);
}

// Builds the state a pad would have reported. Always fills it in; returns whether anything is held, which is
// only used to decide whether to say so the first time.
static bool BuildKeyboardState(Win32State *state) {
    memset(state, 0, sizeof(*state));
    if (!GameWindowHasFocus())
        return false;

    // The packet number only has to change when the state does, which is what the game's own edge detection
    // watches; counting every poll is simpler and equally true.
    static uint32_t packet = 0;
    state->dwPacketNumber = ++packet;

    bool left = KeyHeld('A') || KeyHeld(VK_LEFT_);
    bool right = KeyHeld('D') || KeyHeld(VK_RIGHT_);
    bool forward = KeyHeld('W') || KeyHeld(VK_UP_);
    bool back = KeyHeld('S') || KeyHeld(VK_DOWN_);

    if (left != right)
        state->Gamepad.sThumbLX = left ? -32767 : 32767;
    state->Gamepad.bRightTrigger = forward ? 255 : 0;
    state->Gamepad.bLeftTrigger = back ? 255 : 0;

    if (KeyHeld(VK_SPACE_))  state->Gamepad.wButtons |= WIN32_GAMEPAD_A;
    if (KeyHeld(VK_SHIFT_))  state->Gamepad.wButtons |= WIN32_GAMEPAD_B;
    if (KeyHeld(VK_RETURN_)) state->Gamepad.wButtons |= WIN32_GAMEPAD_START;
    if (KeyHeld(VK_ESCAPE_)) state->Gamepad.wButtons |= WIN32_GAMEPAD_BACK;
    if (KeyHeld(VK_UP_))     state->Gamepad.wButtons |= WIN32_GAMEPAD_DPAD_UP;
    if (KeyHeld(VK_DOWN_))   state->Gamepad.wButtons |= WIN32_GAMEPAD_DPAD_DOWN;
    if (KeyHeld(VK_LEFT_))   state->Gamepad.wButtons |= WIN32_GAMEPAD_DPAD_LEFT;
    if (KeyHeld(VK_RIGHT_))  state->Gamepad.wButtons |= WIN32_GAMEPAD_DPAD_RIGHT;

    return state->Gamepad.wButtons != 0 || state->Gamepad.sThumbLX != 0 ||
           state->Gamepad.bLeftTrigger != 0 || state->Gamepad.bRightTrigger != 0;
}

// The port-0 read, from a real pad if there is one and from the keyboard if there is not. Everything else in
// this file goes through it, so the keyboard appears as a device to XGetDevices as well - which is what gets
// the game past its "no controller" screen.
// NIGHTFIRE_HOLD=brake (or accelerate) holds that trigger on port 0 for the whole run, over whatever the pad or
// keyboard says. For unattended tests (tools/drive_game.ps1 -GameHold): keys sent from outside the game are
// lost whenever the window is not in front or a real pad is switched on, and this is neither.
static void ApplyForcedHold(Win32State *state) {
    static int hold = -1;   // 0 none, 1 left trigger (brake/reverse), 2 right trigger (accelerate)
    if (hold < 0) {
        char text[32] = "";
        GetEnvironmentVariableA("NIGHTFIRE_HOLD", text, sizeof(text));
        hold = _stricmp(text, "brake") == 0 ? 1 : _stricmp(text, "accelerate") == 0 ? 2 : 0;
        if (hold != 0) {
            printf("[input] NIGHTFIRE_HOLD: holding %s for the whole run\n", hold == 1 ? "brake" : "accelerate");
            fflush(stdout);
        }
    }
    if (hold == 0)
        return;
    if (hold == 1) state->Gamepad.bLeftTrigger = 255;
    if (hold == 2) state->Gamepad.bRightTrigger = 255;
    static uint32_t packet = 0x40000000;   // the unfocused keyboard reports packet 0; a held trigger is still news
    state->dwPacketNumber = ++packet;
}

static bool ReadPort(int port, Win32State *state) {
    if (XInputGetState((unsigned long)port, state) == WIN32_ERROR_SUCCESS) {
        if (port == 0)
            ApplyForcedHold(state);
        return true;
    }
    if (port != 0)
        return false;

    static bool announced = false;
    if (!announced) {
        announced = true;
        printf("[input] no pad on port 1: the keyboard is standing in for one "
               "(WASD or arrows, Enter for START)\n");
        fflush(stdout);
    }
    BuildKeyboardState(state);
    ApplyForcedHold(state);
    return true;
}

// A handle is the port it was opened for, plus one so that no valid handle is null - the game tests handles
// against zero and would treat port 0 as "not open" otherwise.
static int PortOfHandle(void *handle) {
    int port = (int)(intptr_t)handle - 1;
    return (port >= 0 && port < MAX_PORTS) ? port : -1;
}

// What was connected the last time anyone asked, so that XGetDeviceChanges can report the difference.
static uint32_t g_lastConnectedMask = 0;
static bool g_maskEverRead = false;

static uint32_t ConnectedMask(void) {
    uint32_t mask = 0;
    for (int port = 0; port < MAX_PORTS; port++) {
        Win32State state;
        memset(&state, 0, sizeof(state));
        if (ReadPort(port, &state))
            mask |= 1u << port;
    }
    return mask;
}

// Ordinal-free: these are XAPI functions in the XBE, patched at their own addresses by Inject_XboxInput.

// XGetDevices(type) - which ports have a device of this type on them. The type argument names the device
// class (gamepad, memory unit, keyboard); only gamepads are answered, and the game only asks about gamepads.
static uint32_t __stdcall Xbox_XGetDevices(void *deviceType) {
    (void)deviceType;
    uint32_t mask = ConnectedMask();
    if (!g_maskEverRead) {
        g_maskEverRead = true;
        g_lastConnectedMask = mask;
        printf("[input] controllers on ports:%s%s%s%s\n",
               (mask & 1) ? " 1" : "", (mask & 2) ? " 2" : "",
               (mask & 4) ? " 3" : "", (mask & 8) ? " 4" : "");
        fflush(stdout);
    }
    return mask;
}

// XGetDeviceChanges(type, &insertions, &removals) - the edges since the last call. The game polls this every
// frame and opens or closes a handle for each change, so the two masks have to be transitions rather than
// state, and each transition has to be reported exactly once.
static uint32_t __stdcall Xbox_XGetDeviceChanges(void *deviceType, uint32_t *insertions, uint32_t *removals) {
    (void)deviceType;
    uint32_t mask = ConnectedMask();
    uint32_t inserted = mask & ~g_lastConnectedMask;
    uint32_t removed = ~mask & g_lastConnectedMask;
    g_lastConnectedMask = mask;
    g_maskEverRead = true;

    if (insertions != NULL)
        *insertions = inserted;
    if (removals != NULL)
        *removals = removed;
    return (inserted | removed) != 0;
}

// XInputOpen(type, port, slot, pollingParameters) - the slot is for the memory units in a pad's expansion
// ports, and the polling parameters ask the kernel to poll in the background, neither of which applies.
static void *__stdcall Xbox_XInputOpen(void *deviceType, uint32_t port, uint32_t slot,
                                       void *pollingParameters) {
    (void)deviceType; (void)slot; (void)pollingParameters;
    if (port >= MAX_PORTS)
        return NULL;
    return (void *)(intptr_t)(port + 1);
}

static void __stdcall Xbox_XInputClose(void *handle) {
    (void)handle;   // nothing was opened, so nothing has to be closed
}

// XInputGetCapabilities(handle, capabilities) - see the note at the top: the caller has zeroed the structure
// and zero is a truthful answer, so this reports success without writing anything.
static uint32_t __stdcall Xbox_XInputGetCapabilities(void *handle, void *capabilities) {
    (void)handle; (void)capabilities;
    return 0;
}

// XInputGetState(handle, state) - the translation described at the top of the file.
static uint32_t __stdcall Xbox_XInputGetState(void *handle, XboxState *state) {
    int port = PortOfHandle(handle);
    if (port < 0 || state == NULL)
        return XBOX_ERROR_DEVICE_NOT_CONNECTED;

    Win32State win32;
    memset(&win32, 0, sizeof(win32));
    if (!ReadPort(port, &win32)) {
        memset(state, 0, sizeof(*state));
        return XBOX_ERROR_DEVICE_NOT_CONNECTED;
    }

    memset(state, 0, sizeof(*state));
    state->dwPacketNumber = win32.dwPacketNumber;

    // The digital bits that mean the same on both: d-pad, start, back and the thumb clicks. The face and
    // shoulder bits are dropped here and come back below as pressures.
    state->Gamepad.wButtons = (uint16_t)(win32.Gamepad.wButtons & ~WIN32_DIGITAL_ONLY_MASK);

    state->Gamepad.bAnalogButtons[ANALOG_A] = (win32.Gamepad.wButtons & WIN32_GAMEPAD_A) ? 255 : 0;
    state->Gamepad.bAnalogButtons[ANALOG_B] = (win32.Gamepad.wButtons & WIN32_GAMEPAD_B) ? 255 : 0;
    state->Gamepad.bAnalogButtons[ANALOG_X] = (win32.Gamepad.wButtons & WIN32_GAMEPAD_X) ? 255 : 0;
    state->Gamepad.bAnalogButtons[ANALOG_Y] = (win32.Gamepad.wButtons & WIN32_GAMEPAD_Y) ? 255 : 0;
    state->Gamepad.bAnalogButtons[ANALOG_BLACK] =
        (win32.Gamepad.wButtons & WIN32_GAMEPAD_RIGHT_SHOULDER) ? 255 : 0;
    state->Gamepad.bAnalogButtons[ANALOG_WHITE] =
        (win32.Gamepad.wButtons & WIN32_GAMEPAD_LEFT_SHOULDER) ? 255 : 0;
    state->Gamepad.bAnalogButtons[ANALOG_LEFT_TRIGGER] = win32.Gamepad.bLeftTrigger;
    state->Gamepad.bAnalogButtons[ANALOG_RIGHT_TRIGGER] = win32.Gamepad.bRightTrigger;

    state->Gamepad.sThumbLX = win32.Gamepad.sThumbLX;
    state->Gamepad.sThumbLY = win32.Gamepad.sThumbLY;
    state->Gamepad.sThumbRX = win32.Gamepad.sThumbRX;
    state->Gamepad.sThumbRY = win32.Gamepad.sThumbRY;
    return 0;
}

// XInputSetState(handle, feedback) - rumble. On the console this queued a USB write and reported completion
// through the header, which the game reads back; here the write has already happened by the time it returns,
// so the status is set to success before returning.
static uint32_t __stdcall Xbox_XInputSetState(void *handle, XboxFeedback *feedback) {
    int port = PortOfHandle(handle);
    if (port < 0 || feedback == NULL)
        return XBOX_ERROR_DEVICE_NOT_CONNECTED;

    Win32Vibration vibration;
    vibration.wLeftMotorSpeed = feedback->wLeftMotorSpeed;
    vibration.wRightMotorSpeed = feedback->wRightMotorSpeed;
    XInputSetState((unsigned long)port, &vibration);

    feedback->dwStatus = 0;   // ERROR_SUCCESS: the write is finished
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Installing them. Standalone only, as with everything else that replaces a piece of XAPI: under CXBX these
// are already CXBX's own, talking to whatever it has bound to.
// ---------------------------------------------------------------------------------------------------------------

static void WriteJump(unsigned address, const void *target) {
    unsigned char *site = (unsigned char *)address;
    DWORD previous = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[input] could not make 0x%08x writable (error %lu)\n", address, GetLastError());
        return;
    }
    site[0] = 0xE9;                                             // jmp rel32
    *(int *)(site + 1) = (int)((const unsigned char *)target - (site + 5));
}

void Inject_XboxInput(void) {
    if (!Xbox_RunningStandalone())
        return;

    WriteJump(0x001848ad, (void *)Xbox_XInputOpen);
    WriteJump(0x00184903, (void *)Xbox_XInputClose);
    WriteJump(0x0018490f, (void *)Xbox_XInputGetCapabilities);
    WriteJump(0x00184aed, (void *)Xbox_XInputGetState);
    WriteJump(0x00184b59, (void *)Xbox_XInputSetState);
    WriteJump(0x00184bb3, (void *)Xbox_XGetDevices);
    WriteJump(0x00184bd5, (void *)Xbox_XGetDeviceChanges);
}
