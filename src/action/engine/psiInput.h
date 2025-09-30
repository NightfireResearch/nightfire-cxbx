#ifndef PSIINPUT_H_
#define PSIINPUT_H_

#include "../actionhelpers.h"

#include "xinput_xbox.h" // Differs from regular PC in at least one place...

#pragma pack(push, 1)


typedef struct {
    uint controllerIndex;
    XINPUT_STATE controllerState;
    char pad[2];
    float Joystick_LX;
    float Joystick_LY;
    float Joystick_RX;
    float Joystick_RY;
    uint buttons;
    XINPUT_VIBRATION vibrationState;
    // 15x additional vibration samples perhaps?
    char pad_2[60];
    char pad_3[2];
    ushort scaledRumbleA;
    ushort scaledRumbleB;
    ushort lastRumbleA;
    ushort rumbleA;
    ushort lastRumbleB;
    ushort rumbleB;
    char pad_4[2];
    uint prevButtons;
    char pad_5[4];
    int unknown[8]; // Angle-Magnitude or some other representations of joysticks / D-Pads?
} ControllerStateStruct;

static_assert(sizeof(ControllerStateStruct) == 0xa8, "Bad size for ControllerStateStruct");

typedef struct {
    bool Initialised; 
    char pad[3];
    ControllerStateStruct Controllers[4];
} XboxInputs_struct;

static_assert(sizeof(XboxInputs_struct) == 0x2a4, "Bad size for XboxInputs struct");

#pragma pack(pop)

#define XboxInputs (*(XboxInputs_struct*)(0x002ff498))

float psiInput_GetJoystickLX(uint i);
float psiInput_GetJoystickLY(uint i);
float psiInput_GetJoystickRX(uint i);
float psiInput_GetJoystickRY(uint i);
uint psiInput_GetButtons(uint i);

void psiInput_ResetInputState(unsigned int i);
void psiInput_RumbleStart(ushort controllerNum, int time, int intensity);
void psiInput_RumbleUpdate(void);
void psiInput_RumbleSetIntensity(unsigned int i, unsigned short a, unsigned short b);
void psiInputReset(void);
void psiInput_ResetRumble(unsigned int i);


#endif // PSIINPUT_H_