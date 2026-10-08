#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "AudioMgrShadow.h"
#include "FpControl.h"

#include <windows.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <bit>
#include <initializer_list>
#include <vector>

#include "../audio/Sound.h"
#include "../audio/SoundManager.h"
#include "../engine/UMemory.hpp"
#include "../sound/snd/System.h"
#include "../../common/xbeOriginal.h"
#include "../../helpers.h"

// ---------------------------------------------------------------------------------------------------------------
// A shadow test of the sound manager and the sounds (audio/SoundManager.cpp, audio/Sound.cpp), run once from the
// first simulation tick when NIGHTFIRE_AUDIOMGRSHADOW is set. Each case runs the ORIGINAL (every entry of the
// two files swapped back together, so an original reaches the other originals) and then the PORT on the same
// inputs, and compares the bytes they leave and a log of every call they make outside those files:
//
//   - ABaseSound's fade (StartFade, GetFade in random sequences from random states), GetName,
//     Generic_FuncReturnsFalse, NormalizedRandomNumber (rand replaced by a fake generator; the kept second number
//     and the number of draws compared);
//   - the sound list: remove over lists with repeated values, both _Incsize copies, operator new and delete on a
//     private list put in place of the manager's;
//   - the constructors (ABaseSound, ABasic, AMenuSoundPriv) and the deleting destructors with flags 0 (each
//     kind, ABasic with and without a voice), ABasic::Stop, AMenuSoundPriv::Play and ABasic::Play, the three
//     AMenuSound::Trigger, ReportFailure, ASystem::SetOpts;
//   - the manager over a private list of sounds whose vtable records Delete, IsTransient and Play: BuildPaths
//     (random sounds and listener; the play parameters are the original's FUN_0012eab0), Stop, Pause, Resume,
//     StopSoundPos, Restart, ClearMission, SetMissionOver.
//
// The calls out are replaced by recording fakes that answer deterministically: the mixes (Add/Get from a private
// table, Remove, GetVolume, Reset), the voice (constructor, Play, destructors, FUN_000d36c0), AIndex::Lookup,
// ABank::Get/Remove (private banks put in the bank table), AFX, AVoice::PlayVoices, the streams and the fader,
// AEngine::RemoveAll, printf, rand and SNDSYS_getopts/setops. Nothing reaches the sound library or a live object.
// Not tested here (in game): Init, Shutdown, ASystem's constructor, Init and Shutdown.
//
// The play parameters' +0x10 and +0x14 are not compared (the original leaves its stack's in them where FUN_0012eab0
// does not write them). A mutation the test sees: BuildPaths storing 0 instead of the heard volume in lastVolumes
// (the sounds' bytes), or GetFade's `<` made `<=` (the fade sequences).
//
// One summary line: [audiomgrshadow] ...: N cases, M checks, D differ.
// ---------------------------------------------------------------------------------------------------------------

class AFader;
class AStream;

namespace {

// ---- the originals

#define Orig_BaseConstruct ((ABaseSound *(__fastcall *)(void *, int, const char *, int))0x0001bfa0)
#define Orig_FuncReturnsFalse ((bool (*)(void))0x0001c080)
#define Orig_BaseDelete ((void *(__fastcall *)(void *, int, unsigned))0x0001c090)
#define Orig_OneShotDelete ((void *(__fastcall *)(void *, int, unsigned))0x00047900)
#define Orig_LimitedDelete ((void *(__fastcall *)(void *, int, unsigned))0x0004de20)
#define Orig_GetName ((char *(__fastcall *)(ABaseSound *, int))0x0011c710)
#define Orig_StartFade ((void (__fastcall *)(ABaseSound *, int, int))0x0011c720)
#define Orig_GetFade ((float (__fastcall *)(ABaseSound *, int))0x0011c750)
#define Orig_ListRemove ((void (__fastcall *)(ASoundList *, int, ABaseSound *const *))0x0011c7c0)
#define Orig_OperatorDelete ((void (*)(void *, unsigned))0x0011c830)
#define Orig_SoundListIncSize ((void (__fastcall *)(ASoundList *, int, uint32_t))0x0011c860)
#define Orig_OperatorNew ((void *(*)(unsigned, const char *))0x0011c910)
#define Orig_MenuConstruct ((void *(__fastcall *)(void *, int, int, int, const char *, int))0x0011de40)
#define Orig_MenuPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0011dee0)
#define Orig_Trigger1 ((void (*)(int, int, const char *, int))0x0011df50)
#define Orig_Trigger2 ((void (*)(const char *, const char *, const char *, int))0x0011dfd0)
#define Orig_Trigger3 ((void (*)(int, const char *, const char *, int))0x0011e010)
#define Orig_MenuDelete ((void *(__fastcall *)(void *, int, unsigned))0x0011e060)
#define Orig_SetMissionOver ((void (*)(void))0x00120d80)
#define Orig_NormalRandom ((double (*)(void))0x00120db0)
#define Orig_BuildPaths ((void (*)(AListener *))0x00120e60)
#define Orig_Stop ((void (*)(void))0x00120f90)
#define Orig_Pause ((void (*)(void))0x00121070)
#define Orig_Resume ((void (*)(void))0x00121150)
#define Orig_StopSoundPos ((void (*)(const Coord3 *))0x001211d0)
#define Orig_ClearMission ((void (*)(void))0x00121280)
#define Orig_Restart ((void (*)(void))0x00121320)
#define Orig_FailedIncSize ((void (__fastcall *)(StreamNameList *, int, uint32_t))0x00121d90)
#define Orig_ReportFailure ((void (*)(const char *))0x00121e40)
#define Orig_SetOpts ((void (*)(int))0x001283f0)
#define Orig_BasicConstruct ((void *(__fastcall *)(void *, int, const char *, const char *))0x0012fa40)
#define Orig_BasicPlay ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012fa90)
#define Orig_BasicDelete ((void *(__fastcall *)(void *, int, unsigned))0x0012fc00)
#define Orig_BasicStop ((void (__fastcall *)(void *, int, ASoundPlayParams *))0x0012fc30)

// The entries of SoundManager.cpp and Sound.cpp (and the extra entry 0x0001c080), swapped back together for an
// original's run
const unsigned kEntries[] = {
    0x0001bfa0, 0x0001c080, 0x0001c090, 0x000478a0, 0x00047900, 0x0004de20, 0x0004de50, 0x0011c6f0, 0x0011c710,
    0x0011c720, 0x0011c750, 0x0011c7c0, 0x0011c830, 0x0011c860, 0x0011c910, 0x0011de40, 0x0011dee0, 0x0011df50,
    0x0011dfd0, 0x0011e010, 0x0011e060, 0x0011e090, 0x00120d80, 0x00120db0, 0x00120e60, 0x00120f90, 0x00121070,
    0x00121150, 0x001211d0, 0x00121280, 0x00121320, 0x00121470, 0x00121d00, 0x00121d90, 0x00121e40, 0x00128350,
    0x00128390, 0x001283f0, 0x00128470, 0x0012fa40, 0x0012fa90, 0x0012fb80, 0x0012fc00, 0x0012fc30,
};

// The game's list code (std::list of pointers): the head, _Buynode, the destructor
#define List_BuyHead ((PointerListNode *(__fastcall *)(void *, int))0x000b8490)
#define List_BuyNode ((PointerListNode *(__fastcall *)(void *, int, PointerListNode *, PointerListNode *, void *const *))0x000130e0)
#define List_Destruct ((void (__fastcall *)(void *, int))0x00013540)
#define Listener_Construct ((AListener *(__fastcall *)(AListener *, int, int))0x00124ae0)

// The globals the code under test reads and writes
#define ShadowFailedStreams (*(StreamNameList **)0x00243a5c)
#define ShadowLimitedCount I32_AT(0x00243b30)
#define ShadowSpare ((uint8_t *)0x00243a78)
#define ShadowPaused U8_AT(0x002439d0)
#define ShadowMissionOver U8_AT(0x00243a77)
#define ShadowUnknown6c U32_AT(0x00243a6c)
#define ShadowSystem PTR_AT(0x00243b34)

long g_cases, g_checks, g_differ, g_faults;
int g_reported;
unsigned g_x87, g_sse;

void Check(bool same, const char *what, long index) {
    g_checks++;
    if (same)
        return;
    g_differ++;
    if (g_reported < 10) {
        g_reported++;
        printf("[audiomgrshadow] DIFF %s, case %ld\n", what, index);
        fflush(stdout);
    }
}

uint32_t g_seed = 0x3c6ef372;
uint32_t NextRandom() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed;
}
int RandomInt(int lo, int hi) {     // lo..hi inclusive
    return lo + int((NextRandom() >> 8) % uint32_t(hi - lo + 1));
}
float Uniform(float lo, float hi) {
    return lo + (hi - lo) * float(NextRandom() >> 8) * (1.0f / 16777216.0f);
}
uint32_t Bits(float f) {
    return std::bit_cast<uint32_t>(f);
}

template <class F>
bool Guarded(const F &call) {
#ifdef _MSC_VER
    __try {
        call();
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        g_faults++;
        FpControlSetX87(g_x87);
        FpControlSetSse(g_sse);
        return false;
    }
#else
    call();
#endif
    return true;
}

void Originals(bool on) {
    for (unsigned at : kEntries)
        XbeOriginal_Restore(at, on);
}

// ---- hooks: a jump written over an entry (and over the port a patched entry jumps to)

struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[80];
int g_hookCount;

void HookOne(uint32_t at, const void *to) {
    if (g_hookCount == int(sizeof(g_hooks) / sizeof(g_hooks[0])))
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    h.on = false;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old))
        return;
    memcpy(h.saved, (void *)(uintptr_t)at, 5);
    uint8_t jump[5];
    jump[0] = 0xe9;
    int32_t rel = (int32_t)((uint32_t)(uintptr_t)to - (at + 5));
    memcpy(jump + 1, &rel, 4);
    memcpy((void *)(uintptr_t)at, jump, 5);
    VirtualProtect((void *)(uintptr_t)at, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)at, 5);
    h.on = true;
}

void HookInstall(uint32_t at, const void *to) {
    const uint8_t *entry = (const uint8_t *)(uintptr_t)at;
    uint32_t port = 0;
    if (entry[0] == 0xe9) {
        int32_t rel;
        memcpy(&rel, entry + 1, 4);
        port = at + 5 + uint32_t(rel);
    }
    HookOne(at, to);
    if (port != 0 && port != (uint32_t)(uintptr_t)to)
        HookOne(port, to);
}

void HooksRemove() {
    for (int i = g_hookCount; i-- > 0;) {
        Hook &h = g_hooks[i];
        if (!h.on)
            continue;
        DWORD old;
        VirtualProtect((void *)(uintptr_t)h.at, 5, PAGE_EXECUTE_READWRITE, &old);
        memcpy((void *)(uintptr_t)h.at, h.saved, 5);
        VirtualProtect((void *)(uintptr_t)h.at, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), (void *)(uintptr_t)h.at, 5);
        h.on = false;
    }
    g_hookCount = 0;
}

// ---- the private objects the fakes hand out, and tags for pointers into them

struct FakeMix {
    AMix mix;
    char name[32];
};
FakeMix g_mixes[24];
int g_mixCount;
ABank g_banks[kBankSlotCount];
uint8_t g_stream[0x100];
uint8_t g_fader[0x40];
alignas(16) AListener g_listener;
ABaseSound g_sounds[8];
alignas(16) uint8_t g_object[0x200];
const uint8_t *g_base;          // the object under test
size_t g_baseSize;
const uint8_t *g_voiceBase;     // a voice from the pools
AVoice g_voice;

uint32_t Tag(const void *p) {
    if (p == NULL)
        return 0;
    const uint8_t *b = static_cast<const uint8_t *>(p);
    if (g_base != NULL && b >= g_base && b < g_base + g_baseSize)
        return 0x10000 + uint32_t(b - g_base);
    if (g_voiceBase != NULL && b >= g_voiceBase && b < g_voiceBase + sizeof(AVoice))
        return 0x18000 + uint32_t(b - g_voiceBase);
    const uint8_t *sounds = reinterpret_cast<const uint8_t *>(g_sounds);
    if (b >= sounds && b < sounds + sizeof(g_sounds))
        return 0x20000 + uint32_t(b - sounds);
    if (p == &g_listener)
        return 0x30000;
    const uint8_t *mixes = reinterpret_cast<const uint8_t *>(g_mixes);
    if (b >= mixes && b < mixes + sizeof(g_mixes))
        return 0x40000 + uint32_t(b - mixes);
    const uint8_t *banks = reinterpret_cast<const uint8_t *>(g_banks);
    if (b >= banks && b < banks + sizeof(g_banks))
        return 0x50000 + uint32_t(b - banks);
    if (b >= g_stream && b < g_stream + sizeof(g_stream))
        return 0x60000;
    if (b >= g_fader && b < g_fader + sizeof(g_fader))
        return 0x70000;
    const uint8_t *voice = reinterpret_cast<const uint8_t *>(&g_voice);
    if (b >= voice && b < voice + sizeof(g_voice))
        return 0x80000 + uint32_t(b - voice);
    return 0xffffffff;
}

uint32_t Hash(const void *data, size_t size) {
    uint32_t h = 2166136261u;
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        h = (h ^ b[i]) * 16777619u;
    return h;
}
uint32_t HashString(const char *s) {
    return s == NULL ? 0 : Hash(s, strlen(s));
}

void ResetMixes() {
    memset(g_mixes, 0, sizeof(g_mixes));
    g_mixCount = 0;
}
AMix *MixByName(const char *name) {
    for (int i = 0; i < g_mixCount; i++) {
        if (strcmp(g_mixes[i].name, name) == 0)
            return &g_mixes[i].mix;
    }
    if (g_mixCount == int(sizeof(g_mixes) / sizeof(g_mixes[0])))
        return &g_mixes[0].mix;
    FakeMix &m = g_mixes[g_mixCount];
    strncpy(m.name, name, sizeof(m.name) - 1);
    m.mix.volume = 0.5f + 0.03f * float(g_mixCount);
    m.mix.previous = 0.25f;
    g_mixCount++;
    return &m.mix;
}

// ---- the call log and the fakes

enum : uint32_t {
    kMixAdd = 1, kMixGet, kMixRemove, kMixVolume, kMixReset, kVoiceNew, kVoicePlay, kViewDestruct, kVoiceDestruct,
    kVoiceFinished, kLookup, kBankGet, kBankRemove, kFxUpdate, kFxPause, kFxResume, kPlayVoices, kStreamGet,
    kStreamFadeOut, kStreamStop, kFaderGet, kFaderSecondary, kEngineRemoveAll, kPrintf, kGetOpts, kSetOps,
    kSoundDelete, kSoundTransient, kSoundPlay,
};

std::vector<uint32_t> g_log;
void Log(uint32_t type, std::initializer_list<uint32_t> words) {
    g_log.push_back(type);
    g_log.push_back(uint32_t(words.size()));
    g_log.insert(g_log.end(), words.begin(), words.end());
}

bool g_finished;
int g_lookupAnswer;
uint32_t g_randState;
int g_randCalls;

AMix *FakeMixAdd(const char *name) {
    Log(kMixAdd, { HashString(name) });
    return MixByName(name);
}
AMix *FakeMixGet(const char *name) {
    Log(kMixGet, { HashString(name) });
    return MixByName(name);
}
void __fastcall FakeMixRemove(AMix *mix, int) {
    Log(kMixRemove, { Tag(mix) });
}
double __fastcall FakeMixGetVolume(AMix *mix, int) {
    Log(kMixVolume, { Tag(mix) });
    return double(mix->volume) * (1.0 / 3.0);      // unrounded, as the x87 would leave it
}
void FakeMixReset(int steps) {
    Log(kMixReset, { uint32_t(steps) });
}
AVoice *__fastcall FakeVoiceConstruct(AVoice *voice, int, AMix *mix, int bank, int patch) {
    Log(kVoiceNew, { Tag(voice), Tag(mix), uint32_t(bank), uint32_t(patch) });
    voice->mix = mix;
    for (int i = 0; i < 3; i++) {
        memset(&voice->views[i], 0x40 + i, sizeof(AVoice::View));
        voice->views[i].bank = bank;
        voice->views[i].patch = patch;
    }
    return voice;
}
void __fastcall FakeVoicePlay(AVoice *voice, int, int view, float volume, float pitch, float azimuth, float delay,
                              float fxLevel) {
    Log(kVoicePlay, { Tag(voice), uint32_t(view), Bits(volume), Bits(pitch), Bits(azimuth), Bits(delay),
                      Bits(fxLevel) });
}
void __fastcall FakeViewDestruct(AVoice::View *view, int) {
    Log(kViewDestruct, { Tag(view) });
}
void __fastcall FakeVoiceDestruct(AVoice *voice, int) {
    Log(kVoiceDestruct, { Tag(voice) });
}
bool __fastcall FakeVoiceIsFinished(AVoice *voice, int) {
    Log(kVoiceFinished, { Tag(voice) });
    return g_finished;
}
int __fastcall FakeLookup(AIndex *index, int, const char *name) {
    Log(kLookup, { Tag(index), HashString(name) });
    return g_lookupAnswer;
}
ABank *FakeBankGet(const char *name) {
    Log(kBankGet, { HashString(name) });
    return &g_banks[3];
}
void __fastcall FakeBankRemove(ABank *bank, int) {
    Log(kBankRemove, { Tag(bank) });
}
void FakeFxUpdate() {
    Log(kFxUpdate, {});
}
void FakeFxPause() {
    Log(kFxPause, {});
}
void FakeFxResume() {
    Log(kFxResume, {});
}
void FakePlayVoices() {
    Log(kPlayVoices, {});
}
AStream *FakeStreamGet(const char *name) {
    Log(kStreamGet, { HashString(name) });
    return reinterpret_cast<AStream *>(g_stream);
}
void __fastcall FakeStreamFadeOut(AStream *stream, int) {
    Log(kStreamFadeOut, { Tag(stream) });
}
void __fastcall FakeStreamStop(AStream *stream, int) {
    Log(kStreamStop, { Tag(stream) });
}
AFader *FakeFaderGet(const char *name) {
    Log(kFaderGet, { HashString(name) });
    return reinterpret_cast<AFader *>(g_fader);
}
void __fastcall FakeFaderSetSecondary(AFader *fader, int, bool on) {
    Log(kFaderSecondary, { Tag(fader), uint32_t(on) });
}
void FakeEngineRemoveAll() {
    Log(kEngineRemoveAll, {});
}
int FakePrintf(const char *format, ...) {
    va_list args;
    va_start(args, format);
    uint32_t words[6];
    for (int i = 0; i < 3; i++) {
        double d = va_arg(args, double);
        memcpy(&words[i * 2], &d, 8);
    }
    va_end(args);
    Log(kPrintf, { HashString(format), words[0], words[1], words[2], words[3], words[4], words[5] });
    return 0;
}
int FakeRand() {
    g_randCalls++;
    g_randState = g_randState * 214013u + 2531011u;
    return int((g_randState >> 16) & 0x7fff);
}
int FakeGetOpts(SND::SysOpts *opts) {
    Log(kGetOpts, {});
    uint8_t *b = reinterpret_cast<uint8_t *>(opts);
    for (size_t i = 0; i < sizeof(SND::SysOpts); i++)
        b[i] = uint8_t(i * 7 + 3);
    return 0;
}
int FakeSetOps(const SND::SysOpts *opts) {
    g_log.push_back(kSetOps);
    g_log.push_back(uint32_t(sizeof(SND::SysOpts) / 4));
    const uint32_t *w = reinterpret_cast<const uint32_t *>(opts);
    for (size_t i = 0; i < sizeof(SND::SysOpts) / 4; i++)
        g_log.push_back(w[i]);
    return 0;
}

// The private sounds' vtable
uint32_t ListenerWords(const AListener *listener, uint32_t *words) {
    if (listener == NULL)
        return 0;
    words[0] = Tag(listener);
    words[1] = uint32_t(listener->unknown5c);
    words[2] = Hash(listener, 0x5c);
    words[3] = Bits(listener->unknown70) ^ listener->unknown74;
    return 4;
}
ABaseSound *__fastcall FakeSoundDelete(ABaseSound *sound, int, unsigned int flags) {
    Log(kSoundDelete, { Tag(sound), flags });
    return sound;
}
bool __fastcall FakeSoundIsTransient(ABaseSound *sound, int) {
    Log(kSoundTransient, { Tag(sound) });
    return (sound->unknown04[0] & 1) != 0;
}
void __fastcall FakeSoundPlay(ABaseSound *sound, int, ASoundPlayParams *params) {
    uint32_t l[4] = {};
    ListenerWords(params->listener, l);
    Log(kSoundPlay, { Tag(sound), Bits(params->volume), Bits(params->pitch), Bits(params->azimuth),
                      Bits(params->unknown0c), l[0], l[1], l[2], l[3] });
}
const void *g_fakeVtable[5] = {
    (const void *)&FakeSoundDelete, (const void *)&FakeSoundIsTransient, (const void *)0x0011c710,
    (const void *)&FakeSoundPlay, (const void *)&FakeSoundPlay,
};
uint32_t FakeVtable() {
    return uint32_t(uintptr_t(g_fakeVtable));
}

// ---- the state the code under test writes outside its objects

struct Globals {
    ASoundList *soundList;
    StreamNameList *failed;
    uint32_t views;
    ABank *banks[kBankSlotCount];
    int32_t limited;
    uint8_t spare[8];
    uint8_t paused;
    uint8_t missionOver;
    uint32_t unknown6c;
    void *system;
};

void SaveGlobals(Globals *g) {
    g->soundList = fgSoundList;
    g->failed = ShadowFailedStreams;
    g->views = fgActiveViews;
    memcpy(g->banks, fgBanks, sizeof(g->banks));
    g->limited = ShadowLimitedCount;
    memcpy(g->spare, ShadowSpare, 8);
    g->paused = ShadowPaused;
    g->missionOver = ShadowMissionOver;
    g->unknown6c = ShadowUnknown6c;
    g->system = ShadowSystem;
}

void LoadGlobals(const Globals &g) {
    fgSoundList = g.soundList;
    ShadowFailedStreams = g.failed;
    fgActiveViews = g.views;
    memcpy(fgBanks, g.banks, sizeof(g.banks));
    ShadowLimitedCount = g.limited;
    memcpy(ShadowSpare, g.spare, 8);
    ShadowPaused = g.paused;
    ShadowMissionOver = g.missionOver;
    ShadowUnknown6c = g.unknown6c;
    ShadowSystem = g.system;
}

void GlobalWords(std::vector<uint32_t> &out) {
    out.push_back(fgActiveViews);
    out.push_back(uint32_t(ShadowLimitedCount));
    uint32_t spare[2];
    memcpy(spare, ShadowSpare, 8);
    out.push_back(spare[0]);
    out.push_back(spare[1] & 0xff);     // the flag; the bytes after it are not the manager's
    out.push_back(ShadowPaused);
    out.push_back(ShadowMissionOver);
    out.push_back(ShadowUnknown6c);
}

// ---- private lists, through the game's own list code

void ListInit(void *list) {
    ASoundList *l = static_cast<ASoundList *>(list);
    memset(l, 0, sizeof(*l));
    l->head = List_BuyHead(l, 0);
    l->size = 0;
}
void ListPush(void *list, void *value) {
    ASoundList *l = static_cast<ASoundList *>(list);
    PointerListNode *end = l->head;
    PointerListNode *node = List_BuyNode(l, 0, end, end->prev, &value);
    l->size++;
    end->prev = node;
    node->prev->next = node;
}
void ListWords(std::vector<uint32_t> &out, const void *list, bool raw) {
    const ASoundList *l = static_cast<const ASoundList *>(list);
    out.push_back(l->size);
    int guard = 0;
    for (PointerListNode *node = l->Begin(); node != l->head && guard < 64; node = node->next, guard++)
        out.push_back(raw ? uint32_t(uintptr_t(node->value)) : Tag(node->value));
}

// ---- one case: the original's run, then the port's, compared

struct Outcome {
    std::vector<uint32_t> words;
    std::vector<uint32_t> log;
};

void Append(std::vector<uint32_t> &out, const void *data, size_t size) {
    const uint8_t *b = static_cast<const uint8_t *>(data);
    for (size_t i = 0; i < size; i += 4) {
        uint32_t w = 0;
        memcpy(&w, b + i, size - i < 4 ? size - i : 4);
        out.push_back(w);
    }
}

// The first differing case's words, 8 to a line (at most 256)
void DumpWords(const char *what, const std::vector<uint32_t> &words) {
    printf("[audiomgrshadow]   %s (%u words):\n", what, unsigned(words.size()));
    for (size_t i = 0; i < words.size() && i < 256; i += 8) {
        printf("[audiomgrshadow]     %3u:", unsigned(i));
        for (size_t j = i; j < i + 8 && j < words.size(); j++)
            printf(" %08x", words[j]);
        printf("\n");
    }
    fflush(stdout);
}

// setup() puts the inputs in place; run(original) makes the call; capture(words) records what it left.
template <class Setup, class Run, class Capture>
void Case(const char *what, Setup setup, Run run, Capture capture) {
    Outcome results[2];
    for (int phase = 0; phase < 2; phase++) {
        bool original = phase == 0;
        setup();
        g_log.clear();
        bool ok;
        if (original) {
            Originals(true);
            ok = Guarded([&] { run(true); });
            Originals(false);
        } else {
            ok = Guarded([&] { run(false); });
        }
        results[phase].words.push_back(ok);
        capture(results[phase].words);
        results[phase].log = g_log;
    }
    char label[96];
    snprintf(label, sizeof(label), "%s (state)", what);
    Check(results[0].words == results[1].words, label, g_cases);
    snprintf(label, sizeof(label), "%s (calls)", what);
    Check(results[0].log == results[1].log, label, g_cases);
    static bool dumped;
    if (!dumped && (results[0].words != results[1].words || results[0].log != results[1].log)) {
        dumped = true;
        DumpWords("state, original", results[0].words);
        DumpWords("state, port    ", results[1].words);
        DumpWords("calls, original", results[0].log);
        DumpWords("calls, port    ", results[1].log);
    }
    g_cases++;
}

// ---- the tests

void TestFade() {
    for (int c = 0; c < 1500; c++) {
        ABaseSound start;
        memset(&start, 0, sizeof(start));
        start.fading = (NextRandom() & 1) != 0;
        start.fadeStep = RandomInt(-3, 12);
        start.fadeSteps = RandomInt(-2, 12);
        start.fade = Uniform(-1.0f, 2.0f);
        int count = RandomInt(1, 6);
        int ops[6];
        for (int i = 0; i < count; i++)
            ops[i] = (NextRandom() % 4 == 0) ? RandomInt(-1, 8) : -100;     // -100: GetFade
        ABaseSound s;
        std::vector<uint32_t> answers;
        Case("fade", [&] { s = start; answers.clear(); },
             [&](bool original) {
                 for (int i = 0; i < count; i++) {
                     if (ops[i] == -100)
                         answers.push_back(Bits(original ? Orig_GetFade(&s, 0) : s.GetFade()));
                     else if (original)
                         Orig_StartFade(&s, 0, ops[i]);
                     else
                         s.StartFade(ops[i]);
                 }
             },
             [&](std::vector<uint32_t> &w) {
                 w.insert(w.end(), answers.begin(), answers.end());
                 Append(w, &s, sizeof(s));
             });
    }
    ABaseSound s;
    memset(&s, 0x5a, sizeof(s));
    uint32_t name = 0, falseAnswer = 0;
    Case("GetName", [&] {},
         [&](bool original) {
             char *answer = original ? Orig_GetName(&s, 0) : s.GetName();
             name = uint32_t(answer - reinterpret_cast<char *>(&s));
             falseAnswer = original ? Orig_FuncReturnsFalse() : Generic_FuncReturnsFalse();
         },
         [&](std::vector<uint32_t> &w) {
             w.push_back(name);
             w.push_back(falseAnswer);
         });
}

void TestRandom() {
    for (int c = 0; c < 400; c++) {
        uint32_t seed = NextRandom();
        uint8_t spare[8] = {};
        float value = Uniform(-3.0f, 3.0f);
        memcpy(spare, &value, 4);
        spare[4] = c % 4 == 0;
        double answer = 0.0;
        Case("NormalizedRandomNumber",
             [&] {
                 g_randState = seed;
                 g_randCalls = 0;
                 memcpy(ShadowSpare, spare, 5);
             },
             [&](bool original) {
                 answer = original ? Orig_NormalRandom() : ASoundManager::NormalizedRandomNumber();
             },
             [&](std::vector<uint32_t> &w) {
                 Append(w, &answer, 8);
                 Append(w, ShadowSpare, 5);
                 w.push_back(uint32_t(g_randCalls));
             });
    }
}

void TestLists() {
    for (int c = 0; c < 300; c++) {
        int count = RandomInt(0, 8);
        ABaseSound *values[8];
        for (int i = 0; i < count; i++)
            values[i] = &g_sounds[RandomInt(0, 3)];
        ABaseSound *removed = &g_sounds[RandomInt(0, 4)];
        ASoundList list;
        Case("list remove",
             [&] {
                 ListInit(&list);
                 for (int i = 0; i < count; i++)
                     ListPush(&list, values[i]);
             },
             [&](bool original) {
                 if (original)
                     Orig_ListRemove(&list, 0, &removed);
                 else
                     list.Remove(removed);
             },
             [&](std::vector<uint32_t> &w) {
                 ListWords(w, &list, false);
                 List_Destruct(&list, 0);
             });
    }
    for (int c = 0; c < 100; c++) {
        uint32_t size = NextRandom() % 100000;
        uint32_t count = NextRandom() % 100;
        ASoundList sounds;
        StreamNameList names;
        Case("list _Incsize",
             [&] {
                 memset(&sounds, 0x11, sizeof(sounds));
                 memset(&names, 0x22, sizeof(names));
                 sounds.size = size;
                 names.size = size + 1;
             },
             [&](bool original) {
                 if (original) {
                     Orig_SoundListIncSize(&sounds, 0, count);
                     Orig_FailedIncSize(&names, 0, count);
                 } else {
                     sounds.IncreaseSize(count);
                     names.IncreaseSize(count);
                 }
             },
             [&](std::vector<uint32_t> &w) {
                 Append(w, &sounds, sizeof(sounds));
                 Append(w, &names, sizeof(names));
             });
    }
}

const char *const kNames[] = { "AMenuSoundPriv", "ABasic", "Engine", "ASound" };
const char *const kMixNames[] = { "Menu Sounds", "World Sounds", "0:Fader2", "Music" };
const unsigned kSizes[] = { 0xc0, 0xf0, 0x120 };

void TestNewDelete() {
    for (int c = 0; c < 60; c++) {
        int count = RandomInt(0, 4);
        unsigned size = kSizes[c % 3];
        const char *name = kNames[c % 4];
        ASoundList list;
        Case("operator new, delete",
             [&] {
                 ListInit(&list);
                 for (int i = 0; i < count; i++)
                     ListPush(&list, &g_sounds[i]);
                 fgSoundList = &list;
             },
             [&](bool original) {
                 void *block = original ? Orig_OperatorNew(size, name) : ABaseSound::OperatorNew(size, name);
                 g_base = static_cast<uint8_t *>(block);
                 g_baseSize = size;
                 std::vector<uint32_t> &w = g_log;      // what new left, before delete runs
                 ListWords(w, &list, false);
                 Append(w, static_cast<ABaseSound *>(block)->name, 16);
                 if (original)
                     Orig_OperatorDelete(block, size);
                 else
                     ABaseSound::OperatorDelete(block, size);
                 g_base = NULL;
             },
             [&](std::vector<uint32_t> &w) {
                 ListWords(w, &list, false);
                 List_Destruct(&list, 0);
             });
    }
}

void FillRandom(void *data, size_t size) {
    uint8_t *b = static_cast<uint8_t *>(data);
    for (size_t i = 0; i < size; i++)
        b[i] = uint8_t(NextRandom() >> 24);
}

void TestConstructors() {
    for (int c = 0; c < 90; c++) {
        int kind = c % 3;
        int view = RandomInt(0, 4);
        uint32_t views = NextRandom() & 7;
        const char *name = kNames[RandomInt(0, 3)];
        const char *mixName = kMixNames[RandomInt(0, 3)];
        int bank = RandomInt(-1, 9);
        int patch = RandomInt(-1, 200);
        uint8_t fill = uint8_t(NextRandom());
        uint32_t answer = 0;
        Case("constructors",
             [&] {
                 memset(g_object, fill, sizeof(g_object));
                 g_base = g_object;
                 g_baseSize = sizeof(g_object);
                 ResetMixes();
                 fgActiveViews = views;
             },
             [&](bool original) {
                 void *result;
                 if (kind == 0)
                     result = original ? Orig_BaseConstruct(g_object, 0, mixName, view)
                                       : reinterpret_cast<ABaseSound *>(g_object)->Construct(mixName, view);
                 else if (kind == 1)
                     result = original ? Orig_BasicConstruct(g_object, 0, name, mixName)
                                       : reinterpret_cast<ABasic *>(g_object)->Construct(name, mixName);
                 else
                     result = original ? Orig_MenuConstruct(g_object, 0, bank, patch, mixName, view)
                                       : reinterpret_cast<AMenuSoundPriv *>(g_object)->Construct(bank, patch, mixName,
                                                                                               view);
                 answer = Tag(result);
             },
             [&](std::vector<uint32_t> &w) {
                 w.push_back(answer);
                 Append(w, g_object, 0x120);
                 Append(w, g_mixes, sizeof(g_mixes));
                 g_base = NULL;
             });
    }
}

void TestDestructors() {
    for (int c = 0; c < 120; c++) {
        int kind = c % 8;
        uint8_t start[0x120];
        FillRandom(start, sizeof(start));
        int32_t limited = RandomInt(0, 9);
        void *voice = NULL;
        uint32_t answer = 0;
        Case("destructors, Stop",
             [&] {
                 memcpy(g_object, start, sizeof(start));
                 g_base = g_object;
                 g_baseSize = sizeof(start);
                 ResetMixes();
                 reinterpret_cast<ABaseSound *>(g_object)->mix = MixByName("m");
                 ShadowLimitedCount = limited;
                 ABasic *basic = reinterpret_cast<ABasic *>(g_object);
                 basic->voice = NULL;
                 if (kind == 5 || kind == 6) {
                     voice = UMemory::FastAlloc(sizeof(AVoice), "AudioMgrShadow");
                     g_voiceBase = static_cast<uint8_t *>(voice);
                     basic->voice = static_cast<AVoice *>(voice);
                 }
             },
             [&](bool original) {
                 ASoundPlayParams params = {};
                 void *result = NULL;
                 switch (kind) {
                 case 0:
                     result = original ? Orig_BaseDelete(g_object, 0, 0)
                                       : reinterpret_cast<ABaseSound *>(g_object)->Delete(0);
                     break;
                 case 1:
                     result = original ? Orig_OneShotDelete(g_object, 0, 0)
                                       : reinterpret_cast<AOneShotSound *>(g_object)->Delete(0);
                     break;
                 case 2:
                     result = original ? Orig_LimitedDelete(g_object, 0, 0)
                                       : reinterpret_cast<ALimitedSound *>(g_object)->Delete(0);
                     break;
                 case 3:
                     result = original ? Orig_MenuDelete(g_object, 0, 0)
                                       : reinterpret_cast<AMenuSoundPriv *>(g_object)->Delete(0);
                     break;
                 case 4:
                 case 5:
                     result = original ? Orig_BasicDelete(g_object, 0, 0)
                                       : reinterpret_cast<ABasic *>(g_object)->Delete(0);
                     break;
                 default:
                     if (original)
                         Orig_BasicStop(g_object, 0, &params);
                     else
                         reinterpret_cast<ABasic *>(g_object)->Stop(&params);
                     break;
                 }
                 answer = Tag(result);
             },
             [&](std::vector<uint32_t> &w) {
                 w.push_back(answer);
                 Append(w, g_object, sizeof(start));
                 w.push_back(uint32_t(ShadowLimitedCount));
                 g_base = NULL;
                 g_voiceBase = NULL;
             });
    }
}

void RandomListener(int view) {
    Listener_Construct(&g_listener, 0, view);
    float *m = reinterpret_cast<float *>(&g_listener.matrix);
    for (int i = 0; i < 12; i++)
        m[i] = Uniform(-1.0f, 1.0f);
    g_listener.position.x = Uniform(-300.0f, 300.0f);
    g_listener.position.y = Uniform(-50.0f, 50.0f);
    g_listener.position.z = Uniform(-300.0f, 300.0f);
    g_listener.velocity.x = Uniform(-30.0f, 30.0f);
    g_listener.velocity.z = Uniform(-30.0f, 30.0f);
}

void TestPlay() {
    for (int c = 0; c < 150; c++) {
        int kind = c % 3;
        uint8_t start[0x120];
        FillRandom(start, sizeof(start));
        ABaseSound *s = reinterpret_cast<ABaseSound *>(start);
        s->vtable = FakeVtable();
        s->volume = Uniform(0.0f, 1.5f);
        s->pitch = Uniform(0.5f, 2.0f);
        ASoundPlayParams params;
        FillRandom(&params, sizeof(params));
        params.volume = Uniform(-0.2f, 1.2f);
        params.pitch = Uniform(0.0f, 3.0f);
        params.azimuth = Uniform(-1.0f, 1.0f);
        params.listener = &g_listener;
        int view = RandomInt(0, 2);
        bool finished = (NextRandom() & 1) != 0;
        int lookup = RandomInt(-1, 40);
        int handle = RandomInt(0, 15);
        void *voice = NULL;
        Case("Play",
             [&] {
                 memcpy(g_object, start, sizeof(start));
                 g_base = g_object;
                 g_baseSize = sizeof(start);
                 ResetMixes();
                 reinterpret_cast<ABaseSound *>(g_object)->mix = MixByName("play");
                 Listener_Construct(&g_listener, 0, view);
                 g_finished = finished;
                 g_lookupAnswer = lookup;
                 g_banks[0].handle = handle;
                 fgBanks[0] = &g_banks[0];
                 if (kind == 1)
                     reinterpret_cast<ABasic *>(g_object)->voice = NULL;
                 else if (kind == 2)
                     reinterpret_cast<ABasic *>(g_object)->voice = &g_voice;
                 voice = NULL;
             },
             [&](bool original) {
                 if (kind == 0) {
                     if (original)
                         Orig_MenuPlay(g_object, 0, &params);
                     else
                         reinterpret_cast<AMenuSoundPriv *>(g_object)->Play(&params);
                 } else {
                     if (original)
                         Orig_BasicPlay(g_object, 0, &params);
                     else
                         reinterpret_cast<ABasic *>(g_object)->Play(&params);
                 }
             },
             [&](std::vector<uint32_t> &w) {
                 ABasic *basic = reinterpret_cast<ABasic *>(g_object);
                 if (kind == 1) {
                     voice = basic->voice;
                     w.push_back(voice != NULL);
                     if (voice != NULL) {
                         // the voice from the pools: its contents, and the log's pointers to it as unknown
                         Append(w, voice, sizeof(AVoice));
                         basic->voice = NULL;
                         UMemory::FastFree(voice, sizeof(AVoice));
                     }
                 }
                 Append(w, g_object, sizeof(start));
                 g_base = NULL;
             });
    }
}

void TestTriggers() {
    for (int c = 0; c < 90; c++) {
        int which = c % 3;
        uint32_t views = NextRandom() & 7;
        int bank = RandomInt(0, 9);
        int patch = RandomInt(-1, 60);
        int view = RandomInt(0, 4);
        int lookup = RandomInt(-2, 30);
        const char *mixName = kMixNames[RandomInt(0, 3)];
        ASoundList list;
        Case("AMenuSound::Trigger",
             [&] {
                 ListInit(&list);
                 ListPush(&list, &g_sounds[0]);
                 fgSoundList = &list;
                 fgActiveViews = views;
                 ResetMixes();
                 for (int i = 0; i < kBankSlotCount; i++) {
                     g_banks[i].handle = 100 + i;
                     fgBanks[i] = &g_banks[i];
                 }
                 g_lookupAnswer = lookup;
             },
             [&](bool original) {
                 if (which == 0) {
                     if (original)
                         Orig_Trigger1(bank, patch, mixName, view);
                     else
                         AMenuSound::Trigger(bank, patch, mixName, view);
                 } else if (which == 1) {
                     if (original)
                         Orig_Trigger2("bank", "patch", mixName, view);
                     else
                         AMenuSound::Trigger("bank", "patch", mixName, view);
                 } else {
                     if (original)
                         Orig_Trigger3(bank, "patch", mixName, view);
                     else
                         AMenuSound::Trigger(bank, "patch", mixName, view);
                 }
             },
             [&](std::vector<uint32_t> &w) {
                 w.push_back(list.size);
                 if (list.size == 2) {
                     ABaseSound *made = static_cast<ABaseSound *>(list.head->prev->value);
                     Append(w, made, sizeof(AMenuSoundPriv));
                     ABaseSound::OperatorDelete(made, sizeof(AMenuSoundPriv));
                 }
                 w.push_back(list.size);
                 List_Destruct(&list, 0);
                 Append(w, g_mixes, sizeof(g_mixes));
             });
    }
}

// The manager over private sounds
void RandomSound(ABaseSound *s, const AListener *around) {
    FillRandom(s, sizeof(*s));
    s->vtable = FakeVtable();
    s->unknown04[0] = uint8_t(NextRandom());
    s->position.x = around->position.x + Uniform(-500.0f, 500.0f);
    s->position.y = around->position.y + Uniform(-20.0f, 20.0f);
    s->position.z = around->position.z + Uniform(-500.0f, 500.0f);
    s->velocity.x = Uniform(-40.0f, 40.0f);
    s->velocity.y = 0.0f;
    s->velocity.z = Uniform(-40.0f, 40.0f);
    s->forward.x = 0.0f; s->forward.y = 0.0f; s->forward.z = 1.0f;
    s->right.x = 1.0f; s->right.y = 0.0f; s->right.z = 0.0f;
    s->up.x = 0.0f; s->up.y = 1.0f; s->up.z = 0.0f;
    s->cameraAim = s->forward;
    s->minDistance = Uniform(0.0f, 20.0f);
    s->maxDistance = NextRandom() % 5 == 0 ? 0.0f : Uniform(10.0f, 600.0f);
    s->maxDistanceSq = s->maxDistance * s->maxDistance;
    s->falloff = Uniform(0.0f, 2.0f);
    s->volume = Uniform(0.0f, 1.2f);
    s->pitch = Uniform(0.5f, 2.0f);
    for (int i = 0; i < kSoundViewCount; i++) {
        int r = RandomInt(0, 3);
        s->lastVolumes[i] = r == 0 ? 0.0f : r == 1 ? -Uniform(0.0f, 1.0f) : Uniform(0.0f, 1.0f);
    }
    s->paused = NextRandom() % 5 == 0;
    s->mix = MixByName(kMixNames[RandomInt(0, 3)]);
    s->views = NextRandom() & 7;
    s->fade = Uniform(0.0f, 1.0f);
    s->fading = false;
}

void TestManager() {
    static const char *const kFailedNames[] = { "speech1", "speech2", "music3" };
    for (int c = 0; c < 400; c++) {
        int op = c % 8;
        int count = RandomInt(1, 6);
        int view = RandomInt(0, 2);
        RandomListener(view);
        AListener listener = g_listener;
        ResetMixes();
        ABaseSound start[8];
        for (int i = 0; i < count; i++)
            RandomSound(&start[i], &listener);
        Coord3 position;
        if (count > 1 && NextRandom() % 2 == 0)
            start[count - 1].position = start[0].position;      // two sounds at one place
        position = start[RandomInt(0, count - 1)].position;
        if (NextRandom() % 4 == 0)
            position.y += 1.0f;
        int failedCount = RandomInt(0, 3);
        uint8_t paused = uint8_t(NextRandom() & 1);
        uint8_t missionOver = uint8_t(NextRandom() & 1);
        uint32_t unknown6c = NextRandom();
        bool system = (NextRandom() & 1) != 0;
        ASoundList list;
        StreamNameList failed;
        Case("ASoundManager",
             [&] {
                 memcpy(g_sounds, start, sizeof(start));
                 g_listener = listener;
                 ResetMixes();
                 ListInit(&list);
                 for (int i = 0; i < count; i++)
                     ListPush(&list, &g_sounds[i]);
                 ListInit(&failed);
                 for (int i = 0; i < failedCount; i++)
                     ListPush(&failed, const_cast<char *>(kFailedNames[i]));
                 fgSoundList = &list;
                 ShadowFailedStreams = &failed;
                 for (int i = 0; i < kBankSlotCount; i++)
                     fgBanks[i] = &g_banks[i];
                 ShadowPaused = paused;
                 ShadowMissionOver = missionOver;
                 ShadowUnknown6c = unknown6c;
                 ShadowSystem = system ? (void *)g_object : NULL;
             },
             [&](bool original) {
                 switch (op) {
                 case 0:
                     if (original) Orig_BuildPaths(&g_listener); else ASoundManager::BuildPaths(&g_listener);
                     break;
                 case 1:
                     if (original) Orig_Stop(); else ASoundManager::Stop();
                     break;
                 case 2:
                     if (original) Orig_Pause(); else ASoundManager::Pause();
                     break;
                 case 3:
                     if (original) Orig_Resume(); else ASoundManager::Resume();
                     break;
                 case 4:
                     if (original) Orig_StopSoundPos(&position); else ASoundManager::StopSoundPos(&position);
                     break;
                 case 5:
                     if (original) Orig_Restart(); else ASoundManager::Restart();
                     break;
                 case 6:
                     if (original) Orig_ClearMission(); else ASoundManager::ClearMission();
                     break;
                 default:
                     if (original) Orig_SetMissionOver(); else ASoundManager::SetMissionOver();
                     break;
                 }
             },
             [&](std::vector<uint32_t> &w) {
                 Append(w, g_sounds, sizeof(ABaseSound) * count);
                 GlobalWords(w);
                 Append(w, g_mixes, sizeof(g_mixes));
                 ListWords(w, &list, false);
                 ListWords(w, &failed, true);
                 List_Destruct(&list, 0);
                 List_Destruct(&failed, 0);
             });
    }
}

void TestMisc() {
    for (int mode = -1; mode <= 5; mode++) {
        Case("ASystem::SetOpts", [&] {},
             [&](bool original) {
                 if (original)
                     Orig_SetOpts(mode);
                 else
                     ASystem::SetOpts(mode);
             },
             [&](std::vector<uint32_t> &) {});
    }
    static char kStreamNames[3][16] = { "level01", "speech", "x" };
    for (int c = 0; c < 30; c++) {
        int count = RandomInt(0, 3);
        char *name = kStreamNames[c % 3];
        StreamNameList failed;
        Case("ReportFailure",
             [&] {
                 ListInit(&failed);
                 for (int i = 0; i < count; i++)
                     ListPush(&failed, kStreamNames[i]);
                 ShadowFailedStreams = &failed;
             },
             [&](bool original) {
                 if (original)
                     Orig_ReportFailure(name);
                 else
                     ASoundManager::ReportFailure(name);
             },
             [&](std::vector<uint32_t> &w) {
                 ListWords(w, &failed, true);
                 List_Destruct(&failed, 0);
             });
    }
}

}  // namespace

void AudioMgrShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_AUDIOMGRSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    static bool ran;
    if (ran)
        return;
    ran = true;
    if (fgSoundList == NULL) {
        printf("[audiomgrshadow] no sound list yet: nothing tested\n");
        fflush(stdout);
        return;
    }
    FpControlGet(&g_x87, &g_sse);

    Globals saved;
    SaveGlobals(&saved);
    AVoice savedVoice = g_voice;

    HookInstall(0x0011d6a0, (const void *)&FakeMixAdd);
    HookInstall(0x0011d720, (const void *)&FakeMixGet);
    HookInstall(0x0011d750, (const void *)&FakeMixRemove);
    HookInstall(0x0011ca10, (const void *)&FakeMixGetVolume);
    HookInstall(0x0011cb80, (const void *)&FakeMixReset);
    HookInstall(0x00123ca0, (const void *)&FakeVoiceConstruct);
    HookInstall(0x00124690, (const void *)&FakeVoicePlay);
    HookInstall(0x00123c50, (const void *)&FakeViewDestruct);
    HookInstall(0x0003f400, (const void *)&FakeVoiceDestruct);
    HookInstall(0x000d36c0, (const void *)&FakeVoiceIsFinished);
    HookInstall(0x00126d40, (const void *)&FakeLookup);
    HookInstall(0x001269d0, (const void *)&FakeBankGet);
    HookInstall(0x001269f0, (const void *)&FakeBankRemove);
    HookInstall(0x001248b0, (const void *)&FakeFxUpdate);
    HookInstall(0x00124850, (const void *)&FakeFxPause);
    HookInstall(0x00124870, (const void *)&FakeFxResume);
    HookInstall(0x00123a30, (const void *)&FakePlayVoices);
    HookInstall(0x001237d0, (const void *)&FakeStreamGet);
    HookInstall(0x00121f90, (const void *)&FakeStreamFadeOut);
    HookInstall(0x00122430, (const void *)&FakeStreamStop);
    HookInstall(0x00125d40, (const void *)&FakeFaderGet);
    HookInstall(0x00125210, (const void *)&FakeFaderSetSecondary);
    HookInstall(0x0012f9e0, (const void *)&FakeEngineRemoveAll);
    HookInstall(0x00132192, (const void *)&FakePrintf);
    HookInstall(0x00133ee0, (const void *)&FakeRand);
    HookInstall(0x0013d090, (const void *)&FakeGetOpts);
    HookInstall(0x0013d0f0, (const void *)&FakeSetOps);

    TestFade();
    TestRandom();
    TestLists();
    TestNewDelete();
    TestConstructors();
    TestDestructors();
    TestPlay();
    TestTriggers();
    TestManager();
    TestMisc();

    HooksRemove();
    LoadGlobals(saved);
    g_voice = savedVoice;

    printf("[audiomgrshadow] sound manager and sounds: %ld cases, %ld checks, %ld differ (%ld calls faulted)\n",
           g_cases, g_checks, g_differ, g_faults);
    fflush(stdout);
}
