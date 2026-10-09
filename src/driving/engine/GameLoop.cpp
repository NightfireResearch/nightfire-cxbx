#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif
#pragma fp_contract(off)

#include "GameLoop.h"
#include "ActionQueue.hpp"
#include "CoreFoundation.h"
#include "IOModule.hpp"
#include "MissionManager.h"
#include "PlayMPC.hpp"
#include "SimRandom.h"
#include "UFileLoader.h"
#include "UGroup.h"
#include "UMemory.hpp"
#include "USingleton.h"
#include "../render/Draw.h"
#include "../render/RenderHigh.h"
#include "../render/RSceneObj.hpp"
#include "../Scheduler.hpp"
#include "../anim/Actor.h"              // ActActorDatabase, ActorDatabase
#include "../anim/Manager.h"            // ActManager
#include "../camera/CameraIniLoader.h"
#include "../camera/PlayerCamera.h"
#include "../EventManager.hpp"
#include "../data/AttributeSet.h"
#include "../data/RCARPFile.h"
#include "../data/StdStreams.h"
#include "../data/Tuning.h"
#include "../eagl/Realgraph.h"
#include "../eagl/RenderContext.h"
#include "../eagl/View.h"
#include "../physics/RigidBody.h"
#include "../platform/FileSys.h"
#include "../platform/RealMemory.h"
#include "../platform/RealPrint.h"
#include "../platform/RealSystem.h"
#include "../platform/X87.h"
#include "../render/Renderer.h"
#include "../world/RoadNetwork.h"
#include "../world/TriggerManager.h"
#include "../world/World.h"
#include "../audio/Bank.h"
#include "../audio/Fader.h"
#include "../audio/SoundManager.h"
#include "../audio/Stream.h"
#include "../../common/launchInfo.h"
#include "../../helpers.h"

#include <mmintrin.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// main, the start-up and clean-up, the game loop and the launch page - see GameLoop.h for the shape. Each function
// makes the original's calls in the original's order, with the original's arguments; the loading screen's status
// lines included, since GLoadingScreen::Status also draws.
// ---------------------------------------------------------------------------------------------------------------

namespace {

// Objects the game's constructors build on our stack, as big as the original's frames make room for.
struct IFeedbackStorage { uint32_t words[1]; };
struct GSubtitlesStorage { uint32_t words[0x94 / 4]; };

// The 'Map ' group of the track file and its 'AIEl' data.
const uint32_t kTagMap = 0x4d617020;
const uint32_t kTagAIElements = 0x4149456c;
// The action that saves a screenshot.
const int32_t kActionScreenCapture = 0x48;
// The simulation's state when the level is over.
const int32_t kSimFinished = 2;
// The tasks RunTheGame adds, by event number.
enum TaskEvent {
    EAIUpdate = 1,
    EAudioUpdate = 2,
    ERenderFrame = 4,
    ESimFrameUpdate = 5,
    ESimEndFrame = 6,
    ECameraUpdate = 8,
    EAnimUpdate = 0x10,
};
// The launch page's video modes.
enum VideoMode { kVideoNtsc = 0, kVideoPal = 2, kVideoPal60 = 3, kVideoPalToo = 5 };
// Its languages, the ones with files of their own.
enum Language { kBrazilian = 0, kFrench, kGerman, kSpanish, kItalian, kDutch, kSwedish = 8 };
// What a driving-engine relaunch boots.
enum ReturnTarget { kReturnToActionEngine = 0, kNextDrivingMission = 1 };

char kDefaultXbe[] = "D:\\DEFAULT.XBE";
char kDrivingXbe[] = "D:\\DRIVING.XBE";
char *const kReturnImages[] = { kDefaultXbe, kDrivingXbe };

const char kLoaderReady[] = "LOADER READY";
const char kCarModels[] = "data\\car\\model\\";

}  // namespace

// ---- the game's globals

#define Launch (*(LaunchPage *)0x00243b90)
#define MissionName (Launch.missionName)
#define LaunchInfoRead I32_AT(0x00244790)
#define LaunchCommandLine ((char *)0x001e4660)    // a debugger command line, if the page was one

#define HeapConfigured BOOL8_AT(0x001e47bc)
#define gDAudio BOOL8_AT(0x001e4761)
#define ScreenCaptureEnabled BOOL8_AT(0x001e4766)
#define gDVD I32_AT(0x001e4768)
#define gSuperEasy BOOL8_AT(0x001e476c)
#define gSubtitles BOOL8_AT(0x001e476d)
#define gUsingMisc BOOL8_AT(0x001e476e)
#define gHandleMisc I32_AT(0x001e4658)
// Bond_SetExitToDriving's request, read by main at the end
#define ExitToDriving BOOL8_AT(0x001e476f)
#define ExitClearScore BOOL8_AT(0x001e4770)
#define ExitMission I32_AT(0x001e4774)
#define ExitMissionName ((char *)0x001e4778)

#define gStreams BOOL8_AT(0x001b7056)             // -streams / +streams
#define gPlayMovies BOOL8_AT(0x001b7057)
#define gSoundOption BOOL8_AT(0x001b7054)         // ASoundManager::Init's second argument
#define gUseBigFile BOOL8_AT(0x001e4854)          // UFileLoader::StartUsingBigFile's third
#define CurrentMission I32_AT(0x001e4850)
#define missionNames ((const char **)0x001b7060)   // "mis01" ... by mission index

#define fgScheduler (*(Scheduler **)0x001e520c)
#define GlobalActionQueue (*(ActionQueue *)0x001e4870)
#define Sim ((void *)0x00233ff0)                  // the Simulation
#define SimState I32_AT(0x00234e24)               // Sim.simState
#define WeaponManager (*(void **)0x0023923c)
#define playerPhysicsObject (*(RigidVehicle ***)0x00234e40)
#define CollisionManager (*(void **)0x00239a70)
#define TrafficSeed I16_AT(0x001de914)

// The disc error screen's: a one-byte block standing for "made", and the font
#define DiscErrorMade (*(void **)0x001e5210)
#define DiscErrorFont (*(const uint8_t **)0x001e5214)
typedef const char *DiscErrorText[3];
#define DiscErrorMessages ((DiscErrorText *)0x001c3450)   // English, French, German, Spanish, Italian, Swedish, Dutch
#define EAGLdriver ((void *)0x001cd114)
#define DebugFontData ((const uint8_t *)0x001b7480)

// Game code passed as a pointer, not called: the file system keeps the pointers, so they are the original's
// addresses (which jump to UMemory's callbacks), as the original stores them
#define UMemoryREALAllocCallbackAt ((FileMallocFn)0x00114630)
#define UMemoryREALFreeCallbackAt ((FileFreeFn)0x00114670)
#define GSubtitles_DrawFrame ((void (*)(int))0x000e3ab0)

// ---- the game's functions, not ours yet

// the C runtime: output, locale and heap are the game's
#define CRT_printf ((int (*)(const char *, ...))0x00132192)
#define CRT_stricmp ((int (*)(const char *, const char *))0x00134537)
#define CRT_free ((void (*)(void *))0x001331dc)
#define CRT_atexit ((int (*)(void (*)(void)))0x00132a7b)
#define CRT_clearfp ((unsigned (*)(void))0x00133c9e)

// XAPI (ours, behind the import thunk)
#define XInitDevices ((void (__stdcall *)(unsigned, void *))0x00184bae)

// render

// the rest of the game
#define GLoadingScreen_Status ((void (*)(const char *, ...))0x000e2ff0)
#define GLoadingScreen_InitLoadScreen ((void (*)(const char *, const char *))0x000e2390)
#define GLoadingScreen_InitLoadDots ((void (*)(bool))0x000e1f90)
#define GLoadingScreen_AddLoadingScreenSyncTask ((void (*)(void))0x000e2b70)
#define GLoadingScreen_DeleteLoadingScreenSyncTask ((void (*)(void))0x000e2b90)
#define GLoadingScreen_Shutdown ((void (*)(void))0x000e2a70)
#define GLoadingScreen_LoadAndDrawUnLoadingScreen ((void (*)(void))0x000e2110)
#define GLoadingScreen_FreeUnloadingScreen ((void (*)(void))0x000e2ba0)
#define GLocale_GetLanguage ((int (*)(void))0x000e30d0)
#define GLocale_GetSubDirectory ((const char *(*)(void))0x000e3140)
#define GSubtitles_TheApp ((void *(*)(void))0x000e3180)
#define GSubtitles_Construct ((void *(__fastcall *)(GSubtitlesStorage *, int))0x000e34e0)
#define GSubtitles_Destruct ((void (__fastcall *)(GSubtitlesStorage *, int))0x000e3730)
#define GSubtitles_LoadSubtitles ((void (__fastcall *)(void *, int, const char *))0x000e3840)
#define GSubtitles_ClearSubtitles ((void (__fastcall *)(void *, int))0x000e3630)
#define GSystem_CURATOR_Init ((void (*)(void))0x000e3fd0)
#define CURATOR_Shutdown ((void (*)(void))0x000e3eb0)
#define RayShell_Reset ((void (*)(void))0x00071d40)
#define Simulation_Reset ((void (__fastcall *)(void *, int))0x000b49f0)
#define Simulation_CleanUpObjects ((void (__fastcall *)(void *, int))0x000b4750)
#define Simulation_SpawnCarObject ((void *(__fastcall *)(void *, int, int, const char *, int, float *, float *))0x000b5700)
#define SMissionManager_GetCarType ((const char *(__fastcall *)(void *, int))0x000b6140)
#define SMissionManager_ClearScoreBuffer ((void (__fastcall *)(void *, int, LaunchPage *))0x000b63f0)
#define SWeaponManager_Construct ((void *(__fastcall *)(void *, int))0x000bb220)
#define SWeaponManager_Destruct ((void (__fastcall *)(void *, int))0x000bad40)
#define SWeaponManager_SetPlayerCar ((void (__fastcall *)(void *, int, void *))0x000ba990)
#define PVehicle_InitializeGlobals ((void (*)(void))0x0006f930)
#define PVehicle_Shutdown ((void (*)(void))0x00071900)
#define PVehicle_GetNamedAttribs ((AttributeSet *(*)(AttributeSet *, const char *))0x0006f810)
#define PVehicle_RenderNameAttrib ((const char *(*)(AttributeSet *))0x0006f860)
#define PBondCar_InitializeBondCarGlobals ((void (*)(void))0x00061ae0)
#define WCollisionMgr_GetWorldHeightAtPoint ((void (__fastcall *)(void *, int, float *, float *, bool))0x000bf210)
#define AIElementController_Construct ((void (*)(void *))0x000285b0)
#define AIVehicleController_Get ((void *(*)(void))0x00035c00)
#define AIVehicleController_Init ((void (*)(void))0x00035c10)
#define AIVehicleController_Shutdown ((void (*)(void))0x000364d0)
#define AIVehicleController_CreateSimTrafficCars ((void (__fastcall *)(void *, int, bool))0x000366e0)
#define AIVehicleController_RegisterSimCarList ((void (__fastcall *)(void *, int))0x00036990)
#define AIZoneController_Init ((void (*)(void))0x00036a20)
#define AIZoneController_Shutdown ((void (*)(void))0x00036b00)
#define AIRoadSpawn_Init ((void (*)(void))0x000347f0)
#define AIRoadSpawn_Shutdown ((void (*)(void))0x000349e0)
#define AICharacter_Init ((void (*)(void))0x0001c190)
#define AICharacter_Shutdown ((void (*)(void))0x0001c1b0)
#define InputConfigManager_Get ((void *(*)(void))0x00050270)
#define InputConfigManager_InitAndPreload ((void (__fastcall *)(void *, int))0x000504d0)
#define InputConfigManager_Shutdown ((void (__fastcall *)(void *, int))0x00050080)
#define IFeedback_Construct ((void *(__fastcall *)(IFeedbackStorage *, int, int))0x0004fe30)
#define IFeedback_Destruct ((void (__fastcall *)(IFeedbackStorage *, int))0x0004fb10)
#define IFeedback_Pause ((void (*)(void))0x0004fa00)

// The car's type name (a virtual method of the player's vehicle).
static const char *PlayerCarTypeName() {
    return (*playerPhysicsObject)->GetCarType();
}

// MissionName against a track name, as the original compares them: the name's bytes and its terminator.
static bool IsMission(const char *track) {
    return memcmp(MissionName, track, strlen(track) + 1) == 0;
}

// ---- small things

// The line's key must start the text or follow a line break; a match elsewhere moves the search on one
// character - and, as in the original, leaks that attempt's copy of the key.
// FUNC_AT(0x0005b870)
OptionParser* OptionParser::Construct(const char *text, const char *key) {
    value = NULL;
    length = 0;
    bool done = false;
    const char *cursor = text;
    while (*cursor != 0) {
        size_t keyLength = strlen(key);
        char *needle = (char *)OperatorNewArray((unsigned)keyLength + 1);
        strcpy(needle, key);
        needle[keyLength] = 0;
        const char *found = strstr(cursor, needle);
        if (found == NULL) {
            done = true;
            value = NULL;
            length = 0;
        } else if (found != text && found[-1] != '\n' && found[-1] != '\r') {
            cursor = found + 1;
            continue;   // the original skips the delete here
        } else {
            cursor = found + 1;
            const char *after = found + keyLength;
            if (*after != ' ' && *after != '=') {
                value = NULL;
                length = 0;
            } else {
                done = true;
                value = after;
                while (*value == ' ' || *value == '=' || *value == '#' || *value == '(' || *value == '[')
                    value++;
                const char *newline = strchr(value, '\n');
                length = newline != NULL ? int(newline - value) : int(strlen(value));
                if (length != 0) {
                    if (value[length - 1] == ']')
                        length--;
                    if (value[length - 1] == ')')   // after a ']' that emptied it, the byte before the value
                        length--;
                }
            }
        }
        OperatorDelete(needle);
        if (done)
            break;
    }
    return this;
}

// FUNC_AT(0x0005b9c0)
bool OptionParser::GetFullString(char *out) {
    if (value == NULL)
        return false;
    strncpy(out, value, length);
    out[length] = 0;
    return true;
}

// ---- the launch page

// FUNC_AT(0x00059900)
void LaunchPage::SetLoaderReady(const char *text) {
    strcpy(loaderReady, text);
}

// FUNC_AT(0x001306d0)
void LaunchPage::GetLaunchInfo(char *commandLine) {
    if (LaunchInfoRead != 0)
        return;
    LaunchInfoRead = 1;
    MEM_fill(this, 0, sizeof(LaunchPage));
    int type = 0;   // the original's was the saved ECX, the page's address - never 3 either
    if (XGetLaunchInfo(&type, &Launch) == 0 && type == 3) {
        if (commandLine != NULL)
            strcpy(commandLine, (const char *)&Launch);
        MEM_fill(this, 0, sizeof(LaunchPage));
    }
}

// FUNC_AT(0x00130740)
void LaunchPage::ReturnToAction(int target) {
    if (loaderReady[0] != 0) {
        returnTarget = target;
        XLaunchNewImageA(kReturnImages[target], &Launch);
        return;
    }
    CRT_printf("Loader not present!\n");
}

// FUNC_AT(0x000596a0)
void MissionNumToString() {
    if (Launch.handOver == 0)
        return;
    Launch.missionNum &= 0xff;
    Launch.savedDifficulty = Launch.difficulty;
    switch (Launch.difficulty) {
    case 1: Launch.difficulty = 0; break;
    case 2: Launch.difficulty = 1; break;
    case 3:
    case 4: Launch.difficulty = 2; break;
    }
    switch (Launch.missionNum) {
    case 1: strcpy(MissionName, "paris_mis01"); break;
    case 2: strcpy(MissionName, "uw_mis11"); break;
    case 3: strcpy(MissionName, "junglea_mis13a"); break;
    case 4: strcpy(MissionName, "jungleb_mis13b"); break;
    case 5: strcpy(MissionName, "snow1a_mis3"); break;
    case 6: strcpy(MissionName, "snow2a_mis4"); break;
    case 7: strcpy(MissionName, "junglec_mis13c"); break;
    case 8: strcpy(MissionName, "snow2a_race"); break;
    default: strcpy(MissionName, "paris_map"); break;
    }
    strcpy(Launch.localeFile, "IDL_ENG.LOC");
    Launch.unknown614 = Launch.unknown970 != 0 ? 2 : 0;
}

// FUNC_AT(0x00059920)
bool ApplicationMemoryHeapConfig() {
    if (!HeapConfigured) {
        Launch.GetLaunchInfo(LaunchCommandLine);
        if (Launch.handOver != 0 && Launch.loaderReady[0] == 0)
            Launch.SetLoaderReady(kLoaderReady);
        XInitDevices(0, NULL);
        if (Launch.loaderReady[0] != 0)
            gDVD = Launch.dvdSource;
        int mode;
        if (Launch.videoMode == kVideoPal || Launch.videoMode == kVideoPalToo)
            mode = 2;
        else if (Launch.videoMode == kVideoPal60)
            mode = 3;
        else
            mode = 1;
        SetFoundationVideoMode(mode);
        PRINT_setdevicestate(1, 0);
        PRINT_setdevicestate(2, 0);
        TIMER_init(Ftol(GetFoundationVideoModeRate()));
        UMemory::Init(0x2400000, 0, NULL);
        UMemory::AddFastBlocks(0x10);
        HeapConfigured = 1;
    }
    NullFunction();
    return true;
}

// The loading screen's picture (W: the widescreen one) and layout. The second jungle part, the third and anything
// unknown take the default layout; anything unknown takes the Paris picture.
// FUNC_AT(0x000599f0)
void Bond_LoadingScreen() {
    char suffix[12];
    char layout[128];
    char picture[128];
    GLoadingScreen_Status("Show loading screen");
    suffix[0] = 0;
    if (fgRenderer->widescreen)
        snprintf(suffix, sizeof(suffix), "W");
    const char *layoutFile;
    if (IsMission("paris_mis01")) {
        snprintf(picture, sizeof(picture), "data\\loading\\paris_mis01%s%s", suffix, ".xsh");
        layoutFile = "data\\loading\\paris_mis01.ini";
    } else if (IsMission("snow1a_mis3")) {
        snprintf(picture, sizeof(picture), "data\\loading\\snow1a_mis3%s%s", suffix, ".xsh");
        layoutFile = "data\\loading\\snow1a_mis3.ini";
    } else if (IsMission("snow2a_mis4")) {
        snprintf(picture, sizeof(picture), "data\\loading\\snow2a_mis4%s%s", suffix, ".xsh");
        layoutFile = "data\\loading\\snow2a_mis4.ini";
    } else if (IsMission("uw_mis11")) {
        snprintf(picture, sizeof(picture), "data\\loading\\uw_mis11%s%s", suffix, ".xsh");
        layoutFile = "data\\loading\\uw_mis11.ini";
    } else if (IsMission("junglea_mis13a")) {
        snprintf(picture, sizeof(picture), "data\\loading\\junglea_mis13a%s%s", suffix, ".xsh");
        layoutFile = "data\\loading\\junglea_mis13a.ini";
    } else if (IsMission("jungleb_mis13b")) {
        snprintf(picture, sizeof(picture), "data\\loading\\jungleb_mis13b%s%s", suffix, ".xsh");
        layoutFile = "data\\loading\\default.ini";
    } else {
        if (IsMission("junglec_mis13c"))
            snprintf(picture, sizeof(picture), "data\\loading\\junglec_mis13c%s%s", suffix, ".xsh");
        else
            snprintf(picture, sizeof(picture), "data\\loading\\paris_mis01%s%s", suffix, ".xsh");
        layoutFile = "data\\loading\\default.ini";
    }
    snprintf(layout, sizeof(layout), "%s", layoutFile);   // the original's sprintf(layout, layoutFile)
    GLoadingScreen_InitLoadScreen(picture, layout);
}

// FUNC_AT(0x00059bf0)
void Bond_SetExitToDriving(int mission, const char *missionName, bool clearScore) {
    strcpy(ExitMissionName, missionName);
    ExitMission = mission;
    ExitToDriving = 1;
    ExitClearScore = clearScore;
}

// FUNC_AT(0x00059d80)
void Bond_StartUpSystem() {
    if (Launch.loaderReady[0] == 0) {
        GLoadingScreen_Status("Init IOP");
        strcpy(Launch.loaderReady, kLoaderReady);
    }
    switch (Launch.language) {
    case kBrazilian: strcpy(Launch.localeFile, "IDL_BRT.loc"); break;
    case kFrench: strcpy(Launch.localeFile, "IDL_FR.loc"); break;
    case kGerman: strcpy(Launch.localeFile, "IDL_GER.loc"); break;
    case kSpanish: strcpy(Launch.localeFile, "IDL_SPA.loc"); break;
    case kItalian: strcpy(Launch.localeFile, "IDL_ITA.loc"); break;
    case kDutch: strcpy(Launch.localeFile, "IDL_DUT.loc"); break;
    case kSwedish: strcpy(Launch.localeFile, "IDL_SWE.loc"); break;
    default: strcpy(Launch.localeFile, "IDL_ENG.loc"); break;
    }

    GLoadingScreen_Status("Init main singletons");
    USingletonManager *singletons = SingletonManager();
    DTuningDBMgr::InitSingleton();
    singletons->Register(reinterpret_cast<USingleton *>(TuningDBMgr));
    singletons = SingletonManager();
    AttributeSystem::Init();
    singletons->Register(reinterpret_cast<USingleton *>(AttributeSystemInstance));

    GLoadingScreen_Status("Init Lib Render");
    Render_InitLibRender();
    GLoadingScreen_InitLoadDots(true);
    GLoadingScreen_AddLoadingScreenSyncTask();
    DiscError_Init();

    GLoadingScreen_Status("Init File System");
    FILESYS_setmemcallbacks(UMemoryREALAllocCallbackAt, UMemoryREALFreeCallbackAt);
    FILESYS_init(0x10, 0x32, 0x20);
    GLoadingScreen_Status("Init ASync File System");
    ASYNCFILE_init(0x14, 0);
    GLoadingScreen_Status("Init Threads");
    THREAD_init();
    GLoadingScreen_Status("CPU Detect");
    CPU_detect();
    UFileLoader::Startup();

    GLoadingScreen_Status("Opening misc.viv");
    if (FILESYS_existssync("driving\\misc.viv", 100))
        gUsingMisc = FILESYS_addbigsync("driving\\misc.viv", 0, 100, &gHandleMisc) != 0;
    if (gUsingMisc)
        GLoadingScreen_Status("*** misc.viv found, and being used! ***");

    GLoadingScreen_Status("Init Global Render");
    RCARPFile::LoadEAGLMaterialsThunk();
    GLoadingScreen_Status("Init Giotto");
    GSystem_CURATOR_Init();
    GLoadingScreen_Status("Load debug font");
    fgRenderer->LoadDebugFont();
    Bond_LoadingScreen();
    GameLoop_StartUsingMainBigFile();

    GLoadingScreen_Status("Init Attribute System");
    AttributeSystemInstance->PrepareDatabase();
    AttributeSet sentryDefaults, sentry;
    sentryDefaults.Construct("sentry", "default");
    sentry.Construct("sentry", "sentry");
    RigidBody::InitRigidBodySystem();

    GLoadingScreen_Status("Loading track file");
    WWorld::InitSingleton();
    fgWorld->LoadTrackFile("data\\track\\", MissionName);

    GLoadingScreen_Status("Init Controllers");
    IOModule::GetIOModule()->Initialize();
    InputConfigManager_InitAndPreload(InputConfigManager_Get(), 0);

    GLoadingScreen_Status("Init Noise");
    Noise::Init();
    sentry.Destruct();
    sentryDefaults.Destruct();
}

// FUNC_AT(0x0005a110)
void Bond_CleanUp() {
    CURATOR_Shutdown();
    ASoundManager::Stop();
    AFader::Remove("SpeechVsAmbience");
    AStream::Remove("speech");
    AStream::Remove("music");
    ASoundManager::Shutdown();
    SingletonManager()->KillAll();
    if (gUsingMisc)
        FILESYS_delbigsync(gHandleMisc, 100);
    NullFunction();
    ASYNCFILE_restore();
    InputConfigManager_Shutdown(InputConfigManager_Get(), 0);
    IOModule::GetIOModule()->Release();
    DiscError_Shutdown();
    FreeEAGLMaterialsThunk();
    RRenderer::Shutdown();
    UMemory::Shutdown();
    CRT_printf("Bond_CleanupDone");
}

// The command line (a first boot only): +/-streams, -ntsc, -pal, -pal60, -T<track>. A track named "mis<N>..."
// also picks mission index N - 1.
// FUNC_AT(0x0005a1b0)
int GameMain(int argc, char **argv) {
    ApplicationMemoryHeapConfig();
    Launch.driveCount++;
    int mission = 0;
    MissionNumToString();
    Launch.bootCount++;
    if (Launch.loaderReady[0] == 0) {
        Launch.musicVolume = 100;
        Launch.effectsVolume = 100;
        Launch.audioMode = 1;
        for (int i = 1; i < argc; i++) {
            const char *arg = argv[i];
            if (arg[0] != '-' && arg[0] != '+')
                continue;
            bool plus = arg[0] == '+';
            if (CRT_stricmp(arg + 1, "streams") == 0) {
                gStreams = plus;
            } else if (CRT_stricmp(argv[i] + 1, "ntsc") == 0) {
                Launch.videoMode = kVideoNtsc;
            } else if (CRT_stricmp(argv[i] + 1, "pal") == 0) {
                Launch.videoMode = kVideoPal;
            } else if (CRT_stricmp(argv[i] + 1, "pal60") == 0) {
                Launch.videoMode = kVideoPal60;
            } else if (argv[i][1] == 'T' || argv[i][1] == 't') {
                strncpy(MissionName, argv[i] + 2, 15);
                MissionName[15] = 0;
                WWorld::SetTrackName(argv[i] + 2, true);
                if (strncmp(argv[i] + 2, "mis", 3) == 0)
                    mission = atol(argv[i] + 5) - 1;
            }
        }
    } else {
        gDAudio = Launch.dolbyAudio != 0;
        gSubtitles = Launch.subtitles != 0;
    }
    if (argv != NULL)
        CRT_free(argv);   // src/driving/platform/XboxStartup.cpp builds it in the game's heap for this

    if (Launch.bootCount == 1 && Launch.difficulty == 0 && Launch.unknowna18 == 0)
        gSuperEasy = 1;
    ExitToDriving = 0;
    ExitClearScore = 0;
    if (CRT_stricmp(MissionName, "paris_mis01") != 0)
        gSuperEasy = 0;
    if (MissionName[0] != 0) {
        WWorld::SetTrackName(MissionName, true);
        if (strncmp(MissionName, "mis", 3) == 0)
            mission = atol(MissionName + 3) - 1;
    }

    Bond_StartUpSystem();
    GLoadingScreen_Status("Entering GameLoop");
    IFeedbackStorage feedback;   // in the original, the slot argv came in
    IFeedback_Construct(&feedback, 0, Launch.controllerPort);
    GameLoop_MainGameLoop(false, false, false, 0, mission, 0);
    IFeedback_Destruct(&feedback, 0);
    Bond_CleanUp();

    if (Launch.unknown978 == 3)
        Launch.unknown978 = 1;
    if ((Launch.flags & 0x80) && !ExitToDriving) {
        Launch.flags &= ~0x80u;
        Launch.unknown978 = 2;
        ExitClearScore = 1;
    }
    if ((Launch.flags & 0x100) && !ExitToDriving) {
        Launch.flags &= ~0x100u;
        Launch.unknown978 = 2;
        ExitClearScore = 1;
    }
    if (Launch.handOver != 0 && Launch.savedDifficulty > 0)
        Launch.difficulty = Launch.savedDifficulty;
    if (!ExitToDriving)
        Launch.flags &= 0xffffc1f9;
    if (Launch.loaderReady[0] != 0) {
        if (ExitClearScore) {
            SMissionManager_ClearScoreBuffer(glbMissionManager, 0, &Launch);
            ExitClearScore = 0;
        }
        if (ExitToDriving) {
            Launch.missionNum = ExitMission;
            strncpy(MissionName, ExitMissionName, 15);
            MissionName[15] = 0;
            Launch.ReturnToAction(kNextDrivingMission);
        } else {
            Launch.ReturnToAction(kReturnToActionEngine);
        }
    }
    return 0;
}

// ---- the game loop

// FUNC_AT(0x0005a650)
void GameLoop_StartUsingMainBigFile() {
    const char *name = MissionName;
    for (const char *at = MissionName; *at != 0; at++)
        if (*at == '_')
            name = at + 1;
    GLoadingScreen_Status("Opening %s.viv", name);
    if (UFileLoader::StartUsingBigFile("driving", name, gUseBigFile))
        GLoadingScreen_Status("*** %s.viv found, and being used! ***", name);
}

// The intro or outro movie: input, rumble and sound paused, every action queue flushed before and after, the
// pads resynchronised afterwards.
void GameLoop_PlayMovie(const char *movie, const char *subtitles) {
    EAGL::RenderContext *context = fgRenderer->renderContext;
    PlayMPC player;
    player.Construct(fgRenderer->device, context, Launch.controllerPort, 0);
    player.Init(movie, Launch.effectsVolume * 127 / 100, false);
    EAGL::ViewPort *view = context->NewViewPort();
    view->SetShape(0.0f, 0.0f, (float)fgRenderer->screenWidth, (float)fgRenderer->screenHeight, 0.01f, 1.0f);
    view->SetOrthographic(0.0f, 200.0f);
    GLoadingScreen_Status("Play Movie");
    IOModule::GetIOModule()->EnableUpdating(false);
    IFeedback_Pause();
    ASoundManager::Pause();
    ActionQueueManager::GetActionQueueManager()->FlushAllQueues();
    fgRenderer->Flush(false);
    if (subtitles != NULL) {
        GSubtitles_LoadSubtitles(GSubtitles_TheApp(), 0, subtitles);
        player.Play(view, fgRenderer->widescreen != 0, GSubtitles_DrawFrame);
        GSubtitles_ClearSubtitles(GSubtitles_TheApp(), 0);
    } else {
        player.Play(view, fgRenderer->widescreen != 0, NULL);
    }
    context->DeleteViewPort(view);
    player.Destruct();
    IOModule::GetIOModule()->resyncDevices = true;
    IOModule::GetIOModule()->EnableUpdating(true);
    ActionQueueManager::GetActionQueueManager()->FlushAllQueues();
    ASoundManager::Resume();
    _mm_empty();
    CRT_clearfp();
}

// The original's entry: the movie on the stack, the subtitle file in EBX (RunTheGame's two calls).
// AUTOLTCG
__declspec(naked) void FUN_0005a6b0(const char *movie) {
    __asm {
        push ebx
        push dword ptr [esp + 8]
        call GameLoop_PlayMovie
        add esp, 8
        ret
    }
}

// FUNC_AT(0x0005a870)
void GameLoop_CleanUp() {
    GLoadingScreen_Status("---------------- Shutting Down Game ---------------");
    GLoadingScreen_Status("Kill World Camera");
    NullFunction();
    GLoadingScreen_Status("Kill Player Camera");
    RPlayerCamera::Shutdown();
    GLoadingScreen_Status("Kill PIP Camera");
    GLoadingScreen_Status("Shutdown AI characters");
    AICharacter_Shutdown();
    RRenderHigh::KillTrackRenderPostSim();
    GLoadingScreen_Status("Shutdown Roads");
    WRoadNetwork::Shutdown();
    GLoadingScreen_Status("Shutdown AI Controller's");
    AIRoadSpawn_Shutdown();
    AIZoneController_Shutdown();
    AIVehicleController_Shutdown();
    GLoadingScreen_Status("Deinit world");
    fgWorld->Close();
    GLoadingScreen_LoadAndDrawUnLoadingScreen();
    fgRenderer->Flush(true);
    GLoadingScreen_FreeUnloadingScreen();

    GLoadingScreen_Status("Delete weapon manager");
    void *weapons = WeaponManager;
    if (weapons != NULL) {
        SWeaponManager_Destruct(weapons, 0);
        OperatorDelete(weapons);
    }
    WeaponManager = NULL;
    const char *carType = PlayerCarTypeName();
    AttributeSet carAttributes;
    RSceneObj::PurgePreloaded(kCarModels, PVehicle_RenderNameAttrib(PVehicle_GetNamedAttribs(&carAttributes, carType)));
    carAttributes.Destruct();

    GLoadingScreen_Status("Shutdown Sim");
    Simulation_CleanUpObjects(Sim, 0);
    NullFunction();
    PVehicle_Shutdown();
    GLoadingScreen_Status("Dismiss actors");
    ActManager::ShutDown();
    ActActorDatabase::ShutDown();
    GLoadingScreen_Status("Cleanup render objects");
    RSceneObj::DestroyAll();
    GLoadingScreen_Status("Deinit world");
    WWorld *world = fgWorld;
    if (world != NULL) {
        world->Destruct();
        OperatorDelete(world);
    }
    fgWorld = NULL;
    GLoadingScreen_Status("Kill Game Render");
    RRenderHigh::KillGameRender();
    GLoadingScreen_Status("Shutdown Scheduler");
    Scheduler::Shutdown();
    GLoadingScreen_Status("Shutdown Event Manger");
    EventManager::Shutdown();
    GLoadingScreen_Status("Shutdown Clock");
    RealClock_CleanUp();
    ASoundManager::Stop();
    GLoadingScreen_Status("Unload banks");
    ASoundManager::ClearMission();
    GLoadingScreen_Status("------------ Finished Shutting Down Game ----------");
}

// The level's run: its tasks, the intro movie, the loop until the simulation ends, the outro movie. With
// simulateOncePerLoop the simulation's tasks go on the per-frame schedule instead of the sim-rate one (nothing
// passes true). A movie path left null by the intro stays null for the outro.
// FUNC_AT(0x0005aa80)
void RunTheGame(bool simulateOncePerLoop, int unused) {
    (void)unused;
    CRT_printf("Track is %s\n", MissionName);
    MEM_validate();
    RRenderHigh render;
    render.Construct();
    MEM_validate();

    Schedule *perFrame = fgScheduler->s_oncePerGameLoop;
    Schedule *simulation = fgScheduler->s_SimRate;
    if (simulateOncePerLoop)
        simulation = perFrame;
    fgScheduler->ResetTime();
    simulation->AddTask(ESimFrameUpdate, 0, 0, true, 0, 0);
    simulation->AddTask(EAnimUpdate, 0, 2, true, 0, 0);
    simulation->AddTask(EAIUpdate, 0, 2, true, 0, 0);
    perFrame->AddTask(ERenderFrame, 0, 6, true, 0, 0);
    perFrame->AddTask(EAudioUpdate, 0, 7, true, 0, 0);
    simulation->AddTask(ESimEndFrame, 0, 7, true, 0, 0);
    simulation->AddTask(ECameraUpdate, 0, 2, true, 0, 0);
    fgTriggerManager->Update();

    char movieFile[128];
    char subtitleFile[128];
    const char *movie = movieFile;
    const char *subtitles = subtitleFile;
    if (IsMission("paris_mis01")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\paris_intro.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\paris_intro.sub");
    } else if (IsMission("uw_mis11")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\island_intro2.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\island_intro2.sub");
    } else if (IsMission("snow1a_mis3")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\snow_intro.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\snow_intro.sub");
    } else if (IsMission("snow2a_mis4")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\alps_intro.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\alps_intro.sub");
    } else if (IsMission("junglea_mis13a")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\ja_intro.mad", GLocale_GetSubDirectory());
        subtitles = NULL;
    } else if (!IsMission("jungleb_mis13b") && IsMission("junglec_mis13c")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\jc_intro.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\jc_intro.sub");
    } else {
        subtitles = NULL;
        movie = NULL;
    }
    GLoadingScreen_DeleteLoadingScreenSyncTask();
    GLoadingScreen_Shutdown();
    GSubtitlesStorage subtitlePlayer;
    GSubtitles_Construct(&subtitlePlayer, 0);
    if (!gSubtitles)
        subtitles = NULL;
    if (gPlayMovies && movie != NULL)
        GameLoop_PlayMovie(movie, subtitles);
    if (IsMission("demo1"))
        AStream::Get("music")->Event("opera\\puccini48sec", -1.0f, true, false, false);
    if (ASystem_fgSystem != NULL) {
        RefCounterNode *scratch;
        RefCounterIterator bank = { *ABank::Begin(&scratch) };
        while (bank.node != *ABank::End(&scratch))
            bank.Increment();
    }
    UFileLoader::DumpFileRequestList();   // the original pushes "..\\bigprocess" to both; neither reads it
    UFileLoader::StopUsingBigFile();
    GLoadingScreen_Status("Entering main loop");
    fgScheduler->ResetTime();

    while (SimState != kSimFinished) {
        while (!GlobalActionQueue.IsEmpty()) {
            ActionRef action;
            GlobalActionQueue.GetAction(&action);
            if (action.data != NULL && action.data->action == kActionScreenCapture && ScreenCaptureEnabled)
                fgRenderer->screenCapturePending = 1;
            GlobalActionQueue.PopAction();
        }
        SYNCTASK_run(0);
        fgScheduler->Run(0);
    }

    GLoadingScreen_Status("Leaving loop");
    if (!ASoundManager_fgIsPaused)
        ASoundManager::Pause();
    fgRenderer->Flush(false);
    simulation->RemoveAllTasks();

    subtitles = subtitleFile;
    if (IsMission("paris_mis01")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\paris_outro.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\paris_outro.sub");
    } else if (IsMission("uw_mis11")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\island_outro.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\island_outro.sub");
    } else if (IsMission("snow1a_mis3")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\snow_outro.mad", GLocale_GetSubDirectory());
        strcpy(subtitleFile, "data\\loading\\snow_outro.sub");
    } else if (IsMission("junglea_mis13a")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\ja_outro.mad", GLocale_GetSubDirectory());
        subtitles = NULL;
    } else if (IsMission("junglec_mis13c")) {
        snprintf(movieFile, sizeof(movieFile), "%s\\jc_outro.mad", GLocale_GetSubDirectory());
        subtitles = NULL;
    } else {
        subtitles = NULL;
        movie = NULL;
    }
    if (!gSubtitles)
        subtitles = NULL;
    if (gPlayMovies && movie != NULL && (Launch.unknown978 == 0 || Launch.unknown5d8 != 0))
        GameLoop_PlayMovie(movie, subtitles);
    GSubtitles_Destruct(&subtitlePlayer, 0);
    render.Destruct();
}

// The speech file is <mission's speech name><language>, falling back to English when the language's is missing.
// FUNC_AT(0x0005b0d0)
void GameLoop_StartUp(int trafficSeed) {
    GLoadingScreen_Status("Reset Ray Shells");
    RayShell_Reset();
    GLoadingScreen_Status("Reset Sim");
    Simulation_Reset(Sim, 0);
    GLoadingScreen_Status("Init Clock");
    RealClock_Init();
    GLoadingScreen_Status("Init Events");
    EventManager::Init();
    GLoadingScreen_Status("Init Scheduler");
    Scheduler::Init();
    PVehicle_InitializeGlobals();
    PBondCar_InitializeBondCarGlobals();
    GLoadingScreen_Status("Init Game Render");
    RRenderHigh::InitGameRender();

    GLoadingScreen_Status("Init Sound");
    GameStd::String speechName;   // constructed empty, inline
    speechName.capacity = 15;
    speechName.size = 0;
    speechName.text.buffer[0] = 0;
    const char *const languageSuffix[8] = { "en", "fr", "ge", "sp", "en", "en", "en", "en" };
    if (CRT_stricmp(MissionName, "uw_mis11") == 0 || CRT_stricmp(MissionName, "uw_map") == 0)
        speechName.AssignText("mis11", 5);
    else if (CRT_stricmp(MissionName, "snow1a_mis3") == 0 || CRT_stricmp(MissionName, "snow1a") == 0)
        speechName.AssignText("mis3", 4);
    else if (CRT_stricmp(MissionName, "snow2a_mis4") == 0 || CRT_stricmp(MissionName, "snow2a") == 0 ||
             CRT_stricmp(MissionName, "snow2a_race") == 0)
        speechName.Assign("mis4");
    else if (CRT_stricmp(MissionName, "paris_mis01") == 0 || CRT_stricmp(MissionName, "paris_map") == 0)
        speechName.Assign("mis01");
    else if (CRT_stricmp(MissionName, "junglea_mis13a") == 0 || CRT_stricmp(MissionName, "junglea") == 0)
        speechName.Assign("mis13a");
    else if (CRT_stricmp(MissionName, "jungleb_mis13b") == 0 || CRT_stricmp(MissionName, "jungleb") == 0)
        speechName.Assign("mis13b");
    else if (CRT_stricmp(MissionName, "junglec_mis13c") == 0 || CRT_stricmp(MissionName, "junglec") == 0)
        speechName.Assign("mis13c");
    else
        speechName.Assign("mis11");
    char speech[128];
    char speechFile[84];
    snprintf(speech, sizeof(speech), "%s%s", speechName.Data(), languageSuffix[GLocale_GetLanguage()]);
    snprintf(speechFile, sizeof(speechFile), "driving\\%s.spe", speech);
    if (!FILESYS_existssync(speechFile, 100)) {
        GLoadingScreen_Status("WARNING: localized speech not found, using English");
        snprintf(speech, sizeof(speech), "%s%s", speechName.Data(), "en");
    }
    MusicVolumeScale = float(double(Launch.musicVolume) * 0.01f);
    EffectsVolumeScale = float(double(Launch.effectsVolume) * 0.01f);
    ASoundManager::Init("data\\audio\\", gSoundOption, speech, MissionName, Launch.audioMode);
    GLoadingScreen_Status("Music Volume : Uber(%d) --> Driving(%f)\n", Launch.musicVolume, double(MusicVolumeScale));
    GLoadingScreen_Status("Effects Volume : Uber(%d) --> Driving(%f)\n", Launch.effectsVolume,
                          double(EffectsVolumeScale));
    GLoadingScreen_Status("Audio System Mode : %d\n", Launch.audioMode);

    GLoadingScreen_Status("Init Actors");
    ActActorDatabase::StartUp();
    ActManager::StartUp(1.0f / GetFoundationVideoModeRate());
    GLoadingScreen_Status("Init Weapon Manager");
    void *weapons = OperatorNew(0x180);
    WeaponManager = weapons != NULL ? SWeaponManager_Construct(weapons, 0) : NULL;
    GLoadingScreen_Status("Init World");
    fgWorld->Open();
    GLoadingScreen_Status("Init AI Elements");
    RRenderHigh::InitTrackRenderPostSim();
    UGroup *map = fgWorld->group->GroupLocateTag(kTagMap);
    AIElementController_Construct(map->DataLocateTag(kTagAIElements));
    GLoadingScreen_Status("Init AI Controllers");
    AIVehicleController_Init();
    AIZoneController_Init();
    GLoadingScreen_Status("Init Roads");
    WRoadNetwork::Init();

    GLoadingScreen_Status("Init Simulation");
    const char *carType = SMissionManager_GetCarType(glbMissionManager, 0);
    float orientation[4] = { 0.0f, 0.0f, 1.0f, 0.0f };   // the fourth word is never written
    float position[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float height = 0.0f;
    WCollisionMgr_GetWorldHeightAtPoint(CollisionManager, 0, position, &height, false);
    position[1] = height + 1.0f;

    GLoadingScreen_Status("Preload common models");
    AttributeSet carAttributes;
    PVehicle_GetNamedAttribs(&carAttributes, carType);
    const char *model = PVehicle_RenderNameAttrib(&carAttributes);
    if (model == NULL)
        AssertMessage("Could not find attribute rendername for %s", carType);
    RSceneObj::PreLoad(kCarModels, model, carType);
    const char *secondaryType = carAttributes.LookupString("SECONDARY_TYPE", NULL);
    if (secondaryType != NULL) {
        AttributeSet secondaryAttributes;
        secondaryAttributes.Construct("pvehicle", secondaryType);
        const char *secondaryModel = PVehicle_RenderNameAttrib(&secondaryAttributes);
        if (secondaryModel != NULL)
            RSceneObj::PreLoad(kCarModels, secondaryModel, secondaryType);
        secondaryAttributes.Destruct();
    }

    GLoadingScreen_Status("Spawn player car");
    Simulation_SpawnCarObject(Sim, 0, 0, carType, 0, orientation, position);
    (*playerPhysicsObject)->SetHitPointLoc(&glbMissionManager->playerHealth);
    AIRoadSpawn_Init();
    AIVehicleController_Get();
    TrafficSeed = (int16_t)trafficSeed;
    AIVehicleController_CreateSimTrafficCars(AIVehicleController_Get(), 0, true);
    AIVehicleController_RegisterSimCarList(AIVehicleController_Get(), 0);
    SWeaponManager_SetPlayerCar(WeaponManager, 0, (*playerPhysicsObject)->renderObject);
    if (ActorDatabase != NULL) {
        GLoadingScreen_Status("Init AI Characters");
        AICharacter_Init();
    }
    RCameraIniLoader::LoadFileThunk();
    GLoadingScreen_Status("Leaving StartUp");
    IOModule::GetIOModule()->EnableUpdating(true);
    carAttributes.Destruct();
    if (speechName.capacity >= 16 && speechName.text.pointer != NULL)
        UMemory::FastFree(speechName.text.pointer, speechName.capacity + 1);
}

// FUNC_AT(0x0005b820)
void GameLoop_MainGameLoop(bool unused0, bool unused1, bool simulateOncePerLoop, int unused3, int mission,
                           int trafficSeed) {
    (void)unused0;
    (void)unused1;
    CurrentMission = mission;
    WWorld::SetTrackName(missionNames[mission], false);
    GameLoop_StartUp(trafficSeed);
    RunTheGame(simulateOncePerLoop, unused3);
    GameLoop_CleanUp();
    UFileLoader::StopUsingBigFile();   // the original pushes "..\\bigprocess", unread
}

// ---- the disc error screen

// FUNC_AT(0x0005c7b0)
void DiscError_Shutdown() {
    if (DiscErrorFont != NULL)
        FONT_destroy(DiscErrorFont);
    if (DiscErrorMade != NULL)
        OperatorDelete(DiscErrorMade);
    DiscErrorMade = NULL;
}

// "There's a problem with the disc..." in up to three lines, centred on a black screen.
// FUNC_AT(0x0005c7e0)
void DiscError_Draw() {
    Draw::DrawBox(0.0f, 0.0f, (float)fgRenderer->screenWidth, (float)fgRenderer->screenHeight, 0xff000000);
    const char *const *lines;
    switch (Launch.language) {
    case kFrench: lines = DiscErrorMessages[1]; break;
    case kGerman: lines = DiscErrorMessages[2]; break;
    case kSpanish: lines = DiscErrorMessages[3]; break;
    case kItalian: lines = DiscErrorMessages[4]; break;
    case kDutch: lines = DiscErrorMessages[6]; break;
    case kSwedish: lines = DiscErrorMessages[5]; break;
    default: lines = DiscErrorMessages[0]; break;
    }
    int count = lines[0] != NULL ? 1 : 0;
    if (lines[1] != NULL)
        count++;
    if (lines[2] != NULL)
        count++;
    const uint32_t textHeight = uint32_t(count * 30);   // converted unsigned, as the original
    float y = float(double(fgRenderer->screenHeight) * 0.5f - double(textHeight) * 0.5f);
    U32_AT(DiscErrorFont + 0x20) = 0xffffffff;          // the font's colour: white
    for (int i = 0; i < 3; i++) {
        if (lines[i] == NULL)
            continue;
        float width;
        FONT_getrectx_thunk(DiscErrorFont, (const uint8_t *)lines[i], NULL, NULL, &width, NULL);
        float x = float(double(fgRenderer->screenWidth) * 0.5f - double(width) * 0.5f);
        FONT_drawtexta(DiscErrorFont, x, y, (const uint8_t *)lines[i]);
        y = y + 30.0f;
    }
}

// FILEDEV's read-error handler: a frame of the message. Answers 1.
// FUNC_AT(0x0005c960)
char DiscError_Handler() {
    fgRenderer->renderContext->BeginFrame();
    fgRenderer->viewPort->BeginView();
    DiscError_Draw();
    fgRenderer->viewPort->EndView();
    fgRenderer->renderContext->EndFrame();
    return 1;
}

// FUNC_AT(0x0005c9a0)
void DiscError_Init() {
    if (DiscErrorMade != NULL)
        return;
    DiscErrorMade = OperatorNew(1);
    FILEDEV_setreaderror(DiscError_Handler);
    FONT_installdriver(EAGLdriver);
    FONT_init();
    DiscErrorFont = FONT_create(DebugFontData);
}
