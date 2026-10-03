#ifndef DRIVING_SOUND_SND_MIXER_H_
#define DRIVING_SOUND_SND_MIXER_H_

// EA's software mixer (docs/driving/sound.md 2.4, 3.6, 4.7): the MIX voices, the slice loop, the gain ramps and
// mixc. See Mixer.cpp. The software reverb and the FX building blocks are in Reverb.h.

#include <stddef.h>
#include <stdint.h>

namespace SND {

// A filter node's process: pull 'frames' from upstream, result in 'out' (the fourth argument), 'scratch' free to
// use; the count made, 0 for none, negative at the end of the voice.
typedef int (*MixProcessFn)(void *node, int frames, float *scratch, float *out, int requester);
typedef void (*MixRestoreFn)(void *node);

// SFILTERNODE's common head (0x1c, sound.md 1.4). The mixer and FX blocks are ours; the filters of module H keep
// their own definition of the same layout.
struct MixFilterNode {
    MixProcessFn process;            // +0x00
    MixRestoreFn restore;            // +0x04 0: nothing to free
    MixFilterNode *upstream;         // +0x08 input 1
    MixFilterNode *upstream2;        // +0x0c input 2 (the FX sum node)
    MixFilterNode *downstream;       // +0x10 written by SFILTER_connect
    uint32_t field14;                // +0x14
    uint16_t priority;               // +0x18 the chain's order: 0xf0 unpacker, 200 time stretch, 0xa0 resampler,
                                     //       0x50 high pass, 0x28 low pass
    uint8_t requester;               // +0x1a passed upstream as the fifth argument
    uint8_t field1b;                 // +0x1b
};
static_assert(sizeof(MixFilterNode) == 0x1c, "a filter node head is 0x1c bytes");

// A MIX voice (Ghidra MIX, 0x60), 32 of them at *0x00245be4.
struct MixVoice {
    uint8_t state;                   // +0x00 0 free, 1 initialised, 2 playing
    uint8_t gainsChanged;            // +0x01 ramp to the targets on the next slice
    uint8_t pad02[2];
    float dry[6];                    // +0x04 current per-speaker gains
    float dryTarget[6];              // +0x1c
    float fx;                        // +0x34 current fx send
    float fxTarget;                  // +0x38
    float lastSample;                // +0x3c the voice's last output sample (for the ramp to zero on a stop)
    MixFilterNode *chain;            // +0x40 the filter chain's head (pulled by the mixer)
    uint32_t field44;                // +0x44 the unpacker init's tenth parameter on return
    MixFilterNode *unpacker;         // +0x48
    MixFilterNode *stretch;          // +0x4c time stretch
    MixFilterNode *resampler;        // +0x50
    uint32_t field54;                // +0x54
    MixFilterNode *lowpass;          // +0x58
    MixFilterNode *highpass;         // +0x5c
};
static_assert(sizeof(MixVoice) == 0x60, "a MIX voice is 0x60 bytes");

// MIX_create's argument
struct MixCreateParams {
    uint32_t rate;                   // +0 -> 0x00245990
    uint32_t counts;                 // +4 -> 0x00245994: byte 0 voices, byte 1 output channels
    void (*voiceFree)(int voice);    // +8 -> 0x00245998
};

void CODASetNew(void *allocate);                                             // 0x001446e0
void CODASetDelete(void *release);                                           // 0x001446f0

}  // namespace SND

void MIXI_interpolateto0(float *gain, float *buffer);                       // 0x001413e0
void MIXI_interpolatemix(float from, float to, float *in, float *out);      // 0x001414d0
void SNDMIX_setmasterlowpass(float cutoff);                                 // 0x00141630
int SNDMIXI_volramp(SND::MixVoice *voice);                                  // 0x00141710
void MIX_destroy(void);                                                     // 0x00141880
void MIX_playinit(int voice, int sampleRep, int kind, int p3, int p4, int stretchData, int p6, int p7, int p8,
                  int loop, int p10, int p11, int requester);               // 0x00141910
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
int MIX_settimemult(int voice, int ratio);                                  // 0x00146570

#endif // DRIVING_SOUND_SND_MIXER_H_
