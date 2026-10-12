#ifndef DRIVING_GAME_LOADINGSCREEN_H_
#define DRIVING_GAME_LOADINGSCREEN_H_

// ---------------------------------------------------------------------------------------------------------------
// GLoadingScreen (statics only): the screens shown while the driving engine starts and stops. Bond_StartUpSystem
// makes the six progress dots (a SHPX kept in the XBE's data) and adds Update as a sync task, which draws a frame
// every timer tick from then on - the dots filling and emptying, and once Bond_LoadingScreen has loaded the
// mission's picture and layout file, the picture and the "loading" text - until RunTheGame takes the task away and
// Shutdown clears the screen. GameLoop_CleanUp draws the unloading screen (two frames) and frees it. Status writes
// a line to the log. See LoadingScreen.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../helpers.h"

struct DAFI;

// loadState: what Update draws
enum LoadScreenState : int32_t {
    kLoadScreenOff = 0,
    kLoadScreenDots = 1,                // InitLoadDots's: the dots
    kLoadScreenPicture = 2,             // InitLoadScreen's: the dots, the picture and the text
};

// The layout file's reader (4 bytes, from operator new; the name ours)
struct LoadScreenLayout {
    DAFI *dafi;
};

class GLoadingScreen {
public:
    // The dots' textures and the screen's frame; `keep` set leaves the screen up at Shutdown
    static void InitLoadDots(bool keep);                                                        // 0x000e1f90
    // The picture (SHPX `picture`) and the "loading" font, and the layout file's ids (`layout`, an ini file:
    // TITLE_ID, DESC_ID, DESC_NUMLINES, HINT_ID, and HINT_NUMLINES ids HINT_ID1.., of which one is taken by the
    // timer)
    static void InitLoadScreen(const char *picture, const char *layout);                        // 0x000e2390
    static void DrawBackground();                                                               // 0x000e2610
    static void DrawDots();                                                                     // 0x000e2710
    static void ClearToBlack();                                                                 // 0x000e2830
    // A frame of the loading screen
    static void Update();                                                                       // 0x000e28a0
    static void Shutdown();                                                                     // 0x000e2a70
    // Update, as a sync task; answers 0
    static unsigned LoadScreenSystemTaskCallback(int argument, int ticksLate);                  // 0x000e2b60
    static void AddLoadingScreenSyncTask();                                                     // 0x000e2b70
    static void DeleteLoadingScreenSyncTask();                                                  // 0x000e2b90
    // The unloading picture and the error font, and two frames of the unloading screen
    static void LoadAndDrawUnLoadingScreen();                                                   // 0x000e2110
    static void FreeUnloadingScreen();                                                          // 0x000e2ba0
    // A line in the log: the timer as hours, minutes and tenths, the memory free, and the message
    static void Status(const char *format, ...);                                                // 0x000e2ff0
};

// The screens' state (LoadingScreen.cpp)
struct FNTXFont;
struct ShapeFile;
namespace EAGL {
struct TAR;
}
extern bool KeepLoadScreen;
extern float DotDirection, LoadScale, DotTime;
extern ShapeFile *BackgroundFile, *Dots;
extern FNTXFont *LoadingFont, *ErrorFont;
extern void *LoadingFontFile, *UnloadingFile, *ErrorFontFile;
extern int32_t TitleId, DescId, DescLines, HintId, LastTick, LitDots, DotCount, BackgroundCount;
constexpr int kMaxDotTextures = 16;
constexpr int kMaxBackgroundTextures = 128;
extern EAGL::TAR *DotTextures[kMaxDotTextures], *BackgroundTextures[kMaxBackgroundTextures], *UnloadingTexture;

// The loading screen's state, a LoadScreenState: GHud::PauseOn reads it too
extern int32_t LoadState;

// The colours the static initialisers convert (StaticInitTable.cpp; names ours)
extern uint32_t LoadingTextColour;
extern uint32_t UnloadingTextColour;
extern uint32_t LoadingColour;          // the picture, the lit dots, the unloading picture
extern uint32_t LoadingDimDotColour;

// ---- the list beside the loading screen's globals: a std::list<std::string> at 0x0023f800, which only the
// static initialisers make and destroy (StaticInitTable.cpp). Its compiled helpers (the names ours).

// A std::string as the game's compiler laid it out (0x1c bytes)
struct LoadScreenString {
    uint32_t allocator;
    union {
        char buffer[16];
        char *pointer;
    };
    uint32_t size;
    uint32_t capacity;                  // 15 while the text fits the buffer
};
static_assert(sizeof(LoadScreenString) == 0x1c, "a std::string is 0x1c bytes");

struct LoadScreenStringNode {           // 0x24 bytes ("STL")
    LoadScreenStringNode *next;
    LoadScreenStringNode *previous;
    LoadScreenString value;

    // The string's memory given back (the node's, as the original passes it)
    void Tidy();                                                                                // 0x000e2c40
};
static_assert(sizeof(LoadScreenStringNode) == 0x24, "a list node is 0x24 bytes");

struct LoadScreenStringList {
    uint32_t allocator;
    LoadScreenStringNode *head;
    uint32_t size;

    // The list's head node, linked to itself (`this` is not read)
    LoadScreenStringNode* NewHead();                                                            // 0x000e2c20
    // erase(first, last): the iterator after the range through `result`
    LoadScreenStringNode** Erase(LoadScreenStringNode **result, LoadScreenStringNode *first,
                                 LoadScreenStringNode *last);                                   // 0x000e2c80
};

#endif // DRIVING_GAME_LOADINGSCREEN_H_
