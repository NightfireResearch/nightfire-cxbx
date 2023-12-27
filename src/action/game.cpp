#include "helpers.h"

#include <stdio.h>
#include <windows.h>

void Input_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x0006cf50);
    return funcPtr();
}

void Sound_UpdateListeners(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000cc1c0);
    return funcPtr();
}

void Camera_UpdateAll(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000260c0);
    return funcPtr();
}

void MenuManager_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x00094370);
    return funcPtr();
}

void MenuManager_Monitor(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x00094720);
    return funcPtr();
}

void Mission_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x0009c280);
    return funcPtr();
}

void MP_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000a2b80);
    return funcPtr();
}

void Text_Update2Line(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000d2400);
    return funcPtr();
}
void Text_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000d2360);
    return funcPtr();
}

void UpdateAllShards(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x0001fd60);
    return funcPtr();
}

void Env_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x00068bf0);
    return funcPtr();
}

void SSys_Monitor(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000cf8d0);
    return funcPtr();
}

void Light_Update(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x0006d660);
    return funcPtr();
}

void control_movement_object_handler(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x0002dd00);
    return funcPtr();
}

void psiDecompressWoman(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000e0020);
    return funcPtr();
}

bool movieFinished(void) {
    bool (*funcPtr)(void) = (bool (*)(void))(0x000dcd90);
    return funcPtr();
}

bool GS_IsPaused(ushort a) {
    bool (*funcPtr)(ushort) = (bool (*)(ushort))(0x0006afb0);
    return funcPtr(a);
}

#define FreezeGame U8_AT(0x001fec48)
#define sloflag U16_AT(0x001fec64)
#define ScriptCam U32_AT(0x001f6678)
#define switch_allowFreeze U32_AT(0x0025d79c)


void psiPreGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2
void psiPostGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2

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

    static int CH = 0;
    static bool debounce = false;

    bool doA = GetKeyState('A');
    if(doA && !debounce) {
        CH++;
        CH%=40;
        printf("Ch: %i\n", CH);
        debounce = true;
    }
    if(!doA && debounce)
        debounce = false;

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
    // The above works for continously-held actions (eg move, scope zoom)
    // ??? for discrete actions (eg trigger)

    // Channel 0: Aim left/right (+: Right)
    // Channel 1: Move left/right (+: Left)
    // Channel 2: Move forward/backward (+: Forward)
    // Channel 3: ???
    // Channel 4: ???
    // Channel 5: Aim up/down (+: Up)
    // Channel 6: ???
    // 7: In space move Up/Down (+: Up)
    // 8: 
    // 19: Zoom
    // 20: 

}

// Process the gameplay / update the state of the world and UI 
void Game_Run(void) {
  
  psiPreGame_Run();
  Input_Update();

  Inject_KeyboardInput();

  if ((FreezeGame != '\0') && (switch_allowFreeze != '\0')) return;

  Sound_UpdateListeners();

  if (sloflag) {
    Camera_UpdateAll();
    psiPostGame_Run();
    return;
  }

  MenuManager_Update();
  MenuManager_Monitor();
  Mission_Update();
  MP_Update();
  if (ScriptCam == 0) {

    if (!movieFinished()) 
      goto LAB_0006aafe;

    if (!GS_IsPaused(0xffff))
      Text_Update2Line();

  }
  else {
LAB_0006aafe:
    Text_Update();
  }

  UpdateAllShards();
  Env_Update();

  if (!GS_IsPaused(0xffff)) {
    SSys_Monitor();
    Light_Update();
    control_movement_object_handler();
    Camera_UpdateAll();
    return;
  }

  if (ScriptCam != 0) {
    Light_Update();
  }

  psiDecompressWoman();

  Camera_UpdateAll();
  psiPostGame_Run();
  return;

}

