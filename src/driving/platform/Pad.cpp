#include "Pad.hpp"

#include <math.h>
#include <stdint.h>
#include <string.h>

// The four records, and the library's initialised flag.
#define gPads ((PadData *)0x00241c50)
#define gPadInitialised (*(bool *)0x00241e90)
static const int kPorts = 4;

// XAPI, called at its entry points in the XBE: under the standalone loader those are XboxInput.cpp's
// replacements, under CXBX the emulator's. XDEVICE_TYPE_GAMEPAD is the address of the device-type table XAPI
// keeps in the XBE.
#define XDEVICE_TYPE_GAMEPAD ((void *)0x00183aec)
struct XboxGamepadState {   // XINPUT_STATE
    uint32_t packetNumber;
    uint16_t buttons;
    uint8_t analog[8];
    int16_t thumbLX, thumbLY, thumbRX, thumbRY;
};
typedef uint32_t (__stdcall *XGetDevicesFn)(void *type);
typedef int (__stdcall *XGetDeviceChangesFn)(void *type, uint32_t *insertions, uint32_t *removals);
typedef void *(__stdcall *XInputOpenFn)(void *type, uint32_t port, uint32_t slot, void *pollingParameters);
typedef void (__stdcall *XInputCloseFn)(void *handle);
typedef uint32_t (__stdcall *XInputGetCapabilitiesFn)(void *handle, void *capabilities);
typedef uint32_t (__stdcall *XInputGetStateFn)(void *handle, XboxGamepadState *state);
#define XGetDevices ((XGetDevicesFn)0x00184bb3)
#define XGetDeviceChanges ((XGetDeviceChangesFn)0x00184bd5)
#define XInputOpen ((XInputOpenFn)0x001848ad)
#define XInputClose ((XInputCloseFn)0x00184903)
#define XInputGetCapabilities ((XInputGetCapabilitiesFn)0x0018490f)
#define XInputGetState ((XInputGetStateFn)0x00184aed)

// The game's task list: PAD_init adds PAD_update to it (0x0010aa50, __cdecl; the two zeros are passed as the
// original passes them).
typedef void (__cdecl *SyncTaskAddFn)(void *task, int a, int b);
#define SYNCTASK_add ((SyncTaskAddFn)0x0010aa50)
#define kPAD_updateOriginal ((void *)0x00108490)   // patched to jump to PAD_update below

// AUTOINJECT
PadData* PAD_getdataptr(int port) {
    return &gPads[port];
}

// AUTOINJECT
int PAD_getpadtype(int port) {
    return gPads[port].handle != nullptr;
}

// A stick axis: the signed 16-bit reading centred and scaled to about -1 .. 1, and zero within 0.25 of the middle.
// The original keeps the unrounded product on the x87 stack for the deadzone test; it has at most 41 significant
// bits, so double holds it exactly.
static float StickValue(int16_t raw) {
    const float scale = *(const float *)0x001a1788;   // 1 / 32767.5, as the game rounds it
    float value = ((float)raw + 0.5f) * scale;
    if (fabs(((double)raw + 0.5) * (double)scale) < 0.25)
        value = 0.0f;
    return value;
}

// AUTOINJECT
void PAD_update() {
    uint32_t insertions = 0, removals = 0;
    XGetDeviceChanges(XDEVICE_TYPE_GAMEPAD, &insertions, &removals);

    for (int port = 0; port < kPorts; port++) {
        PadData *pad = &gPads[port];
        uint32_t bit = 1u << port;

        pad->removed = (removals & bit) != 0;
        if (pad->removed) {
            XInputClose(pad->handle);
            pad->handle = nullptr;
            pad->unknown6a = 0;
            pad->unknown6c = 0;
        }
        pad->inserted = (insertions & bit) != 0;
        if (pad->inserted) {
            pad->handle = XInputOpen(XDEVICE_TYPE_GAMEPAD, port, 0, nullptr);
            XInputGetCapabilities(pad->handle, pad->capabilities);
        }
        if (pad->handle == nullptr)
            continue;

        XboxGamepadState state;
        XInputGetState(pad->handle, &state);

        pad->sticks[0] = StickValue(state.thumbLX);
        pad->sticks[1] = StickValue(state.thumbLY);
        pad->sticks[2] = StickValue(state.thumbRX);
        pad->sticks[3] = StickValue(state.thumbRY);

        uint16_t was = pad->buttons;
        pad->buttons = state.buttons;
        pad->buttonsPressed = (uint16_t)((was ^ state.buttons) & state.buttons);

        // An analogue button counts as pressed above 1 (of 255); its edge is set on the poll it first is.
        for (int i = 0; i < 8; i++) {
            bool now = state.analog[i] > 1;
            bool before = pad->analog[i] > 1;
            pad->analogPressed[i] = now && !before;
            pad->analog[i] = state.analog[i];
        }
    }
}

// AUTOINJECT
void PAD_init() {
    if (gPadInitialised)
        return;
    uint32_t present = XGetDevices(XDEVICE_TYPE_GAMEPAD);
    memset(gPads, 0, sizeof(PadData) * kPorts);
    for (int port = 0; port < kPorts; port++) {
        if (!(present & (1u << port)))
            continue;
        gPads[port].handle = XInputOpen(XDEVICE_TYPE_GAMEPAD, port, 0, nullptr);
        XInputGetCapabilities(gPads[port].handle, gPads[port].capabilities);
    }
    SYNCTASK_add(kPAD_updateOriginal, 0, 0);
    gPadInitialised = true;
}
