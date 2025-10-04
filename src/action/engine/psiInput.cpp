#include "psiInput.h"
#include "../actionhelpers.h"
#include <string.h>

// Array of 4 uint32_t entries, all initialised to 0xFFFFFFFF
#define controller_maybeRumbleTimeout ((int*)0x0019481c)

// AUTOINJECT
bool psiInput_ControllerIsActive(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    assert(i <= 3);

    return XboxInputs.Controllers[i].controllerIndex != 0;
}

// AUTOINJECT
float psiInput_GetJoystickRX(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_RX;
}

// AUTOINJECT
float psiInput_GetJoystickLX(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_LX;
}
// AUTOINJECT
float psiInput_GetJoystickLY(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_LY;
}

// AUTOINJECT
float psiInput_GetJoystickRY(uint i) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_RY;
}

// AUTOINJECT
uint psiInput_GetButtons(uint i) {
    
    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].buttons;
}

// AUTOINJECT
void psiInput_RumbleSetIntensity(unsigned int i, unsigned short a, unsigned short b) {

    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

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
void psiInput_RumbleUpdate(void) {

    for(int i = 0; i < 4; i++) {

        // Skip if timeout has already been hit
        if(controller_maybeRumbleTimeout[i] <= -1)
            continue;

        // Decrement then check if timeout has been hit
        if(--controller_maybeRumbleTimeout[i] < 0)
            psiInput_RumbleSetIntensity(i, 0, 0);
    }
    
}

// AUTOINJECT
void psiInput_ResetInputState(uint i) {
    
    // FIXME: It's unclear what the original logic was for.
    // It should only ever be called with an input in the range 0-3
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

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
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

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

// AUTOINJECT
void psiInput_MapInputs(PlayerInput_tag* playerInputs, int maxPlayers) {
    // Only ever called from Input_Update, with:
    // psiInput_MapInputs(PlayerInputs, (GameState.CurrentLevelHashcode==HT_Level_Menu_Pre ? 4 : MPSettings.NumPlayers))

    // Some initial stuff, only ever used by P_ATTRACT_HANDLER?
    // TODO: This

    for(int i = 0; i < 4; i++) {

        // Default is that all axes are 0
        memset(playerInputs[i].fChannels, 0, sizeof(playerInputs[i].fChannels));

        // If in some pause state, stop rumble
        if(GameState.SomeAlternatePauseState) {
            psiInput_RumbleSetIntensity(i, 0, 0);
            controller_maybeRumbleTimeout[i] = -1;
        }
    }

    // Map each controller's inputs to the corresponding action / intent, according to their control scheme
    for(int i = 0; i < maxPlayers; i++) {
        
        int controllerIdx = playerInputs[i].controllerPort;
        playerInputs[i].controllerIsActive = psiInput_ControllerIsActive(controllerIdx);

        if(playerInputs[i].controllerIsActive) {
     
            switch(playerInputs[i].controlStyle) {
                default:
                case CONTROLSTYLE_NIGHTFIRE:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickRX(controllerIdx);

                    // Aim / Zoom / Scope
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    
                    // Unclear so far
                    playerInputs[i].fChannels[ACTION_4] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_3] = psiInput_GetJoystickRX(controllerIdx);

                    // Dpad Up for zoom?
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    
                    // Dpad Down for zoom?
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);

                    // Jump / Spacesuit move upwards
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    
                    // Crouch / Spacesuit move downwards
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);

                    // Fire
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);

                    // Reload?
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);

                    // Alt fire?
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_SHOULDER ? 1.0f : 0.0f);
                    
                    // Next weapon
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_RIGHT | XBOXINPUT_GAMEPAD_B) ? 1.0f : 0.0f);

                    // Prev weapon
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);

                    // Next gadget
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_UP | XBOXINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1.0f : 0.0f);
                    
                    // Prev gadget
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);

                    // Note this is split by the break statement in the code, I think this is a compiler optimisation realising that the same instruction (store in entry 13) is common.
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_BACK ? 1.0f : 0.0f);
                    
                    break;

                // TODO: Other schemes / default case should not be there...
            }

            // Common to all control schemes

            // Pause
            playerInputs[i].fChannels[ACTION_PAUSE] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_START ? 1.0f : 0.0f);
            
            // Menu Navigation - Select
            playerInputs[i].fChannels[ACTION_MENU_SELECT] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);

            // Menu Navigation - Cancel/Back
            playerInputs[i].fChannels[ACTION_MENU_BACK] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_BACK | XBOXINPUT_GAMEPAD_B) ? 1.0f : 0.0f);
            
            // Menu Navigation - Directional input
            playerInputs[i].fChannels[ACTION_MENU_DIR_UP] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP || psiInput_GetJoystickLY(controllerIdx) > 33.0f) ? 1.0f : 0.0f;
            playerInputs[i].fChannels[ACTION_MENU_DIR_DOWN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN || psiInput_GetJoystickLY(controllerIdx) < -33.0f) ? 1.0f : 0.0f;
            playerInputs[i].fChannels[ACTION_MENU_DIR_LEFT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT || psiInput_GetJoystickLX(controllerIdx) < -33.0f) ? 1.0f : 0.0f;
            playerInputs[i].fChannels[ACTION_MENU_DIR_RIGHT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_RIGHT || psiInput_GetJoystickLX(controllerIdx) > 33.0f) ? 1.0f : 0.0f;

            // ??
            playerInputs[i].fChannels[ACTION_32] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);

            // ??
            playerInputs[i].fChannels[ACTION_33] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_B ? 1.0f : 0.0f);

            // Skip cutscene
            playerInputs[i].fChannels[ACTION_SKIP_CUTSCENE] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_START | XBOXINPUT_GAMEPAD_A | XBOXINPUT_GAMEPAD_B) ? 1.0f : 0.0f);

            // Axis scaling
            playerInputs[i].fChannels[ACTION_WALK_F_B] *= 0.01f;
            playerInputs[i].fChannels[ACTION_WALK_L_R] *= -0.01f; // Flip axis too
            playerInputs[i].fChannels[ACTION_AIM_U_D] *= 0.01f;
            playerInputs[i].fChannels[ACTION_AIM_L_R] *= 0.01f;
            playerInputs[i].fChannels[ACTION_4] *= 0.01f;
            playerInputs[i].fChannels[ACTION_3] *= 0.01f;

            // Zoom is (ZoomIn - ZoomOut)
            playerInputs[i].fChannels[ACTION_SCOPE_ZOOM] = -(playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] - playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT]);
            playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = 0.0f;
            playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = 0.0f;

            // ?
            playerInputs[i].fChannels[ACTION_24] *= -1.0f;

            
        }

    }

}
