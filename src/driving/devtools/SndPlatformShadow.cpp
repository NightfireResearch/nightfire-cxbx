#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS   // as the build defines it (CMakeLists.txt)
#endif

#include "SndPlatformShadow.h"
#include "FpControl.h"

#include "../sound/snd/Mixer.h"
#include "../sound/snd/Platform.h"
#include "../platform/XboxStartup.h"
#include "../platform/XboxXapi.h"
#include "../sound/snd/Reverb.h"
#include "../sound/snd/Streams.h"
#include "../sound/snd/System.h"
#include "../sound/DirectSound.h"
#include "../platform/RealSystem.h"
#include "../sound/snd/Voices.h"
#include "../../common/xbeOriginal.h"

#include <windows.h>
#include <float.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_SNDPLATFORMSHADOW=1, at injection time on the loader's thread before the game runs: EA's platform
// driver (sound/snd/Platform.cpp, docs/driving/sound.md 4.6) against the originals, as sound.md 9.3 step 8 asks.
//
// Every case builds the driver's state in one workspace - 224 logical voices, their platform voices, the 180
// buffer nodes on the four free/active lists (built by the original SNDLINKI), fake DirectSound buffers, bank and
// packet memory, an arena standing in for the sound heap - and points the driver's globals at it, then snapshots
// the workspace and the globals (0x00244ba8..0x00244fc0, the mixer's FX hook pair at 0x002459a0, the speaker gain
// table at 0x00245378). The case's function runs with every function of the module swapped back to the original
// (common/xbeOriginal.h), its results and the state are saved, the snapshot is put back and ours runs. Compared:
// the state byte for byte, the return values and every call either side made to the recorders that stand in, by
// five-byte jumps, for DirectSound (entry, buffer identity as an index into the fake buffers, the arguments, the
// mix bin / filter / wave format / buffer description contents passed by pointer), the mixer and SFILTER_add,
// SNDMEMI, the critical section and SND mutex, the 100 Hz server, the packet player and the thread primitives -
// in order. SNDLINKI, memclr, SNDVOICEI_free and SNDI_aztospkrvol run for real on the workspace.
//
// Covered, over random voices and values: setvol, set3dpos, setpitch (pause/resume, the packet cap, the clamps),
// setfxlevel (both buses), lowpass, highpass, timemult, filteradd, stop, getvoicerange, getmastervoice,
// getsamplechan, mixvoicefree, outputcaps, outputset, DirectSound_SetListenerRelated (every mode), setfx, fxinit,
// dsndCreateBufferAndMixBins, the borrow FUN_0013d550, playtimbre and packetplay (both paths), dsndMixProcess,
// dsndMixInit, dsndMixStop, SNDPLATFORM_init, SNDPLATFORM_restore, SNDDRV_thread (a few ticks, stopped by the
// fake sleep) and the data-dead FUN_00142150 (its first-run warning prints during this test). A fault on both
// sides (the original's borrow with both pools empty) is counted, not fatal.
// ---------------------------------------------------------------------------------------------------------------

namespace {   // this file's own types

using SND::BufferNode;
using SND::DsBufferDesc;
using SND::DsFilterDesc;
using SND::DsMixBins;
using SND::DsWaveFormat;
using SND::PacketFormat;
using SND::PatchHeader;
using SND::PlatformVoice;
using SND::Voice;

// ---- this module's functions, swapped back to the originals for the original's side
const unsigned kMine[] = {
    0x0013d430, 0x0013d550, 0x0013d5c0, 0x0013d5e0, 0x0013d7d0, 0x0013d820, 0x0013d900, 0x0013d980, 0x0013da00,
    0x0013dae0, 0x0013dc50, 0x0013ddc0, 0x0013de50, 0x0013df50, 0x0013e0c0, 0x0013e320, 0x00140070, 0x00140880,
    0x00140a10, 0x00140a80, 0x00142150, 0x00142420, 0x00142460, 0x001424c0, 0x001427d0, 0x001429f0, 0x00142b10,
    0x00144960, 0x001449c0, 0 };

void Originals(bool original) {
    for (const unsigned *a = kMine; *a != 0; a++)
        XbeOriginal_Restore(*a, original);
}

// ---- the driver's globals the cases set (sound.md 2.2-2.6)
const uint32_t kRegion1 = 0x00244ba8, kRegion1Size = 0x00244fc0 - 0x00244ba8;
const uint32_t kRegion2 = 0x002459a0, kRegion2Size = 8;
const uint32_t kGainTable = 0x00245378, kGainTableSize = 256 * 6;

inline uint8_t &U8(uint32_t address) {
    return *(uint8_t *)(uintptr_t)address;
}
inline int16_t &S16(uint32_t address) {
    return *(int16_t *)(uintptr_t)address;
}
inline uint16_t &U16(uint32_t address) {
    return *(uint16_t *)(uintptr_t)address;
}
inline uint32_t &U32(uint32_t address) {
    return *(uint32_t *)(uintptr_t)address;
}
inline void *&Ptr(uint32_t address) {
    return *(void **)(uintptr_t)address;
}
inline uint32_t P(const void *p) {
    return (uint32_t)(uintptr_t)p;
}

// ---- random numbers
uint32_t g_seed = 1;

uint32_t Next() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
int Range(int lo, int hi) {   // inclusive
    return lo + (int)(Next() % (uint32_t)(hi - lo + 1));
}
bool Chance(int percent) {
    return (int)(Next() % 100) < percent;
}

// ---- the workspace
const int kVoices = 224, kNodes = 180, kBuffers = 512;
const int kArenaSize = 0x20000;

struct Workspace {
    uint8_t voices[kVoices * 0x88];
    uint8_t platform[kVoices * 0x18];
    BufferNode nodes[kNodes];
    uint8_t buffers[kBuffers];   // the fake DirectSound buffers: one byte each, identity = index
    uint8_t device[16];
    uint8_t bank[0x2000];
    uint8_t packets[0x4000];
    PatchHeader header;
    PacketFormat format;
    uint8_t *data[6];
    uint32_t nextBuffer;
    uint32_t arenaUsed;
    uint32_t playCursor, writeCursor;
    uint32_t tick, fakeSeed;
    int32_t sleeps;              // Sleep calls left before the flag below is cleared
    uint32_t sleepClears;
    int32_t packetCalls;
    int32_t packetNone;
    int32_t results[4];
    int32_t outFirst, outEnd;
    uint32_t pad;
    alignas(16) uint8_t arena[kArenaSize];
};

Workspace *W, *g_snapshot, *g_resultOriginal;
uint8_t g_r1Snapshot[kRegion1Size], g_r1Original[kRegion1Size], g_r1Saved[kRegion1Size];
uint8_t g_r2Snapshot[kRegion2Size], g_r2Original[kRegion2Size], g_r2Saved[kRegion2Size];
uint8_t g_gtSnapshot[kGainTableSize], g_gtOriginal[kGainTableSize], g_gtSaved[kGainTableSize];

inline Voice *VoiceAt(int index) {
    return (Voice *)(W->voices + index * 0x88);
}
inline PlatformVoice *PlatformAt(int index) {
    return (PlatformVoice *)(W->platform + index * 0x18);
}
inline SND::BufferList *FreeList(int pool) {
    return (SND::BufferList *)(uintptr_t)(0x00244c60u + (uint32_t)(pool * 12));
}
inline SND::BufferList *ActiveList(int pool) {
    return (SND::BufferList *)(uintptr_t)(0x00244c48u + (uint32_t)(pool * 12));
}

// ---- the recorders' log: per side, words in call order
const int kLogWords = 1 << 16;
uint32_t g_log[2][kLogWords];
int g_logLength[2];
bool g_logOverflow[2];
int g_side;

void Log(uint32_t id, int count, ...) {
    int &n = g_logLength[g_side];
    if (n + count + 2 > kLogWords) {
        g_logOverflow[g_side] = true;
        return;
    }
    g_log[g_side][n++] = id;
    g_log[g_side][n++] = (uint32_t)count;
    va_list args;
    va_start(args, count);
    for (int i = 0; i < count; i++)
        g_log[g_side][n++] = va_arg(args, uint32_t);
    va_end(args);
}
void LogWords(uint32_t id, const uint32_t *words, int count) {
    int &n = g_logLength[g_side];
    if (n + count + 2 > kLogWords) {
        g_logOverflow[g_side] = true;
        return;
    }
    g_log[g_side][n++] = id;
    g_log[g_side][n++] = (uint32_t)count;
    for (int i = 0; i < count; i++)
        g_log[g_side][n++] = words[i];
}

// A buffer's identity: its index among the fake buffers (0x8000xxxx otherwise: the raw pointer's low bits)
uint32_t Buf(const void *buffer) {
    uint32_t p = P(buffer), lo = P(W->buffers);
    if (p >= lo && p < lo + kBuffers)
        return p - lo;
    return 0x80000000u | (p & 0x7fffffffu);
}
void *NewBuffer() {
    uint32_t i = W->nextBuffer++;
    return &W->buffers[i % kBuffers];
}
uint32_t FloatBits(float f) {
    uint32_t u;
    memcpy(&u, &f, 4);
    return u;
}
uint32_t FakeNext() {   // the fakes' own sequence, kept in the workspace so both sides see the same one
    uint32_t s = W->fakeSeed;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    W->fakeSeed = s;
    return s;
}

// ---- DirectSound recorders (stdcall, as the entry points)
enum {
    L_CREATE = 1, L_EFFECTS, L_CREATEBUFFER, L_CREATESOUNDBUFFER, L_LISTENER, L_DEVRELEASE, L_RELEASE, L_BUFFERDATA,
    L_LOOP, L_SETPOS, L_GETPOS, L_PLAY, L_STOP, L_VOLUME, L_FREQUENCY, L_MIXBINS, L_MIXBINVOLUMES, L_FILTER,
    L_ENTER = 20, L_LEAVE, L_LOCK, L_UNLOCK, L_ALLOC, L_FREE, L_SERVER, L_FLUSH, L_TICK, L_SLEEP, L_THREAD,
    L_PRIORITY, L_GET, L_FREEFRAMES, L_PRE, L_POST,
    L_MIXCREATE = 40, L_MIXDESTROY, L_MIXAUDIO, L_PLAYINIT, L_MIXPLAY, L_MIXSTOP, L_DRYGAIN, L_MIXFX, L_MIXPITCH,
    L_MIXLOWPASS, L_MIXHIGHPASS, L_MIXTIME, L_INITREVERB, L_RESTOREREVERB, L_FILTERADD,
};

void LogDesc(uint32_t id, const DsBufferDesc *desc, uint32_t extra1, uint32_t extra2) {
    uint32_t w[16];
    int n = 0;
    w[n++] = extra1;
    w[n++] = extra2;
    w[n++] = desc->size;
    w[n++] = desc->flags;
    w[n++] = desc->bufferBytes;
    w[n++] = desc->mixBins == NULL ? 0 : 1;
    w[n++] = desc->inputMixBin;
    const DsWaveFormat *f = desc->format;
    if (f != NULL) {
        w[n++] = f->formatTag | ((uint32_t)f->channels << 16);
        w[n++] = f->samplesPerSec;
        w[n++] = f->avgBytesPerSec;
        w[n++] = f->blockAlign | ((uint32_t)f->bitsPerSample << 16);
        w[n++] = f->cbSize;
        if (f->formatTag == 0x69)
            w[n++] = f->samplesPerBlock;
    }
    LogWords(id, w, n);
}
void LogBins(uint32_t id, void *buffer, const DsMixBins *bins) {
    uint32_t w[2 + 2 * 8];
    int n = 0;
    w[n++] = Buf(buffer);
    w[n++] = bins->count;
    for (uint32_t i = 0; i < bins->count && i < 8; i++) {
        w[n++] = bins->pairs[i].bin;
        w[n++] = (uint32_t)bins->pairs[i].volume;
    }
    LogWords(id, w, n);
}

uint32_t __stdcall FakeDirectSoundCreate(void *guid, void **device, void *outer) {
    Log(L_CREATE, 3, P(guid), P(device), P(outer));
    *device = W->device;
    return 0;
}
uint32_t __stdcall FakeDownloadEffectsImage(void *device, const void *image, uint32_t size, uint32_t *location,
                                            void **desc) {
    (void)desc;
    Log(L_EFFECTS, 5, P(device), P(image), size, location[0], location[1]);
    return 0;
}
uint32_t __stdcall FakeCreateBuffer(const DsBufferDesc *desc, void **buffer) {
    *buffer = NewBuffer();
    LogDesc(L_CREATEBUFFER, desc, Buf(*buffer), 0);
    return 0;
}
uint32_t __stdcall FakeCreateSoundBuffer(void *device, const DsBufferDesc *desc, void **buffer, void *outer) {
    *buffer = NewBuffer();
    LogDesc(L_CREATESOUNDBUFFER, desc, P(device) ^ P(outer), P(buffer));
    return 0;
}
uint32_t __stdcall FakeSetI3DL2Listener(void *device, const void *properties, uint32_t apply) {
    Log(L_LISTENER, 3, P(device), P(properties), apply);
    return 0x1234;
}
uint32_t __stdcall FakeDeviceRelease(void *device) {
    Log(L_DEVRELEASE, 1, P(device));
    return 0;
}
uint32_t __stdcall FakeBufferRelease(void *buffer) {
    Log(L_RELEASE, 1, Buf(buffer));
    return 0;
}
uint32_t __stdcall FakeSetBufferData(void *buffer, void *data, uint32_t bytes) {
    Log(L_BUFFERDATA, 3, Buf(buffer), P(data), bytes);
    return 0;
}
uint32_t __stdcall FakeSetLoopRegion(void *buffer, uint32_t start, uint32_t length) {
    Log(L_LOOP, 3, Buf(buffer), start, length);
    return 0;
}
uint32_t __stdcall FakeSetCurrentPosition(void *buffer, uint32_t position) {
    Log(L_SETPOS, 2, Buf(buffer), position);
    return 0;
}
uint32_t __stdcall FakeGetCurrentPosition(void *buffer, uint32_t *play, uint32_t *write) {
    Log(L_GETPOS, 3, Buf(buffer), play != NULL ? 1u : 0u, write != NULL ? 1u : 0u);
    if (play != NULL)
        *play = W->playCursor;
    if (write != NULL)
        *write = W->writeCursor;
    return 0;
}
uint32_t __stdcall FakePlay(void *buffer, uint32_t r1, uint32_t r2, uint32_t flags) {
    Log(L_PLAY, 4, Buf(buffer), r1, r2, flags);
    return 0;
}
uint32_t __stdcall FakeStop(void *buffer) {
    Log(L_STOP, 1, Buf(buffer));
    return 0;
}
uint32_t __stdcall FakeSetVolume(void *buffer, int32_t volume) {
    Log(L_VOLUME, 2, Buf(buffer), (uint32_t)volume);
    return 0;
}
uint32_t __stdcall FakeSetFrequency(void *buffer, uint32_t frequency) {
    Log(L_FREQUENCY, 2, Buf(buffer), frequency);
    return 0;
}
uint32_t __stdcall FakeSetMixBins(void *buffer, const DsMixBins *bins) {
    LogBins(L_MIXBINS, buffer, bins);
    return 0;
}
uint32_t __stdcall FakeSetMixBinVolumes(void *buffer, const DsMixBins *bins) {
    LogBins(L_MIXBINVOLUMES, buffer, bins);
    return 0;
}
uint32_t __stdcall FakeSetFilter(void *buffer, const DsFilterDesc *desc) {
    Log(L_FILTER, 7, Buf(buffer), desc->mode, desc->q, desc->coefficients[0], desc->coefficients[1],
        desc->coefficients[2], desc->coefficients[3]);
    return 0;
}

// ---- the system module, the packet player, the thread primitives
void FakeEnter() {
    Log(L_ENTER, 0);
}
void FakeLeave() {
    Log(L_LEAVE, 0);
}
void FakeLock() {
    Log(L_LOCK, 0);
}
void FakeUnlock() {
    Log(L_UNLOCK, 0);
}
void *FakeAlloc(int32_t size) {
    uint32_t at = W->arenaUsed;
    uint32_t rounded = ((uint32_t)size + 15u) & ~15u;
    void *p = NULL;
    if (size >= 0 && at + rounded <= (uint32_t)kArenaSize) {
        p = W->arena + at;
        W->arenaUsed = at + rounded;
    }
    Log(L_ALLOC, 2, (uint32_t)size, P(p));
    return p;
}
void FakeFree(void *p) {
    Log(L_FREE, 1, P(p));
}
void FakeServer() {
    Log(L_SERVER, 0);
}
void FakeFlush() {
    Log(L_FLUSH, 0);
}
uint32_t FakeTick() {
    W->tick += FakeNext() % 26;
    Log(L_TICK, 1, W->tick);
    return W->tick;
}
void __stdcall FakeSleep(uint32_t ms) {
    Log(L_SLEEP, 1, ms);
    if (--W->sleeps <= 0)
        U8(W->sleepClears) = 0;
}
void *__stdcall FakeCreateThread(void *attributes, uint32_t stack, uint32_t entry, void *parameter, uint32_t flags,
                                 uint32_t *id) {
    Log(L_THREAD, 6, P(attributes), stack, entry, P(parameter), flags, P(id) != 0 ? 1u : 0u);
    return (void *)(uintptr_t)0x7eadu;
}
int __stdcall FakeSetThreadPriority(void *thread, int priority) {
    Log(L_PRIORITY, 2, P(thread), (uint32_t)priority);
    return 1;
}
uint8_t *FakePacketGet(int player, int channel, int *frames, int *other) {
    (void)other;
    uint8_t *p = NULL;
    int n = 0;
    if (channel == 0)   // the round's choice of data or none, the same for every channel
        W->packetNone = W->packetCalls >= 2000 || FakeNext() % 3 == 0;
    W->packetCalls++;
    if (!W->packetNone) {
        p = W->packets + FakeNext() % 0x1000;
        n = (int)(FakeNext() % 400);
    }
    *frames = n;
    Log(L_GET, 4, (uint32_t)player, (uint32_t)channel, P(p), (uint32_t)n);
    return p;
}
void FakeFreeFrames(int player, int channel, int frames) {
    Log(L_FREEFRAMES, 3, (uint32_t)player, (uint32_t)channel, (uint32_t)frames);
}
void FakePre() {
    Log(L_PRE, 0);
}
void FakePost() {
    Log(L_POST, 0);
}

// ---- the mixer and SFILTER_add
void FakeMixCreate(const uint32_t *params) {
    // byte 7 of the parameters is never written by the original (stack garbage): masked
    Log(L_MIXCREATE, 3, params[0], params[1] & 0x00ffffffu, params[2]);
}
void FakeMixDestroy() {
    Log(L_MIXDESTROY, 0);
}
void FakeMixAudio(int16_t **outputs, int frames) {
    Log(L_MIXAUDIO, 7, P(outputs[0]), P(outputs[1]), P(outputs[2]), P(outputs[3]), P(outputs[4]), P(outputs[5]),
        (uint32_t)frames);
}
void FakePlayInit(int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9, int a10, int a11, int a12,
                  int a13) {
    Log(L_PLAYINIT, 13, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13);
}
void FakeMixPlay(int voice) {
    Log(L_MIXPLAY, 1, (uint32_t)voice);
}
void FakeMixStop(int voice) {
    Log(L_MIXSTOP, 1, (uint32_t)voice);
}
void FakeDryGain(int voice, int speaker, float gain) {
    Log(L_DRYGAIN, 3, (uint32_t)voice, (uint32_t)speaker, FloatBits(gain));
}
void FakeMixFx(int voice, int send, float level) {
    Log(L_MIXFX, 3, (uint32_t)voice, (uint32_t)send, FloatBits(level));
}
void FakeMixPitch(int voice, uint32_t pitch) {
    Log(L_MIXPITCH, 2, (uint32_t)voice, pitch);
}
void FakeMixLowpass(int voice, float cutoff) {
    Log(L_MIXLOWPASS, 2, (uint32_t)voice, FloatBits(cutoff));
}
void FakeMixHighpass(int voice, int cutoff) {
    Log(L_MIXHIGHPASS, 2, (uint32_t)voice, (uint32_t)cutoff);
}
void FakeMixTime(int voice, int mult) {
    Log(L_MIXTIME, 2, (uint32_t)voice, (uint32_t)mult);
}
void FakeInitReverb(int rate, uint32_t description) {
    Log(L_INITREVERB, 2, (uint32_t)rate, description);
}
void FakeRestoreReverb() {
    Log(L_RESTOREREVERB, 0);
}
void *FakeFilterAdd(int voice, int channel, int filter) {
    Log(L_FILTERADD, 3, (uint32_t)voice, (uint32_t)channel, (uint32_t)filter);
    return NULL;
}

// ---- five-byte jumps over the originals the fakes stand in for, and over our ports of them
struct Hook {
    uint32_t at;
    uint8_t saved[5];
    bool on;
};
Hook g_hooks[128];
int g_hookCount;

void HookInstall(uint32_t at, const void *to) {
    if (g_hookCount >= 128)
        return;
    Hook &h = g_hooks[g_hookCount++];
    h.at = at;
    DWORD old;
    if (!VirtualProtect((void *)(uintptr_t)at, 5, PAGE_EXECUTE_READWRITE, &old)) {
        h.on = false;
        return;
    }
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

// A ported callee: the original's entry and our port's, both to the fake (our ports call each other directly, the
// originals by address). The answer is whether the original's entry took the jump.
bool HookBoth(uint32_t at, const void *ours, const void *to) {
    HookInstall(at, to);
    bool on = g_hooks[g_hookCount - 1].on;
    if ((uint32_t)(uintptr_t)ours != at)
        HookInstall((uint32_t)(uintptr_t)ours, to);
    return on;
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

void HooksInstall() {
    HookBoth(0x0017c259, (const void *)&DirectSoundCreate, (const void *)&FakeDirectSoundCreate);
    HookBoth(0x0017b59d, (const void *)&IDirectSound_DownloadEffectsImage, (const void *)&FakeDownloadEffectsImage);
    HookBoth(0x0017c2a0, (const void *)&DirectSoundCreateBuffer, (const void *)&FakeCreateBuffer);
    HookBoth(0x0017c09f, (const void *)&IDirectSound_CreateSoundBuffer, (const void *)&FakeCreateSoundBuffer);
    HookBoth(0x0017be1b, (const void *)&IDirectSound_SetI3DL2Listener, (const void *)&FakeSetI3DL2Listener);
    HookBoth(0x0017ad34, (const void *)&IDirectSound_Release, (const void *)&FakeDeviceRelease);
    HookBoth(0x0017ad4a, (const void *)&IDirectSoundBuffer_Release, (const void *)&FakeBufferRelease);
    HookBoth(0x0017be3b, (const void *)&IDirectSoundBuffer_SetBufferData, (const void *)&FakeSetBufferData);
    HookBoth(0x0017b670, (const void *)&IDirectSoundBuffer_SetLoopRegion, (const void *)&FakeSetLoopRegion);
    HookBoth(0x0017b6cc, (const void *)&IDirectSoundBuffer_SetCurrentPosition, (const void *)&FakeSetCurrentPosition);
    HookBoth(0x0017b6ac, (const void *)&IDirectSoundBuffer_GetCurrentPosition, (const void *)&FakeGetCurrentPosition);
    HookBoth(0x0017b634, (const void *)&IDirectSoundBuffer_Play, (const void *)&FakePlay);
    HookBoth(0x0017b658, (const void *)&IDirectSoundBuffer_Stop, (const void *)&FakeStop);
    HookBoth(0x0017b5c4, (const void *)&IDirectSoundBuffer_SetVolume, (const void *)&FakeSetVolume);
    HookBoth(0x0017b996, (const void *)&IDirectSoundBuffer_SetFrequency, (const void *)&FakeSetFrequency);
    HookBoth(0x0017b5fc, (const void *)&IDirectSoundBuffer_SetMixBins, (const void *)&FakeSetMixBins);
    HookBoth(0x0017b618, (const void *)&IDirectSoundBuffer_SetMixBinVolumes, (const void *)&FakeSetMixBinVolumes);
    HookBoth(0x0017b5e0, (const void *)&IDirectSoundBuffer_SetFilter, (const void *)&FakeSetFilter);
    HookBoth(0x0013b950, (const void *)&SNDSYS_entercritical, (const void *)&FakeEnter);
    HookBoth(0x0013b970, (const void *)&SNDSYS_leavecritical, (const void *)&FakeLeave);
    HookBoth(0x0013e7b0, (const void *)&SNDI_mutexlock, (const void *)&FakeLock);
    HookBoth(0x0013e7c0, (const void *)&SNDI_mutexunlock, (const void *)&FakeUnlock);
    HookBoth(0x0013f780, (const void *)&SNDMEMI_alloc, (const void *)&FakeAlloc);
    HookBoth(0x0013f880, (const void *)&SNDMEMI_free, (const void *)&FakeFree);
    HookBoth(0x0013b7b0, (const void *)&SNDSYSI_100hzserver, (const void *)&FakeServer);
    HookBoth(0x0013efd0, (const void *)&SNDPKTPLAYI_flushcallbackdata, (const void *)&FakeFlush);
    HookBoth(0x0013ee00, (const void *)&SNDPKTPLAYI_get, (const void *)&FakePacketGet);
    HookBoth(0x0013ef80, (const void *)&SNDPKTPLAYI_freeframes, (const void *)&FakeFreeFrames);
    HookBoth(0x0010e1e0, (const void *)&getTickCount, (const void *)&FakeTick);
    HookBoth(0x0010e9ab, (const void *)&Xbox_Sleep, (const void *)&FakeSleep);                          // Sleep
    HookBoth(0x0010ec6a, (const void *)&Xbox_CreateThread, (const void *)&FakeCreateThread);            // CreateThread
    HookBoth(0x0010ea0f, (const void *)&Xbox_SetThreadPriority, (const void *)&FakeSetThreadPriority);  // SetThreadPriority
    HookBoth(0x00141c20, (const void *)&MIX_create, (const void *)&FakeMixCreate);
    HookBoth(0x00141880, (const void *)&MIX_destroy, (const void *)&FakeMixDestroy);
    HookBoth(0x00142050, (const void *)&MIX_audio, (const void *)&FakeMixAudio);
    HookBoth(0x00141910, (const void *)&MIX_playinit, (const void *)&FakePlayInit);
    HookBoth(0x00141ad0, (const void *)&MIX_play, (const void *)&FakeMixPlay);
    HookBoth(0x00141b20, (const void *)&MIX_stop, (const void *)&FakeMixStop);
    HookBoth(0x00141bb0, (const void *)&SNDMIX_setdrygain, (const void *)&FakeDryGain);
    HookBoth(0x00141be0, (const void *)&MIX_setfxlevel, (const void *)&FakeMixFx);
    HookBoth(0x001420c0, (const void *)&MIX_setpitch, (const void *)&FakeMixPitch);
    HookBoth(0x00144af0, (const void *)&MIX_setlowpass, (const void *)&FakeMixLowpass);
    HookBoth(0x001464c0, (const void *)&MIX_sethighpass, (const void *)&FakeMixHighpass);
    HookBoth(0x00146570, (const void *)&MIX_settimemult, (const void *)&FakeMixTime);
    HookBoth(0x00143510, (const void *)&MIX_initreverb, (const void *)&FakeInitReverb);
    HookBoth(0x001434b0, (const void *)&MIX_restorereverb, (const void *)&FakeRestoreReverb);
    HookBoth(0x00144a30, (const void *)&SFILTER_add, (const void *)&FakeFilterAdd);
}

// ---- the original SNDLINKI, used to build the lists
void LinkInit(SND::BufferList *list) {
    ((void (*)(SND::BufferList *))0x0013f0a0u)(list);
}
void LinkPush(SND::BufferList *list, BufferNode *node) {
    ((void (*)(SND::BufferList *, BufferNode *))0x0013f0b0u)(list, node);
}

// ---- a case's state
struct CaseParams {
    int voice;
    int arg1, arg2, arg3, arg4, arg5;
    int freePercent[2];
} g_p;

bool g_activeNode[kNodes];

void CommonSetup() {
    for (size_t i = 0; i < sizeof(W->voices); i++)
        W->voices[i] = (uint8_t)Next();
    for (size_t i = 0; i < sizeof(W->platform); i++)
        W->platform[i] = (uint8_t)Next();
    for (size_t i = 0; i < sizeof(W->arena); i += 4)
        *(uint32_t *)&W->arena[i] = Next();
    for (int i = 0; i < kGainTableSize; i++)
        U8(kGainTable + (uint32_t)i) = (uint8_t)Next();
    W->fakeSeed = Next() | 1;
    W->tick = Next();
    W->nextBuffer = 200;
    W->arenaUsed = 0x8000;
    W->playCursor = (uint32_t)Range(0, 9599);
    W->writeCursor = (uint32_t)Range(0, 4799);
    memset(g_activeNode, 0, sizeof(g_activeNode));

    Ptr(0x00244f3c) = W->voices;
    S16(0x00244ed8) = kVoices;
    U8(0x00244cf9) = Chance(10) ? (uint8_t)Range(0, 4) : 0;
    U8(0x00244cfa) = 192;
    U8(0x00244d10) = (uint8_t)Range(1, 6);
    U16(0x00244cf2) = Chance(80) ? 48000 : (uint16_t)Range(8000, 48000);
    U8(0x00244cf6) = 32;
    Ptr(0x00244c80) = W->platform;
    Ptr(0x00244c44) = W->nodes;
    Ptr(0x00244c84) = W->device;
    U32(0x00244c78) = 152;
    U32(0x00244c7c) = 28;
    U32(0x00244edc) = Next();
    for (int i = 0; i < 6; i++) {
        Ptr(0x00244c88 + (uint32_t)(i * 4)) = &W->buffers[180 + i];
        Ptr(0x00244ca0 + (uint32_t)(i * 4)) = W->arena + i * 0x1400;
    }
    U32(0x00244cb8) = 2400;
    U32(0x00244cbc) = Chance(90) ? 960 : (uint32_t)Range(0, 2400);
    U32(0x00244cc0) = (uint32_t)Range(0, 149) * 16;
    U32(0x00244fb8) = 0;
    U32(0x00244fbc) = 0;
    U8(0x00244c3c) = 0;
    U8(0x00244c3d) = 0;
    for (int i = 0; i < kNodes; i++) {
        BufferNode *n = &W->nodes[i];
        uint8_t *raw = (uint8_t *)n;
        for (int k = 0; k < (int)sizeof(BufferNode); k++)
            raw[k] = (uint8_t)Next();
        n->buffer = reinterpret_cast<IDirectSoundBuffer *>(&W->buffers[i]);   // a fake: its index is its identity
        n->pool = i < 152 ? 0 : 1;
        n->platformVoice = (int16_t)Range(0, kVoices - 1);
        n->player = Chance(70) ? -1 : Range(0, 15);
    }
    for (int i = 0; i < kVoices; i++) {   // every voice's channel list in range: no stray stores
        Voice *v = VoiceAt(i);
        v->channels = (int8_t)Range(1, 6);
        for (int c = 0; c < 6; c++)
            v->platformVoices[c] = (int16_t)Range(0, kVoices - 1);
        v->master = (int16_t)(Chance(50) ? -1 : Range(0, kVoices - 1));
        // SNDVOICEI_free's multi-timbre bookkeeping: a sound key only with a last voice to find (a key whose
        // sounds have no last voice makes the original read voice -1)
        v->key = 0;
        v->keyLast = 0;
        if (Chance(10)) {
            v->key = (uint8_t)Range(1, 3);
            v->keyLast = 1;
            v->handle = Range(0, 0x7fffffff);
            v->inUse = (uint8_t)Range(1, 2);
        }
    }
    for (int i = 0; i < kVoices; i++) {
        PlatformVoice *p = PlatformAt(i);
        p->node = &W->nodes[Range(0, kNodes - 1)];
        p->playing = (uint8_t)(Chance(90) ? Range(0, 1) : 2);
        p->looping = Range(0, 1);
    }
    g_p.freePercent[0] = Chance(10) ? 0 : Range(30, 100);
    g_p.freePercent[1] = Chance(10) ? 0 : Range(30, 100);
}

// The four lists: the target voice's nodes (and any marked) on their pool's active list, the rest free with the
// case's odds
void BuildLists() {
    for (int pool = 0; pool < 2; pool++) {
        LinkInit(ActiveList(pool));
        LinkInit(FreeList(pool));
    }
    for (int i = 0; i < kNodes; i++) {
        BufferNode *n = &W->nodes[i];
        if (!g_activeNode[i] && Chance(g_p.freePercent[n->pool]))
            LinkPush(FreeList(n->pool), n);
        else
            LinkPush(ActiveList(n->pool), n);
    }
}

// A sound on 'channels' consecutive-in-the-sort voices: hardware ones from 0..191, mixer ones from 192..223; the
// master's index is its first channel. Each channel's platform voice gets its own node, marked active.
int MakeVoice(bool hardware, int channels) {
    static const uint16_t kHardwareModes[] = { 0x420, 0x420, 0x420, 0x400, 0x410, 0x10 };
    static const uint16_t kMixerModes[] = { 0x24, 0x24, 0x24, 0x4, 0x20 };
    int lo = hardware ? 0 : 192, hi = hardware ? 191 : 223;
    int chosen[6];
    int count = channels < 1 ? 1 : channels;
    for (int c = 0; c < count; c++) {
        bool again;
        do {
            chosen[c] = Range(lo, hi);
            again = false;
            for (int k = 0; k < c; k++)
                again |= chosen[k] == chosen[c];
        } while (again);
    }
    int index = chosen[0];
    Voice *v = VoiceAt(index);
    v->renderMode = hardware ? kHardwareModes[Range(0, 5)] : kMixerModes[Range(0, 4)];
    if (Chance(3))
        v->renderMode = (uint16_t)Next();
    v->channels = (int8_t)channels;
    v->master = -1;
    v->handle = (int32_t)(Next() & 0x7fffffff);
    for (int c = 0; c < 6; c++)
        v->platformVoices[c] = (int16_t)(c < count ? chosen[c] : Range(0, kVoices - 1));
    if (Chance(60))
        v->sampleRate = (uint16_t)(Chance(50) ? 48000 : Range(4000, 48000));
    int pick = Range(0, 99);
    if (pick < 50) {
        v->pitch = (uint16_t)Range(0x400, 0x2000);
    } else if (pick < 62) {
        v->pitch = 0;
    } else if (pick < 77) {
        v->pitch = (uint16_t)Range(0x2000, 0xffff);
    } else if (pick < 90) {   // a frequency on either side of either clamp
        static const uint32_t kEdges[] = { 187, 188, 191983, 191984 };
        uint32_t edge = kEdges[Range(0, 3)];
        for (int tries = 0; tries < 1000; tries++) {
            uint32_t rate = edge < 1000 ? (uint32_t)Range(1, 1000) : (uint32_t)Range(12000, 65535);
            uint32_t pitch = (edge * 4096u + rate - 1) / rate;
            if (pitch <= 0xffff && rate * pitch / 4096u == edge) {
                v->sampleRate = (uint16_t)rate;
                v->pitch = (uint16_t)pitch;
                break;
            }
        }
    }
    if (Chance(60)) {
        v->vol = (int8_t)Range(0, 127);
        v->progVol = (uint8_t)Range(0, 127);
    }
    if (Chance(50))
        v->fxSend = (int16_t)Range(0, 0x7fff);
    if (Chance(10))   // the products' zero signs: one factor 0, the other negative
        v->vol = 0;
    if (Chance(10))
        v->progVol = 0;
    if (Chance(10))
        v->fxSend = 0;
    for (int c = 0; c < count; c++) {
        int nodeIndex;
        do {
            nodeIndex = Range(0, kNodes - 1);
        } while (g_activeNode[nodeIndex]);
        g_activeNode[nodeIndex] = true;
        PlatformAt(chosen[c])->node = &W->nodes[nodeIndex];
        W->nodes[nodeIndex].platformVoice = (int16_t)chosen[c];
        if (c > 0) {
            Voice *s = VoiceAt(chosen[c]);
            s->master = (int16_t)index;
            s->handle = -1;
        }
    }
    return index;
}

int SomeChannels() {
    return Chance(60) ? 1 : Chance(60) ? 2 : Chance(10) ? 0 : Range(3, 6);
}

// ---- the kinds
typedef void (*SetupFn)(int index);
typedef void (*RunFn)();

void SetupVoice(int) {
    CommonSetup();
    g_p.voice = MakeVoice(Chance(55), SomeChannels());
    if (Chance(5))
        g_p.voice = Range(0, kVoices - 1);
    g_p.arg1 = Chance(80) ? Range(0, 1) : Range(0, 30000);
    if (Chance(50))
        g_p.arg1 = Range(-0x400, 0x3000);
    BuildLists();
}
void SetupPacketVoice(int) {   // setpitch's packet cap: the target's own platform voice a packet voice
    SetupVoice(0);
    PlatformAt(g_p.voice)->node->player = Range(0, 15);
}
void RunSetVol() {
    ((void (*)(int))0x0013df50u)(g_p.voice);
}
void Run3dPos() {
    ((void (*)(int))0x0013e0c0u)(g_p.voice);
}
void RunSetPitch() {
    W->results[0] = ((int (*)(int))0x0013e320u)(g_p.voice);
}
void RunSetFxLevel() {
    W->results[0] = ((int (*)(int, int))0x00140070u)(g_p.voice, g_p.arg1 & 1);
}
void RunLowpass() {
    ((void (*)(int, int))0x001429f0u)(g_p.voice, g_p.arg1);
}
void RunHighpass() {
    ((void (*)(int, int))0x00144960u)(g_p.voice, g_p.arg1);
}
void RunTimeMult() {
    W->results[0] = ((int (*)(int, int))0x001449c0u)(g_p.voice, g_p.arg1);
}
void RunFilterAdd() {
    W->results[0] = ((int (*)(int, int))0x001427d0u)(g_p.voice, g_p.arg1);
}
void RunStop() {
    W->results[0] = ((int (*)(int))0x0013de50u)(g_p.voice);
}

void SetupRange(int) {
    CommonSetup();
    static const int kModes[] = { 0x400, 0x10, 4, 0, 0x420, 0x24, 0x414 };
    g_p.arg1 = Chance(80) ? kModes[Range(0, 6)] : (int)Next();
    W->outFirst = (int32_t)Next();
    W->outEnd = (int32_t)Next();
    BuildLists();
}
void RunRange() {
    ((void (*)(int, int *, int *))0x0013d900u)(g_p.arg1, &W->outFirst, &W->outEnd);
}

void SetupMixQuery(int) {   // a mixer sound and one of its voices, as a MIX voice index
    CommonSetup();
    int channels = Range(1, 6);
    int master = MakeVoice(false, channels);
    Voice *v = VoiceAt(master);
    int pick = v->platformVoices[Range(0, channels - 1)];
    if (Chance(10))
        pick = Range(192, 223);
    g_p.arg1 = pick - U8(0x00244cf9) - U8(0x00244cfa);
    BuildLists();
}
void RunMasterVoice() {
    W->results[0] = ((int (*)(int))0x00142420u)(g_p.arg1);
}
void RunSampleChan() {
    W->results[0] = ((int (*)(int))0x00142460u)(g_p.arg1);
}
void RunMixVoiceFree() {
    ((void (*)(int))0x0013d5c0u)(g_p.arg1);
}

void SetupOptions(int) {
    CommonSetup();
    for (uint32_t a = 0x00244cc8; a < 0x00244ed8; a++)
        U8(a) = (uint8_t)Next();
    U8(0x00244ed0) = Chance(20) ? 1 : 0;
    U8(0x00244d15) = (uint8_t)Range(0, 8);
    for (int i = 0; i < 9; i++)
        U16(0x00244d18 + (uint32_t)(i * 2)) = Chance(40) ? 0 : (uint16_t)(Chance(50) ? 0x420 : Next());
    if (Chance(50)) {   // the caps as outputcaps leaves them, the options random around them
        U8(0x00244ccc) = 5;
        U8(0x00244ccd) = 5;
        U16(0x00244cc8) = 8000;
        U16(0x00244cca) = 48000;
    }
    BuildLists();
}
void RunOutputCaps() {
    W->results[0] = ((int (*)(void))0x0013da00u)();
}
void RunOutputSet() {
    W->results[0] = ((int (*)(void))0x0013dae0u)();
}

void SetupFx(int index) {
    CommonSetup();
    S16(0x00244f58) = (int16_t)(index < 70 ? index - 4 : Range(-0x8000, 0x7fff));
    static const int kPaths[] = { 0x420, 0x400, 0x10, 0x24, 4, 0, 0x20 };
    g_p.arg1 = Chance(90) ? kPaths[Range(0, 6)] : (int)Next();
    g_p.arg2 = Range(0, 3);
    U16(0x00244f44) = (uint16_t)(Chance(80) ? Range(0, 3) : Next());
    U32(0x00244f48) = Next();
    U32(0x002459a0) = Next();
    U32(0x002459a4) = Next();
    BuildLists();
}
void RunListener() {
    W->results[0] = ((int (*)(void))0x00140880u)();
}
void RunSetFx() {
    ((void (*)(int))0x00140a10u)(g_p.arg1);
}
void RunFxInit() {
    W->results[0] = ((int (*)(int, int))0x00140a80u)(g_p.arg2, g_p.arg1);
}

void SetupPools(int) {
    CommonSetup();
    g_p.arg1 = Chance(80) ? Range(0, 1) : Range(2, 9);
    if (Chance(50))
        g_p.freePercent[g_p.arg1 & 1] = 0;   // the borrow's usual reason
    BuildLists();
}
void RunCreateBuffer() {
    W->results[0] = (int32_t)Buf(((void *(*)(int))0x0013d430u)(g_p.arg1));
}
void RunBorrow() {
    ((void (*)(int))0x0013d550u)(g_p.arg1 & 1);
}

void SetupPlayTimbre(int) {
    CommonSetup();
    bool hardware = Chance(60);
    int channels = SomeChannels();
    g_p.voice = MakeVoice(hardware, channels);
    if (Chance(5))
        g_p.voice = Range(0, kVoices - 1);
    PatchHeader *h = &W->header;
    uint8_t *raw = (uint8_t *)h;
    for (size_t i = 0; i < sizeof(PatchHeader); i++)
        raw[i] = (uint8_t)Next();
    Voice *v = VoiceAt(g_p.voice);
    v->frames = Chance(80) ? Range(1, 200000) : (int32_t)Next();
    h->sampleRep = (uint8_t)(Chance(70) ? 20 : Chance(50) ? 10 : Range(0, 30));
    if (Chance(30)) {
        h->loopEnd = Chance(50) ? 0 : -Range(1, 100);
    } else {
        h->loopStart = Range(0, v->frames > 0 ? v->frames : 1000);
        h->loopEnd = h->loopStart + Range(1, 70000);
        if (Chance(20))
            h->loopEnd = (int32_t)(Next() & 0x7fffffff);
        if (Chance(40)) {   // the block rounding's edges
            h->loopStart = (h->loopStart & ~0x3f) | Range(0x1f, 0x21);
            h->loopEnd = (h->loopEnd & ~0x3f) | Range(0x1f, 0x21);
            if (Chance(50))
                v->frames = h->loopEnd + Range(0x1f, 0x21) - 0x1f + 0x1f;
        }
    }
    for (int c = 0; c < 6; c++)
        h->sampleOffsets[c] = Range(0, 0x1fff);
    g_p.arg1 = Range(0, 0x2000);       // time multiplier
    g_p.arg2 = (int)Next();            // the unused distort level
    g_p.arg3 = Range(-0x100, 0x3000);  // low pass
    g_p.arg4 = Range(0, 24000);        // high pass
    BuildLists();
}
void RunPlayTimbre() {
    W->results[0] = ((int (*)(PatchHeader *, uint8_t *, int, int, int, int, int))0x00142b10u)(
        &W->header, W->bank, g_p.voice, g_p.arg1, g_p.arg2, g_p.arg3, g_p.arg4);
}

void SetupPacketPlay(int) {
    CommonSetup();
    bool hardware = Chance(50);
    g_p.voice = MakeVoice(hardware, Chance(50) ? 1 : Range(1, 6));
    W->format.sampleRate = (uint16_t)(Chance(60) ? 48000 : Range(1000, 0xffff));
    W->format.channels = (uint8_t)Range(1, 2);
    W->format.sampleRep = (uint8_t)(Chance(60) ? 10 : Chance(50) ? 20 : Range(0, 30));
    for (int c = 0; c < 6; c++)
        W->data[c] = W->packets + Range(0, 0x3fff);
    VoiceAt(g_p.voice)->frames = Range(1, 100000);
    g_p.arg5 = Range(0, 15);           // the packet player
    g_p.arg1 = Range(0, 0x2000);
    g_p.arg2 = (int)Next();
    g_p.arg3 = Range(-0x100, 0x3000);
    g_p.arg4 = Range(0, 24000);
    BuildLists();
}
void RunPacketPlay() {
    W->results[0] = ((int (*)(int, int, int, int, int, int, const PacketFormat *, uint8_t *const *))0x001424c0u)(
        g_p.arg5, g_p.voice, g_p.arg1, g_p.arg2, g_p.arg3, g_p.arg4, &W->format, W->data);
}

void SetupMix(int) {
    CommonSetup();
    if (Chance(30)) {   // the target exactly at (or around) the ring's end, or at the last position
        uint32_t frames = U32(0x00244cb8), lead = U32(0x00244cbc);
        int32_t cursor = (int32_t)frames - (int32_t)lead + 16 * Range(-1, 1);
        if (cursor < 0)
            cursor += (int32_t)frames;
        W->writeCursor = (uint32_t)cursor * 2 + (uint32_t)Range(0, 31);
        if (Chance(30))
            U32(0x00244cc0) = ((U32(0x00244cbc) + (W->writeCursor >> 1)) & 0xffff0) % frames;
    }
    BuildLists();
}
void RunMixProcess() {
    ((void (*)(void))0x0013d820u)();
}
void RunMixInit() {
    ((void (*)(void))0x0013d5e0u)();
}
void RunMixStop() {
    ((void (*)(void))0x0013d7d0u)();
}

void SetupInit(int) {
    CommonSetup();
    if (Chance(30))
        S16(0x00244ed8) = (int16_t)Range(150, kVoices);
    BuildLists();
}
void RunInit() {
    W->results[0] = ((int (*)(void))0x0013dc50u)();
}

void SetupRestore(int) {
    CommonSetup();
    U8(0x00244c3d) = 1;
    U8(0x00244c3c) = Chance(70) ? 1 : 0;
    W->sleeps = Range(1, 3);
    W->sleepClears = 0x00244c3c;
    BuildLists();
}
void RunRestore() {
    W->results[0] = ((int (*)(void))0x0013ddc0u)();
}

void SetupThread(int) {
    CommonSetup();
    U8(0x00244c3d) = Chance(90) ? 1 : 0;
    U8(0x00244c3c) = 1;
    W->sleeps = Range(1, 5);
    W->sleepClears = 0x00244c3d;
    U32(0x00244c38) = W->tick + (uint32_t)Range(-40, 40);
    if (Chance(30))
        U32(0x00244fb8) = P((void *)&FakePre);
    if (Chance(30))
        U32(0x00244fbc) = P((void *)&FakePost);
    BuildLists();
}
void RunThread() {
    W->results[0] = (int32_t)((uint32_t (__stdcall *)(void *))0x0013d980u)(NULL);
}

void SetupRefill(int) {
    CommonSetup();
    int channels = Range(1, 3);
    int master = MakeVoice(true, channels);
    Voice *v = VoiceAt(master);
    BufferNode *node = PlatformAt(master)->node;
    uint32_t size = 36u * (uint32_t)Range(2, 100);
    uint8_t *memory = W->arena + 0x8000;
    W->arenaUsed = 0x8000 + 3 * 3600 + 16;
    node->pool = (uint8_t)Range(0, 1);
    node->player = Range(0, 15);
    bool hasPacket = Chance(50);
    for (int c = 0; c < channels; c++) {
        BufferNode *n = PlatformAt(v->platformVoices[c])->node;
        n->memory = memory + size * (uint32_t)c;
        n->size = size;
        n->packet = hasPacket ? W->packets + Range(0, 0x1000) : NULL;
        n->silence = (uint32_t)Range(0, (int)size * 2);
    }
    node->writePos = (uint32_t)Range(0, (int)size - 1);
    if (Chance(50))
        node->writePos -= node->writePos % 36;
    node->packetBytes = Range(-10, 2000);
    node->packetUsed = Range(0, 2000);
    W->playCursor = Chance(25) ? (uint32_t)Range(0, 35) : (uint32_t)Range(0, (int)size - 1);
    g_p.voice = master;
    BuildLists();
}
void RunRefill() {
    ((void (*)(BufferNode *))0x00142150u)(PlatformAt(g_p.voice)->node);
}

struct Kind {
    const char *name;
    int cases;
    SetupFn setup;
    RunFn run;
    int ran, differ, faults;
};

Kind g_kinds[] = {
    { "setvol", 400, SetupVoice, RunSetVol, 0, 0, 0 },
    { "set3dpos", 400, SetupVoice, Run3dPos, 0, 0, 0 },
    { "setpitch", 400, SetupVoice, RunSetPitch, 0, 0, 0 },
    { "setpitch packet", 150, SetupPacketVoice, RunSetPitch, 0, 0, 0 },
    { "setfxlevel", 400, SetupVoice, RunSetFxLevel, 0, 0, 0 },
    { "lowpass", 400, SetupVoice, RunLowpass, 0, 0, 0 },
    { "highpass", 150, SetupVoice, RunHighpass, 0, 0, 0 },
    { "timemult", 150, SetupVoice, RunTimeMult, 0, 0, 0 },
    { "filteradd", 150, SetupVoice, RunFilterAdd, 0, 0, 0 },
    { "stop", 300, SetupVoice, RunStop, 0, 0, 0 },
    { "getvoicerange", 100, SetupRange, RunRange, 0, 0, 0 },
    { "getmastervoice", 150, SetupMixQuery, RunMasterVoice, 0, 0, 0 },
    { "getsamplechan", 150, SetupMixQuery, RunSampleChan, 0, 0, 0 },
    { "mixvoicefree", 150, SetupMixQuery, RunMixVoiceFree, 0, 0, 0 },
    { "outputcaps", 20, SetupOptions, RunOutputCaps, 0, 0, 0 },
    { "outputset", 300, SetupOptions, RunOutputSet, 0, 0, 0 },
    { "setlistener", 140, SetupFx, RunListener, 0, 0, 0 },
    { "setfx", 100, SetupFx, RunSetFx, 0, 0, 0 },
    { "fxinit", 100, SetupFx, RunFxInit, 0, 0, 0 },
    { "createbuffer", 40, SetupPools, RunCreateBuffer, 0, 0, 0 },
    { "borrow", 150, SetupPools, RunBorrow, 0, 0, 0 },
    { "playtimbre", 400, SetupPlayTimbre, RunPlayTimbre, 0, 0, 0 },
    { "packetplay", 300, SetupPacketPlay, RunPacketPlay, 0, 0, 0 },
    { "mixprocess", 200, SetupMix, RunMixProcess, 0, 0, 0 },
    { "mixinit", 30, SetupMix, RunMixInit, 0, 0, 0 },
    { "mixstop", 20, SetupMix, RunMixStop, 0, 0, 0 },
    { "init", 20, SetupInit, RunInit, 0, 0, 0 },
    { "restore", 40, SetupRestore, RunRestore, 0, 0, 0 },
    { "thread", 60, SetupThread, RunThread, 0, 0, 0 },
    { "refill", 300, SetupRefill, RunRefill, 0, 0, 0 },
};

// ---- running a case on both sides
unsigned g_x87, g_sse;
int g_cases, g_checks, g_differ, g_faultsBoth, g_details;

void ResetFpu() {
    _fpreset();
    FpControlSetX87(g_x87);
    FpControlSetSse(g_sse);
}

bool Guarded(RunFn run) {
#ifdef _MSC_VER
    __try {
        run();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        ResetFpu();
        return false;
    }
#else
    run();
    return true;
#endif
}

void Detail(const char *format, ...) {
    if (g_details++ >= 10)
        return;
    char line[400];
    va_list args;
    va_start(args, format);
    vsnprintf(line, sizeof(line), format, args);
    va_end(args);
    printf("[sndplatformshadow]   %s\n", line);
}

void SaveRegions(uint8_t *r1, uint8_t *r2, uint8_t *gt) {
    memcpy(r1, (void *)(uintptr_t)kRegion1, kRegion1Size);
    memcpy(r2, (void *)(uintptr_t)kRegion2, kRegion2Size);
    memcpy(gt, (void *)(uintptr_t)kGainTable, kGainTableSize);
}
void LoadRegions(const uint8_t *r1, const uint8_t *r2, const uint8_t *gt) {
    memcpy((void *)(uintptr_t)kRegion1, r1, kRegion1Size);
    memcpy((void *)(uintptr_t)kRegion2, r2, kRegion2Size);
    memcpy((void *)(uintptr_t)kGainTable, gt, kGainTableSize);
}

int FirstDiff(const uint8_t *a, const uint8_t *b, size_t size, int *count) {
    int first = -1;
    *count = 0;
    for (size_t i = 0; i < size; i++) {
        if (a[i] == b[i])
            continue;
        if (first < 0)
            first = (int)i;
        (*count)++;
    }
    return first;
}

const char *Region(size_t offset) {
    if (offset < offsetof(Workspace, platform))
        return "voices";
    if (offset < offsetof(Workspace, nodes))
        return "platform voices";
    if (offset < offsetof(Workspace, buffers))
        return "nodes";
    if (offset < offsetof(Workspace, arena))
        return "workspace";
    return "arena";
}

void RunCase(Kind &kind, int index, int kindIndex) {
    g_seed = 0x9e3779b9u ^ ((uint32_t)kindIndex * 0x01000193u) ^ ((uint32_t)index * 0x85ebca6bu);
    if (g_seed == 0)
        g_seed = 1;
    memset(W, 0, sizeof(Workspace));
    memset(&g_p, 0, sizeof(g_p));
    kind.setup(index);
    memcpy(g_snapshot, W, sizeof(Workspace));
    SaveRegions(g_r1Snapshot, g_r2Snapshot, g_gtSnapshot);

    g_side = 0;
    g_logLength[0] = 0;
    g_logOverflow[0] = false;
    Originals(true);
    bool okOriginal = Guarded(kind.run);
    Originals(false);
    memcpy(g_resultOriginal, W, sizeof(Workspace));
    SaveRegions(g_r1Original, g_r2Original, g_gtOriginal);

    memcpy(W, g_snapshot, sizeof(Workspace));
    LoadRegions(g_r1Snapshot, g_r2Snapshot, g_gtSnapshot);
    g_side = 1;
    g_logLength[1] = 0;
    g_logOverflow[1] = false;
    bool okOurs = Guarded(kind.run);

    g_cases++;
    kind.ran++;
    bool differ = false;
    g_checks++;
    if (okOriginal != okOurs) {
        differ = true;
        Detail("%s case %d: the original %s, ours %s", kind.name, index, okOriginal ? "ran" : "faulted",
               okOurs ? "ran" : "faulted");
    } else if (!okOriginal) {
        g_faultsBoth++;
        kind.faults++;
    }
    int count;
    g_checks++;
    int first = FirstDiff((const uint8_t *)g_resultOriginal, (const uint8_t *)W, sizeof(Workspace), &count);
    if (first >= 0) {
        differ = true;
        Detail("%s case %d: %d workspace bytes differ, first in the %s at +0x%x: original %02x, ours %02x", kind.name,
               index, count, Region((size_t)first), (unsigned)first, ((const uint8_t *)g_resultOriginal)[first],
               ((const uint8_t *)W)[first]);
    }
    uint8_t r1[kRegion1Size], r2[kRegion2Size];
    static uint8_t gt[kGainTableSize];
    SaveRegions(r1, r2, gt);
    g_checks += 3;
    first = FirstDiff(g_r1Original, r1, kRegion1Size, &count);
    if (first >= 0) {
        differ = true;
        Detail("%s case %d: %d global bytes differ, first 0x%08x: original %02x, ours %02x", kind.name, index, count,
               kRegion1 + (unsigned)first, g_r1Original[first], r1[first]);
    }
    if (memcmp(g_r2Original, r2, kRegion2Size) != 0 || memcmp(g_gtOriginal, gt, kGainTableSize) != 0) {
        differ = true;
        Detail("%s case %d: the FX hook pair or the speaker gain table differ", kind.name, index);
    }
    g_checks++;
    if (g_logOverflow[0] || g_logOverflow[1] || g_logLength[0] != g_logLength[1] ||
        memcmp(g_log[0], g_log[1], (size_t)g_logLength[0] * 4) != 0) {
        differ = true;
        int n = g_logLength[0] < g_logLength[1] ? g_logLength[0] : g_logLength[1];
        int at = 0, call = 0;
        while (at < n && at + 1 < n) {   // the first differing call
            int words = 2 + (int)g_log[0][at + 1];
            if (at + words > n || memcmp(&g_log[0][at], &g_log[1][at], (size_t)words * 4) != 0)
                break;
            at += words;
            call++;
        }
        uint32_t a[5] = { 0, 0, 0, 0, 0 }, b[5] = { 0, 0, 0, 0, 0 };
        for (int k = 0; k < 5; k++) {
            if (at + k < g_logLength[0])
                a[k] = g_log[0][at + k];
            if (at + k < g_logLength[1])
                b[k] = g_log[1][at + k];
        }
        Detail("%s case %d: the calls differ (%d / %d words%s), first at call %d: original %u (%x %x %x), ours %u "
               "(%x %x %x)",
               kind.name, index, g_logLength[0], g_logLength[1],
               g_logOverflow[0] || g_logOverflow[1] ? ", log overflow" : "", call, a[0], a[2], a[3], a[4], b[0], b[2],
               b[3], b[4]);
    }
    if (differ) {
        g_differ++;
        kind.differ++;
    }
    LoadRegions(g_r1Snapshot, g_r2Snapshot, g_gtSnapshot);
}

}   // namespace

void SndPlatformShadow_Run(void) {
    const char *env = getenv("NIGHTFIRE_SNDPLATFORMSHADOW");
    if (env == NULL || atoi(env) == 0)
        return;
    FpControlGet(&g_x87, &g_sse);

    W = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_snapshot = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_resultOriginal = (Workspace *)VirtualAlloc(NULL, sizeof(Workspace), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (W == NULL || g_snapshot == NULL || g_resultOriginal == NULL) {
        printf("[sndplatformshadow] no memory\n");
        fflush(stdout);
        return;
    }

    int replaced = 0, total = 0;
    for (const unsigned *a = kMine; *a != 0; a++) {
        total++;
        if (XbeOriginal_Restore(*a, true)) {
            XbeOriginal_Restore(*a, false);
            replaced++;
        }
    }

    SaveRegions(g_r1Saved, g_r2Saved, g_gtSaved);
    HooksInstall();
    int kindCount = (int)(sizeof(g_kinds) / sizeof(g_kinds[0]));
    for (int k = 0; k < kindCount; k++) {
        Kind &kind = g_kinds[k];
        for (int i = 0; i < kind.cases; i++)
            RunCase(kind, i, k);
    }
    HooksRemove();
    LoadRegions(g_r1Saved, g_r2Saved, g_gtSaved);

    for (int k = 0; k < kindCount; k++)
        if (g_kinds[k].differ != 0 || g_kinds[k].faults != 0)
            printf("[sndplatformshadow]   %s: %d of %d cases differ, %d faulted on both sides\n", g_kinds[k].name,
                   g_kinds[k].differ, g_kinds[k].ran, g_kinds[k].faults);
    printf("[sndplatformshadow] platform driver vs originals (%d of %d replaced): %d cases, %d checks, %d differ "
           "(%d faulted on both sides)\n",
           replaced, total, g_cases, g_checks, g_differ, g_faultsBoth);
    fflush(stdout);
    VirtualFree(W, 0, MEM_RELEASE);
    VirtualFree(g_snapshot, 0, MEM_RELEASE);
    VirtualFree(g_resultOriginal, 0, MEM_RELEASE);
    W = g_snapshot = g_resultOriginal = NULL;
}
