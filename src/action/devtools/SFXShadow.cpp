// Shadow tests for the sound layer (sound/SFX.cpp, sound/SFXStream.cpp): each function, the original and ours, on
// the same made-up state - sounds in the list and free, banks of made-up sounds and samples, voices, the stream with
// its file buffers and markers, the listeners, the mixer's volumes - with the psi layer below (the voices, the
// timer, the files) a logging fake.
// Run with the soundport suite (MenuShadowTests=on, NIGHTFIRE_SHADOW_ONLY=soundport, or =sfx for these alone);
// NIGHTFIRE_SFX_GROUP=<part of a name> runs one group.

#include "SFXShadow.h"
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
};

const int DEFS = 8;
const int POOL = 8;
const int BANKS = 3;
const int BANK_SFX = 6;
const int SAMPLES = 12;
const int HANDLES = 4;

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
    int intArg;
};
World world;

int cases, differ;
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

uint32_t F(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
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

void InstallFakes(void) {
    ShadowHook_Install(0x000e0a80, (const void *)&FakeGetTimer);
    ShadowHook_Install(0x000e0ab0, (const void *)&FakeSetReverb);
    ShadowHook_Install(0x000e0b50, (const void *)&FakeSetupListener);
    ShadowHook_Install(0x000e0dc0, (const void *)&FakeSetVolume);
    ShadowHook_Install(0x000cad40, (const void *)&FakeSFXUpdate);
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
    p->SampleCount = RI(R(6) == 0 ? 0 : 1, POOL);
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
        int slot = (int)R(SFX_NUM_BANKS);
        SoundBankHeader *h = &SoundBankData[slot];
        h->Loaded = R(5) != 0;
        h->HashCode = SBInfoList[slot];
        h->SFXBuffer = &f->count;
        h->SFXCount = f->count;
        h->SFXHeaderData = f->headers;
        h->SampleHeader = world.samples[b];
        h->SampleCount = SAMPLES;
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
        s->Last = (SFXItem *)(uintptr_t)R(0);
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

void RandomStream(void) {
    StreamData *s = &Streams[0];
    static const int status[] = {0, 1, 2, 3, 4, 5, 9, 10};
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
    C("SFXSuspendFileAccess", RunSuspendFileAccess),
    C("SFXUnSuspendFileAccess", RunUnSuspendFileAccess),
    C("FindMusicTrackNumber", RunFindMusicTrack),
    C("ES_MusicSetVolume", RunMusicSetVolumeES),
    C("SFXMusicSetVolume", RunMusicSetVolume),
    C("SFXMusicGetVolume", RunMusicGetVolume),
};

void Group(const char *name, const Case *list, int count, int trials) {
    const char *only = getenv("NIGHTFIRE_SFX_GROUP");
    if (only != NULL && strstr(name, only) == NULL)
        return;
    ShadowHook_RemoveAll();
    InstallFakes();
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
    cases = differ = 0;

    Group("the settings and the mixer", kSettings, COUNT(kSettings), 3000);
    ShadowHook_RemoveAll();

    NfWarnMuted--;
    for (int i = 0; i < COUNT(kGameState); i++)
        memcpy((void *)(uintptr_t)kGameState[i].at, saved[i].data(), kGameState[i].size);
    printf("[sfx] %d cases, %d differ\n", cases, differ);
    fflush(stdout);
}
