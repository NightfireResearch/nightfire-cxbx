#ifndef DRIVING_SOUND_SND_REVERB_H_
#define DRIVING_SOUND_SND_REVERB_H_

// EA's software reverb on the mixer's FX send (docs/driving/sound.md 2.4, 3.6, 4.7): the standard network of
// prime-length allpass taps (MIX_initreverb, SNDMIXI_fxadd, MIXI_reverbblock) and the "fx2" node network
// (SNDMIXI_initfx2/initfx, the never-made process 0x001437e0) with its building blocks. See Reverb.cpp.

#include "Mixer.h"
#include "Filters.h"

#include <stddef.h>
#include <stdint.h>

namespace SND {

// The standard reverb's description (sound.md 3.6): byte 2 the mode (0 standard, 10 fx2), bytes 4..6 the three
// groups' tap counts, byte 7 the damping, then per tap:
struct ReverbTapDesc {
    uint8_t milliseconds[2];         // +0 the delay, big-endian
    int8_t gain;                     // +2 the allpass gain, / 127
    int8_t feedback;                 // +3 the feedback low pass's keep, / 127
};
static_assert(sizeof(ReverbTapDesc) == 4, "a tap's description is 4 bytes");

// The fx2 node types (a node entry's first byte)
enum Fx2NodeType : uint8_t {
    kFx2Source = 0,                  // reads sndfx (module H's SOURCE)
    kFx2LowpassRC = 1,               // module H's RC low pass
    kFx2FirLowpass = 2,              // module H's FIR, design kind 2 (SFilterFIR8)
    kFx2FirHighpass = 3,             // module H's 8-tap FIR high pass
    kFx2FirBandpass = 4,             // the FIR with two corners, design kind 4
    kFx2Resonator = 5,
    kFx2Gain = 6,
    kFx2Allpass = 7,
    kFx2Sum = 8,
    kFx2Tee = 9,
};

// fx2 node type 9: a tee - pulls its input once and serves the copy to a second reader (0x28)
struct FxTeeNode : SFilterNode {     // process 0x00144bc0, restore 0x00144ca0
    float *cache;                    // +0x1c
    int32_t capacity;                // +0x20 frames
    uint16_t served;                 // +0x24 the copy was handed out: pull again next time
    uint16_t fetch;                  // +0x26 pull upstream on the next call
};
static_assert(sizeof(FxTeeNode) == 0x28, "the tee node is 0x28 bytes");

// fx2 node type 8: the sum of its two inputs (0x24)
struct FxSumNode : SFilterNode {     // process 0x00144d00, restore 0x00144ca0
    float *buffer;                   // +0x1c the second input
    int32_t capacity;                // +0x20 frames
};
static_assert(sizeof(FxSumNode) == 0x24, "the sum node is 0x24 bytes");

// fx2 node type 7: an allpass delay (0x34)
struct FxAllpassNode : SFilterNode {   // process 0x00144f30, restore 0x00144fc0
    float *buffer;                   // +0x1c 16-byte aligned
    void *memory;                    // +0x20 the allocation
    int32_t position;                // +0x24
    int32_t length;                  // +0x28 a multiple of 16
    float gain;                      // +0x2c
    float negGain;                   // +0x30
};
static_assert(sizeof(FxAllpassNode) == 0x34, "the allpass node is 0x34 bytes");

// fx2 node type 6: a gain (0x20)
struct FxGainNode : SFilterNode {    // process 0x001450b0
    float gain;                      // +0x1c
};
static_assert(sizeof(FxGainNode) == 0x20, "the gain node is 0x20 bytes");

// fx2 node type 5: a two-pole resonator (0x40)
struct FxResonatorNode : SFilterNode {   // process 0x00145390
    int32_t rate;                    // +0x1c
    float frequency;                 // +0x20
    float bandwidth;                 // +0x24
    float radius;                    // +0x28 r = 1 - pi x bandwidth / rate
    float b1;                        // +0x2c cos(w) x 2r / (1 + r^2): y += 2 b1 r y1 ... as the code has it
    float a0;                        // +0x30 input gain
    float unknown34;                 // +0x34
    float y1;                        // +0x38
    float y2;                        // +0x3c
};
static_assert(sizeof(FxResonatorNode) == 0x40, "the resonator node is 0x40 bytes");

// fx2 node types 2 and 4 are module H's FIR node, SND::SFilterFIR8 (Filters.h): process 0x001455f0 (type 2),
// module H's SFILTER_hpfFIR8 (type 4)

}  // namespace SND

void SNDMIXI_restorefx2(void);                                              // 0x00142ee0
void SNDMIXI_initfx(int rate);                                              // 0x00142fc0
void SNDMIXI_initfx2(int rate, const uint8_t *description);                 // 0x001433a0
void MIX_restorereverb(void);                                               // 0x001434b0
void MIX_initreverb(int rate, const uint8_t *description);                  // 0x00143510
void SNDMIXI_fxinit(const uint8_t *description);                            // 0x00143780
void FUN_001437e0(int frames);                                              // 0x001437e0 never made: the fx2 hook
void SNDMIXI_fxadd(int frames);                                             // 0x00143970
int FUN_00144bc0(SND::FxTeeNode *node, int frames, float *scratch, float *out, int requester);   // 0x00144bc0
void FUN_00144ca0(SND::FxSumNode *node);                                    // 0x00144ca0 never made: restore
int FUN_00144cc0(SND::FxTeeNode *node);                                     // 0x00144cc0
int FUN_00144d00(SND::FxSumNode *node, int frames, float *scratch, float *out, int requester);   // 0x00144d00 never made
int FUN_00144e20(SND::FxSumNode *node);                                     // 0x00144e20
void FUN_00144e50(SND::FxAllpassNode *node, int count, const float *in, float *out);              // 0x00144e50
int FUN_00144f30(SND::FxAllpassNode *node, int frames, float *scratch, float *out, int requester);   // 0x00144f30
void FUN_00144fc0(SND::FxAllpassNode *node);                                // 0x00144fc0
int FUN_00144ff0(SND::FxAllpassNode *node);                                 // 0x00144ff0
void FUN_00145020(SND::FxAllpassNode *node, const int *params);             // 0x00145020
int FUN_001450b0(SND::FxGainNode *node, int frames, float *scratch, float *out, int requester);   // 0x001450b0
int FUN_00145160(SND::FxGainNode *node);                                    // 0x00145160
void FUN_00145190(SND::FxGainNode *node, const int *params);                // 0x00145190
void FUN_001451b0(SND::FxResonatorNode *node, int count, const float *in, float *out);            // 0x001451b0
int FUN_00145390(SND::FxResonatorNode *node, int frames, float *scratch, float *out, int requester);   // 0x00145390
int FUN_001453e0(SND::FxResonatorNode *node);                               // 0x001453e0
void FUN_00145410(SND::FxResonatorNode *node, const int *params);           // 0x00145410
void FUN_00145500(SND::SFilterFIR8 *node, const int *params);               // 0x00145500
int FUN_001455f0(SND::SFilterFIR8 *node, int frames, float *scratch, float *out, int requester);   // 0x001455f0
int FUN_00145640(SND::SFilterFIR8 *node);                                   // 0x00145640
void FUN_00145670(SND::SFilterFIR8 *node, const int *params);               // 0x00145670
void MIXI_reverbblock(SND::ReverbTap *tap, int count, const float *in, float *out);               // 0x00145760
int FUN_001465a0(int rate, int milliseconds);                               // 0x001465a0
double FUN_001465c0(float x);                                               // 0x001465c0 sqrt(1 + x), series
double FUN_00146640(float x);                                               // 0x00146640 cos(x), series
int findprime(int rate, int milliseconds);                                  // 0x0014a260

#endif // DRIVING_SOUND_SND_REVERB_H_
