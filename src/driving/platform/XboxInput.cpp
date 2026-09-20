#include "XboxInput.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

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
// zero is a truthful answer to "what extra features does this pad have". Keyboard input, which the action
// engine synthesises as a virtual pad on port 0, would go here when the driving engine wants it.
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
        if (XInputGetState((unsigned long)port, &state) == WIN32_ERROR_SUCCESS)
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
        if (mask == 0)
            printf("[input] no controller found - the driving engine has no keyboard fallback yet\n");
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
    if (XInputGetState((unsigned long)port, &win32) != WIN32_ERROR_SUCCESS) {
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
