#ifndef DRIVING_SOUND_SND_PLATFORM_H_
#define DRIVING_SOUND_SND_PLATFORM_H_

// EA's sound library, module F: the Xbox platform driver (docs/driving/sound.md 2.2, 2.3, 3.1, 4.6, 5.1, 8) - the
// device, the 180 pooled DirectSound buffers that play bank samples in place, the six speaker rings of the software
// mixer, the SND thread's 100 Hz loop, and the per-voice setters that turn a logical voice's volume, pitch, pan, fx
// send and filters into DirectSound or MIX calls. See Platform.cpp.

#include <stddef.h>
#include <stdint.h>

namespace SND {

// The render-mode bits (Voice::renderMode, the configured modes 0x420 and 0x24): which path a voice plays on. The
// code tests 0x410, 0x400 and 4; 0x20, set in both configured modes, is never tested here.
enum RenderModeBits : uint16_t {
    kRenderMainCpu = 0x0004,     // EA's software mixer (module G): voices 192..223
    kRenderMode10 = 0x0010,      // a range with no voices (192..192); tested with the hardware bit
    kRenderHardware = 0x0400,    // pooled DirectSound buffers: voices 0..191
};

// The two buffer pools (BufferNode::pool)
enum BufferPool {
    kPoolAdpcm = 0,              // Xbox ADPCM, 152 buffers
    kPoolPcm16 = 1,              // 16-bit PCM, 28 buffers
};

struct BufferNode;

// SNDPLATFORMVOICE (0x18), array at *0x00244c80 (NUM_VOICES of them)
struct PlatformVoice {
    int16_t gains[6];            // +0x00 per speaker (SNDI_aztospkrvol); the high byte indexes the volume table
    BufferNode *node;            // +0x0c the hardware buffer node playing it
    uint8_t playing;             // +0x10 1 playing, 0 paused (a pitch multiplier of 0)
    uint8_t pad11[3];
    int32_t looping;             // +0x14 the Play flag to resend on resume
};
static_assert(sizeof(PlatformVoice) == 0x18, "SNDPLATFORMVOICE is 0x18 bytes");

// A hardware buffer node (0x30; Ghidra SNDLINKNODE), 180 of them at *0x00244c44
struct BufferNode {
    BufferNode *next;            // +0x00 } SNDLINKI's (Ghidra's tail/head are the wrong way round)
    BufferNode *prev;            // +0x04 }
    void *buffer;                // +0x08 the DirectSound buffer
    int16_t platformVoice;       // +0x0c
    uint8_t pool;                // +0x0e 0 Xbox ADPCM, 1 PCM16
    uint8_t pad0f;
    uint8_t *memory;             // +0x10 the ring (packet voices; the master's node owns the allocation)
    uint32_t size;               // +0x14 ring bytes
    uint32_t writePos;           // +0x18 ring write position
    uint8_t *packet;             // +0x1c the current packet's channel data (0: none)
    int32_t player;              // +0x20 packet player handle, -1 for a bank sample
    int32_t packetBytes;         // +0x24 the packet's bytes per channel
    int32_t packetUsed;          // +0x28 bytes of it copied
    uint32_t silence;            // +0x2c bytes of silence written since the last packet
};
static_assert(sizeof(BufferNode) == 0x30, "the hardware buffer node is 0x30 bytes");

// SNDLINKLIST (0xc): LList_maybeFreeDsndBuffers[2] at 0x00244c60, Llist_MaybeActiveDsndBuffers[2] at 0x00244c48
struct BufferList {
    BufferNode *first;           // +0x00
    BufferNode *last;            // +0x04
    int32_t count;               // +0x08
};
static_assert(sizeof(BufferList) == 0xc, "a buffer list (SNDLINKLIST) is 0xc bytes");

// The Xbox DirectSound structures the driver fills
struct DsMixBinPair {
    uint32_t bin;
    int32_t volume;              // hundredths of a dB
};
struct DsMixBins {
    uint32_t count;
    DsMixBinPair *pairs;
};
struct DsFilterDesc {
    uint32_t mode;               // 1
    uint32_t q;
    uint32_t coefficients[4];
};
#pragma pack(push, 1)
struct DsWaveFormat {            // WAVEFORMATEX + the ADPCM samples-per-block word (0x14 cleared)
    uint16_t formatTag;          // +0x00 0x69 Xbox ADPCM, 1 PCM
    uint16_t channels;           // +0x02
    uint32_t samplesPerSec;      // +0x04
    uint32_t avgBytesPerSec;     // +0x08
    uint16_t blockAlign;         // +0x0c
    uint16_t bitsPerSample;      // +0x0e
    uint16_t cbSize;             // +0x10
    uint16_t samplesPerBlock;    // +0x12
};
#pragma pack(pop)
static_assert(sizeof(DsWaveFormat) == 0x14, "the wave format is 0x14 bytes here");
struct DsBufferDesc {            // 0x18 cleared
    uint32_t size;               // +0x00
    uint32_t flags;              // +0x04
    uint32_t bufferBytes;        // +0x08
    DsWaveFormat *format;        // +0x0c
    DsMixBins *mixBins;          // +0x10
    uint32_t inputMixBin;        // +0x14
};
static_assert(sizeof(DsBufferDesc) == 0x18, "the buffer description is 0x18 bytes here");

// DSI3DL2LISTENER (0x30): an I3DL2 reverb's listener properties
struct DsI3dl2Listener {
    int32_t room;                // +0x00 hundredths of a dB
    int32_t roomHF;              // +0x04
    float roomRolloffFactor;     // +0x08
    float decayTime;             // +0x0c seconds
    float decayHFRatio;          // +0x10
    int32_t reflections;         // +0x14
    float reflectionsDelay;      // +0x18
    int32_t reverb;              // +0x1c
    float reverbDelay;           // +0x20
    float diffusion;             // +0x24 percent
    float density;               // +0x28 percent
    float hfReference;           // +0x2c Hz
};
static_assert(sizeof(DsI3dl2Listener) == 0x30, "DSI3DL2LISTENER is 0x30 bytes");

// SNDPKTPLAY_start's format (only the rate and the sample representation are read here)
struct PacketFormat {
    uint16_t sampleRate;         // +0x00
    uint8_t channels;            // +0x02
    uint8_t sampleRep;           // +0x03 20 Xbox ADPCM
};

struct PatchHeader;

}  // namespace SND

// The driver's setters and queries (voice = logical voice index)
void* dsndCreateBufferAndMixBins(int pool);                                  // 0x0013d430
void FUN_0013d550(int pool);                                                 // 0x0013d550 borrow from the other pool
void SNDDRV_mixvoicefree(int mixVoice);                                      // 0x0013d5c0
void dsndMixInit(void);                                                      // 0x0013d5e0
void dsndMixStop(void);                                                      // 0x0013d7d0
void dsndMixProcess(void);                                                   // 0x0013d820
void SNDPLATFORM_getvoicerange(int mode, int *first, int *end);              // 0x0013d900
uint32_t __stdcall SNDDRV_thread(void *parameter);                           // 0x0013d980
int SNDPLATFORM_outputcaps(void);                                            // 0x0013da00
int SNDPLATFORM_outputset(void);                                             // 0x0013dae0
int SNDPLATFORM_init(void);                                                  // 0x0013dc50
int SNDPLATFORM_restore(void);                                               // 0x0013ddc0
int SNDPLATFORM_stop(int voice);                                             // 0x0013de50
void SNDPLATFORM_setvol(int voice);                                          // 0x0013df50
void SNDPLATFORM_set3dpos(int voice);                                        // 0x0013e0c0
int SNDPLATFORM_setpitch(int voice);                                         // 0x0013e320
int SNDPLATFORM_setfxlevel(int voice, int bus);                              // 0x00140070
int DirectSound_SetListenerRelated(void);                                    // 0x00140880
void SNDDRV_setfx(int path);                                                 // 0x00140a10
int SNDPLATFORM_fxinit(int bus, int path);                                   // 0x00140a80
void FUN_00142150(SND::BufferNode *node);                                    // 0x00142150 refill a packet voice's ring
int SNDDRV_getmastervoice(int mixVoice);                                     // 0x00142420
int SNDDRV_getsamplechan(int mixVoice);                                      // 0x00142460
int SNDPLATFORM_packetplay(int player, int voice, int timeMult, int distort, int lowpass, int highpass,
                           const SND::PacketFormat *format, uint8_t *const *data);   // 0x001424c0
int SNDPLATFORM_filteradd(int voice, int filter);                            // 0x001427d0
void SNDPLATFORM_lowpass(int voice, int cutoff);                             // 0x001429f0
int SNDPLATFORM_playtimbre(SND::PatchHeader *header, uint8_t *base, int voice, int timeMult, int distort,
                           int lowpass, int highpass);                        // 0x00142b10
void SNDPLATFORM_highpass(int voice, int cutoff);                            // 0x00144960
int SNDPLATFORM_timemult(int voice, int mult);                               // 0x001449c0

#endif // DRIVING_SOUND_SND_PLATFORM_H_
