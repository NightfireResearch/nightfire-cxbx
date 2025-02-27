#include <stdio.h>
#include <windows.h>

#include "input.h"

void Inject_KeyboardInput(void) {

    // Inject WASD control into controller 1 for now


    // if(GetKeyState(VK_UP) & 0x8000) {
    //     printf("Has UP\n");
    // }
    // if(GetKeyState(VK_DOWN) & 0x8000) {
    //     printf("Has DOWN\n");
    // }

    if(GetKeyState('W') & 0x8000) {
        PlayerInputs[0].fChannels[2] = 1.0f;
        PlayerInputs[0].actions[2] = 1;
    }
    if(GetKeyState('S') & 0x8000) {
        PlayerInputs[0].fChannels[2] = -1.0f;
        PlayerInputs[0].actions[2] = 1;
    }
    if(GetKeyState('A') & 0x8000) {
        PlayerInputs[0].fChannels[1] = 1.0f;
        PlayerInputs[0].actions[1] = 1;
    }
    if(GetKeyState('D') & 0x8000) {
        PlayerInputs[0].fChannels[1] = -1.0f;
        PlayerInputs[0].actions[1] = 1;
    }
    if(GetKeyState('E') & 0x8000) { // Action / stabilize space suit? Channel 14
        PlayerInputs[0].fChannels[14] = 1.0f;
        PlayerInputs[0].actions[14] = 4;
    }
    if(GetKeyState('Q') & 0x8000) { // Trigger - channel 9
        PlayerInputs[0].fChannels[9] = 1.0f;
        PlayerInputs[0].actions[9] = 4;
    }
    if(GetKeyState('1') & 0x8000) { // Alt fire switch - channel 12
        PlayerInputs[0].fChannels[12] = 1.0f;
        PlayerInputs[0].actions[12] = 4;
    }
    if(GetKeyState('P') & 0x8000) { // Pause / Start - channel 30
        PlayerInputs[0].fChannels[30] = 1.0f;
        PlayerInputs[0].actions[30] = 4;
    }
    // 1 for continously-held actions (eg move, scope zoom)?
    // 4 for discrete actions (eg trigger, stabilize spacesuit) - should be true for 1 frame only to avoid repeatedly performing action

    // Channel 0: Aim left/right (+: Right)
    // Channel 1: Move left/right (+: Left)
    // Channel 2: Move forward/backward (+: Forward)
    // Channel 3: ???
    // Channel 4: ???
    // Channel 5: Aim up/down (+: Up)
    // Channel 6: ???
    // Channel 7: In space move Up/Down (+: Up)
    // Channel 8-18: ??? 
    // Channel 9: Fire
    // Channel 10-11: ???
    // Channel 12: Alt fire
    // Channel 12-18: ???
    // Channel 19: Zoom
    // Channel 20-29: ???
    // Channel 30: Pause/start
    // Channel 31-39: ??? 

}

// Does not need to be injected, it's only called from the game loop which we've replaced
void Input_Update(void) {

    // Game functions - poll, compensate stick, map from keys to actions
    void (*funcPtr)(void) = (void (*)(void))(0x0006cf50);
    funcPtr();

    // Our added function - keyboard input
    Inject_KeyboardInput();
}

// AUTOINJECT
unsigned short Input_Action(short playerNum,unsigned int action,unsigned char flags) {
  
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
float Input_Actionf(short playerNum,unsigned int action, unsigned char flags) {
  
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
void Input_ClearAction(short playerNum,unsigned int action) {
  
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
void Input_SetAction(short playerNum,unsigned int action,unsigned char val) {

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
