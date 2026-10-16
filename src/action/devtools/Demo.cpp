#include "Demo.h"

#include "../actionhelpers.h"
#include "../game.h"
#include "../game/obj/object.h"
#include "../game/sp/SwitchChannels.h"
#include "../engine/XboxSettings.h"
#include "../engine/LaunchOptions.h"
#include "../../common/renderWindow.h"
#include "../../common/fmv/FmvAudio.h"
#include "../../common/gfx/d3d9Backend.h"
#include "../../common/xbeOriginal.h"

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

enum DemoMode { DEMO_OFF, DEMO_RECORD, DEMO_PLAY };

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

// The random generator's state and multipliers (Rand_Random, 0x000d52c0)
#define RandState ((const uint8_t *)0x0018cdf8)
#define RAND_STATE_BYTES 0x10
// The head of the live object list (control_first_object)
#define FirstObject (*(obj_tag **)0x001df34c)
// KeQuerySystemTime's slot in the XBE's kernel import table
#define KernelKeQuerySystemTime (*(void(__stdcall **)(LARGE_INTEGER *))0x0015d198)

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
static uint32_t g_firstDivergence = 0;
static uint32_t g_bookmarks = 0;
static bool g_stopOnDivergence = false;
static std::vector<uint32_t> g_dumpFrames;
static int g_presentEvery = 0;
static uint32_t g_dumpStateFrame = 0xffffffffu;   // NIGHTFIRE_DEMO_DUMP_GAMESTATE
static int32_t g_stampOffset = 0;                  // see StampOffsetMatches

// The last state per port and of the mouse, both modes
static uint8_t g_lastPad[4][PAD_BYTES];
static bool g_havePad[4];
static uint32_t g_padPacket[4];
static uint8_t g_lastMouse[MOUSE_BYTES];
static bool g_haveMouse = false;
static uint32_t g_lastSound[SOUND_BUFFERS + 1][2];   // per buffer (and "none") and query
static bool g_haveSound[SOUND_BUFFERS + 1][2];

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

static void __stdcall Demo_KeQuerySystemTime(LARGE_INTEGER *systemTime) {
    if (systemTime != NULL)
        systemTime->QuadPart = g_systemTimeBase + (int64_t)(g_frameMs * 10000.0);
}

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
    const char *fast = getenv("NIGHTFIRE_DEMO_FAST");
    g_presentEvery = fast != NULL ? atoi(fast) : 0;
    const char *dumpState = getenv("NIGHTFIRE_DEMO_DUMP_GAMESTATE");
    if (dumpState != NULL)
        g_dumpStateFrame = (uint32_t)strtoul(dumpState, NULL, 10);
    const char *dumps = getenv("NIGHTFIRE_DEMO_DUMP");
    for (const char *p = dumps; p != NULL && *p != '\0';) {
        g_dumpFrames.push_back((uint32_t)strtoul(p, (char **)&p, 10));
        while (*p == ',' || *p == ' ')
            p++;
    }
    return true;
}

void Demo_Init(void) {
    const char *play = getenv("NIGHTFIRE_DEMO_PLAY");
    if (play != NULL && play[0] != '\0') {
        if (!StartPlayback(play))
            ExitProcess(4);
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
    KernelKeQuerySystemTime = Demo_KeQuerySystemTime;

    FmvAudio::SetVirtualClock(MovieClockSeconds);
}

void Demo_RestoreOriginals(void) {
    const char *list = getenv("NIGHTFIRE_ORIGINAL");
    if (list == NULL || list[0] == '\0')
        return;
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

bool Demo_Active(void) {
    return g_mode != DEMO_OFF;
}

bool Demo_Playing(void) {
    return g_mode == DEMO_PLAY;
}

int Demo_PresentEvery(void) {
    return g_mode == DEMO_PLAY ? g_presentEvery : 0;
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
    if (!g_finished)
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
    if (PeekByte() < 0)
        FinishPlayback("finished");
    if (PeekByte() == EV_DRIVING)
        FinishPlayback("reached the driving engine");
    for (uint32_t frame : g_dumpFrames) {
        if (frame == g_frame)
            printf("[demo] frame %u: dumping frame %u\n", g_frame, D3D9_RequestDump());
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
    sums[SUM_RNG] = Fnv(kFnvBasis, RandState, RAND_STATE_BYTES);
    sums[SUM_GAMESTATE] = Fnv(kFnvBasis, &GameState, sizeof(GameState));
    sums[SUM_CHANNELS] = Fnv(Fnv(kFnvBasis, switch_channels, sizeof(switch_channels)), switch_channels_time,
                             sizeof(switch_channels_time));

    uint32_t objects = kFnvBasis;
    for (obj_tag *o = FirstObject; o != NULL; o = o->nextObject) {
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
    if (g_mode == DEMO_OFF)
        return;

    uint32_t sums[SUM_COUNT];
    Checksums(sums);

    if (g_mode == DEMO_RECORD) {
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
        for (obj_tag *o = FirstObject; o != NULL; o = o->nextObject)
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
    if (g_mode == DEMO_RECORD) {
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
    if (g_mode == DEMO_RECORD) {
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

    if (g_mode == DEMO_RECORD) {
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

void Demo_LeavingForDriving(void) {
    if (g_mode == DEMO_RECORD) {
        PutByte(EV_DRIVING);
        fclose(g_out);
        g_out = NULL;
        printf("[demo] recording %s ends here: the session leaves for the driving engine\n", g_path.c_str());
        fflush(stdout);
        g_mode = DEMO_OFF;
    } else if (g_mode == DEMO_PLAY) {
        FinishPlayback("reached the driving engine");
    }
}
