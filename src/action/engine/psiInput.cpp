#include "psiInput.h"
#include <assert.h>
#include "../actionhelpers.h"

// Array of 4 uint32_t entries, all initialised to 0xFFFFFFFF
#define controller_maybeRumbleTimeout ((int*)0x0019481c)

// AUTOINJECT
float psiInput_GetJoystickRX(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    return XboxInputs.Controllers[i].Joystick_RX;
}

// AUTOINJECT
float psiInput_GetJoystickLX(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    return XboxInputs.Controllers[i].Joystick_LX;
}
// AUTOINJECT
float psiInput_GetJoystickLY(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    return XboxInputs.Controllers[i].Joystick_LY;
}

// AUTOINJECT
float psiInput_GetJoystickRY(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    return XboxInputs.Controllers[i].Joystick_RY;
}

// AUTOINJECT
uint psiInput_GetButtons(uint i) {
    
    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    return XboxInputs.Controllers[i].buttons;
}

// AUTOINJECT
void psiInput_RumbleSetIntensity(unsigned int i, unsigned short a, unsigned short b) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    XboxInputs.Controllers[i].rumbleA = a;
    XboxInputs.Controllers[i].rumbleB = b;
    
}

// AUTOINJECT
void psiInput_RumbleStart(ushort controllerNum, int time, int intensity) {
    
    // Motor has a minimum
    if((intensity > 0) && (intensity <= 30))
        intensity = 30;
    
    if(time >= 0)
        time = time + 3;
    
    psiInput_RumbleSetIntensity(controllerNum, intensity, intensity);
    
    if(controller_maybeRumbleTimeout[controllerNum] < time)
        controller_maybeRumbleTimeout[controllerNum] = time;

    // Weird self-assignment - ternary operator of some sort?
    
}

// AUTOINJECT
void psiInput_ResetInputState(uint i) {
    
    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    XboxInputs.Controllers[i].Joystick_LX = 0.0f;
    XboxInputs.Controllers[i].Joystick_LY = 0.0f;
    XboxInputs.Controllers[i].Joystick_RX = 0.0f;
    XboxInputs.Controllers[i].Joystick_RY = 0.0f;
    XboxInputs.Controllers[i].buttons = 0;
    XboxInputs.Controllers[i].prevButtons = 0xffffffff;
    
}

// AUTOINJECT
void psiInput_ResetRumble(unsigned int i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    psiInput_RumbleSetIntensity(i, 0, 0);
    controller_maybeRumbleTimeout[i] = 0xffffffff;

}

// AUTOINJECT
void psiInputReset(void) {

    psiInput_ResetInputState(0);
    psiInput_ResetInputState(1);
    psiInput_ResetInputState(2);
    psiInput_ResetInputState(3);
    
    psiInput_ResetRumble(0);
    psiInput_ResetRumble(1);
    psiInput_ResetRumble(2);
    psiInput_ResetRumble(3);    

}