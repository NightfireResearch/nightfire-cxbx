#include "game/obj/Creature.h"
#include "actionhelpers.h"
#include "input.h"
#include "memory.h"
#include "game.h"
#include "game/mp/multiplayer.h" // for MPSettings
#include "game/sp/SSys.h"
#include "ui/MenuManager.h"
#include "engine/Text.h"
#include "gfx/Font.h"
#include "util/Random.h"
#include "engine/XboxSettings.h"
#include "engine/Direct3D/GraphicsSystem.h" // Gfx
#include "engine/Fmv.h"
#include "engine/Direct3D/d3dSeam.h" // the immediate-mode quads psiUpdateBackgroundMovie draws

#include "../driving/platform/Host.h"
#include "../driving/platform/Win32Types.h"   // LARGE_INTEGER

#include <cstring>
#include <cstdio>

#include <stdio.h>
#include "engine/XboxSystem.h"
#include "devtools/Teleport.h"
#include "devtools/Demo.h"
#include "engine/LaunchOptions.h"
#include "game/obj/Break.h"
#include "game/view.h"
#include "engine/Camera.h"
#include "engine/viewer.h"
#include "engine/AIPath.h"
#include "engine/Anim.h"
#include "engine/Collide.h"
#include "engine/Loader.h"
#include "engine/Script.h"
#include "engine/Woman.h"
#include "engine/parsemap.h"
#include "engine/psiGraphics.h"
#include "engine/psiLight.h"
#include "game/drone/Drone.h"
#include "game/obj/Copter.h"
#include "game/obj/Door.h"
#include "game/obj/Effect.h"
#include "game/obj/Env.h"
#include "game/obj/Light.h"
#include "game/obj/Player.h"
#include "game/obj/ScriptPlayer.h"
#include "game/obj/Sensor.h"
#include "game/obj/build.h"
#include "game/obj/bullet.h"
#include "game/obj/car.h"
#include "game/obj/control.h"
#include "game/sp/Mission.h"
#include "game/sp/SwitchChannels.h"
#include "game/sp/PlayerStats.h"
#include "ui/Menu.h"
#include "engine/LoadSave.h"
#include "sound/SFX.h"
#include "gfx/LowLevel.h"
#include "gfx/Sprite.h"
#include <math.h>
#include "devtools/MenuProbe.h"
#include "sound/SFX.h"
#include "math/X87Ops.h"
#if NIGHTFIRE_DATA_PS2
#include "ps2/Ps2Loader.h"
#endif

// XBE_GLOBAL(0x00262738, 0x230)
MPGameStruct MPGame;

// XBE_GLOBAL(0x001f661c, 0x2c)
viewer_tag *glb_viewer[NUM_VIEWERS];
// XBE_GLOBAL(0x001f6674, 0x4)
world_tag *glb_world;
// XBE_GLOBAL(0x002774b8, 0x10)
BLData *glb_blokes[NUM_PLAYERS];
// XBE_GLOBAL(0x001f6654, 0x10)
obj_tag *glb_players[NUM_PLAYERS];
// XBE_GLOBAL(0x0029d6d4, 0xc)
_VECTOR CONST_X_VECTOR;
// XBE_GLOBAL(0x0029d6e0, 0x3c)
_MATRIX MAT_IDENTITY;
// XBE_GLOBAL(0x0029d71c, 0xc)
_VECTOR CONST_UP_VECTOR;
// XBE_GLOBAL(0x001f6648, 0xc)
_VECTOR GRAVITY_VECTOR;
// XBE_GLOBAL(0x0029d728, 0xc)
_VECTOR CONST_ZERO_VECTOR;

// XBE_GLOBAL(0x0017c0f0, 0x4)
uint32_t VIDEO_FRAME_RATE = 60;
// XBE_GLOBAL(0x0017c0f4, 0x4)
uint32_t FRAME_RATE_INT = 60;
// XBE_GLOBAL(0x0017c0f8, 0x4)
float _FRAME_RATE = 60.0f;
// XBE_GLOBAL(0x0017c0fc, 0x4)
float FRAME_RATE_DIV = 1.0f;
// XBE_GLOBAL(0x0017c100, 0x4)
float FRAME_RATE_MUL = 1.0f;
// XBE_GLOBAL(0x0017c104, 0x4)
float REC_FRAME_RATE = 0.016666668f;

// XBE_GLOBAL(0x001f6580, 0x58)
GameState_t GameState;

// XBE_GLOBAL(0x002ae288, 0x4)
uint32_t BackgroundMovieHashcode;

// Functions taking void and returning through registers are fine in either __cdecl or __stdcall
// It's only when they take arguments that the calling convention matters

// AUTOINJECT
bool GS_IsPaused(short playerNum) {

  // A specific player?
  if(playerNum != -1)
    return MPGame.players[playerNum].paused;

  // Any player?
  if(MPSettings.isMultiplayer) {
    for(uint i = 0; i < MPSettings.numPlayers; i++) {
      if(MPGame.players[i].paused)
        return true;
    }
    return false;
  }

  // Single player, use GameState
  return GameState.SomeAlternatePauseState;
}

// AUTOINJECT
void GS_PausePlayer(char pause, ushort playerNum) {
  if(playerNum < 4)
    MPGame.players[playerNum].paused = pause;
}

// FUNC_AT(000dcd90)
bool movieFinished(void) {
  return BackgroundMovieHashcode == 0;
}

// XBE_GLOBAL(0x001fec48, 0x1)
uint8_t FreezeGame;
// XBE_GLOBAL(0x001fec64, 0x2)
static uint16_t sloflag;
// ScriptCam is defined in engine/Script.h (same address, HASHCODE-typed)
// XBE_GLOBAL(0x0025d79c, 0x1)
char switch_allowFreeze;  // a byte: C_CHCHALLOWFREEZE_Handler writes it

void psiPreGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2
void psiPostGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2

// Process the gameplay / update the state of the world and UI
// AUTOINJECT
void Game_Run(void) {

  psiPreGame_Run();
  Input_UpdateWithMouse();
  Devtools_Tick();   // devtools: F8/F9 teleport, F7 frame dump, per-frame shadow tests

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

    if (!GS_IsPaused(-1))
      Text_Update2Line();

  }
  else {
LAB_0006aafe:
    Text_Update();
  }

  UpdateAllShards();
  Env_Update();

  if (!GS_IsPaused(-1)) {
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

// XBE_GLOBAL(0x0017bfe8, 0x2)
uint16_t StackIndex = 1;
// XBE_GLOBAL(0x001f6564, 0x1)
static uint8_t SkipCodeFrame;
// XBE_GLOBAL(0x0017bff0, 0x100)
uint GameStateStack[64] = {1};  // Not zero-initialised - first entry must be 1

// XBE_GLOBAL(0x001f65dc, 0x30)
CheatInfo_t CheatInfo;
// XBE_GLOBAL(0x001f6568, 0x18)
GlobalVars_t GlobalVars;
// The pages handed between the engines, one after the other as the game has them: LS_LoadFromBuffer reads 0x12c0
// bytes from PTPDATA, on into the page coming back, and psiGetDrivingData fills that one with 0xc00 bytes, 0x1b0 past
// its end
typedef struct PTP_PAGES {
    sNightFireShared_tag data;      // PTPDATA, what goes to the driving engine and is kept
    sNightFireShared_tag in;        // PTPDataIn, what psiGetDrivingData fills
    uint8_t past[0x1b0];
} PTP_PAGES;
static_assert(sizeof(PTP_PAGES) == 0x1650, "the pages are 0x1650 bytes");
// XBE_GLOBAL(0x001d7e90, 0x1650)
static PTP_PAGES PTPPages;
#define PTPDATA (PTPPages.data)
#define PTPDataIn (PTPPages.in)

// XBE_GLOBAL(0x001f6618, 0x4)
uint32_t HintsEnabled;
// XBE_GLOBAL(0x001f6614, 0x4)
uint32_t SubtitlesEnabled;

// XBE_GLOBAL(0x001f65d8, 0x4)
uint32_t SoundInfo;

// Maybe hashcode of playing FMV

// AUTOINJECT
void psiStopBackgroundMovie(void) {
    BackgroundMovieCleanup();
    BackgroundMovieHashcode = 0;
}

// XBE_GLOBAL(0x002ae3f0, 0x100)
static char BackgroundMovieFilename[0x100];
// XBE_GLOBAL(0x002ae28c, 0x1)
static uint8_t LoopingMovie;
// XBE_GLOBAL(0x00194818, 0x4)
static uint32_t BackgroundMovieVolume = 0x64;

// AUTOINJECT
void psiStartBackgroundMovie(HASHCODE hashcode, char looping, int volume) {
  int scaledVolume = (volume * 90) / 100;

  if(scaledVolume > 100)
    scaledVolume = 100;

  if(hashcode == HT_Level_Menu_EALogo) {
    if(!IsNotPalI()) {
      switch(Language_Get()) {
        case 2:
          hashcode = HT_Level_Menu_EALogo_FR;
          break;
        case 3:
          hashcode = HT_Level_Menu_EALogo_DE;
          break;
        case 6:
          hashcode = HT_Level_Menu_EALogo_ES;
          break;
        default:
          break;
      }
    }
  }

  BackgroundMovieCleanup();
  BackgroundMovieHashcode = 0;

  if (hashcode != 0x4e504c59) {
    LoopingMovie = (looping != 0);
    sprintf(BackgroundMovieFilename,"%08x.xmv",hashcode);
  }
  BackgroundMovieVolume = scaledVolume;
  BackgroundMovieHashcode = hashcode;
  BackgroundMovieSetVolume(scaledVolume);
  printf("Playing background movie %s\n", BackgroundMovieFilename);
  BackgroundMoviePlayFile(BackgroundMovieFilename);

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
        case HT_Level_Unnamed4C:
            return true;
      }
      return false;
}

HASHCODE GetLevelWithFmv(HASHCODE level) {
  int fmvLevel = 0;
  switch(level) {
    case HT_Level_HendersonA:
      fmvLevel = HT_Level_HendersonA_Intro;
      break;
    case HT_Level_HendersonB:
      fmvLevel = HT_Level_HendersonB_Intro;
      break;
    case HT_Level_HendersonC:
      fmvLevel = HT_Level_HendersonC_Intro;
      break;
    case HT_Level_HendersonD:
      fmvLevel = HT_Level_HendersonD_Intro;
      break;
    case HT_Level_CastleExterior:
      fmvLevel = HT_Level_CastleExterior_Intro;
      break;
    case HT_Level_CastleCourtyard:
      fmvLevel = HT_Level_CastleCourtyard_Intro;
      break;
    case HT_Level_CastleIndoors1:
      fmvLevel = HT_Level_CastleIndoors1_Intro;
      break;
    case HT_Level_CastleIndoors2:
      fmvLevel = HT_Level_CastleIndoors2_Intro;
      break;
    case HT_Level_TowerA:
      fmvLevel = HT_Level_TowerA_Intro;
      break;
    case HT_Level_TowerB:
      fmvLevel = HT_Level_TowerB_Intro;
      break;
    case HT_Level_TowerC:
      fmvLevel = HT_Level_TowerC_Intro;
      break;
    case HT_Level_PowerStationA1:
      fmvLevel = HT_Level_PowerStationA1_Intro;
      break;
    case HT_Level_PowerStationA2:
      fmvLevel = HT_Level_PowerStationA2_Intro;
      break;
    case HT_Level_Cut_Level1:
      fmvLevel = HT_Level_Cut_Level1_Intro;
      break;
    case HT_Level_Cut_Level2:
      fmvLevel = HT_Level_Cut_Level2_Intro;
      break;
    case HT_Level_Cut_Level3:
      fmvLevel = HT_Level_Cut_Level3_Intro;
      break;
    case HT_Level_Tower2A:
      fmvLevel = HT_Level_Tower2A_Intro;
      break;
    case HT_Level_Tower2B:
      fmvLevel = HT_Level_Tower2B_Intro;
      break;
    case HT_Level_Tower2C:
      fmvLevel = HT_Level_Tower2C_Intro;
      break;
    case HT_Level_EvilBase:
      fmvLevel = HT_Level_EvilBase_Intro;
      break;
    case HT_Level_EvilSilo:
      fmvLevel = HT_Level_EvilSilo_Intro;
      break;
    case HT_Level_EvilBaseC:
      fmvLevel = HT_Level_EvilBaseC_Intro;
      break;
    case HT_Level_SpaceStationD:
      fmvLevel = HT_Level_SpaceStationD_Intro;
      break;
    case HT_Level_Tower2Elevator:
      fmvLevel = HT_Level_Tower2Elevator_Intro;
      break;
    default: // No FMV, just load the level
      fmvLevel = level;
      break;
    }
    return (HASHCODE)fmvLevel;
}

// XBE_GLOBAL(0x00279250, 0x1)
uint8_t IsWarmReset;

// AUTOINJECT
void GS_PauseGame(bool pause) {

    GameState.SomeAlternatePauseState = pause;

    for(int i = 0; i < ARRAY_SIZE(MPGame.players); i++) {
        MPGame.players[i].paused = pause;
    }

    if (pause) {
        PlarStat_LogTimerPause(0);
    } else {
        PlarStat_LogTimerUnpause(0);
    }

}

// AUTOINJECT
void ResetMap_LevelToLoad(HASHCODE level, bool warmReset, bool skipFmv) {

  if(level == 0xFFFFFFFF)
    return;

  IsWarmReset = warmReset;
  switch(GameFlow_GetState()) {
    case GAMEFLOW_GAME:
    case GAMEFLOW_FADE:
    case GAMEFLOW_FADING:
    case GAMEFLOW_FADE_FROZEN:
    case GAMEFLOW_FADING_FROZEN:

      GameState.InhibitGameDraw = 1;
      GameState.isMultiplayerLevel = 0;

      if(Menu_IsDrivingLevel(level)) {

        GameFlow_PushState(GAMEFLOW_DRIVING, 0.0, 0xff);
        GameState.NextLevelHashcode = level;

      } else {

        if(IsMultiplayerMission(level)) {
          GameState.isMultiplayerLevel = 1;
          MP_setLoadingSkins();
        }

        GameState.NextLevelHashcode = (skipFmv ? level : GetLevelWithFmv(level));
        GameFlow_PushState(GAMEFLOW_LOAD_LEVEL, 0.0, 0xff);

      }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The loading screen's picture for a level, and the text shown on it: a hint (one of the level's own, or one of a
// set shared by every level) and the level's objective. ResetMap_GenLoadScreen is the only caller.
// ---------------------------------------------------------------------------------------------------------------

// The hints shared by every level, which replace a level's own hint half the time (and always when it has none).
// 0x1000129 is not among them.
static const uint32_t kGeneralHints[] = {
    0x1000111, 0x1000112, 0x1000113, 0x1000114, 0x1000115, 0x1000116, 0x1000117, 0x1000118, 0x1000119, 0x100011a,
    0x100011b, 0x100011c, 0x100011d, 0x100011e, 0x100011f, 0x1000120, 0x1000121, 0x1000122, 0x1000123, 0x1000124,
    0x1000125, 0x1000126, 0x1000127, 0x1000128, 0x100012a, 0x100012b, 0x100012c, 0x100012d, 0x100012e, 0x100012f,
    0x1000130, 0x1000131, 0x1000132, 0x1000133, 0x1000134, 0x1000135, 0x1000136, 0x1000137, 0x1000138, 0x1000139,
    0x100013a, 0x100013b, 0x100013c,
};

// The hints for the levels that use the multiplayer loading screen.
static const uint32_t kMultiplayerHints[] = {
    0x100013e, 0x100013f, 0x1000140, 0x1000141, 0x1000142, 0x1000143, 0x1000144, 0x1000145, 0x1000146, 0x1000147,
    0x1000148, 0x1000149, 0x100014a, 0x100014b, 0x100014c, 0x100014d, 0x100014e, 0x100014f, 0x1000150, 0x1000151,
    0x1000152, 0x1000153, 0x1000154, 0x1000155, 0x1000156, 0x1000157, 0x1000158, 0x1000159, 0x100015a, 0x100015b,
    0x100015c, 0x100015d, 0x100015e, 0x100015f, 0x1000160, 0x1000161, 0x1000162, 0x1000163, 0x1000164, 0x1000165,
};

static char *LoadText(uint32_t label) {
    return (char *)Txt_BindLabel((Action_TranslatedText)label, 0);
}

// One of the hints at random, if the caller wants a hint. The random number is drawn only then - including for
// a level with a single hint, so the generator advances as the original's does.
template <size_t N>
static void PickHint(char **hintOut, const uint32_t (&hints)[N]) {
    if (hintOut == NULL)
        return;
    *hintOut = LoadText(hints[Rand_Random() % N]);
}

static void SetObjective(char **objectiveOut, uint32_t objective) {
    if (objectiveOut == NULL)
        return;
    *objectiveOut = LoadText(objective);
}

// Half the time, and whenever the level gave no hint of its own, one of the shared hints instead.
static void PickGeneralHint(char **hintOut) {
    if ((Rand_Random() & 1) != 0 && (hintOut == NULL || *hintOut != NULL))
        return;
    PickHint(hintOut, kGeneralHints);
}

HASHCODE ResetMap_LevelCode2Img(HASHCODE level, char **hintOut, char **objectiveOut) {
    if (hintOut != NULL)
        *hintOut = NULL;
    if (objectiveOut != NULL)
        *objectiveOut = NULL;

    HASHCODE image;
    switch (level) {
    // A hint from kMultiplayerHints, no objective, and no shared hint.
    case HT_Level_SpaceStation:
    case HT_Level_Facility:
    case HT_Level_Atlantis:
    case HT_Level_SkyRail:
    case HT_Level_SubPen:
    case HT_Level_StealthShip:
    case HT_Level_FortKnox:
    case HT_Level_MissileSilo:
    case HT_Level_SnowBlind:
    case HT_Level_Ravine:
    case HT_Level_Unnamed4C: {
        PickHint(hintOut, kMultiplayerHints);
        return HT_Texture_load_MP;
    }

    // The castle exterior keeps its one hint: no shared hint.
    case HT_Level_CastleExterior: {
        static const uint32_t hints[] = { 0x1000126 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000000);
        return HT_Texture_Castle;
    }
    case HT_Level_CastleCourtyard: {
        static const uint32_t hints[] = { 0x10000c0, 0x10000c1 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000001);
        image = HT_Texture_Castle;
        break;
    }
    case HT_Level_CastleIndoors1: {
        static const uint32_t hints[] = { 0x10000c2 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000002);
        image = HT_Texture_Castle;
        break;
    }
    case HT_Level_CastleIndoors2: {
        static const uint32_t hints[] = { 0x10000c3, 0x10000c4, 0x10000c5, 0x10000c6 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000003);
        image = HT_Texture_Castle;
        break;
    }

    case HT_Level_HendersonA: {
        static const uint32_t hints[] = { 0x10000d3, 0x10000d4 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000006);
        image = HT_Texture_Henderson;
        break;
    }
    case HT_Level_HendersonB: {
        static const uint32_t hints[] = { 0x10000c7, 0x10000c8, 0x10000cb, 0x10000d1 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000008);
        image = HT_Texture_Henderson;
        break;
    }
    case HT_Level_HendersonC: {
        static const uint32_t hints[] = { 0x10000ca, 0x10000cd, 0x10000ce, 0x10000c9, 0x10000d2 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400000a);
        image = HT_Texture_Henderson;
        break;
    }
    case HT_Level_HendersonD: {
        static const uint32_t hints[] = { 0x10000cf, 0x10000d0 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400000d);
        image = HT_Texture_Henderson;
        break;
    }

    case HT_Level_TowerA: {
        static const uint32_t hints[] = { 0x10000d5, 0x10000d8, 0x10000df, 0x10000e2 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000016);
        image = HT_Texture_Tower;
        break;
    }
    case HT_Level_TowerB: {
        static const uint32_t hints[] = { 0x10000d7, 0x10000d9, 0x10000db, 0x10000e0, 0x10000e3, 0x10000e4, 0x10000e6 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000019);
        image = HT_Texture_Tower;
        break;
    }
    case HT_Level_TowerC: {
        static const uint32_t hints[] = { 0x10000dc, 0x10000da, 0x10000dd, 0x10000de, 0x10000e1, 0x10000e5 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400001d);
        image = HT_Texture_Tower;
        break;
    }

    case HT_Level_PowerStationA1: {
        static const uint32_t hints[] = { 0x10000e7, 0x10000e9, 0x10000eb, 0x10000ed, 0x10000ef, 0x10000f1, 0x10000f4 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400002c);
        image = HT_Texture_Power;
        break;
    }
    case HT_Level_PowerStationA2: {
        static const uint32_t hints[] = { 0x10000e8, 0x10000ea, 0x10000ec, 0x10000ee, 0x10000f0, 0x10000f2, 0x10000f3,
                                          0x10000f5 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400002f);
        image = HT_Texture_Power;
        break;
    }

    // Tower2A's list really has 0x10000f8 twice (0x10000f7 never appears), so it comes up two times in three.
    // Tower2C has no hint of its own; the elevator keeps its one hint, with no shared hint.
    case HT_Level_Tower2A: {
        static const uint32_t hints[] = { 0x10000f6, 0x10000f8, 0x10000f8 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000036);
        image = HT_Texture_Tower2;
        break;
    }
    case HT_Level_Tower2B: {
        static const uint32_t hints[] = { 0x10000fe, 0x10000f9, 0x10000fa, 0x10000fc, 0x10000fd };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000037);
        image = HT_Texture_Tower2;
        break;
    }
    case HT_Level_Tower2C:
        if (hintOut != NULL)
            *hintOut = NULL;
        SetObjective(objectiveOut, 0x400003a);
        image = HT_Texture_Tower2;
        break;
    case HT_Level_Tower2Elevator: {
        static const uint32_t hints[] = { 0x10000fb };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000039);
        return HT_Texture_Tower2;
    }

    case HT_Level_EvilBase: {
        static const uint32_t hints[] = { 0x1000103, 0x1000104, 0x1000105, 0x1000106 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000049);
        image = HT_Texture_Evil;
        break;
    }
    case HT_Level_EvilSilo: {
        static const uint32_t hints[] = { 0x10000ff };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400004d);
        image = HT_Texture_Evil;
        break;
    }
    case HT_Level_EvilBaseC: {
        static const uint32_t hints[] = { 0x1000100, 0x1000101, 0x1000102 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400004f);
        image = HT_Texture_Evil;
        break;
    }

    // The original stores the hint it picks through the objective pointer, not the hint pointer (and only when
    // there is a hint pointer); the objective then overwrites it, so this level never shows a hint of its own -
    // only, half the time, a shared one.
    case HT_Level_SpaceStationD: {
        static const uint32_t hints[] = { 0x1000108, 0x1000109, 0x100010a, 0x100010b, 0x100010c, 0x100010d, 0x100010e,
                                          0x100010f, 0x1000110 };
        if (hintOut != NULL)
            *objectiveOut = LoadText(hints[Rand_Random() % ARRAY_SIZE(hints)]);
        SetObjective(objectiveOut, 0x400005a);
        image = HT_Texture_Space;
        break;
    }

    default:
        if (hintOut != NULL)
            *hintOut = NULL;
        if (objectiveOut != NULL)
            *objectiveOut = NULL;
        return HT_Texture_General;
    }

    PickGeneralHint(hintOut);
    return image;
}

// ---- Loading a level (ResetMap_Load), over several calls of the game flow's state 3

// Empty on the Xbox, where the linker shares one empty function among them (nightfire-port's names)
static void View_Init(void) {}
static void AnimCacheInit(void) {}
static void Sound_SetLogSfxFile(void) {}
static void build_MakeCelOverlapBox(world_tag *world) {}

// The movie that follows a movie (0x072000xx to 0x071000xx), or the front end after one of the 0x073000xx ones;
// otherwise the next level, its variant byte cleared
// FUNC_AT(0x000bdcd0)
HASHCODE ResetMap_LevelAfterMovie(HASHCODE movie) {
    switch ((uint)movie) {
        case HT_Level_HendersonA_Next: return HT_Level_HendersonB_Intro;
        case HT_Level_HendersonB_Next: return HT_Level_HendersonC_Intro;
        case HT_Level_HendersonC_Next: return HT_Level_HendersonD_Intro;
        case HT_Level_CastleExterior_Next: return HT_Level_CastleCourtyard_Intro;
        case HT_Level_CastleCourtyard_Next: return HT_Level_CastleIndoors1_Intro;
        case HT_Level_CastleIndoors1_Next: return HT_Level_CastleIndoors2_Intro;
        case HT_Level_TowerA_Next: return HT_Level_TowerB_Intro;
        case HT_Level_TowerB_Next: return HT_Level_TowerC_Intro;
        case HT_Level_PowerStationA1_Next: return HT_Level_PowerStationA2_Intro;
        case HT_Level_Cut_Level1_Next: return HT_Level_Cut_Level2_Intro;
        case HT_Level_Tower2A_Next: return HT_Level_Tower2B_Intro;
        case HT_Level_Tower2B_Next: return HT_Level_Tower2Elevator_Intro;
        case HT_Level_EvilBase_Next: return HT_Level_EvilSilo_Intro;
        case HT_Level_EvilSilo_Next: return HT_Level_EvilBaseC_Intro;
        case HT_Level_Tower2Elevator_Next: return HT_Level_Tower2C_Intro;
        case HT_Level_HendersonD_Outro:
        case HT_Level_CastleIndoors2_Outro:
        case HT_Level_TowerC_Outro:
        case HT_Level_PowerStationA2_Outro:
        case HT_Level_Cut_Level2_Outro:
        case HT_Level_Cut_Level3_Outro:
        case HT_Level_Tower2C_Outro:
        case HT_Level_EvilBaseC_Outro:
        case HT_Level_SpaceStationD_Outro:
            return HT_Level_Menu_Pre;
    }
    return (HASHCODE)((uint)GameState.NextLevelHashcode & 0xff00ffff);
}

// The step ResetMap_Load is at (RESETMAP_)
// XBE_GLOBAL(0x00279284, 0x4)
uint32_t ResetMap_State;

// The level to load, while the shared files (0x07000500) load first
// XBE_GLOBAL(0x00279280, 0x4)
HASHCODE ResetMap_SavedLevel;
// Set once a level has finished loading (cleared while the loader runs)
// XBE_GLOBAL(0x00279251, 0x1)
uint8_t bLevelLoaded;
// XBE_GLOBAL(0x00279180, 0x1)
uint8_t LoadableIndex;
// XBE_GLOBAL(0x001f6664, 0x4)
cel_tag **gbl_room_cels;
// The map file's (parsemap.cpp)
// XBE_GLOBAL(0x00274c58, 0x4)
void *DynamicPtr;
// XBE_GLOBAL(0x00274c74, 0x4)
uint8_t *FileLastBlock;
// XBE_GLOBAL(0x00274c78, 0x4)
uint8_t *FileDiscard;
// XBE_GLOBAL(0x0025fb11, 0x1)
uint8_t controller_mission_end;  // cleared at each level's start (nightfire-port: mf_menu)
// XBE_GLOBAL(0x001df99c, 0x4)
uint32_t switch_NO_DRONES;
// The loading screen's picture (LoadingGfx), its texture set to the level's
// XBE_GLOBAL(0x00181bb8, 0x2c)
SpriteInfo LoadingGfx = {
    .colour = 0x7f7f7fff,
    .shadowColour = 0x7f7f7fff,
    .depth = 30,
    .onscreenWidth = 640,
    .onscreenHeight = 480,
    .textRef = Action_TranslatedText_NULLVALUE,
    .textureHashcode = HT_Texture_LoadingGfxDefault,
    .linkedViewer = 99,
};
#define LoadScreenFont FontFormat2Centred

#define HT_MainMenu HT_Level_Menu_Pre

enum {
    RESETMAP_START = 0,         // the heap and the shared files; the movie before the level, if any
    RESETMAP_MOVIE = 1,
    RESETMAP_AFTER_MOVIE = 2,   // the level the movie leads to
    RESETMAP_LOAD = 3,          // the level itself
};

// The game's state for a new level: nothing left of the last, the world and every subsystem started again
// AUTOINJECT
void ResetMap_GameInit(void) {
    GameState.CurrentLevelHashcode = (HASHCODE)(GameState.NextLevelHashcode & 0xff00ffff);
    MPSettings.isMultiplayer = GameState.isMultiplayerLevel;
    GameState.ReloadGame = 0;
    GameState.LevelEnding = 0;
    GameState.SomeAlternatePauseState = 0;
    GameState.VibrationEnabled = 1;
    controller_mission_end = 0;
    if (switch_NO_DRONES != 0)
        Drone_bDisableSystem = 1;
    GS_PauseGame(0);
    InitShards();
    Player_ResetStartPos();
    Env_Reset();
    memset(&glb_players, 0, sizeof(glb_players));
    memset(&glb_viewer, 0, sizeof(glb_viewer));
    glb_world = build_alloc_world();
    Txt_LanguageInit();          // the original passes IsWarmReset == 0, which it does not read
    Script_Init();
    control_init_object_lists();
    View_Init();
    Sprite_InitLists();
    Text_Init(IsWarmReset);
    Effect_InitLists();
    Light_Init();
    Drone_LevelReset();
    Door_Init();
    Copter_Init();
    Sensor_Init();
    Coll_ResetHitHeap();
    Bullet_InitCasingLists();
    AnimCacheInit();
    AnimPreLoadInit(0);
    AIPath_Init();
    Creature_Init();
    MP_Init();
    Camera_CreateCameras();
    Drone_PreLoad_Init();
    MenuManager_Init();
    Car_Init();
    SSys_Init();
    gbl_room_cels = NULL;
    if (!IsWarmReset)
        Init_SwitchChannels();
    Sound_SetLogSfxFile();
    if (!(GameState.NextLevelHashcode & 0xff0000))
        Mission_Init(GameState.CurrentLevelHashcode, (char)IsWarmReset);
}

// The loading screen: the level's picture, label 0x3c1 wrapped in lines 15 apart from 408 down, drawn four times
// AUTOINJECT
void ResetMap_GenLoadScreen(void) {
    char *label = (char *)Txt_BindLabel((Action_TranslatedText)0x3c1, 0);
    char *hint, *objective;
    LoadingGfx.textureHashcode = ResetMap_LevelCode2Img((HASHCODE)(GameState.NextLevelHashcode & 0xff00ffff), &hint,
                                                             &objective);
    Sprite_Create2(&LoadingGfx);
    Txt_GetStringFromHeap(0);
    Txt_GetStringFromHeap(0);
    float y = 408.0f;
    if (label != NULL) {
        char *text = label;
        do {
            char *line = Font_WordWrapString(&text, (int)strlen(text), LoadScreenFont, 0x21c);
            if (line == NULL)
                break;
            sprite *spr = Sprite_CreateAndLink2Viewer(0x63);
            Sprite_SetParams(spr, 0, 0x138, (short)Ftol(y), HASHCODE_NONE, TEXT_NORMAL);
            Sprite_SetTextNFmt(spr, line, LoadScreenFont);
            y = y + 15.0f;
        } while (text != NULL);
    }
    LoadScreen_Draw();
    LoadScreen_Draw();
    LoadScreen_Draw();
    LoadScreen_Draw();
    LoadingScreenMakeVisible();
}

// The subtitles of the movie before a level, or 0 for none
static HASHCODE ResetMap_MovieSubtitles(uint movie) {
    switch (movie) {
    case HT_Level_HendersonA_Intro: return HT_Script_Subtitles_IntroMayhewEntry;
    case HT_Level_CastleExterior_Intro: return HT_Script_Subtitles_IntroStolenHardware;
    case HT_Level_TowerA_Intro: return HT_Script_Subtitles_IntroPhoenixTower;
    case HT_Level_PowerStationA1_Intro: return HT_Script_Subtitles_IntroPowerplant;
    case HT_Level_Tower2A_Intro: return HT_Script_Subtitles_IntroPhoenixTower2;
    case HT_Level_EvilBase_Intro: return HT_Script_Subtitles_IntroIslandRadiotower;
    case HT_Level_SpaceStationD_Intro: return HT_Script_Subtitles_IntroSpaceStation;
    case HT_Level_HendersonD_Outro: return HT_Script_Subtitles_OutroMayhewDead;
    case HT_Level_CastleIndoors2_Outro: return HT_Script_Subtitles_OutroHelicopterCrash;
    case HT_Level_TowerC_Outro: return HT_Script_Subtitles_OutroParachuteOffTower;
    case HT_Level_PowerStationA2_Outro: return HT_Script_Subtitles_OutroThroughVentKiko;
    case HT_Level_Tower2C_Outro: return HT_Script_Subtitles_OutroLobbyEscape;
    case HT_Level_EvilBaseC_Outro: return HT_Script_Subtitles_OutroKikoRocketlaunch;
    case HT_Level_SpaceStationD_Outro: return HT_Script_Subtitles_OutroEscapePodEndGame;
    default: return (HASHCODE)0;
    }
}

// Loads the next level, a step a call: the heap, sounds and input started again and the shared files loaded; a movie
// first if the level comes with one (its subtitles, and then the level the movie leads to); then the level, its
// rooms, objects, doors and paths, and the game flow on to state 4.
// AUTOINJECT
void ResetMap_Load(void) {
    switch (ResetMap_State) {
    case RESETMAP_START:
        psiResetResources();
        MenuManager_Load(0, 0);
        Sound_Kill();
        Mem_Init();
        hashtable_initialise();
        Sound_Init();
        Input_Ready();
        if (GameState.NextLevelHashcode != HT_MainMenu) {
            ResetMap_GameInit();
            ResetMap_SavedLevel = GameState.NextLevelHashcode;
            GameState.NextLevelHashcode = HT_Level_SharedFiles;
            bLevelLoaded = 0;
            while (LoaderLoad(1, GameState.NextLevelHashcode, 0, 0))
                ;
            GameState.NextLevelHashcode = ResetMap_SavedLevel;
        }
        LoadWoman_Thunk();           // the Xbox layer's (0x000dc860)
        if (!(GameState.NextLevelHashcode & 0xff0000)) {
            ResetMap_State = RESETMAP_LOAD;
            return;
        }
        if (SubtitlesEnabled) {
            HASHCODE subtitles = ResetMap_MovieSubtitles(GameState.NextLevelHashcode);
            if (subtitles != 0 && hashtable_getitem(subtitles) != NULL)
                SP_Create(&CONST_ZERO_VECTOR, &CONST_ZERO_VECTOR, subtitles, (HASHCODE)0, SP_PLAY_NOW, 1, NULL, NULL,
                          NULL, 0);
        }
        GameFlow_PushState(GAMEFLOW_MOVIE, 0.0f, 0xff);
        GameState.MovieHashcode = GameState.NextLevelHashcode;
        GameState.NextLevelHashcode = (HASHCODE)(GameState.NextLevelHashcode & 0xff00ffff);
        GameState.field5_0x14 = 0;
        if (GameState.NextLevelHashcode == HT_Level_SpaceStationA)
            GameState.NextLevelHashcode = HT_Level_SpaceStationD;
        Mem_SetMallocMethod(MALLOC_NEXT_FIT);
        psiStartBackgroundMovie((HASHCODE)GameState.MovieHashcode, 0, SFXGetVolume());
        ResetMap_State = RESETMAP_MOVIE;
        return;

    case RESETMAP_MOVIE:
        ResetMap_State = RESETMAP_AFTER_MOVIE;
        return;

    case RESETMAP_AFTER_MOVIE:
        GameState.NextLevelHashcode = ResetMap_LevelAfterMovie((HASHCODE)GameState.MovieHashcode);
        GameState.CurrentLevelHashcode = GameState.NextLevelHashcode;
        if (GameState.NextLevelHashcode == HT_MainMenu)
            GameState.ReloadMenupage = P_NFRESULTS;
        ResetMap_State = (GameState.NextLevelHashcode & 0xff0000) ? RESETMAP_START : RESETMAP_LOAD;
        return;

    case RESETMAP_LOAD:
        if (GameState.NextLevelHashcode != HT_MainMenu)
            ResetMap_GenLoadScreen();
        LoadableIndex = 0;
        psiInitTimeIn100ths();
        psiResetResources();
        MenuManager_Load(0, 0);
        Sound_Kill();
        Mem_Init();
        hashtable_initialise();
        Sound_Init();
        Input_Ready();
        ResetMap_GameInit();
        SFXLoadCorrectBank();
        psiLight_Create();           // the original passes 1, which it does not read
        bLevelLoaded = 0;
#if NIGHTFIRE_DATA_PS2
        Ps2_LoadLevelSequences(GameState.NextLevelHashcode);   // the PS2's LoaderLoad(0, level) (docs/ps2-data.md)
#endif
        while (LoaderLoad(1, GameState.NextLevelHashcode, 0, 0))
            ;
        ResetMap_State = 4;
        build_MakeCelOverlapBox(glb_world);
        build_link_objects_to_rooms(glb_world->firstCel);
        parsemap_reset_to_block_memory_discard();
        parsemap_block_map_data_dynamic(&DynamicBH, (uchar *)DynamicPtr, 1);
        build_LinkDoors2Portals(glb_world->firstCel, control_first_object());
        AIPath_BindNodes();
        Mem_Shrink((void **)&FileDiscard, (uint)(FileLastBlock - FileDiscard));
        LoadingScreenMakeInvisible();
        bLevelLoaded = 1;
        ReadTuningVars();
        Player_Start();
        MP_Start();
        Drone_PostLoad_Init();
        MP_PostLoad_Init();
        Cameras_PostLoad();
        LoadWoman_Thunk();
        if (GameState.CurrentLevelHashcode == HT_MainMenu)
            MenuManager_Create(GameState.ReloadMainMenu, (HASHCODE)GameState.ReloadMenupage, -1, 0,
                               MENU_NO_CLOSE_ACTION, 0, MENU_STATUS_FRONT_END);
        Mem_SetMallocMethod(MALLOC_NEXT_FIT);
        hashtable_create_sections();
        GameFlow_PopState();
        GameFlow_PushState(GAMEFLOW_INIT_LOCKS, 0.0f, 0xff);
        ResetMap_State = RESETMAP_START;
        return;
    }
}

// The state on top of the game flow's stack; 0 when it is empty
// AUTOINJECT
uint GameFlow_GetState(void) {
  if (StackIndex == 0) {
    return 0;
  }
  return GameStateStack[StackIndex - 1];
}

// The game is not drawn in some states (loading, the movies)
// AUTOINJECT
void set_InhibitGameDrawIfRequired(void) {
  switch(GameFlow_GetState()) {
    case GAMEFLOW_BOOT:
    case GAMEFLOW_LOAD_LEVEL:
    case GAMEFLOW_INIT_LOCKS:
    case GAMEFLOW_IDLE:
    case GAMEFLOW_DRIVING:
      GameState.InhibitGameDraw = 1;
      break;
    case GAMEFLOW_GAME:
    case GAMEFLOW_MOVIE:
    case GAMEFLOW_FADE:
    case GAMEFLOW_FADING:
    case GAMEFLOW_10:
    case GAMEFLOW_DELAYING:
    case GAMEFLOW_FADE_FROZEN:
    case GAMEFLOW_FADING_FROZEN:
    GameState.InhibitGameDraw = 0;
      break;
    }
}

// Takes the top state off the stack and answers it (0 when the stack is empty)
// AUTOINJECT
uint GameFlow_PopState(void)
{
  if (StackIndex == 0) {
    return 0;
  }
  StackIndex--;
  set_InhibitGameDrawIfRequired();
  return GameStateStack[StackIndex];
}

// Puts a state on the stack, which wraps round at 64. GAMEFLOW_FADE and GAMEFLOW_FADE_FROZEN fade the screen first
// (over 'time' frames, to 'colour') and are pushed as their fading states once the fade has started, or not at all
// if it cannot; GAMEFLOW_DELAY sets the fade viewer's rate.
// AUTOINJECT
void GameFlow_PushState(int state, float time, uint colour) {
  switch (state) {
    case GAMEFLOW_FADE:
      if (!Camera_SetFade(VIEWER_FADE, time, colour, 1))
        return;
      state = GAMEFLOW_FADING;
      GameState.InhibitGameDraw = 0;
      break;
    case GAMEFLOW_DELAY:
      glb_viewer[VIEWER_FADE]->fadeRate = time;
      state = GAMEFLOW_DELAYING;
      GameState.InhibitGameDraw = 0;
      break;
    case GAMEFLOW_FADE_FROZEN:
      if (!Camera_SetFade(VIEWER_FADE, time, colour, 1))
        return;
      state = GAMEFLOW_FADING_FROZEN;
      GameState.InhibitGameDraw = 0;
      break;
  }
  // GC check (0x80051420): the stack wraps round (kept)
  NF_WARN_IF(StackIndex + 1 > 0x3f, "**** STACK OVERFLOW ****\n");
  GameStateStack[StackIndex] = state;
  StackIndex++;
  if (StackIndex >= ARRAY_SIZE(GameStateStack)) {
    StackIndex = 0;
  }
  set_InhibitGameDrawIfRequired();
}

void GameFlow_QuickPushState(uint state) {
    GameStateStack[StackIndex++] = state;
    if (StackIndex >= ARRAY_SIZE(GameStateStack)) {
      StackIndex = 0;
    }
    set_InhibitGameDrawIfRequired();
}

// ---------------------------------------------------------------------------------------------------------------
// Eurocom's frame-timing helper. The original is four instructions and they are worth reading, because the
// reason this is reimplemented is in them:
//
//     rdtsc                        ; the CPU's cycle counter
//     imul  3 / idiv 2200          ; 2200/3 = 733.333 - the Xbox's 733 MHz Pentium III, baked in
//     fmul  0.001                  ; microseconds to milliseconds
//
// The divisor is the console's own clock speed. Run that on a host CPU and the instruction returns the host's
// counter, so the clock comes out fast by exactly the ratio of the two clock speeds - measured at 6.41x on a
// 4.7 GHz part, and a different number on every machine it runs on. Everything timed by it ran that much fast:
// psiGetTimeIn100ths, psiSFXGetTimer, the streaming timeouts in FS_AllocateAndLoadBlocking, and the pulsing
// glow on pickups and usable doors, which is what led here.
//
// CXBX never had the problem because it rewrites every rdtsc in the image and emulates it at the Xbox's rate
// (Cxbx-Reloaded/src/core/kernel/support/PatchRdtsc.cpp). The loader executes the instruction
// natively, so the arithmetic has to be corrected here instead.
//
// The host's QueryPerformanceCounter rather than rdtsc scaled by a measured CPU frequency: it is already the fixed-rate
// monotonic counter that measuring rdtsc would be trying to approximate, and it does not drift when the CPU
// changes speed. Every caller takes a difference between two of these, so the epoch is arbitrary and only the
// rate matters - which is just as well, since the original counted from power-on and this counts from the
// first call.
//
// Two rdtsc sites remain in the image and are deliberately not touched here. QueryPerformanceCounter at
// 0x000f53d4 is the XBE's own, and is wrong in the same way for anything that calls it. FUN_0010afe0 is a bare
// rdtsc used by the XBE's NV2A driver to timestamp vblank interrupts and predict the next one; that layer
// talks to real graphics hardware registers and does not run at all behind the D3D9 backend.
//
// AUTOINJECT
double timestamp(void) {
  if (Demo_Active())
    return Demo_TimestampMs();

  static int64_t frequency = 0;
  static int64_t origin = 0;

  if (frequency == 0) {
    int64_t f, o;
    if (!HostQueryPerformanceFrequency(&f) || f == 0) {
      f = 10000000; // the usual value; being wrong beats dividing by zero
    }
    HostQueryPerformanceCounter(&o);
    origin = o;
    frequency = f; // published last, so nothing can see a frequency without an origin to go with it
  }

  int64_t now;
  HostQueryPerformanceCounter(&now);
  long long ticks = now - origin;

  // Split rather than (ticks * 1000000) / frequency, which overflows a 64-bit multiply after a few days of
  // uptime. Microseconds first, then scaled, to keep the original's quantisation - it truncated to whole
  // microseconds before converting to milliseconds and callers may compare small differences.
  long long micros = (ticks / frequency) * 1000000 + ((ticks % frequency) * 1000000) / frequency;
  return (double)micros * 0.001;
}

// The same fault one layer down, in the XAPI the game links against. These two are a matched pair and the
// original is self-consistent only on a real console:
//
//     QueryPerformanceCounter   (000f53d0)  returns the raw cycle counter
//     QueryPerformanceFrequency (000f53e1)  returns the literal 0x2bb5c755 = 733,333,333
//
// i.e. the counter is whatever the CPU is running at, and the frequency is a hard-coded claim that it is a
// 733 MHz Xbox. Anything dividing one by the other therefore gets time that passes too fast by the ratio of
// the real clock to that constant - the same 6.41x that made pickups pulse wrongly, arrived at by a different
// route. The XMV video decoder is the caller that matters: XmvDecoderCreate asks for the frequency once,
// stores frequency/1000 as ticks-per-millisecond, and XmvDecoderUpdate divides counter deltas by it to
// decide when each frame of a background movie is due.
//
// Replacing both with the host's own pair keeps them consistent with each other, which is the only property
// the callers actually depend on - none of them assumes any particular frequency, they all ask. Patched by
// address rather than by name because the names collide with the Win32 functions being called here.
//
// Both are __stdcall taking one pointer and returning 1 in EAX, confirmed from the image: each ends RET 4.

// FUNC_AT(000f53d0)
uint32_t __stdcall Xbox_QueryPerformanceCounter(LARGE_INTEGER *counter) {
  if (Demo_Active()) {
    counter->QuadPart = Demo_Counter();
    return 1;
  }
  int64_t now;
  HostQueryPerformanceCounter(&now);
  counter->QuadPart = now;
  return 1;
}

// FUNC_AT(000f53e1)
uint32_t __stdcall Xbox_QueryPerformanceFrequency(LARGE_INTEGER *frequency) {
  if (Demo_Active()) {
    frequency->QuadPart = Demo_CounterFrequency();
    return 1;
  }
  int64_t rate;
  frequency->QuadPart = HostQueryPerformanceFrequency(&rate) ? rate : 10000000;   // as in timestamp() above
  return 1;
}

// Used to insert a CALL location within a larger function that a debugger or profiler can hook into
void __profiling_or_debugging_hook_point(void) {
  return;
}

// Only used in these two functions, so no need to use the original location
// XBE_GLOBAL(0x002adf48, 0x8)
double INITIALISATION_TIME;

// AUTOINJECT
void psiInitTimeIn100ths(void) {
  INITIALISATION_TIME = timestamp();
}

// AUTOINJECT
ulonglong psiGetTimeIn100ths(void) {
  return (ulonglong)Ftol64((Extended(timestamp()) - Extended(INITIALISATION_TIME)) * Extended(0.1));
}

// Only used in GameFlow_Main, so technically no need to inject, but helps us track project completion
// AUTOINJECT
void bootup_bootup(void) {

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

  GameState.difficultyModifier = DIFF_AGENT;
  MPSettings.RespawnSelectionMode = RESPAWN_RANDOM;
  GameState.ReloadMainMenu = HT_Menu_ControllerMenu;
  GameState.ReloadMenupage = P_LANGUAGE;
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

  for(int i = 0; i < NUM_AGENTS; i++) {

    MPSettings.Player[i].TeamId = (i & 1) ? MI6 : PHOENIX;
    MPSettings.Player[i].SkinNum = 0;
    MPSettings.Player[i].radar = 1;
    MPSettings.Player[i].HealthModifier = 0;

    if(i < NUM_PLAYERS) {
      sprintf(MPSettings.Player[i].Name, "%s %d", Txt_BindLabel(PLAYER, 0), i + 1);
    } else {
      sprintf(MPSettings.Player[i].Name, "%s %d", "Bot", i - NUM_PLAYERS + 1);
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

  CONST_X_VECTOR.x = 1.0;
  CONST_X_VECTOR.y = 0.0;
  CONST_X_VECTOR.z = 0.0;

  Mat_IdentityT(&MAT_IDENTITY);

  Input_Init();
  PlrStat_Init();
  __profiling_or_debugging_hook_point();
  Input_Ready();
  SFXSetMode(1);
  NewScoresRef = &(PTPDATA.Scoring);
}

// ---- The page handed between the engines (PTPDATA): what the action engine passes to the driving engine and
// ---- takes back from it

// XBE_GLOBAL(0x0025d79e, 0x1)
char menu_unlock_everything;  // Menu.cpp
// XBE_GLOBAL(0x0025d7a0, 0x4)
int LanguageToLoad;  // XboxSystem.cpp
// XBE_GLOBAL(0x001f6610, 0x4)
uint32_t IsWidescreen;

#define PTP_VERSION 1
#define PTP_LOADED_FROM_DVD 2
#define PTP_CHEAT_PARIS_PLATINUM 8  // fCheat: Paris has its platinum medal

// The driving engine's level states (LevelStatus) a return reports
#define PTP_LEVEL_COMPLETED 0
#define PTP_LEVEL_QUIT 4

// The page for the driving level about to be played: the codename and its settings, the level, the medals
// AUTOINJECT
void Boot_GetPTPData(void **data, uint *size) {
    Mission_SetMapHCode(GameState.NextLevelHashcode);
    PTP_Eurocom *ec = &PTPDATA.ECDataBuf;
    int difficulty;
    // The mini mission after Paris is played at difficulty 1, and not as a codename's
    if (Mission_BaseMapHCode() == HT_Level_Driving_Paris && ls.doMiniMission) {
        difficulty = 1;
        PTPDATA.fLoadedFromMemCard = 0;
    } else {
        difficulty = GameState.difficultyModifier;
        PTPDATA.fLoadedFromMemCard = 1;
    }
    PTPDATA.fSkillLevel = difficulty;
    ec->difficulty = difficulty;
    ec->canSave = ls.canSave;
    ec->crouchToggle = PlayerInputs[0].crouchToggle;
    memcpy(ec->codename, ls.codename, sizeof(ec->codename));
    ec->codenameSlot = ls.slot;
    ec->manualAimToggle = PlayerInputs[0].manualAimToggle;
    ec->autoAim = PlayerInputs[0].autoAim;
    ec->autoSwitchBetterWeapon = PlayerInputs[0].autoSwitchBetterWeapon;
    ec->input05 = PlayerInputs[0].field5_0x5;
    ec->levelViewOnPace = PlayerInputs[0].levelViewOnPace;
    ec->rememberZoom = PlayerInputs[0].rememberZoom;
    ec->flashingObjects = PlayerInputs[0].flashingObjects;
    ec->sfxLanguage = GetSFXLanguage();
    ec->controlStyle = PlayerInputs[0].controlStyle;
    ec->unlockEverything = menu_unlock_everything;
    ec->Cheat_Immortal = CheatInfo.Immortal;
    ec->Cheat_AllWeapons = CheatInfo.AllWeapons;
    ec->Cheat_UnlimitedAmmo = CheatInfo.UnlimitedAmmo;
    ec->mpRadar = MPSettings.Player[0].radar;
    ec->healthModifier = MPSettings.Player[0].HealthModifier;
    ec->autoAimMultiplayer = PlayerInputs[0].autoAimMultiplayer;
    ec->splitScreenLayout = MultiplayerLayout_LeftRightOrTopBtm;
    ec->NightfireStatus = Menu_GetNightfireStatus();
    uint64_t bonus = Menu_GetBonus(0);
    ec->bonusHi = (uint)(bonus >> 32);
    ec->bonusLo = (uint)bonus;
    ec->MP_MaxPoints = MPSettings.MaxPoints;
    ec->weaponUpgrade = GameState.WeaponUpgradeRelated;
    ec->MP_MaxDuration = MPSettings.MaxDuration;
    ec->MP_ProfessionalModeDamage = MPSettings.TripleDamageModifierProfessionalMode;
    ec->MP_FriendlyFire = MPSettings.FriendlyFire;
    ec->MP_WeaponSet = MPSettings.weaponSet;
    ec->MP_RespawnMode = MPSettings.RespawnSelectionMode;
    ec->mainMenuSound = ls.relatedToMainMenuSound;
    ec->MP_LocationDamage = MPSettings.LocationDamageEnabled;
    ec->MP_ShowTeamAndName = MPSettings.ShowTeamAndNameOverhead;
    ec->MP_GunEmplacements = MPSettings.GunEmplacementsEnabled;
    ec->MP_ExplosiveScenery = MPSettings.ExplosiveSceneryEnabled;
    memcpy(ec->defaultCodename, &def_codename[1], sizeof(ec->defaultCodename));
    PlrStats_GetDrivingData(ec->bestScores, ec->bondMoments);
    ec->level = GameState.NextLevelHashcode;
    PTPDATA.LevelToLoad = GameState.NextLevelHashcode - HT_Level_Driving_HASHCODE_BASE;   // its number among the driving levels
    PTPDATA.fLanguage = GetLanguage();
    PTPDATA.Version = PTP_VERSION;
    PTPDATA.TvType = GetTvType();
    PTPDATA.fLoadedFromCDOrDVD = PTP_LOADED_FROM_DVD;
    PTPDATA.fSoundMode = SFXGetMode();
    PTPDATA.fControllerInvert = PlayerInputs[0].inverted;
    PTPDATA.fSubTitles = SubtitlesEnabled;
    PTPDATA.SoundVol = SFXGetVolume();
    ec->sfxVolume = SFXGetVolume();
    PTPDATA.MusicVol = SFXMusicGetVolume();
    ec->musicVolume = SFXMusicGetVolume();
    PTPDATA.fControllerConfig = PlayerInputs[0].controlStyle;
    PTPDATA.fControllerPort = PlayerInputs[0].controllerPort;
    PTPDATA.fControllerConfigDriving = PlayerInputs[0].controlStyleDriving;
    PTPDATA.fVibration = PlayerInputs[0].vibrationEnabled;
    PTPDATA.fCrossHairOff = PlayerInputs[0].crosshairsEnabled == 0;
    PTPDATA.fAutoAim = PlayerInputs[0].autoAim;
    PTPDATA.fManualAimToggle = PlayerInputs[0].manualAimToggle;
    PTPDATA.fHudAlwaysVisible = PlayerInputs[0].hudVisibility == 0;
    PTPDATA.fAutoSwitchWeapons = PlayerInputs[0].autoSwitchBetterWeapon;
    PTPDATA.WideScreen = IsWidescreen == 1;
    PTPDATA.fAntialiasMode = 0;
    PTPDATA.fMedalAwarded = Menu_GetBestMedal(GameState.NextLevelHashcode, 0);
    // Medal 3 is MEDAL_GOLD by P_NFRESULTS's medals; the bit is named for platinum
    if (Menu_HasMedal(HT_Level_Driving_Paris, 3, 0))
        PTPDATA.fCheat |= PTP_CHEAT_PARIS_PLATINUM;
    else
        PTPDATA.fCheat &= ~PTP_CHEAT_PARIS_PLATINUM;
    *data = &PTPDATA;
    *size = sizeof(PTPDATA);
}

// Back from the driving engine: its page, if it has one of this version and the codename's save loads from it,
// gives the mission's state and the codename's settings back, and the front end opens on the results, the map
// (the level failed or quit) or the title sequence (from the mini mission after Paris)
// AUTOINJECT
void __stdcall Boot_LoadPTPData(void) {
    if (!psiGetDrivingData((uint32_t *)&PTPDataIn))
        return;
    PTPDATA = PTPDataIn;
    if (PTPDataIn.Version != PTP_VERSION || LS_LoadFromBuffer((uchar *)&PTPDATA, 0x12c0) == 0)
        return;
    PTP_Eurocom *ec = &PTPDATA.ECDataBuf;
    Mission_SetMapHCode(ec->level);
    if (PTPDATA.LevelStatus == PTP_LEVEL_COMPLETED) {
        GameState.BaseMapHashCode = Mission_BaseMapHCode();
        Mission_SetStatus(MISSION_STATUS_COMPLETE);
    } else if (PTPDATA.LevelStatus == PTP_LEVEL_QUIT) {
        Mission_SetStatus(MISSION_STATUS_QUIT);
    } else {
        Mission_SetStatus(MISSION_STATUS_FAILED);
    }
    GameState.difficultyModifier = ec->difficulty;
    PTPDATA.fSkillLevel = ec->difficulty;
    PlrStat_GetScore(0);
    memcpy(ls.codename, ec->codename, sizeof(ls.codename));
    ls.canSave = (uchar)ec->canSave;
    ls.slot = ec->codenameSlot;
    PlayerInputs[0].autoAim = (uchar)ec->autoAim;
    PlayerInputs[0].crouchToggle = (uchar)ec->crouchToggle;
    PlayerInputs[0].manualAimToggle = (uchar)ec->manualAimToggle;
    PlayerInputs[0].levelViewOnPace = (uchar)ec->levelViewOnPace;
    PlayerInputs[0].autoSwitchBetterWeapon = (uchar)ec->autoSwitchBetterWeapon;
    PlayerInputs[0].field5_0x5 = (uchar)ec->input05;
    PlayerInputs[0].rememberZoom = (uchar)ec->rememberZoom;
    PlayerInputs[0].flashingObjects = (uchar)ec->flashingObjects;
    SFXSetLanguage(ec->sfxLanguage);
    CheatInfo.UnlimitedAmmo = ec->Cheat_UnlimitedAmmo;
    menu_unlock_everything = (char)ec->unlockEverything;
    CheatInfo.Immortal = ec->Cheat_Immortal;
    MPSettings.Player[0].HealthModifier = ec->healthModifier;
    CheatInfo.AllWeapons = ec->Cheat_AllWeapons;
    MPSettings.Player[0].radar = ec->mpRadar;
    PlayerInputs[0].autoAimMultiplayer = (uchar)ec->autoAimMultiplayer;
    MultiplayerLayout_LeftRightOrTopBtm = ec->splitScreenLayout;
    Menu_SetNightfireStatus(ec->NightfireStatus);
    Menu_SetBonus(ec->bonusLo, ec->bonusHi, 0, 0);
    GameState.WeaponUpgradeRelated = (uchar)ec->weaponUpgrade;
    MPSettings.MaxDuration = ec->MP_MaxDuration;
    MPSettings.MaxPoints = ec->MP_MaxPoints;
    MPSettings.FriendlyFire = ec->MP_FriendlyFire;
    MPSettings.weaponSet = (WeaponSet)ec->MP_WeaponSet;
    MPSettings.TripleDamageModifierProfessionalMode = ec->MP_ProfessionalModeDamage;
    MPSettings.LocationDamageEnabled = ec->MP_LocationDamage;
    MPSettings.ShowTeamAndNameOverhead = ec->MP_ShowTeamAndName;
    MPSettings.ExplosiveSceneryEnabled = ec->MP_ExplosiveScenery;
    MPSettings.RespawnSelectionMode = (RespawnMode)ec->MP_RespawnMode;
    MPSettings.GunEmplacementsEnabled = ec->MP_GunEmplacements;
    ls.relatedToMainMenuSound = (uchar)ec->mainMenuSound;
    memcpy(&def_codename[1], ec->defaultCodename, sizeof(ec->defaultCodename));
    PlrStats_SetDrivingData(ec->bestScores, ec->bondMoments);
    Txt_SetLanguage((tLANGUAGE)PTPDATA.fLanguage);
    LanguageToLoad = PTPDATA.fLanguage;
    PTPDATA.Version = PTP_VERSION;
    SFXSetMode(PTPDATA.fSoundMode);
    PlayerInputs[0].inverted = (uchar)PTPDATA.fControllerInvert;
    SubtitlesEnabled = PTPDATA.fSubTitles;
    SFXSetVolume(ec->sfxVolume);
    SFXMusicSetVolume(ec->musicVolume);
    PlayerInputs[0].controlStyle = (short)PTPDATA.fControllerConfig;
    PlayerInputs[0].controllerPort = (uchar)PTPDATA.fControllerPort;
    PlayerInputs[0].controlStyleDriving = (short)PTPDATA.fControllerConfigDriving;
    PlayerInputs[0].vibrationEnabled = (uchar)PTPDATA.fVibration;
    PlayerInputs[0].crosshairsEnabled = PTPDATA.fCrossHairOff == 0;
    PlayerInputs[0].autoAim = (uchar)PTPDATA.fAutoAim;
    PlayerInputs[0].manualAimToggle = (uchar)PTPDATA.fManualAimToggle;
    PlayerInputs[0].autoSwitchBetterWeapon = (uchar)PTPDATA.fAutoSwitchWeapons;
    PlayerInputs[0].hudVisibility = PTPDATA.fHudAlwaysVisible == 0;
    IsWidescreen = PTPDATA.WideScreen != 0;
    if (Mission_BaseMapHCode() == HT_Level_Driving_Paris && PTPDATA.fLoadedFromMemCard == 0) {
        GameState.ReloadMenupage = P_ESTHERO;
        ResetMap_LevelToLoad(HT_Level_Menu_Pre, false, false);
        return;
    }
    switch (PTPDATA.LevelStatus) {
    case PTP_LEVEL_COMPLETED:
        GameState.ReloadMenupage = P_NFRESULTS;
        break;
    case 1:
    case 2:
    case PTP_LEVEL_QUIT:
        GameState.ReloadMenupage = P_NFMAP;
        break;
    }
    ResetMap_LevelToLoad(HT_Level_Menu_Pre, false, false);
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
  GameState.NumFrames++;

  if ((sloflag == 0) && !GS_IsPaused(-1)) {
    GameState.NumFramesUnpaused = GameState.NumFramesUnpaused + 1;
    GameState.VideoFrames += VIDEO_FRAME_RATE / FRAME_RATE_INT;
  }

  // Nothing reads Pulse
  byte pulse = (byte)GameState.NumFrames & 0x3f;
  if (pulse > 0x1f) {
    pulse = 0x3f - pulse;
  }
  GameState.Pulse = pulse << 1;

  switch(GameFlow_GetState()) {
  case GAMEFLOW_BOOT:
    GameState.InhibitGameDraw = 0;
    GameState.NumFramesUnpaused = 1;
    GameState.NumFrames = 1;
    GameState.VideoFrames = 1;
    GameState.NumDraws = 0;
    Mem_Init();
    bootup_bootup();
    GameFlow_QuickPushState(GAMEFLOW_GAME);
    Boot_LoadPTPData();
    LaunchOptions_Apply();   // -level, -difficulty
    Reset_MapLoadSettings();
    return;
  case GAMEFLOW_GAME:
    GameState.LoadTimeStart = psiGetTimeIn100ths();
    if (GameState.ReloadGame != 0) {
      GameFlow_QuickPushState(GAMEFLOW_LOAD_LEVEL);
      return;
    }
    break;
  case GAMEFLOW_LOAD_LEVEL:
    ResetMap_Load();
    return;
  case GAMEFLOW_INIT_LOCKS:
    Locks_Init();
    GameFlow_PopState();
    break;
  case GAMEFLOW_MOVIE:
    if (movieFinished() || Input_Action(-1, ACTION_SKIP_CUTSCENE, ACTION_HELD)) {
      psiStopBackgroundMovie();
      GameFlow_PopState();
    }
    break;
  case GAMEFLOW_FADING:
    if (glb_viewer[VIEWER_FADE] != NULL && glb_viewer[VIEWER_FADE]->fadeState != VIEWFADE_FADING) {
      GameFlow_PopState();
    }
    break;
  case GAMEFLOW_DRIVING: {
    void *data = NULL;
    uint size = 0;
    Boot_GetPTPData(&data, &size);
    psiLaunchDriving(data, size);
    GameFlow_PopState();
    break;
  }
  case GAMEFLOW_10:
    Input_UpdateWithMouse();
    if (CheatInfo.someThing != 0) {
      GameFlow_PopState();
    }
    Game_Draw();
    return;
  case GAMEFLOW_DELAYING:
    if (glb_viewer[VIEWER_FADE] != NULL) {
      if (!(glb_viewer[VIEWER_FADE]->fadeRate <= 0.0f)) {
        glb_viewer[VIEWER_FADE]->fadeRate = glb_viewer[VIEWER_FADE]->fadeRate - 1.0f;
      }
      else {
        GameFlow_PopState();
      }
    }
    break;
  case GAMEFLOW_FADING_FROZEN:
    if (glb_viewer[VIEWER_FADE] == NULL || glb_viewer[VIEWER_FADE]->fadeState == VIEWFADE_FADING) {
      Camera_UpdateAll();
      SkipCodeFrame = '\x01';
      break;
    }
    GameFlow_PopState();
    SkipCodeFrame = '\0';
    break;
  default:
    // GC check (0x800515e8)
    NF_WARN_IF(GameFlow_GetState() != GAMEFLOW_IDLE, "Game state not valid %d\n", GameFlow_GetState());
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

// No need for autoinjection, only called once from the function immediately below. Do it anyway to track project completion
// AUTOINJECT
void GS_SetRefreshRate(int gameFrameRate, int videoFrameRate) {

  VIDEO_FRAME_RATE = videoFrameRate;

  _FRAME_RATE = (float)(uint)gameFrameRate;
  FRAME_RATE_INT = gameFrameRate;
  FRAME_RATE_DIV = _FRAME_RATE * 0.016666668f;
  // The reciprocal stays on the x87 stack for the multiply: at 50 Hz FRAME_RATE_MUL is 1.2, not the float
  // reciprocal's 1.1999999
  FRAME_RATE_MUL = (float)(Extended(1.0) / Extended(_FRAME_RATE) * Extended(60.0));
  REC_FRAME_RATE = (float)(Extended(1.0) / Extended(_FRAME_RATE));

}

// AUTOINJECT
bool Graphics_IsPalI(void) {
  return Gfx.is60Hz;
}

// The other region and video-mode flags xboxInitGraphics works out; each original is one MOV AL and a RET.

// AUTOINJECT
bool IsNotPalI(void) {
  return Gfx.isNotPalI;
}

// AUTOINJECT
bool Graphics_IsSomeGraphicsRegion(void) {
  return Gfx.isNtscM;
}

// The television's standard for the driving engine: 0 NTSC-M, 1 not PAL-I, 2 PAL-I
// AUTOINJECT
int GetTvType(void) {
  if (Graphics_IsSomeGraphicsRegion())
    return 0;
  return IsNotPalI() ? 1 : 2;
}

// AUTOINJECT
bool Graphics_IsWidescreen(void) {
  return Gfx.isWidescreen;
}

// AUTOINJECT
bool Graphics_IsSomeRegionBasedThing(void) {
  return Gfx.videoModeBit3;
}

// AUTOINJECT
void mainloop(void) {
  // Graphics_IsPalI reads Gfx.is60Hz: true outside the PAL-I region
  int refreshRate = Graphics_IsPalI() ? 60 : 50;

  int fpsOverride = Settings_GetFPSOverride();
  if (fpsOverride > 0)
    refreshRate = fpsOverride;

  GS_SetRefreshRate(refreshRate, refreshRate);
  Demo_FrameStart();   // devtools: time demos
  GameFlow_Main();
  Demo_FrameEnd();
}

// Keeps the background movie going, called every frame: decodes and draws the next frame, masks the top and
// bottom 66 lines to black for 0x073a0048 (letterboxed), and when the movie ends either replays it (looping,
// through psiStartBackgroundMovie's 'NPLY' "same file again") or stops it.
// FUNC_AT(0x000dcda0)
void psiUpdateBackgroundMovie(void) {
    if (BackgroundMovieHashcode == 0)
        return;
    BackgroundMovieUpdate();
    if (BackgroundMovieHashcode == HT_Level_Menu_AttractMovie) {
        resetRenderState(1);
        immediateModePushItem(0.0f, 0.0f, 640.0f, 66.0f, 0, 0, 0.0f, 0.0f, 0xff000000);
        immediateModePushItem(0.0f, 414.0f, 640.0f, 66.0f, 0, 0, 0.0f, 0.0f, 0xff000000);
        immediateModeFlush();
    }
    if (BackgroundMovieIsPlaying())
        return;
    if (LoopingMovie != 0) {
        psiStartBackgroundMovie((HASHCODE)0x4e504c59, LoopingMovie, BackgroundMovieVolume);
        return;
    }
    BackgroundMovieCleanup();
    BackgroundMovieHashcode = 0;
}
