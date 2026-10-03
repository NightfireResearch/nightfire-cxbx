#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------
// The driving engine's top: main, the system start-up and clean-up around the game loop (Bond_*), the loop
// itself (GameLoop_*, RunTheGame), and the launch data page the action engine hands over. The order of every call
// is the original's - the lockstep runs compare frames across the whole start-up.
//
//     main
//       ApplicationMemoryHeapConfig   the launch page, video mode, timer, memory
//       MissionNumToString            the page's mission number to a track name
//       (the command line, on a first boot only)
//       Bond_StartUpSystem            singletons, render library, files, threads, loading screen, attribute
//                                     system, track file, controllers, noise
//       GameLoop_MainGameLoop
//         GameLoop_StartUp            clock, events, scheduler, render, sound, actors, world, AI, the player's car
//         RunTheGame                  the tasks, the intro movie, the loop until the simulation ends, the outro
//         GameLoop_CleanUp            everything StartUp made, in reverse
//       Bond_CleanUp
//       ReturnToAction                relaunch: the next part of the mission, or back to the action engine
// ---------------------------------------------------------------------------------------------------------------

// The warning beside a provisional port: code no shipped data reaches, ported from the listing without a test.
inline void CoreUntested(const char *what) {
    printf("[core] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define CORE_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            CoreUntested(what); \
        } \
    } while (0)

// The launch data page (Ghidra: uberBondScoreBuffer, 0xa4c bytes at 0x00243b90): what the action engine hands the
// driving engine through XLaunchNewImage - its profile (volumes, options, progress) and the hand-over block - and
// what the driving engine hands back (src/common/launchInfo.cpp, src/driving/platform/LaunchOptions.cpp). Only
// the fields the game loop uses are named.
struct LaunchPage {
    uint8_t unknown000[0x4b4];
    char loaderReady[12];       // +0x4b4 "LOADER READY": set, this image has run before (a relaunch between parts)
    uint8_t unknown4c0[0x34];
    char missionName[16];       // +0x4f4 the track to load (MissionName)
    uint8_t unknown504[0x3c];
    char localeFile[12];        // +0x540 the locale's string file, "IDL_ENG.LOC" and so on
    uint8_t unknown54c[0x38];
    int32_t dolbyAudio;         // +0x584 non-zero: gDAudio
    uint8_t unknown588[0x50];
    int32_t unknown5d8;         // +0x5d8 non-zero lets the outro movie play after a failed mission
    int32_t driveCount;         // +0x5dc counted up by every start of the driving engine
    uint8_t unknown5e0[0x34];
    int32_t unknown614;         // +0x614 2 when unknown970 is set, else 0
    int32_t bootCount;          // +0x618 counted up by every start of main; 1 on the first
    uint8_t unknown61c[0x8];
    int32_t savedDifficulty;    // +0x624 the difficulty as the action engine wrote it (1-4)
    uint8_t unknown628[0x338];
    uint32_t handOver;          // +0x960 non-zero: the action engine sent us here, the fields below are valid
    int32_t effectsVolume;      // +0x964 0-100
    int32_t musicVolume;        // +0x968 0-100
    uint8_t unknown96c[0x4];
    int32_t unknown970;         // +0x970
    int32_t missionNum;         // +0x974 1-8, see MissionNumToString
    int32_t unknown978;         // +0x978 the mission's end state as main leaves it (2 after a chained part)
    int32_t returnTarget;       // +0x97c the image ReturnToAction launched: 0 DEFAULT.XBE, 1 DRIVING.XBE
    uint8_t unknown980[0x4];
    int32_t difficulty;         // +0x984 1-4 from the action engine, 0-2 once MissionNumToString has folded it
    int32_t language;           // +0x988 0 Brazilian, 1 French, 2 German, 3 Spanish, 4 Italian, 5 Dutch, 8 Swedish
    uint8_t unknown98c[0x4];
    int32_t controllerPort;     // +0x990 the player's controller: the movies' skip pad, IFeedback's port
    uint8_t unknown994[0xc];
    uint32_t flags;             // +0x9a0
    uint8_t unknown9a4[0x74];
    int32_t unknowna18;         // +0xa18
    int32_t videoMode;          // +0xa1c 0 NTSC, 2 PAL, 3 PAL60, 5 (PAL as well)
    int32_t dvdSource;          // +0xa20 gDVD on a relaunch
    uint8_t unknowna24[0x4];
    int32_t audioMode;          // +0xa28
    int32_t subtitles;          // +0xa2c non-zero: subtitles on
    uint8_t unknowna30[0x1c];

    // Copies `text` into loaderReady (0x00059900).
    void SetLoaderReady(const char *text);
    // Reads the page, once (0x001306d0): zeroed, then XGetLaunchInfo's. A debugger command line (type 3) is
    // copied to `commandLine` instead and the page zeroed again.
    void GetLaunchInfo(char *commandLine);
    // Relaunches: DRIVING.XBE (next part of the mission) or DEFAULT.XBE (the action engine), with the page
    // (0x00130740). Only when the loader is present.
    void ReturnToAction(int target);
};
static_assert(sizeof(LaunchPage) == 0xa4c, "the launch page is 0xa4c bytes");
static_assert(offsetof(LaunchPage, missionName) == 0x4f4 && offsetof(LaunchPage, localeFile) == 0x540 &&
                  offsetof(LaunchPage, bootCount) == 0x618 && offsetof(LaunchPage, handOver) == 0x960 &&
                  offsetof(LaunchPage, missionNum) == 0x974 && offsetof(LaunchPage, language) == 0x988 &&
                  offsetof(LaunchPage, flags) == 0x9a0 && offsetof(LaunchPage, subtitles) == 0xa2c,
              "launch page offsets");

// An MSVC 7 std::string (0x1c bytes): the small-string buffer holds up to 15 characters, longer ones live in a
// block from the pools. Constructed and destroyed inline by its users.
struct GameString {
    uint8_t allocator;          // +0x00 std::allocator, empty
    uint8_t _pad01[3];
    union {
        char buf[16];           // +0x04 capacity < 16
        char *ptr;              // +0x04 capacity >= 16
    };
    uint32_t size;              // +0x14
    uint32_t capacity;          // +0x18

    const char *Text() const { return capacity < 16 ? buf : ptr; }
    // assign(text) (0x0005b0a0).
    GameString* Assign(const char *text);
};
static_assert(sizeof(GameString) == 0x1c, "std::string is 0x1c bytes");

// The STL's length_error throw, as the game's _Xlen functions build it: std::string message, logic_error's
// constructor, length_error's vtable, _CxxThrowException. Not reached by anything the game does.
void ThrowLengthError(const char *message);

// The USingletonManager instance (a function-local static at 0x001e47c0, 16 bytes): a vector of the singletons
// it kills at clean-up.
struct SingletonRegistry {
    uint32_t unknown00;
    void **first;               // +0x04
    void **last;                // +0x08
    void **end;                 // +0x0c

    // The static's destructor, registered with atexit (0x00059c30): kills them all, frees the vector.
    void Destruct();
    // The vector's _Xlen (0x00059ca0).
    static void Xlen();
};
static_assert(sizeof(SingletonRegistry) == 0x10, "the singleton manager is 16 bytes");

// The singleton manager, made on first use (0x00059d20).
SingletonRegistry* SingletonManager();

// A key/value line in a tuning file (Ghidra: OptionParser, 8 bytes): where the value after `key` starts in the
// text and how long it is, or null.
struct OptionParser {
    const char *value;          // +0x00
    int length;                 // +0x04

    // Finds `key` at the start of a line in `text` (0x0005b870): the value skips ' ', '=', '#', '(' and '[', runs to
    // the end of the line, and loses one trailing ']' and then one trailing ')'.
    OptionParser* Construct(const char *text, const char *key);
    // The value into `out`, terminated (0x0005b9c0). False if the key was not found.
    bool GetFullString(char *out);
};
static_assert(sizeof(OptionParser) == 8, "OptionParser is 8 bytes");

// Mission number to track name (0x000596a0), if the action engine handed over.
void MissionNumToString();
// The launch page, video mode, timer and memory, once (0x00059920). Answers true.
bool ApplicationMemoryHeapConfig();
// The loading screen for the mission (0x000599f0).
void Bond_LoadingScreen();
// Asks for a relaunch into another driving mission when this one ends (0x00059bf0): won with a chain to the next
// part, or a cheat.
void Bond_SetExitToDriving(int mission, const char *missionName, bool clearScore);
void Bond_StartUpSystem();    // 0x00059d80
void Bond_CleanUp();          // 0x0005a110
// The game's main (0x0005a1b0), called by preMain (src/driving/main.cpp).
int GameMain(int argc, char **argv);
// The level's big file, data\driving\<name>.viv, the name after the last '_' (0x0005a650).
void GameLoop_StartUsingMainBigFile();
// An intro or outro movie, with subtitles if `subtitles` is not null (the core of FUN_0005a6b0).
void GameLoop_PlayMovie(const char *movie, const char *subtitles);
void FUN_0005a6b0(const char *movie);   // 0x0005a6b0, the original's entry: the subtitles come in EBX
void GameLoop_CleanUp();      // 0x0005a870
void RunTheGame(bool simulateOncePerLoop, int unused);    // 0x0005aa80
void GameLoop_StartUp(int trafficSeed);                    // 0x0005b0d0
void GameLoop_MainGameLoop(bool unused0, bool unused1, bool simulateOncePerLoop, int unused3, int mission,
                           int trafficSeed);              // 0x0005b820

// The disc error screen (docs: FILEDEV's read-error handler): a font, and the handler that draws the message.
void DiscError_Shutdown();    // 0x0005c7b0
void DiscError_Draw();        // 0x0005c7e0
char DiscError_Handler();     // 0x0005c960
void DiscError_Init();        // 0x0005c9a0
