#include "mouseLook.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "XboxSettings.h"
#include "../../driving/platform/HostInput.h"
#include "../devtools/Demo.h"

// ---------------------------------------------------------------------------------------------------------------
// See mouseLook.h for what this is for. How the movement is read and the pointer captured is the host's
// (src/driving/platform/HostInput.h: raw input and a clipped, hidden pointer on Win32, relative mouse mode on SDL);
// what counts, and when the pointer is held, is decided here.
// ---------------------------------------------------------------------------------------------------------------

// How many frames the aim hook may go missing before the capture is dropped. It runs every frame of normal
// play, so this only needs to cover a frame the game skips for its own reasons rather than because a menu
// opened - one would probably do, and five costs a tenth of a second of holding on too long.
#define IN_LEVEL_GRACE_FRAMES 5

// Radians per raw count at MouseSensitivity=1. About 0.086 degrees, so a 360 degree turn is roughly 4200
// counts - a little over five inches on an 800 DPI mouse, which is in the usual range for a shooter and is
// what the setting exists to move.
#define RADIANS_PER_COUNT 0.0015f

#define TWO_OVER_PI 0.63661977f

// Multipliers on the configured sensitivity, chosen so that scoped aiming is a quarter of the speed
// of hip fire rather than the same.
#define HIPFIRE_SENSITIVITY 2.0f
#define SCOPED_SENSITIVITY 0.5f

// How quickly MouseLook_TakeStick's virtual stick springs back to centre once the mouse stops: the time constant
// of its return, in seconds. About three of these and it is back where it started - a quarter of a second, which
// is long enough to smooth the jitter between one frame's movement and the next, and short enough that the stick
// feels held by the hand rather than left wherever it was pushed.
#define STICK_RETURN_SECONDS 0.08f

// A notch of the wheel, in the host's units (HostMouseState)
#define WHEEL_NOTCH 120

static bool    g_captured = false;

static int32_t  g_accumX = 0, g_accumY = 0; // raw counts the game has not consumed yet
static unsigned g_frame = 0;                // incremented once per MouseLook_Update
static unsigned g_lastAimFrame = 0;         // g_frame the last time the aim hook ran; 0 = never
static bool     g_prevButtonDown = false, g_prevEscapeDown = false;
static bool     g_leftDown = false, g_rightDown = false; // as of the last MouseLook_Update
static bool     g_prevRightDown = false;
static bool     g_zoomLatched = false;                   // MouseZoom=toggle: the trigger as the clicks left it
static bool     g_swallowLeftUntilRelease = false;       // see Capture
static int      g_wheelAccum = 0;                        // raw wheel movement not yet turned into notches
static int      g_wheelStep = 0;                         // -1, 0 or +1, for this frame only
static bool     g_scoped = false;                        // as of the last Player_ViewClamping
static float    g_stickRight = 0.0f, g_stickUp = 0.0f;   // MouseLook_TakeStick's deflection, -1 to 1

static void Capture(void) {
    bool hidden = HostMouse_Capture(true);

    g_accumX = 0;
    g_accumY = 0;
    g_captured = true;

    // The click that takes control must not also pull the trigger, so the fire button stays suppressed until
    // it is let go of. Without this, every single time the pointer is captured the player fires a round.
    g_swallowLeftUntilRelease = true;

    printf("[mouse] captured - Escape releases it%s\n", hidden ? "" : " (the pointer could not be hidden)");
}

static void Release(void) {
    if (!g_captured)
        return;

    HostMouse_Capture(false);

    g_accumX = 0;
    g_accumY = 0;
    g_wheelAccum = 0;
    g_wheelStep = 0;
    g_scoped = false;
    g_zoomLatched = false;
    g_stickRight = 0.0f;
    g_stickUp = 0.0f;
    g_captured = false;
    printf("[mouse] released\n");
}

// Whether the game is actually playing a level, which is the only time it makes sense to hold the pointer.
// See the comment on MouseLook_TakeAimDelta in the header for why this is the signal rather than a flag.
static bool InLevel(void) {
    return g_lastAimFrame != 0 && (g_frame - g_lastAimFrame) <= IN_LEVEL_GRACE_FRAMES;
}

void MouseLook_Update(void) {
    g_frame++;

    if (!Settings_GetMouseLook()) {
        Release();
        return;
    }

    HostMouseState mouse;
    HostMouse_Read(&mouse);
    g_accumX += mouse.dx;
    g_accumY += mouse.dy;
    g_wheelAccum += mouse.wheel;

    if (!HostMouse_HasWindow()) {
        Release();
        return;
    }

    // Both are edge-triggered - holding Escape must not keep the capture off, and holding the button must not
    // re-take it the instant it is released.
    bool escapeDown = HostInput_KeyHeld(kKeyEscape);
    bool buttonDown = mouse.left;

    // Read here rather than from psiInput.cpp so that everything the mouse reports is read at one point in the
    // frame, and so that the capture rules are the only thing deciding whether any of it counts.
    g_leftDown = buttonDown;
    g_rightDown = mouse.right;
    bool rightPressed = g_rightDown && !g_prevRightDown;
    g_prevRightDown = g_rightDown;
    if (!buttonDown)
        g_swallowLeftUntilRelease = false;

    bool escapePressed = escapeDown && !g_prevEscapeDown;
    bool buttonPressed = buttonDown && !g_prevButtonDown;
    g_prevEscapeDown = escapeDown;
    g_prevButtonDown = buttonDown;

    if (!g_captured) {
        g_accumX = 0;
        g_accumY = 0;
        g_wheelAccum = 0;
        g_wheelStep = 0;
        if (buttonPressed && InLevel() && HostInput_WindowHasFocus() && HostMouse_InWindow())
            Capture();
        return;
    }

    // Escape is also the keyboard's START button, so it opens the pause menu at the same time - which would
    // release the capture a frame later anyway, through InLevel. Handling it directly as well means the
    // pointer comes back immediately, and that it still comes back if the game declines to pause.
    if (escapePressed || !InLevel() || !HostInput_WindowHasFocus()) {
        Release();
        return;
    }

    if (rightPressed)
        g_zoomLatched = !g_zoomLatched;

    // One notch per frame, so that a flick of the wheel walks through the weapons one at a time instead of
    // jumping several at once - the game changes weapon on a press, and several presses in one frame would
    // be indistinguishable from one.
    g_wheelStep = 0;
    if (g_wheelAccum >= WHEEL_NOTCH) {
        g_wheelStep = 1;
        g_wheelAccum -= WHEEL_NOTCH;
    }
    else if (g_wheelAccum <= -WHEEL_NOTCH) {
        g_wheelStep = -1;
        g_wheelAccum += WHEEL_NOTCH;
    }
}

void MouseLook_DemoExchange(void) {
    Demo_Mouse(&g_captured, &g_accumX, &g_accumY);
}

bool MouseLook_FireHeld(void) {
    return g_captured && g_leftDown && !g_swallowLeftUntilRelease;
}

bool MouseLook_ZoomHeld(bool gameTogglesScope) {
    if (!g_captured)
        return false;
    if (Settings_GetMouseZoomToggle() && !gameTogglesScope)
        return g_zoomLatched;
    return g_rightDown;
}

void MouseLook_SetScoped(bool scoped) {
    g_scoped = scoped;
}

bool MouseLook_NextWeapon(void) {
    return g_captured && !g_scoped && g_wheelStep > 0;
}

bool MouseLook_PrevWeapon(void) {
    return g_captured && !g_scoped && g_wheelStep < 0;
}

bool MouseLook_ZoomIn(void) {
    return g_captured && g_scoped && g_wheelStep > 0;
}

bool MouseLook_ZoomOut(void) {
    return g_captured && g_scoped && g_wheelStep < 0;
}

bool MouseLook_TakeAimDelta(float *yawRadians, float *pitchFraction) {
    g_lastAimFrame = g_frame;

    // The stick belongs to a device that has been let go of; the next one starts from centre.
    g_stickRight = 0.0f;
    g_stickUp = 0.0f;

    if (!g_captured)
        return false;

    int32_t dx = g_accumX, dy = g_accumY;
    g_accumX = 0;
    g_accumY = 0;
    if (dx == 0 && dy == 0)
        return false;

    // No frame-time term anywhere in here, on purpose. These are counts the mouse has already moved, so the
    // angle they are worth is the same whether they arrived over one long frame or several short ones - and
    // scaling them by frame time would make the same movement turn a different amount depending on load.
    // Down a scope the view covers a much smaller angle, so the same hand movement should turn the player
    // less; everywhere else the original default was on the slow side for a mouse. The setting keeps meaning
    // hip-fire speed, and these two multipliers hang off it.
    float radiansPerCount = RADIANS_PER_COUNT * Settings_GetMouseSensitivity()
                          * (g_scoped ? SCOPED_SENSITIVITY : HIPFIRE_SENSITIVITY);

    // Screen axes against game axes: x grows to the right while the object's rotation grows turning left, and
    // y grows downwards while pitch grows looking up. Both are therefore negated.
    float yaw = -(float)dx * radiansPerCount;
    float pitch = -(float)dy * radiansPerCount;
    if (Settings_GetMouseInvertY())
        pitch = -pitch;

    *yawRadians = yaw;
    *pitchFraction = pitch * TWO_OVER_PI; // the game stores pitch as a fraction of a right angle, not radians
    return true;
}

void MouseLook_LeaveForSteering(void) {
    g_lastAimFrame = g_frame;
}

bool MouseLook_TakeStick(float fullTurnPerSecond, float seconds, float *right, float *up) {
    if (!g_captured)
        return false;

    int32_t dx = g_accumX, dy = g_accumY;
    g_accumX = 0;
    g_accumY = 0;

    // How fast the mouse is moving, as the turn it would make aiming at hip-fire speed. Signs as in
    // MouseLook_TakeAimDelta, but kept the way a person would say them - right and up.
    float radiansPerCount = RADIANS_PER_COUNT * Settings_GetMouseSensitivity() * HIPFIRE_SENSITIVITY;
    float rightSpeed = (float)dx * radiansPerCount / seconds;
    float upSpeed = -(float)dy * radiansPerCount / seconds;
    if (Settings_GetMouseInvertY())
        upSpeed = -upSpeed;

    // The deflection that would turn the device at that speed, which the stick eases towards: a steady drag
    // holds it there, so the device turns just as aiming would have, and when the drag stops the target is
    // centre, so the stick springs back. Eased by elapsed time rather than per frame, so the feel is the same
    // at any frame rate.
    float ease = 1.0f - expf(-seconds / STICK_RETURN_SECONDS);
    g_stickRight += (rightSpeed / fullTurnPerSecond - g_stickRight) * ease;
    g_stickUp += (upSpeed / fullTurnPerSecond - g_stickUp) * ease;

    // A round gate, as a pad's stick has: never further than full deflection, in any direction. This is the
    // limit that keeps the mouse fair - however hard it is flung, the device turns no faster than its stick
    // can turn it - and holding the stick itself back here means it comes home promptly after a flick.
    float length = sqrtf(g_stickRight * g_stickRight + g_stickUp * g_stickUp);
    if (length > 1.0f) {
        g_stickRight /= length;
        g_stickUp /= length;
    }
    else if (length < 0.001f) {
        g_stickRight = 0.0f;
        g_stickUp = 0.0f;
        return false;
    }

    *right = g_stickRight;
    *up = g_stickUp;
    return true;
}
