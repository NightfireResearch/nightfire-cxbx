#include <stdio.h>
#include <windows.h>

void Inject_KeyboardInput(void) {

    // Inject WASD control into controller 1 for now

    void* ps = (void*)0x001fe6d0;
    float* fChannels = (float*)((int)ps + 0x14);
    char* actions = (char*)((int)ps + 0x104);

    // if(GetKeyState(VK_UP) & 0x8000) {
    //     printf("Has UP\n");
    // }
    // if(GetKeyState(VK_DOWN) & 0x8000) {
    //     printf("Has DOWN\n");
    // }

    if(GetKeyState('W') & 0x8000) {
        fChannels[2] = 1.0f;
        actions[2] = 1;
    }
    if(GetKeyState('S') & 0x8000) {
        fChannels[2] = -1.0f;
        actions[2] = 1;
    }
    if(GetKeyState('A') & 0x8000) {
        fChannels[1] = 1.0f;
        actions[1] = 1;
    }
    if(GetKeyState('D') & 0x8000) {
        fChannels[1] = -1.0f;
        actions[1] = 1;
    }
    if(GetKeyState('E') & 0x8000) { // Action / stabilize space suit?
        fChannels[14] = 1.0f;
        actions[14] = 4;
    }
    if(GetKeyState('Q') & 0x8000) { // Trigger
        fChannels[9] = 1.0f;
        actions[9] = 4;
    }
    if(GetKeyState('1') & 0x8000) { // Alt fire switch
        fChannels[12] = 1.0f;
        actions[12] = 4;
    }
    if(GetKeyState('P') & 0x8000) { // Pause / Start
        fChannels[30] = 1.0f;
        actions[30] = 4;
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

void Input_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x0006cf50);
    funcPtr();
    Inject_KeyboardInput();
}

unsigned short __cdecl Input_Action(short playerNum,unsigned int action,unsigned short flags) {
  
    // Any player (specified with a negative value)
    if (playerNum < 0) {
        unsigned short uVar2 = 0;
        for(int i = 0; i<4; i++)
            uVar2 |= Input_Action(i,action,flags);
        return uVar2;
    }

    // One specific player

    // Pointer arithmetic gets us:
    // - PlayerInputs.player[playerNum].actions[action]
    // - PlayerInputs.player[playerNum].fChannels[action]

    if ((playerNum < 4) && ((*(char*)(0x001fe6d0 + 1376*playerNum + 0x104 + action) & flags) != 0)) {
      return (unsigned short)(int)(*(float*)(0x001fe6d0 + 1376*playerNum + 0x14 + 4*action) * 100.0);
    }

    // Invalid player number
    return 0;
}
