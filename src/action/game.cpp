#include "actionhelpers.h"
#include "input.h"
#include "memory.h"
#include "game.h"
#include "game/mp/multiplayer.h" // for MPSettings

#include <cstring>
#include <cstdio>

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
uint *GameStateStack = (uint*)0x0017bff0; // Not zero-initialised - first entry must be 1

#define CheatInfo (*((CheatInfo_t*)0x001f65dc))
#define MPGame (*((MPGame_t*)0x00262738))
#define GlobalVars (*((GlobalVars_t*)0x001f6568))
#define PTPDATA (*((sNightFireShared_tag*)0x001d7e90))

#define CONST_ZERO_VECTOR (*((_VECTOR*)0x0029d728))
#define GRAVITY_VECTOR (*((_VECTOR*)0x001f6648))
#define CONST_UP_VECTOR (*((_VECTOR*)0x0029d71c))
#define MAYBE_CONST_FORWARD_VECTOR (*((_VECTOR*)0x0029d6d4))
#define MAT_IDENTITY (*((_MATRIX*)0x0029d6e0))

#define NewScoresRef PTR_AT(0x002790a0)

#define HintsEnabled U32_AT(0x001f6618)
#define SubtitlesEnabled U32_AT(0x001f6614)

#define SoundInfo U32_AT(0x001f65d8)

// AUTOGEN
void __cdecl psiLaunchDriving(void* a, uint b);
// AUTOGEN
void __stdcall Game_Draw(void);
// AUTOGEN
void __stdcall Boot_LoadPTPData(void);
// AUTOGEN
uint __stdcall Locks_Init(void);
// AUTOGEN
void __stdcall ResetMap_Load(void);
// AUTOGEN
void __stdcall psiStopBackgroundMovie(void);
// AUTOGEN
void Boot_GetPTPData(void **param_1,uint *param_2);

// AUTOINJECT
bool Menu_IsDrivingLevel(HASHCODE level) {
    switch(level) {
        case HT_Level_Driving_Paris:
        case HT_Level_Driving_Underwater:
        case HT_Level_Driving_JungleA:
        case HT_Level_Driving_SnowMobile:
        case HT_Level_Driving_Alps:
            return true;
    }
    return false;
}

bool IsMultiplayerMission(HASHCODE level) {
    switch(level) {
        case HT_Level_SpaceStation: // FIXME: This is weird to see here?
        case HT_Level_Facility:
        case HT_Level_Atlantis:
        case HT_Level_SkyRail:
        case HT_Level_SubPen:
        case HT_Level_StealthShip:
        case HT_Level_FortKnox:
        case HT_Level_MissileSilo:
        case HT_Level_SnowBlind:
        case HT_Level_Ravine:
        case 0x700004c:
            return true;
      }
      return false;
}

HASHCODE GetFmvForLevel(HASHCODE level) {
  int param_1 = 0;
  switch(level) {
    case HT_Level_HendersonA:
      param_1 = FMV_INTRO_MAYHEW_ENTRY;
      break;
    case HT_Level_HendersonB:
      param_1 = 0x7100002;
      break;
    case HT_Level_HendersonC:
      param_1 = 0x7100003;
      break;
    case HT_Level_HendersonD:
      param_1 = 0x7100004;
      break;
    case HT_Level_CastleExterior:
      param_1 = FMV_INTRO_STOLEN_HARDWARE;
      break;
    case HT_Level_CastleCourtyard:
      param_1 = 0x7100006;
      break;
    case HT_Level_CastleIndoors1:
      param_1 = 0x7100007;
      break;
    case HT_Level_CastleIndoors2:
      param_1 = 0x7100008;
      break;
    case HT_Level_TowerA:
      param_1 = FMV_INTRO_PHOENIX_TOWER;
      break;
    case HT_Level_TowerB:
      param_1 = 0x710000a;
      break;
    case HT_Level_TowerC:
      param_1 = 0x710000b;
      break;
    case HT_Level_PowerStationA1:
      param_1 = FMV_INTRO_POWERPLANT;
      break;
    case HT_Level_PowerStationA2:
      param_1 = 0x710000d;
      break;
    case 0x700000e:
      param_1 = 0x710000e;
      break;
    case 0x700000f:
      param_1 = 0x710000f;
      break;
    case 0x7000010:
      param_1 = 0x7100010;
      break;
    case HT_Level_Tower2A:
      param_1 = FMV_INTRO_PHOENIX_TOWER_2;
      break;
    case HT_Level_Tower2B:
      param_1 = 0x7100012;
      break;
    case HT_Level_Tower2C:
      param_1 = 0x7100013;
      break;
    case HT_Level_EvilBase:
      param_1 = FMV_INTRO_ISLAND_RADIOTOWER;
      break;
    case HT_Level_EvilSilo:
      param_1 = 0x7100015;
      break;
    case HT_Level_EvilBaseC:
      param_1 = 0x7100016;
      break;
    case HT_Level_SpaceStationD:
      param_1 = FMV_INTRO_SPACE_STATION;
      break;
    case HT_Level_Tower2Elevator:
      param_1 = 0x710004a;
    }
    return (HASHCODE)param_1;
}

#define IsWarmReset U8_AT(0x00279250)

// AUTOGEN
void __cdecl GameFlow_PushState(int state, float param_2, uint param_3);

// AUTOINJECT
void ResetMap_LevelToLoad(HASHCODE level, bool warmReset, bool skipFmv) {

  if(level == 0xFFFFFFFF)
    return;

  IsWarmReset = warmReset;
  switch(GameFlow_GetState()) {
    case 2:
    case 7:
    case 8:
    case 0xd:
    case 0xe:

      GameState.InhibitGameDraw = 1;
      GameState.isMultiplayerLevel = 0;

      if(Menu_IsDrivingLevel(level)) {

        GameFlow_PushState(9, 0.0, 0xff);
        GameState.NextLevelHashcode = level;

      } else {

        if(IsMultiplayerMission(level)) {
          GameState.isMultiplayerLevel = 1;
          MP_setLoadingSkins();
        }

        HASHCODE fmv = (HASHCODE)0;

        if(!skipFmv) {
          fmv = GetFmvForLevel(level);
        }
        
        GameState.NextLevelHashcode = (fmv ? fmv : level);
        GameFlow_PushState(3, 0.0, 0xff);

      }
    }
}

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
      GameState.InhibitGameDraw = 1;
      break;
    case 2:
    case 6:
    case 7:
    case 8:
    case 10:
    case 0xc:
    case 0xd:
    case 0xe:
    GameState.InhibitGameDraw = 0;
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

// AUTOGEN
void __stdcall Input_Init(void);

// AUTOGEN
void __stdcall Input_Ready(void);

// AUTOGEN
void __stdcall PlrStat_Init(void);

// AUTOGEN
void SFXSetMode(unsigned int mode);

// AUTOGEN
char* Txt_BindLabel(Action_TranslatedText a, unsigned int b);

// Used to insert a CALL location within a larger function that a debugger or profiler can hook into
void __profiling_or_debugging_hook_point(void) {
  return;
}

// Only used in these two functions, so no need to use the original location
// #define INITIALISATION_TIME (*((double*)0x002adf48))
double INITIALISATION_TIME;

// AUTOINJECT
void psiInitTimeIn100ths(void) {
  INITIALISATION_TIME = timestamp();
}

// AUTOINJECT
ulonglong psiGetTimeIn100ths(void) {
  return ((timestamp() - (float)INITIALISATION_TIME) * 0.1f);
}


// Only used in GameFlow_Main, so no need to inject
void bootup_bootup(void) {

  uint *puVar3;
  undefined4 *puVar5;
  int local_4;

  memset(&GameState, 0, sizeof(GameState));
  memset(&CheatInfo, 0, sizeof(CheatInfo));
  memset(&MPSettings, 0, sizeof(MPSettings));
  memset(&MPGame, 0, sizeof(MPGame));
  memset(&GlobalVars, 0, sizeof(GlobalVars));
  memset(&PTPDATA, 0, sizeof(sNightFireShared_tag));
  
  SoundInfo = 0;

  psiInitTimeIn100ths();

  PlayerInputs[0].controllerPort = 0;
  PlayerInputs[1].controllerPort = 1;
  PlayerInputs[2].controllerPort = 2;
  PlayerInputs[3].controllerPort = 3;

  GameState.difficultyModifier = 2;
  MPSettings.RespawnSelectionMode = 2;
  GameState.field8_0x20 = 0x80000002;
  GameState.ReloadMenupage = 0x40000034;
  MPSettings.numPlayers = 1;
  SoundInfo = 0x640064;
  SubtitlesEnabled = 0;
  HintsEnabled = 1;
  MPSettings.GunEmplacementsEnabled = 0;
  MPSettings.weaponSet = WEAPSET_NORMAL;
  MPSettings.MaxDuration = 10;
  MPSettings.MaxPoints = 10;
  MPSettings.FriendlyFire = 0;
  MPSettings.MiniVehiclesEnabled = 0;
  MPSettings.GrappleEnabled = 0;
  MPSettings.ExplosiveSceneryEnabled = 0;
  MPSettings.LocationDamageEnabled = 1;
  MPSettings.TripleDamageModifierProfessionalMode = 0;
  MPSettings.ShowTeamAndNameOverhead = 1;
  MPSettings.GameMode = GM_ARENA;
  
  for(int i = 0; i < 10; i++) {

    MPSettings.Player[i].TeamId = (i & 1) ? MI6 : PHOENIX;
    MPSettings.Player[i].SkinNum = 0;
    MPSettings.Player[i].SomeField2 = 1;
    MPSettings.Player[i].HealthModifier = 0;
  
    if(i < 4) {
      sprintf(MPSettings.Player[i].Name, "%s %d", Txt_BindLabel(PLAYER, 0), i + 1);
    } else {
      sprintf(MPSettings.Player[i].Name, "%s %d", "Bot", i - 3);
    }

  }

  GameState.ReloadGame = 1;

  CONST_ZERO_VECTOR.x = 0.0f;
  CONST_ZERO_VECTOR.y = 0.0f;
  CONST_ZERO_VECTOR.z = 0.0f;

  GRAVITY_VECTOR.x = 0.0f;
  GRAVITY_VECTOR.y = -9.8f;
  GRAVITY_VECTOR.z = 0.0f;

  CONST_UP_VECTOR.x = 0.0f;
  CONST_UP_VECTOR.y = 1.0f;
  CONST_UP_VECTOR.z = 0.0f;
                          
  MAYBE_CONST_FORWARD_VECTOR.x = 1.0;
  MAYBE_CONST_FORWARD_VECTOR.y = 0.0;
  MAYBE_CONST_FORWARD_VECTOR.z = 0.0;

  Mat_IdentityT(&MAT_IDENTITY);

  Input_Init();
  PlrStat_Init();
  __profiling_or_debugging_hook_point();
  Input_Ready();
  SFXSetMode(1);
  NewScoresRef = &(PTPDATA.Scoring);
}

// AUTOINJECT
void Reset_MapLoadSettings(void) {
  if(GameState.NextLevelHashcode == 0) {
    GameState.NextLevelHashcode = HT_Level_Menu_Pre;
  }
  ResetMap_LevelToLoad(GameState.NextLevelHashcode, false, false);
}

// AUTOINJECT
void GameFlow_Main(void) {
  byte bVar2;
  uint local_8;
  void *local_4;
  
  GameState.NumFrames++;

  if ((sloflag == 0) && !GS_IsPaused(0xffff)) {
    GameState.NumFramesUnpaused = GameState.NumFramesUnpaused + 1;
    GameState.VideoFrames += VIDEO_FRAME_RATE / FRAME_RATE_INT;
  }

  // vestigial logic, value never read?
  bVar2 = (byte)GameState.NumFrames & 0x3f;
  if (0x1f < bVar2) {
    bVar2 = 0x3f - bVar2;
  }
  GameState.maybeUnused = bVar2 << 1;

  switch(GameFlow_GetState()) {
  case 1:
    GameState.InhibitGameDraw = 0;
    GameState.NumFramesUnpaused = 1;
    GameState.NumFrames = 1;
    GameState.VideoFrames = 1;
    GameState.maybeLoadingBlobs = 0;
    Mem_Init();
    bootup_bootup();
    GameFlow_QuickPushState(2);
    Boot_LoadPTPData();
    Reset_MapLoadSettings();
    return;
  case 2:
    GameState.LoadTimeStart = psiGetTimeIn100ths();
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

  if ((GameState.InhibitGameDraw == '\0') && (sloflag == 0)) {
    Game_Draw();
  }
  GameState.InhibitGameDraw = 0;
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

// AUTOINJECT
bool Graphics_IsPalI(void) {
  return IsPalI;
}

// AUTOINJECT
void mainloop(void) {
  int refreshRate = Graphics_IsPalI() ? 50 : 60;
  GS_SetRefreshRate(refreshRate, refreshRate);
  GameFlow_Main();
}