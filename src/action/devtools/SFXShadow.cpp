// Shadow tests for the sound layer (sound/SFX.cpp, sound/SFXStream.cpp): each function, the original and ours, on
// the same made-up state - sounds in the list and free, banks of made-up sounds and samples, voices, the stream with
// its file buffers and markers, the listeners, the mixer's volumes - with the psi layer below (the voices, the
// timer, the files) a logging fake.
// Run with the soundport suite (MenuShadowTests=on, NIGHTFIRE_SHADOW_ONLY=soundport, or =sfx for these alone);
// NIGHTFIRE_SFX_GROUP=<part of a name> runs one group.

#include "SFXShadow.h"
#include "AllOriginal.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../actionhelpers.h"
#include "../assets.h"
#include "../sound/SFX.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <map>
#include <string>
#include <vector>

namespace {

#define ORIGINAL(at, type) ((type)(uintptr_t)(at))
#ifndef COUNT
#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))
#endif

// The layer's state in the game: the globals from ES_MuteMusic to the stream's jump state, the banks to the
// listeners, the track list and the sounds, and the bank list to the end of the stream's
const struct {
    uint32_t at;
    uint32_t size;
} kGameState[] = {
    {0x00182748, 0x00182b80 - 0x00182748},
    {0x00279510, 0x0027af28 - 0x00279510},
    {0x00286a90, 0x00297fe8 - 0x00286a90},
    {0x002981e8, 0x00299a20 - 0x002981e8},
    {0x0017d581, 1},                    // ls.relatedToMainMenuSound
    {XbeOurs(0x001f658c), 4},                    // GameState.CurrentLevelHashcode
    {0x00260018, 4},                    // MPSettings.isMultiplayer
    {0x00260044, 4},                    // MPSettings.numPlayers
    {XbeOurs(0x0029a180), 8},                    // MusicEventList[0] and [1]
    {XbeOurs(0x0029a280), 4},                    // SoundInit
};

const int DEFS = 8;
const int POOL = 8;
const int BANKS = 3;
const int BANK_SFX = 6;
const int SAMPLES = 12;
const int HANDLES = 4;
const int STARTS = 8;
const int MARKERS = 24;

// A sound's settings and its samples, as a bank's .SFX file has them
struct Def {
    SFXParameters p;
    SFXSamplePoolFiles pool[POOL];
};

struct BankFile {
    int count;
    SFXHeader headers[BANK_SFX];
    Def defs[DEFS];
};

struct World {
    BankFile banks[BANKS];
    SampleHeaderData samples[BANKS][SAMPLES];
    uint8_t handles[HANDLES][0x48];
    _VECTOR vectors[6];
    char path[64];
    psiSFX psi;
    SFXItem *next;
    // The files psiSFXLoadFile gives: a bank's three, SBINFO.SBI
    struct {
        int count;
        SFXHeader headers[BANK_SFX];
        Def defs[2];
    } sfxFile;
    struct {
        int count;
        SampleHeaderData samples[SAMPLES];
    } shfFile;
    uint8_t sbfFile[0x100];
    int sbinfo[SFX_NUM_BANKS];
    // A track's .SMF: the header, the sections' starts, the markers
    struct {
        StreamMarkerHeaderData header;
        MusicMarkerStartData starts[STARTS];
        MusicMarkerData markers[MARKERS];
    } markerFile;
    char readBuffers[STREAM_READ_BUFFERS][0x40];
    char dataBuffer2[0x40];
    StreamLookupFileDetails lookup[4];
};
World world;

int cases, differ, nanChoices;

bool IsNaN(uint32_t u) {
    return (u & 0x7f800000u) == 0x7f800000u && (u & 0x007fffffu) != 0;
}
uint32_t gen, caseSeed;

uint32_t R(uint32_t n) {
    gen = gen * 1664525u + 1013904223u;
    uint32_t v = gen ^ (gen >> 15);
    v *= 0x2c1b3c6du;
    v ^= v >> 12;
    v *= 0x297a2d39u;
    v ^= v >> 15;
    return n ? v % n : v;
}

int RI(int lo, int hi) {
    return lo + (int)R((uint32_t)(hi - lo + 1));
}

float RF(float lo, float hi) {
    return lo + (hi - lo) * (float)R(0x10000) / 65535.0f;
}

// A number for a float: mostly in range, sometimes a signed zero, an end, a NaN or an infinity
float Num(float lo, float hi) {
    switch (R(24)) {
        case 0: return 0.0f;
        case 1: return -0.0f;
        case 2: { uint32_t u = 0x7fc00000u | R(0x400000); float f; memcpy(&f, &u, 4); return f; }
        case 3: return hi;
        case 4: return lo;
        case 5: return R(2) ? INFINITY : -INFINITY;
        default: return RF(lo, hi);
    }
}

void RandomVector(_VECTOR *v, float range) {
    v->x = Num(-range, range);
    v->y = Num(-range, range);
    v->z = Num(-range, range);
}

// A pointer for the log: an offset into the world, else the address with the top bit set
uint32_t W(const void *p) {
    if (p == NULL)
        return 0xffffffffu;
    uintptr_t at = (uintptr_t)p, base = (uintptr_t)&world;
    if (at >= base && at < base + sizeof(world))
        return (uint32_t)(at - base);
    return 0x80000000u | (uint32_t)at;
}

// A float's bits for the log, any NaN the same
uint32_t F(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return IsNaN(u) ? 0x7fc0ffffu : u;
}

typedef uint32_t (*Runner)(bool original);
struct Case {
    const char *name;
    Runner run;
};
const Case *current;

__declspec(noinline) void ZeroStack(void) {
    volatile uint32_t below[0x1800];
    for (int i = 0; i < 0x1800; i++)
        below[i] = 0;
}

#define CDECL_RUN(at, ours, type, ...)                                                                                \
    (original ? ([&]() { XbeOriginalScope o(at); ZeroStack(); return (uint32_t)ORIGINAL(at, type)(__VA_ARGS__); }()) \
              : ([&]() { ZeroStack(); return (uint32_t)ours(__VA_ARGS__); }()))

#define CDECL_VOID(at, ours, type, ...)                                                                               \
    do {                                                                                                              \
        ZeroStack();                                                                                                  \
        if (original) {                                                                                               \
            XbeOriginalScope o(at);                                                                                   \
            ORIGINAL(at, type)(__VA_ARGS__);                                                                          \
        } else {                                                                                                      \
            ours(__VA_ARGS__);                                                                                        \
        }                                                                                                             \
    } while (0)

// ---- the fakes' answers, drawn per case

struct Answers {
    int timer;
    int timerStep;
    uint32_t playingMask[2];    // psiIsSamplePlaying, by voice
    int requestVoice;           // psiRequestVoiceHandle: 0 to 63, or -1
    int initialiseStream;       // SFXInitialiseStream: 0, or -1
    uint32_t loadFails;         // psiSFXLoadFile: a bit for each file asked for, NULL where set
    int loads;
    int playbackBuffer;         // psiStreamCreatePlaybackBuffer
    int openFile;               // psiAsyncOpenFile
    uint32_t readFinished;      // psiAsyncHasReadFinished: a bit for each call
    int finishCalls;
    int buffers;                // psiAsyncCreateBuffer's so far
    int playPos;
};
Answers answers;

// ---- the regions and the comparison

struct Area {
    uint8_t *at;
    size_t size;
};
Area areas[16];
int areaCount;
ShadowRegions regions;

void AddArea(void *at, size_t size) {
    regions.Add(at, size);
    areas[areaCount].at = (uint8_t *)at;
    areas[areaCount++].size = size;
}

void RandomWorld(void);

uint32_t RunCase(bool original) {
    gen = caseSeed;
    return current->run(original);
}

bool CompareCase(const char *what, int trial) {
    static std::vector<uint8_t> theirState, ourState;
    Answers saved = answers;
    regions.Save();
    ShadowLog_Clear();
    uint32_t theirResult = RunCase(true);
    regions.Capture(theirState);
    std::string theirLog = ShadowLog_Text();
    regions.Restore();
    answers = saved;
    ShadowLog_Clear();
    uint32_t ourResult = RunCase(false);
    regions.Capture(ourState);
    const std::string &ourLog = ShadowLog_Text();

    static std::map<std::string, int> reports;
    int &reported = reports[what];
    bool same = true;
    if (theirResult != ourResult) {
        same = false;
        if (reported++ < 3)
            printf("[sfx] %s case %d: returns 0x%x, the original 0x%x\n", what, trial, ourResult, theirResult);
    }
    // Two NaNs the x87 and SSE2 chose differently between (a float sum written in plain C++) count as the same
    bool nans = false;
    if (theirState != ourState && theirState.size() == ourState.size()) {
        bool onlyNaNs = true;
        for (size_t at = 0; at + 4 <= theirState.size() && onlyNaNs; at += 4) {
            uint32_t a, b;
            memcpy(&a, &theirState[at], 4);
            memcpy(&b, &ourState[at], 4);
            if (a != b)
                onlyNaNs = IsNaN(a) && IsNaN(b);
        }
        if (onlyNaNs) {
            nans = true;
            ourState = theirState;
        }
    }
    if (theirState != ourState) {
        same = false;
        int region;
        size_t offset;
        if (regions.Describe(theirState, ourState, &region, &offset) && reported++ < 3) {
            size_t at = 0;
            for (int a = 0; a < region; a++)
                at += areas[a].size;
            size_t word = offset & ~(size_t)3;
            uint32_t theirs = 0, ours = 0;
            if (word + 4 <= areas[region].size) {
                memcpy(&theirs, &theirState[at + word], 4);
                memcpy(&ours, &ourState[at + word], 4);
            }
            printf("[sfx] %s case %d: memory differs at 0x%08x: 0x%08x, the original 0x%08x\n", what, trial,
                   (unsigned)((uintptr_t)areas[region].at + word), ours, theirs);
        }
    }
    if (theirLog != ourLog) {
        same = false;
        if (reported++ < 3) {
            size_t d = 0;
            while (d < theirLog.size() && d < ourLog.size() && theirLog[d] == ourLog[d])
                d++;
            size_t from = d > 120 ? d - 120 : 0;
            printf("[sfx] %s case %d: calls differ at %d\n  ours:     %.400s\n  original: %.400s\n", what, trial,
                   (int)d, ourLog.c_str() + from, theirLog.c_str() + from);
        }
    }
    if (same && nans)
        nanChoices++;
    return same;
}

void Test(const char *group, const Case *list, int count, int trials) {
    int groupCases = cases, groupDiffer = differ;
    for (int c = 0; c < count; c++) {
        current = &list[c];
        if (getenv("NIGHTFIRE_SHADOW_TRACE") != NULL) {
            printf("[sfx] %s\n", current->name);
            fflush(stdout);
        }
        for (int trial = 0; trial < trials; trial++) {
            gen = ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
            RandomWorld();
            caseSeed = gen;
            cases++;
            if (!CompareCase(current->name, trial))
                differ++;
        }
    }
    printf("[sfx] %s: %d cases, %d differ\n", group, cases - groupCases, differ - groupDiffer);
    fflush(stdout);
}

#define C(name, run) {name, run}

// ---- the psi layer's fakes

void LogVector(const char *what, const _VECTOR *v) {
    ShadowLog("%s %s(%08x %08x %08x)\n", what, v == NULL ? "-" : "", v ? F(v->x) : 0, v ? F(v->y) : 0,
              v ? F(v->z) : 0);
}

int64_t FakeGetTimer(void) {
    ShadowLog("timer\n");
    answers.timer += answers.timerStep;
    return answers.timer;
}
void FakeSetReverb(int depth) {
    ShadowLog("reverb %d\n", depth);
}
void FakeSetupListener(_VECTOR *pos, _VECTOR *vel, _VECTOR *dir, _VECTOR *up, _VECTOR *norm) {
    ShadowLog("listener %x %x %x %x %x\n", W(pos), W(vel), W(dir), W(up), W(norm));
    LogVector(" pos", pos);
    LogVector(" vel", vel);
    LogVector(" dir", dir);
    LogVector(" up", up);
    LogVector(" norm", norm);
}
void FakeSetVolume(uint32_t voice, int volume) {
    ShadowLog("volume %d %d\n", voice, volume);
}
void FakeSFXUpdate(void) {
    ShadowLog("SFXUpdate\n");
}
void FakeMarkInactive(uint32_t voice) {
    ShadowLog("inactive %d\n", voice);
}
uint32_t FakeIsSamplePlaying(uint32_t voice) {
    ShadowLog("playing? %d\n", voice);
    return voice < 64 ? (answers.playingMask[voice / 32] >> (voice % 32)) & 1 : voice & 1;
}
uint32_t FakeRequestVoiceHandle(SampleHeaderData *sample, int is3d) {
    ShadowLog("request %x %d\n", W(sample), is3d);
    return (uint32_t)answers.requestVoice;
}
void FakeSamplePause(uint32_t voice) {
    ShadowLog("pause %d\n", voice);
}
void FakeUnPause(uint32_t voice) {
    ShadowLog("unpause %d\n", voice);
}
uint32_t FakeSFXStop(uint32_t voice) {
    ShadowLog("stop %d\n", voice);
    return 0;
}
void FakeStopStream(StreamData *stream) {
    ShadowLog("stop stream %x\n", W(stream));
}
void LogSfx(const char *what, const psiSFX *s) {
    ShadowLog("%s %d %d %d %d %d %d %d %d %d %x %x %x %d\n", what, s->Handle, s->InnerRadius, s->OuterRadius,
              s->Volume, s->Frequency, s->Is3d, s->Positioned, s->IsStreamed, s->Pan, W(s->pos), W(s->vel),
              W(s->Specials), s->TrackingType);
    LogVector(" pos", s->pos);
    LogVector(" vel", s->vel);
}
void FakeInitialiseSound(psiSFX *s) {
    LogSfx("initialise", s);
}
void FakeUpdateSound(psiSFX *s) {
    LogSfx("update", s);
}
int FakeSampleKeyOn(uint32_t voice, int reverb) {
    ShadowLog("key on %d %d\n", voice, reverb);
    return 1;
}
void FakeUpdateMusicalEvents(void) {
    ShadowLog("music\n");
}
void FakeUpdateStreams(void) {
    ShadowLog("streams\n");
}
void *FakeLoadFile(char *fileName) {
    ShadowLog("load %s\n", fileName);
    int n = answers.loads++;
    if ((answers.loadFails >> (n & 31)) & 1)
        return NULL;
    size_t length = strlen(fileName);
    const char *type = length >= 4 ? fileName + length - 4 : "";
    if (strcmp(type, ".SFX") == 0)
        return &world.sfxFile;
    if (strcmp(type, ".SHF") == 0)
        return &world.shfFile;
    if (strcmp(type, ".SBF") == 0)
        return world.sbfFile;
    if (strcmp(type, ".SBI") == 0 || strcmp(type, ".MXI") == 0)
        return world.sbinfo;
    if (strcmp(type, ".SMF") == 0)
        return &world.markerFile;
    return NULL;
}
void FakeFreeFileMem(void *data) {
    ShadowLog("free %x\n", W(data));
}
void FakeReleaseStreamSystem(void) {
    ShadowLog("release streams\n");
}
void FakeInitialiseStreamSystem(void) {
    ShadowLog("initialise streams\n");
}
void FakeDeleteChannel(uint32_t voice) {
    ShadowLog("delete channel %d\n", voice);
}
int FakeCreatePlaybackBuffer(int frequency, int channels, int byteSize) {
    ShadowLog("playback buffer %d %d %x\n", frequency, channels, byteSize);
    return answers.playbackBuffer;
}
int FakeCreateBuffer(int size) {
    ShadowLog("buffer %x\n", size);
    return (int)(uintptr_t)world.readBuffers[answers.buffers++ & 7];
}
void FakeAsyncReadFile(int fileHandle, int byteOffset, int byteSize, void *destBuffer) {
    ShadowLog("read %d %x %x %x\n", fileHandle, byteOffset, byteSize, W(destBuffer));
}
void FakeAsyncCloseFile(int fileHandle) {
    ShadowLog("close %d\n", fileHandle);
}
void FakeStreamStop(uint32_t voice) {
    ShadowLog("stream stop %d\n", voice);
}
int FakeAsyncOpenFile(char *fileName) {
    ShadowLog("open %s\n", fileName);
    return answers.openFile;
}
int FakeAsyncHasReadFinished(int fileHandle) {
    ShadowLog("finished? %d\n", fileHandle);
    return (answers.readFinished >> (answers.finishCalls++ & 31)) & 1;
}
uint32_t FakeGetPlayPos(uint32_t voice) {
    ShadowLog("play pos %d\n", voice);
    return (uint32_t)answers.playPos;
}
void FakeStreamClear(uint32_t size, void *dest) {
    ShadowLog("clear %x %x\n", size, W(dest));
}
void FakeStreamWrite(uint32_t voice, int offset, const void *src, uint32_t length) {
    ShadowLog("write %d %x %x %x\n", voice, offset, W(src), length);
}
void FakeStreamPlay(uint32_t stream, int reverbDepth) {
    ShadowLog("stream play %d %d\n", stream, reverbDepth);
}
void FakeInitialiseStreamUpdate(void) {
    ShadowLog("initialise stream update\n");
}
int FakeInitialiseStream(int hashCode, int is3d) {
    ShadowLog("initialise stream %d %d\n", hashCode, is3d);
    return answers.initialiseStream;
}

void InstallFakes(void) {
    ShadowHook_Install(0x000e0a80, (const void *)&FakeGetTimer);
    ShadowHook_Install(0x000e0ab0, (const void *)&FakeSetReverb);
    ShadowHook_Install(0x000e0b50, (const void *)&FakeSetupListener);
    ShadowHook_Install(0x000e0dc0, (const void *)&FakeSetVolume);
    ShadowHook_Install(0x000e0a60, (const void *)&FakeMarkInactive);
    ShadowHook_Install(0x000e0bc0, (const void *)&FakeIsSamplePlaying);
    ShadowHook_Install(0x000e0a00, (const void *)&FakeRequestVoiceHandle);
    ShadowHook_Install(0x000e0db0, (const void *)&FakeSamplePause);
    ShadowHook_Install(0x000e0a70, (const void *)&FakeUnPause);
    ShadowHook_Install(0x000e09c0, (const void *)&FakeSFXStop);
    ShadowHook_Install(0x000e0ed0, (const void *)&FakeInitialiseSound);
    ShadowHook_Install(0x000e0e10, (const void *)&FakeUpdateSound);
    ShadowHook_Install(0x000e0b00, (const void *)&FakeSampleKeyOn);
    ShadowHook_Install(0x000e0a90, (const void *)&FakeLoadFile);
    ShadowHook_Install(0x000e09e0, (const void *)&FakeFreeFileMem);
    ShadowHook_Install(0x000e0d30, (const void *)&FakeDeleteChannel);
    ShadowHook_Install(0x000e0cd0, (const void *)&FakeCreatePlaybackBuffer);
    ShadowHook_Install(0x000e0cb0, (const void *)&FakeCreateBuffer);
    ShadowHook_Install(0x000e0c20, (const void *)&FakeAsyncReadFile);
    ShadowHook_Install(0x000e0c00, (const void *)&FakeAsyncCloseFile);
    ShadowHook_Install(0x000e0da0, (const void *)&FakeStreamStop);
    ShadowHook_Install(0x000e0be0, (const void *)&FakeAsyncOpenFile);
    ShadowHook_Install(0x000e0c90, (const void *)&FakeAsyncHasReadFinished);
    ShadowHook_Install(0x000e0de0, (const void *)&FakeGetPlayPos);
    ShadowHook_Install(0x000e0df0, (const void *)&FakeStreamClear);
    ShadowHook_Install(0x000e0dd0, (const void *)&FakeStreamWrite);
    ShadowHook_Install(0x000e0d60, (const void *)&FakeStreamPlay);
}

// The rest of the layer, for the groups testing what calls it
void InstallLayerFakes(void) {
    ShadowHook_Install(0x000c8c40, (const void *)&FakeStopStream);
    ShadowHook_Install(0x000c8fa0, (const void *)&FakeInitialiseStream);
    ShadowHook_Install(0x000ca620, (const void *)&FakeUpdateMusicalEvents);
    ShadowHook_Install(0x000c9030, (const void *)&FakeUpdateStreams);
    ShadowHook_Install(0x000c5ea0, (const void *)&FakeReleaseStreamSystem);
    ShadowHook_Install(0x000c5f10, (const void *)&FakeInitialiseStreamSystem);
}

// ---- the made-up state

const int kSounds[] = {SFX_MUSIC_FRONT_END_LOOP_01, SFX_MUSIC_FRONT_END_LOOP_02, 0x14f, 0x89, 0x202, 0x09,
                       0x172, 0x10, 0x11, 0x12, 0x13, 0x14};

int SomeSound(void) {
    switch (R(8)) {
        case 0: return -1;
        case 1: return (int)R(0x700);
        default: return kSounds[R(COUNT(kSounds))];
    }
}

DYNAMICSOUNDS *SomeHandle(void) {
    return R(5) == 0 ? NULL : (DYNAMICSOUNDS *)world.handles[R(HANDLES)];
}

void RandomDef(Def *d) {
    SFXParameters *p = &d->p;
    p->ReverbSend = RI(0, 100);
    p->TrackingType = RI(0, 3);
    p->InnerRadiusReal = RI(0, 20);
    p->OuterRadiusReal = RI(0, 60);
    p->MaxVoices = R(2) ? 0 : RI(1, 4);
    p->Priority = RI(0, 150);
    p->Group = RI(0, 3);
    p->MaxReject = (short)R(2);
    p->Action2 = (short)R(3);
    p->IgnoreAge = (short)R(2);
    p->Ducker = R(2) ? 0 : (short)RI(1, 60);
    p->DuckerLength = RI(0, 3000);
    p->MasterVolume = RI(0, 100);
    p->MinDelay = RI(0, 5);
    p->MaxDelay = RI(0, 10);
    p->MultiSample = (short)R(2);
    p->RandomPick = (short)R(2);
    p->Shuffled = (short)R(2);
    p->Loop = (short)R(2);
    p->Polyphonic = (short)(R(3) == 0);
    p->Outdoors = (char)R(2);
    p->PauseInNis = (char)R(2);
    // A sequence of none would play the word before its cued samples, in the original too
    p->SampleCount = RI(R(6) == 0 && !p->MultiSample ? 0 : 1, POOL);
    for (int i = 0; i < POOL; i++) {
        SFXSamplePoolFiles *s = &d->pool[i];
        s->FileRef = R(8) == 0 ? -RI(1, 4) : RI(0, SAMPLES - 1);
        s->PitchOffset = RI(-0x3000, 0x3000);
        s->RandomPitchOffset = RI(0, 0x800);
        s->BaseVolume = RI(-10, 120);
        s->RandomVolumeOffset = RI(0, 100);
        s->Pan = RI(-120, 120);
        s->RandomPan = RI(0, 100);
    }
}

SFXParameters *SomeDef(void) {
    return &world.banks[R(BANKS)].defs[R(DEFS)].p;
}

void RandomBanks(void) {
    memset(SoundBankData, 0, sizeof(SoundBankData));
    for (int b = 0; b < SFX_NUM_BANKS; b++)
        SBInfoList[b] = R(4) == 0 ? -1 : (int)R(0x40);
    for (int b = 0; b < BANKS; b++) {
        BankFile *f = &world.banks[b];
        f->count = RI(0, BANK_SFX);
        for (int i = 0; i < BANK_SFX; i++) {
            f->headers[i].HashCode = kSounds[R(COUNT(kSounds))];
            f->headers[i].Offset = (int)((uintptr_t)&f->defs[R(DEFS)] - (uintptr_t)f);
        }
        for (int i = 0; i < DEFS; i++)
            RandomDef(&f->defs[i]);
        for (int i = 0; i < SAMPLES; i++) {
            SampleHeaderData *s = &world.samples[b][i];
            memset(s, 0, sizeof(*s));
            s->Flags = (int)R(2);
            s->Size = RI(0, 0x4000);
            s->Frequency = RI(11025, 48000);
            s->NumberOfChannels = RI(1, 2);
            s->LoopOffset = RI(0, 0x100);
        }
        // The sounds' SoundBankSlot is 0 to 2, so the banks are there, but one is sometimes elsewhere
        int slot = b == 2 && R(3) == 0 ? (int)R(SFX_NUM_BANKS) : b;
        SoundBankHeader *h = &SoundBankData[slot];
        h->Loaded = R(5) != 0;
        h->HashCode = SBInfoList[slot];
        h->SFXBuffer = &f->count;
        h->SFXCount = f->count;
        h->SFXHeaderData = f->headers;
        h->SampleHeader = world.samples[b];
        h->SampleCount = SAMPLES;
        SoundBankData[b].SampleHeader = world.samples[b];
    }
}

void RandomVoices(void) {
    for (int i = 0; i < SFX_NUM_VOICE_HANDLES; i++) {
        ES_VoiceHandleDetails *v = &ES_VoiceHandleData[i];
        v->IsStream = R(8) == 0;
        v->Handle = v->IsStream ? 0 : RI(0, 63);
        v->Status = R(3) != 0;
    }
}

int SomeVoice(void) {
    return R(3) == 0 ? -1 : RI(0, SFX_NUM_VOICE_HANDLES - 1);
}

void RandomItem(SFXItem *s) {
    s->HashCode = SomeSound();
    s->Counter = RI(0, 50);
    s->StreamHandle = RI(-1, 3);
    for (int i = 0; i < SFX_MAX_VOICES; i++) {
        s->VoiceHandle[i] = R(3) == 0 ? SomeVoice() : -1;
        s->BaseVolume[i] = RI(0, 100);
        s->Pitch[i] = RI(0, 48000);
        s->Pan[i] = RI(-100, 100);
    }
    s->Volume3D = RI(0, 100);
    s->SFXData = SomeDef();
    s->CuePos = RI(0, s->SFXData->SampleCount);
    for (int i = 0; i < SFX_MAX_CUED; i++) {
        s->CueSampleWaiting[i] = RI(-2, 4);
        s->CuedSamples[i] = &((Def *)s->SFXData)->pool[R(POOL)];
    }
    s->SoundBankSlot = RI(0, 2);
    s->UpdatedCounter = RI(0, 3);
    RandomVector(&s->RealPos, 500.0f);
    RandomVector(&s->pos, 500.0f);
    RandomVector(&s->vel, 5.0f);
    s->Handle = SomeHandle();
    s->VolumeOverRide = RI(0, 100);
    s->OuterRadius = RI(0, 60);
    s->InnerRadius = RI(0, 30);
    s->DeleteMe = R(6) == 0;
    s->Paused = R(4) == 0;
    s->StartedWhenPaused = R(2);
    s->StartedWhenNisPlaying = R(2);
    s->FadeAndKill = R(4) == 0;
    s->FadeAndKillVolume = RI(0, 100);
    s->Specials.Volume3D = RI(0, 100);
    s->KeyOnFrameDelay = R(3) == 0 ? RI(0, 3) : 0;
    s->FirstTransferComplete = R(2);
    s->IsStreamed = R(5) == 0;
}

// Some sounds in the list in a random order, some in the free list, the rest in neither
void RandomItems(void) {
    int order[SFX_NUM_ITEMS];
    for (int i = 0; i < SFX_NUM_ITEMS; i++)
        order[i] = i;
    for (int i = SFX_NUM_ITEMS - 1; i > 0; i--) {
        int j = (int)R((uint32_t)i + 1);
        int t = order[i];
        order[i] = order[j];
        order[j] = t;
    }
    int active = R(4) == 0 ? 0 : RI(1, 14);
    int free = R(5) == 0 ? 0 : RI(1, 6);
    if (R(20) == 0)
        free = SFX_NUM_ITEMS - active;
    SFXItem *prev = NULL;
    BaseSFXActive = NULL;
    for (int i = 0; i < active; i++) {
        SFXItem *s = &SFXItems[order[i]];
        RandomItem(s);
        s->Last = prev;
        s->Next = NULL;
        if (prev != NULL)
            prev->Next = s;
        else
            BaseSFXActive = s;
        prev = s;
    }
    SFXFreeList = NULL;
    for (int i = active; i < active + free; i++) {
        SFXItem *s = &SFXItems[order[i]];
        RandomItem(s);
        s->Next = SFXFreeList;
        s->Last = NULL;
        SFXFreeList = s;
    }
}

void RandomRequests(void) {
    for (int i = 0; i < SFX_NUM_ITEMS; i++) {
        SFXRequest *r = &SFXRequestedActiveData[i];
        r->HashCode = SomeSound();
        r->Handle = SomeHandle();
        r->Life = R(4) == 0 ? RI(-1, 5) : 0;
    }
    SFXActiveCount = R(4) == 0 ? 0 : RI(0, 12);
    for (int i = 0; i < SFX_NUM_ITEMS; i++) {
        SFXActiveData[i].HashCode = SomeSound();
        SFXActiveData[i].Handle = SomeHandle();
    }
}

void RandomMarker(TestMarker *m) {
    m->CheckMe = R(2);
    m->EndMustToggle = R(2);
    m->EndHere = R(6) == 0 ? -1 : RI(0, 0x4800);
}

void RandomMarkerFile(void) {
    StreamMarkerHeaderData *h = &world.markerFile.header;
    h->StartMarkerCount = STARTS;
    h->MarkerCount = MARKERS;
    h->StartMarkerOffset = (int)((uintptr_t)world.markerFile.starts - (uintptr_t)h);
    h->MarkerOffset = (int)((uintptr_t)world.markerFile.markers - (uintptr_t)h);
    h->BaseVolume = RI(0, 100);
    int pos = RI(0, 0x2000);
    static const int types[] = {0, 1, 5, 6, 7, 9, 2, 2, 2, 2};
    for (int i = 0; i < MARKERS; i++) {
        MusicMarkerData *m = &world.markerFile.markers[i];
        pos += RI(0, 0x3000);
        m->Name = RI(0, STARTS - 1);
        m->Pos = i == MARKERS - 1 ? 0x7fffffff : pos;
        m->MType = types[R(COUNT(types))];
        m->Flags = R(4);
        m->Extra = R(4);
        m->LoopStart = RI(0, pos);
        m->MarkerCount = RI(0, MARKERS - 1);
        m->LoopMarkerCount = RI(0, MARKERS - 2);
    }
    for (int i = 0; i < STARTS; i++) {
        MusicMarkerStartData *s = &world.markerFile.starts[i];
        s->Marker = world.markerFile.markers[R(MARKERS - 1)];
        s->MarkerPos = RI(0, MARKERS - 2);
        s->IsInstant = R(2);
        s->InstantBuffer = R(2);
        s->State[0] = (int)R(0);
        s->State[1] = (int)R(0);
    }
}

void RandomStream(void) {
    StreamData *s = &Streams[0];
    static const int status[] = {0, 1, 2, 3, 4, 5, 9, 10, 2, 2, 4};
    RandomMarkerFile();
    FileBufferMask = R(2) ? 3 : 7;
    s->FileHandle = RI(-1, 5);
    s->StreamHandle2 = R(2) ? -1 : RI(0, 63);
    s->BufferToggle = R(2);
    s->Test1 = R(2);
    s->SoftwareAdpcm = R(3) == 0;
    s->Frequency = R(2) ? 44100 : 22050;
    s->FileOffset = RI(0, 0x10000);
    s->Is3d = R(2);
    s->SetJumpRequestCompleteTimer = RI(0, 3);
    s->SetEndMarker = R(2);
    s->SetCanJumpMarker = R(2);
    s->SetJumpRequestCompleteMarker = R(2);
    RandomMarker(&s->EndMarker);
    RandomMarker(&s->CanJumpMarker);
    RandomMarker(&s->JumpRequestCompleteMarker);
    RandomMarker(&s->DataEndMarker);
    s->EndingSoon = R(3) == 0;
    s->FilePos = RI(0, 0x40000);
    s->LastFilePos = R(2) ? s->FilePos : RI(0, 0x40000);
    s->TimedOut = R(4) == 0 ? RI(195, 210) : RI(0, 5);
    s->DataBuffer2 = world.dataBuffer2;
    s->BufferSize = R(2) ? 0x4800 : 0x2400;
    s->PlayBackBufferSize = R(4) == 0 ? RI(0x100, 0x4800) : s->BufferSize;
    s->PlayBackPos = RI(0, s->PlayBackBufferSize);
    s->LastPlayBackPos = RI(0, s->PlayBackBufferSize);
    for (int i = 0; i < STREAM_READ_BUFFERS; i++) {
        StreamReadData *r = &s->Read[i];
        r->DataBuffer = world.readBuffers[i];
        r->FilePos = RI(0, 0x40000);
        r->ByteOffset = R(4) == 0 ? s->PlayBackBufferSize : RI(0, s->PlayBackBufferSize);
        r->ByteSize = R(5) == 0 ? 0 : RI(0, s->BufferSize);
        r->SetToBlank = R(3) == 0;
        r->NoDataToRead = R(3) == 0;
        r->State[0] = (int)R(0);
        r->State[1] = (int)R(0);
        r->GetState[0] = (int)R(0);
        r->GetState[1] = (int)R(0);
        r->StateSet = R(2);
        r->UseThisState = R(2);
        RandomMarker(&r->EndMarker);
        RandomMarker(&r->CanJumpMarker);
        RandomMarker(&r->JumpRequestCompleteMarker);
        r->MarkerPassed = R(3) == 0;
        r->MarkerPassedByteSize = RI(0, 0x2400);
    }
    s->FileBufferCount = FileBufferMask + 1;
    s->LastFileBuffersFull = RI(0, 7);
    s->FileBuffersFull = RI(0, 7);
    s->ReadBufferNumber = RI(0, (int)FileBufferMask);
    s->TransferBufferNumber = R(4) == 0 ? s->ReadBufferNumber : RI(0, (int)FileBufferMask);
    s->State[0] = (int)R(0);
    s->State[1] = (int)R(0);
    s->ShutDown = R(2);
    s->ShutDownCounter = RI(25, 35);
    s->StreamMarkerHeader = &world.markerFile.header;
    s->MusicMarker = &world.markerFile.markers[R(MARKERS / 2)];
    s->JumpMusicMarker = &world.markerFile.markers[R(MARKERS)];
    s->MusicMarkerStart = &world.markerFile.starts[R(STARTS)];
    s->BaseMusicMarker = world.markerFile.markers;
    s->BaseMusicMarkerStart = world.markerFile.starts;
    s->StreamJumpTo = R(2) ? -1 : RI(0, STARTS - 1);
    s->HasJustJumped = R(2);
    s->HasJustRequestedJump = R(2);
    s->HasJustLoadedJump = R(2);
    s->HasJustRequestedLoadedJump = R(2);
    StreamMemCpyHandle *c = &StreamMemCpyHandles[0];
    c->StreamHandle = RI(0, 63);
    c->StreamByteOffset = RI(0, s->PlayBackBufferSize);
    c->SrcData = world.readBuffers[R(STREAM_READ_BUFFERS)];
    c->SrcByteSize = RI(0, 0x4800);
    c->TransferPos = R(3) == 0 ? c->SrcByteSize : RI(0, c->SrcByteSize);
    c->TransferedData = RI(0, 0x4800);
    c->TransferStatus = R(2) ? STREAM_COPYING : RI(0, 9);
    c->Stream = s;
    for (int i = 0; i < SFX_NUM_STREAM_REQUESTS; i++) {
        StreamIntialiseDetails *w = &StreamWaitingIntialise[i];
        w->HashCode = RI(-3, 40);
        w->ChannelNumber = 0;
        w->Is3d = R(2);
        w->Status = status[R(COUNT(status))];
        w->KeyOn = R(2);
        w->ReverbDepth = RI(0, 100);
    }
    StreamWaitingIntialiseCount = RI(0, SFX_NUM_STREAM_REQUESTS - 1);
    InitialiseStatus = R(2) ? 2 : 3;
    EnableReleaseAudioStreamSystem = R(2);
    DoInitialStreamBuffer = R(2);
    HaveCalledOnce = R(2);
    MusicStartSection = RI(0, STARTS - 1);
    SFXLoadedMarker = RI(-1, STARTS - 1);
    SFXActiveMarker = RI(-1, STARTS - 1);
    SFXJumpMarker = R(3) == 0 ? -1 : RI(0, STARTS - 1);
    SFXJumpInstant = R(2);
    AllStreamsPaused = R(2);
    StreamWaitingIntialiseMe = R(3) == 0 ? StreamWaitingIntialiseCount : RI(0, SFX_NUM_STREAM_REQUESTS - 1);
    InitialisingStream = s;
    InitialisingHashCode = RI(0, 3);
    for (int i = 0; i < 4; i++) {
        world.lookup[i].MarkerOffset = RI(0, 0x10000);
        world.lookup[i].MarkerSize = RI(0, 0x1000);
        world.lookup[i].AudioOffset = RI(0, 0x100000);
        world.lookup[i].AudioSize = RI(0, 0x100000);
    }
    StreamLookupFileData = world.lookup;
    for (int i = 0; i < COUNT(world.dataBuffer2); i++)
        world.dataBuffer2[i] = (char)R(0x10);
    // A streamed sound's markers are read into it
    StreamMarkerHeaderData *h = (StreamMarkerHeaderData *)world.dataBuffer2;
    *h = world.markerFile.header;
    h->StartMarkerOffset = (int)((uintptr_t)world.markerFile.starts - (uintptr_t)h);
    h->MarkerOffset = (int)((uintptr_t)world.markerFile.markers - (uintptr_t)h);
    SFXJumpState = RI(0, 3);
    SFXLastJumpMarker = RI(-1, STARTS - 1);
    s->IdNumber = 0;
    s->HashCode = RI(-5, 40);
    s->Status = status[R(COUNT(status))];
    s->StreamHandle = RI(0, 63);
    s->Channels = RI(1, 2);
    s->PauseNow = R(2);
    s->Paused = R(2);
    s->GamePausedMe = R(3) == 0 ? RI(0, 2) : 0;
    s->BaseVolume = RI(0, 100);
    s->MusicStream = R(2);
    s->FinishedInitialise = R(4) != 0;
    s->StartedPlaying = R(2);
    s->AllowStartedPlaying = R(2);
    s->KeyOn = R(2);
    s->ReverbDepth = RI(0, 100);
    s->ShutDownVolume = RI(0, 100);
    s->BeenStolen = R(3) == 0;
    s->BeenStolenByInitialiseRef = RI(0, SFX_NUM_STREAM_REQUESTS - 1);
}

void RandomGlobals(void) {
    ES_MuteMusic = R(5) != 0;
    ES_MuteSFX = R(5) != 0;
    SFXModeNumber = RI(0, 3);
    MusicMasterVolume = RI(0, 100);
    DuckerDuration = RI(0, 3000);
    DuckerTargetVolume = RI(0, 100);
    DuckerCurrentVolume = RI(0, 100);
    SFXVolume = RI(0, 100);
    FadeVolume = R(4) == 0 ? 10000 : RI(0, 10000);
    FadeMusicVolume = RI(0, 10000);
    FadeRate = R(4) == 0 ? 120 : RI(1, 200);
    FadeMusic = R(2);
    SFXNewIndoorsVolume = R(2) ? 50 : 100;
    SFXIndoorsVolume = RI(40, 110);
    sfx_randomseed1 = R(0);
    sfx_randomseed2 = R(0);
    SFXListenerCount = RI(1, 4);
    for (int i = 0; i < SFX_NUM_LISTENERS; i++)
        RandomVector(&ListenerPositions[i], 500.0f);
    RandomVector(&ListenerNorm, 1.0f);
    RandomVector(&ListenerDir, 1.0f);
    RandomVector(&ListenerUp, 1.0f);
    RandomVector(&ListenerPos, 500.0f);
    RandomVector(&ListenerVel, 5.0f);
    for (int i = 0; i < SFX_NUM_MUSIC_TRACKS; i++)
        MusicTrackInfo[i] = RI(0, 60);
    SFXLanguage = RI(-1, 6);
    SFXUnderWater = R(3) == 0;
    SFXNewReverb = RI(-10, 110);
    SFXReverb = RI(-10, 110);
    static const int fade[] = {-1, 0, 1, 2, -3};
    FadeStatus = fade[R(COUNT(fade))];
    FadeStartTimer = (int)R(0);
    SFXFileAccessSuspended = R(3) == 0;
    SFXFileAccessSuspendedToggle = R(2);
    SFXStartLoad = RI(-10, 2500);
    SFXPaused = R(4) == 0;
    NisPlaying = R(4) == 0;
    NisQuitten = R(4) == 0 ? RI(1, 5) : 0;
    SFXKeyOnFrameDelay = R(3) == 0 ? RI(1, 4) : 0;
}

void RandomWorld(void) {
    RandomBanks();
    RandomVoices();
    RandomItems();
    RandomRequests();
    RandomStream();
    RandomGlobals();
    for (int i = 0; i < 6; i++)
        RandomVector(&world.vectors[i], R(2) ? 1.0f : 500.0f);
    int n = (int)R(sizeof(world.path) - 1);
    for (int i = 0; i < n; i++)
        world.path[i] = (char)RI(0x20, 0x7e);
    world.path[n] = 0;
    answers.timer = (int)R(0);
    answers.timerStep = R(2) ? 0 : RI(0, 500);
    answers.playingMask[0] = R(0);
    answers.playingMask[1] = R(0);
    answers.requestVoice = R(4) == 0 ? -1 : RI(0, 63);
    answers.initialiseStream = R(2) ? -1 : 0;
    answers.loadFails = R(3) == 0 ? R(0) & R(0) : 0;
    answers.loads = 0;
    answers.playbackBuffer = R(5) == 0 ? -1 : RI(0, 63);
    answers.openFile = R(5) == 0 ? -1 : RI(0, 5);
    answers.readFinished = R(0);
    answers.finishCalls = 0;
    answers.buffers = 0;
    world.sfxFile.count = RI(0, BANK_SFX);
    for (int i = 0; i < BANK_SFX; i++) {
        world.sfxFile.headers[i].HashCode = kSounds[R(COUNT(kSounds))];
        world.sfxFile.headers[i].Offset = (int)((uintptr_t)&world.sfxFile.defs[R(2)] - (uintptr_t)&world.sfxFile);
    }
    RandomDef(&world.sfxFile.defs[0]);
    RandomDef(&world.sfxFile.defs[1]);
    world.shfFile.count = R(4) == 0 ? 0 : RI(1, SAMPLES);
    for (int i = 0; i < SAMPLES; i++) {
        SampleHeaderData *s = &world.shfFile.samples[i];
        s->Flags = (int)R(2);
        s->Address = (void *)(uintptr_t)R(0x100);
        s->Size = RI(0, 0x10);
        s->Frequency = RI(11025, 48000);
        s->psi_SampleHeader = (char *)(uintptr_t)R(0x100);
    }
    for (int i = 0; i < SFX_NUM_BANKS; i++)
        world.sbinfo[i] = R(4) == 0 ? -1 : (int)R(0x40);
    static const int levels[] = {0x7000001, 0x7000002, 0x7000003, 0x7000004, 0x7000005, 0x7000006, 0x7000007,
                                 0x7000008, 0x7000009, 0x700000a, 0x700000b, 0x700000c, 0x700000d, 0x700000e,
                                 0x7000011, 0x7000012, 0x7000013, 0x7000014, 0x7000015, 0x7000016, 0x700001a,
                                 0x700001b, 0x700001c, 0x7000021, 0x7000025, 0x7000029, 0x700002a, 0x7000041,
                                 0x7000043, 0x7000048, 0x700004a, 0x700004b, 0x700004c, 0x700004d, 0x7000090, 0};
    I32_AT(XbeOurs(0x001f658c)) = levels[R(COUNT(levels))];
    I32_AT(XbeOurs(0x00260018)) = R(2);
    I32_AT(XbeOurs(0x00260044)) = RI(1, 4);
    U8_AT(XbeOurs(0x0017d581)) = (uint8_t)R(2);
    SFXInitialised = R(5) == 0;
    if (R(8) == 0)
        for (int i = 0; i < SFX_NUM_VOICE_HANDLES; i++)
            ES_VoiceHandleData[i].Status = R(30) != 0;
    answers.playPos = RI(0, 0x9000);
    if (R(2))
        FadeStartTimer = answers.timer - RI(-100, 2500);
}

SFXItem *SomeItem(void) {
    SFXItem *list[SFX_NUM_ITEMS];
    int n = 0;
    for (SFXItem *s = BaseSFXActive; s != NULL && n < SFX_NUM_ITEMS; s = s->Next)
        list[n++] = s;
    if (n == 0 || R(6) == 0) {
        SFXItem *s = &SFXItems[R(SFX_NUM_ITEMS)];
        RandomItem(s);
        return s;
    }
    return list[R(n)];
}

// ---- the cases: the settings, the mixer and the listeners

uint32_t RunRememberRequest(bool original) {
    int hashCode = SomeSound();
    DYNAMICSOUNDS *handle = SomeHandle();
    if (R(4) == 0)
        for (int i = 0; i < SFX_NUM_ITEMS; i++)
            SFXRequestedActiveData[i].Life = R(10) == 0 ? 0 : 1;
    CDECL_VOID(0x000c4b70, SFXRememberRequest, void (__cdecl *)(int, DYNAMICSOUNDS *), hashCode, handle);
    return 0;
}

uint32_t RunAgeRequests(bool original) {
    CDECL_VOID(0x000c4bc0, SFXAgeRequests, void (__cdecl *)(void));
    return 0;
}

uint32_t RunIsSFXPlaying(bool original) {
    DYNAMICSOUNDS *handle = SomeHandle();
    int hashCode = SomeSound();
    return CDECL_RUN(0x000c4c40, SFXIsSFXPlaying, int (__cdecl *)(DYNAMICSOUNDS *, int), handle, hashCode);
}

uint32_t RunSetEnvironment(bool original) {
    int environment = R(6) == 0 ? SFX_ENVIRONMENT_UNDERWATER : RI(-200, 200);
    int outdoors = R(2) ? 0 : (int)R(0);
    CDECL_VOID(0x000c4ed0, SFXSetEnvironment, void (__cdecl *)(int, int), environment, outdoors);
    return 0;
}

uint32_t RunUpdateEnvironment(bool original) {
    CDECL_VOID(0x000c4f20, SFXUpdateEnvironment, void (__cdecl *)(void));
    return 0;
}

uint32_t RunFadeUpdate(bool original) {
    CDECL_VOID(0x000c4f90, SFXFadeUpdate, void (__cdecl *)(void));
    return 0;
}

uint32_t RunFadeDown(bool original) {
    int music = R(3) == 0 ? (int)R(0) : (int)R(2);
    CDECL_VOID(0x000c5030, SFXFadeDown, void (__cdecl *)(int), music);
    return 0;
}

uint32_t RunSetVolume(bool original) {
    int volume = RI(-50, 150);
    CDECL_VOID(0x000c5060, SFXSetVolume, void (__cdecl *)(int), volume);
    return 0;
}

uint32_t RunGetVolume(bool original) {
    return CDECL_RUN(0x000c5090, SFXGetVolume, int (__cdecl *)(void));
}

uint32_t RunIsUnderWater(bool original) {
    SFXItem *s = SomeItem();
    if (R(2))
        s->HashCode = (int)R(0x600);
    return CDECL_RUN(0x000c50a0, IsUnderWaterSFX, int (__cdecl *)(SFXItem *), s);
}

uint32_t RunCombineVolumes(bool original) {
    SFXItem *s = SomeItem();
    int volume = RI(0, 100);
    return CDECL_RUN(0x000c50d0, ES_CombineVolumes, int (__cdecl *)(int, SFXItem *), volume, s);
}

uint32_t RunSetPositionAndVelocity(bool original) {
    DYNAMICSOUNDS *handle = SomeHandle();
    int hashCode = SomeSound();
    CDECL_VOID(0x000c5250, SFXSetPositionAndVelocity, void (__cdecl *)(_VECTOR *, _VECTOR *, DYNAMICSOUNDS *, int),
               &world.vectors[0], &world.vectors[1], handle, hashCode);
    return 0;
}

uint32_t RunSetFilePath(bool original) {
    CDECL_VOID(0x000c52e0, SFXSetFilePath, void (__cdecl *)(char *), world.path);
    return 0;
}

uint32_t RunUpdateLanguageName(bool original) {
    CDECL_VOID(0x000c5350, UpdateLanguageName, void (__cdecl *)(void));
    return 0;
}

uint32_t RunGetLanguage(bool original) {
    return CDECL_RUN(0x000c53f0, GetSFXLanguage, int (__cdecl *)(void));
}

uint32_t RunSetLanguage(bool original) {
    int language = RI(-2, 7);
    CDECL_VOID(0x000c5400, SFXSetLanguage, void (__cdecl *)(int), language);
    return 0;
}

uint32_t RunSetMode(bool original) {
    int mode = RI(-2, 7);
    CDECL_VOID(0x000c5410, SFXSetMode, void (__cdecl *)(int), mode);
    return 0;
}

uint32_t RunGetMode(bool original) {
    return CDECL_RUN(0x000c5430, SFXGetMode, int (__cdecl *)(void));
}

uint32_t RunSetupListener(bool original) {
    _VECTOR *v = world.vectors;
    int n = RI(0, SFX_NUM_LISTENERS - 1);
    CDECL_VOID(0x000c5cd0, SFXSetupListener,
               void (__cdecl *)(_VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, _VECTOR *, int), &v[0], &v[1], &v[2],
               &v[3], &v[4], n);
    return 0;
}

uint32_t RunSetNumberOfListeners(bool original) {
    int n = RI(-1, 4);
    CDECL_VOID(0x000c5e30, SFXSetNumberOfListeners, void (__cdecl *)(int), n);
    return 0;
}

uint32_t RunSuspendFileAccess(bool original) {
    CDECL_VOID(0x000cb140, SFXSuspendFileAccess, void (__cdecl *)(void));
    return 0;
}

uint32_t RunUnSuspendFileAccess(bool original) {
    CDECL_VOID(0x000c6900, SFXUnSuspendFileAccess, void (__cdecl *)(void));
    return 0;
}

uint32_t RunFindMusicTrack(bool original) {
    int track = RI(-2, 62);
    return CDECL_RUN(0x000c5e70, FindMusicTrackNumber, int (__cdecl *)(int), track);
}

uint32_t RunMusicSetVolumeES(bool original) {
    CDECL_VOID(0x000c62a0, ES_MusicSetVolume, void (__cdecl *)(void));
    return 0;
}

uint32_t RunMusicSetVolume(bool original) {
    int volume = RI(-20, 120);
    CDECL_VOID(0x000c6350, SFXMusicSetVolume, void (__cdecl *)(int), volume);
    return 0;
}

uint32_t RunMusicGetVolume(bool original) {
    return CDECL_RUN(0x000c6360, SFXMusicGetVolume, int (__cdecl *)(void));
}

const Case kSettings[] = {
    C("SFXRememberRequest", RunRememberRequest),
    C("SFXAgeRequests", RunAgeRequests),
    C("SFXIsSFXPlaying", RunIsSFXPlaying),
    C("SFXSetEnvironment", RunSetEnvironment),
    C("SFXUpdateEnvironment", RunUpdateEnvironment),
    C("SFXFadeUpdate", RunFadeUpdate),
    C("SFXFadeDown", RunFadeDown),
    C("SFXSetVolume", RunSetVolume),
    C("SFXGetVolume", RunGetVolume),
    C("IsUnderWaterSFX", RunIsUnderWater),
    C("ES_CombineVolumes", RunCombineVolumes),
    C("SFXSetPositionAndVelocity", RunSetPositionAndVelocity),
    C("SFXSetFilePath", RunSetFilePath),
    C("UpdateLanguageName", RunUpdateLanguageName),
    C("GetSFXLanguage", RunGetLanguage),
    C("SFXSetLanguage", RunSetLanguage),
    C("SFXSetMode", RunSetMode),
    C("SFXGetMode", RunGetMode),
    C("SFXSetupListener", RunSetupListener),
    C("SFXSetNumberOfListeners", RunSetNumberOfListeners),
    C("SFXUnSuspendFileAccess", RunUnSuspendFileAccess),
    C("FindMusicTrackNumber", RunFindMusicTrack),
    C("ES_MusicSetVolume", RunMusicSetVolumeES),
    C("SFXMusicSetVolume", RunMusicSetVolume),
    C("SFXMusicGetVolume", RunMusicGetVolume),
};

// ---- the voices and the sounds' list

uint32_t RunFindFreeVoiceHandle(bool original) {
    return CDECL_RUN(0x000c4ea0, ES_FindFreeVoiceHandle, int (__cdecl *)(void));
}

uint32_t RunUnlockVoiceHandle(bool original) {
    int voice = SomeVoice();
    CDECL_VOID(0x000c8810, ES_UnlockVoiceHandle, void (__cdecl *)(int), voice);
    return 0;
}

uint32_t RunHasSampleEnded(bool original) {
    int voice = SomeVoice();
    SFXItem *s = SomeItem();
    return CDECL_RUN(0x000c8870, HasSampleEnded, int (__cdecl *)(int, SFXItem *), voice, s);
}

uint32_t RunRequestVoiceHandle(bool original) {
    int fileRef = RI(-3, SAMPLES - 1);
    int is3d = (int)R(2);
    SampleHeaderData *samples = world.samples[R(BANKS)];
    // The original keeps the voice in EDX across ES_FindFreeVoiceHandle, whose original does not write it
    if (original) {
        XbeOriginalScope find(0x000c4ea0);
        return CDECL_RUN(0x000c97f0, ES_RequestVoiceHandle, int (__cdecl *)(SampleHeaderData *, int, int, int),
                         samples, fileRef, is3d, 0);
    }
    return CDECL_RUN(0x000c97f0, ES_RequestVoiceHandle, int (__cdecl *)(SampleHeaderData *, int, int, int), samples,
                     fileRef, is3d, 0);
}

uint32_t RunSFXIsReady(bool original) {
    int voice = SomeVoice();
    return CDECL_RUN(0x000c9880, ES_SFXIsReady, int (__cdecl *)(int), voice);
}

uint32_t RunPauseSfx(bool original) {
    SFXItem *s = SomeItem();
    CDECL_VOID(0x000c98e0, ES_PauseSfx, void (__cdecl *)(SFXItem *), s);
    return 0;
}

uint32_t RunPause(bool original) {
    CDECL_VOID(0x000c9960, SFXPause, void (__cdecl *)(void));
    return 0;
}

uint32_t RunUnPauseSfx(bool original) {
    SFXItem *s = SomeItem();
    CDECL_VOID(0x000c9990, ES_UnPauseSfx, void (__cdecl *)(SFXItem *), s);
    return 0;
}

uint32_t RunUnPause(bool original) {
    CDECL_VOID(0x000c9a10, SFXUnPause, void (__cdecl *)(void));
    return 0;
}

uint32_t RunKillSFX(bool original) {
    SFXItem *s = SomeItem();
    if (s->Next == NULL && s->Last == NULL && s != BaseSFXActive)
        return 0;
    CDECL_VOID(0x000c9a40, KillSFX, void (__cdecl *)(SFXItem *), s);
    return 0;
}

uint32_t RunRemove(bool original) {
    DYNAMICSOUNDS *handle = SomeHandle();
    int hashCode = R(2) ? -1 : SomeSound();
    CDECL_VOID(0x000c9b10, SFXRemove, void (__cdecl *)(DYNAMICSOUNDS *, int), handle, hashCode);
    return 0;
}

uint32_t RunFillpsiSFXData(bool original) {
    SFXItem *s = SomeItem();
    int n = RI(0, SFX_MAX_VOICES - 1);
    if (s->VoiceHandle[n] == -1)
        s->VoiceHandle[n] = RI(0, SFX_NUM_VOICE_HANDLES - 1);
    CDECL_VOID(0x000c88d0, ES_FillpsiSFXData, void (__cdecl *)(psiSFX *, SFXItem *, int), &world.psi, s, n);
    return 0;
}

uint32_t RunStreamKeyOn(bool original) {
    int reverb = RI(0, 100);
    CDECL_VOID(0x000c61b0, ES_StreamKeyOn, void (__cdecl *)(int, int), 0, reverb);
    return 0;
}

uint32_t RunPauseAllStreams(bool original) {
    CDECL_VOID(0x000c6200, SFXPauseAllStreams, void (__cdecl *)(void));
    return 0;
}

uint32_t RunUnPauseAllStreams(bool original) {
    CDECL_VOID(0x000c6250, SFXUnPauseAllStreams, void (__cdecl *)(void));
    return 0;
}

// SFXUpdate itself a fake
const Case kSuspend[] = {
    C("SFXSuspendFileAccess", RunSuspendFileAccess),
};

void InstallUpdateFake(void) {
    ShadowHook_Install(0x000cad40, (const void *)&FakeSFXUpdate);
}

const Case kVoices[] = {
    C("ES_FindFreeVoiceHandle", RunFindFreeVoiceHandle),
    C("ES_UnlockVoiceHandle", RunUnlockVoiceHandle),
    C("HasSampleEnded", RunHasSampleEnded),
    C("ES_RequestVoiceHandle", RunRequestVoiceHandle),
    C("ES_SFXIsReady", RunSFXIsReady),
    C("ES_PauseSfx", RunPauseSfx),
    C("SFXPause", RunPause),
    C("ES_UnPauseSfx", RunUnPauseSfx),
    C("SFXUnPause", RunUnPause),
    C("KillSFX", RunKillSFX),
    C("SFXRemove", RunRemove),
    C("ES_FillpsiSFXData", RunFillpsiSFXData),
    C("ES_StreamKeyOn", RunStreamKeyOn),
    C("SFXPauseAllStreams", RunPauseAllStreams),
    C("SFXUnPauseAllStreams", RunUnPauseAllStreams),
};

// ---- starting the sounds and the frame

uint32_t RunUpdateDucker(bool original) {
    if (R(2))
        DuckerStartTime = answers.timer - RI(-100, 4000);
    CDECL_VOID(0x000c8d00, ES_UpdateDucker, void (__cdecl *)(void));
    return 0;
}

uint32_t RunSetCueTimer(bool original) {
    SFXParameters *p = SomeDef();
    return CDECL_RUN(0x000c56b0, SetCueTimer, int (__cdecl *)(SFXParameters *), p);
}

uint32_t RunCountVoicesInUse(bool original) {
    SFXItem *s = R(5) == 0 ? NULL : SomeItem();
    return CDECL_RUN(0x000c5700, SFXCountVoicesInUse, int (__cdecl *)(SFXItem *), s);
}

uint32_t RunGetMyPos(bool original) {
    SFXItem *s = SomeItem();
    CDECL_VOID(0x000c5760, ES_GetMyPos, void (__cdecl *)(SFXItem *), s);
    return 0;
}

uint32_t RunGetNearestListenerDistance(bool original) {
    SFXItem *s = SomeItem();
    if (R(2))
        for (int i = 0; i < SFX_NUM_LISTENERS; i++)
            RandomVector(&ListenerPositions[i], R(2) ? 3000.0f : 30.0f);
    return CDECL_RUN(0x000c5a90, ES_GetNearestListenerDistance, int (__cdecl *)(SFXItem *), s);
}

uint32_t RunStartSample(bool original) {
    SFXItem *s = SomeItem();
    SFXSamplePoolFiles *sample = &((Def *)s->SFXData)->pool[R(POOL)];
    int n = RI(0, SFX_MAX_VOICES - 1);
    return CDECL_RUN(0x000c9e20, StartSample, int (__cdecl *)(SFXSamplePoolFiles *, int, SFXItem *, int), sample,
                     s->SoundBankSlot, s, n);
}

uint32_t RunSoundPlay(bool original) {
    SFXItem *s = SomeItem();
    return CDECL_RUN(0x000ca050, SFXPlay, int (__cdecl *)(SFXItem *), s);
}

uint32_t RunUpdateEnding(bool original) {
    SFXItem *s = SomeItem();
    world.next = s->Next;
    CDECL_VOID(0x000ca590, SFXUpdateEnding, void (__cdecl *)(SFXItem *, SFXItem **), s, &world.next);
    return 0;
}

uint32_t RunStart3D(bool original) {
    int hashCode = R(10) == 0 ? SFX_NIS_MUSIC_TRIGGER : SomeSound();
    _VECTOR *pos = R(5) == 0 ? NULL : &world.vectors[0];
    _VECTOR *vel = R(5) == 0 ? NULL : &world.vectors[1];
    DYNAMICSOUNDS *handle = SomeHandle();
    int volume = RI(0, 100);
    int inner = R(2) ? -1 : RI(0, 30);
    int outer = R(2) ? -1 : RI(0, 60);
    return CDECL_RUN(0x000caa90, SFXStart3D,
                     uint32_t (__cdecl *)(int, _VECTOR *, _VECTOR *, DYNAMICSOUNDS *, int, int, int), hashCode, pos,
                     vel, handle, volume, inner, outer);
}

uint32_t RunStart(bool original) {
    int hashCode = SomeSound();
    DYNAMICSOUNDS *handle = SomeHandle();
    return CDECL_RUN(0x000cb150, SFXStart, uint32_t (__cdecl *)(int, DYNAMICSOUNDS *), hashCode, handle);
}

uint32_t RunUpdate(bool original) {
    if (R(2))
        DuckerStartTime = answers.timer - RI(-100, 4000);
    CDECL_VOID(0x000cad40, SFXUpdate, void (__cdecl *)(void));
    return 0;
}

const Case kStarting[] = {
    C("ES_UpdateDucker", RunUpdateDucker),
    C("SetCueTimer", RunSetCueTimer),
    C("SFXCountVoicesInUse", RunCountVoicesInUse),
    C("ES_GetMyPos", RunGetMyPos),
    C("ES_GetNearestListenerDistance", RunGetNearestListenerDistance),
    C("StartSample", RunStartSample),
    C("SFXPlay", RunSoundPlay),
    C("SFXUpdateEnding", RunUpdateEnding),
    C("SFXStart3D", RunStart3D),
    C("SFXStart", RunStart),
    C("SFXUpdate", RunUpdate),
};

// ---- the banks and the reset

int SomeBankIndex(void) {
    return R(4) == 0 ? (int)R(0x40) : SBInfoList[R(SFX_NUM_BANKS)];
}

uint32_t RunInitialiseItems(bool original) {
    CDECL_VOID(0x000c5650, SFXInitialiseItems, void (__cdecl *)(void));
    return 0;
}

uint32_t RunLoadSoundBank(bool original) {
    int index = SomeBankIndex();
    CDECL_VOID(0x000c5440, LoadSoundBank, void (__cdecl *)(int), index);
    return 0;
}

uint32_t RunUnloadSoundBank(bool original) {
    int index = SomeBankIndex();
    if (R(2)) {
        for (int b = 0; b < BANKS; b++)
            SoundBankData[b].HashCode = SBInfoList[b];
        index = SBInfoList[R(BANKS)];
    }
    CDECL_VOID(0x000c9b50, SFXUnloadSoundBank, void (__cdecl *)(int), index);
    return 0;
}

uint32_t RunReset(bool original) {
    for (int b = 0; b < BANKS; b++)
        SoundBankData[b].HashCode = SBInfoList[b];
    CDECL_VOID(0x000c9c30, SFXReset, void (__cdecl *)(void));
    return 0;
}

uint32_t RunInitialise(bool original) {
    char *path = R(3) == 0 ? NULL : world.path;
    CDECL_VOID(0x000c9d60, SFXInitialise, void (__cdecl *)(char *), path);
    return 0;
}

uint32_t RunLoadCorrectBank(bool original) {
    for (int b = 0; b < BANKS; b++)
        SoundBankData[b].HashCode = SBInfoList[b];
    if (R(2))
        for (int b = 0; b < SFX_NUM_BANKS; b++)
            SBInfoList[b] = b;
    CDECL_VOID(0x000cbdf0, SFXLoadCorrectBank, void (__cdecl *)(void));
    return 0;
}

const Case kBanks[] = {
    C("SFXInitialiseItems", RunInitialiseItems),
    C("LoadSoundBank", RunLoadSoundBank),
    C("SFXUnloadSoundBank", RunUnloadSoundBank),
    C("SFXReset", RunReset),
    C("SFXInitialise", RunInitialise),
    C("SFXLoadCorrectBank", RunLoadCorrectBank),
};

// ---- the stream's buffers, starting and stopping it

TestMarker *SomeMarker(void) {
    StreamData *s = &Streams[0];
    switch (R(5)) {
        case 0: return &s->EndMarker;
        case 1: return &s->CanJumpMarker;
        case 2: return &s->JumpRequestCompleteMarker;
        case 3: return &s->DataEndMarker;
        default: return &s->Read[R(STREAM_READ_BUFFERS)].EndMarker;
    }
}

uint32_t RunReleaseStreamSystem(bool original) {
    CDECL_VOID(0x000c5ea0, SFXReleaseStreamSystem, void (__cdecl *)(void));
    return 0;
}

uint32_t RunInitialiseAudioStreamSystem(bool original) {
    CDECL_VOID(0x000c5f10, SFXInitialiseAudioStreamSystem, void (__cdecl *)(void));
    return 0;
}

uint32_t RunInitialStreamBuffer(bool original) {
    CDECL_VOID(0x000c5fb0, ES_InitialStreamBuffer, void (__cdecl *)(void));
    return 0;
}

uint32_t RunSetFileBufferCount(bool original) {
    CDECL_VOID(0x000c6080, ES_SetFileBufferCount, void (__cdecl *)(StreamData *), &Streams[0]);
    return 0;
}

uint32_t RunStreamAsyncReadFile(bool original) {
    int file = RI(-1, 5);
    int pos = RI(-0x100, 0x40000);
    int size = RI(-7, 0x4800);
    char *buffer = world.readBuffers[R(STREAM_READ_BUFFERS)];
    CDECL_VOID(0x000c60c0, ES_StreamAsyncReadFile, void (__cdecl *)(int, int, int, char *, StreamData *), file, pos,
               size, buffer, &Streams[0]);
    return 0;
}

uint32_t RunGetStreamStates(bool original) {
    CDECL_VOID(0x000c6110, ES_GetStreamStates, void (__cdecl *)(StreamData *), &Streams[0]);
    return 0;
}

uint32_t RunSetStreamStates(bool original) {
    CDECL_VOID(0x000c6160, ES_SetStreamStates, void (__cdecl *)(StreamData *), &Streams[0]);
    return 0;
}

uint32_t RunStartStreamMemCpy(bool original) {
    int handle = RI(0, 63);
    int offset = RI(0, 0x4800);
    char *src = world.readBuffers[R(STREAM_READ_BUFFERS)];
    int size = RI(0, 0x4800);
    CDECL_VOID(0x000c6370, ES_StartStreamMemCpy, void (__cdecl *)(int, int, char *, int, StreamData *), handle,
               offset, src, size, &Streams[0]);
    return 0;
}

uint32_t RunCheckEndMarker(bool original) {
    TestMarker *m = SomeMarker();
    return CDECL_RUN(0x000c6890, ES_CheckEndMarker, int (__cdecl *)(StreamData *, TestMarker *), &Streams[0], m);
}

uint32_t RunHandleStreamBufferLoaded(bool original) {
    CDECL_VOID(0x000c6910, ES_HandleStreamBufferLoaded, void (__cdecl *)(StreamData *), &Streams[0]);
    return 0;
}

uint32_t RunInitialiseStream(bool original) {
    int hashCode = RI(-3, 40);
    int is3d = R(2);
    return CDECL_RUN(0x000c8fa0, SFXInitialiseStream, int (__cdecl *)(int, int), hashCode, is3d);
}

uint32_t RunStopStream(bool original) {
    CDECL_VOID(0x000c8c40, SFXStopStream, void (__cdecl *)(StreamData *), &Streams[0]);
    return 0;
}

uint32_t RunStopMusic(bool original) {
    CDECL_VOID(0x000c8ce0, SFXStopMusic, void (__cdecl *)(void));
    return 0;
}

uint32_t RunStartMusic(bool original) {
    int track = RI(-1, 40);
    int section = RI(0, STARTS - 1);
    CDECL_VOID(0x000ca5e0, SFXStartMusic, void (__cdecl *)(int, int), track, section);
    return 0;
}

const Case kStreamBuffers[] = {
    C("SFXReleaseStreamSystem", RunReleaseStreamSystem),
    C("SFXInitialiseAudioStreamSystem", RunInitialiseAudioStreamSystem),
    C("ES_InitialStreamBuffer", RunInitialStreamBuffer),
    C("ES_SetFileBufferCount", RunSetFileBufferCount),
    C("ES_StreamAsyncReadFile", RunStreamAsyncReadFile),
    C("ES_GetStreamStates", RunGetStreamStates),
    C("ES_SetStreamStates", RunSetStreamStates),
    C("ES_StartStreamMemCpy", RunStartStreamMemCpy),
    C("ES_CheckEndMarker", RunCheckEndMarker),
    C("ES_HandleStreamBufferLoaded", RunHandleStreamBufferLoaded),
    C("SFXInitialiseStream", RunInitialiseStream),
    C("SFXStopStream", RunStopStream),
    C("SFXStopMusic", RunStopMusic),
    C("SFXStartMusic", RunStartMusic),
};

// ---- the stream's frame

uint32_t RunUpdateMusicMarkers(bool original) {
    int restart = R(2);
    CDECL_VOID(0x000c6970, UpdateMusicMarkers, void (__cdecl *)(StreamData *, int), &Streams[0], restart);
    return 0;
}

uint32_t RunCheckFileBuffersForJumpMarkers(bool original) {
    if (R(2))
        Streams[0].Status = STREAM_READY;
    return CDECL_RUN(0x000c6db0, ES_CheckFileBuffersForJumpMarkers, int (__cdecl *)(StreamData *), &Streams[0]);
}

uint32_t RunJumpToMusicMarker(bool original) {
    int instant = R(2);
    if (R(2))
        Streams[0].Status = R(2) ? STREAM_READY : 8;
    CDECL_VOID(0x000c89e0, ES_JumpToMusicMarker, void (__cdecl *)(StreamData *, int), &Streams[0], instant);
    return 0;
}

uint32_t RunJumpToMusicMarkerUpdate(bool original) {
    CDECL_VOID(0x000c8b40, SFXJumpToMusicMarkerUpdate, void (__cdecl *)(void));
    return 0;
}

uint32_t RunInitialiseStreamUpdate(bool original) {
    if (R(2)) {
        StreamIntialiseDetails *w = &StreamWaitingIntialise[StreamWaitingIntialiseMe];
        w->HashCode = R(3) == 0 ? -RI(1, 3) : RI(0, 60);
    }
    CDECL_VOID(0x000c63c0, SFXInitialiseStreamUpdate, void (__cdecl *)(void));
    return 0;
}

uint32_t RunUpdateStreams(bool original) {
    CDECL_VOID(0x000c9030, SFXUpdateStreams, void (__cdecl *)(void));
    return 0;
}

const Case kStreamFrame[] = {
    C("UpdateMusicMarkers", RunUpdateMusicMarkers),
    C("ES_CheckFileBuffersForJumpMarkers", RunCheckFileBuffersForJumpMarkers),
    C("ES_JumpToMusicMarker", RunJumpToMusicMarker),
    C("SFXJumpToMusicMarkerUpdate", RunJumpToMusicMarkerUpdate),
    C("SFXInitialiseStreamUpdate", RunInitialiseStreamUpdate),
    C("SFXUpdateStreams", RunUpdateStreams),
};

void InstallStreamUpdateFake(void) {
    ShadowHook_Install(0x000c63c0, (const void *)&FakeInitialiseStreamUpdate);
}

void Group(const char *name, const Case *list, int count, int trials, void (*more)(void) = NULL) {
    const char *only = getenv("NIGHTFIRE_SFX_GROUP");
    if (only != NULL && strstr(name, only) == NULL)
        return;
    ShadowHook_RemoveAll();
    InstallFakes();
    if (more != NULL)
        more();
    Test(name, list, count, trials);
}

} // namespace

void SFXShadow_Run(void) {
    std::vector<std::vector<uint8_t>> saved;
    regions.Clear();
    areaCount = 0;
    AddArea(&world, sizeof(world));
    for (const auto &g : kGameState) {
        uint8_t *at = (uint8_t *)(uintptr_t)g.at;
        saved.emplace_back(at, at + g.size);
        AddArea(at, g.size);
    }
    NfWarnMuted++;
    cases = differ = nanChoices = 0;

    Group("the settings and the mixer", kSettings, COUNT(kSettings), 3000);
    Group("the file access", kSuspend, COUNT(kSuspend), 3000, InstallUpdateFake);
    Group("the voices and the list", kVoices, COUNT(kVoices), 3000, InstallLayerFakes);
    Group("starting the sounds and the frame", kStarting, COUNT(kStarting), 4000, InstallLayerFakes);
    Group("the banks and the reset", kBanks, COUNT(kBanks), 3000, InstallLayerFakes);
    Group("the stream's buffers", kStreamBuffers, COUNT(kStreamBuffers), 3000, InstallStreamUpdateFake);
    Group("the stream's frame", kStreamFrame, COUNT(kStreamFrame), 8000);
    ShadowHook_RemoveAll();

    NfWarnMuted--;
    for (int i = 0; i < COUNT(kGameState); i++)
        memcpy((void *)(uintptr_t)kGameState[i].at, saved[i].data(), kGameState[i].size);
    if (nanChoices != 0)
        printf("[sfx] %d cases matched with NaNs the x87 and SSE2 chose differently\n", nanChoices);
    printf("[sfx] %d cases, %d differ\n", cases, differ);
    fflush(stdout);
}
