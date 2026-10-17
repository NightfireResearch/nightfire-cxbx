#include "HostInput.h"

#include <windows.h>

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
    };
    return (GetAsyncKeyState(kVirtualKeys[key]) & 0x8000) != 0;
}

// The render window is found by its class (src/common/renderWindow.h). If it cannot be found the keys are accepted
// anyway: better stray input than input that silently does nothing.
bool HostInput_WindowHasFocus(void) {
    HWND render = FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
    if (render == NULL)
        return true;
    HWND root = GetAncestor(render, GA_ROOT);
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == root);
}
