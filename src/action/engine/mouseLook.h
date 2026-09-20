#ifndef MOUSELOOK_H_
#define MOUSELOOK_H_

// ---------------------------------------------------------------------------------------------------------------
// Mouse look. The game was built for a pad and has no mouse support of any kind; this adds it as a host input
// device, in the same spirit as engine/psiInput.cpp presenting the keyboard as a virtual pad.
//
// It deliberately does NOT present the mouse as a stick, which is what a naive version would do. A stick
// reports a deflection, which the game turns into a turn rate through a deadzone (psiInput_PollDevices) and an
// acceleration ramp (AccelFunc0, in Player_Aiming and Player_Move). A mouse reports a distance already moved,
// which is an angle - it has no centre to be dead around and nothing to ramp up from, and putting it through
// either would only make it feel wrong. So the delta is applied straight to the player's aim instead; see
// Player_ViewClamping in game/obj/Player.cpp for where and why that is the right moment.
// ---------------------------------------------------------------------------------------------------------------

// Once per frame, from Input_Update - including in menus, which is when the mouse has to be let go of again.
// Handles capture and release and collects movement and button state; applies nothing by itself.
void MouseLook_Update(void);

// Whether the fire and zoom buttons are held. Both are false whenever the pointer is not captured, so a click
// in a menu, or on some other application, cannot reach the game.
//
// These are read by BuildKeyboardPadState in psiInput.cpp and folded into the virtual pad's triggers rather
// than applied to the player directly. That is the opposite of what the aiming does, and deliberately so: a
// trigger is a trigger whatever it is attached to, and going in at the pad level means the buttons inherit
// every control style, the analog-to-digital thresholds and the action flags for free. Aiming had to bypass
// all that because a mouse is not a stick; a mouse button really is just a button.
bool MouseLook_FireHeld(void);
bool MouseLook_ZoomHeld(void);

// Whether the player is currently looking down a scope, which changes what the mouse does: the wheel adjusts
// the zoom instead of changing weapon, and aiming is slowed down. Set once a frame from Player_ViewClamping,
// which is the only place that both runs every frame of play and has the player object to ask.
void MouseLook_SetScoped(bool scoped);

// Whether the pointer is currently captured, i.e. whether the player is driving with a mouse at all. Used by
// Player_Move to decide whether the scope's walk restriction applies.
bool MouseLook_Captured(void);

// One notch of the wheel, reported as a single frame's worth of "that d-pad direction is pressed" and then
// forgotten - a wheel has no held state to report, and the game acts on a press rather than while a direction
// is held. At most one notch is handed over per frame, so a fast scroll is spread across frames rather than
// skipping several weapons at once. All four are false unless the pointer is captured, and the pair that
// applies depends on whether the player is scoped.
//
// Wheel movement only arrives through raw input; there is nothing to fall back on if that is unavailable,
// because the fallback reads the cursor's position, which a wheel does not move.
bool MouseLook_NextWeapon(void);
bool MouseLook_PrevWeapon(void);
bool MouseLook_ZoomIn(void);
bool MouseLook_ZoomOut(void);

// Called from Player_ViewClamping, which the game runs once a frame for each player it is actively updating
// in a level. That makes the call itself the "we are in a level" signal the capture logic needs: menus, the
// pause screen, cutscenes and the front end all stop it by construction, with no game-state flag to consult.
//
// Returns false, leaving both outputs untouched, when the mouse is not captured or has not moved. Otherwise
// yawRadians is this frame's turn (negative turns right, matching the game's rotation), and pitchFraction is
// the change in view pitch as a fraction of a right angle, which is the unit the game stores pitch in.
bool MouseLook_TakeAimDelta(float *yawRadians, float *pitchFraction);

#endif // MOUSELOOK_H_
