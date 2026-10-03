#ifndef DRIVING_SOUND_SND_MIXER_H_
#define DRIVING_SOUND_SND_MIXER_H_

// EA's software mixer (docs/driving/sound.md 2.4, 3.6, 4.7): the MIX voices, the slice loop, the gain ramps and
// mixc. See Mixer.cpp. The software reverb and the FX building blocks are in Reverb.h; the mixer's and the reverb's
// state is one block of globals, SND::MixState below.

#include "Filters.h"
#include "FiltersUnused.h"

#include <stddef.h>
#include <stdint.h>

namespace SND {

// A MIX voice (Ghidra MIX, 0x60), 32 of them at SndMix.voices.
struct MixVoice {
    uint8_t state;                   // +0x00 MixVoiceState
    uint8_t gainsChanged;            // +0x01 ramp to the targets on the next slice
    uint8_t pad02[2];
    float dry[6];                    // +0x04 current per-speaker gains
    float dryTarget[6];              // +0x1c
    float fx;                        // +0x34 current fx send
    float fxTarget;                  // +0x38
    float lastSample;                // +0x3c the voice's last output sample (for the ramp to zero on a stop)
    SFilterNode *chain;              // +0x40 the filter chain's head (pulled by the mixer)
    SFilterGetFrame getFrame;        // +0x44 UnpackInfo::getFrame after the unpacker's init
    SFilterNode *unpacker;           // +0x48
    SFilterStretch *stretch;         // +0x4c time stretch
    SFilterRSF *resampler;           // +0x50
    uint32_t unknown54;              // +0x54
    SFilterLPFRC *lowpass;           // +0x58
    SFilterFIR8 *highpass;           // +0x5c
};
static_assert(sizeof(MixVoice) == 0x60, "a MIX voice is 0x60 bytes");

enum MixVoiceState : uint8_t {
    kMixVoiceFree = 0,
    kMixVoiceInitialised = 1,
    kMixVoicePlaying = 2,
};

// The reverb on the FX send (SndMix.reverbState)
enum MixReverbState : uint8_t {
    kReverbOff = 0,
    kReverbFx2 = 1,                  // the node network (description mode 10)
    kReverbStandard = 2,             // the allpass taps
};

typedef void (*MixVoiceFreeFn)(int voice);
typedef void (*MixFxHook)(int frames);                                   // run once a slice
typedef void (*MixFxInitFn)(const uint8_t *description);
typedef void (*MixFn)(int count, float gain, const float *in, float *out);

// NUM_MIXES: written as a dword (MIX_create), read as bytes
struct MixCounts {
    uint8_t voices;                  // +0 MIX voices (32)
    uint8_t channels;                // +1 output channels (6)
    uint8_t unknown2[2];
};
static_assert(sizeof(MixCounts) == 4, "NUM_MIXES is a dword");

// MIX_create's argument
struct MixCreateParams {
    uint32_t rate;                   // +0 -> SndMix.outputRate
    MixCounts counts;                // +4 -> SndMix.counts
    MixVoiceFreeFn voiceFree;        // +8 -> SndMix.voiceFree
};

// A standard reverb tap (0x24), up to 11 in SndMix.taps in three groups
struct ReverbTap {
    int32_t writePos;                // +0x00 counts down to 0, then wraps to length
    int32_t readPos;                 // +0x04
    int32_t length;                  // +0x08 a prime number of samples (findprime)
    float *buffer;                   // +0x0c
    float gain;                      // +0x10 the allpass coefficient g
    float negGain;                   // +0x14 -g
    float lowpass;                   // +0x18 the feedback low pass's state
    float lowpassKeep;               // +0x1c
    float lowpassInput;              // +0x20
};
static_assert(sizeof(ReverbTap) == 0x24, "a reverb tap is 0x24 bytes");

// The head of an fx2 description (sound.md 3.6), copied to SndMix.fx2Header: per output configuration (the
// channel count - 1) the number of node entries and of connections, big-endian
struct Fx2Header {
    uint8_t head[8];                 // +0x00 byte 2 the mode (10)
    uint8_t nodeCounts[6][4];        // +0x08
    uint8_t connCounts[6][4];        // +0x20
};
static_assert(sizeof(Fx2Header) == 0x38, "an fx2 description's head is 14 dwords");

// An fx2 connection entry (12 bytes): node 'from' feeds input 'input' of node 'to'
struct Fx2Connection {
    uint8_t from[4];                 // +0x00 node index, big-endian
    uint8_t to[4];                   // +0x04
    uint8_t output;                  // +0x08
    uint8_t input;                   // +0x09
    uint8_t unknown0a[2];
};
static_assert(sizeof(Fx2Connection) == 12, "an fx2 connection is 12 bytes");

// The mixer's and the reverb's globals (sound.md 2.4), 0x00245990..0x002475fc
struct MixState {
    uint32_t outputRate;             // +0x0000 0x00245990 sndmix (48000)
    MixCounts counts;                // +0x0004 0x00245994 NUM_MIXES
    MixVoiceFreeFn voiceFree;        // +0x0008 0x00245998 MIX_VOICE_FREE_FUNC: SNDDRV_mixvoicefree
    uint16_t outputStep;             // +0x000c 0x0024599c bytes a ring advances per slice (0x400)
    uint8_t resamplerMode;           // +0x000e 0x0024599e >= 50: the resampler's second mode (nothing writes it)
    uint8_t reverbState;             // +0x000f 0x0024599f MixReverbState
    MixFxInitFn fxInit;              // +0x0010 0x002459a0 SNDMIXI_fxinit
    MixFxHook fxHook;                // +0x0014 0x002459a4 unpackerInitFuncs[0]: the per-slice FX hook
    SFilterUnpackInit unpackerInit[31];   // +0x0018 0x002459a8 unpackerInitFuncs[1..31], by MIX_playinit's
                                          //         index: 3-5 PCM16 (plain, loop, packet), 6-8 EA-XA, 9/11
                                          //         MicroTalk
    uint32_t unknown94;              // +0x0094 0x00245a24
    uint32_t unknown98;              // +0x0098 0x00245a28 unpackerInitSizes[0]
    uint32_t unpackerSize[31];       // +0x009c 0x00245a2c unpackerInitSizes[1..31]: the node sizes
    uint32_t unknown118[2];          // +0x0118 0x00245aa8
    void *scratchRaw[2];             // +0x0120 0x00245ab0 two 0x40bc-byte allocations
    float *scratch[2];               // +0x0128 0x00245ab8 their 64-byte-aligned buffers: [0] A, [1] B
    void *accumRaw[6];               // +0x0130 0x00245ac0 the six channels' 0x840-byte accumulators
    float *accum[6];                 // +0x0148 0x00245ad8 the same, 64-byte aligned
    float rampToZero[6];             // +0x0160 0x00245af0 per channel: gain still ramping to zero
    float fxRampToZero;              // +0x0178 0x00245b08
    SFilterNode *outputList[6];      // +0x017c 0x00245b0c per channel: the output filter list
    SFilterLPFRC *masterFilter[6];   // +0x0194 0x00245b24 per channel: the optional master low pass
    SFilterNode outputNodes[6];      // +0x01ac 0x00245b3c the output stages, process SFILTER_ft24_32
    MixVoice *voices;                // +0x0254 0x00245be4 MixList: [counts.voices]
    MixFn mixFunc;                   // +0x0258 0x00245be8 mixc
    uint32_t unknown25c;             // +0x025c 0x00245bec
    int32_t tapCounts[3];            // +0x0260 0x00245bf0 the standard reverb's three groups
    const Fx2Connection *fx2Connections;   // +0x026c 0x00245bfc the selected configuration's connections
    ReverbTap taps[11];              // +0x0270 0x00245c00
    const uint8_t *fx2Entries;       // +0x03fc 0x00245d8c the selected configuration's node entries
    Fx2Header fx2Header;             // +0x0400 0x00245d90
    uint32_t fx2Active;              // +0x0438 0x00245dc8
    uint32_t unknown43c;             // +0x043c 0x00245dcc
    float fxSend[512];               // +0x0440 0x00245dd0 sndfx, the FX send accumulator
    float fxGroup1[512];             // +0x0c40 0x002465d0 the standard reverb's group 1 output
    float fxGroup2[512];             // +0x1440 0x00246dd0 group 2's
    int32_t fxIdle;                  // +0x1c40 0x002475d0 slices since a voice last fed the FX send
    SFilterNode **fx2Nodes;          // +0x1c44 0x002475d4 [node count]
    const Fx2Header *fx2Desc;        // +0x1c48 0x002475d8 = &fx2Header
    SFilterNode **fx2Routing;        // +0x1c4c 0x002475dc [6] the routed nodes, by route
    uint32_t fx2Source;              // +0x1c50 0x002475e0 index of the SOURCE node
    int32_t fx2Config;               // +0x1c54 0x002475e4 configuration + 1
    const Fx2Connection *fx2ConnCursor;    // +0x1c58 0x002475e8
    const uint8_t *fx2EntryCursor;   // +0x1c5c 0x002475ec
    int32_t fx2ConfigCopy;           // +0x1c60 0x002475f0
    void *codaNew;                   // +0x1c64 0x002475f4 CODA_New: SNDMEMI_alloc's thunk
    void *codaDelete;                // +0x1c68 0x002475f8 CODA_Delete
};
static_assert(offsetof(MixState, scratchRaw) == 0x120, "the scratch allocations are at 0x00245ab0");
static_assert(offsetof(MixState, voices) == 0x254, "MixList is at 0x00245be4");
static_assert(offsetof(MixState, taps) == 0x270, "the reverb taps are at 0x00245c00");
static_assert(offsetof(MixState, fxSend) == 0x440, "sndfx is at 0x00245dd0");
static_assert(offsetof(MixState, fxIdle) == 0x1c40, "the FX idle count is at 0x002475d0");
static_assert(sizeof(MixState) == 0x1c6c, "the mixer's globals end at 0x002475fc");

void CODASetNew(void *allocate);                                             // 0x001446e0
void CODASetDelete(void *release);                                           // 0x001446f0

}  // namespace SND

// The mixer's globals, where the original keeps them (the filters, the decoders and the platform driver read some)
#define SndMix (*(SND::MixState *)0x00245990)

void MIXI_interpolateto0(float *gain, float *buffer);                       // 0x001413e0
void MIXI_interpolatemix(float from, float to, float *in, float *out);      // 0x001414d0
void SNDMIX_setmasterlowpass(float cutoff);                                 // 0x00141630
int SNDMIXI_volramp(SND::MixVoice *voice);                                  // 0x00141710
void MIX_destroy(void);                                                     // 0x00141880
void MIX_playinit(int voice, int sampleRep, int kind, const void *data, int p4, const uint8_t *stretchData, int p6,
                  int p7, int p8, int loop, int p10, int p11, int requester);       // 0x00141910
void MIX_play(int voice);                                                   // 0x00141ad0
void MIX_stop(int voice);                                                   // 0x00141b20
void SNDMIX_setdrygain(int voice, int speaker, float gain);                 // 0x00141bb0
void MIX_setfxlevel(int voice, int send, float level);                      // 0x00141be0
void MIX_create(const SND::MixCreateParams *params);                        // 0x00141c20
void MIX_audioslice(int16_t **outputs, int frames);                         // 0x00141db0
void MIX_audio(int16_t **outputs, int frames);                              // 0x00142050
void MIX_setpitch(int voice, int pitch);                                    // 0x001420c0
void mixc(int count, float gain, const float *in, float *out);              // 0x00143b40
void MIXI_initunpackmt(void);                                               // 0x00144630
void MIXI_initunpackxa(void);                                               // 0x00144660
void MIXI_initunpack16(void);                                               // 0x001446a0
void MIX_setlowpass(int voice, float cutoff);                               // 0x00144af0
void MIX_sethighpass(int voice, int cutoff);                                // 0x001464c0
void MIX_settimemult(int voice, int ratio);                                 // 0x00146570

#endif // DRIVING_SOUND_SND_MIXER_H_
