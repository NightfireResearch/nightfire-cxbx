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


#endif // XINPUT_XBOX_H_