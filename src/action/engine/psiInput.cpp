#include "psiInput.h"
#include "../actionhelpers.h"
#include "../../common/renderWindow.h"
#include "mouseLook.h"
#include <string.h>
#include <math.h>

// ---------------------------------------------------------------------------------------------------------------
// Real hardware polling - talks to the host's actual gamepads via Win32 XInput, instead of going through
// CXBX's emulation of the original Xbox kernel's XAPILIB device layer (XInitDevices/XGetDevices/XInputOpen/
// XGetDeviceChanges/XInputGetState/XInputSetState/XInputClose - on real hardware, all backed by IOCTLs
// against the Xbox's USB gamepad driver). xboxInitInputDevices and psiInput_PollDevices below replace that
// whole chain; everything else in this file (psiInput_GetJoystickLX and friends, psiInput_MapInputs, ...)
// is unmodified and keeps reading/writing the same XboxInputs global exactly as before.
//
// We declare the real XInputGetState/XInputSetState ourselves against locally-named struct shapes rather
// than including <xinput.h>, because the real Win32 XINPUT_STATE/XINPUT_GAMEPAD/XINPUT_VIBRATION names
// collide with this codebase's Xbox-shaped versions of the same names (see xinput_xbox.h, included via
// psiInput.h) - which differ in layout (the original Xbox pad exposed 6 analog buttons instead of 2 analog
// triggers, among other things).
// ---------------------------------------------------------------------------------------------------------------

#pragma pack(push, 1)
struct Win32_XINPUT_GAMEPAD {
    unsigned short wButtons;
    unsigned char  bLeftTrigger;
    unsigned char  bRightTrigger;
    short          sThumbLX;
    short          sThumbLY;
    short          sThumbRX;
    short          sThumbRY;
};

struct Win32_XINPUT_STATE {
    unsigned long        dwPacketNumber;
    Win32_XINPUT_GAMEPAD Gamepad;
};

struct Win32_XINPUT_VIBRATION {
    unsigned short wLeftMotorSpeed;
    unsigned short wRightMotorSpeed;
};
#pragma pack(pop)

#define WIN32_XINPUT_GAMEPAD_A              0x1000
#define WIN32_XINPUT_GAMEPAD_B              0x2000
#define WIN32_XINPUT_GAMEPAD_X              0x4000
#define WIN32_XINPUT_GAMEPAD_Y              0x8000
#define WIN32_XINPUT_GAMEPAD_LEFT_SHOULDER  0x0100
#define WIN32_XINPUT_GAMEPAD_RIGHT_SHOULDER 0x0200

extern "C" __declspec(dllimport) unsigned long __stdcall XInputGetState(unsigned long dwUserIndex, Win32_XINPUT_STATE *pState);
extern "C" __declspec(dllimport) unsigned long __stdcall XInputSetState(unsigned long dwUserIndex, Win32_XINPUT_VIBRATION *pVibration);
#pragma comment(lib, "xinput9_1_0.lib")

#define WIN32_ERROR_SUCCESS 0

// ---------------------------------------------------------------------------------------------------------------
// Keyboard fallback - a virtual Xbox pad on port 0, used only when no real pad is plugged into port 0.
//
// This deliberately synthesizes a Win32_XINPUT_STATE and lets it fall through the rest of psiInput_PollDevices
// untouched, rather than writing game action channels directly (which is what the older Inject_KeyboardInput in
// input.cpp did). Everything downstream then behaves exactly as it does for a real pad: the deadzone and gain
// curves, the analog-to-digital button thresholds, all four control styles, and - the part that matters most -
// psiInput_MapInputs, which only maps anything at all for a player whose controllerIsActive is set, and which
// derives the action flags the game reads. Pretending to be a pad at the lowest level is both less code and
// less guesswork than reproducing that mapping.
//
// As a side effect this is also why the game no longer stops at "no controller present": port 0 now always has
// something on it. A real pad appearing later takes over on the next poll.
//
// The bindings are a keyboard reading of the default NIGHTFIRE control style - see psiInput_MapInputs for what
// each pad button actually does, and note that several are deliberately overloaded exactly as they are on the
// pad (A is crouch in game and select in menus, START is pause and skip-cutscene, and so on).
//
//   W / A / S / D          left stick   - move and strafe
//   arrow keys             right stick  - look (60% deflection, full with Shift held)
//   Left Ctrl  or  mouse 1 right trigger - fire        (the mouse only while the pointer is captured)
//   Z                      left shoulder - alternate fire
//   X          or  mouse 2 left trigger  - scope zoom   (likewise)
//   Space                  Y  - jump                 (also menu alt-select 1)
//   C  or  Return          A  - crouch               (also menu select)
//   R  or  E               X  - reload and interact   (also menu alt-select 2)
//   Backspace              B  - next weapon          (also menu back)
//   N                      BACK - night vision       (also menu back)
//   P  or  Escape          START - pause             (also skip cutscene)
//   Q  or  wheel down      d-pad left  - previous weapon
//   wheel up               d-pad right - next weapon
//   1 / 2                  d-pad down/up    - previous / next gadget, and scope zoom out / in
//
// E is "interact" as much as "reload": Player_WeaponFiring calls Player_Activate - doors, triggers, cars,
// turrets, monitors, locks - when ACTION_RELOAD has just been pressed and fire is not held. The game has no
// separate use button, so a key that opens doors necessarily reloads as well, exactly as the pad's X does.
//
// The weapon d-pad used to be Q and E, and E was taken for the above once the wheel could do the same job.
// Next weapon is still on Backspace (the B button) for anyone without a wheel.
//
// In menus the left stick doubles as the directional input (psiInput_MapInputs treats a stick deflection past
// 33% the same as a d-pad press), so WASD navigates menus as well as walking.
// ---------------------------------------------------------------------------------------------------------------

extern "C" __declspec(dllimport) short __stdcall GetAsyncKeyState(int vKey);
extern "C" __declspec(dllimport) void *__stdcall GetForegroundWindow(void);
extern "C" __declspec(dllimport) void *__stdcall FindWindowA(const char *lpClassName, const char *lpWindowName);
extern "C" __declspec(dllimport) void *__stdcall GetAncestor(void *hWnd, unsigned int gaFlags);
#pragma comment(lib, "user32.lib")

#define VK_BACKSPACE 0x08
#define VK_RETURN_   0x0D
#define VK_SHIFT_    0x10
#define VK_ESCAPE_   0x1B
#define VK_SPACE_    0x20
#define VK_LEFT_     0x25
#define VK_UP_       0x26
#define VK_RIGHT_    0x27
#define VK_DOWN_     0x28
#define VK_LCONTROL_ 0xA2
#define GA_ROOT_     2

// GetAsyncKeyState rather than GetKeyState: the game loop thread does not pump a message queue of its own, and
// GetKeyState reports the state as of the calling thread's last message - which on this thread never updates.
static inline bool KeyDown(int vk) {
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

// Only feed the keyboard in when the game's own window is in front, so keys typed into another application do
// not drive Bond around. The window belongs to the launcher process (and the render window is CXBX's child of
// it), not to the process this DLL is injected into, so this goes via the window handle rather than the pid. If
// the render window cannot be found at all, fail open - better to accept stray keystrokes than to leave someone
// with no working input and no way to tell why.
static bool KeyboardPadHasFocus(void) {
    // Either host's window: CXBX's, or the one the loader creates when running standalone.
    void *render = FindWindowA("CxbxRender", NULL);
    if (render == NULL)
        render = FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
    if (render == NULL)
        return true;
    void *root = GetAncestor(render, GA_ROOT_);
    void *foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == root);
}

// Fills in state as though a pad were connected, and returns whether any key is actually being held. The state
// is always fully written, so a caller can use it regardless of the return value.
static bool BuildKeyboardPadState(Win32_XINPUT_STATE *state) {
    memset(state, 0, sizeof(*state));

    if (!KeyboardPadHasFocus())
        return false;

    // A counter is enough for dwPacketNumber: the only thing that reads it is the original's own
    // "has anything changed" bookkeeping, which just wants it to move when the state does.
    static unsigned long packet = 0;
    state->dwPacketNumber = ++packet;

    const short kFull = 32767;
    const short kLook = (short)(KeyDown(VK_SHIFT_) ? 32767 : 19660); // ~60% unless Shift is held

    short moveX = 0, moveY = 0, lookX = 0, lookY = 0;
    if (KeyDown('D')) moveX += kFull;
    if (KeyDown('A')) moveX -= kFull;
    if (KeyDown('W')) moveY += kFull;
    if (KeyDown('S')) moveY -= kFull;
    if (KeyDown(VK_RIGHT_)) lookX += kLook;
    if (KeyDown(VK_LEFT_))  lookX -= kLook;
    if (KeyDown(VK_UP_))    lookY += kLook;
    if (KeyDown(VK_DOWN_))  lookY -= kLook;

    state->Gamepad.sThumbLX = moveX;
    state->Gamepad.sThumbLY = moveY;
    state->Gamepad.sThumbRX = lookX;
    state->Gamepad.sThumbRY = lookY;

    unsigned short buttons = 0;
    if (KeyDown('C') || KeyDown(VK_RETURN_)) buttons |= WIN32_XINPUT_GAMEPAD_A;
    if (KeyDown(VK_BACKSPACE))               buttons |= WIN32_XINPUT_GAMEPAD_B;
    if (KeyDown('R') || KeyDown('E'))        buttons |= WIN32_XINPUT_GAMEPAD_X;
    if (KeyDown(VK_SPACE_))                  buttons |= WIN32_XINPUT_GAMEPAD_Y;
    if (KeyDown('Z'))                        buttons |= WIN32_XINPUT_GAMEPAD_LEFT_SHOULDER;
    if (KeyDown('P') || KeyDown(VK_ESCAPE_)) buttons |= XINPUT_GAMEPAD_START;
    if (KeyDown('N'))                        buttons |= XINPUT_GAMEPAD_BACK;
    if (MouseLook_NextWeapon())              buttons |= XINPUT_GAMEPAD_DPAD_RIGHT;
    if (KeyDown('Q') || MouseLook_PrevWeapon()) buttons |= XINPUT_GAMEPAD_DPAD_LEFT;
    if (KeyDown('2'))                        buttons |= XINPUT_GAMEPAD_DPAD_UP;
    if (KeyDown('1'))                        buttons |= XINPUT_GAMEPAD_DPAD_DOWN;
    state->Gamepad.wButtons = buttons;

    // The mouse buttons come in here rather than being applied to the player directly, so that they go
    // through the same mapping as every other button - see the comment on MouseLook_FireHeld. Both are held
    // false unless the pointer is captured, so this cannot pick up a click meant for something else.
    state->Gamepad.bRightTrigger = (KeyDown(VK_LCONTROL_) || MouseLook_FireHeld()) ? 0xFF : 0; // fire
    state->Gamepad.bLeftTrigger  = (KeyDown('X') || MouseLook_ZoomHeld()) ? 0xFF : 0;          // scope zoom

    return buttons != 0 || moveX != 0 || moveY != 0 || lookX != 0 || lookY != 0 ||
           state->Gamepad.bLeftTrigger != 0 || state->Gamepad.bRightTrigger != 0;
}

// AUTOINJECT
void xboxInitInputDevices(void) {
    memset(&XboxInputs, 0, sizeof(XboxInputs));
    XboxInputs.Initialised = true;
}

// Faithfully reproduces the original's per-axis "sticky" deadzone widening: once an axis saturates past
// +-90 (out of the -100..100 range produced further down), a 5-frame countdown starts, and while it's
// running the deadzone threshold used below widens from 30 to 60 - this makes it easier to stay pinned at
// full deflection without a stick's natural jitter dipping back under the (otherwise much tighter)
// deadzone. counterA's countdown restarts while axisValue is pinned to the negative extreme, counterB's
// while it's pinned to the positive extreme (matching the original's two independent per-direction
// countdowns per axis).
static float UpdateAxisSnapThreshold(float axisValue, int *counterA, int *counterB) {
    if (*counterA <= 0 && *counterB <= 0)
        return 30.0f;

    if (*counterA > 0) {
        int c = *counterA - 1;
        if (axisValue >= -90.0f) c = 5;
        *counterA = c;
    }
    if (*counterB > 0) {
        int c = *counterB - 1;
        if (axisValue <= 90.0f) c = 5;
        *counterB = c;
    }
    return 60.0f;
}

// Applies the deadzone (anything within +-threshold collapses to 0) then rescales the remaining range back
// out to +-100 - exactly as the original does per-axis below.
static float ApplyDeadzoneAndRescale(float raw, float threshold) {
    float v;
    if (raw >= -threshold && raw <= threshold)
        v = 0.0f;
    else if (raw > threshold)
        v = raw - threshold;
    else
        v = raw + threshold;
    return (v / (100.0f - threshold)) * 100.0f;
}

// Real signature/behaviour: see the block comment above. Called once per frame by the original (untouched)
// Input_Update, and again in a drain loop by the original (untouched) maybeInputShutdown to let rumble
// motors spin down before handing off to another engine.
//
// AUTOINJECT
void psiInput_PollDevices(void) {
    if (!XboxInputs.Initialised)
        return;

    for (int i = 0; i < 4; i++) {
        ControllerStateStruct *c = &XboxInputs.Controllers[i];

        Win32_XINPUT_STATE winState;
        memset(&winState, 0, sizeof(winState));
        bool connected = XInputGetState((unsigned long)i, &winState) == WIN32_ERROR_SUCCESS;

        // No real pad on port 0: present the keyboard as one instead (see BuildKeyboardPadState above). Note
        // this claims "connected" whether or not a key is currently held - a pad that is plugged in but idle is
        // still a connected pad, and the game needs port 0 occupied to let you past the front end.
        if (!connected && i == 0) {
            BuildKeyboardPadState(&winState);
            connected = true;
        }

        if (!connected) {
            c->controllerIndex = 0;
            c->controllerState.dwPacketNumber = 0;
            memset(&c->controllerState.Gamepad, 0, sizeof(c->controllerState.Gamepad));
        } else {
            // Any nonzero value marks "connected" here - the original's real device handle is never
            // otherwise inspected by anything reimplemented in this file, only ever compared against 0.
            c->controllerIndex = (uint)(i + 1);
            c->controllerState.dwPacketNumber = winState.dwPacketNumber;

            // The low 8 bits (D-pad/start/back/thumbstick-click) sit at the same bit positions in both the
            // real Win32 layout and the original Xbox one - see xinput_xbox.h.
            c->controllerState.Gamepad.wButtons = winState.Gamepad.wButtons & 0x00FF;

            // The original Xbox pad exposed A/B/X/Y/BLACK/WHITE/triggers as 6+2 analog bytes rather than
            // digital bits (see xinput_xbox.h) - synthesize those from the real pad's digital face buttons
            // (full-scale 0 or 255) and real analog triggers (already 0-255). Index 5 (WHITE, i.e. the left
            // shoulder on a modern pad) is wired like the others: leaving it out (an earlier reading of the
            // original's unrolled per-button code) lost the alternate-fire button, which the game maps to
            // XBOXINPUT_GAMEPAD_LEFT_SHOULDER further down in this file.
            unsigned char *analog = (unsigned char*)c->controllerState.Gamepad.bAnalogButtons;
            analog[0] = (winState.Gamepad.wButtons & WIN32_XINPUT_GAMEPAD_A) ? 0xFF : 0;
            analog[1] = (winState.Gamepad.wButtons & WIN32_XINPUT_GAMEPAD_B) ? 0xFF : 0;
            analog[2] = (winState.Gamepad.wButtons & WIN32_XINPUT_GAMEPAD_X) ? 0xFF : 0;
            analog[3] = (winState.Gamepad.wButtons & WIN32_XINPUT_GAMEPAD_Y) ? 0xFF : 0;
            analog[4] = (winState.Gamepad.wButtons & WIN32_XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 0xFF : 0;
            analog[5] = (winState.Gamepad.wButtons & WIN32_XINPUT_GAMEPAD_LEFT_SHOULDER) ? 0xFF : 0;
            analog[6] = winState.Gamepad.bLeftTrigger;
            analog[7] = winState.Gamepad.bRightTrigger;

            c->controllerState.Gamepad.sThumbLX = winState.Gamepad.sThumbLX;
            c->controllerState.Gamepad.sThumbLY = winState.Gamepad.sThumbLY;
            c->controllerState.Gamepad.sThumbRX = winState.Gamepad.sThumbRX;
            c->controllerState.Gamepad.sThumbRY = winState.Gamepad.sThumbRY;
        }

        // --- from here down, faithfully transliterated from the original polling function (0x000e76a0) ---

        float lx = (float)(int)c->controllerState.Gamepad.sThumbLX * 0.0030518044f + 0.0015259022f;
        float ly = (float)(int)c->controllerState.Gamepad.sThumbLY * 0.0030518044f + 0.0015259022f;
        float rx = (float)(int)c->controllerState.Gamepad.sThumbRX * 0.0030518044f + 0.0015259022f;
        float ry = (float)(int)c->controllerState.Gamepad.sThumbRY * 0.0030518044f + 0.0015259022f;

        // unknown[8] (offsets 0x88-0xa4, in pairs per axis) are the snap-to-edge countdowns above;
        // pad_5[4] (offsets 0x84-0x87, one byte per axis in LX,LY,RX,RY order) are the "locked" flags
        // handled further down.
        float lxThreshold = UpdateAxisSnapThreshold(lx, &c->unknown[0], &c->unknown[1]);
        float lyThreshold = UpdateAxisSnapThreshold(ly, &c->unknown[3], &c->unknown[2]);
        float rxThreshold = UpdateAxisSnapThreshold(rx, &c->unknown[4], &c->unknown[5]);
        float ryThreshold = UpdateAxisSnapThreshold(ry, &c->unknown[7], &c->unknown[6]);

        lx = ApplyDeadzoneAndRescale(lx, lxThreshold);
        ly = ApplyDeadzoneAndRescale(ly, lyThreshold);
        rx = ApplyDeadzoneAndRescale(rx, rxThreshold);
        ry = ApplyDeadzoneAndRescale(ry, ryThreshold);

        // Extra gain, then clamp each stick's magnitude (not just its individual axes) to 100.
        lx *= 1.2f;
        ly *= 1.2f;
        float lMag = sqrtf(lx * lx + ly * ly);
        if (lMag > 100.0f) {
            lx *= 100.0f / lMag;
            ly *= 100.0f / lMag;
        }

        rx *= 1.2f;
        ry *= 1.2f;
        float rMag = sqrtf(rx * rx + ry * ry);
        if (rMag > 100.0f) {
            rx *= 100.0f / rMag;
            ry *= 100.0f / rMag;
        }

        c->Joystick_LX = lx;
        c->Joystick_LY = ly;
        c->Joystick_RX = rx;
        c->Joystick_RY = ry;

        // Digital button mask: low 8 bits are D-pad/start/back/thumbclick (already copied above); the rest
        // are synthesized from the analog bytes at a ~5.5% (buttons) / ~11% (triggers) threshold, exactly
        // matching the original's fixed-point 0xe/0x1c comparisons against (byte*100)>>8.
        unsigned char *analog = (unsigned char*)c->controllerState.Gamepad.bAnalogButtons;
        unsigned int buttons = c->controllerState.Gamepad.wButtons;
        buttons |= ((analog[0] * 100) >> 8) > 0xe ? XBOXINPUT_GAMEPAD_A : 0;
        buttons |= ((analog[1] * 100) >> 8) > 0xe ? XBOXINPUT_GAMEPAD_B : 0;
        buttons |= ((analog[2] * 100) >> 8) > 0xe ? XBOXINPUT_GAMEPAD_X : 0;
        buttons |= ((analog[3] * 100) >> 8) > 0xe ? XBOXINPUT_GAMEPAD_Y : 0;
        buttons |= ((analog[4] * 100) >> 8) > 0xe ? XBOXINPUT_GAMEPAD_RIGHT_SHOULDER : 0;
        buttons |= ((analog[5] * 100) >> 8) > 0xe ? XBOXINPUT_GAMEPAD_LEFT_SHOULDER : 0;
        buttons |= ((analog[6] * 100) >> 8) > 0x1c ? XBOXINPUT_GAMEPAD_LEFT_TRIGGER : 0;
        buttons |= ((analog[7] * 100) >> 8) > 0x1c ? XBOXINPUT_GAMEPAD_RIGHT_TRIGGER : 0;
        c->buttons = buttons;

        // Per-axis "locked" flag: once set, holds that axis at 0 until it drops back under 80 (of the
        // -100..100 range above). We haven't found anything in this binary that ever sets pad_5[0..3]
        // nonzero, so in practice this is inert - kept here for fidelity in case that's wrong.
        if (c->pad_5[0] != 0) { if (fabsf(c->Joystick_LX) < 80.0f) c->pad_5[0] = 0; else c->Joystick_LX = 0.0f; }
        if (c->pad_5[1] != 0) { if (fabsf(c->Joystick_LY) < 80.0f) c->pad_5[1] = 0; else c->Joystick_LY = 0.0f; }
        if (c->pad_5[2] != 0) { if (fabsf(c->Joystick_RX) < 80.0f) c->pad_5[2] = 0; else c->Joystick_RX = 0.0f; }
        if (c->pad_5[3] != 0) { if (fabsf(c->Joystick_RY) < 80.0f) c->pad_5[3] = 0; else c->Joystick_RY = 0.0f; }

        // Edge-trigger: .buttons becomes "just pressed this frame" (prevButtons masks out anything that
        // was already held last frame), and prevButtons is retained as "still held" for next frame.
        unsigned int prevHeld = c->prevButtons;
        unsigned int rawButtons = c->buttons;
        c->prevButtons = prevHeld & rawButtons;
        c->buttons = ~prevHeld & rawButtons;

        // Rumble dispatch. The original's XInputSetState is asynchronous, taking a pointer to a real Xbox
        // XINPUT_FEEDBACK (header + status/context fields, with the actual XINPUT_RUMBLE payload right at
        // the end - which is exactly what "vibrationState"/pad_2/pad_3/scaledRumbleA/B here are: a 4-byte
        // status word checked against 0x3e5 (997, ERROR_IO_PENDING) to skip re-issuing a call that's still
        // in flight, followed by padding for the rest of the header, followed by the real rumble values -
        // confirmed from the raw disassembly of 0x000e76a0, not just its decompile, since the decompile's
        // own account of this part doesn't hold together on its own). Win32's XInputSetState is
        // synchronous and takes only the plain 4-byte rumble struct, so none of that header/pending-check
        // machinery applies here - we just send the new values directly whenever they've changed.
        if (c->controllerIndex != 0 &&
            (c->lastRumbleA != c->rumbleA || c->lastRumbleB != c->rumbleB)) {
            Win32_XINPUT_VIBRATION vibration;
            vibration.wLeftMotorSpeed = (unsigned short)(c->rumbleA * 655);
            vibration.wRightMotorSpeed = (unsigned short)(c->rumbleB * 655);
            XInputSetState((unsigned long)i, &vibration);

            c->lastRumbleA = c->rumbleA;
            c->lastRumbleB = c->rumbleB;
        }
    }
}

// Array of 4 uint32_t entries, all initialised to 0xFFFFFFFF
#define controller_maybeRumbleTimeout ((int*)0x0019481c)

// AUTOINJECT
bool psiInput_ControllerIsActive(uint i) {

    NF_ASSERT(i <= 3, "Assumed that controller index alwas in range 0-3");

    return XboxInputs.Controllers[i].controllerIndex != 0;
}

// AUTOINJECT
float psiInput_GetJoystickRX(uint i) {

    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_RX;
}

// AUTOINJECT
float psiInput_GetJoystickLX(uint i) {

    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_LX;
}
// AUTOINJECT
float psiInput_GetJoystickLY(uint i) {

    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_LY;
}

// AUTOINJECT
float psiInput_GetJoystickRY(uint i) {

    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].Joystick_RY;
}

// AUTOINJECT
uint psiInput_GetButtons(uint i) {
    
    NF_ASSERT(i <= 3, "Incorrectly assumed controller index <= 3");

    return XboxInputs.Controllers[i].buttons;
}

// AUTOINJECT
void psiInput_RumbleSetIntensity(unsigned int i, unsigned short a, unsigned short b) {

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

#define bSkipAttract U8_AT(0x0025d79d)

// Array of 4x bool32
#define controllerIsPresent ((unsigned int*)(0x0019482c))

// AUTOINJECT
void psiInput_MapInputs(PlayerInput_tag* playerInputs, int maxPlayers) {
    // Only ever called from Input_Update, with:
    // psiInput_MapInputs(PlayerInputs, (GameState.CurrentLevelHashcode==HT_Level_Menu_Pre ? 4 : MPSettings.NumPlayers))

    // Detect controller being connected, break out of the Attract movie if so.
    bSkipAttract = false;
    for(int i = 0; i < 4; i++) {
        bool isPresent = psiInput_ControllerIsActive(i);
        if(isPresent && !controllerIsPresent[i]) {
            bSkipAttract = true;
        }
        controllerIsPresent[i] = isPresent;
    }

    // Clear out state from the previous frame
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
                case CONTROLSTYLE_NIGHTFIRE:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_RIGHT | XBOXINPUT_GAMEPAD_B) ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_UP | XBOXINPUT_GAMEPAD_RIGHT_SHOULDER) ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_BACK ? 1.0f : 0.0f);
                    break;

                case CONTROLSTYLE_MOONRAKER:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_B ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_RIGHT | XBOXINPUT_GAMEPAD_X) ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = 0.0f; // Only able to cycle in one direction
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_BACK ? 1.0f : 0.0f);
                    break;

                case CONTROLSTYLE_OCTOPUSSY:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_B ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_RIGHT | XBOXINPUT_GAMEPAD_X) ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = 0.0f; // Only able to cycle in one direction
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_BACK ? 1.0f : 0.0f);
                    if(playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] != 0.0f) {
                        playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    }
                    break;

                case CONTROLSTYLE_GOLDFINGER:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_B ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_RIGHT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & (XBOXINPUT_GAMEPAD_RIGHT_SHOULDER | XBOXINPUT_GAMEPAD_LEFT_SHOULDER) ? 1.0f : 0.0f);
                    break;

                case CONTROLSTYLE_DRNO:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_B ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT) ? 1.0f : 0.0f);
                    break;

                case CONTROLSTYLE_THUNDERBALL:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_B ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_RIGHT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & (XBOXINPUT_GAMEPAD_RIGHT_SHOULDER | XBOXINPUT_GAMEPAD_LEFT_SHOULDER) ? 1.0f : 0.0f);
                    break;

                case CONTROLSTYLE_GOLDENEYE:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_DOWN | XBOXINPUT_GAMEPAD_B) ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_RIGHT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_SHOULDER ? 1.0f : 0.0f);
                    break;

                case CONTROLSTYLE_CLASSICBOND:
                    playerInputs[i].fChannels[ACTION_WALK_F_B] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_WALK_L_R] = psiInput_GetJoystickRX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_U_D] = psiInput_GetJoystickRY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_L_R] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_AIM_ZOOM_SCOPE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] = psiInput_GetJoystickLY(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TURRET_AIM_X] = psiInput_GetJoystickLX(controllerIdx);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_DOWN ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_JUMP] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_CROUCH] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_FIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_TRIGGER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_RELOAD] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_A ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_ALTFIRE] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_RIGHT_SHOULDER ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_NEXT] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_DPAD_DOWN | XBOXINPUT_GAMEPAD_B) ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_WEAPON_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_UP ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_NEXT] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_RIGHT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_GADGET_PREV] = (psiInput_GetButtons(controllerIdx) & XINPUT_GAMEPAD_DPAD_LEFT ? 1.0f : 0.0f);
                    playerInputs[i].fChannels[ACTION_NIGHTVISION] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_LEFT_SHOULDER ? 1.0f : 0.0f);
                    break;

                // There's a default case fully implemented, but all the control schemes have already been covered. Unclear what this is here for.
                default:
                    NF_ASSERT(false, "Assumed that the default case was never used in psiInput_MapInputs, but it is!");
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
            playerInputs[i].fChannels[ACTION_MENU_ALTSELECT_1] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_Y ? 1.0f : 0.0f);

            // ??
            playerInputs[i].fChannels[ACTION_MENU_ALTSELECT_2] = (psiInput_GetButtons(controllerIdx) & XBOXINPUT_GAMEPAD_X ? 1.0f : 0.0f);

            // Skip cutscene
            playerInputs[i].fChannels[ACTION_SKIP_CUTSCENE] = (psiInput_GetButtons(controllerIdx) & (XINPUT_GAMEPAD_START | XBOXINPUT_GAMEPAD_A | XBOXINPUT_GAMEPAD_B) ? 1.0f : 0.0f);

            // Axis scaling
            playerInputs[i].fChannels[ACTION_WALK_F_B] *= 0.01f;
            playerInputs[i].fChannels[ACTION_WALK_L_R] *= -0.01f; // Flip axis too
            playerInputs[i].fChannels[ACTION_AIM_U_D] *= 0.01f;
            playerInputs[i].fChannels[ACTION_AIM_L_R] *= 0.01f;
            playerInputs[i].fChannels[ACTION_TURRET_AIM_Y] *= 0.01f;
            playerInputs[i].fChannels[ACTION_TURRET_AIM_X] *= 0.01f;

            // Zoom is (ZoomIn - ZoomOut)
            playerInputs[i].fChannels[ACTION_SCOPE_ZOOM] = -(playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] - playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT]);
            playerInputs[i].fChannels[ACTION_TMP_ZOOMOUT] = 0.0f;
            playerInputs[i].fChannels[ACTION_TMP_ZOOMIN] = 0.0f;

            // Ghidra can't find any uses of this action
            playerInputs[i].fChannels[ACTION_UNUSED_24] *= -1.0f;

            
        }

    }

}
