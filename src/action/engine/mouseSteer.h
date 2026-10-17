#ifndef MOUSESTEER_H_
#define MOUSESTEER_H_

#include "../actionhelpers.h"

// ---------------------------------------------------------------------------------------------------------------
// The mouse for everything the player controls remotely: the Sentinel's guided missile, RC tanks and helicopters,
// gun emplacements and the Ronin.
//
// Each of these steers itself from a stick, in its own update function, by turning stick deflection times a fixed
// rate per frame - with no deadzone or acceleration ramp on the way, unlike the player's own aiming (see
// engine/mouseLook.h for why that one had to be bypassed). So here the mouse becomes that stick: an elastic one
// (MouseLook_TakeStick), pushed over by dragging and springing back to centre when the drag stops, whose
// deflection is added to player 0's channels on top of whatever the pad or keyboard is doing, after the game's
// own poll and before anything reads them. Every device then turns through its own original code, at its own
// rate, with its own limits - the only thing that knows about the mouse is this file.
//
// The virtual stick never goes past full deflection, and the sum with the real stick is clamped the same way,
// so nothing turns faster with the mouse than the pad could turn it.
//
//   device              substate      mouse moves (channels)               full deflection, per 60 Hz frame
//   Sentinel missile    10            the missile (AIM_L_R, AIM_U_D)       pi/128 rad  (Bullet_Update)
//   RC helicopter       11            the helicopter (AIM_L_R, AIM_U_D)    0.05 rad    (Car_Update)
//   RC tank             11            the turret (AIM_L_R, AIM_U_D)        0.05 rad    (Car_SetBits; the body is
//                                                                                      driven on the walk stick,
//                                                                                      WASD on the keyboard)
//   gun emplacement     12            the gun (TURRET_AIM_X, _Y)           0.015 rad   (GunImp_Update)
//   Ronin               16            the turret (TURRET_AIM_X, _Y)        pi/128 rad  (GT_Update, state 7)
//
// Player 0 only: there is one mouse, and split-screen players are on pads.
// ---------------------------------------------------------------------------------------------------------------

// Whether a player in this substate is controlling something remotely, and so is not aiming the view: the mouse
// is left for MouseSteer_Update instead of turning the player. Player_ViewClamping asks.
bool MouseSteer_IsRemoteControl(short subState);

// Once a frame, from Input_Update, straight after the game has polled its pads.
void MouseSteer_Update(void);

#endif // MOUSESTEER_H_
