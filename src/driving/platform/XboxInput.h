#ifndef DRIVING_PLATFORM_XBOXINPUT_H_
#define DRIVING_PLATFORM_XBOXINPUT_H_

#include <windows.h>
#include <stdint.h>

// Replaces XAPI's controller API with Win32's XInput, so that IOModule reads real pads. See XboxInput.cpp for
// the translation between the two, and for what is deliberately not implemented. Called from Inject().
void Inject_XboxInput(void);

// XDEVICE_TYPE_GAMEPAD's table, which only names the type here
struct XboxDeviceType {
    uint32_t unknown00;
};
extern XboxDeviceType XboxGamepadType;

// The XInput entry points (XAPILIB's, at 0x001848ad-0x00184bd5), for the ports that call them
struct XboxState;
struct XboxFeedback;
uint32_t __stdcall Xbox_XGetDevices(void *deviceType);
uint32_t __stdcall Xbox_XGetDeviceChanges(void *deviceType, uint32_t *insertions, uint32_t *removals);
void *__stdcall Xbox_XInputOpen(void *deviceType, uint32_t port, uint32_t slot, void *pollingParameters);
void __stdcall Xbox_XInputClose(void *handle);
uint32_t __stdcall Xbox_XInputGetCapabilities(void *handle, void *capabilities);
uint32_t __stdcall Xbox_XInputGetState(void *handle, XboxState *state);
uint32_t __stdcall Xbox_XInputSetState(void *handle, XboxFeedback *feedback);

#endif // DRIVING_PLATFORM_XBOXINPUT_H_
