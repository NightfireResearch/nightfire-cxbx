#ifndef XINPUT_XBOX_H_
#define XINPUT_XBOX_H_

#pragma pack(push, 1)

// Note that this differs from the standard PC version, which does not support analog buttons
typedef struct {
    unsigned short wButtons;
    char bAnalogButtons[8];
    short sThumbLX;
    short sThumbLY;
    short sThumbRX;
    short sThumbRY;
} XINPUT_GAMEPAD;

static_assert(sizeof(XINPUT_GAMEPAD) == 0x12, "Bad size for XINPUT_GAMEPAD");

typedef struct {
    unsigned int dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
} XINPUT_STATE;

static_assert(sizeof(XINPUT_STATE) == 0x16, "Bad size for XINPUT_STATE");

typedef struct {
    ushort wLeftMotorSpeed;
    ushort wRightMotorSpeed;
} XINPUT_VIBRATION;

static_assert(sizeof(XINPUT_VIBRATION) == 0x4, "Bad size for XINPUT_VIBRATION");

#pragma pack(pop)

typedef enum {
    XINPUT_GAMEPAD_DPAD_UP 	= 0x0001,
    XINPUT_GAMEPAD_DPAD_DOWN 	= 0x0002,
    XINPUT_GAMEPAD_DPAD_LEFT 	= 0x0004,
    XINPUT_GAMEPAD_DPAD_RIGHT 	= 0x0008,
    XINPUT_GAMEPAD_START 	= 0x0010,
    XINPUT_GAMEPAD_BACK 	= 0x0020,
    XINPUT_GAMEPAD_LEFT_THUMB 	= 0x0040,
    XINPUT_GAMEPAD_RIGHT_THUMB 	= 0x0080,

    // Does not match modern XInput...
    XBOXINPUT_GAMEPAD_A 	= 0x10000,
    XBOXINPUT_GAMEPAD_X 	= 0x40000,
    XBOXINPUT_GAMEPAD_B 	= 0x20000,
    XBOXINPUT_GAMEPAD_Y 	= 0x80000,
    XBOXINPUT_GAMEPAD_RIGHT_SHOULDER 	= 0x100000,
    XBOXINPUT_GAMEPAD_LEFT_SHOULDER 	= 0x200000,
    XBOXINPUT_GAMEPAD_LEFT_TRIGGER 	= 0x400000,
    XBOXINPUT_GAMEPAD_RIGHT_TRIGGER 	= 0x800000,

} XInput_Gamepad_ButtonBits;


#endif // XINPUT_XBOX_H_