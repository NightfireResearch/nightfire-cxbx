#include "Platform.h"
#include "Voices.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _MSC_VER
#pragma float_control(precise, on)
#pragma fp_contract(off)
#else
#pragma STDC FP_CONTRACT OFF
#endif

// ---------------------------------------------------------------------------------------------------------------
// EA's sound library, module F (docs/driving/sound.md 2.2, 2.3, 3.1, 4.6, 5.1): the Xbox platform driver.
//
// Two voice paths. Logical voices 0..191 (render mode 0x400, "hardware") each own pooled DirectSound buffers: 152
// Xbox ADPCM and 28 PCM16, borrowed across the pools when one runs dry (FUN_0013d550), playing the bank's sample
// in place (SetBufferData with a pointer into the bank). Voices 192..223 (mode 4) are EA's software mixer's: MIX
// calls, mixed by dsndMixProcess into six looping 50 ms PCM rings, one per 5.1 speaker, 20 ms ahead of ring 0's
// write cursor. SNDDRV_thread is the SND thread: every 10 ms, under SoundMutex, the 100 Hz server, the mix and the
// packet callbacks.
//
// Each function is the original at its address, ported from the listing. DirectSound is called by the original
// entry points' addresses (our seam, src/driving/sound/dsndSeam.cpp, stdcall with the interface first), the mixer
// (module G) and the system module (SNDLINKI, SNDMEMI, the critical section, the thread primitives) by their
// addresses too; SFILTER_add as well, though it is ours, so that the shadow test can stand a recorder in front of
// it. The library's globals stay where they are. x87 only in the gain and frequency products, computed in double
// in the original's order (exact here: every product is of integers and a float, and fits 53 bits) and truncated
// as __ftol2 truncates; the low-pass ratio is a division, rounded to double as a 53-bit x87 would. The integer
// operands are multiplied as doubles, as FILD/FIMUL do, never as ints: a zero product then keeps the sign the x87
// gives it (0 x -5 = -0), which reaches the dry gains MIX is handed.
//
// FUN_00142150, the hardware packet voice's ring refill, is data-dead (3.8: no stream plays on a hardware voice):
// provisional, it warns once when it first runs. devtools/SndPlatformShadow.cpp compares the rest with the
// originals on snapshots of the driver's state, DirectSound and the mixer replaced by recorders.
// ---------------------------------------------------------------------------------------------------------------

// The warning beside a provisional port (the pattern of eagl/anim/AnimUntested.h). Guarded: other sound modules
// may define the same macro.
#ifndef SND_UNTESTED
inline void SndPlatformUntested(const char *what) {
    printf("[snd] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define SND_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            SndPlatformUntested(what); \
        } \
    } while (0)
#endif

namespace {

using SND::BufferNode;
using SND::DsBufferDesc;
using SND::DsFilterDesc;
using SND::DsMixBinPair;
using SND::DsMixBins;
using SND::DsWaveFormat;
using SND::BufferList;
using SND::PlatformVoice;
using SND::Voice;

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

// ---- the driver's globals (sound.md 2.2-2.6)
const uint32_t kNextDeadline = 0x00244c38;     // the SND thread's next deadline (ms)
const uint32_t kIsRunning = 0x00244c3c;        // SNDDRV_isRunning
const uint32_t kKeepRunning = 0x00244c3d;      // SNDDRV_shouldContinueRunning
const uint32_t kSetFxFunc = 0x00244c40;        // p_setfx_func, written, never read
const uint32_t kNodes = 0x00244c44;            // SND_BUFFER_LIST: 180 BufferNodes
const uint32_t kActiveLists = 0x00244c48;      // BufferList[2]
const uint32_t kFreeLists = 0x00244c60;        // BufferList[2]
const uint32_t kPoolSizes = 0x00244c78;        // int32[2]: 152, 28
const uint32_t kPlatformVoices = 0x00244c80;   // PlatformVoice *
const uint32_t kDevice = 0x00244c84;           // the IDirectSound
const uint32_t kRings = 0x00244c88;            // six DirectSound buffers
const uint32_t kRingMemory = 0x00244ca0;       // their six memory blocks
const uint32_t kRingFrames = 0x00244cb8;       // 2400
const uint32_t kMixLead = 0x00244cbc;          // 960
const uint32_t kMixPosition = 0x00244cc0;      // the next frame to mix
const uint32_t kOutputRate = 0x00244cf2;       // platformSampleRate, u16
const uint32_t kMixVoiceCount = 0x00244cf6;    // u8: 32
const uint32_t kMixOffset1 = 0x00244cf9;       // u8 mixVoiceOffset1: 0
const uint32_t kMixOffset2 = 0x00244cfa;       // u8 mixVoiceOffset2: 192 hardware voices
const uint32_t kOutputMode = 0x00244d10;       // u8 speakers
const uint32_t kModeCount = 0x00244d15;        // u8 render modes configured
const uint32_t kModes = 0x00244d18;            // u16 each
const uint32_t kInited = 0x00244ed0;           // SNDSYS_is_inited
const uint32_t kNumVoices = 0x00244ed8;        // s16
const uint32_t kVoices = 0x00244f3c;           // sndvoicei_buffer
const uint32_t kFxBus0 = 0x00244f44;           // FXBUS[0]: mode (u16), +4 its description
const uint32_t kFxBus1Mode = 0x00244f58;       // the hardware bus's mode (s16)
const uint32_t kPreFrame = 0x00244fb8;         // SNDDRVPreFrameCb
const uint32_t kPostFrame = 0x00244fbc;        // SNDDRVPostFrameCb
const uint32_t kFxInitFunc = 0x002459a0;       // the mixer's FX init (SNDMIXI_fxinit)
const uint32_t kFxHook = 0x002459a4;           // unpackerInitFuncs[0]: the per-slice FX hook

// ---- constant tables and the x87 constants, read where the original reads them
const uint32_t kVolumeTable = 0x001a71e0;      // int16 [128] hundredths of a dB
const uint32_t kRingBins = 0x001a72e0;         // int32 [7]: 2, 1, 5, 4, 0, 3, 11
const uint32_t kLowpassTable = 0x001a7350;     // uint16 [256]
const uint32_t kEffectsImage = 0x001a3e80;
const uint32_t kListenerPresets = 0x001d98d8;  // I3DL2 listener properties, 0x30 each
const uint32_t kFxDefault = 0x001d98c0;
inline double K(uint32_t address) {
    return (double)*(const float *)(uintptr_t)address;
}
const uint32_t kOver127 = 0x001a296c;          // float 1/127
const uint32_t kHalf = 0x0018e9f8;             // double 0.5
const uint32_t kDryScale = 0x001a72fc;         // float 1.892e-9
const uint32_t kOver4096 = 0x001a7300;         // float 1/4096
const uint32_t kFxScale = 0x001a7304;          // float 2.403e-7
const uint32_t kPacketSeconds = 0x00189ff8;    // float 0.03
const uint32_t kHalfF = 0x00189eb0;            // float 0.5
const uint32_t kOne = 0x00189de8;              // float 1.0

// The original addresses of this module's functions (the thread, the voice-free callback), stored where the
// original stores them
const uint32_t kThreadEntry = 0x0013d980;
const uint32_t kMixVoiceFreeEntry = 0x0013d5c0;
const uint32_t kSetFxEntry = 0x00140a10;

inline Voice *VoiceAt(int index) {
    return (Voice *)((uint8_t *)Ptr(kVoices) + index * 0x88);
}
inline PlatformVoice *PlatformAt(int index) {
    return (PlatformVoice *)((uint8_t *)Ptr(kPlatformVoices) + index * 0x18);
}
inline BufferList *FreeList(int pool) {
    return (BufferList *)(uintptr_t)(kFreeLists + (uint32_t)(pool * 12));
}
inline BufferList *ActiveList(int pool) {
    return (BufferList *)(uintptr_t)(kActiveLists + (uint32_t)(pool * 12));
}
inline int MixIndex(int platformVoice) {   // the MIX voice of a platform voice
    return platformVoice - U8(kMixOffset1) - U8(kMixOffset2);
}
inline int16_t VolumeDb(int index) {
    return *(const int16_t *)(uintptr_t)(kVolumeTable + (uint32_t)(index * 2));
}
// The volume index from a gain's high byte (the original's MOVSX byte [gains + 2k + 1])
inline int16_t GainDb(const PlatformVoice *pv, int k) {
    return VolumeDb(((const int8_t *)pv->gains)[2 * k + 1]);
}

// __ftol2: truncate ST0 to 64 bits; the callers keep EAX
inline int32_t Ftol(double d) {
    return (int32_t)(int64_t)d;
}

// ---- DirectSound, the seam's entry points (stdcall, the interface first)
inline uint32_t DirectSoundCreate(void *guid, void **device, void *outer) {
    return ((uint32_t (__stdcall *)(void *, void **, void *))0x0017c259u)(guid, device, outer);
}
inline uint32_t DownloadEffectsImage(void *device, const void *image, uint32_t size, void *location, void **desc) {
    return ((uint32_t (__stdcall *)(void *, const void *, uint32_t, void *, void **))0x0017b59du)(device, image,
                                                                                                 size, location,
                                                                                                 desc);
}
inline uint32_t DirectSoundCreateBuffer(const DsBufferDesc *desc, void **buffer) {
    return ((uint32_t (__stdcall *)(const DsBufferDesc *, void **))0x0017c2a0u)(desc, buffer);
}
inline uint32_t CreateSoundBuffer(void *device, const DsBufferDesc *desc, void **buffer, void *outer) {
    return ((uint32_t (__stdcall *)(void *, const DsBufferDesc *, void **, void *))0x0017c09fu)(device, desc, buffer,
                                                                                              outer);
}
inline uint32_t SetI3DL2Listener(void *device, const void *properties, uint32_t apply) {
    return ((uint32_t (__stdcall *)(void *, const void *, uint32_t))0x0017be1bu)(device, properties, apply);
}
inline uint32_t DeviceRelease(void *device) {
    return ((uint32_t (__stdcall *)(void *))0x0017ad34u)(device);
}
inline uint32_t BufferRelease(void *buffer) {
    return ((uint32_t (__stdcall *)(void *))0x0017ad4au)(buffer);
}
inline uint32_t SetBufferData(void *buffer, void *data, uint32_t bytes) {
    return ((uint32_t (__stdcall *)(void *, void *, uint32_t))0x0017be3bu)(buffer, data, bytes);
}
inline uint32_t SetLoopRegion(void *buffer, uint32_t start, uint32_t length) {
    return ((uint32_t (__stdcall *)(void *, uint32_t, uint32_t))0x0017b670u)(buffer, start, length);
}
inline uint32_t SetCurrentPosition(void *buffer, uint32_t position) {
    return ((uint32_t (__stdcall *)(void *, uint32_t))0x0017b6ccu)(buffer, position);
}
inline uint32_t GetCurrentPosition(void *buffer, uint32_t *play, uint32_t *write) {
    return ((uint32_t (__stdcall *)(void *, uint32_t *, uint32_t *))0x0017b6acu)(buffer, play, write);
}
inline uint32_t Play(void *buffer, uint32_t reserved1, uint32_t reserved2, uint32_t flags) {
    return ((uint32_t (__stdcall *)(void *, uint32_t, uint32_t, uint32_t))0x0017b634u)(buffer, reserved1, reserved2,
                                                                                     flags);
}
inline uint32_t Stop(void *buffer) {
    return ((uint32_t (__stdcall *)(void *))0x0017b658u)(buffer);
}
inline uint32_t SetVolume(void *buffer, int32_t volume) {
    return ((uint32_t (__stdcall *)(void *, int32_t))0x0017b5c4u)(buffer, volume);
}
inline uint32_t SetFrequency(void *buffer, uint32_t frequency) {
    return ((uint32_t (__stdcall *)(void *, uint32_t))0x0017b996u)(buffer, frequency);
}
inline uint32_t SetMixBins(void *buffer, const DsMixBins *bins) {
    return ((uint32_t (__stdcall *)(void *, const DsMixBins *))0x0017b5fcu)(buffer, bins);
}
inline uint32_t SetMixBinVolumes(void *buffer, const DsMixBins *bins) {
    return ((uint32_t (__stdcall *)(void *, const DsMixBins *))0x0017b618u)(buffer, bins);
}
inline uint32_t SetFilter(void *buffer, const DsFilterDesc *desc) {
    return ((uint32_t (__stdcall *)(void *, const DsFilterDesc *))0x0017b5e0u)(buffer, desc);
}

// ---- the system module (A), the mixer (G) and SFILTER_add, by address
inline void EnterCritical() {
    ((void (*)(void))0x0013b950u)();                       // SNDSYS_entercritical
}
inline void LeaveCritical() {
    ((void (*)(void))0x0013b970u)();                       // SNDSYS_leavecritical
}
inline void MutexLock() {
    ((void (*)(void))0x0013e7b0u)();                       // SNDI_mutexlock
}
inline void MutexUnlock() {
    ((void (*)(void))0x0013e7c0u)();                       // SNDI_mutexunlock
}
inline void *MemAlloc(int32_t size) {
    return ((void *(*)(int32_t))0x0013f780u)(size);        // SNDMEMI_alloc
}
inline void MemFree(void *p) {
    ((void (*)(void *))0x0013f880u)(p);                    // SNDMEMI_free
}
inline void MemClear(void *p, int32_t size) {
    ((void (*)(void *, int32_t))0x0013f600u)(p, size);     // memclr
}
inline void LinkInit(BufferList *list) {
    ((void (*)(BufferList *))0x0013f0a0u)(list);             // SNDLINKI_init
}
inline void LinkPush(BufferList *list, BufferNode *node) {
    ((void (*)(BufferList *, BufferNode *))0x0013f0b0u)(list, node);    // SNDLINKI_push
}
inline void LinkPushTail(BufferList *list, BufferNode *node) {
    ((void (*)(BufferList *, BufferNode *))0x0013f0e0u)(list, node);    // SNDLINKI_pushtail
}
inline BufferNode *LinkPop(BufferList *list) {
    return ((BufferNode *(*)(BufferList *))0x0013f110u)(list);          // SNDLINKI_pop
}
inline void LinkRemove(BufferList *list, BufferNode *node) {
    ((void (*)(BufferList *, BufferNode *))0x0013f140u)(list, node);    // SNDLINKI_remove
}
inline void Server100Hz() {
    ((void (*)(void))0x0013b7b0u)();                       // SNDSYSI_100hzserver
}
inline void FlushPacketCallbacks() {
    ((void (*)(void))0x0013efd0u)();                       // SNDPKTPLAYI_flushcallbackdata
}
inline uint8_t *PacketGet(int player, int channel, int *frames, int *other) {
    return ((uint8_t *(*)(int, int, int *, int *))0x0013ee00u)(player, channel, frames, other);   // SNDPKTPLAYI_get
}
inline void PacketFreeFrames(int player, int channel, int frames) {
    ((void (*)(int, int, int))0x0013ef80u)(player, channel, frames);  // SNDPKTPLAYI_freeframes
}
inline uint32_t GetTickCount32() {
    return ((uint32_t (*)(void))0x0010e1e0u)();            // getTickCount
}
inline void SleepMs(uint32_t ms) {
    ((void (__stdcall *)(uint32_t))0x0010e9abu)(ms);       // SleepMilliseconds
}
inline void *CreateThreadX(void *attributes, uint32_t stack, uint32_t entry, void *parameter, uint32_t flags,
                           uint32_t *id) {
    return ((void *(__stdcall *)(void *, uint32_t, uint32_t, void *, uint32_t, uint32_t *))0x0010ec6au)(
        attributes, stack, entry, parameter, flags, id);  // CreateThread
}
inline void SetThreadPriorityX(void *thread, int priority) {
    ((int (__stdcall *)(void *, int))0x0010ea0fu)(thread, priority);  // SetThreadPriority
}

inline void MixCreate(const void *params) {
    ((void (*)(const void *))0x00141c20u)(params);         // MIX_create
}
inline void MixDestroy() {
    ((void (*)(void))0x00141880u)();                       // MIX_destroy
}
inline void MixAudio(int16_t **outputs, int frames) {
    ((void (*)(int16_t **, int))0x00142050u)(outputs, frames);        // MIX_audio
}
inline void MixPlayInit(int voice, int sampleRep, int kind, int p3, int p4, int p5, int p6, int p7, int p8, int p9,
                        int p10, int p11, int requester) {
    ((void (*)(int, int, int, int, int, int, int, int, int, int, int, int, int))0x00141910u)(
        voice, sampleRep, kind, p3, p4, p5, p6, p7, p8, p9, p10, p11, requester);   // MIX_playinit
}
inline void MixPlay(int voice) {
    ((void (*)(int))0x00141ad0u)(voice);                   // MIX_play
}
inline void MixStop(int voice) {
    ((void (*)(int))0x00141b20u)(voice);                   // MIX_stop
}
inline void MixSetDryGain(int voice, int speaker, float gain) {
    ((void (*)(int, int, float))0x00141bb0u)(voice, speaker, gain);   // SNDMIX_setdrygain
}
inline void MixSetFxLevel(int voice, int send, float level) {
    ((void (*)(int, int, float))0x00141be0u)(voice, send, level);     // MIX_setfxlevel
}
inline void MixSetPitch(int voice, uint32_t pitch) {
    ((void (*)(int, uint32_t))0x001420c0u)(voice, pitch);  // MIX_setpitch
}
inline void MixSetLowpass(int voice, float cutoff) {
    ((void (*)(int, float))0x00144af0u)(voice, cutoff);    // MIX_setlowpass
}
inline void MixSetHighpass(int voice, int cutoff) {
    ((void (*)(int, int))0x001464c0u)(voice, cutoff);      // MIX_sethighpass
}
inline void MixSetTimeMult(int voice, int mult) {
    ((void (*)(int, int))0x00146570u)(voice, mult);        // MIX_settimemult
}
inline void MixInitReverb(int rate, uint32_t description) {
    ((void (*)(int, uint32_t))0x00143510u)(rate, description);        // MIX_initreverb
}
inline void MixRestoreReverb() {
    ((void (*)(void))0x001434b0u)();                       // MIX_restorereverb
}
inline void FilterAdd(int voice, int channel, int filter) {
    ((void *(*)(int, int, int))0x00144a30u)(voice, channel, filter);  // SFILTER_add (ours; by address, see above)
}

// MIX_create's argument as dsndMixInit builds it on the stack
struct MixParams {
    uint32_t rate;               // +0
    uint8_t voices;              // +4
    uint8_t outputs;             // +5 6
    uint8_t zero;                // +6
    uint8_t unset;               // +7 never written by the original (stack garbage there); 0 here
    uint32_t voiceFree;          // +8 0x0013d5c0
};

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The buffer pools
// ---------------------------------------------------------------------------------------------------------------

// One pooled buffer: 48 kHz mono, Xbox ADPCM (pool 0) or PCM16 (pool 1), routed FC FR BR BL FL LFE at 0 dB and the
// first FX send at -100 dB.
// FUNC_AT(0x0013d430)
void* dsndCreateBufferAndMixBins(int pool) {
    void *buffer;
    DsMixBins bins;
    DsWaveFormat format;
    DsBufferDesc desc;
    DsMixBinPair pairs[7];
    MemClear(&format, 0x14);
    MemClear(&desc, 0x18);
    uint16_t blockAlign;
    if (pool == 0) {
        format.formatTag = 0x69;
        format.cbSize = 2;
        format.bitsPerSample = 4;
        format.samplesPerBlock = 0x40;
        blockAlign = 0x24;
    } else {
        format.formatTag = 1;
        format.bitsPerSample = 0x10;
        blockAlign = 2;
    }
    format.blockAlign = blockAlign;
    desc.format = &format;
    format.channels = 1;
    format.samplesPerSec = 48000;
    format.avgBytesPerSec = (uint32_t)blockAlign * 48000;
    DirectSoundCreateBuffer(&desc, &buffer);
    bins.pairs = pairs;
    pairs[0].bin = 2;
    pairs[0].volume = 0;
    pairs[1].bin = 1;
    pairs[1].volume = 0;
    pairs[2].bin = 5;
    pairs[2].volume = 0;
    pairs[3].bin = 4;
    pairs[3].volume = 0;
    pairs[4].bin = 0;
    pairs[4].volume = 0;
    pairs[5].bin = 3;
    pairs[5].volume = 0;
    pairs[6].bin = 0xb;
    pairs[6].volume = -10000;
    bins.count = 7;
    SetMixBins(buffer, &bins);
    return buffer;
}

// A node for 'pool' when its free list is empty: one from the other pool's free list, its buffer released and
// made again in this pool's format. The other pool is chosen as the original chooses it: when both are empty it
// pops from the list 0xc below the free lists (the PCM active list) - and faults if that is empty too (sound.md
// 8.5); kept.
// FUNC_AT(0x0013d550)
void FUN_0013d550(int pool) {
    int best = 0;
    int from = -1;
    if (pool != 0) {
        int count = FreeList(0)->count;
        if (count > 0) {
            best = count;
            from = 0;
        }
    }
    if (pool != 1 && FreeList(1)->count > best)
        from = 1;
    BufferNode *node = LinkPop(FreeList(from));
    BufferRelease(node->buffer);
    node->buffer = dsndCreateBufferAndMixBins(pool);
    LinkPush(FreeList(pool), node);
}

// MIX_VOICE_FREE_FUNC: a MIX voice has ended
// FUNC_AT(0x0013d5c0)
void SNDDRV_mixvoicefree(int mixVoice) {
    SNDVOICEI_free(mixVoice + U8(kMixOffset1) + U8(kMixOffset2));
}

// ---------------------------------------------------------------------------------------------------------------
// The software mixer's rings
// ---------------------------------------------------------------------------------------------------------------

// MIX_create, then six rings of 50 ms of 16-bit mono PCM (rounded down to 16 frames), cleared, each routed to one
// speaker, played looping - for good.
// FUNC_AT(0x0013d5e0)
void dsndMixInit(void) {
    MixParams params;
    uint32_t rate = U16(kOutputRate);
    params.voices = U8(kMixVoiceCount);
    params.outputs = 6;
    params.rate = rate;
    params.voiceFree = kMixVoiceFreeEntry;
    params.zero = 0;
    params.unset = 0;
    MixCreate(&params);

    rate = U16(kOutputRate);
    int32_t lead = (int32_t)(rate * 20) / 1000;
    U32(kMixPosition) = 0;
    U32(kMixLead) = (uint32_t)lead;
    DsBufferDesc desc;
    MemClear(&desc, 0x18);
    rate = U16(kOutputRate);
    int32_t frames = ((int32_t)(rate * 50) / 1000) & 0xffff0;
    U32(kRingFrames) = (uint32_t)frames;
    int32_t bytes = frames * 2;
    DsWaveFormat format;
    format.channels = 1;
    format.avgBytesPerSec = rate * 2;
    format.formatTag = 1;
    format.blockAlign = 2;
    desc.bufferBytes = 0;
    desc.format = &format;
    format.samplesPerSec = rate;
    format.bitsPerSample = 0x10;
    format.cbSize = 0;
    DsMixBins bins;
    DsMixBinPair pairs[7];
    bins.count = 7;
    bins.pairs = pairs;
    for (int i = 0; i < 6; i++) {
        void **ring = (void **)(uintptr_t)(kRings + (uint32_t)(i * 4));
        void **memory = (void **)(uintptr_t)(kRingMemory + (uint32_t)(i * 4));
        CreateSoundBuffer(Ptr(kDevice), &desc, ring, NULL);
        EnterCritical();
        *memory = MemAlloc(bytes);
        LeaveCritical();
        SetBufferData(*ring, *memory, (uint32_t)bytes);
        SetCurrentPosition(*ring, 0);
        MemClear(*memory, bytes);
        for (int k = 0; k < 7; k++) {
            pairs[k].bin = *(const uint32_t *)(uintptr_t)(kRingBins + (uint32_t)(k * 4));
            pairs[k].volume = -10000;
        }
        pairs[i].bin = *(const uint32_t *)(uintptr_t)(kRingBins + (uint32_t)(i * 4));
        pairs[i].volume = 0;
        SetMixBins(*ring, &bins);
        SetVolume(*ring, 0);
    }
    for (int i = 0; i < 6; i++)
        Play(Ptr(kRings + (uint32_t)(i * 4)), 0, 0, 1);
}

// FUNC_AT(0x0013d7d0)
void dsndMixStop(void) {
    for (int i = 0; i < 6; i++) {
        void *ring = Ptr(kRings + (uint32_t)(i * 4));
        Stop(ring);
        BufferRelease(Ptr(kRings + (uint32_t)(i * 4)));
        EnterCritical();
        MemFree(Ptr(kRingMemory + (uint32_t)(i * 4)));
        LeaveCritical();
    }
    EnterCritical();
    MixDestroy();
    LeaveCritical();
}

// Mix from the last position to 20 ms past ring 0's write cursor, in two parts across the wrap.
// FUNC_AT(0x0013d820)
void dsndMixProcess(void) {
    uint32_t write;
    GetCurrentPosition(Ptr(kRings), NULL, &write);
    int32_t ringFrames = (int32_t)U32(kRingFrames);
    uint32_t cursor = (write >> 1) & 0xfffff0;
    int32_t target = (int32_t)((U32(kMixLead) + cursor) & 0xffff0);
    if (target >= ringFrames)
        target -= ringFrames;
    int32_t last = (int32_t)U32(kMixPosition);
    int32_t counts[2];
    if (target < last) {
        counts[1] = target;
        counts[0] = ringFrames - last;
    } else {
        counts[1] = 0;
        counts[0] = target - last;
    }
    for (int part = 0; part < 2; part++) {
        if (counts[part] == 0)
            continue;
        int16_t *outputs[6];
        for (int k = 0; k < 6; k++)
            outputs[k] = (int16_t *)Ptr(kRingMemory + (uint32_t)(k * 4)) + last;
        MixAudio(outputs, counts[part]);
        last = 0;
        U32(kMixPosition) = 0;
    }
    U32(kMixPosition) = (uint32_t)target;
}

// ---------------------------------------------------------------------------------------------------------------
// Options, the device, the thread
// ---------------------------------------------------------------------------------------------------------------

// The logical voices a render mode may use: hardware 0..191, 0x10 none (192..192), main CPU 192..223.
// FUNC_AT(0x0013d900)
void SNDPLATFORM_getvoicerange(int mode, int *first, int *end) {
    if (mode & 0x400) {
        *first = 0;
        *end = U8(kMixOffset2);
        return;
    }
    if (mode & 0x10) {
        *first = U8(kMixOffset2);
        *end = U8(kMixOffset1) + U8(kMixOffset2);
        return;
    }
    if (mode & 4) {
        *first = U8(kMixOffset1) + U8(kMixOffset2);
        *end = S16(kNumVoices);
    }
}

// The SND thread: every 10 ms under SoundMutex the 100 Hz server, the mix and the packet callbacks. The deadline
// moves on by 10 ms a tick; a deadline already past sleeps 1 ms, so after a stall the ticks run back to back until
// they have caught up (sound.md 3.1, 8.3).
// FUNC_AT(0x0013d980)
uint32_t __stdcall SNDDRV_thread(void *parameter) {
    (void)parameter;
    volatile uint8_t *keepRunning = (volatile uint8_t *)(uintptr_t)kKeepRunning;
    if (*keepRunning != 0) {
        do {
            MutexLock();
            void (*pre)(void) = *(void (*volatile *)(void))(uintptr_t)kPreFrame;
            if (pre != NULL)
                pre();
            Server100Hz();
            dsndMixProcess();
            FlushPacketCallbacks();
            void (*post)(void) = *(void (*volatile *)(void))(uintptr_t)kPostFrame;
            if (post != NULL)
                post();
            MutexUnlock();
            uint32_t now = GetTickCount32();
            uint32_t deadline = U32(kNextDeadline);
            int32_t wait = (int32_t)(deadline - now);
            U32(kNextDeadline) = deadline + 10;
            if (wait < 0)
                wait = 1;
            SleepMs((uint32_t)wait);
        } while (*keepRunning != 0);
    }
    *(volatile uint8_t *)(uintptr_t)kIsRunning = 0;
    return 0;
}

// The option ranges (MW: SNDSYSCAP)
// FUNC_AT(0x0013da00)
int SNDPLATFORM_outputcaps(void) {
    U8(0x00244cd1) = 0xc0;       // hardware voices
    U8(0x00244cd2) = 0;          // 0x10 voices
    U8(0x00244cce) = 0x40;       // mixer voices
    U16(0x00244cc8) = 8000;      // minSampleRate
    U16(0x00244cca) = 48000;     // maxSampleRate
    U8(0x00244ccc) = 5;          // output modes
    U8(0x00244ccd) = 5;
    U8(0x00244cdb) = 2;          // render modes
    U16(0x00244cdc) = 0x420;
    U16(0x00244cde) = 0x24;
    U8(kModeCount) = 2;
    U16(kModes) = 0x420;
    U16(kModes + 2) = 0x24;
    U8(kMixVoiceCount) = 0x20;
    U8(kMixOffset2) = 0xc0;
    U8(kMixOffset1) = 0;
    U8(0x00244d0f) = 0x10;       // NUM_STREAMS
    U16(kOutputRate) = 48000;
    U8(kOutputMode) = 5;
    U8(0x00244d11) = 1;
    U8(0x00244d09) = 0;
    U8(0x00244cfc) = 0xa;
    U16(0x00244d70) = 0;         // speaker azimuths
    U16(0x00244d72) = 0x2000;
    U16(0x00244d74) = 0x6000;
    U16(0x00244d76) = 0xa000;
    U16(0x00244d78) = 0xe000;
    U8(0x00244ed7) = 2;
    return 0;
}

// The options clamped to the caps (MW: SNDSYSSET); once inited, the saved copy put back instead.
// FUNC_AT(0x0013dae0)
int SNDPLATFORM_outputset(void) {
    if (U8(kInited) != 0) {
        memcpy((void *)(uintptr_t)0x00244ce8u, (const void *)(uintptr_t)0x00244de8u, 0x3a * 4);
        return 0;
    }
    uint8_t mode = U8(kOutputMode);
    if (mode < U8(0x00244ccc)) {
        mode = U8(0x00244ccc);
        U8(kOutputMode) = mode;
    }
    if (mode > U8(0x00244ccd))
        U8(kOutputMode) = U8(0x00244ccd);
    uint16_t rate = U16(kOutputRate);
    if (rate < U16(0x00244cc8)) {
        rate = U16(0x00244cc8);
        U16(kOutputRate) = rate;
    }
    if (rate > U16(0x00244cca))
        U16(kOutputRate) = U16(0x00244cca);
    uint8_t hardware = U8(kMixOffset2);
    if (hardware > U8(0x00244cd1)) {
        hardware = U8(0x00244cd1);
        U8(kMixOffset2) = hardware;
    }
    uint8_t middle = U8(kMixOffset1);
    if (middle > U8(0x00244cd2)) {
        middle = U8(0x00244cd2);
        U8(kMixOffset1) = middle;
    }
    uint8_t mixer = U8(kMixVoiceCount);
    if (mixer > U8(0x00244cce)) {
        mixer = U8(0x00244cce);
        U8(kMixVoiceCount) = mixer;
    }
    if (U8(0x00244d0f) > 0x20)
        U8(0x00244d0f) = 0x20;
    S16(kNumVoices) = (int16_t)(uint16_t)(mixer + middle + hardware);
    // drop the zero render modes, closing the gaps
    uint8_t count = U8(kModeCount);
    for (;;) {
        int i = 0;
        while (i < (int)count && U16(kModes + (uint32_t)(i * 2)) != 0)
            i++;
        if (i >= (int)count)
            break;
        count--;
        U8(kModeCount) = count;
        for (; i < (int)count; i++) {
            U16(kModes + (uint32_t)(i * 2)) = U16(kModes + (uint32_t)(i * 2 + 2));
            count = U8(kModeCount);
        }
    }
    int k = U8(0x00244cfc);
    if (k < 1) {
        U8(0x00244cfc) = 1;
        return 0;
    }
    if (k > 200)
        k = 200;
    U8(0x00244cfc) = (uint8_t)k;
    return 0;
}

// The device, the effects image, the platform voices, the 180 pooled buffers, the mixer's rings, the thread.
// FUNC_AT(0x0013dc50)
int SNDPLATFORM_init(void) {
    void *imageDesc;
    uint32_t location[2];
    DirectSoundCreate(NULL, (void **)(uintptr_t)kDevice, NULL);
    location[0] = 0;
    location[1] = 0xffffffffu;
    DownloadEffectsImage(Ptr(kDevice), (const void *)(uintptr_t)kEffectsImage, 0x3360, location, &imageDesc);
    EnterCritical();
    void *voices = MemAlloc((int32_t)S16(kNumVoices) * 0x18);
    Ptr(kPlatformVoices) = voices;
    MemClear(voices, (int32_t)S16(kNumVoices) * 0x18);
    U32(kPoolSizes) = 0x98;
    U32(kPoolSizes + 4) = 0x1c;
    Ptr(kNodes) = MemAlloc(0x21c0);
    LeaveCritical();
    int node = 0;
    uint32_t pool = 0;
    for (; pool < 2; pool++) {
        LinkInit(ActiveList((int)pool));
        LinkInit(FreeList((int)pool));
        int32_t size = (int32_t)U32(kPoolSizes + pool * 4);
        for (int i = 0; i < size; i++) {
            void *buffer = dsndCreateBufferAndMixBins((int)pool);
            ((BufferNode *)Ptr(kNodes))[node].buffer = buffer;
            LinkPush(FreeList((int)pool), &((BufferNode *)Ptr(kNodes))[node]);
            node++;
            size = (int32_t)U32(kPoolSizes + pool * 4);
        }
    }
    dsndMixInit();
    U8(kIsRunning) = 1;
    U8(kKeepRunning) = 1;
    uint32_t now = GetTickCount32();
    U32(kNextDeadline) = now;
    void *thread = CreateThreadX(NULL, 0x7d000, kThreadEntry, NULL, 0, &pool);
    SetThreadPriorityX(thread, 15);
    return 0;
}

// Stop the thread (and wait for it), release every free buffer, the rings and the device.
// FUNC_AT(0x0013ddc0)
int SNDPLATFORM_restore(void) {
    U8(kKeepRunning) = 0;
    while (*(volatile uint8_t *)(uintptr_t)kIsRunning != 0)
        SleepMs(0);
    for (int pool = 0; pool < 2; pool++) {
        BufferNode *node;
        while ((node = LinkPop(FreeList(pool))) != NULL)
            BufferRelease(node->buffer);
    }
    dsndMixStop();
    EnterCritical();
    MemFree(Ptr(kNodes));
    MemFree(Ptr(kPlatformVoices));
    LeaveCritical();
    DeviceRelease(Ptr(kDevice));
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The per-voice operations
// ---------------------------------------------------------------------------------------------------------------

// Stop a sound: hardware buffers back to the free lists' tails (a packet voice's ring freed with its first
// channel), or the MIX voices stopped; then every channel's voice freed.
// FUNC_AT(0x0013de50)
int SNDPLATFORM_stop(int voice) {
    Voice *v = VoiceAt(voice);
    if (v->renderMode & 0x410) {
        for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
            BufferNode *node = PlatformAt(v->platformVoices[i])->node;
            Stop(node->buffer);
            if (node->player >= 0 && i == 0)
                MemFree(node->memory);
            LinkRemove(ActiveList(node->pool), node);
            LinkPushTail(FreeList(node->pool), node);
        }
    } else {
        for (int i = 0; i < (int)(uint8_t)v->channels; i++)
            MixStop(MixIndex(v->platformVoices[i]));
    }
    for (int i = 0; i < (int)(uint8_t)v->channels; i++)
        SNDVOICEI_free(v->platformVoices[i]);
    return 0;
}

// Volume: a hardware buffer's from the volume table at vol x programmed / 127, rounded; a MIX voice's per-speaker
// dry gains, speaker gain x vol x programmed x 1.89e-9. Then the fx send again if there is one.
// FUNC_AT(0x0013df50)
void SNDPLATFORM_setvol(int voice) {
    Voice *v = VoiceAt(voice);
    if (v->renderMode & 0x410) {
        for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
            double product = (double)v->vol * (double)(int8_t)v->progVol;   // FILD, FIMUL: a zero keeps its sign
            int index = Ftol(product * K(kOver127) + *(const double *)(uintptr_t)kHalf);
            int16_t db = VolumeDb(index);
            SetVolume(PlatformAt(v->platformVoices[i])->node->buffer, db);
        }
    } else {
        double product = (double)(int8_t)v->progVol * (double)v->vol;   // FILD, FIMUL: -0 when one is 0, one < 0
        float scale = (float)(product * K(kDryScale));
        for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
            int pv = v->platformVoices[i];
            PlatformVoice *p = PlatformAt(pv);
            for (int s = 0; s < (int)U8(kOutputMode); s++) {
                float gain = (float)((double)p->gains[s] * (double)scale);
                MixSetDryGain(MixIndex(v->platformVoices[i]), s, gain);
            }
        }
    }
    if (v->fxSend > 0)
        SNDPLATFORM_setfxlevel(v->platformVoices[0], 0);
}

// Pan: the speaker gains from the azimuth (one channel) or per channel from its azimuth offset, the sixth channel
// straight to the LFE; a hardware voice's mix bin volumes from them, a MIX voice's dry gains through setvol.
// FUNC_AT(0x0013e0c0)
void SNDPLATFORM_set3dpos(int voice) {
    Voice *v = VoiceAt(voice);
    PlatformVoice *self = PlatformAt(voice);
    if ((uint8_t)v->channels == 1) {
        SNDI_aztospkrvol(v->azimuth, self->gains);
    } else {
        for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
            PlatformVoice *p = PlatformAt(v->platformVoices[i]);
            if (i == 5) {
                memset(p->gains, 0, 10);
                p->gains[5] = 0x7fff;
            } else {
                SNDI_aztospkrvol(v->channelAzimuth[i], p->gains);
                p->gains[5] = 0;
            }
        }
    }
    if ((v->renderMode & 0x400) == 0) {
        SNDPLATFORM_setvol(voice);
        return;
    }
    DsMixBinPair pairs[6];
    DsMixBins bins;
    bins.count = 6;
    bins.pairs = pairs;
    if ((uint8_t)v->channels == 1) {
        pairs[0].bin = 2;
        pairs[0].volume = GainDb(self, 0);
        pairs[1].bin = 1;
        pairs[1].volume = GainDb(self, 1);
        pairs[2].bin = 5;
        pairs[2].volume = GainDb(self, 2);
        pairs[3].bin = 4;
        pairs[3].volume = GainDb(self, 3);
        pairs[4].bin = 0;
        pairs[4].volume = GainDb(self, 4);
        pairs[5].bin = 3;
        pairs[5].volume = -10000;
        SetMixBinVolumes(self->node->buffer, &bins);
        return;
    }
    for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
        PlatformVoice *p = PlatformAt(v->platformVoices[i]);
        pairs[0].bin = 2;
        pairs[0].volume = GainDb(p, 0);
        pairs[1].bin = 1;
        pairs[1].volume = GainDb(p, 1);
        pairs[2].bin = 5;
        pairs[2].volume = GainDb(p, 2);
        pairs[3].bin = 4;
        pairs[3].volume = GainDb(p, 3);
        pairs[4].bin = 0;
        pairs[4].volume = GainDb(p, 4);
        pairs[5].bin = 3;
        pairs[5].volume = GainDb(p, 5);
        SetMixBinVolumes(PlatformAt(v->platformVoices[i])->node->buffer, &bins);
    }
}

// Pitch: a hardware buffer's frequency, rate x pitch / 4096 clamped to 188..191983 Hz (a packet voice's multiplier
// capped at 2.0); a multiplier of 0 pauses (Stop), and the first non-zero one after resumes (Play with the saved
// loop flag). A MIX voice's 16.16 step, (rate << 16) / output rate x pitch >> 12.
// FUNC_AT(0x0013e320)
int SNDPLATFORM_setpitch(int voice) {
    Voice *v = VoiceAt(voice);
    if ((v->renderMode & 0x410) == 0) {
        uint32_t step = ((uint32_t)v->sampleRate << 16) / (uint32_t)U16(kOutputRate);
        step = (step * (uint32_t)v->pitch) >> 12;
        for (int i = 0; i < (int)(uint8_t)v->channels; i++)
            MixSetPitch(MixIndex(v->platformVoices[i]), step);
        return 0;
    }
    if (v->pitch == 0) {
        bool stopped = true;
        if ((uint8_t)v->channels != 0) {
            for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
                PlatformVoice *p = PlatformAt(v->platformVoices[i]);
                uint8_t playing = p->playing;
                if (playing == 1) {
                    Stop(p->node->buffer);
                    PlatformAt(v->platformVoices[i])->playing = 0;
                    stopped = true;
                } else if (playing == 0) {
                    stopped = false;
                }
            }
            if (!stopped)
                return 0;
        }
    } else {
        for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
            PlatformVoice *p = PlatformAt(v->platformVoices[i]);
            if (p->playing == 0) {
                Play(p->node->buffer, 0, 0, (uint32_t)p->looping);
                PlatformAt(v->platformVoices[i])->playing = 1;
            }
        }
    }
    if (PlatformAt(voice)->node->player >= 0 && v->pitch > 0x2000)
        v->pitch = 0x2000;
    uint32_t frequency = (uint32_t)Ftol((double)v->sampleRate * (double)v->pitch * K(kOver4096));
    if (frequency < 0xbc)
        frequency = 0xbc;
    else if (frequency > 0x2edef)
        frequency = 0x2edef;
    for (int i = 0; i < (int)(uint8_t)v->channels; i++)
        SetFrequency(PlatformAt(v->platformVoices[i])->node->buffer, frequency);
    return 0;
}

// The fx send: a MIX voice's per bus, send x vol x 2.4e-7; a hardware voice's (bus 0's send whatever the bus)
// through mix bin 11 at the volume table's entry for send x vol / 127 >> 8.
// FUNC_AT(0x00140070)
int SNDPLATFORM_setfxlevel(int voice, int bus) {
    Voice *v = VoiceAt(voice);
    if (v->renderMode & 4) {
        int32_t send = *(const int16_t *)((const uint8_t *)v + 0x5e + bus * 2);
        float level = (float)((double)send * (double)v->vol * K(kFxScale));
        for (int i = 0; i < (int)(uint8_t)v->channels; i++)
            MixSetFxLevel(MixIndex(v->platformVoices[i]), bus, level);
        return 0;
    }
    DsMixBinPair pair;
    DsMixBins bins;
    bins.pairs = &pair;
    bins.count = 1;
    int32_t index = Ftol((double)v->fxSend * (double)v->vol * K(kOver127));
    if ((uint8_t)v->channels == 0)
        return 0;
    int16_t db = VolumeDb(index >> 8);
    for (int i = 0; i < (int)(uint8_t)v->channels; i++) {
        pair.volume = db;
        pair.bin = 0xb;
        SetMixBinVolumes(PlatformAt(v->platformVoices[i])->node->buffer, &bins);
    }
    return 0;
}

// The hardware bus's reverb: its mode picks one of 23 I3DL2 listener presets (anything else the first).
// FUNC_AT(0x00140880)
int DirectSound_SetListenerRelated(void) {
    static const uint8_t kPreset[61] = {
        1, 1, 1, 11, 0, 4, 0, 0, 0, 0, 2, 0, 0, 19, 0, 15, 0, 17, 0, 0, 16, 0, 0, 20, 0, 12, 0, 14, 0, 0, 0,
        0, 5, 3, 18, 21, 8, 13, 6, 0, 7, 0, 0, 0, 0, 0, 0, 0, 0, 0, 9, 0, 0, 0, 0, 10, 0, 0, 0, 0, 22,
    };
    uint32_t mode = (uint32_t)(int32_t)S16(kFxBus1Mode);
    uint32_t preset = mode <= 0x3c ? kPreset[mode] : 0;
    return (int)SetI3DL2Listener(Ptr(kDevice), (const void *)(uintptr_t)(kListenerPresets + preset * 0x30), 0);
}

// SNDPLATFORM_fxinit's worker: the hardware path's reverb preset, or the mixer's software reverb (off, the bus's
// own description in mode 1, the default otherwise).
// FUNC_AT(0x00140a10)
void SNDDRV_setfx(int path) {
    if (path & 0x410) {
        DirectSound_SetListenerRelated();
        return;
    }
    if ((path & 4) == 0)
        return;
    EnterCritical();
    uint16_t mode = U16(kFxBus0);
    if (mode == 0) {
        U32(kFxInitFunc) = 0;
        U32(kFxHook) = 0;
        MixRestoreReverb();
        LeaveCritical();
        return;
    }
    uint32_t description = mode == 1 ? U32(kFxBus0 + 4) : kFxDefault;
    MixInitReverb(U16(kOutputRate), description);
    LeaveCritical();
}

// FUNC_AT(0x00140a80)
int SNDPLATFORM_fxinit(int bus, int path) {
    (void)bus;
    U32(kSetFxFunc) = kSetFxEntry;
    SNDDRV_setfx(path);
    return 0;
}

// A stream on a hardware voice (data-dead, sound.md 3.5): refill the looping ring up to the play cursor (rounded
// down to 36 bytes) from the packet player, a channel at a time, or with silence once it runs dry (only into the
// master's ring, as the original does), reporting consumed frames back. The write position ends at the play
// cursor whatever was written.
// FUNC_AT(0x00142150)
void FUN_00142150(SND::BufferNode *node) {
    SND_UNTESTED("FUN_00142150 (the hardware packet voice's refill)");
    Voice *v = VoiceAt(*(const uint16_t *)&node->platformVoice);
    int frames = 0;
    int other;
    uint32_t play;
    GetCurrentPosition(node->buffer, &play, NULL);
    uint32_t aligned = play / 36 * 36;
    uint32_t write = node->writePos;
    int32_t space = aligned >= write ? (int32_t)(aligned - write) : (int32_t)(node->size - write + aligned);
    if (space <= 0) {
        node->writePos = aligned;
        return;
    }
    uint8_t *source[6];
    do {
        if (node->packet == NULL) {
            for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++) {
                uint8_t *data = PacketGet(node->player, ch, &frames, &other);
                PlatformAt(v->platformVoices[ch])->node->packet = data;
            }
            node->packetUsed = 0;
            if (node->pool == 0)
                node->packetBytes = (int32_t)((uint32_t)frames * 36) >> 6;
            else
                node->packetBytes = frames * 2;
        }
        int32_t chunk = space;
        if (node->packet != NULL) {
            int32_t used = node->packetUsed;
            int32_t remaining = node->packetBytes - used;
            if (remaining <= 0) {
                node->packet = NULL;
                continue;   // to the loop's test: space is unchanged
            }
            if (chunk > remaining)
                chunk = remaining;
            for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
                source[ch] = PlatformAt(v->platformVoices[ch])->node->packet + used;
            node->packetUsed = used + chunk;
        }
        space -= chunk;
        while (chunk > 0) {
            int32_t part = (int32_t)(node->size - node->writePos);
            if (part > chunk)
                part = chunk;
            for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++) {
                BufferNode *n = PlatformAt(v->platformVoices[ch])->node;
                if (node->packet != NULL) {
                    memcpy(n->memory + node->writePos, source[ch], (size_t)(uint32_t)part);
                    source[ch] += part;
                    n->silence = 0;
                    int32_t consumed = node->pool == 0 ? (int32_t)((uint32_t)part << 6) / 36 : part >> 1;
                    PacketFreeFrames(node->player, ch, consumed);
                } else if (node->silence < node->size) {
                    MemClear(PlatformAt(*(const uint16_t *)&node->platformVoice)->node->memory + node->writePos,
                             part);
                    node->silence += (uint32_t)part;
                }
            }
            uint32_t position = node->writePos + (uint32_t)chunk;
            chunk -= part;
            node->writePos = position;
            if (position >= node->size)
                node->writePos = 0;
        }
    } while (space > 0);
    node->writePos = aligned;
}

// A MIX voice's master (the voice itself when it is the master)
// FUNC_AT(0x00142420)
int SNDDRV_getmastervoice(int mixVoice) {
    int index = mixVoice + U8(kMixOffset2) + U8(kMixOffset1);
    int16_t master = VoiceAt(index)->master;
    if (master != -1)
        return master;
    return index;
}

// Which channel of its sound a MIX voice is (0 for the master or when not found)
// FUNC_AT(0x00142460)
int SNDDRV_getsamplechan(int mixVoice) {
    int index = mixVoice + U8(kMixOffset2) + U8(kMixOffset1);
    int16_t master = VoiceAt(index)->master;
    if (master == -1)
        return 0;
    Voice *m = VoiceAt(master);
    for (int ch = 1; ch < 6; ch++)
        if (m->platformVoices[ch] == index)
            return ch;
    return 0;
}

// Start a stream's voices: MIX voices on the packets (the live path), or (data-dead) hardware buffers each with a
// looping ring of 30 ms of samples in one allocation, refilled by FUN_00142150.
// FUNC_AT(0x001424c0)
int SNDPLATFORM_packetplay(int player, int voice, int timeMult, int distort, int lowpass, int highpass,
                           const SND::PacketFormat *format, uint8_t *const *data) {
    (void)distort;
    Voice *v = VoiceAt(voice);
    if (v->renderMode & 4) {
        for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
            MixPlayInit(MixIndex(v->platformVoices[ch]), format->sampleRep, 0, 0, -1, (int)P(data[ch]),
                        (int)(uint8_t)v->channels, v->frames, -1, -1, 0, 0, ch);
        int16_t *channels = v->platformVoices;
        SNDPLATFORM_setpitch(channels[0]);
        SNDPLATFORM_set3dpos(channels[0]);
        SNDPLATFORM_setfxlevel(channels[0], 0);
        SNDPLATFORM_timemult(channels[0], timeMult);
        SNDPLATFORM_lowpass(channels[0], lowpass);
        SNDPLATFORM_highpass(channels[0], highpass);
        for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
            MixPlay(MixIndex(channels[ch]));
        return 0;
    }
    SND_UNTESTED("SNDPLATFORM_packetplay on a hardware voice");
    PlatformVoice *self = PlatformAt(voice);
    int32_t frames = Ftol((double)format->sampleRate * K(kPacketSeconds));
    int pool;
    int32_t bytes;
    if (format->sampleRep == 0x14) {
        bytes = ((int32_t)((uint32_t)frames * 36) >> 6) + 0x23;
        bytes = bytes / 36 * 36;
        pool = 0;
    } else {
        pool = 1;
        bytes = (int32_t)((uint32_t)frames << 1);
    }
    for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++) {
        if (FreeList(pool)->count <= 0)
            FUN_0013d550(pool);
        BufferNode *node = LinkPop(FreeList(pool));
        LinkPush(ActiveList(pool), node);
        PlatformAt(v->platformVoices[ch])->node = node;
        node->platformVoice = v->platformVoices[ch];
        node->pool = (uint8_t)pool;
        node->size = (uint32_t)bytes;
        node->writePos = 0;
        node->packet = NULL;
        node->player = player;
        node->packetBytes = 0;
        node->packetUsed = 0;
        node->silence = 0;
        if (ch == 0) {
            node->memory = (uint8_t *)MemAlloc((int32_t)(uint8_t)v->channels * bytes);
            MemClear(node->memory, (int32_t)(uint8_t)v->channels * bytes);
        } else {
            node->memory = self->node->memory + bytes * ch;
        }
        SetBufferData(node->buffer, node->memory, (uint32_t)bytes);
        SetLoopRegion(node->buffer, 0, (uint32_t)bytes);
        SetCurrentPosition(node->buffer, 0);
    }
    SNDPLATFORM_setpitch(voice);
    SNDPLATFORM_set3dpos(voice);
    SNDPLATFORM_setfxlevel(voice, 0);
    SNDPLATFORM_setvol(voice);
    SNDPLATFORM_lowpass(voice, lowpass);
    for (int ch = (int)(uint8_t)v->channels - 1; ch >= 0; ch--) {
        int pv = v->platformVoices[ch];
        Play(PlatformAt(pv)->node->buffer, 0, 0, 1);
        PlatformAt(v->platformVoices[ch])->playing = 1;
        PlatformAt(v->platformVoices[ch])->looping = 1;
    }
    return 0;
}

// A user filter on a MIX voice's chains (-7 on a hardware voice)
// FUNC_AT(0x001427d0)
int SNDPLATFORM_filteradd(int voice, int filter) {
    Voice *v = VoiceAt(voice);
    if ((v->renderMode & 4) == 0)
        return -7;
    for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
        FilterAdd(v->platformVoices[ch], ch, filter);
    return 0;
}

// Low pass: a hardware buffer's DirectSound filter from the coefficient table (cutoff >> 5, capped at 0x1fe0), a
// MIX voice's as a fraction of the Nyquist rate.
// FUNC_AT(0x001429f0)
void SNDPLATFORM_lowpass(int voice, int cutoff) {
    Voice *v = VoiceAt(voice);
    uint32_t mode = v->renderMode;
    if (mode & 0x410) {
        if (cutoff > 0x1fe0)
            cutoff = 0x1fe0;
        DsFilterDesc desc;
        desc.mode = 1;
        desc.coefficients[0] = *(const uint16_t *)(uintptr_t)(kLowpassTable + (uint32_t)((cutoff >> 5) * 2));
        desc.coefficients[1] = 0xffff;
        desc.coefficients[2] = 0;
        desc.coefficients[3] = 0;
        desc.q = 0;
        for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
            SetFilter(PlatformAt(v->platformVoices[ch])->node->buffer, &desc);
        return;
    }
    if ((mode & 4) == 0)
        return;
    double nyquist = (double)(int32_t)U16(kOutputRate) * K(kHalfF);
    float fraction = (float)(K(kOne) / nyquist * (double)cutoff);
    for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
        MixSetLowpass(MixIndex(v->platformVoices[ch]), fraction);
}

// Start a bank sample: hardware buffers from the pools (ADPCM for sample representation 20, else PCM16, borrowing
// when a pool is empty) given the sample in place in the bank, the loop rounded to whole 64-sample ADPCM blocks;
// or MIX voices on the bank data.
// FUNC_AT(0x00142b10)
int SNDPLATFORM_playtimbre(SND::PatchHeader *header, uint8_t *base, int voice, int timeMult, int distort,
                           int lowpass, int highpass) {
    (void)distort;
    Voice *v = VoiceAt(voice);
    int32_t loopStart = 0;
    int32_t loopLength = 0;
    if (v->renderMode & 0x400) {
        uint8_t rep = header->sampleRep;
        int32_t frames = v->frames;
        int pool;
        uint32_t bytes;
        if (rep == 0x14) {
            bytes = ((((uint32_t)(frames + 0x3f) >> 6) * 9) << 8) >> 6;
            pool = 0;
        } else {
            pool = 1;
            bytes = (uint32_t)(frames * 2);
        }
        int32_t end = header->loopEnd;
        if (end > 0) {
            int32_t start = header->loopStart;
            if (rep == 0x14) {
                if ((start & 0x3f) >= 0x20)
                    start = (int32_t)(((uint32_t)(start + 0x3f) >> 6) << 6);
                else
                    start &= 0x7fffffc0;
                if ((end & 0x3f) >= 0x20 && end + 0x20 < frames)
                    end = (int32_t)(((uint32_t)(end + 0x3f) >> 6) << 6);
                else
                    end &= 0x7fffffc0;
                int32_t length = end - start;
                loopStart = (int32_t)((uint32_t)start * 36) >> 6;
                loopLength = (int32_t)((uint32_t)length * 36) >> 6;
            } else {
                loopStart = start * 2;
                loopLength = (end - start) * 2 + 2;
            }
        }
        for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++) {
            if (FreeList(pool)->count <= 0)
                FUN_0013d550(pool);
            BufferNode *node = LinkPop(FreeList(pool));
            LinkPush(ActiveList(pool), node);
            PlatformAt(v->platformVoices[ch])->node = node;
            node->platformVoice = v->platformVoices[ch];
            node->pool = (uint8_t)pool;
            node->player = -1;
            SetBufferData(node->buffer, base + header->sampleOffsets[ch], bytes);
            if (header->loopEnd > 0)
                SetLoopRegion(node->buffer, (uint32_t)loopStart, (uint32_t)loopLength);
            SetCurrentPosition(node->buffer, 0);
        }
        SNDPLATFORM_setpitch(voice);
        SNDPLATFORM_set3dpos(voice);
        SNDPLATFORM_setfxlevel(voice, 0);
        SNDPLATFORM_setvol(voice);
        SNDPLATFORM_lowpass(voice, lowpass);
        int32_t looping = header->loopEnd > 0 ? 1 : 0;
        for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++) {
            Play(PlatformAt(v->platformVoices[ch])->node->buffer, 0, 0, (uint32_t)looping);
            PlatformAt(v->platformVoices[ch])->playing = 1;
            PlatformAt(v->platformVoices[ch])->looping = looping;
        }
        return 0;
    }
    for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
        MixPlayInit(MixIndex(v->platformVoices[ch]), header->sampleRep, 1, (int)P(base + header->sampleOffsets[ch]),
                    header->tag1a, (int)P(header->blobs[ch]), (int)(uint8_t)v->channels, v->frames,
                    header->loopStart, header->loopEnd, 0, 0, ch);
    SNDPLATFORM_setpitch(voice);
    SNDPLATFORM_set3dpos(voice);
    SNDPLATFORM_setfxlevel(voice, 0);
    int16_t *channels = v->platformVoices;
    SNDPLATFORM_timemult(channels[0], timeMult);
    SNDPLATFORM_lowpass(channels[0], lowpass);
    SNDPLATFORM_highpass(channels[0], highpass);
    for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
        MixPlay(MixIndex(channels[ch]));
    return 0;
}

// FUNC_AT(0x00144960)
void SNDPLATFORM_highpass(int voice, int cutoff) {
    Voice *v = VoiceAt(voice);
    if ((v->renderMode & 4) == 0)
        return;
    for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
        MixSetHighpass(MixIndex(v->platformVoices[ch]), cutoff);
}

// FUNC_AT(0x001449c0)
int SNDPLATFORM_timemult(int voice, int mult) {
    Voice *v = VoiceAt(voice);
    if (v->renderMode & 4) {
        for (int ch = 0; ch < (int)(uint8_t)v->channels; ch++)
            MixSetTimeMult(MixIndex(v->platformVoices[ch]), mult);
    }
    return 0;
}
