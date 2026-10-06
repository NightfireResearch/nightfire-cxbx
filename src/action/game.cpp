#include "actionhelpers.h"
#include "input.h"
#include "memory.h"
#include "game.h"
#include "game/mp/multiplayer.h" // for MPSettings
#include "ui/MenuManager.h"
#include "engine/Text.h"
#include "util/Random.h"
#include "engine/XboxSettings.h"
#include "engine/Direct3D/GraphicsSystem.h" // Gfx
#include "engine/Fmv.h"
#include "engine/Direct3D/d3dSeam.h" // the immediate-mode quads maybeStartBackgroundMovie draws

#include <windows.h>

#include <cstring>
#include <cstdio>

#include <stdio.h>
#include "engine/XboxSystem.h"
#include "devtools/Teleport.h"

// XBE_GLOBAL(0x002ae288, 0x4)
uint32_t BackgroundMovieHashcode;

// Functions taking void and returning through registers are fine in either __cdecl or __stdcall
// It's only when they take arguments that the calling convention matters

// AUTOGEN
void __stdcall UpdateAllShards(void);
// AUTOGEN
void __stdcall Env_Update(void);
// AUTOGEN
void __stdcall SSys_Monitor(void);
// AUTOGEN
void __stdcall Light_Update(void);



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
#define switch_allowFreeze U32_AT(0x0025d79c)


void psiPreGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2
void psiPostGame_Run(void) {} // No effect on XBox, does some PS2-specific stuff on PS2



// Process the gameplay / update the state of the world and UI
// AUTOINJECT
void Game_Run(void) {

  psiPreGame_Run();
  Input_Update();
  ActionTeleport_Tick();   // devtools: F8/F9 teleport, F7 frame dump


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

#define StackIndex U16_AT(0x0017bfe8)
#define glb_viewer_6 ((uint)glb_viewer[6])
// XBE_GLOBAL(0x001f6564, 0x1)
static uint8_t SkipCodeFrame;
#define GameStateStack (*(uint (*)[64])0x0017bff0) // Not zero-initialised - first entry must be 1

// XBE_GLOBAL(0x001f65dc, 0x30)
#define CheatInfo (*((CheatInfo_t*)0x001f65dc))
// XBE_GLOBAL(0x001f6568, 0x18)
#define GlobalVars (*((GlobalVars_t*)0x001f6568))
#define PTPDATA (*((sNightFireShared_tag*)0x001d7e90))


// XBE_GLOBAL(0x001f6618, 0x4)
uint32_t HintsEnabled;
#define SubtitlesEnabled U32_AT(0x001f6614)

// XBE_GLOBAL(0x001f65d8, 0x4)
uint32_t SoundInfo;

// Maybe hashcode of playing FMV


// AUTOGEN
void __stdcall Game_Draw(void);
// AUTOGEN
void __stdcall Boot_LoadPTPData(void);
// AUTOGEN
void __stdcall ResetMap_Load(void);
// AUTOGEN
void Boot_GetPTPData(void **param_1,uint *param_2);

// AUTOINJECT
void psiStopBackgroundMovie(void) {
    maybeBackgroundMovieCleanup();
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

  if(hashcode == FMV_IDENT_EAGAMES_EN) {
    if(!IsNotPalI()) {
      switch(Language_Get()) {
        case 2:
          hashcode = FMV_IDENT_EAGAMES_FR;
          break;
        case 3:
          hashcode = FMV_IDENT_EAGAMES_DE;
          break;
        case 6:
          hashcode = FMV_IDENT_EAGAMES_ES;
          break;
        default:
          break;
      }
    }
  }

  maybeBackgroundMovieCleanup();
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
        case 0x700004c:
            return true;
      }
      return false;
}

HASHCODE GetLevelWithFmv(HASHCODE level) {
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
    case HT_Level_Cut_Level1:
      param_1 = 0x710000e;
      break;
    case HT_Level_Cut_Level2:
      param_1 = 0x710000f;
      break;
    case HT_Level_Cut_Level3:
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
      break;
    default: // No FMV, just load the level
      param_1 = level;
      break;
    }
    return (HASHCODE)param_1;
}

#define IsWarmReset U8_AT(0x00279250)

// AUTOGEN
void __cdecl GameFlow_PushState(int state, float param_2, uint param_3);

// AUTOINJECT
void GS_PauseGame(bool param_1) {

    GameState.SomeAlternatePauseState = param_1;

    for(int i = 0; i < ARRAY_SIZE(MPGame.players); i++) {
        MPGame.players[i].paused = param_1;
    }

    if (param_1) {
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

        GameState.NextLevelHashcode = (skipFmv ? level : GetLevelWithFmv(level));
        GameFlow_PushState(3, 0.0, 0xff);

      }
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The loading screen's picture for a level, and the text shown on it: a hint (one of the level's own, or, often,
// a general gameplay tip) and the level's objective. ResetMap_GenLoadScreen is the only caller.
// ---------------------------------------------------------------------------------------------------------------

// General gameplay tips, which replace a level's own hint half the time (and always when it has none). 0x1000129
// is not among them.
static const uint32_t kGeneralHints[] = {
    0x1000111, 0x1000112, 0x1000113, 0x1000114, 0x1000115, 0x1000116, 0x1000117, 0x1000118, 0x1000119, 0x100011a,
    0x100011b, 0x100011c, 0x100011d, 0x100011e, 0x100011f, 0x1000120, 0x1000121, 0x1000122, 0x1000123, 0x1000124,
    0x1000125, 0x1000126, 0x1000127, 0x1000128, 0x100012a, 0x100012b, 0x100012c, 0x100012d, 0x100012e, 0x100012f,
    0x1000130, 0x1000131, 0x1000132, 0x1000133, 0x1000134, 0x1000135, 0x1000136, 0x1000137, 0x1000138, 0x1000139,
    0x100013a, 0x100013b, 0x100013c,
};

// Multiplayer tips, for every multiplayer map.
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
    if (hintOut != NULL)
        *hintOut = LoadText(hints[Rand_Random() % N]);
}

static void SetObjective(char **objectiveOut, uint32_t objective) {
    if (objectiveOut != NULL)
        *objectiveOut = LoadText(objective);
}

// Half the time, and whenever the level gave no hint of its own, a general tip instead.
static void MaybeGeneralHint(char **hintOut) {
    bool keepLevelHint = (Rand_Random() & 1) != 0 && (hintOut == NULL || *hintOut != NULL);
    if (!keepLevelHint)
        PickHint(hintOut, kGeneralHints);
}

// The C++ under the register-argument entry below (not static: only the entry's inline assembly calls it).
HASHCODE ResetMap_LevelCode2ImgCore(HASHCODE level, char **hintOut, char **objectiveOut) {
    if (hintOut != NULL)
        *hintOut = NULL;
    if (objectiveOut != NULL)
        *objectiveOut = NULL;

    HASHCODE image;
    switch (level) {
    // Multiplayer maps: a multiplayer tip and no objective, and no general tip.
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
    case 0x700004c: {
        PickHint(hintOut, kMultiplayerHints);
        return LOADSCREEN_MULTIPLAYER;
    }

    // Castle. The exterior keeps its one hint: no general tip.
    case HT_Level_CastleExterior: {
        static const uint32_t hints[] = { 0x1000126 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000000);
        return LOADSCREEN_CASTLE;
    }
    case HT_Level_CastleCourtyard: {
        static const uint32_t hints[] = { 0x10000c0, 0x10000c1 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000001);
        image = LOADSCREEN_CASTLE;
        break;
    }
    case HT_Level_CastleIndoors1: {
        static const uint32_t hints[] = { 0x10000c2 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000002);
        image = LOADSCREEN_CASTLE;
        break;
    }
    case HT_Level_CastleIndoors2: {
        static const uint32_t hints[] = { 0x10000c3, 0x10000c4, 0x10000c5, 0x10000c6 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000003);
        image = LOADSCREEN_CASTLE;
        break;
    }

    // Henderson (Mayhew's estate and headquarters).
    case HT_Level_HendersonA: {
        static const uint32_t hints[] = { 0x10000d3, 0x10000d4 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000006);
        image = LOADSCREEN_HENDERSON;
        break;
    }
    case HT_Level_HendersonB: {
        static const uint32_t hints[] = { 0x10000c7, 0x10000c8, 0x10000cb, 0x10000d1 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000008);
        image = LOADSCREEN_HENDERSON;
        break;
    }
    case HT_Level_HendersonC: {
        static const uint32_t hints[] = { 0x10000ca, 0x10000cd, 0x10000ce, 0x10000c9, 0x10000d2 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400000a);
        image = LOADSCREEN_HENDERSON;
        break;
    }
    case HT_Level_HendersonD: {
        static const uint32_t hints[] = { 0x10000cf, 0x10000d0 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400000d);
        image = LOADSCREEN_HENDERSON;
        break;
    }

    // Tower.
    case HT_Level_TowerA: {
        static const uint32_t hints[] = { 0x10000d5, 0x10000d8, 0x10000df, 0x10000e2 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000016);
        image = LOADSCREEN_TOWER;
        break;
    }
    case HT_Level_TowerB: {
        static const uint32_t hints[] = { 0x10000d7, 0x10000d9, 0x10000db, 0x10000e0, 0x10000e3, 0x10000e4, 0x10000e6 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000019);
        image = LOADSCREEN_TOWER;
        break;
    }
    case HT_Level_TowerC: {
        static const uint32_t hints[] = { 0x10000dc, 0x10000da, 0x10000dd, 0x10000de, 0x10000e1, 0x10000e5 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400001d);
        image = LOADSCREEN_TOWER;
        break;
    }

    // Power station.
    case HT_Level_PowerStationA1: {
        static const uint32_t hints[] = { 0x10000e7, 0x10000e9, 0x10000eb, 0x10000ed, 0x10000ef, 0x10000f1, 0x10000f4 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400002c);
        image = LOADSCREEN_POWERSTATION;
        break;
    }
    case HT_Level_PowerStationA2: {
        static const uint32_t hints[] = { 0x10000e8, 0x10000ea, 0x10000ec, 0x10000ee, 0x10000f0, 0x10000f2, 0x10000f3,
                                          0x10000f5 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400002f);
        image = LOADSCREEN_POWERSTATION;
        break;
    }

    // The second tower. Tower2A's list really has 0x10000f8 twice (0x10000f7 never appears), so it comes up two
    // times in three. Tower2C has no hint of its own; the elevator keeps its one hint, with no general tip.
    case HT_Level_Tower2A: {
        static const uint32_t hints[] = { 0x10000f6, 0x10000f8, 0x10000f8 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000036);
        image = LOADSCREEN_TOWER2;
        break;
    }
    case HT_Level_Tower2B: {
        static const uint32_t hints[] = { 0x10000fe, 0x10000f9, 0x10000fa, 0x10000fc, 0x10000fd };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000037);
        image = LOADSCREEN_TOWER2;
        break;
    }
    case HT_Level_Tower2C:
        if (hintOut != NULL)
            *hintOut = NULL;
        SetObjective(objectiveOut, 0x400003a);
        image = LOADSCREEN_TOWER2;
        break;
    case HT_Level_Tower2Elevator: {
        static const uint32_t hints[] = { 0x10000fb };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000039);
        return LOADSCREEN_TOWER2;
    }

    // Drake's island base and the missile silo.
    case HT_Level_EvilBase: {
        static const uint32_t hints[] = { 0x1000103, 0x1000104, 0x1000105, 0x1000106 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x4000049);
        image = LOADSCREEN_EVILBASE;
        break;
    }
    case HT_Level_EvilSilo: {
        static const uint32_t hints[] = { 0x10000ff };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400004d);
        image = LOADSCREEN_EVILBASE;
        break;
    }
    case HT_Level_EvilBaseC: {
        static const uint32_t hints[] = { 0x1000100, 0x1000101, 0x1000102 };
        PickHint(hintOut, hints);
        SetObjective(objectiveOut, 0x400004f);
        image = LOADSCREEN_EVILBASE;
        break;
    }

    // Space station. The original stores the hint it picks through the objective pointer, not the hint pointer
    // (and only when there is a hint pointer); the objective then overwrites it, so this level never shows a hint
    // of its own - only, half the time, a general tip.
    case HT_Level_SpaceStationD: {
        static const uint32_t hints[] = { 0x1000108, 0x1000109, 0x100010a, 0x100010b, 0x100010c, 0x100010d, 0x100010e,
                                          0x100010f, 0x1000110 };
        if (hintOut != NULL)
            *objectiveOut = LoadText(hints[Rand_Random() % ARRAY_SIZE(hints)]);
        SetObjective(objectiveOut, 0x400005a);
        image = LOADSCREEN_SPACESTATION;
        break;
    }

    default:
        if (hintOut != NULL)
            *hintOut = NULL;
        if (objectiveOut != NULL)
            *objectiveOut = NULL;
        return LOADSCREEN_UNKNOWN;
    }

    MaybeGeneralHint(hintOut);
    return image;
}

// The original (0x000bedf0) takes the level in EAX, the hint pointer in ESI and the objective pointer in ECX, and
// preserves ESI and EDI, which its caller relies on; the C++ core preserves them too.
// AUTOLTCG
HASHCODE __declspec(naked) ResetMap_LevelCode2Img(HASHCODE level, char **hintOut, char **objectiveOut) {
    _asm {
        push ecx
        push esi
        push eax
        call ResetMap_LevelCode2ImgCore
        add esp, 12
        ret
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
// QueryPerformanceCounter rather than rdtsc scaled by a measured CPU frequency: it is already the fixed-rate
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

  static LARGE_INTEGER frequency = { 0 };
  static LARGE_INTEGER origin = { 0 };

  if (frequency.QuadPart == 0) {
    LARGE_INTEGER f, o;
    if (!QueryPerformanceFrequency(&f) || f.QuadPart == 0) {
      f.QuadPart = 10000000; // the usual value; being wrong beats dividing by zero
    }
    QueryPerformanceCounter(&o);
    origin = o;
    frequency = f; // published last, so nothing can see a frequency without an origin to go with it
  }

  LARGE_INTEGER now;
  QueryPerformanceCounter(&now);
  long long ticks = now.QuadPart - origin.QuadPart;

  // Split rather than (ticks * 1000000) / frequency, which overflows a 64-bit multiply after a few days of
  // uptime. Microseconds first, then scaled, to keep the original's quantisation - it truncated to whole
  // microseconds before converting to milliseconds and callers may compare small differences.
  long long micros = (ticks / frequency.QuadPart) * 1000000
                   + ((ticks % frequency.QuadPart) * 1000000) / frequency.QuadPart;
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
// route. The XMV video decoder is the caller that matters: maybeXmvDecoderCreate asks for the frequency once,
// stores frequency/1000 as ticks-per-millisecond, and maybeXmvDecoderUpdate divides counter deltas by it to
// decide when each frame of a background movie is due.
//
// Replacing both with the host's own pair keeps them consistent with each other, which is the only property
// the callers actually depend on - none of them assumes any particular frequency, they all ask. Patched by
// address rather than by name because the names collide with the Win32 functions being called here.
//
// Both are __stdcall taking one pointer and returning 1 in EAX, confirmed from the image: each ends RET 4.

// FUNC_AT(000f53d0)
uint32_t __stdcall Xbox_QueryPerformanceCounter(LARGE_INTEGER *counter) {
  QueryPerformanceCounter(counter);
  return 1;
}

// FUNC_AT(000f53e1)
uint32_t __stdcall Xbox_QueryPerformanceFrequency(LARGE_INTEGER *frequency) {
  if (!QueryPerformanceFrequency(frequency)) {
    frequency->QuadPart = 10000000; // as in timestamp() above: being wrong beats a divide by zero
  }
  return 1;
}

// AUTOGEN
void __stdcall PlrStat_Init(void);

// AUTOGEN
void SFXSetMode(unsigned int mode);

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
  return ((timestamp() - (float)INITIALISATION_TIME) * 0.1f);
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

  GameState.difficultyModifier = 2;
  MPSettings.RespawnSelectionMode = RESPAWN_RANDOM;
  GameState.ReloadMainMenu = 0x80000002;
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

  if ((sloflag == 0) && !GS_IsPaused(-1)) {
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
    if (movieFinished() || Input_Action(-1, ACTION_SKIP_CUTSCENE, 1)) {
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
  default:
    // GC check (0x800515e8). 5 is a valid state that does nothing this frame
    NF_WARN_IF(GameFlow_GetState() != 5, "Game state not valid %d\n", GameFlow_GetState());
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

  _FRAME_RATE = (float) gameFrameRate;
  FRAME_RATE_INT = gameFrameRate;
  FRAME_RATE_DIV = _FRAME_RATE * 0.016666667f;
  FRAME_RATE_MUL = (1.0f / _FRAME_RATE) * 60.0f;
  REC_FRAME_RATE = 1.0f / _FRAME_RATE;

}

// AUTOINJECT
bool Graphics_IsPalI(void) {
  return Gfx.isPalI;
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
  // Fixed: this was backwards (50 for PAL, 60 otherwise) relative to the original's own formula at this
  // exact spot ("(-(uint)(cVar1 != 0) & 10) + 50", i.e. 60 when Graphics_IsPalI() is true, 50 otherwise) -
  // confirmed against the raw disassembly of both this function and xboxInitGraphics's matching refresh-
  // rate calculation, which agree with each other and disagree with the ternary this used to have here.
  // Despite the name, Graphics_IsPalI() reads true for everywhere except the PAL-I region specifically.
  int refreshRate = Graphics_IsPalI() ? 60 : 50;

  int fpsOverride = Settings_GetFPSOverride();
  if (fpsOverride > 0)
    refreshRate = fpsOverride;

  GS_SetRefreshRate(refreshRate, refreshRate);
  GameFlow_Main();
}


// Keeps the background movie going, called every frame: decodes and draws the next frame, masks the top and
// bottom 66 lines to black for 0x073a0048 (letterboxed), and when the movie ends either replays it (looping,
// through psiStartBackgroundMovie's 'NPLY' "same file again") or stops it.
// AUTOINJECT
void maybeStartBackgroundMovie(void) {
    if (BackgroundMovieHashcode == 0)
        return;
    maybeDecodeMpgAudio();
    if (BackgroundMovieHashcode == 0x073a0048) {
        maybeResetRenderState(1);
        maybeImmediateModePushItem(0.0f, 0.0f, 640.0f, 66.0f, 0, 0, 0.0f, 0.0f, 0xff000000);
        maybeImmediateModePushItem(0.0f, 414.0f, 640.0f, 66.0f, 0, 0, 0.0f, 0.0f, 0xff000000);
        maybeImmediateModeFlush();
    }
    if (maybeBackgroundMovieIsPlaying())
        return;
    if (LoopingMovie != 0) {
        psiStartBackgroundMovie((HASHCODE)0x4e504c59, LoopingMovie, BackgroundMovieVolume);
        return;
    }
    maybeBackgroundMovieCleanup();
    BackgroundMovieHashcode = 0;
}
