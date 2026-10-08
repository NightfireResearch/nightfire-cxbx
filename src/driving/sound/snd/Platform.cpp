#include "Platform.h"
#include "Filters.h"
#include "Mixer.h"
#include "Reverb.h"
#include "Streams.h"
#include "System.h"
#include "Voices.h"
#include "../DirectSound.h"
#include "../../platform/RealPrint.h"
#include "../../platform/RealSystem.h"
#include "../../platform/XboxStartup.h"
#include "../../platform/XboxXapi.h"
#include "SndUntested.h"
#include "SndGlobals.h"
#include "../../devtools/SndLockstep.h"

#include <bit>
#include <stddef.h>
#include <stdint.h>
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
// Each function is the original at its address, ported from the listing. DirectSound is our seam's API
// (DirectSound.h, src/driving/sound/dsndSeam.cpp), the mixer (module G), the system module (SNDLINKI, SNDMEMI, the
// critical section), SFILTER_add and platform.system's getTickCount/memclr are called directly; only XAPI's thread
// primitives, whose replacements are private to platform/, still go through the originals' addresses. The
// library's globals stay where they are. x87 only in the gain and frequency products, computed in double
// in the original's order (exact here: every product is of integers and a float, and fits 53 bits) and truncated
// as __ftol2 truncates; the low-pass ratio is a division, rounded to double as a 53-bit x87 would. The integer
// operands are multiplied as doubles, as FILD/FIMUL do, never as ints: a zero product then keeps the sign the x87
// gives it (0 x -5 = -0), which reaches the dry gains MIX is handed.
//
// FUN_00142150, the hardware packet voice's ring refill, is data-dead (3.8: no stream plays on a hardware voice):
// provisional, it warns once when it first runs. devtools/SndPlatformShadow.cpp compares the rest with the
// originals on snapshots of the driver's state, DirectSound and the mixer replaced by recorders.
// ---------------------------------------------------------------------------------------------------------------

namespace {

using SND::BufferList;
using SND::BufferNode;
using SND::DsBufferDesc;
using SND::DsFilterDesc;
using SND::DsI3dl2Listener;
using SND::DsMixBinPair;
using SND::DsMixBins;
using SND::DsWaveFormat;
using SND::PlatformVoice;
using SND::Voice;

// ---- the driver's globals (sound.md 2.2-2.6)
typedef void (*FrameCallback)(void);

#define NextDeadline U32_AT(0x00244c38)                // the SND thread's next deadline (ms)
#define IsRunning U8_AT(0x00244c3c)                    // SNDDRV_isRunning
#define KeepRunning U8_AT(0x00244c3d)                  // SNDDRV_shouldContinueRunning
#define SetFxFunc U32_AT(0x00244c40)                   // p_setfx_func, written, never read
#define BufferNodes (*(BufferNode **)0x00244c44)      // SND_BUFFER_LIST: [180]
#define FreeBufferLists ((BufferList *)0x00244c60)     // LList_maybeFreeDsndBuffers[pool]
#define PoolSizes ((int32_t *)0x00244c78)              // [pool]: 152, 28
#define PlatformVoices (*(PlatformVoice **)0x00244c80)   // [NumVoices]
#define Device (*(IDirectSound **)0x00244c84)        // the IDirectSound
#define Rings ((IDirectSoundBuffer **)0x00244c88)      // the mixer's six DirectSound buffers, one per speaker
#define RingMemory ((int16_t **)0x00244ca0)            // their memory blocks
#define RingFrames I32_AT(0x00244cb8)                  // 2400
#define MixLead I32_AT(0x00244cbc)                     // 960
#define MixPosition I32_AT(0x00244cc0)                 // the next frame to mix
#define PreFrameCallback (*(FrameCallback volatile *)0x00244fb8)    // SNDDRVPreFrameCb
#define PostFrameCallback (*(FrameCallback volatile *)0x00244fbc)   // SNDDRVPostFrameCb
#define MixFxInitFunc U32_AT(0x002459a0)               // the mixer's FX init (SNDMIXI_fxinit)
#define MixFxHook U32_AT(0x002459a4)                   // unpackerInitFuncs[0]: the per-slice FX hook

// The voice counts the options configure (SNDPLATFORM_outputcaps): the hardware voices first, the 0x10 mode's
// next (none), the mixer's last
#define HardwareVoices (SndOptions.set.hardwareVoices)   // 0x00244cfa mixVoiceOffset2: 192
#define MiddleVoices (SndOptions.set.middleVoices)       // 0x00244cf9 mixVoiceOffset1: 0
#define MixerVoices (SndOptions.set.mixerVoices)         // 0x00244cf6: 32

// ---- tables in the original's .rdata, read in place
// The volume table: int16 [128], hundredths of a dB (-10000 at 0 up to 0 at 127). Read where it is: the callers'
// indices are not clamped (a negative programmed volume makes a negative one), as in the original.
#define VolumeTable ((const int16_t *)0x001a71e0)
// The low-pass coefficients: uint16 [256], indexed by cutoff >> 5 (a negative cutoff reads before it, as the
// original's does)
#define LowpassTable ((const uint16_t *)0x001a7350)
// The DSP effects image (0x3360 bytes), the I3DL2 listener presets (23, in DirectSound's order) and the mixer's
// default reverb description - handed over by address
#define EffectsImage ((const void *)0x001a3e80)
#define ListenerPresets ((const DsI3dl2Listener *)0x001d98d8)
#define DefaultReverb ((const uint8_t *)0x001d98c0)

// ---- constants (the original reads the floats from .rdata: the same bits)
constexpr float kOver127 = 0x1.020408p-7f;          // 1/127 as a float
constexpr float kDryScale = 0x1.040e28p-29f;        // 1.892e-9: 0x7fff x 127 x 127 x this ~ 1
constexpr float kOver4096 = 0x1p-12f;
constexpr float kFxScale = 0x1.02060cp-22f;         // 2.403e-7
constexpr float kPacketSeconds = 0.03f;             // a packet voice's ring
static_assert(std::bit_cast<uint32_t>(kOver127) == 0x3c010204);
static_assert(std::bit_cast<uint32_t>(kDryScale) == 0x31020714);
static_assert(std::bit_cast<uint32_t>(kOver4096) == 0x39800000);
static_assert(std::bit_cast<uint32_t>(kFxScale) == 0x34810306);
static_assert(std::bit_cast<uint32_t>(kPacketSeconds) == 0x3cf5c28f);

constexpr int kBufferNodeCount = 180;
constexpr uint8_t kSampleRepXboxAdpcm = 20;

// Xbox DirectSound's values
enum MixBin : uint32_t {         // DSMIXBIN_*
    kBinFrontLeft = 0,
    kBinFrontRight = 1,
    kBinFrontCenter = 2,
    kBinLowFrequency = 3,
    kBinBackLeft = 4,
    kBinBackRight = 5,
    kBinFxSend0 = 11,
};
constexpr int32_t kVolumeMin = -10000;              // DSBVOLUME_MIN, hundredths of a dB
constexpr uint32_t kPlayLooping = 1;                // DSBPLAY_LOOPING
constexpr uint32_t kFrequencyMin = 188;             // DSBFREQUENCY_MIN
constexpr uint32_t kFrequencyMax = 191983;          // DSBFREQUENCY_MAX
constexpr uint16_t kFormatPcm = 1;                  // WAVE_FORMAT_PCM
constexpr uint16_t kFormatXboxAdpcm = 0x69;         // WAVE_FORMAT_XBOX_ADPCM
constexpr int kThreadPriorityTimeCritical = 15;     // THREAD_PRIORITY_TIME_CRITICAL

// The speaker each mixer ring plays to: FC FR BR BL FL LFE, and the FX send the rings mute
const uint32_t kRingBins[7] = {
    kBinFrontCenter, kBinFrontRight, kBinBackRight, kBinBackLeft, kBinFrontLeft, kBinLowFrequency, kBinFxSend0,
};

// The I3DL2 listener presets (ListenerPresets' order)
enum I3dl2Preset : uint8_t {
    kPresetGeneric, kPresetPaddedCell, kPresetRoom, kPresetBathroom, kPresetLivingRoom, kPresetStoneRoom,
    kPresetAuditorium, kPresetConcertHall, kPresetCave, kPresetArena, kPresetHangar, kPresetCarpetedHallway,
    kPresetHallway, kPresetStoneCorridor, kPresetAlley, kPresetForest, kPresetCity, kPresetMountains, kPresetQuarry,
    kPresetPlain, kPresetParkingLot, kPresetSewerPipe, kPresetUnderwater,
};

// The hardware bus's mode -> its listener preset (modes 0..60; the original's switch, as a table)
const I3dl2Preset kBusModePreset[61] = {
    kPresetPaddedCell,       // 0
    kPresetPaddedCell,       // 1
    kPresetPaddedCell,       // 2
    kPresetCarpetedHallway,  // 3
    kPresetGeneric,
    kPresetLivingRoom,       // 5
    kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric,
    kPresetRoom,             // 10
    kPresetGeneric, kPresetGeneric,
    kPresetPlain,            // 13
    kPresetGeneric,
    kPresetForest,           // 15
    kPresetGeneric,
    kPresetMountains,        // 17
    kPresetGeneric, kPresetGeneric,
    kPresetCity,             // 20
    kPresetGeneric, kPresetGeneric,
    kPresetParkingLot,       // 23
    kPresetGeneric,
    kPresetHallway,          // 25
    kPresetGeneric,
    kPresetAlley,            // 27
    kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric,
    kPresetStoneRoom,        // 32
    kPresetBathroom,         // 33
    kPresetQuarry,           // 34
    kPresetSewerPipe,        // 35
    kPresetCave,             // 36
    kPresetStoneCorridor,    // 37
    kPresetAuditorium,       // 38
    kPresetGeneric,
    kPresetConcertHall,      // 40
    kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric,
    kPresetGeneric, kPresetGeneric,
    kPresetArena,           // 50
    kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric,
    kPresetHangar,           // 55
    kPresetGeneric, kPresetGeneric, kPresetGeneric, kPresetGeneric,
    kPresetUnderwater,       // 60
};

// The original addresses of this module's functions (the thread, the voice-free callback, the fx setter), stored
// where the original stores them
const uint32_t kThreadEntry = 0x0013d980;
const uint32_t kMixVoiceFreeEntry = 0x0013d5c0;
const uint32_t kSetFxEntry = 0x00140a10;

// A voice's channel count (Voice::channels), read unsigned as the original's MOVZX
inline int ChannelCount(const Voice *v) {
    return uint8_t(v->channels);
}
inline int MixIndex(int platformVoice) {   // the MIX voice of a platform voice
    return platformVoice - MiddleVoices - HardwareVoices;
}
// The volume index from a gain's high byte (the original's MOVSX byte [gains + 2k + 1])
inline int16_t GainDb(const PlatformVoice *pv, int k) {
    return VolumeTable[reinterpret_cast<const int8_t *>(pv->gains)[2 * k + 1]];
}

// __ftol2: truncate ST0 to 64 bits; the callers keep EAX
inline int32_t Ftol(double d) {
    return int32_t(int64_t(d));
}

// ---- SNDLINKI on the buffer lists: a BufferList is an SNDLINKLIST whose nodes are BufferNodes (the links first)
static_assert(offsetof(BufferNode, next) == offsetof(SND::LinkNode, next) &&
              offsetof(BufferNode, prev) == offsetof(SND::LinkNode, prev), "a buffer node starts with the links");
static_assert(sizeof(BufferList) == sizeof(SND::LinkList), "a buffer list is an SNDLINKLIST");
inline SND::LinkList *AsLinkList(BufferList *list) {
    return reinterpret_cast<SND::LinkList *>(list);
}
inline SND::LinkNode *AsLinkNode(BufferNode *node) {
    return reinterpret_cast<SND::LinkNode *>(node);
}
inline void LinkInit(BufferList *list) {
    SNDLINKI_init(AsLinkList(list));
}
inline void LinkPush(BufferList *list, BufferNode *node) {
    SNDLINKI_push(AsLinkList(list), AsLinkNode(node));
}
inline void LinkPushTail(BufferList *list, BufferNode *node) {
    SNDLINKI_pushtail(AsLinkList(list), AsLinkNode(node));
}
inline BufferNode *LinkPop(BufferList *list) {
    return reinterpret_cast<BufferNode *>(SNDLINKI_pop(AsLinkList(list)));
}
inline void LinkRemove(BufferList *list, BufferNode *node) {
    SNDLINKI_remove(AsLinkList(list), AsLinkNode(node));
}

}  // namespace

// ---------------------------------------------------------------------------------------------------------------
// The buffer pools
// ---------------------------------------------------------------------------------------------------------------

// One pooled buffer: 48 kHz mono, Xbox ADPCM (pool 0) or PCM16 (pool 1), routed FC FR BR BL FL LFE at 0 dB and the
// first FX send at -100 dB.
// FUNC_AT(0x0013d430)
IDirectSoundBuffer* dsndCreateBufferAndMixBins(int pool) {
    IDirectSoundBuffer *buffer;
    DsMixBins bins;
    DsWaveFormat format;
    DsBufferDesc desc;
    DsMixBinPair pairs[7];
    memclr(&format, sizeof(format));
    memclr(&desc, sizeof(desc));
    uint16_t blockAlign;
    if (pool == SND::kPoolAdpcm) {
        format.formatTag = kFormatXboxAdpcm;
        format.cbSize = 2;
        format.bitsPerSample = 4;
        format.samplesPerBlock = 64;
        blockAlign = 36;
    } else {
        format.formatTag = kFormatPcm;
        format.bitsPerSample = 16;
        blockAlign = 2;
    }
    format.blockAlign = blockAlign;
    desc.format = &format;
    format.channels = 1;
    format.samplesPerSec = 48000;
    format.avgBytesPerSec = blockAlign * 48000u;
    DirectSoundCreateBuffer(&desc, &buffer);
    bins.pairs = pairs;
    pairs[0].bin = kBinFrontCenter;
    pairs[0].volume = 0;
    pairs[1].bin = kBinFrontRight;
    pairs[1].volume = 0;
    pairs[2].bin = kBinBackRight;
    pairs[2].volume = 0;
    pairs[3].bin = kBinBackLeft;
    pairs[3].volume = 0;
    pairs[4].bin = kBinFrontLeft;
    pairs[4].volume = 0;
    pairs[5].bin = kBinLowFrequency;
    pairs[5].volume = 0;
    pairs[6].bin = kBinFxSend0;
    pairs[6].volume = kVolumeMin;
    bins.count = 7;
    IDirectSoundBuffer_SetMixBins(buffer, &bins);
    return buffer;
}

// A node for 'pool' when its free list is empty: one from the other pool's free list, its buffer released and
// made again in this pool's format. The other pool is chosen as the original chooses it: when both are empty it
// pops from the list 0xc below the free lists (FreeBufferLists[-1], the PCM active list) - and faults if that is
// empty too (sound.md 8.5); kept.
// FUNC_AT(0x0013d550)
void FUN_0013d550(int pool) {
    int best = 0;
    int from = -1;
    if (pool != SND::kPoolAdpcm) {
        int count = FreeBufferLists[SND::kPoolAdpcm].count;
        if (count > 0) {
            best = count;
            from = SND::kPoolAdpcm;
        }
    }
    if (pool != SND::kPoolPcm16 && FreeBufferLists[SND::kPoolPcm16].count > best)
        from = SND::kPoolPcm16;
    BufferNode *node = LinkPop(&FreeBufferLists[from]);
    IDirectSoundBuffer_Release(node->buffer);
    node->buffer = dsndCreateBufferAndMixBins(pool);
    LinkPush(&FreeBufferLists[pool], node);
}

// MIX_VOICE_FREE_FUNC: a MIX voice has ended
// FUNC_AT(0x0013d5c0)
void SNDDRV_mixvoicefree(int mixVoice) {
    SNDVOICEI_free(mixVoice + MiddleVoices + HardwareVoices);
}

// ---------------------------------------------------------------------------------------------------------------
// The software mixer's rings
// ---------------------------------------------------------------------------------------------------------------

// MIX_create, then six rings of 50 ms of 16-bit mono PCM (rounded down to 16 frames), cleared, each routed to one
// speaker, played looping - for good.
// FUNC_AT(0x0013d5e0)
void dsndMixInit(void) {
    SND::MixCreateParams params;  // as the original builds it on the stack
    uint32_t rate = PlatformRate;
    params.counts.voices = MixerVoices;
    params.counts.channels = 6;
    params.rate = rate;
    params.voiceFree = reinterpret_cast<SND::MixVoiceFreeFn>(kMixVoiceFreeEntry);
    params.counts.unknown2[0] = 0;
    params.counts.unknown2[1] = 0;   // never written by the original (stack garbage there); 0 here
    MIX_create(&params);

    rate = PlatformRate;
    int32_t lead = int32_t(rate * 20) / 1000;
    MixPosition = 0;
    MixLead = lead;
    DsBufferDesc desc;
    memclr(&desc, sizeof(desc));
    rate = PlatformRate;
    int32_t frames = (int32_t(rate * 50) / 1000) & 0xffff0;
    RingFrames = frames;
    int32_t bytes = frames * 2;
    DsWaveFormat format;
    format.channels = 1;
    format.avgBytesPerSec = rate * 2;
    format.formatTag = kFormatPcm;
    format.blockAlign = 2;
    desc.bufferBytes = 0;
    desc.format = &format;
    format.samplesPerSec = rate;
    format.bitsPerSample = 16;
    format.cbSize = 0;
    DsMixBins bins;
    DsMixBinPair pairs[7];
    bins.count = 7;
    bins.pairs = pairs;
    for (int i = 0; i < 6; i++) {
        IDirectSound_CreateSoundBuffer(Device, &desc, &Rings[i], NULL);
        SNDSYS_entercritical();
        RingMemory[i] = static_cast<int16_t *>(SNDMEMI_alloc(bytes));
        SNDSYS_leavecritical();
        IDirectSoundBuffer_SetBufferData(Rings[i], RingMemory[i], bytes);
        IDirectSoundBuffer_SetCurrentPosition(Rings[i], 0);
        memclr(RingMemory[i], bytes);
        for (int k = 0; k < 7; k++) {
            pairs[k].bin = kRingBins[k];
            pairs[k].volume = kVolumeMin;
        }
        pairs[i].bin = kRingBins[i];
        pairs[i].volume = 0;
        IDirectSoundBuffer_SetMixBins(Rings[i], &bins);
        IDirectSoundBuffer_SetVolume(Rings[i], 0);
    }
    for (int i = 0; i < 6; i++)
        IDirectSoundBuffer_Play(Rings[i], 0, 0, kPlayLooping);
}

// FUNC_AT(0x0013d7d0)
void dsndMixStop(void) {
    for (int i = 0; i < 6; i++) {
        IDirectSoundBuffer_Stop(Rings[i]);
        IDirectSoundBuffer_Release(Rings[i]);
        SNDSYS_entercritical();
        SNDMEMI_free(RingMemory[i]);
        SNDSYS_leavecritical();
    }
    SNDSYS_entercritical();
    MIX_destroy();
    SNDSYS_leavecritical();
}

// Mix from the last position to 20 ms past ring 0's write cursor, in two parts across the wrap.
// FUNC_AT(0x0013d820)
void dsndMixProcess(void) {
    uint32_t write;
    IDirectSoundBuffer_GetCurrentPosition(Rings[0], NULL, &write);
    int32_t ringFrames = RingFrames;
    uint32_t cursor = (write >> 1) & 0xfffff0;
    int32_t target = int32_t((MixLead + cursor) & 0xffff0);
    if (target >= ringFrames)
        target -= ringFrames;
    int32_t last = MixPosition;
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
            outputs[k] = RingMemory[k] + last;
        MIX_audio(outputs, counts[part]);
        last = 0;
        MixPosition = 0;
    }
    MixPosition = target;
}

// ---------------------------------------------------------------------------------------------------------------
// Options, the device, the thread
// ---------------------------------------------------------------------------------------------------------------

// The logical voices a render mode may use: hardware 0..191, 0x10 none (192..192), main CPU 192..223.
// FUNC_AT(0x0013d900)
void SNDPLATFORM_getvoicerange(int mode, int *first, int *end) {
    if (mode & SND::kRenderHardware) {
        *first = 0;
        *end = HardwareVoices;
        return;
    }
    if (mode & SND::kRenderMode10) {
        *first = HardwareVoices;
        *end = MiddleVoices + HardwareVoices;
        return;
    }
    if (mode & SND::kRenderMainCpu) {
        *first = MiddleVoices + HardwareVoices;
        *end = NumVoices;
    }
}

// The SND thread: every 10 ms under SoundMutex the 100 Hz server, the mix and the packet callbacks. The deadline
// moves on by 10 ms a tick; a deadline already past sleeps 1 ms, so after a stall the ticks run back to back until
// they have caught up (sound.md 3.1, 8.3).
// FUNC_AT(0x0013d980)
uint32_t __stdcall SNDDRV_thread(void *parameter) {
    (void)parameter;
    // NIGHTFIRE_SNDLOCKSTEP: the steps run when the simulation hands them over, so the thread's loop is devtools'
    if (SndLockstep_Enabled())
        return SndLockstep_DriverThread();
    volatile uint8_t &keepRunning = KeepRunning;   // cleared by SNDPLATFORM_restore on another thread
    if (keepRunning != 0) {
        do {
            SNDI_mutexlock();
            FrameCallback pre = PreFrameCallback;
            if (pre != NULL)
                pre();
            SNDSYSI_100hzserver();
            dsndMixProcess();
            SNDPKTPLAYI_flushcallbackdata();
            FrameCallback post = PostFrameCallback;
            if (post != NULL)
                post();
            SNDI_mutexunlock();
            uint32_t now = getTickCount();
            uint32_t deadline = NextDeadline;
            int32_t wait = int32_t(deadline - now);
            NextDeadline = deadline + 10;
            if (wait < 0)
                wait = 1;
            Xbox_Sleep(wait);
        } while (keepRunning != 0);
    }
    static_cast<volatile uint8_t &>(IsRunning) = 0;
    return 0;
}

// The option ranges (MW: SNDSYSCAP)
// FUNC_AT(0x0013da00)
int SNDPLATFORM_outputcaps(void) {
    SND::SysCaps &caps = SndOptions.caps;
    caps.hardwareVoicesMax = 0xc0;
    caps.middleVoicesMax = 0;
    caps.mixerVoicesMax = 0x40;
    caps.outputRateMin = 8000;
    caps.outputRateMax = 48000;
    caps.outputModeMin = 5;
    caps.outputModeMax = 5;
    caps.numRenderModes = 2;
    caps.renderModes[0] = 0x420;
    caps.renderModes[1] = 0x24;
    RenderModeCount = 2;
    RenderModes[0] = 0x420;
    RenderModes[1] = 0x24;
    MixerVoices = 0x20;
    HardwareVoices = 0xc0;
    MiddleVoices = 0;
    NumStreams = 0x10;
    PlatformRate = 48000;
    OutputMode = 5;
    SndOptions.set.unknown29 = 1;
    SndOptions.set.unknown21 = 0;
    SndOptions.set.unknown14 = 0xa;
    uint16_t *azimuth = SndOptions.set.speakerAzimuth[5];   // five speakers
    azimuth[0] = 0;
    azimuth[1] = 0x2000;
    azimuth[2] = 0x6000;
    azimuth[3] = 0xa000;
    azimuth[4] = 0xe000;
    MixQuality = 2;
    return 0;
}

// The options clamped to the caps (MW: SNDSYSSET); once inited, the saved copy put back instead.
// FUNC_AT(0x0013dae0)
int SNDPLATFORM_outputset(void) {
    const SND::SysCaps &caps = SndOptions.caps;
    if (SystemInited != 0) {
        memcpy(&SndOptions.set, &SndSavedSet, sizeof(SND::SysSet));
        return 0;
    }
    uint8_t mode = OutputMode;
    if (mode < caps.outputModeMin) {
        mode = caps.outputModeMin;
        OutputMode = mode;
    }
    if (mode > caps.outputModeMax)
        OutputMode = caps.outputModeMax;
    uint16_t rate = PlatformRate;
    if (rate < caps.outputRateMin) {
        rate = caps.outputRateMin;
        PlatformRate = rate;
    }
    if (rate > caps.outputRateMax)
        PlatformRate = caps.outputRateMax;
    uint8_t hardware = HardwareVoices;
    if (hardware > caps.hardwareVoicesMax) {
        hardware = caps.hardwareVoicesMax;
        HardwareVoices = hardware;
    }
    uint8_t middle = MiddleVoices;
    if (middle > caps.middleVoicesMax) {
        middle = caps.middleVoicesMax;
        MiddleVoices = middle;
    }
    uint8_t mixer = MixerVoices;
    if (mixer > caps.mixerVoicesMax) {
        mixer = caps.mixerVoicesMax;
        MixerVoices = mixer;
    }
    if (NumStreams > 0x20)
        NumStreams = 0x20;
    NumVoices = int16_t(mixer + middle + hardware);
    // drop the zero render modes, closing the gaps
    uint8_t count = RenderModeCount;
    for (;;) {
        int i = 0;
        while (i < count && RenderModes[i] != 0)
            i++;
        if (i >= count)
            break;
        count--;
        RenderModeCount = count;
        for (; i < count; i++) {
            RenderModes[i] = RenderModes[i + 1];
            count = RenderModeCount;
        }
    }
    int k = SndOptions.set.unknown14;
    if (k < 1) {
        SndOptions.set.unknown14 = 1;
        return 0;
    }
    if (k > 200)
        k = 200;
    SndOptions.set.unknown14 = uint8_t(k);
    return 0;
}

// The device, the effects image, the platform voices, the 180 pooled buffers, the mixer's rings, the thread.
// FUNC_AT(0x0013dc50)
int SNDPLATFORM_init(void) {
    void *imageDesc;
    uint32_t location[2];        // DSEFFECTIMAGELOC: the I3DL2 reverb's index, the crosstalk's (none)
    DirectSoundCreate(NULL, &Device, NULL);
    location[0] = 0;
    location[1] = 0xffffffff;
    IDirectSound_DownloadEffectsImage(Device, EffectsImage, 0x3360, location, &imageDesc);
    SNDSYS_entercritical();
    void *voices = SNDMEMI_alloc(NumVoices * int32_t(sizeof(PlatformVoice)));
    PlatformVoices = static_cast<PlatformVoice *>(voices);
    memclr(voices, NumVoices * int32_t(sizeof(PlatformVoice)));
    PoolSizes[SND::kPoolAdpcm] = 152;
    PoolSizes[SND::kPoolPcm16] = 28;
    BufferNodes = static_cast<BufferNode *>(SNDMEMI_alloc(kBufferNodeCount * int32_t(sizeof(BufferNode))));
    SNDSYS_leavecritical();
    int node = 0;
    for (int pool = 0; pool < 2; pool++) {
        LinkInit(&ActiveBufferLists[pool]);
        LinkInit(&FreeBufferLists[pool]);
        for (int i = 0; i < PoolSizes[pool]; i++) {
            IDirectSoundBuffer *buffer = dsndCreateBufferAndMixBins(pool);
            BufferNodes[node].buffer = buffer;
            LinkPush(&FreeBufferLists[pool], &BufferNodes[node]);
            node++;
        }
    }
    dsndMixInit();
    IsRunning = 1;
    KeepRunning = 1;
    uint32_t now = getTickCount();
    NextDeadline = now;
    DWORD threadId;
    HANDLE thread = Xbox_CreateThread(NULL, 0x7d000, reinterpret_cast<LPTHREAD_START_ROUTINE>(kThreadEntry), NULL, 0,
                                      &threadId);
    Xbox_SetThreadPriority(thread, kThreadPriorityTimeCritical);
    return 0;
}

// Stop the thread (and wait for it), release every free buffer, the rings and the device.
// FUNC_AT(0x0013ddc0)
int SNDPLATFORM_restore(void) {
    KeepRunning = 0;
    while (static_cast<volatile uint8_t &>(IsRunning) != 0)   // cleared by the thread as it ends
        Xbox_Sleep(0);
    for (int pool = 0; pool < 2; pool++) {
        BufferNode *node;
        while ((node = LinkPop(&FreeBufferLists[pool])) != NULL)
            IDirectSoundBuffer_Release(node->buffer);
    }
    dsndMixStop();
    SNDSYS_entercritical();
    SNDMEMI_free(BufferNodes);
    SNDMEMI_free(PlatformVoices);
    SNDSYS_leavecritical();
    IDirectSound_Release(Device);
    return 0;
}

// ---------------------------------------------------------------------------------------------------------------
// The per-voice operations
// ---------------------------------------------------------------------------------------------------------------

// Stop a sound: hardware buffers back to the free lists' tails (a packet voice's ring freed with its first
// channel), or the MIX voices stopped; then every channel's voice freed.
// FUNC_AT(0x0013de50)
int SNDPLATFORM_stop(int voice) {
    Voice *v = &VoiceArray[voice];
    if (v->renderMode & (SND::kRenderHardware | SND::kRenderMode10)) {
        for (int i = 0; i < ChannelCount(v); i++) {
            BufferNode *node = PlatformVoices[v->platformVoices[i]].node;
            IDirectSoundBuffer_Stop(node->buffer);
            if (node->player >= 0 && i == 0)
                SNDMEMI_free(node->memory);
            LinkRemove(&ActiveBufferLists[node->pool], node);
            LinkPushTail(&FreeBufferLists[node->pool], node);
        }
    } else {
        for (int i = 0; i < ChannelCount(v); i++)
            MIX_stop(MixIndex(v->platformVoices[i]));
    }
    for (int i = 0; i < ChannelCount(v); i++)
        SNDVOICEI_free(v->platformVoices[i]);
    return 0;
}

// Volume: a hardware buffer's from the volume table at vol x programmed / 127, rounded; a MIX voice's per-speaker
// dry gains, speaker gain x vol x programmed x 1.89e-9. Then the fx send again if there is one.
// FUNC_AT(0x0013df50)
void SNDPLATFORM_setvol(int voice) {
    Voice *v = &VoiceArray[voice];
    if (v->renderMode & (SND::kRenderHardware | SND::kRenderMode10)) {
        for (int i = 0; i < ChannelCount(v); i++) {
            double product = double(v->vol) * int8_t(v->progVol);   // FILD, FIMUL: a zero keeps its sign
            int index = Ftol(product * kOver127 + 0.5);
            int16_t db = VolumeTable[index];
            IDirectSoundBuffer_SetVolume(PlatformVoices[v->platformVoices[i]].node->buffer, db);
        }
    } else {
        double product = double(int8_t(v->progVol)) * v->vol;   // FILD, FIMUL: -0 when one is 0, one < 0
        float scale = float(product * kDryScale);
        for (int i = 0; i < ChannelCount(v); i++) {
            PlatformVoice *p = &PlatformVoices[v->platformVoices[i]];
            for (int s = 0; s < OutputMode; s++) {
                float gain = p->gains[s] * scale;   // one product, rounded once to float: as the x87's
                SNDMIX_setdrygain(MixIndex(v->platformVoices[i]), s, gain);
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
    Voice *v = &VoiceArray[voice];
    PlatformVoice *self = &PlatformVoices[voice];
    if (ChannelCount(v) == 1) {
        SNDI_aztospkrvol(v->azimuth, self->gains);
    } else {
        for (int i = 0; i < ChannelCount(v); i++) {
            PlatformVoice *p = &PlatformVoices[v->platformVoices[i]];
            if (i == 5) {
                memset(p->gains, 0, 10);
                p->gains[5] = 0x7fff;
            } else {
                SNDI_aztospkrvol(v->channelAzimuth[i], p->gains);
                p->gains[5] = 0;
            }
        }
    }
    if ((v->renderMode & SND::kRenderHardware) == 0) {
        SNDPLATFORM_setvol(voice);
        return;
    }
    DsMixBinPair pairs[6];
    DsMixBins bins;
    bins.count = 6;
    bins.pairs = pairs;
    if (ChannelCount(v) == 1) {
        pairs[0].bin = kBinFrontCenter;
        pairs[0].volume = GainDb(self, 0);
        pairs[1].bin = kBinFrontRight;
        pairs[1].volume = GainDb(self, 1);
        pairs[2].bin = kBinBackRight;
        pairs[2].volume = GainDb(self, 2);
        pairs[3].bin = kBinBackLeft;
        pairs[3].volume = GainDb(self, 3);
        pairs[4].bin = kBinFrontLeft;
        pairs[4].volume = GainDb(self, 4);
        pairs[5].bin = kBinLowFrequency;
        pairs[5].volume = kVolumeMin;
        IDirectSoundBuffer_SetMixBinVolumes(self->node->buffer, &bins);
        return;
    }
    for (int i = 0; i < ChannelCount(v); i++) {
        PlatformVoice *p = &PlatformVoices[v->platformVoices[i]];
        pairs[0].bin = kBinFrontCenter;
        pairs[0].volume = GainDb(p, 0);
        pairs[1].bin = kBinFrontRight;
        pairs[1].volume = GainDb(p, 1);
        pairs[2].bin = kBinBackRight;
        pairs[2].volume = GainDb(p, 2);
        pairs[3].bin = kBinBackLeft;
        pairs[3].volume = GainDb(p, 3);
        pairs[4].bin = kBinFrontLeft;
        pairs[4].volume = GainDb(p, 4);
        pairs[5].bin = kBinLowFrequency;
        pairs[5].volume = GainDb(p, 5);
        IDirectSoundBuffer_SetMixBinVolumes(PlatformVoices[v->platformVoices[i]].node->buffer, &bins);
    }
}

// Pitch: a hardware buffer's frequency, rate x pitch / 4096 clamped to 188..191983 Hz (a packet voice's multiplier
// capped at 2.0); a multiplier of 0 pauses (Stop), and the first non-zero one after resumes (Play with the saved
// loop flag). A MIX voice's 16.16 step, (rate << 16) / output rate x pitch >> 12.
// FUNC_AT(0x0013e320)
int SNDPLATFORM_setpitch(int voice) {
    Voice *v = &VoiceArray[voice];
    if ((v->renderMode & (SND::kRenderHardware | SND::kRenderMode10)) == 0) {
        uint32_t step = (uint32_t(v->sampleRate) << 16) / PlatformRate;
        step = (step * v->pitch) >> 12;
        for (int i = 0; i < ChannelCount(v); i++)
            MIX_setpitch(MixIndex(v->platformVoices[i]), step);
        return 0;
    }
    // The platform voice is looked up again after each DirectSound call, as the original does
    if (v->pitch == 0) {
        bool stopped = true;
        if (ChannelCount(v) != 0) {
            for (int i = 0; i < ChannelCount(v); i++) {
                PlatformVoice *p = &PlatformVoices[v->platformVoices[i]];
                uint8_t playing = p->playing;
                if (playing == 1) {
                    IDirectSoundBuffer_Stop(p->node->buffer);
                    PlatformVoices[v->platformVoices[i]].playing = 0;
                    stopped = true;
                } else if (playing == 0) {
                    stopped = false;
                }
            }
            if (!stopped)
                return 0;
        }
    } else {
        for (int i = 0; i < ChannelCount(v); i++) {
            PlatformVoice *p = &PlatformVoices[v->platformVoices[i]];
            if (p->playing == 0) {
                IDirectSoundBuffer_Play(p->node->buffer, 0, 0, p->looping);
                PlatformVoices[v->platformVoices[i]].playing = 1;
            }
        }
    }
    if (PlatformVoices[voice].node->player >= 0 && v->pitch > 0x2000)
        v->pitch = 0x2000;
    uint32_t frequency = Ftol(double(v->sampleRate) * v->pitch * kOver4096);
    if (frequency < kFrequencyMin)
        frequency = kFrequencyMin;
    else if (frequency > kFrequencyMax)
        frequency = kFrequencyMax;
    for (int i = 0; i < ChannelCount(v); i++)
        IDirectSoundBuffer_SetFrequency(PlatformVoices[v->platformVoices[i]].node->buffer, frequency);
    return 0;
}

// The fx send: a MIX voice's per bus, send x vol x 2.4e-7; a hardware voice's (bus 0's send whatever the bus)
// through mix bin 11 at the volume table's entry for send x vol / 127 >> 8.
// FUNC_AT(0x00140070)
int SNDPLATFORM_setfxlevel(int voice, int bus) {
    Voice *v = &VoiceArray[voice];
    if (v->renderMode & SND::kRenderMainCpu) {
        int32_t send = (&v->fxSend)[bus];   // bus 1's is the next field's halfword, as the original indexes it
        float level = float(double(send) * v->vol * kFxScale);
        for (int i = 0; i < ChannelCount(v); i++)
            MIX_setfxlevel(MixIndex(v->platformVoices[i]), bus, level);
        return 0;
    }
    DsMixBinPair pair;
    DsMixBins bins;
    bins.pairs = &pair;
    bins.count = 1;
    int32_t index = Ftol(double(v->fxSend) * v->vol * kOver127);
    if (ChannelCount(v) == 0)
        return 0;
    int16_t db = VolumeTable[index >> 8];
    for (int i = 0; i < ChannelCount(v); i++) {
        pair.volume = db;
        pair.bin = kBinFxSend0;
        IDirectSoundBuffer_SetMixBinVolumes(PlatformVoices[v->platformVoices[i]].node->buffer, &bins);
    }
    return 0;
}

// The hardware bus's reverb: its mode picks one of 23 I3DL2 listener presets (anything else the first). The mode is
// read sign-extended, as the original reads it.
// FUNC_AT(0x00140880)
int DirectSound_SetListenerRelated(void) {
    uint32_t mode = int16_t(FxBusHardware[0].mode);
    uint32_t preset = kPresetGeneric;
    if (mode <= 60)
        preset = kBusModePreset[mode];
    return int(IDirectSound_SetI3DL2Listener(Device, &ListenerPresets[preset], 0));
}

// SNDPLATFORM_fxinit's worker: the hardware path's reverb preset, or the mixer's software reverb (off, the bus's
// own description in mode 1 - held in its delay field - the default otherwise).
// FUNC_AT(0x00140a10)
void SNDDRV_setfx(int path) {
    if (path & (SND::kRenderHardware | SND::kRenderMode10)) {
        DirectSound_SetListenerRelated();
        return;
    }
    if ((path & SND::kRenderMainCpu) == 0)
        return;
    SNDSYS_entercritical();
    uint16_t mode = FxBusMainCpu[0].mode;
    if (mode == 0) {
        MixFxInitFunc = 0;
        MixFxHook = 0;
        MIX_restorereverb();
        SNDSYS_leavecritical();
        return;
    }
    const uint8_t *description = mode == 1 ? reinterpret_cast<const uint8_t *>(FxBusMainCpu[0].delay) : DefaultReverb;
    MIX_initreverb(PlatformRate, description);
    SNDSYS_leavecritical();
}

// FUNC_AT(0x00140a80)
int SNDPLATFORM_fxinit(int bus, int path) {
    (void)bus;
    SetFxFunc = kSetFxEntry;
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
    Voice *v = &VoiceArray[uint16_t(node->platformVoice)];
    int frames = 0;
    int other;
    uint32_t play;
    IDirectSoundBuffer_GetCurrentPosition(node->buffer, &play, NULL);
    uint32_t aligned = play / 36 * 36;
    uint32_t write = node->writePos;
    int32_t space = aligned >= write ? int32_t(aligned - write) : int32_t(node->size - write + aligned);
    if (space <= 0) {
        node->writePos = aligned;
        return;
    }
    uint8_t *source[6];
    do {
        if (node->packet == NULL) {
            for (int ch = 0; ch < ChannelCount(v); ch++) {
                uint8_t *data = SNDPKTPLAYI_get(node->player, ch, &frames, &other);
                PlatformVoices[v->platformVoices[ch]].node->packet = data;
            }
            node->packetUsed = 0;
            if (node->pool == SND::kPoolAdpcm)
                node->packetBytes = int32_t(uint32_t(frames) * 36) >> 6;
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
            for (int ch = 0; ch < ChannelCount(v); ch++)
                source[ch] = PlatformVoices[v->platformVoices[ch]].node->packet + used;
            node->packetUsed = used + chunk;
        }
        space -= chunk;
        while (chunk > 0) {
            int32_t part = int32_t(node->size - node->writePos);
            if (part > chunk)
                part = chunk;
            for (int ch = 0; ch < ChannelCount(v); ch++) {
                BufferNode *n = PlatformVoices[v->platformVoices[ch]].node;
                if (node->packet != NULL) {
                    memcpy(n->memory + node->writePos, source[ch], uint32_t(part));
                    source[ch] += part;
                    n->silence = 0;
                    int32_t consumed = node->pool == SND::kPoolAdpcm ? int32_t(uint32_t(part) << 6) / 36 : part >> 1;
                    SNDPKTPLAYI_freeframes(node->player, ch, consumed);
                } else if (node->silence < node->size) {
                    memclr(PlatformVoices[uint16_t(node->platformVoice)].node->memory + node->writePos, part);
                    node->silence += part;
                }
            }
            uint32_t position = node->writePos + chunk;
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
    int index = mixVoice + HardwareVoices + MiddleVoices;
    int16_t master = VoiceArray[index].master;
    if (master != -1)
        return master;
    return index;
}

// Which channel of its sound a MIX voice is (0 for the master or when not found)
// FUNC_AT(0x00142460)
int SNDDRV_getsamplechan(int mixVoice) {
    int index = mixVoice + HardwareVoices + MiddleVoices;
    int16_t master = VoiceArray[index].master;
    if (master == -1)
        return 0;
    Voice *m = &VoiceArray[master];
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
    Voice *v = &VoiceArray[voice];
    if (v->renderMode & SND::kRenderMainCpu) {
        for (int ch = 0; ch < ChannelCount(v); ch++)
            MIX_playinit(MixIndex(v->platformVoices[ch]), format->sampleRep, 0, NULL, -1, data[ch], ChannelCount(v),
                         v->frames, -1, -1, 0, 0, ch);
        int16_t *channels = v->platformVoices;
        SNDPLATFORM_setpitch(channels[0]);
        SNDPLATFORM_set3dpos(channels[0]);
        SNDPLATFORM_setfxlevel(channels[0], 0);
        SNDPLATFORM_timemult(channels[0], timeMult);
        SNDPLATFORM_lowpass(channels[0], lowpass);
        SNDPLATFORM_highpass(channels[0], highpass);
        for (int ch = 0; ch < ChannelCount(v); ch++)
            MIX_play(MixIndex(channels[ch]));
        return 0;
    }
    SND_UNTESTED("SNDPLATFORM_packetplay on a hardware voice");
    PlatformVoice *self = &PlatformVoices[voice];
    int32_t frames = Ftol(double(format->sampleRate) * kPacketSeconds);
    int pool;
    int32_t bytes;
    if (format->sampleRep == kSampleRepXboxAdpcm) {
        bytes = (int32_t(uint32_t(frames) * 36) >> 6) + 35;   // whole 36-byte blocks, rounded up
        bytes = bytes / 36 * 36;
        pool = SND::kPoolAdpcm;
    } else {
        pool = SND::kPoolPcm16;
        bytes = int32_t(uint32_t(frames) << 1);
    }
    for (int ch = 0; ch < ChannelCount(v); ch++) {
        if (FreeBufferLists[pool].count <= 0)
            FUN_0013d550(pool);
        BufferNode *node = LinkPop(&FreeBufferLists[pool]);
        LinkPush(&ActiveBufferLists[pool], node);
        PlatformVoices[v->platformVoices[ch]].node = node;
        node->platformVoice = v->platformVoices[ch];
        node->pool = uint8_t(pool);
        node->size = bytes;
        node->writePos = 0;
        node->packet = NULL;
        node->player = player;
        node->packetBytes = 0;
        node->packetUsed = 0;
        node->silence = 0;
        if (ch == 0) {
            node->memory = static_cast<uint8_t *>(SNDMEMI_alloc(ChannelCount(v) * bytes));
            memclr(node->memory, ChannelCount(v) * bytes);
        } else {
            node->memory = self->node->memory + bytes * ch;
        }
        IDirectSoundBuffer_SetBufferData(node->buffer, node->memory, bytes);
        IDirectSoundBuffer_SetLoopRegion(node->buffer, 0, bytes);
        IDirectSoundBuffer_SetCurrentPosition(node->buffer, 0);
    }
    SNDPLATFORM_setpitch(voice);
    SNDPLATFORM_set3dpos(voice);
    SNDPLATFORM_setfxlevel(voice, 0);
    SNDPLATFORM_setvol(voice);
    SNDPLATFORM_lowpass(voice, lowpass);
    for (int ch = ChannelCount(v) - 1; ch >= 0; ch--) {
        int pv = v->platformVoices[ch];
        IDirectSoundBuffer_Play(PlatformVoices[pv].node->buffer, 0, 0, kPlayLooping);
        PlatformVoices[v->platformVoices[ch]].playing = 1;
        PlatformVoices[v->platformVoices[ch]].looping = 1;
    }
    return 0;
}

// A user filter on a MIX voice's chains (-7 on a hardware voice)
// FUNC_AT(0x001427d0)
int SNDPLATFORM_filteradd(int voice, int filter) {
    Voice *v = &VoiceArray[voice];
    if ((v->renderMode & SND::kRenderMainCpu) == 0)
        return -7;
    for (int ch = 0; ch < ChannelCount(v); ch++)
        SFILTER_add(v->platformVoices[ch], ch, reinterpret_cast<const SND::SFilterDesc *>(filter));
    return 0;
}

// Low pass: a hardware buffer's DirectSound filter from the coefficient table (cutoff >> 5, capped at 0x1fe0), a
// MIX voice's as a fraction of the Nyquist rate.
// FUNC_AT(0x001429f0)
void SNDPLATFORM_lowpass(int voice, int cutoff) {
    Voice *v = &VoiceArray[voice];
    uint32_t mode = v->renderMode;
    if (mode & (SND::kRenderHardware | SND::kRenderMode10)) {
        if (cutoff > 0x1fe0)
            cutoff = 0x1fe0;
        DsFilterDesc desc;
        desc.mode = 1;
        desc.coefficients[0] = LowpassTable[cutoff >> 5];
        desc.coefficients[1] = 0xffff;
        desc.coefficients[2] = 0;
        desc.coefficients[3] = 0;
        desc.q = 0;
        for (int ch = 0; ch < ChannelCount(v); ch++)
            IDirectSoundBuffer_SetFilter(PlatformVoices[v->platformVoices[ch]].node->buffer, &desc);
        return;
    }
    if ((mode & SND::kRenderMainCpu) == 0)
        return;
    double nyquist = PlatformRate * 0.5;
    float fraction = float(1.0 / nyquist * cutoff);
    for (int ch = 0; ch < ChannelCount(v); ch++)
        MIX_setlowpass(MixIndex(v->platformVoices[ch]), fraction);
}

// Start a bank sample: hardware buffers from the pools (ADPCM for sample representation 20, else PCM16, borrowing
// when a pool is empty) given the sample in place in the bank, the loop rounded to whole 64-sample ADPCM blocks;
// or MIX voices on the bank data.
// FUNC_AT(0x00142b10)
int SNDPLATFORM_playtimbre(SND::PatchHeader *header, uint8_t *base, int voice, int timeMult, int distort,
                           int lowpass, int highpass) {
    (void)distort;
    Voice *v = &VoiceArray[voice];
    int32_t loopStart = 0;
    int32_t loopLength = 0;
    if (v->renderMode & SND::kRenderHardware) {
        uint8_t rep = header->sampleRep;
        int32_t frames = v->frames;
        int pool;
        uint32_t bytes;
        if (rep == kSampleRepXboxAdpcm) {
            bytes = (((uint32_t(frames + 0x3f) >> 6) * 9) << 8) >> 6;   // whole blocks of 64 samples, 36 bytes
            pool = SND::kPoolAdpcm;
        } else {
            pool = SND::kPoolPcm16;
            bytes = uint32_t(frames * 2);
        }
        int32_t end = header->loopEnd;
        if (end > 0) {
            int32_t start = header->loopStart;
            if (rep == kSampleRepXboxAdpcm) {
                if ((start & 0x3f) >= 0x20)
                    start = int32_t((uint32_t(start + 0x3f) >> 6) << 6);
                else
                    start &= 0x7fffffc0;
                if ((end & 0x3f) >= 0x20 && end + 0x20 < frames)
                    end = int32_t((uint32_t(end + 0x3f) >> 6) << 6);
                else
                    end &= 0x7fffffc0;
                int32_t length = end - start;
                loopStart = int32_t(uint32_t(start) * 36) >> 6;
                loopLength = int32_t(uint32_t(length) * 36) >> 6;
            } else {
                loopStart = start * 2;
                loopLength = (end - start) * 2 + 2;
            }
        }
        for (int ch = 0; ch < ChannelCount(v); ch++) {
            if (FreeBufferLists[pool].count <= 0)
                FUN_0013d550(pool);
            BufferNode *node = LinkPop(&FreeBufferLists[pool]);
            LinkPush(&ActiveBufferLists[pool], node);
            PlatformVoices[v->platformVoices[ch]].node = node;
            node->platformVoice = v->platformVoices[ch];
            node->pool = uint8_t(pool);
            node->player = -1;
            IDirectSoundBuffer_SetBufferData(node->buffer, base + header->sampleOffsets[ch], bytes);
            if (header->loopEnd > 0)
                IDirectSoundBuffer_SetLoopRegion(node->buffer, loopStart, loopLength);
            IDirectSoundBuffer_SetCurrentPosition(node->buffer, 0);
        }
        SNDPLATFORM_setpitch(voice);
        SNDPLATFORM_set3dpos(voice);
        SNDPLATFORM_setfxlevel(voice, 0);
        SNDPLATFORM_setvol(voice);
        SNDPLATFORM_lowpass(voice, lowpass);
        int32_t looping = header->loopEnd > 0 ? 1 : 0;
        for (int ch = 0; ch < ChannelCount(v); ch++) {
            IDirectSoundBuffer_Play(PlatformVoices[v->platformVoices[ch]].node->buffer, 0, 0, looping);
            PlatformVoices[v->platformVoices[ch]].playing = 1;
            PlatformVoices[v->platformVoices[ch]].looping = looping;
        }
        return 0;
    }
    for (int ch = 0; ch < ChannelCount(v); ch++)
        MIX_playinit(MixIndex(v->platformVoices[ch]), header->sampleRep, 1, base + header->sampleOffsets[ch],
                     header->tag1a, header->stretchData[ch], ChannelCount(v), v->frames, header->loopStart,
                     header->loopEnd, 0, 0, ch);
    SNDPLATFORM_setpitch(voice);
    SNDPLATFORM_set3dpos(voice);
    SNDPLATFORM_setfxlevel(voice, 0);
    int16_t *channels = v->platformVoices;
    SNDPLATFORM_timemult(channels[0], timeMult);
    SNDPLATFORM_lowpass(channels[0], lowpass);
    SNDPLATFORM_highpass(channels[0], highpass);
    for (int ch = 0; ch < ChannelCount(v); ch++)
        MIX_play(MixIndex(channels[ch]));
    return 0;
}

// FUNC_AT(0x00144960)
void SNDPLATFORM_highpass(int voice, int cutoff) {
    Voice *v = &VoiceArray[voice];
    if ((v->renderMode & SND::kRenderMainCpu) == 0)
        return;
    for (int ch = 0; ch < ChannelCount(v); ch++)
        MIX_sethighpass(MixIndex(v->platformVoices[ch]), cutoff);
}

// FUNC_AT(0x001449c0)
int SNDPLATFORM_timemult(int voice, int mult) {
    Voice *v = &VoiceArray[voice];
    if (v->renderMode & SND::kRenderMainCpu) {
        for (int ch = 0; ch < ChannelCount(v); ch++)
            MIX_settimemult(MixIndex(v->platformVoices[ch]), mult);
    }
    return 0;
}
