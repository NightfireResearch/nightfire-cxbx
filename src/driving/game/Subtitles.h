#ifndef DRIVING_GAME_SUBTITLES_H_
#define DRIVING_GAME_SUBTITLES_H_

// ---------------------------------------------------------------------------------------------------------------
// The subtitles (Ghidra: GSubtitles, 0x94 bytes, vtable 0x001a08b8; GSubtitleSplitter, 0x64 bytes;
// 0x000e3180-0x000e3af0): a locale string shown at the bottom of the screen for a time, split into lines shown one
// after another. A mission's subtitles come from DisplaySubtitle and are timed by the simulation's steps; a movie's
// come from a cue file (LoadSubtitles) and are started by the movie's frame (DrawFrame, the movie player's
// callback) and timed by the real clock. See Subtitles.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

struct FNTXFont;

// Splits a text into lines and answers the one due (the field names ours)
class GSubtitleSplitter {
public:
    static constexpr int kMaxLines = 10;
    static constexpr unsigned kLineLength = 52;

    char *lines[kMaxLines];             // +0x00 operator new[]'s
    float times[kMaxLines];             // +0x28 each line's share of the duration, by its length
    unsigned count;                     // +0x50
    unsigned current;                   // +0x54 the line shown
    float duration;                     // +0x58 seconds
    int32_t startTick;                  // +0x5c when the line shown started
    bool simClock;                      // +0x60 timed by the simulation's steps, else by the real clock
    uint8_t unknown61[3];

    // The text cut into lines of at most `width` characters at the last space (anywhere when there is none),
    // the spaces after each skipped, at most ten lines (the last takes what is left, cut at `width`). Answers the
    // number of lines.
    unsigned WordWrap(const char *text, unsigned width, char **lines);                          // 0x000e31a0
    void Init(const char *text, float duration, bool simClock);                                 // 0x000e3390
    // The lines freed (the pointers are left), the time cleared
    void Reset();                                                                               // 0x000e33e0
    // The line due, moving on when the one shown has had its time; NULL when there is no duration
    const char* GetString();                                                                    // 0x000e3430
};
static_assert(sizeof(GSubtitleSplitter) == 0x64, "GSubtitleSplitter is 0x64 bytes");
static_assert(offsetof(GSubtitleSplitter, times) == 0x28 && offsetof(GSubtitleSplitter, count) == 0x50 &&
              offsetof(GSubtitleSplitter, startTick) == 0x5c && offsetof(GSubtitleSplitter, simClock) == 0x60,
              "GSubtitleSplitter layout");

// A movie's subtitle: the frame it starts at and its locale symbol (the names ours)
struct SubtitleCue {
    uint32_t frame;
    int32_t symbol;
};

class GSubtitles {
public:
    const void *vtable;                 // +0x00 0x001a08b8: the scalar deleting destructor
    bool simClock;                      // +0x04 timed by the simulation's steps (DisplaySubtitle's), else by the
                                        //       real clock (a movie's)
    uint8_t unknown05[3];
    float duration;                     // +0x08 seconds
    int32_t startTick;                  // +0x0c
    FNTXFont *font;                     // +0x10 "data\loading\loading.xfn"
    void *fontFile;                     // +0x14
    float screenWidth;                  // +0x18 the renderer's, at construction
    float screenHeight;                 // +0x1c
    SubtitleCue *cues;                  // +0x20 operator new[]'s; the first is frame 0, symbol 0
    unsigned nextCue;                   // +0x24
    unsigned cueCount;                  // +0x28
    int32_t symbol;                     // +0x2c the locale symbol shown, 0 for none
    GSubtitleSplitter splitter;         // +0x30

    // The one made last, NULL once it is destroyed
    static GSubtitles* TheApp();                                                                // 0x000e3180
    GSubtitles* Construct();                                                                    // 0x000e34e0
    void Destruct();                                                                            // 0x000e3730
    GSubtitles* Delete(unsigned flags);     // the scalar deleting destructor, vtable slot 0    // 0x000e3ad0

    bool IsSubtitleDisplaying();                                                                // 0x000e3190
    // A movie's cues: a line with their count, then a line each, its first '='-separated field read as the frame
    // from its seventh character, its second as the symbol (frames scaled by 5/6 in PAL 50)
    void LoadSubtitles(const char *file);                                                       // 0x000e3840
    void ClearSubtitles();                                                                      // 0x000e3630
    // The next cue started once the movie reaches its frame, lasting until the cue after it
    void SetupFMVSubtitles(unsigned frame);                                                     // 0x000e3560
    // The symbol's string for `duration` seconds of the simulation's steps (nothing while a cinematic is skipped)
    void DisplaySubtitle(int symbol, float duration);                                           // 0x000e3790
    // The line due, centred at the bottom of the screen; the subtitle cleared once its time is up
    void DrawSubtitles();                                                                       // 0x000e39b0
    // The movie player's callback (Ghidra counts it as DrawSubtitles' tail; the name ours)
    static void DrawFrame(int frame);                                                           // 0x000e3ab0
    // The text (as a format) at x, y
    void DrawFont(const char *text, float x, float y, float scale, uint32_t colour);            // 0x000e3670
    void GetWidthHeight(const char *text, float scale, float *width, float *height);            // 0x000e36f0
};
static_assert(sizeof(GSubtitles) == 0x94, "GSubtitles is 0x94 bytes");
static_assert(offsetof(GSubtitles, font) == 0x10 && offsetof(GSubtitles, cues) == 0x20 &&
              offsetof(GSubtitles, symbol) == 0x2c && offsetof(GSubtitles, splitter) == 0x30,
              "GSubtitles layout");

extern GSubtitles *TheSubtitles;        // TheApp's

#endif // DRIVING_GAME_SUBTITLES_H_
