#include <stdio.h>
#include <windows.h>

#include "input.h"
#include "engine/psiInput.h"
#include "engine/psiDraw.h"           // psiInput_PollDevices_Thunk
#include <string.h>
#include "engine/mouseLook.h"
#include "engine/mouseSteer.h"
#include "devtools/Demo.h"
#include "game/mp/multiplayer.h"
#include "game.h"
#include <math.h>

// XBE_GLOBAL(0x001fe6d0, 0x560)
PlayerInput PlayerInputs[4];

// MSVC's __ftol2 as the original uses it: the low word of a truncation to 64 bits. NaN, values out of the 64-bit range
// and values that round to a multiple of 2^32 give 0 (as src/driving/platform/X87.h has it).
static int32_t Ftol(double x) {
    if (!(fabs(x) < 9223372036854775808.0))
        return 0;
    long long nearest = llrint(x);
    if (uint32_t(nearest) == 0)
        return 0;
    return int32_t(int64_t(x));
}

// Keyboard input proper now lives in engine/psiInput.cpp, which presents the keyboard as a virtual Xbox pad on
// port 0 whenever no real pad is plugged in - see the binding table in the block comment above
// BuildKeyboardPadState there. Doing it at that level means the game derives the action flags itself, exactly
// as it does for a real pad, instead of this function having to guess them (which is why it only ever managed a
// handful of channels, and why a released key could leave an action stuck on).
//
// What is left here is debug-only, on a key that cannot collide with a binding.
void Inject_KeyboardInput(void) {

    // F9 dumps the multiplayer settings table. Edge-triggered, or it would print every frame it is held.
    static bool dumpHeld = false;
    bool dumpDown = (GetAsyncKeyState(0x78) & 0x8000) != 0; // VK_F9
    if (dumpDown && !dumpHeld) {
      printf("MP settings:\n");
      for(int i = 0; i < NUM_AGENTS; i++) {
        printf("Index %i: %-16s\t%-10s\t%i\t%i\t%i\n", i, MPSettings.Player[i].Name, TEAM_GET_NAME(MPSettings.Player[i].TeamId), MPSettings.Player[i].SkinNum, MPSettings.Player[i].SomeField2, MPSettings.Player[i].HealthModifier);
      }
    }
    dumpHeld = dumpDown;

    // The game's action channels, for reference (index is GameActions_tag - see engine/psiInput.h for the
    // named enum, which supersedes this list):
    // Channel 0: Aim left/right (+: Right)      Channel 9:  Fire
    // Channel 1: Move left/right (+: Left)      Channel 12: Alt fire
    // Channel 2: Move forward/backward (+: Fwd) Channel 19: Zoom
    // Channel 5: Aim up/down (+: Up)            Channel 30: Pause/start
    // Channel 7: In space move up/down (+: Up)
    //
    // Action flag values: 1 for continuously-held actions (move, scope zoom), 4 for discrete ones (trigger,
    // stabilize spacesuit) which should be true for a single frame so the action is not repeated.
}

// AUTOINJECT
unsigned short Input_Action(short playerNum, GameActions_tag action,unsigned char flags) {
  
    // Any player (specified with a negative value)
    if (playerNum < 0) {
        unsigned short uVar2 = 0;
        for(int i = 0; i<4; i++)
            uVar2 |= Input_Action(i,action,flags);
        return uVar2;
    }

    // One specific player and the action is pressed
    if ((playerNum < NUM_PLAYERS) && (PlayerInputs[playerNum].actions[action] & flags)) {
      return (unsigned short)(int)(PlayerInputs[playerNum].fChannels[action] * 100.0);
    }

    // Invalid player number or no action pressed
    return 0;
}

// AUTOINJECT
float Input_Actionf(short playerNum, GameActions_tag action, unsigned char flags) {
  
  // Any player
  if (playerNum < 0) {
    float max = 0.0;
    
    for(int i = 0; i < 4; i++) {
      float current = Input_Actionf(i,action,flags);
      if (max <= current) {
        max = current;
      }
    }
    return max;
    }

  // A specific player and the action is pressed
  if ((playerNum < NUM_PLAYERS) && (PlayerInputs[playerNum].actions[action] & flags)) {
      return PlayerInputs[playerNum].fChannels[action];
  }

  // Invalid player number or the action is not pressed
  return 0.0f;
}

// AUTOINJECT
void Input_ClearAction(short playerNum, GameActions_tag action) {
  
  // All players
  if (playerNum < 0) {
    for(int i = 0; i < 4; i++) {
      Input_ClearAction(i, action);
    }
    return;
  } 

  // A specific player
  if (playerNum < 4) {
    PlayerInputs[playerNum].fChannels[action] = 0.0;
    PlayerInputs[playerNum].actions[action] = 0;
  }
  return;
}

// AUTOINJECT
void Input_SetAction(short playerNum, GameActions_tag action,unsigned char val) {

  // All players
  if (playerNum < 0) {
    for(int i = 0; i < 4; i++) {
      Input_SetAction(i,action,val);
    }
    return;
  }

  // A specific player
  if (playerNum < 4) {
    PlayerInputs[playerNum].fChannels[action] = 1.0;
    PlayerInputs[playerNum].actions[action] = val;
  }

  return;
}

// Zeroes the analogue channels of every action, for one player or (-1) all of them, after flushing the pads.
// AUTOINJECT
void Input_ClearAllActions(short playerNum) {
  psiInputReset();
  for (int i = 0; i < 4; i++) {
    if (playerNum == -1 || playerNum == i)
      memset(PlayerInputs[i].fChannels, 0, sizeof(PlayerInputs[i].fChannels));
  }
}

// AUTOINJECT
bool Input_ChangeControllerStyle(ushort playerNum, int controllerStyle) {

  if(playerNum >= 4)
    return false;

  if(PlayerInputs[playerNum].controlStyle != controllerStyle) {
    PlayerInputs[playerNum].controlStyle = controllerStyle;
    Input_ClearAllActions(playerNum);
  }

  return true;
}

// AUTOINJECT
void Input_RumbleStart(ushort playerNum, int time, int intensity) {
  if(!PlayerInputs[playerNum].vibrationEnabled)
    return;
  if(!GameState.VibrationEnabled)
    return;
  if(PlayerInputs[playerNum].controllerPort >= 4)
    return;
  psiInput_RumbleStart(PlayerInputs[playerNum].controllerPort, time, intensity);
}

extern uint8_t FreezeGame; // defined in game.cpp
// An input event list beside it (entries of 0x18 bytes, 10 preallocated), walked by Input_ProcessEvents at the end of
// every Input_Update. Nothing in the game adds to it, so it is always empty.
// XBE_GLOBAL(0x001fec30, 0x18)
static DLISTINFO_tag InputEventList;

// An input event (0x18 bytes, from InputEventList's pool): watches player 0's actions and flips *target when they
// happen - either a sequence (the actions pressed one after another) or, for an analog event, the product of the
// actions' values passing 0.3. INVENTED NAMES, not canonical: nothing creates one, so they come from the walker alone.
#pragma pack(push, 1)
typedef struct InputEvent {
    LLNODE_tag node;
    ushort progress;        // 0x08 - how far along a sequence it is
    ushort count;           // 0x0a - actions in the sequence / in the product
    ushort analog;          // 0x0c - 0: a sequence
    ushort armed;           // 0x0e - analog: set once it has fired, until the product drops back under 0.0002
    uint *actions;          // 0x10 - GameActions_tag ids
    uint *target;           // 0x14 - flipped (0 <-> 1) each time the event completes
} InputEvent;
#pragma pack(pop)
static_assert(sizeof(InputEvent) == 0x18, "InputEvent is InputEventList's entry size");

#define ACTION_HELD 1               // PlayerInput.actions bits the walker tests
#define ACTION_PRESSED 4

// Whether player 0's action is pressed right now and has a value (in hundredths) that is not 0 in its low 16 bits
static bool Input_EventActionOn(uint action) {
    if (!(PlayerInputs[0].actions[action] & ACTION_PRESSED))
        return false;
    // in double, as the x87 multiplies it, then truncated (__ftol2) and only the low 16 bits tested
    return (short)(int64_t)((double)PlayerInputs[0].fChannels[action] * (double)100.0f) != 0;
}

// Runs every input event (the tail of Input_Update: the original jumps here from its end). INVENTED NAME, not
// canonical (FUN_0006cd20).
// FUNC_AT(0006cd20)
void Input_ProcessEvents(void) {
    for (InputEvent *ev = (InputEvent *)InputEventList.activeList.head; ev != NULL; ev = (InputEvent *)ev->node.next) {
        if (ev->analog != 0) {
            // The product of the held actions' values (a released one counts 0): fires once past 0.3, re-arms
            // under 0.0002. The x87 keeps the product at double precision.
            double product = 1.0;
            for (ushort i = 0; i < ev->count; i++) {
                uint action = ev->actions[i];
                product *= (PlayerInputs[0].actions[action] & ACTION_HELD) ? PlayerInputs[0].fChannels[action] : 0.0f;
            }
            double magnitude = product < 0.0 ? -product : product;
            if (magnitude > (double)0.3f) {
                if (ev->armed == 0)
                    *ev->target = (*ev->target == 0);
                ev->armed = 1;
            } else if (magnitude < (double)0.0002f) {
                ev->armed = 0;
            }
            continue;
        }

        // A sequence: the next action advances it; the previous one pressed again (with byte +0x154 set) starts over
        ushort progress = ev->progress;
        int previous = progress - 1;
        previous = previous < 0 ? 0 : (previous > ev->count ? ev->count : previous);
        if (Input_EventActionOn(ev->actions[progress])) {
            ev->progress = progress + 1;
        } else if (Input_EventActionOn(ev->actions[(ushort)previous]) && PlayerInputs[0].anyInput) {
            ev->progress = 0;
        }
        if (ev->progress >= ev->count) {
            *ev->target = (*ev->target == 0);
            ev->progress = 0;
        }
    }
}

// The frame's input, with what the core adds around the game's own (docs/core-deviations.md): the mouse look before
// the pads are read, the mouse steering after, then the debug keys. Called by our game loop in place of
// Input_Update.
void Input_UpdateWithMouse(void) {

    // The mouse goes first, so that the buttons it reports land in the pad state the game's own poll
    // builds immediately below, rather than a frame behind it. It is serviced from here rather than
    // alongside the aiming it feeds because letting go of the pointer is a menu-time job, and the aim
    // hook is precisely what stops running in menus. See engine/mouseLook.h.
    // A time demo's playback takes the mouse from the recording instead of the host (devtools/Demo.h)
    if (!Demo_Playing())
        MouseLook_Update();
    MouseLook_DemoExchange();

    // The game's own: poll, compensate the sticks, map the pads to actions
    Input_Update();

    // While the player is controlling something remotely, the mouse joins the sticks that steer it - after the
    // poll has filled them in, and before any object reads them. See engine/mouseSteer.h.
    MouseSteer_Update();

    // Our added function - the debug keys
    Inject_KeyboardInput();
}

// The sticks' response: a length through the table, in 255ths, from 0 to 128 in 127ths
// XBE_GLOBAL(0x0017c170, 0x81)
uchar AnalogStickMapping[129] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10,
    10, 10, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
    37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 52, 54, 56, 58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 78, 80,
    82, 84, 86, 88, 90, 94, 98, 102, 106, 110, 114, 118, 122, 126, 130, 134, 138, 142, 146, 150, 160, 170, 180, 190,
    200, 210, 220, 230, 240, 250, 255, 255, 255,
};
#define STICK_DEAD_ZONE 0.0002f
// An axis within this of the centre sends the other, past it, to its end (StickCompensation3)
#define STICK_DIAGONAL 0.707107f

// Fires when held this long, every 8 frames
#define ACTION_REPEAT_FRAMES 0x2d
#define ACTION_REPEAT 8
#define ACTION_MAX_HOLD 0x7fff

// PlayerInput::actionLock: Input_ClearAction-style holds. Bit 0x10 off, the action is not reported until neither the
// held (1) nor the pressed (4) state it locks is there.
#define ACTIONLOCK_OPEN 0x10

// The control styles that take the left stick as one
#define CONTROLSTYLE_DRNO_STICK 2
#define CONTROLSTYLE_GOLDENEYE_STICK 5

// The stick (x, y) with its length passed through AnalogStickMapping, its direction kept
void _StickCompensation2(float *x, float *y) {
    double length = sqrt((double)*y * *y + (double)*x * *x);
    if (length <= STICK_DEAD_ZONE && length >= -STICK_DEAD_ZONE)
        return;
    int value = Ftol(127.0f * length);
    int magnitude = value < 0 ? -value : value;
    if (magnitude > 0x80)
        magnitude = 0x80;
    int mapped = AnalogStickMapping[magnitude];
    if (value < 0)
        mapped = -mapped;
    double scale = (double)mapped * 0.003921569f / length;
    *x = (float)(scale * *x);
    *y = (float)(scale * *y);
}

// The original takes x in EDI and y in ESI
// AUTOLTCG
__declspec(naked) void StickCompensation2(void) {
    _asm {
        push ecx
        push edx
        push esi
        push edi
        call _StickCompensation2
        add esp, 8
        pop edx
        pop ecx
        ret
    }
}

// With y near the centre, x past the diagonal goes to its end; then, with x as it was near the centre, y does
// AUTOINJECT
void __fastcall StickCompensation3(float *x, float *y) {
    float oldX = *x;
    float oldY = *y;
    if (oldY >= -STICK_DIAGONAL && oldY <= STICK_DIAGONAL) {
        if (oldX <= -STICK_DIAGONAL)
            *x = -1.0f;
        else if (oldX >= STICK_DIAGONAL)
            *x = 1.0f;
    }
    if (oldX >= -STICK_DIAGONAL && oldX <= STICK_DIAGONAL) {
        if (oldY <= -STICK_DIAGONAL)
            *y = -1.0f;
        else if (oldY >= STICK_DIAGONAL)
            *y = 1.0f;
    }
}

// Every player's actions from the pads: the sticks compensated, the look inverted if asked, each channel clamped to
// [-1, 1] and its flags worked out (held, pressed on the frame it starts, repeating every 8 frames once held long
// enough), unless its lock holds. Ends with the input events.
// AUTOINJECT
void Input_Update(void) {
    for (int i = 0; i < 4; i++)
        memset(PlayerInputs[i].fChannels, 0, sizeof(PlayerInputs[i].fChannels));

    psiInput_PollDevices_Thunk();
    int players = GameState.CurrentLevelHashcode == HT_Level_Menu_Pre ? 4 : MPSettings.numPlayers;
    psiInput_MapInputs((PlayerInput_tag *)PlayerInputs, players);

    for (int p = 0; p < 4; p++) {
        PlayerInput *input = &PlayerInputs[p];
        float *channel = input->fChannels;
        if (input->controlStyle == CONTROLSTYLE_DRNO_STICK || input->controlStyle == CONTROLSTYLE_GOLDENEYE_STICK) {
            _StickCompensation2(&channel[3], &channel[4]);
            StickCompensation3(&channel[2], &channel[0]);
        } else {
            _StickCompensation2(&channel[3], &channel[4]);
            _StickCompensation2(&channel[0], &channel[5]);
            StickCompensation3(&channel[2], &channel[1]);
        }
        if (input->inverted) {
            channel[4] = channel[4] * -1.0f;
            channel[5] = channel[5] * -1.0f;
        }

        input->anyInput = 0;
        for (int i = 0; i < 40; i++) {
            float value = channel[i];
            if (value < -1.0f)
                value = -1.0f;
            else if (value > 1.0f)
                value = 1.0f;
            channel[i] = value;

            uchar flags;
            if (value == 0.0f) {
                input->holdFrames[i] = 0;
                flags = 0;
            } else {
                flags = input->actions[i] == 0 ? ACTION_HELD | ACTION_PRESSED : ACTION_HELD;
                int held = input->holdFrames[i] + 1;
                if (held > ACTION_MAX_HOLD)
                    held = ACTION_MAX_HOLD;
                input->holdFrames[i] = held;
                if (input->holdFrames[i] > ACTION_REPEAT_FRAMES && input->holdFrames[i] % ACTION_REPEAT == 0)
                    flags |= ACTION_REPEAT;
                input->anyInput = 1;
            }

            uchar lock = input->actionLock[i];
            if (lock & ACTIONLOCK_OPEN)
                input->actions[i] = flags;
            else if (!((flags & ACTION_HELD) && (lock & ACTION_HELD)) && !((flags & ACTION_PRESSED) && (lock & ACTION_PRESSED)))
                input->actionLock[i] = lock | ACTIONLOCK_OPEN;
        }
    }

    Input_ProcessEvents();
}

// AUTOINJECT
void Input_RumbleStop(ushort playerNum) {
    if (PlayerInputs[playerNum].vibrationEnabled && PlayerInputs[playerNum].controllerPort < 4)
        psiInput_ResetRumble(PlayerInputs[playerNum].controllerPort);
}

// Every player's settings to the defaults. The original ends with a jump to the empty debug hook (0x000e0ec0).
// AUTOINJECT
void Input_Init(void) {
    for (int i = 0; i < 4; i++) {
        PlayerInput *input = &PlayerInputs[i];
        memset(input, 0, sizeof(*input));
        input->autoAim = 1;
        input->manualAimToggle = 0;
        input->crouchToggle = 1;
        input->field5_0x5 = 1;
        input->levelViewOnPace = 1;
        input->rememberZoom = 1;
        input->crosshairsEnabled = 1;
        input->controlStyle = 0;
        input->controlStyleDriving = 0;
        input->autoSwitchBetterWeapon = 1;
        input->hudVisibility = 0;
        input->flashingObjects = 1;
    }
}

// Readies input for a level: unfreezes the game, clears every player's actions and resets the event list.
// The original ends with a jump to an empty debug hook (0x000e0ec0, a bare RET).
// AUTOINJECT
void Input_Ready(void) {
    FreezeGame = 0;
    Input_ClearAllActions(-1);
    DList_Init(&InputEventList, 0x18, 10);
}
