#include "mouseSteer.h"

#include "mouseLook.h"
#include "../input.h"
#include "../game/obj/Player.h"

// See mouseSteer.h for what this is for and the table of devices.

#define ACTION_HELD 1 // PlayerInput.actions bit that Input_Actionf(..., 1) tests

typedef struct {
    GameActions_tag right;      // the channel whose positive deflection turns the device right
    GameActions_tag up;         // and the one that turns it up
    float fullTurnPerFrame;     // radians per 60 Hz frame at full deflection, from the device's update
} SteerChannels;

static const SteerChannels SteerSentinelMissile = { ACTION_AIM_L_R, ACTION_AIM_U_D, 0.024543693f };
static const SteerChannels SteerRCVehicle = { ACTION_AIM_L_R, ACTION_AIM_U_D, 0.05f };
static const SteerChannels SteerEmplacement = { ACTION_TURRET_AIM_X, ACTION_TURRET_AIM_Y, 0.015f };
static const SteerChannels SteerRonin = { ACTION_TURRET_AIM_X, ACTION_TURRET_AIM_Y, 0.024543693f };

bool MouseSteer_IsRemoteControl(short subState) {
    return subState == MovementType_RemoteControl || subState == MovementType_RCVehicle
        || subState == MovementType_Emplacement || subState == MovementType_Ronin;
}

static const SteerChannels *ChannelsFor(short subState) {
    switch (subState) {
        case MovementType_RemoteControl:
            return &SteerSentinelMissile;
        case MovementType_RCVehicle:
            // The helicopter flies on the aim stick and the tank aims its turret with it; both turn at the same
            // rate (Car_Update, Car_SetBits).
            return &SteerRCVehicle;
        case MovementType_Emplacement:
            return &SteerEmplacement;
        case MovementType_Ronin:
            return &SteerRonin;
    }
    return NULL;
}

// Adds deflection to one of player 0's channels as a stick would, so the game cannot tell the two apart: a
// channel the game has not marked as held counts as centred, whatever value was left in it.
static void AddDeflection(GameActions_tag action, float deflection) {
    PlayerInput *input = &PlayerInputs[0];

    float stick = (input->actions[action] & ACTION_HELD) ? input->fChannels[action] : 0.0f;
    stick += deflection;
    if (stick > 1.0f)
        stick = 1.0f;
    else if (stick < -1.0f)
        stick = -1.0f;

    input->fChannels[action] = stick;
    if (stick != 0.0f)
        input->actions[action] |= ACTION_HELD;
}

void MouseSteer_Update(void) {
    obj_tag *player = glb_players[0];
    if (player == NULL || player->objectType != OBJECTTYPE_PLAYER)
        return;

    BLData *blData = (BLData*)player->extraObjectData;
    obj_tag *device = blData->remoteControlDevice;
    if (device == NULL)
        return;

    const SteerChannels *channels = ChannelsFor(player->subState);
    if (channels == NULL)
        return;

    float right, up;
    if (!MouseLook_TakeStick(channels->fullTurnPerFrame * 60.0f, REC_FRAME_RATE, &right, &up))
        return;

    AddDeflection(channels->right, right);
    AddDeflection(channels->up, up);
}
