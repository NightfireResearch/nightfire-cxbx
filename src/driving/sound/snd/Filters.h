#ifndef DRIVING_SOUND_SND_FILTERS_H_
#define DRIVING_SOUND_SND_FILTERS_H_

// EA's SFILTER graph, the live part (docs/driving/sound.md 3.6, 4.8): the node list, the EA-XA packet unpacker,
// the resampler and its SSE kernel, the one-pole low pass, the 8-tap FIR high pass and its design, the SOURCE node
// and the float -> 16-bit output stage. See Filters.cpp. The data-dead unpackers and the time stretch are in
// FiltersUnused.h.

#include <stddef.h>
#include <stdint.h>

namespace SND {

struct SFilterNode;
struct UnpackInfo;

// A node's process function: pull 'frames' frames, leave them in 'out'. 'scratch' is a buffer of the same size the
// node may use; a node asks its upstream node for frames with (out, scratch) swapped, so what the upstream node
// produces lands in this node's scratch. The answer is the frames produced, <= 0 at the end of the data.
typedef int (*SFilterProcess)(SFilterNode *node, int frames, float *scratch, float *out, int requester);
// Frees what a node owns before the node itself is freed (0: nothing to free)
typedef void (*SFilterRestore)(SFilterNode *node);
// A description's init (SFILTER_add): called on the newly allocated node with the description's param
typedef void (*SFilterInit)(SFilterNode *node, uint32_t param, uint32_t arg);
// A bank unpacker's frame getter, which its init hands back in UnpackInfo::getFrame
typedef int (*SFilterGetFrame)(SFilterNode *node);
// An unpacker's init (MIX_playinit's table, SndMix.unpackerInit)
typedef void (*SFilterUnpackInit)(SFilterNode *node, UnpackInfo *info);

// The node head every SFILTER node, the mixer's output stages and the reverb's blocks start with. The function
// slots hold the originals' addresses (which jump to our ports), so a node's bytes are the original's.
struct SFilterNode {                 // 0x1c (MW: SFILTERNODE)
    SFilterProcess process;          // +0x00
    SFilterRestore restore;          // +0x04 0: nothing to free
    SFilterNode *input;              // +0x08 upstream node (input 1)
    SFilterNode *input2;             // +0x0c upstream node (input 2, the FX sum node)
    SFilterNode *output;             // +0x10 downstream node (output 1); the list's previous node
    SFilterNode *output2;            // +0x14 downstream node (output 2)
    uint16_t priority;               // +0x18 the list is sorted by it, lowest first: 0xf0 unpacker, 200 time
                                     //       stretch, 0xa0 resampler, 0x50 high pass, 0x28 low pass
    uint8_t requester;               // +0x1a passed upstream as the fifth argument (the connection's slot)
    uint8_t flags;                   // +0x1b
};
static_assert(sizeof(SFilterNode) == 0x1c, "SFilterNode");

// What SFILTER_add takes (SNDPLATFORM_filteradd's filter description)
struct SFilterDesc {
    uint32_t param;                  // +0x00 passed to init
    uint32_t size;                   // +0x04 the node's size
    uint16_t priority;               // +0x08
    uint16_t pad0a;
    SFilterInit init;                // +0x0c
    SFilterProcess process;          // +0x10
    SFilterRestore restore;          // +0x14
};
static_assert(sizeof(SFilterDesc) == 0x18, "SFilterDesc");

struct SFilterLPFRC {                // 0x28, SFILTER_lpfRC: y = y * a + b * x
    SFilterNode node;
    float y;                         // +0x1c the last output
    float a;                         // +0x20 feedback
    float b;                         // +0x24 input gain
};
static_assert(sizeof(SFilterLPFRC) == 0x28, "SFilterLPFRC");

// The FIR state the design and the filter take (SFilterFIR8 from +0x1c)
struct FirState {
    float history[8];                // +0x00 [0] the newest input
    float coef[5];                   // +0x20 symmetric taps: [0] outermost .. [4] centre
    float cutoff;                    // +0x34 (design mode 3/4: the high pass edge, clamped to 0.8)
    float cutoff2;                   // +0x38 (design mode 2/4: the low pass edge)
};
static_assert(sizeof(FirState) == 0x3c, "FirState");

// SFILTER_hpfFIR8's node, and the reverb's FIR blocks (fx2 types 2 and 4, Reverb.cpp)
struct SFilterFIR8 {                 // 0x58
    SFilterNode node;
    FirState fir;                    // +0x1c
};
static_assert(sizeof(SFilterFIR8) == 0x58, "SFilterFIR8");

// The resampler kernel: cdecl, seven stack arguments (FUN_001462b0)
typedef void (*RsfKernel)(int count, const float *src, float *dst, int *index, uint32_t *fraction, int stepInt,
                          uint32_t stepFraction);

struct SFilterRSF {                  // 0x3c, SFILTER_rsf
    SFilterNode node;
    uint32_t pitch;                  // +0x1c 16.16, 0x10000 passes through
    uint32_t phase;                  // +0x20 the fraction (16 bits; the high half is cleared after each call)
    RsfKernel kernel;                // +0x24 FUN_001462b0, the original's address
    int16_t primed;                  // +0x28 the history is valid
    int16_t offset;                  // +0x2a history offset (always 0: only rsfinit writes it)
    float history[4];                // +0x2c the last four input samples
};
static_assert(sizeof(SFilterRSF) == 0x3c, "SFilterRSF");

struct SFilterSource {               // 0x20, PS2: SFILTER_src
    SFilterNode node;
    const float *data;               // +0x1c the next sample
};
static_assert(sizeof(SFilterSource) == 0x20, "SFilterSource");

// What the unpacker inits take (MIX_playinit's description of the sample, built from its arguments)
struct UnpackInfo {
    const void *data;                // +0x00 the sample data (bank unpackers)
    uint32_t unknown04;              // +0x04
    uint32_t unknown08;              // +0x08
    int frames;                      // +0x0c
    int loopStart;                   // +0x10
    int loopEnd;                     // +0x14
    uint32_t flag;                   // +0x18 (PCM16: decode; the packet variant reads its low byte)
    int voice;                       // +0x1c the SND voice
    uint32_t quality;                // +0x20 MixQuality
    SFilterGetFrame getFrame;        // +0x24 written by the bank unpackers' inits (the original's address)
};
static_assert(sizeof(UnpackInfo) == 0x28, "UnpackInfo");

// SND::CEAXABLKDecf (module I), only its size and address matter here
struct CEAXABLKDecf;

struct SFilterXAPF {                 // 0x38, SFILTER_unpackxapf: EA-XA from stream packets
    SFilterNode node;
    CEAXABLKDecf *decoder;           // +0x1c
    const int16_t *packet;           // +0x20 the current packet's data (after the two history shorts)
    int packetFrames;                // +0x24 the frames in it (SNDPKTPLAYI_get's)
    int position;                    // +0x28 frames decoded from it
    int packetPlayer;                // +0x2c SNDPKTPLAYI handle
    int pending;                     // +0x30 frames to report with SNDPKTPLAYI_freeframes
    uint8_t channel;                 // +0x34 the sample channel
    uint8_t pad35[3];
};
static_assert(sizeof(SFilterXAPF) == 0x38, "SFilterXAPF");

}  // namespace SND

// The node list (MIX +0x40) and the connections
SND::SFilterNode* SFILTER_add(int voice, uint32_t arg, const SND::SFilterDesc *desc);         // 0x00144a30
SND::SFilterNode* SFILTER_addtofilterlist(SND::SFilterNode **head, SND::SFilterNode *node);   // 0x00144460
SND::SFilterNode* SFILTER_remove(SND::SFilterNode **head, SND::SFilterNode *node);            // 0x001444a0
int SFILTER_connect(SND::SFilterNode *from, SND::SFilterNode *to, int fromSlot, int toSlot);  // 0x001444f0

// One-pole low pass
int SFILTER_lpfRC(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);  // 0x001436c0
int SFILTER_createLPFRC(SND::SFilterLPFRC *node);                                                   // 0x00143710
void SFILTER_modifyLPFRC(SND::SFilterLPFRC *node, const int *params);                               // 0x00143740
void FUN_00145720(float *state, int frames, const float *in, float *out);                           // 0x00145720

// 8-tap FIR high pass
int SFILTER_hpfFIR8(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester); // 0x00145560
int SFILTER_createHPFFIR8(SND::SFilterFIR8 *node);                                                  // 0x001454d0
void SFILTER_modifyHPFFIR8(SND::SFilterFIR8 *node, const int *params);                              // 0x001455b0
void FUN_001466c0(SND::FirState *state);                                                            // 0x001466c0
void FUN_001466e0(SND::FirState *state, int frames, const float *in, float *out);                   // 0x001466e0
void FUN_00146930(SND::FirState *state, int mode);                                                  // 0x00146930
double FUN_0014a2e0(float x);                                                                       // 0x0014a2e0

// Resampler
int SFILTER_rsf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);     // 0x00144710
void SFILTER_rsfinit(SND::SFilterRSF *node, uint32_t param, int noKernel);                          // 0x00144920
void SFILTER_rsfsetpitch(SND::SFilterRSF *node, uint32_t pitch);                                    // 0x00144700
void FUN_001462b0(int count, const float *src, float *dst, int *index, uint32_t *fraction, int stepInt,
                  uint32_t stepFraction);                                                           // 0x001462b0

// SOURCE
int SFILTER_src(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);     // 0x001456b0
void SFILTER_initSOURCE(SND::SFilterSource *node, const float *data);                               // 0x001456e0
int SFILTER_createSOURCE(SND::SFilterSource *node);                                                 // 0x00145700

// Output stage
int SFILTER_ft24_32(SND::SFilterNode *node, int frames, float *scratch, int16_t *out, int requester); // 0x00144590
void SFILTER_ft16init(SND::SFilterNode *node);                                                        // 0x00144610

// EA-XA from stream packets
int SFILTER_unpackxapf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester); // 0x00145ab0
void SFILTER_unpackxapfrestore(SND::SFilterXAPF *node);                                                // 0x00145bf0
void SFILTER_unpackxapfinit(SND::SFilterXAPF *node, const SND::UnpackInfo *info);                      // 0x00145c10

#endif // DRIVING_SOUND_SND_FILTERS_H_
