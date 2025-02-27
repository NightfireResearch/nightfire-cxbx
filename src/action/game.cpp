#include "actionhelpers.h"
#include "input.h"
#include "memory.h"
#include "game.h"

#include <cstring>

// Functions taking void and returning through registers are fine in either __cdecl or __stdcall
// It's only when they take arguments that the calling convention matters
// AUTOGEN
void __stdcall Sound_UpdateListeners(void);
// AUTOGEN
void __stdcall Camera_UpdateAll(void);
// AUTOGEN
int** __stdcall MenuManager_Update(void);
// AUTOGEN
void __stdcall MenuManager_Monitor(void);
// AUTOGEN
void __stdcall Mission_Update(void);
// AUTOGEN
void __stdcall MP_Update(void);
// AUTOGEN
void __stdcall Text_Update2Line(void);
// AUTOGEN
void __stdcall Text_Update(void);
// AUTOGEN
void __stdcall UpdateAllShards(void);
// AUTOGEN
void __stdcall Env_Update(void);
// AUTOGEN
void __stdcall SSys_Monitor(void);
// AUTOGEN
void __stdcall Light_Update(void);
// AUTOGEN
void __cdecl control_movement_object_handler(char);
// AUTOGEN
void __stdcall psiDecompressWoman(void);
// AUTOGEN
uint GS_IsPaused(ushort a);



#define BackgroundMovieHashcode U32_AT(0x002ae288)

// FUNC_AT(000dcd90)
bool movieFinished(void) {
  return BackgroundMovieHashcode == 0;
}


#define FreezeGame U8_AT(0x001fec48)
#define sloflag U16_AT(0x001fec64)
#define ScriptCam U32_AT(0x001f6678)
#define switch_allowFreeze U32_AT(0x0025d79c)


void psiPreGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2
void psiPostGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2



// Process the gameplay / update the state of the world and UI
// AUTOINJECT
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
    control_movement_object_handler(0);
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

#define StackIndex U16_AT(0x0017bfe8)
#define glb_viewer_6 U32_AT(0x001f6634)
#define SkipCodeFrame U8_AT(0x001f6564)
#define VIDEO_FRAME_RATE U32_AT(0x0017c0f0)
#define FRAME_RATE_INT U32_AT(0x0017c0f4)
#define _FRAME_RATE FLOAT_AT(0x0017c0f8)
#define FRAME_RATE_DIV FLOAT_AT(0x0017c0fc)
#define FRAME_RATE_MUL FLOAT_AT(0x0017c100)
#define REC_FRAME_RATE FLOAT_AT(0x0017c104)
#define inhibitGameDraw U8_AT(0x001f65c0)
uint *GameStateStack = (uint*)0x0017bff0; // Not zero-initialised - first entry must be 1
#define MainLoopCycles U32_AT(0x001f65bc)
#define VideoFrames U32_AT(0x001f65b8)
#define DAT_001f65ac U8_AT(0x001f65ac)
#define LoadTimeStart U32_AT(0x001f65cc)

#define GameState (*((GameState_t*)0x001f6580))
#define CheatInfo (*((CheatInfo_t*)0x001f65dc))
#define MPSettings (*((MPSettings_t*)0x0025fe38))
#define MPGame (*((MPGame_t*)0x00262738))
#define GlobalVars (*((GlobalVars_t*)0x001f6568))
#define PTPDATA (*((sNightFireShared_tag*)0x001d7e90))

#define CONST_ZERO_VECTOR (*((_VECTOR*)0x0029d728))
#define GRAVITY_VECTOR (*((_VECTOR*)0x001f6648))
#define CONST_UP_VECTOR (*((_VECTOR*)0x0029d71c))
#define MAYBE_CONST_FORWARD_VECTOR (*((_VECTOR*)0x0029d6d4))
#define MAT_IDENTITY (*((_MATRIX*)0x0029d6e0))

// AUTOGEN
void __cdecl psiLaunchDriving(void* a, uint b);
// AUTOGEN
void __stdcall Game_Draw(void);
// AUTOGEN
void __stdcall Boot_LoadPTPData(void);
// easy to port
// AUTOGEN 
void __stdcall Reset_MapLoadSettings(void);
// AUTOGEN
uint __stdcall Locks_Init(void);
// AUTOGEN
void __stdcall ResetMap_Load(void);
// AUTOGEN
void __stdcall psiStopBackgroundMovie(void);
// AUTOGEN
void Boot_GetPTPData(void **param_1,uint *param_2);


uint GameFlow_GetState(void) {
  if (StackIndex == 0) {
    return 0;
  }
  return GameStateStack[StackIndex - 1];
}

void set_InhibitGameDrawIfRequired(void) { 
  switch(GameFlow_GetState()) {
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
      break;
    }
}

uint GameFlow_PopState(void)
{ 
  if (StackIndex != 0) {
    StackIndex--;
    set_InhibitGameDrawIfRequired();
  }
  return GameFlow_GetState();
}

void GameFlow_QuickPushState(uint state) {
    StackIndex++;
    GameStateStack[StackIndex-1] = state;
    if (0x3f < StackIndex) {
      StackIndex = 0;
    }
    set_InhibitGameDrawIfRequired();
}

// AUTOGEN
double timestamp(void);

#define INITIALISATION_TIME (*((double*)0x002adf48))

// AUTOINJECT
void psiInitTimeIn100ths(void) {
  INITIALISATION_TIME = timestamp();
}

// AUTOINJECT
ulonglong psiGetTimeIn100ths(void) {
  return ((timestamp() - (float)INITIALISATION_TIME) * 0.1f);
}

// AUTOGEN
void __stdcall bootup_bootup(void);

// // NOAUTOINJECT
// void bootup_bootup(void) {

//   char *pcVar1;
//   int iVar2;
//   uint *puVar3;
//   ushort i;
//   undefined4 *puVar5;
//   int local_4;

//   memset(&GameState, 0, sizeof(GameState));
//   memset(&CheatInfo, 0, sizeof(CheatInfo));
//   memset(&MPSettings, 0, sizeof(MPSettings));
//   memset(&MPGame, 0, sizeof(MPGame));
//   memset(&GlobalVars, 0, sizeof(GlobalVars));
//   _SoundInfo = 0;
//   memset(&PTPDATA, 0, sizeof(sNightFireShared_tag));
  
//   psiInitTimeIn100ths();
//   GameState.difficultyModifier = 2;
//   PlayerInputs[2].controllerPort = 2;
//   MPSettings_respawnSelectionMode = 2;
//   DAT_001f65a0 = 0x80000002;
//   GameState.ReloadMenupage = 0x40000034;
//   MPSettings_numPlayers = 1;
//   _SoundInfo = 0x640064;
//   SubtitlesEnabled = 0;
//   DAT_001f6618 = 1;
//   PlayerInputs[0].controllerPort = 0;
//   PlayerInputs[1].controllerPort = 1;
//   PlayerInputs[3].controllerPort = 3;
//   DAT_00260050 = 0;
//   MPSettings_weaponSet = WEAPSET_NORMAL;
//   MPSettings_MaxDuration = 10;
//   MPSettings_MaxPoints = 10;
//   MPSettings_friendlyFire = 0;
//   MPSettings_MiniVehiclesEnabled = 0;
//   DAT_00260068 = 0;
//   DAT_0026006c = 0;
//   DAT_00260060 = 1;
//   MPSettings_ProfessionalModeTripleDamage = 0;
//   DAT_0026005c = 1;
//   MPSettings_GameMode = GM_ARENA;
//   i = 0;
//   iVar2 = 1;
//   puVar3 = &MPSettings_botsAndPlayers_teamId;
//   do {
//     puVar3[2] = 1;
//     *puVar3 = iVar2 - 1U & 1;
//     puVar3[3] = 0;
//     puVar3[1] = 0;
//     if (i < 4) {
//       pcVar1 = Txt_BindLabel(PLAYER,0);
//       local_4 = iVar2;
//     }
//     else {
//       pcVar1 = "Bot";
//       local_4 = iVar2 + -4;
//     }
//     sprintf((char *)(puVar3 + -8),"%s %d",pcVar1,local_4);
//     i = i + 1;
//     iVar2 = iVar2 + 1;
//     puVar3 = puVar3 + 0xc;
//   } while (i < 10);
//   GameState.ReloadGame = 1;

//   CONST_ZERO_VECTOR.x = 0.0f;
//   CONST_ZERO_VECTOR.y = 0.0f;
//   CONST_ZERO_VECTOR.z = 0.0f;

//   GRAVITY_VECTOR.x = 0.0f;
//   GRAVITY_VECTOR.y = -9.8f;
//   GRAVITY_VECTOR.z = 0.0f;

//   CONST_UP_VECTOR.x = 0.0f;
//   CONST_UP_VECTOR.y = 1.0f;
//   CONST_UP_VECTOR.z = 0.0f;
                          
//   MAYBE_CONST_FORWARD_VECTOR.x = 1.0;
//   MAYBE_CONST_FORWARD_VECTOR.y = 0.0;
//   MAYBE_CONST_FORWARD_VECTOR.z = 0.0;

//   Mat_IdentityT(&MAT_IDENTITY);

//   Input_Init();
//   PlrStat_Init();
//   __profiling_or_debugging_hook_point();
//   Input_Ready();
//   SFXSetMode(1);
//   NewScoresRef = 0x1d8838;
// }

// AUTOINJECT
void GameFlow_Main(void) {
  byte bVar2;
  uint local_8;
  void *local_4;
  
  MainLoopCycles++;

  if ((sloflag == 0) && !GS_IsPaused(0xffff)) {
    GameState.NumFramesUnpaused = GameState.NumFramesUnpaused + 1;
    VideoFrames += VIDEO_FRAME_RATE / FRAME_RATE_INT;
  }
  bVar2 = (byte)MainLoopCycles & 0x3f;
  if (0x1f < bVar2) {
    bVar2 = 0x3f - bVar2;
  }
  DAT_001f65ac = bVar2 << 1;

  switch(GameFlow_GetState()) {
  case 1:
    inhibitGameDraw = 0;
    GameState.NumFramesUnpaused = 1;
    MainLoopCycles = 1;
    VideoFrames = 1;
    GameState.maybeLoadingBlobs = 0;
    Mem_Init();
    bootup_bootup();
    GameFlow_QuickPushState(2);
    Boot_LoadPTPData();
    Reset_MapLoadSettings();
    return;
  case 2:
    LoadTimeStart = psiGetTimeIn100ths();
    if (GameState.ReloadGame != 0) {
      GameFlow_QuickPushState(3);
      return;
    }
    break;
  case 3:
    ResetMap_Load();
    return;
  case 4:
    Locks_Init();
    GameFlow_PopState();
    break;
  case 6:
    if (movieFinished() || Input_Action(-1,0x19,1)) {
      psiStopBackgroundMovie();
      GameFlow_PopState();
    }
    break;
  case 8:
    if ((glb_viewer_6 != 0) && (*(char *)(glb_viewer_6 + 0x205) != '\x01')) {
      GameFlow_PopState();
    }
    break;
  case 9:
    local_4 = (void *)0x0;
    local_8 = 0;
    Boot_GetPTPData(&local_4,&local_8);
    psiLaunchDriving(local_4,local_8);
    GameFlow_PopState();
    break;
  case 10:
    Input_Update();
    if (CheatInfo.someThing != 0) {
      GameFlow_PopState();
    }
    Game_Draw();
    return;
  case 0xc:
    if (glb_viewer_6 != 0) { // Cutscene camera?
      if (*(float *)(glb_viewer_6 + 0x1fc) < 0.0f == (*(float *)(glb_viewer_6 + 0x1fc) == 0.0f)) {
        *(float *)(glb_viewer_6 + 0x1fc) = *(float *)(glb_viewer_6 + 0x1fc) - 1.0f;
      }
      else {
        GameFlow_PopState();
      }
    }
    break;
  case 0xe:
    if ((glb_viewer_6 == 0) || (*(char *)(glb_viewer_6 + 0x205) == '\x01')) {
      Camera_UpdateAll();
      SkipCodeFrame = '\x01';
      break;
    }
    GameFlow_PopState();
    SkipCodeFrame = '\0';
    break;
  }

  if (SkipCodeFrame == '\0') {
    Game_Run();
  }

  if ((inhibitGameDraw == '\0') && (sloflag == 0)) {
    Game_Draw();
  }
  inhibitGameDraw = 0;
  return;
}

// No need for autoinjection, only called once from the function immediately below
void GS_SetRefreshRate(int gameFrameRate, int videoFrameRate) {

  VIDEO_FRAME_RATE = videoFrameRate;

  _FRAME_RATE = (float) gameFrameRate;
  FRAME_RATE_INT = gameFrameRate;
  FRAME_RATE_DIV = _FRAME_RATE * 0.016666667f;
  FRAME_RATE_MUL = (1.0f / _FRAME_RATE) * 60.0f;
  REC_FRAME_RATE = 1.0f / _FRAME_RATE;

}

#define IsPalI U8_AT(0x002c5760)

// FUNC_AT(000e5fb0)
bool Graphics_IsPalI(void) {
  return IsPalI;
}

// FUNC_AT(000dd1d0)
void mainloop(void) {
  int refreshRate = Graphics_IsPalI() ? 50 : 60;
  GS_SetRefreshRate(refreshRate, refreshRate);
  GameFlow_Main();
}