#include "HostInput.h"
#include "Host.h"
#include "HostSdl.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

// Controllers and the keyboard from SDL3 (NIGHTFIRE_SDL), given in XInput's numbers so that XboxInput.cpp's
// translation to the Xbox's is the one the XInput path has.
//
// Pads are opened and closed on the window's thread as SDL reports them (HostWindow_Run hands the events here); a
// pad takes the port its player index names, as XInput's user index does, or the first one free. The game's thread
// reads them under the same lock, so a pad is never closed in the middle of a read.

static const int kPorts = 4;

static SDL_Gamepad *Ports[kPorts];

static HostLock *PortsLock(void) {
    static HostLock lock;
    static bool ready = (HostInitLock(&lock), true);
    (void)ready;
    return &lock;
}

static void Opened(SDL_JoystickID id) {
    SDL_Gamepad *pad = SDL_OpenGamepad(id);
    if (pad == NULL)
        return;
    HostEnterLock(PortsLock());
    int port = SDL_GetGamepadPlayerIndex(pad);
    if (port < 0 || port >= kPorts || Ports[port] != NULL) {
        port = -1;
        for (int i = 0; i < kPorts && port < 0; i++)
            if (Ports[i] == NULL)
                port = i;
    }
    if (port >= 0)
        Ports[port] = pad;
    HostLeaveLock(PortsLock());
    if (port < 0) {
        SDL_CloseGamepad(pad);
        return;
    }
    printf("[input] pad on port %d: %s\n", port + 1, SDL_GetGamepadName(pad));
    fflush(stdout);
}

static void Closed(SDL_JoystickID id) {
    HostEnterLock(PortsLock());
    for (int i = 0; i < kPorts; i++) {
        if (Ports[i] != NULL && SDL_GetGamepadID(Ports[i]) == id) {
            SDL_CloseGamepad(Ports[i]);
            Ports[i] = NULL;
        }
    }
    HostLeaveLock(PortsLock());
}

static void MouseEvent(const SDL_Event *event);

void HostInputSdl_Event(const SDL_Event *event) {
    if (event->type == SDL_EVENT_GAMEPAD_ADDED)
        Opened(event->gdevice.which);
    else if (event->type == SDL_EVENT_GAMEPAD_REMOVED)
        Closed(event->gdevice.which);
    else
        MouseEvent(event);
}

// A trigger, 0..32767, back to XInput's byte (SDL makes it from the byte as byte * 257 over the full range)
static uint8_t TriggerByte(Sint16 value) {
    return (uint8_t)((value * 255 + 16383) / 32767);
}

bool HostInput_ReadPad(int port, HostPadState *state) {
    static const struct {
        SDL_GamepadButton button;
        uint16_t bit;
    } kButtons[] = {
        { SDL_GAMEPAD_BUTTON_DPAD_UP, kPadDpadUp },
        { SDL_GAMEPAD_BUTTON_DPAD_DOWN, kPadDpadDown },
        { SDL_GAMEPAD_BUTTON_DPAD_LEFT, kPadDpadLeft },
        { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, kPadDpadRight },
        { SDL_GAMEPAD_BUTTON_START, kPadStart },
        { SDL_GAMEPAD_BUTTON_BACK, kPadBack },
        { SDL_GAMEPAD_BUTTON_LEFT_STICK, kPadLeftThumb },
        { SDL_GAMEPAD_BUTTON_RIGHT_STICK, kPadRightThumb },
        { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, kPadLeftShoulder },
        { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, kPadRightShoulder },
        { SDL_GAMEPAD_BUTTON_SOUTH, kPadA },
        { SDL_GAMEPAD_BUTTON_EAST, kPadB },
        { SDL_GAMEPAD_BUTTON_WEST, kPadX },
        { SDL_GAMEPAD_BUTTON_NORTH, kPadY },
    };
    // The packet number changes when the state does, as XInput's
    static HostPadState last[kPorts];
    static uint32_t packets[kPorts];

    HostEnterLock(PortsLock());
    SDL_Gamepad *pad = Ports[port];
    if (pad == NULL) {
        HostLeaveLock(PortsLock());
        return false;
    }
    HostPadState now;
    memset(&now, 0, sizeof(now));
    for (const auto &entry : kButtons)
        if (SDL_GetGamepadButton(pad, entry.button))
            now.buttons |= entry.bit;
    now.leftTrigger = TriggerByte(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    now.rightTrigger = TriggerByte(SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    // SDL's sticks have down positive: XInput's Y readings are inverted bit for bit on the way in, so here too
    now.thumbLX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTX);
    now.thumbLY = (int16_t)~SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_LEFTY);
    now.thumbRX = SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTX);
    now.thumbRY = (int16_t)~SDL_GetGamepadAxis(pad, SDL_GAMEPAD_AXIS_RIGHTY);
    HostLeaveLock(PortsLock());

    if (memcmp(&now, &last[port], sizeof(now)) != 0)
        packets[port]++;
    last[port] = now;
    now.packetNumber = packets[port];
    *state = now;
    return true;
}

// Each call replaces the last; SDL ends a rumble after at most 65 seconds unless it is set again.
void HostInput_SetRumble(int port, uint16_t leftMotor, uint16_t rightMotor) {
    HostEnterLock(PortsLock());
    if (Ports[port] != NULL)
        SDL_RumbleGamepad(Ports[port], leftMotor, rightMotor, 0xffffffffu);
    HostLeaveLock(PortsLock());
}

bool HostInput_KeyHeld(HostKey key) {
    const bool *keys = SDL_GetKeyboardState(NULL);
    switch (key) {
        case kKeyA:      return keys[SDL_SCANCODE_A];
        case kKeyD:      return keys[SDL_SCANCODE_D];
        case kKeyS:      return keys[SDL_SCANCODE_S];
        case kKeyW:      return keys[SDL_SCANCODE_W];
        case kKeyLeft:   return keys[SDL_SCANCODE_LEFT];
        case kKeyRight:  return keys[SDL_SCANCODE_RIGHT];
        case kKeyUp:     return keys[SDL_SCANCODE_UP];
        case kKeyDown:   return keys[SDL_SCANCODE_DOWN];
        case kKeySpace:  return keys[SDL_SCANCODE_SPACE];
        case kKeyShift:  return keys[SDL_SCANCODE_LSHIFT] || keys[SDL_SCANCODE_RSHIFT];
        case kKeyEnter:  return keys[SDL_SCANCODE_RETURN] || keys[SDL_SCANCODE_KP_ENTER];
        case kKeyEscape: return keys[SDL_SCANCODE_ESCAPE];
        case kKeyF8:     return keys[SDL_SCANCODE_F8];
        case kKeyF9:     return keys[SDL_SCANCODE_F9];
        case kKeyQ:      return keys[SDL_SCANCODE_Q];
        case kKeyE:      return keys[SDL_SCANCODE_E];
        case kKeyR:      return keys[SDL_SCANCODE_R];
        case kKeyC:      return keys[SDL_SCANCODE_C];
        case kKeyZ:      return keys[SDL_SCANCODE_Z];
        case kKeyX:      return keys[SDL_SCANCODE_X];
        case kKeyP:      return keys[SDL_SCANCODE_P];
        case kKeyN:      return keys[SDL_SCANCODE_N];
        case kKey1:      return keys[SDL_SCANCODE_1];
        case kKey2:      return keys[SDL_SCANCODE_2];
        case kKeyBackspace: return keys[SDL_SCANCODE_BACKSPACE];
        case kKeyLeftCtrl:  return keys[SDL_SCANCODE_LCTRL];
        case kKeyF6:     return keys[SDL_SCANCODE_F6];
    }
    return false;
}

bool HostInput_WindowHasFocus(void) {
    return HostWindowSdl_HasKeyboard();
}

// ---- the mouse
//
// Its events arrive on the window's thread (HostInputSdl_Event) and are added up under a lock of their own until the
// game's thread reads them. Captured, the window is in SDL's relative mouse mode: the pointer is hidden and held,
// and the motion events carry the movement, unaccelerated where the platform allows.

static struct {
    int32_t dx, dy;
    float wheel;          // notches, which SDL reports in fractions on some devices
    bool left, right;
    bool inWindow;
} Mouse;

static HostLock *MouseLock(void) {
    static HostLock lock;
    static bool ready = (HostInitLock(&lock), true);
    (void)ready;
    return &lock;
}

static void MouseEvent(const SDL_Event *event) {
    HostEnterLock(MouseLock());
    switch (event->type) {
        case SDL_EVENT_MOUSE_MOTION:
            Mouse.dx += (int32_t)event->motion.xrel;
            Mouse.dy += (int32_t)event->motion.yrel;
            break;
        case SDL_EVENT_MOUSE_WHEEL:
            Mouse.wheel += event->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event->wheel.y : event->wheel.y;
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event->button.button == SDL_BUTTON_LEFT)
                Mouse.left = event->button.down;
            else if (event->button.button == SDL_BUTTON_RIGHT)
                Mouse.right = event->button.down;
            break;
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
            Mouse.inWindow = true;
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            Mouse.inWindow = false;
            break;
    }
    HostLeaveLock(MouseLock());
}

void HostMouse_Read(HostMouseState *state) {
    HostEnterLock(MouseLock());
    state->dx = Mouse.dx;
    state->dy = Mouse.dy;
    int32_t notches = (int32_t)(Mouse.wheel * 120.0f);
    state->wheel = notches;
    Mouse.wheel -= (float)notches / 120.0f;
    state->left = Mouse.left;
    state->right = Mouse.right;
    Mouse.dx = Mouse.dy = 0;
    HostLeaveLock(MouseLock());
}

bool HostMouse_HasWindow(void) {
    return HostWindowSdl_Window() != NULL;
}

bool HostMouse_InWindow(void) {
    HostEnterLock(MouseLock());
    bool inside = Mouse.inWindow;
    HostLeaveLock(MouseLock());
    return inside;
}

static void SDLCALL ApplyCapture(void *data) {
    SDL_SetWindowRelativeMouseMode(HostWindowSdl_Window(), *static_cast<bool *>(data));
}

bool HostMouse_Capture(bool capture) {
    if (HostWindowSdl_Window() == NULL)
        return false;
    SDL_RunOnMainThread(ApplyCapture, &capture, true);
    HostEnterLock(MouseLock());
    Mouse.dx = Mouse.dy = 0;
    if (!capture)
        Mouse.wheel = 0.0f;
    HostLeaveLock(MouseLock());
    return capture;
}
