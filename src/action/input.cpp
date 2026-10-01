#include <stdio.h>
#include <windows.h>

#include "input.h"
#include "engine/mouseLook.h"
#include "game/mp/multiplayer.h"

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
      for(int i = 0; i < 10; i++) {
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

// Does not need to be injected, it's only called from the game loop which we've replaced
// Weirdly, injecting this causes problems though - maybe some custom registers not identified?
// UNINJECTABLE
void Input_Update(void) {

    // The mouse goes first, so that the buttons it reports land in the pad state the game's own poll
    // builds immediately below, rather than a frame behind it. It is serviced from here rather than
    // alongside the aiming it feeds because letting go of the pointer is a menu-time job, and the aim
    // hook is precisely what stops running in menus. See engine/mouseLook.h.
    MouseLook_Update();

    // Game functions - poll, compensate stick, map from keys to actions (the original's own body, which ends in
    // Input_ProcessEvents)
    void (*funcPtr)(void) = (void (*)(void))(0x0006cf50);
    funcPtr();

    // Our added function - the debug keys
    Inject_KeyboardInput();
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
    if ((playerNum < 4) && (PlayerInputs[playerNum].actions[action] & flags)) {
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
  if ((playerNum < 4) && (PlayerInputs[playerNum].actions[action] & flags)) {
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

// AUTOGEN
void Input_ClearAllActions(short playerNum);

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

// AUTOGEN
void Input_Init(void);

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
        } else if (Input_EventActionOn(ev->actions[(ushort)previous]) && PlayerInputs[0].field141_0x154) {   // +0x154, meaning unknown
            ev->progress = 0;
        }
        if (ev->progress >= ev->count) {
            *ev->target = (*ev->target == 0);
            ev->progress = 0;
        }
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