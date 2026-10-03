#ifndef DRIVING_SOUND_SND_VOICES_H_
#define DRIVING_SOUND_SND_VOICES_H_

// EA's sound library, module B: the voice manager and the control API (docs/driving/sound.md 2.1, 3.3, 4.2). See
// Voices.cpp. The records the library shares between modules (the logical voice, the parsed patch header, the play
// options, the effects bus, the stream attributes) are declared here; Banks.h adds the bank's own.
//
// Plain data only (no member functions). "MW:" gives MW's SND 9 name (sndo.h, sndcmn.h) where its layout agrees.

#include <stddef.h>
#include <stdint.h>

namespace SND {

// An envelope segment (MW: ENVELOPE), the bank's { ticks, level } pairs
struct EnvSegment {
    int32_t ticks;               // +0x00 negative: forever
    int32_t level;               // +0x04 0..127
};

// SNDVOICEI: a logical voice (0x88), array at 0x00244f3c (224 of them, NUM_VOICES at 0x00244ed8)
struct Voice {
    int32_t handle;              // +0x00 index | generation; -1 on a slave voice
    int16_t platformVoices[6];   // +0x04 the sound's channels (the master's record lists them all)
    int16_t patch;               // +0x10
    int16_t bank;                // +0x12 (0xffff: a stream)
    int32_t frames;              // +0x14
    int32_t sustainEnd;          // +0x18 loop end
    uint16_t azimuth;            // +0x1c built-in azimuth + the play option's
    int16_t elevation;           // +0x1e
    uint16_t sampleRate;         // +0x20
    uint8_t sampleRep;           // +0x22 20 Xbox ADPCM, 10 EA-XA ...
    int8_t channels;             // +0x23
    uint16_t renderMode;         // +0x24 0x420 / 0x24 as configured: the chosen one
    uint8_t key;                 // +0x26 the sound's key (SNDBANKI_findfreekey); 0 = a sound of one voice
    uint8_t keyLast;             // +0x27 set on the last voice of a multi-timbre sound
    int16_t master;              // +0x28 master voice on a slave, -1 on the master
    uint8_t priority;            // +0x2a 0..100; 101 = streams
    uint8_t pad2b;
    uint32_t tick;               // +0x2c allocation tick (0x00244edc): the steal order
    int32_t fadeStep;            // +0x30 16.16 per tick
    int32_t fadeTarget;          // +0x34
    int32_t fade;                // +0x38 16.16 (iSNDcalcvol reads the high half)
    int32_t envStep;             // +0x3c
    int32_t env;                 // +0x40 16.16 (the high half)
    int32_t envTicks;            // +0x44 ticks left in the segment
    int8_t builtinVol;           // +0x48 patch volume x velocity / 127
    int8_t vol;                  // +0x49 the final 0..127
    uint16_t builtinAzimuth;     // +0x4a
    uint16_t channelAzimuth[6];  // +0x4c per-channel offsets
    uint8_t envCount;            // +0x58
    uint8_t envCurrent;          // +0x59
    uint8_t envRelease;          // +0x5a
    uint8_t progVol;             // +0x5b
    int8_t builtinFxLevel;       // +0x5c the patch's [0x13] } SNDI_calcfxlevel indexes these by bus past their
    int8_t fxLevel;              // +0x5d the play option's    } size: bus 1's are the next fields' bytes
    int16_t fxSend;              // +0x5e the product, x the bus's level >> 6
    int8_t bend;                 // +0x60 velocity / pitch bend byte
    uint8_t volLfoLength;        // +0x61
    uint8_t pitchLfoLength;      // +0x62
    uint8_t volLfoPos;           // +0x63
    uint8_t pitchLfoPos;         // +0x64
    uint8_t inUse;               // +0x65 1 playing, 2 the last of a multi-timbre sound, freed once
    uint16_t timeMult;           // +0x66
    EnvSegment *envTable;        // +0x68 in the bank
    int8_t *volTable;            // +0x6c volume scaling (indexed by the signed volume)
    int8_t *bendTable;           // +0x70
    int8_t *volLfo;              // +0x74
    int8_t *pitchLfo;            // +0x78
    int16_t pitchLfoDepth;       // +0x7c
    int16_t bendRange;           // +0x7e x 100
    int16_t detune;              // +0x80 cents
    uint16_t detuneLinear;       // +0x82 4.12 (0 = recompute)
    uint16_t pitchMult;          // +0x84 the programmed 4.12 multiplier
    uint16_t pitch;              // +0x86 the final one
};
static_assert(sizeof(Voice) == 0x88, "SNDVOICEI is 0x88 bytes");

// SNDIPATCHHEADER as SNDI_parsetimbre fills it (0xb8). Tag numbers in brackets.
struct PatchHeader {
    uint16_t detuneRandom;       // +0x00 [0x24] randrange(this) added to the detune once per play
    uint8_t platformVersion;     // +0x02 [0x80] (default 2)
    int8_t channels;             // +0x03 [0x82] (default 1)
    uint16_t tune;               // +0x04 [0x10] detune base
    uint16_t tuneRandom;         // +0x06 [0x11]
    int8_t velocityLow;          // +0x08 [0x01]
    int8_t velocityHigh;         // +0x09 [0x02]
    int8_t keyLow;               // +0x0a [0x03]
    int8_t keyHigh;              // +0x0b [0x04]
    int8_t priority;             // +0x0c [0x06]
    int8_t rootKey;              // +0x0d [0x07]
    uint8_t envRelease;          // +0x0e [0x08]
    uint8_t envCount;            // +0x0f [0x09]
    int8_t bendRange;            // +0x10 [0x0a]
    int8_t pan;                  // +0x11 [0x0c]
    int8_t panRandom;            // +0x12 [0x0d]
    uint8_t sampleRep;           // +0x13 [0xa0] (default 8)
    int8_t volume;               // +0x14 [0x0e]
    int8_t volumeRandom;         // +0x15 [0x0f]
    uint8_t fxLevel;             // +0x16 [0x13]
    int8_t userDataCount;        // +0x17 [0x14] occurrences
    uint16_t renderMode;         // +0x18 [0x8c]
    int8_t envStart;             // +0x1a [0x1c]
    uint8_t volLfoLength;        // +0x1b [0x1e]
    int8_t volLfoRandom;         // +0x1c [0x1f] the LFO starts at a random position modulo it (sign-extended)
    uint8_t pitchLfoLength;      // +0x1d [0x21]
    uint8_t pitchLfoRandom;      // +0x1e [0x23]
    uint8_t panMult;             // +0x1f [0x25] (default 1; MW: panmult)
    int8_t *volTable;            // +0x20 [0x12] data pointer + value
    int8_t *bendTable;           // +0x24 [0x17]
    EnvSegment *envTable;        // +0x28 [0x19] (default 0x001d9d28)
    int8_t *volLfo;              // +0x2c [0x1d]
    int8_t *pitchLfo;            // +0x30 [0x20]
    uint8_t *userData[4];        // +0x34 [0x14]
    int32_t userDataSize[4];     // +0x44
    uint16_t pitchLfoDepth;      // +0x54 [0x22]
    uint16_t sampleRate;         // +0x56 [0x84] (default 24000)
    int32_t frames;              // +0x58 [0x85]
    int32_t loopStart;           // +0x5c [0x86]
    int32_t loopEnd;             // +0x60 [0x87]
    uint8_t *sampleData;         // +0x64 [0x8a] its data pointer
    int32_t sampleOffsets[6];    // +0x68 [0x88] [0x89] [0x94] [0x95] [0xa2] [0xa3]
    int32_t tag1a;               // +0x80 [0x1a] (default -1), handed to MIX_playinit
    uint8_t *stretchData[6];     // +0x84 [0x98] [0x99] [0x9a] [0x9b] [0xa4] [0xa5] per channel: time-stretch data
    uint16_t stretchDataSizes[6];   // +0x9c (MW: ptimestretchdata, timestretchsize)
    uint16_t azimuth[6];         // +0xa8 default speaker azimuth + [0x9c] [0x9d] [0x9e] [0x9f] [0xa6] [0xa7]
    uint8_t *start;              // +0xb4 where the tags began
};
static_assert(sizeof(PatchHeader) == 0xb8, "SNDIPATCHHEADER is 0xb8 bytes here");

// SNDPLAYOPTS (0x18), defaults by SNDplaysetdef
struct PlayOpts {
    int8_t vol;                  // +0x00 0x7f
    int8_t bend;                 // +0x01 0x40 the pitch bend (the voice's bend byte)
    int8_t key;                  // +0x02 0x3c
    int8_t velocity;             // +0x03 0x7f
    uint8_t progVol;             // +0x04 0x7f (MW: drylevel)
    int8_t fxLevel;              // +0x05 0x7f
    uint8_t pad06[2];
    uint16_t azimuth;            // +0x08 0
    uint16_t elevation;          // +0x0a 0
    uint16_t pitchMult;          // +0x0c 0x1000
    uint16_t timeMult;           // +0x0e 0x1000
    uint16_t tempoMult;          // +0x10 0x1000
    uint16_t distort;            // +0x12 0       } passed on to SNDPLATFORM_playtimbre / packetplay, which do not
                                 //                 use distort (MW: pad2)
    uint16_t lowpass;            // +0x14 0xffff  } the cutoffs
    uint16_t highpass;           // +0x16 0       }
};
static_assert(sizeof(PlayOpts) == 0x18, "SNDPLAYOPTS is 0x18 bytes");

// An effects bus entry (0x14): FXBUS at 0x00244f44 (main-CPU path) and 0x00244f58 (hardware path), indexed by
// bus - the two paths' entries overlap from bus 1 on, as in the original.
struct FxBus {
    uint16_t mode;               // +0x00
    int8_t level;                // +0x02 master level
    uint8_t pad03;
    int32_t delay;               // +0x04
    int32_t feedback;            // +0x08
    uint8_t pad0c[8];
};
static_assert(sizeof(FxBus) == 0x14, "an FXBUS entry is 0x14 bytes");

// The stream attributes SNDI_patchtohdr fills (0x68, MW: SNDSAMPLEATTR), defaults by SND_attrsetdef
struct Attributes {
    int16_t detune;              // +0x00 0
    uint8_t priority;            // +0x02 [0x06] 0
    uint8_t vol;                 // +0x03 0x7f
    uint8_t pan;                 // +0x04 0x40
    uint8_t fxLevel;             // +0x05 [0x13] 0
    uint8_t bendRange;           // +0x06 [0x0a] 0
    uint8_t platformVersion;     // +0x07 [0x80] 2
    uint16_t renderMode;         // +0x08 [0x8c]
    uint8_t pad0a[2];
    uint16_t azimuth[6];         // +0x0c
    uint8_t *stretchData[6];     // +0x18 [0x98] [0x99] [0x9a] [0x9b] [0xa4] [0xa5] copies in the sound heap
    int32_t stretchDataSizes[6];    // +0x30 (MW: ptsdata, tsdatasize)
    uint8_t *userData[4];        // +0x48 [0x14]
    int32_t userDataSize[4];     // +0x58
};
static_assert(sizeof(Attributes) == 0x68, "the stream attributes are 0x68 bytes");

}  // namespace SND

// The control API: handle in, the voice index (or a negative error) out
int SNDplaysetdef(SND::PlayOpts *opts);                                      // 0x0013c5c0
int SNDstop(int handle);                                                     // 0x0013c900
int SNDfxlevel(int handle, int bus, int level);                              // 0x0013c960
int SND3dpos(int handle, int azimuth, int elevation);                        // 0x0013c9f0
int SNDpitchmult(int handle, int mult);                                      // 0x0013caa0
int SNDvol(int handle, int vol);                                             // 0x0013cb40
int SNDover(int handle);                                                     // 0x0013cc00
SND::FxBus* SNDCTRLI_getfxbus(int bus, int renderMode);                      // 0x0013cc80
int SNDfxmasterlevel(int bus, int level);                                    // 0x0013ccc0
int SNDfxinitbus(int bus, int level, int mode, int delay, int feedback);     // 0x0013cd50
int SNDautovol(int handle, int ticks, int vol);                              // 0x0013e7d0
int SNDCTRL_filteradd(int handle, int filter);                               // 0x0013e860
int SNDCTRL_lowpass(int handle, int cutoff);                                 // 0x0013fa80

// The voice manager
void iSNDserve(void);                                                        // 0x0013e530
void iSNDcalcvol(int voice);                                                 // 0x0013e5d0
int iSNDdetunetolinear(int cents);                                           // 0x0013e660
void iSNDcalcpitch(int voice);                                               // 0x0013e700
int iSNDpatchkey(int voice, int *iterator);                                  // 0x0013fae0
int SNDVOICEI_alloc(int count, int priority, int *handle, int first, int end);  // 0x0013fb70
void SNDVOICEI_free(int voice);                                              // 0x0013fef0
int SNDVOICEI_get(int handle);                                               // 0x00140020
void SNDI_calcfxlevel(int bus, int voice);                                   // 0x001401b0
void SNDI_precalcaztospkrvol(void);                                          // 0x00141070
void SNDI_aztospkrvol(int azimuth, int16_t *gains);                          // 0x00141230
int SNDI_validrendermode(int *index, SND::PatchHeader *header);              // 0x00142830
int SND_attrsetdef(SND::Attributes *attributes);                             // 0x00142970
int SNDI_pantoazimuth(int pan);                                              // 0x00142e90

#endif // DRIVING_SOUND_SND_VOICES_H_
