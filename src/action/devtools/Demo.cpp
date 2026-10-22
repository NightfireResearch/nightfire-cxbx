#include "Demo.h"
#ifndef NIGHTFIRE_STANDALONE
#include "DataWrites.h"
#include "MemTrace.h"
#include "AllOriginal.h"
#include "../../common/xbeOriginal.h"
#endif

#include "../actionhelpers.h"
#include "../game.h"
#include "../game/obj/object.h"
#include "../engine/viewer.h"
#include "../game/sp/SwitchChannels.h"
#include "../game/obj/control.h"
#include "../util/Random.h"
#include "../engine/XboxSettings.h"
#include "../engine/LaunchOptions.h"
#include "../../common/renderWindow.h"
#include "../../common/fmv/FmvAudio.h"
#include "../../common/gfx/d3d9Backend.h"

#include <windows.h>
#include <direct.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------------------------------------------
// Time demos (docs/action-timedemo.md).
//
// A recording is the files the session started with, then a stream of what the host told the game: every poll of
// every controller port, the mouse once a frame, and at the end of every frame a checksum of the game's state.
// A playback starts from the same files in a sandbox, hands the game the recorded input in place of the host's,
// and compares the checksums as it goes.
//
// For that to mean anything, everything else the game can see of the host has to be the same both times, so in
// either mode:
//  - the clock (timestamp(), the XAPI performance counter, KeQuerySystemTime) is a function of the frame count,
//    moved on a microsecond by every read so that a loop waiting on it still finishes;
//  - overlapped disc reads finish before they return (XboxFile.cpp);
//  - the movie player's clock (FmvAudio) follows that clock too;
//  - what the sound device says of each buffer - playing or not, where its play cursor is - is recorded, and a
//    playback gives the game the recorded answers, so the sound is the game's real sound while recording;
//  - file times from a directory enumeration read zero;
//  - the debug teleport keys do nothing.
// The sound still plays and the picture is still drawn and paced as usual, so a recording plays like the game.
//
// The file: "NFDEMO" 0 2, sections of [four-character tag][u32 length][data] (INFO, ARGS - the launch options -
// and a FILE per starting file) up to a STRM section, then the stream to the end of the file. Stream events are one tag byte and a fixed payload:
//   0x10+port  a poll of that port, the same as its last           0x20+port  a poll, changed: 14 bytes
//   0x30       the mouse, the same as last frame                    0x31       the mouse, changed: 9 bytes
//   0x40       end of a frame: five u32 checksums                   0x50       a bookmark (F6), u32 frame
//   0x60       the session left for the driving engine
//   0x70       a sound answer: buffer, query, u32 answer            0x71       a sound answer the same as that
//                                                                              buffer and query's last: buffer, query
// ---------------------------------------------------------------------------------------------------------------

enum DemoMode { DEMO_OFF, DEMO_RECORD, DEMO_PLAY, DEMO_REBASE };

enum {
    EV_PAD_SAME = 0x10,
    EV_PAD = 0x20,
    EV_MOUSE_SAME = 0x30,
    EV_MOUSE = 0x31,
    EV_FRAME = 0x40,
    EV_BOOKMARK = 0x50,
    EV_DRIVING = 0x60,
    EV_SOUND = 0x70,
    EV_SOUND_SAME = 0x71,
};

enum { SUM_RNG, SUM_GAMESTATE, SUM_CHANNELS, SUM_OBJECTS, SUM_PLAYERS, SUM_COUNT };
static const char *const kSumNames[SUM_COUNT] = { "rng", "gamestate", "channels", "objects", "players" };

#define PAD_BYTES 14
#define MOUSE_BYTES 9
#define SOUND_BUFFERS 256

#ifndef NIGHTFIRE_STANDALONE
// KeQuerySystemTime's slot in the XBE's kernel import table (the originals')
#define KernelKeQuerySystemTime (*(void(__stdcall **)(LARGE_INTEGER *))0x0015d198)
#endif

#define VK_F6_ 0x75

static DemoMode g_mode = DEMO_OFF;
static std::string g_path;

// The clock
static double g_frameMs = 1000.0;       // the clock at the start of this frame
static uint32_t g_readsThisFrame = 0;
static uint32_t g_frame = 0;            // frames since the demo began
static int64_t g_systemTimeBase;        // KeQuerySystemTime at the start, in 100 ns units

// Recording
static FILE *g_out = NULL;

// Playback
static std::vector<uint8_t> g_stream;
static size_t g_at = 0;
static bool g_streamBroken = false;
static uint32_t g_divergedFrames = 0;
// From Mem_Init to control_init_object_lists the object list and the players point into the wiped heap. A level load
// does both in one frame; a restart does not, and the game walks neither meanwhile: the checksums count them as empty
static bool g_staleObjects = false;
static uint32_t g_firstDivergence = 0;
static uint32_t g_bookmarks = 0;
static bool g_stopOnDivergence = false;
static uint32_t g_untilFrame = 0;   // NIGHTFIRE_DEMO_UNTIL: a playback ends here, for bisecting a difference
static std::vector<uint32_t> g_dumpFrames;
static uint32_t g_shotEvery = 0;                   // NIGHTFIRE_DEMO_SHOT_EVERY
static uint32_t g_shotFrom = 0;                    // NIGHTFIRE_DEMO_SHOT_FROM
static int g_presentEvery = 0;
static uint32_t g_dumpStateFrame = 0xffffffffu;   // NIGHTFIRE_DEMO_DUMP_GAMESTATE
static uint32_t g_dumpObjectsFrame = 0xffffffffu; // NIGHTFIRE_DEMO_DUMP_OBJECTS
static int32_t g_stampOffset = 0;                  // see StampOffsetMatches

// The last state per port and of the mouse, both modes
static uint8_t g_lastPad[4][PAD_BYTES];
static bool g_havePad[4];
static uint32_t g_padPacket[4];
static uint8_t g_lastMouse[MOUSE_BYTES];
static bool g_haveMouse = false;
static uint32_t g_lastSound[SOUND_BUFFERS + 1][2];   // per buffer (and "none") and query
static bool g_haveSound[SOUND_BUFFERS + 1][2];

// Rebasing (NIGHTFIRE_DEMO_REBASE): the old recording's input, frame by frame, and where each frame's polls are up to
struct RebaseFrame {
    std::vector<std::vector<uint8_t>> pads[4];   // each poll of each port, PAD_BYTES each
    std::vector<std::vector<uint8_t>> mice;      // MOUSE_BYTES each
    std::vector<uint32_t> bookmarks;
    uint32_t sums[SUM_COUNT];                    // the old recording's checksums at the frame's end
};
static std::vector<RebaseFrame> g_rebase;
static std::vector<uint8_t> g_header;            // the old recording's sections, written again as they were
static size_t g_rebasePadAt[4], g_rebaseMouseAt;
static uint8_t g_rebaseLastPad[4][PAD_BYTES];
static uint8_t g_rebaseLastMouse[MOUSE_BYTES];

// ---------------------------------------------------------------------------------------------------------------
// Small helpers

static void Put(const void *data, size_t size) {
    fwrite(data, 1, size, g_out);
}

static void PutByte(uint8_t value) {
    fputc(value, g_out);
}

static void PutU32(uint32_t value) {
    Put(&value, 4);
}

static bool Take(void *out, size_t size) {
    if (g_at + size > g_stream.size())
        return false;
    memcpy(out, &g_stream[g_at], size);
    g_at += size;
    return true;
}

static int PeekByte(void) {
    return g_at < g_stream.size() ? g_stream[g_at] : -1;
}

static uint32_t Fnv(uint32_t hash, const void *data, size_t size) {
    const uint8_t *p = (const uint8_t *)data;
    for (size_t i = 0; i < size; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

static const uint32_t kFnvBasis = 2166136261u;

static bool ReadWholeFile(const char *path, std::vector<uint8_t> *out) {
    FILE *file = fopen(path, "rb");
    if (file == NULL)
        return false;
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    out->resize(size > 0 ? (size_t)size : 0);
    size_t read = size > 0 ? fread(out->data(), 1, (size_t)size, file) : 0;
    fclose(file);
    return read == out->size();
}

static bool GameWindowHasFocus(void) {
    HWND render = FindWindowA(NIGHTFIRE_RENDER_WINDOW_CLASS, NULL);
    if (render == NULL)
        return false;
    HWND foreground = GetForegroundWindow();
    return foreground != NULL && (foreground == render || foreground == GetAncestor(render, GA_ROOT));
}

// ---------------------------------------------------------------------------------------------------------------
// The files a session starts with: settings.ini and psiLaunch.bin beside the executable's working directory, and
// everything under the writable drives (saves = U:, tdata = T:, cache = Z:)

static const char *const kStartFiles[] = { "settings.ini", "psiLaunch.bin" };
static const char *const kStartDirectories[] = { "saves", "tdata", "cache" };

static void WriteSection(const char tag[4], const void *data, uint32_t size) {
    Put(tag, 4);
    PutU32(size);
    Put(data, size);
}

static void WriteFileSection(const char *path) {
    std::vector<uint8_t> data;
    if (!ReadWholeFile(path, &data))
        return;
    uint16_t pathLength = (uint16_t)strlen(path);
    uint32_t size = 2 + pathLength + (uint32_t)data.size();
    Put("FILE", 4);
    PutU32(size);
    Put(&pathLength, 2);
    Put(path, pathLength);
    Put(data.data(), data.size());
    printf("[demo]   %s (%u bytes)\n", path, (unsigned)data.size());
}

static void WriteDirectorySections(const std::string &directory) {
    WIN32_FIND_DATAA find;
    HANDLE handle = FindFirstFileA((directory + "\\*").c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    do {
        if (strcmp(find.cFileName, ".") == 0 || strcmp(find.cFileName, "..") == 0)
            continue;
        std::string path = directory + "\\" + find.cFileName;
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
            WriteDirectorySections(path);
        else
            WriteFileSection(path.c_str());
    } while (FindNextFileA(handle, &find));
    FindClose(handle);
}

// Removes everything under a directory of the playback sandbox, which this file made
static void EmptyDirectory(const std::string &directory) {
    WIN32_FIND_DATAA find;
    HANDLE handle = FindFirstFileA((directory + "\\*").c_str(), &find);
    if (handle == INVALID_HANDLE_VALUE)
        return;
    do {
        if (strcmp(find.cFileName, ".") == 0 || strcmp(find.cFileName, "..") == 0)
            continue;
        std::string path = directory + "\\" + find.cFileName;
        if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            EmptyDirectory(path);
            RemoveDirectoryA(path.c_str());
        } else {
            DeleteFileA(path.c_str());
        }
    } while (FindNextFileA(handle, &find));
    FindClose(handle);
}

static void MakeParentDirectories(const std::string &path) {
    for (size_t i = 0; i < path.size(); i++) {
        if (path[i] == '\\' || path[i] == '/')
            _mkdir(path.substr(0, i).c_str());
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The clock

#ifndef NIGHTFIRE_STANDALONE
static void __stdcall Demo_KeQuerySystemTime(LARGE_INTEGER *systemTime) {
    if (systemTime != NULL)
        systemTime->QuadPart = g_systemTimeBase + (int64_t)(g_frameMs * 10000.0);
}
#endif

double Demo_TimestampMs(void) {
    return g_frameMs + 0.001 * g_readsThisFrame++;
}

int64_t Demo_Counter(void) {
    return (int64_t)(Demo_TimestampMs() * 10000.0);
}

int64_t Demo_CounterFrequency(void) {
    return 10000000;
}

static double MovieClockSeconds(void) {
    return g_frameMs * 0.001;
}

// ---------------------------------------------------------------------------------------------------------------
// Starting

// At exit: what the last frames recorded, which the regular flush has not written yet
static void CloseRecording(void) {
    if (g_out != NULL) {
        fclose(g_out);
        g_out = NULL;
    }
}

static void StartRecording(const char *path) {
    MakeParentDirectories(path);
    g_out = fopen(path, "wb");
    if (g_out == NULL) {
        printf("[demo] cannot write %s; not recording\n", path);
        return;
    }
    setvbuf(g_out, NULL, _IOFBF, 1 << 16);
    g_path = path;
    g_mode = DEMO_RECORD;

    Put("NFDEMO\0\2", 8);
    char info[256];
    SYSTEMTIME now;
    GetLocalTime(&now);
    int infoLength = snprintf(info, sizeof(info), "recorded %04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth,
                              now.wDay, now.wHour, now.wMinute, now.wSecond);
    WriteSection("INFO", info, (uint32_t)infoLength);
    WriteSection("ARGS", LaunchOptions_Text(), (uint32_t)strlen(LaunchOptions_Text()));
    printf("[demo] recording to %s, starting from:\n", path);
    for (const char *file : kStartFiles)
        WriteFileSection(file);
    for (const char *directory : kStartDirectories)
        WriteDirectorySections(directory);
    WriteSection("STRM", NULL, 0);
    fflush(g_out);
    atexit(CloseRecording);
}

static void PlaybackAtExit(void);

// A recording whose game was stopped from outside ends part-way through a frame: playback goes to the end of the
// last whole one. Returns the length of the stream up to there.
static size_t WholeFrames(const std::vector<uint8_t> &stream) {
    size_t at = 0, whole = 0;
    while (at < stream.size()) {
        uint8_t tag = stream[at++];
        size_t payload;
        if ((tag & 0xf0) == EV_PAD_SAME || tag == EV_MOUSE_SAME)
            payload = 0;
        else if ((tag & 0xf0) == EV_PAD)
            payload = PAD_BYTES;
        else if (tag == EV_MOUSE)
            payload = MOUSE_BYTES;
        else if (tag == EV_FRAME)
            payload = SUM_COUNT * 4;
        else if (tag == EV_BOOKMARK)
            payload = 4;
        else if (tag == EV_DRIVING)
            payload = 0;
        else if (tag == EV_SOUND)
            payload = 2 + 4;
        else if (tag == EV_SOUND_SAME)
            payload = 2;
        else
            break;
        if (at + payload > stream.size())
            break;
        at += payload;
        if (tag == EV_FRAME || tag == EV_DRIVING)
            whole = at;
    }
    return whole;
}

static bool StartPlayback(const char *path) {
    std::vector<uint8_t> file;
    if (!ReadWholeFile(path, &file) || file.size() < 8 || memcmp(file.data(), "NFDEMO\0\2", 8) != 0) {
        printf("[demo] %s is not a demo this build can play\n", path);
        return false;
    }

    // The sandbox: demoplay\<name>, emptied, then filled with the files the recording started with
    std::string name = path;
    size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);
    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    char fullPath[MAX_PATH];
    GetFullPathNameA(path, MAX_PATH, fullPath, NULL);
    std::string sandbox = std::string("demoplay\\") + name;
    _mkdir("demoplay");
    _mkdir(sandbox.c_str());
    EmptyDirectory(sandbox);

    size_t at = 8;
    while (at + 8 <= file.size()) {
        char tag[5] = {};
        uint32_t size;
        memcpy(tag, &file[at], 4);
        memcpy(&size, &file[at + 4], 4);
        at += 8;
        if (strcmp(tag, "STRM") == 0)
            break;
        if (at + size > file.size()) {
            printf("[demo] %s is cut short in its %s section\n", path, tag);
            return false;
        }
        if (strcmp(tag, "ARGS") == 0) {
            LaunchOptions_SetFromText(std::string((const char *)&file[at], size).c_str());
        } else if (strcmp(tag, "INFO") == 0) {
            printf("[demo] playing %s (%.*s)\n", path, (int)size, (const char *)&file[at]);
        } else if (strcmp(tag, "FILE") == 0) {
            uint16_t pathLength;
            memcpy(&pathLength, &file[at], 2);
            std::string relative((const char *)&file[at + 2], pathLength);
            std::string target = sandbox + "\\" + relative;
            MakeParentDirectories(target);
            FILE *out = fopen(target.c_str(), "wb");
            if (out != NULL) {
                fwrite(&file[at + 2 + pathLength], 1, size - 2 - pathLength, out);
                fclose(out);
            }
        }
        at += size;
    }
    g_header.assign(file.begin() + 8, file.begin() + (ptrdiff_t)at - 8);   // up to the STRM section
    g_stream.assign(file.begin() + (ptrdiff_t)at, file.end());
    g_stream.resize(WholeFrames(g_stream));
    g_at = 0;

    if (!SetCurrentDirectoryA(sandbox.c_str())) {
        printf("[demo] cannot enter the sandbox %s\n", sandbox.c_str());
        return false;
    }
    printf("[demo] sandbox %s, %u bytes of input\n", sandbox.c_str(), (unsigned)g_stream.size());
    g_path = fullPath;
    g_mode = DEMO_PLAY;
    atexit(PlaybackAtExit);

    g_stopOnDivergence = getenv("NIGHTFIRE_DEMO_STOP") != NULL;
    if (const char *until = getenv("NIGHTFIRE_DEMO_UNTIL"))
        g_untilFrame = (uint32_t)strtoul(until, NULL, 0);
    const char *fast = getenv("NIGHTFIRE_DEMO_FAST");
    g_presentEvery = fast != NULL ? atoi(fast) : 0;
    const char *dumpState = getenv("NIGHTFIRE_DEMO_DUMP_GAMESTATE");
    if (dumpState != NULL)
        g_dumpStateFrame = (uint32_t)strtoul(dumpState, NULL, 10);
    if (const char *dumpObjects = getenv("NIGHTFIRE_DEMO_DUMP_OBJECTS"))
        g_dumpObjectsFrame = (uint32_t)strtoul(dumpObjects, NULL, 10);
    if (const char *shotEvery = getenv("NIGHTFIRE_DEMO_SHOT_EVERY"))
        g_shotEvery = (uint32_t)strtoul(shotEvery, NULL, 10);
    if (const char *shotFrom = getenv("NIGHTFIRE_DEMO_SHOT_FROM"))
        g_shotFrom = (uint32_t)strtoul(shotFrom, NULL, 10);
    const char *dumps = getenv("NIGHTFIRE_DEMO_DUMP");
    for (const char *p = dumps; p != NULL && *p != '\0';) {
        g_dumpFrames.push_back((uint32_t)strtoul(p, (char **)&p, 10));
        while (*p == ',' || *p == ' ')
            p++;
    }
    return true;
}

// The old recording's input, cut into its frames: each frame's polls of each port and reads of the mouse, in order,
// and its bookmarks. The sound answers are left out: a rebase asks the device again.
static void ReadRebaseFrames(void) {
    uint8_t pad[4][PAD_BYTES] = {}, mouse[MOUSE_BYTES] = {};
    RebaseFrame frame;
    size_t at = 0;
    while (at < g_stream.size()) {
        uint8_t tag = g_stream[at++];
        if ((tag & 0xf0) == EV_PAD_SAME) {
            frame.pads[tag & 3].emplace_back(pad[tag & 3], pad[tag & 3] + PAD_BYTES);
        } else if ((tag & 0xf0) == EV_PAD) {
            memcpy(pad[tag & 3], &g_stream[at], PAD_BYTES);
            at += PAD_BYTES;
            frame.pads[tag & 3].emplace_back(pad[tag & 3], pad[tag & 3] + PAD_BYTES);
        } else if (tag == EV_MOUSE_SAME) {
            frame.mice.emplace_back(mouse, mouse + MOUSE_BYTES);
        } else if (tag == EV_MOUSE) {
            memcpy(mouse, &g_stream[at], MOUSE_BYTES);
            at += MOUSE_BYTES;
            frame.mice.emplace_back(mouse, mouse + MOUSE_BYTES);
        } else if (tag == EV_BOOKMARK) {
            uint32_t mark;
            memcpy(&mark, &g_stream[at], 4);
            at += 4;
            frame.bookmarks.push_back(mark);
        } else if (tag == EV_SOUND) {
            at += 2 + 4;
        } else if (tag == EV_SOUND_SAME) {
            at += 2;
        } else if (tag == EV_FRAME) {
            memcpy(frame.sums, &g_stream[at], SUM_COUNT * 4);
            at += SUM_COUNT * 4;
            g_rebase.push_back(frame);
            frame = RebaseFrame();
        } else {
            break;   // EV_DRIVING: the old session ended here
        }
    }
}

// A playback that writes a new recording instead of comparing: the old recording's input, given to the game as it
// is now, with the device's own sound answers and the game's own checksums. For when a change to the code is meant
// to change the game - a port made exact to the original, say - and the recordings have to follow it.
static void StartRebase(const char *outPath) {
    MakeParentDirectories(outPath);
    g_out = fopen(outPath, "wb");
    if (g_out == NULL) {
        printf("[demo] cannot write %s; not rebasing\n", outPath);
        ExitProcess(4);
    }
    setvbuf(g_out, NULL, _IOFBF, 1 << 16);
    Put("NFDEMO\0\2", 8);
    char info[256];
    SYSTEMTIME now;
    GetLocalTime(&now);
    int infoLength = snprintf(info, sizeof(info), "rebased %04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth,
                              now.wDay, now.wHour, now.wMinute, now.wSecond);
    WriteSection("INFO", info, (uint32_t)infoLength);
    Put(g_header.data(), g_header.size());
    WriteSection("STRM", NULL, 0);
    ReadRebaseFrames();
    g_path = outPath;
    g_mode = DEMO_REBASE;
    // At the game's own pace: the sound answers come from the device, which plays in real time, so frames that went
    // by faster would hear every sound last longer (and whatever waits on one, later)
    g_presentEvery = 0;
    atexit(CloseRecording);
    printf("[demo] rebasing %u frames of input to %s\n", (unsigned)g_rebase.size(), outPath);
    fflush(stdout);
}

void Demo_Init(void) {
    const char *play = getenv("NIGHTFIRE_DEMO_PLAY");
    if (play != NULL && play[0] != '\0') {
        // The rebase's output, before the playback moves into its sandbox
        const char *rebase = getenv("NIGHTFIRE_DEMO_REBASE");
        char rebasePath[MAX_PATH] = "";
        if (rebase != NULL && rebase[0] != '\0')
            GetFullPathNameA(rebase, MAX_PATH, rebasePath, NULL);
        if (!StartPlayback(play))
            ExitProcess(4);
        if (rebasePath[0] != '\0')
            StartRebase(rebasePath);
    } else {
        const char *record = getenv("NIGHTFIRE_DEMO_RECORD");
        if (record != NULL && record[0] != '\0') {
            StartRecording(record);
        } else if (Settings_GetDemoRecord()) {
            SYSTEMTIME now;
            GetLocalTime(&now);
            char path[MAX_PATH];
            snprintf(path, sizeof(path), "demos\\%04u%02u%02u-%02u%02u%02u.nfdemo", now.wYear, now.wMonth, now.wDay,
                     now.wHour, now.wMinute, now.wSecond);
            StartRecording(path);
        }
    }
    if (g_mode == DEMO_OFF)
        return;

    // 18 November 2002, midnight UTC, as KeQuerySystemTime's 100 ns units since 1601
    SYSTEMTIME epoch = { 2002, 11, 1, 18, 0, 0, 0, 0 };
    FILETIME epochTime;
    SystemTimeToFileTime(&epoch, &epochTime);
    g_systemTimeBase = ((int64_t)epochTime.dwHighDateTime << 32) | epochTime.dwLowDateTime;
#ifndef NIGHTFIRE_STANDALONE
    KernelKeQuerySystemTime = Demo_KeQuerySystemTime;
#endif

    FmvAudio::SetVirtualClock(MovieClockSeconds);
}

#ifndef NIGHTFIRE_STANDALONE
void Demo_RestoreOriginals(void) {
    const char *list = getenv("NIGHTFIRE_ORIGINAL");
    if (list == NULL || list[0] == '\0')
        return;
    if (strcmp(list, "game") == 0) {
        AllOriginal_Install();
        return;
    }
    int restored = 0, unpatched = 0;
    for (const char *p = list; *p != '\0';) {
        char *end;
        unsigned address = (unsigned)strtoul(p, &end, 16);
        if (end == p)
            break;
        if (XbeOriginal_Restore(address, true))
            restored++;
        else
            unpatched++;
        p = end;
        while (*p == ',' || *p == ' ')
            p++;
    }
    printf("[demo] NIGHTFIRE_ORIGINAL: %d functions run as the original, %d had no port\n", restored, unpatched);
    fflush(stdout);
}
#endif

bool Demo_Active(void) {
    return g_mode != DEMO_OFF;
}

bool Demo_Playing(void) {
    return g_mode == DEMO_PLAY || g_mode == DEMO_REBASE;
}

int Demo_PresentEvery(void) {
    return g_mode == DEMO_PLAY || g_mode == DEMO_REBASE ? g_presentEvery : 0;
}

// Recording, or rebasing: writing a stream
static bool Writing(void) {
    return g_mode == DEMO_RECORD || g_mode == DEMO_REBASE;
}

// ---------------------------------------------------------------------------------------------------------------
// The end of a playback

static bool g_finished = false;

static void PrintResult(const char *why) {
    g_finished = true;
    printf("[demo] playback %s at frame %u: ", why, g_frame);
    if (g_streamBroken)
        printf("the input no longer lined up with the game (see above)\n");
    else if (g_divergedFrames == 0)
        printf("every frame matched the recording\n");
    else
        printf("%u frames differed from the recording, the first at frame %u\n", g_divergedFrames,
               g_firstDivergence);
    fflush(stdout);
}

static void FinishPlayback(const char *why) {
    PrintResult(why);
    ExitProcess(g_streamBroken ? 3 : g_divergedFrames != 0 ? 2 : 0);
}

// The game quitting by itself (a MenuProbe script's "quit", say) still gets the result said
static void PlaybackAtExit(void) {
    if (!g_finished && g_mode == DEMO_PLAY)
        PrintResult("ended by the game");
}

static void StreamBroken(const char *what) {
    if (!g_streamBroken)
        printf("[demo] frame %u: %s - the game asked for input the recording does not have here\n", g_frame, what);
    g_streamBroken = true;
    FinishPlayback("stopped");
}

// ---------------------------------------------------------------------------------------------------------------
// Frames

void Demo_FrameStart(void) {
    if (g_mode == DEMO_OFF)
        return;

    if (g_frame != 0)
        g_frameMs += 1000.0 / (FRAME_RATE_INT > 0 ? FRAME_RATE_INT : 60);
    g_readsThisFrame = 0;

    if (g_mode == DEMO_REBASE) {
        if (g_frame >= g_rebase.size()) {
            CloseRecording();
            printf("[demo] rebased %u frames to %s (%u of them differ from the old recording)\n", g_frame,
                   g_path.c_str(), g_divergedFrames);
            fflush(stdout);
            ExitProcess(0);
        }
        for (uint32_t mark : g_rebase[g_frame].bookmarks) {
            PutByte(EV_BOOKMARK);
            PutU32(mark);
        }
        for (int i = 0; i < 4; i++)
            g_rebasePadAt[i] = 0;
        g_rebaseMouseAt = 0;
        return;
    }

    if (g_mode == DEMO_RECORD) {
        static bool f6Down = false;
        bool down = (GetAsyncKeyState(VK_F6_) & 0x8000) != 0;
        if (down && !f6Down && GameWindowHasFocus()) {
            PutByte(EV_BOOKMARK);
            PutU32(g_frame);
            printf("[demo] bookmark at frame %u\n", g_frame);
            fflush(stdout);
        }
        f6Down = down;
        return;
    }

    while (PeekByte() == EV_BOOKMARK) {
        uint32_t frame;
        g_at++;
        Take(&frame, 4);
        printf("[demo] bookmark %u (recorded at frame %u) at frame %u\n", ++g_bookmarks, frame, g_frame);
        fflush(stdout);
    }
    if (PeekByte() < 0 || (g_untilFrame != 0 && g_frame >= g_untilFrame))
        FinishPlayback("finished");
    if (PeekByte() == EV_DRIVING)
        FinishPlayback("reached the driving engine");
    for (uint32_t frame : g_dumpFrames) {
        if (frame == g_frame)
            printf("[demo] frame %u: dumping frame %u\n", g_frame, D3D9_RequestDump());
    }
    if (g_shotEvery != 0 && g_frame >= g_shotFrom && g_frame % g_shotEvery == 0) {
        char name[64];
        snprintf(name, sizeof(name), "demo_shots/%06u", g_frame);
        CreateDirectoryA("demo_shots", NULL);   // in the sandbox
        D3D9_RequestScreenshot(name);
    }
}

// GameState's bytes, for finding the field behind a "gamestate" difference: printed at the first frame that
// differs, and at NIGHTFIRE_DEMO_DUMP_GAMESTATE's frame, to compare with a run of the originals
static void PrintGameState(void) {
    const uint8_t *bytes = (const uint8_t *)&GameState;
    printf("[demo] frame %u GameState:", g_frame);
    for (size_t i = 0; i < sizeof(GameState); i++)
        printf("%s%02x", i % 4 == 0 ? " " : "", bytes[i]);
    printf("\n");
    fflush(stdout);
}

// What the objects' checksum covers, an object a line, for finding the object behind an "objects" difference:
// printed at NIGHTFIRE_DEMO_DUMP_OBJECTS's frame, to compare with a run of the originals
static void PrintObjects(void) {
    int i = 0;
    for (obj_tag *o = control_first_object(); o != NULL; o = o->nextObject, i++) {
        uint32_t p[3], r[3];
        memcpy(p, &o->position, sizeof(p));
        memcpy(r, &o->rotation, sizeof(r));
        printf("[demo] frame %u object %d: type %u flags %02x state %u/%u pos %08x %08x %08x rot %08x %08x %08x\n",
               g_frame, i, (uint)o->objectType, (uint)o->flags, (uint)o->curState, (uint)(ushort)o->subState, p[0],
               p[1], p[2], r[0], r[1], r[2]);
    }
    fflush(stdout);
}

// GameState+0x04 is Collide_StraddleCels' visit stamp: every collision query takes the next number and marks the
// portals it crosses with it, so as not to test one twice. Nothing reads its value but those comparisons, and they
// only ask whether a portal carries this query's number. Some queries come from drawing (View_AddForcedObjects),
// which the host can make happen a different number of times, so a playback's stamp can run a constant few behind
// or ahead of the recording's with the game otherwise the same. A "gamestate" difference that is only that is not
// counted as one. Every change of the offset is reported: a large one in a single frame is more likely a port making
// a different number of queries, which the other checks then show as the game going another way.
static bool StampOffsetMatches(uint32_t recorded) {
    GameState_t state = GameState;
    uint32_t stamp = state.collisionStamp;
    state.collisionStamp = stamp + g_stampOffset;
    if (Fnv(kFnvBasis, &state, sizeof(state)) == recorded)
        return true;
    for (int32_t offset = -4096; offset <= 4096; offset++) {
        state.collisionStamp = stamp + offset;
        if (Fnv(kFnvBasis, &state, sizeof(state)) != recorded)
            continue;
        printf("[demo] frame %u: GameState+0x04, the collision visit stamp, is %d %s the recording's (it was %d); the "
               "game does not depend on its value, so this is not counted as a difference\n", g_frame,
               offset < 0 ? -offset : offset, offset < 0 ? "ahead of" : "behind", g_stampOffset);
        fflush(stdout);
        g_stampOffset = offset;
        return true;
    }
    return false;
}

static void Checksums(uint32_t sums[SUM_COUNT]) {
    // The random generator's state and multipliers, in the game's order (Rand_Random)
    const uint32_t rand[4] = {RandStateA, RandStateB, RandMultiplierB, RandMultiplierA};
    sums[SUM_RNG] = Fnv(kFnvBasis, rand, sizeof(rand));
    sums[SUM_GAMESTATE] = Fnv(kFnvBasis, &GameState, sizeof(GameState));
    sums[SUM_CHANNELS] = Fnv(Fnv(kFnvBasis, switch_channels, sizeof(switch_channels)), switch_channels_time,
                             sizeof(switch_channels_time));

    if (g_staleObjects) {
        sums[SUM_OBJECTS] = kFnvBasis;
        sums[SUM_PLAYERS] = kFnvBasis;
        return;
    }

    uint32_t objects = kFnvBasis;
    for (obj_tag *o = control_first_object(); o != NULL; o = o->nextObject) {
        objects = Fnv(objects, &o->objectType, 1);
        objects = Fnv(objects, &o->flags, 1);
        objects = Fnv(objects, &o->curState, 2);
        objects = Fnv(objects, &o->subState, 2);
        objects = Fnv(objects, &o->position, sizeof(o->position));
        objects = Fnv(objects, &o->rotation, sizeof(o->rotation));
    }
    sums[SUM_OBJECTS] = objects;

    uint32_t players = kFnvBasis;
    for (int i = 0; i < 4; i++) {
        obj_tag *player = glb_players[i];
        if (player == NULL)
            continue;
        players = Fnv(players, &i, sizeof(i));
        players = Fnv(players, &player->position, sizeof(player->position));
        players = Fnv(players, &player->rotation, sizeof(player->rotation));
        players = Fnv(players, &player->curState, 2);
        players = Fnv(players, &player->subState, 2);
    }
    sums[SUM_PLAYERS] = players;
}

void Demo_FrameEnd(void) {
#ifndef NIGHTFIRE_STANDALONE
    DataWrites_Frame();
    MemTrace_Frame();
#endif
    if (g_mode == DEMO_OFF)
        return;

    uint32_t sums[SUM_COUNT];
    Checksums(sums);

    if (g_mode == DEMO_REBASE && memcmp(sums, g_rebase[g_frame].sums, sizeof(sums)) != 0 && g_divergedFrames++ == 0) {
        printf("[demo] frame %u: the game parts from the old recording here (expected, for a change meant to change "
               "it)\n", g_frame);
        fflush(stdout);
    }
    if (Writing()) {
        PutByte(EV_FRAME);
        Put(sums, sizeof(sums));
        if (g_frame % 60 == 0)
            fflush(g_out);
    } else {
        uint32_t recorded[SUM_COUNT];
        if (PeekByte() != EV_FRAME) {
            StreamBroken("a frame ended");
            return;
        }
        g_at++;
        Take(recorded, sizeof(recorded));
        if (recorded[SUM_GAMESTATE] != sums[SUM_GAMESTATE] && StampOffsetMatches(recorded[SUM_GAMESTATE]))
            sums[SUM_GAMESTATE] = recorded[SUM_GAMESTATE];
        bool diverged = false;
        for (int i = 0; i < SUM_COUNT; i++)
            diverged |= recorded[i] != sums[i];
        if (g_frame == g_dumpStateFrame)
            PrintGameState();
        if (g_frame == g_dumpObjectsFrame)
            PrintObjects();
        if (getenv("NIGHTFIRE_DEMO_DUMP_VIEWER") != NULL && glb_viewer[0] != NULL) {
            uint32_t v[7];
            memcpy(v, &glb_viewer[0]->pos, 12);
            memcpy(v + 3, &glb_viewer[0]->someVec, 12);
            memcpy(v + 6, &glb_viewer[0]->zoom, 4);
            printf("[demo] frame %u viewer: %08x %08x %08x %08x %08x %08x %08x\n", g_frame, v[0], v[1], v[2], v[3], v[4],
                   v[5], v[6]);
        }
        if (diverged) {
            if (g_divergedFrames == 0) {
                g_firstDivergence = g_frame;
                if (recorded[SUM_GAMESTATE] != sums[SUM_GAMESTATE] && g_frame != g_dumpStateFrame)
                    PrintGameState();
            }
            if (g_divergedFrames < 20) {
                printf("[demo] frame %u differs:", g_frame);
                for (int i = 0; i < SUM_COUNT; i++) {
                    if (recorded[i] != sums[i])
                        printf(" %s", kSumNames[i]);
                }
                printf("\n");
                fflush(stdout);
            }
            g_divergedFrames++;
            if (g_stopOnDivergence)
                FinishPlayback("stopped at the first difference");
        }
    }

    if (g_frame % 1800 == 0) {
        int objects = 0;
        for (obj_tag *o = g_staleObjects ? NULL : control_first_object(); o != NULL; o = o->nextObject)
            objects++;
        printf("[demo] frame %u: level 0x%08x, %d objects\n", g_frame, (unsigned)GameState.CurrentLevelHashcode,
               objects);
        fflush(stdout);
    }

    g_frame++;
}

// ---------------------------------------------------------------------------------------------------------------
// Input

static void PackPad(const DemoPad *pad, uint8_t out[PAD_BYTES]) {
    out[0] = pad->connected;
    memcpy(out + 1, &pad->buttons, 2);
    out[3] = pad->leftTrigger;
    out[4] = pad->rightTrigger;
    memcpy(out + 5, &pad->thumbLX, 2);
    memcpy(out + 7, &pad->thumbLY, 2);
    memcpy(out + 9, &pad->thumbRX, 2);
    memcpy(out + 11, &pad->thumbRY, 2);
    out[13] = 0;
}

static void UnpackPad(const uint8_t in[PAD_BYTES], DemoPad *pad) {
    pad->connected = in[0];
    memcpy(&pad->buttons, in + 1, 2);
    pad->leftTrigger = in[3];
    pad->rightTrigger = in[4];
    memcpy(&pad->thumbLX, in + 5, 2);
    memcpy(&pad->thumbLY, in + 7, 2);
    memcpy(&pad->thumbRX, in + 9, 2);
    memcpy(&pad->thumbRY, in + 11, 2);
}

void Demo_Pad(int port, DemoPad *pad) {
    if (g_mode == DEMO_OFF || port < 0 || port > 3)
        return;

    uint8_t packed[PAD_BYTES];
    if (g_mode == DEMO_REBASE) {
        // This frame's next poll of the port; a game that polls more often than the recording did gets the last again
        const std::vector<std::vector<uint8_t>> &polls = g_rebase[g_frame].pads[port];
        if (g_rebasePadAt[port] < polls.size())
            memcpy(g_rebaseLastPad[port], polls[g_rebasePadAt[port]++].data(), PAD_BYTES);
        UnpackPad(g_rebaseLastPad[port], pad);
    }
    if (Writing()) {
        PackPad(pad, packed);
        bool same = g_havePad[port] && memcmp(packed, g_lastPad[port], PAD_BYTES) == 0;
        if (same) {
            PutByte((uint8_t)(EV_PAD_SAME + port));
        } else {
            PutByte((uint8_t)(EV_PAD + port));
            Put(packed, PAD_BYTES);
        }
    } else {
        int tag = PeekByte();
        if (tag == EV_PAD_SAME + port && g_havePad[port]) {
            g_at++;
            memcpy(packed, g_lastPad[port], PAD_BYTES);
        } else if (tag == EV_PAD + port) {
            g_at++;
            Take(packed, PAD_BYTES);
        } else {
            StreamBroken("a controller was polled");
            return;
        }
        UnpackPad(packed, pad);
    }

    if (!g_havePad[port] || memcmp(packed, g_lastPad[port], PAD_BYTES) != 0)
        g_padPacket[port]++;
    memcpy(g_lastPad[port], packed, PAD_BYTES);
    g_havePad[port] = true;
    pad->packet = g_padPacket[port];
}

void Demo_Mouse(bool *captured, long *countsX, long *countsY) {
    if (g_mode == DEMO_OFF)
        return;

    uint8_t packed[MOUSE_BYTES];
    if (g_mode == DEMO_REBASE) {
        const std::vector<std::vector<uint8_t>> &reads = g_rebase[g_frame].mice;
        if (g_rebaseMouseAt < reads.size())
            memcpy(g_rebaseLastMouse, reads[g_rebaseMouseAt++].data(), MOUSE_BYTES);
        int32_t x, y;
        memcpy(&x, g_rebaseLastMouse + 1, 4);
        memcpy(&y, g_rebaseLastMouse + 5, 4);
        *captured = g_rebaseLastMouse[0] != 0;
        *countsX = x;
        *countsY = y;
    }
    if (Writing()) {
        int32_t x = (int32_t)*countsX, y = (int32_t)*countsY;
        packed[0] = *captured ? 1 : 0;
        memcpy(packed + 1, &x, 4);
        memcpy(packed + 5, &y, 4);
        if (g_haveMouse && memcmp(packed, g_lastMouse, MOUSE_BYTES) == 0) {
            PutByte(EV_MOUSE_SAME);
        } else {
            PutByte(EV_MOUSE);
            Put(packed, MOUSE_BYTES);
        }
    } else {
        int tag = PeekByte();
        if (tag == EV_MOUSE_SAME && g_haveMouse) {
            g_at++;
            memcpy(packed, g_lastMouse, MOUSE_BYTES);
        } else if (tag == EV_MOUSE) {
            g_at++;
            Take(packed, MOUSE_BYTES);
        } else {
            StreamBroken("the mouse was read");
            return;
        }
        int32_t x, y;
        memcpy(&x, packed + 1, 4);
        memcpy(&y, packed + 5, 4);
        *captured = packed[0] != 0;
        *countsX = x;
        *countsY = y;
    }
    memcpy(g_lastMouse, packed, MOUSE_BYTES);
    g_haveMouse = true;
}

void Demo_SoundAnswer(int buffer, DemoSoundQuery query, uint32_t *answer) {
    if (g_mode == DEMO_OFF)
        return;
    uint8_t slot = buffer >= 0 && buffer < SOUND_BUFFERS ? (uint8_t)buffer : 0xff;
    int row = slot == 0xff ? SOUND_BUFFERS : slot;

    if (Writing()) {
        if (g_haveSound[row][query] && g_lastSound[row][query] == *answer) {
            PutByte(EV_SOUND_SAME);
            PutByte(slot);
            PutByte((uint8_t)query);
        } else {
            PutByte(EV_SOUND);
            PutByte(slot);
            PutByte((uint8_t)query);
            PutU32(*answer);
        }
    } else {
        int tag = PeekByte();
        if ((tag != EV_SOUND && tag != EV_SOUND_SAME) || g_at + 3 > g_stream.size() || g_stream[g_at + 1] != slot ||
            g_stream[g_at + 2] != (uint8_t)query || (tag == EV_SOUND_SAME && !g_haveSound[row][query])) {
            StreamBroken("the sound device was asked about a buffer");
            return;
        }
        g_at += 3;
        if (tag == EV_SOUND)
            Take(answer, 4);
        else
            *answer = g_lastSound[row][query];
    }
    g_lastSound[row][query] = *answer;
    g_haveSound[row][query] = true;
}

void Demo_HeapReset(void) {
    g_staleObjects = true;
}

void Demo_ObjectListsReset(void) {
    g_staleObjects = false;
}

void Demo_LeavingForDriving(void) {
    bool rebasing = g_mode == DEMO_REBASE;
    if (Writing()) {
        PutByte(EV_DRIVING);
        fclose(g_out);
        g_out = NULL;
        printf("[demo] recording %s ends here: the session leaves for the driving engine\n", g_path.c_str());
        fflush(stdout);
        g_mode = DEMO_OFF;
        if (rebasing)
            ExitProcess(0);   // a rebase stops where the old session left; it does not launch the driving engine
    } else if (g_mode == DEMO_PLAY) {
        FinishPlayback("reached the driving engine");
    }
}
