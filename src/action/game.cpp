#include "helpers.h"
#include "input.h"
#include "memory.h"



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



// Process the gameplay / update the state of the world and UI 
void Game_Run(void) {
  
  psiPreGame_Run();
  Input_Update();


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

#define GameStateStack U16_AT(0x0017bfe8)
#define glb_viewer_6 U32_AT(0x001f6634)
#define SkipCodeFrame U8_AT(0x001f6564)
#define gs_NumFramesUnpaused U32_AT(0x001f65b4)
#define FRAME_RATE_INT U32_AT(0x0017c0f4)
#define inhibitGameDraw U8_AT(0x001f65c0)
#define maybe_gs_LoadingBlobs U32_AT(0x001f65b0)
#define gameState_ReloadGame U32_AT(0x001f6598)

void bootup_bootup(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000197d0);
    return funcPtr();
}
void psiLaunchDriving(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000dfb50);
    return funcPtr();
}
void Game_Draw(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000dac20);
    return funcPtr();
}
void Boot_LoadPTPData(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x00019db0);
    return funcPtr();
}
void Reset_MapLoadSettings(void) { // easy to port
    void (*funcPtr)(void) = (void (*)(void))(0x000be080);
    return funcPtr();
}
void Locks_Init(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000d01c0);
    return funcPtr();
}
void ResetMap_Load(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000bfb60);
    return funcPtr();
}
void psiStopBackgroundMovie(void) {
    void (*funcPtr)(void) = (void (*)(void))(0x000dccc0);
    return funcPtr();
}


void set_InhibitGameDrawIfRequired(void)

{
  undefined4 uVar1;
  
  if (GameStateStack == 0) {
    uVar1 = 0;
  }
  else {
    uVar1 = *(undefined4 *)((uint)GameStateStack * 4 + 0x17bfec);
  }
  if (true) {
    switch(uVar1) {
    case 1:
    case 3:
    case 4:
    case 5:
    case 9:
      inhibitGameDraw = 1;
      break;
    case 2:
    case 6:
    case 7:
    case 8:
    case 10:
    case 0xc:
    case 0xd:
    case 0xe:
      inhibitGameDraw = 0;
      return;
    }
  }
  return;
}

undefined4 maybePopState(void)
{
  ushort extraout_DX;
  
  if (GameStateStack != 0) {
    GameStateStack = GameStateStack + -1;
    set_InhibitGameDrawIfRequired();
    return (&DAT_0017bff0)[extraout_DX];
  }
  return 0;
}

void GameFlow_Main(void)
{
  uint uVar1;
  undefined4 curFlowState;
  byte bVar2;
  ulonglong uVar3;
  uint local_8;
  void *local_4;
  
  DAT_001f65bc = DAT_001f65bc + 1;
  if ((sloflag == 0) && (uVar1 = GS_IsPaused(0xffff), (char)uVar1 == '\0')) {
    gs_NumFramesUnpaused = gs_NumFramesUnpaused + 1;
    DAT_001f65b8 = DAT_001f65b8 + DAT_0017c0f0 / FRAME_RATE_INT;
  }
  bVar2 = (byte)DAT_001f65bc & 0x3f;
  if (0x1f < bVar2) {
    bVar2 = 0x3f - bVar2;
  }
  DAT_001f65ac = bVar2 << 1;
  if (GameStateStack == 0) {
    curFlowState = 0;
  }
  else {
    curFlowState = *(undefined4 *)((uint)GameStateStack * 4 + 0x17bfec);
  }
  if (false) goto switchD_0006ad26_caseD_5;
  switch(curFlowState) {
  case 1:
    inhibitGameDraw = 0;
    gs_NumFramesUnpaused = 1;
    DAT_001f65bc = 1;
    DAT_001f65b8 = 1;
    maybe_gs_LoadingBlobs = 0;
    Mem_Init();
    bootup_bootup();
    uVar1 = (uint)GameStateStack;
    GameStateStack = GameStateStack + 1;
    (&DAT_0017bff0)[uVar1] = 2;
    if (0x3f < GameStateStack) {
      GameStateStack = 0;
    }
    set_InhibitGameDrawIfRequired();
    Boot_LoadPTPData();
    Reset_MapLoadSettings();
    return;
  case 2:
    uVar3 = psiGetTimeIn100ths();
    DAT_001f65cc = (undefined4)uVar3;
    if (gameState_ReloadGame != 0) {
      uVar1 = (uint)GameStateStack;
      GameStateStack = GameStateStack + 1;
      (&DAT_0017bff0)[uVar1] = 3;
      if (0x3f < GameStateStack) {
        GameStateStack = 0;
      }
      set_InhibitGameDrawIfRequired();
      return;
    }
    break;
  case 3:
    ResetMap_Load();
    return;
  case 4:
    Locks_Init();
    maybePopState();
    break;
  case 6:
    curFlowState = movieFinished();
    if (((char)curFlowState != '\0') || (uVar1 = Input_Action(-1,0x19,1), (short)uVar1 != 0)) {
      psiStopBackgroundMovie();
      maybePopState();
    }
    break;
  case 8:
    if ((glb_viewer_6 != 0) && (*(char *)(glb_viewer_6 + 0x205) != '\x01')) {
      maybePopState();
    }
    break;
  case 9:
    local_4 = (void *)0x0;
    local_8 = 0;
    Boot_GetPTPData(&local_4,&local_8);
    psiLaunchDriving(local_4,local_8);
    maybePopState();
    break;
  case 10:
    Input_Update();
    if (_DAT_001f65ec != 0) {
      maybePopState();
    }
    Game_Draw();
    return;
  case 0xc:
    if (glb_viewer_6 != 0) {
      if (*(float *)(glb_viewer_6 + 0x1fc) < 0.0f == (*(float *)(glb_viewer_6 + 0x1fc) == 0.0f)) {
        *(float *)(glb_viewer_6 + 0x1fc) = *(float *)(glb_viewer_6 + 0x1fc) - 1.0f;
      }
      else {
        maybePopState();
      }
    }
    break;
  case 0xe:
    if ((glb_viewer_6 == 0) || (*(char *)(glb_viewer_6 + 0x205) == '\x01')) {
      Camera_UpdateAll();
      SkipCodeFrame = '\x01';
      goto LAB_0006af06;
    }
    maybePopState();
    SkipCodeFrame = '\0';
    goto LAB_0006af01;
  }
switchD_0006ad26_caseD_5:
  if (SkipCodeFrame == '\0') {
LAB_0006af01:
    Game_Run();
  }
LAB_0006af06:
  if ((inhibitGameDraw == '\0') && (sloflag == 0)) {
    Game_Draw();
  }
  inhibitGameDraw = 0;
  return;
}