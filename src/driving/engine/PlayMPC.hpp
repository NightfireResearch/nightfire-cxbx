#pragma once

#include <stdint.h>

#include "../render/RenderState.hpp"

// Draws the subtitles for a frame of the movie (the caller's; it is handed the frame number)
typedef void (*SubtitleCallback)(int frame);

// EA's movie player object (Ghidra: PlayMPC; 0x20 bytes, on its caller's stack). Its two callers - the level intro
// and outro runner (FUN_0005a6b0, called from RunTheGame) and ESetVideo's destructor (a movie a mission event plays) -
// construct it, Init it with the file, Play it (one blocking call that runs the whole movie) and destroy it. Init and
// Play are ours (PlayMPC.cpp, on FFmpeg); the constructor and destructor stay original, as our Init never creates the
// EA player object the destructor would free. docs/driving-fmv.md maps the original.
class PlayMPC {
public:
    void *device;                   // +0x00 the EAGL device
    RenderContext *context;         // +0x04 the render context drawn into
    int16_t unknown08;              // +0x08 cleared by the constructor and by Play
    uint8_t _pad0a[2];
    int32_t frameShown;             // +0x0c the frame number handed to the subtitle callback
    uint8_t skipped;                // +0x10 the player pressed A, B or Start
    uint8_t _pad11[3];
    void *player;                   // +0x14 EA's RCMP::AV_PLAYER; always null with our Init
    int32_t padPort;                // +0x18 the pad whose buttons skip the movie
    int32_t volume;                 // +0x1c 0-127

    // Opens the movie at `path` (the locale's directory, e.g. "pal\eng\paris_intro.mad") at `volume` 0-127. The
    // last argument picked between two EA player modes, which ours does not have (0x001307d0).
    void Init(const char *path, uint32_t volume, bool mode);

    // Plays it to the end, or until A, B or Start is pressed on the pad at +0x18, drawing into `viewport`. With
    // `widescreen` the 4:3 picture is stretched to fill a 16:9 screen as the original does. `subtitles`, if not null,
    // is called with the frame number once per frame drawn, between the picture and the end of the frame (0x001308a0).
    void Play(ViewPort *viewport, bool widescreen, SubtitleCallback subtitles);
};
static_assert(sizeof(PlayMPC) == 0x20, "PlayMPC is 0x20 bytes");

// EA's task and thread services, which the original's wait loop keeps running while it waits for the next frame
// (SYNCTASK_run, THREAD_yield: ours now, platform/RealSystem.cpp)
#include "../platform/RealSystem.h"
