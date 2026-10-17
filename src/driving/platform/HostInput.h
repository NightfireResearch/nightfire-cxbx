#ifndef DRIVING_PLATFORM_HOSTINPUT_H_
#define DRIVING_PLATFORM_HOSTINPUT_H_

#include <stdint.h>

// The controllers and keyboard under XAPI's controller API (XboxInput.cpp, which turns these into what the Xbox
// reported). Two bodies: HostInputWin32.cpp (XInput, and the keyboard as Win32 sees it) and HostInputSdl.cpp (SDL3's
// gamepads and keyboard, with NIGHTFIRE_SDL).

// A pad as Win32's XInput reports it (XINPUT_STATE): the digital buttons, the two triggers 0..255, the sticks with up
// positive. The SDL body gives the same numbers.
struct HostPadState {
    uint32_t packetNumber;
    uint16_t buttons;
    uint8_t leftTrigger;
    uint8_t rightTrigger;
    int16_t thumbLX, thumbLY, thumbRX, thumbRY;
};

// XInput's button bits
enum : uint16_t {
    kPadDpadUp = 0x0001,
    kPadDpadDown = 0x0002,
    kPadDpadLeft = 0x0004,
    kPadDpadRight = 0x0008,
    kPadStart = 0x0010,
    kPadBack = 0x0020,
    kPadLeftThumb = 0x0040,
    kPadRightThumb = 0x0080,
    kPadLeftShoulder = 0x0100,
    kPadRightShoulder = 0x0200,
    kPadA = 0x1000,
    kPadB = 0x2000,
    kPadX = 0x4000,
    kPadY = 0x8000,
};

// The keys the keyboard pad uses (XboxInput.cpp)
enum HostKey {
    kKeyA, kKeyD, kKeyS, kKeyW,
    kKeyLeft, kKeyRight, kKeyUp, kKeyDown,
    kKeySpace, kKeyShift, kKeyEnter, kKeyEscape,
    kKeyF8, kKeyF9,
    kKeyQ, kKeyE, kKeyR,
};

// A pad on port 0-3: false when there is none.
bool HostInput_ReadPad(int port, HostPadState *state);
// The two motors' speeds, as XInputSetState takes them
void HostInput_SetRumble(int port, uint16_t leftMotor, uint16_t rightMotor);
bool HostInput_KeyHeld(HostKey key);
// Whether the game's window has the keyboard (true when it cannot be told)
bool HostInput_WindowHasFocus(void);

#endif // DRIVING_PLATFORM_HOSTINPUT_H_
