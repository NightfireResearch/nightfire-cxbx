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

    // Game functions - poll, compensate stick, map from keys to actions
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

// AUTOGEN
void Input_Ready(void);