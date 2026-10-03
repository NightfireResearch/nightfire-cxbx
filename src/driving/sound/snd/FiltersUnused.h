#ifndef DRIVING_SOUND_SND_FILTERSUNUSED_H_
#define DRIVING_SOUND_SND_FILTERSUNUSED_H_

// The SFILTER code no data on the disc reaches (docs/driving/sound.md 3.8): the bank EA-XA, MicroTalk and PCM16
// unpackers and the time stretch. Ported from the listing, provisionally: each says so once when it first runs
// (SND_UNTESTED). See FiltersUnused.cpp.

#include "Filters.h"

#include <stdint.h>
#include <stdio.h>

// The warning beside a provisional port (the pattern of eagl/anim/AnimUntested.h). Guarded: other sound modules
// may define the same macro.
#ifndef SND_UNTESTED
inline void SndFiltersUntested(const char *what) {
    printf("[snd] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define SND_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            SndFiltersUntested(what); \
        } \
    } while (0)
#endif

namespace SND {

struct SFilterXAF {                  // 0x2c, SFILTER_unpackxaf: EA-XA bank sample, one shot
    SFilterNode node;
    CEAXABLKDecf *decoder;           // +0x1c
    const uint8_t *data;             // +0x20
    int frames;                      // +0x24
    int position;                    // +0x28
};
static_assert(sizeof(SFilterXAF) == 0x2c, "SFilterXAF");

struct SFilterXALF {                 // 0x44, SFILTER_unpackxalf: EA-XA bank sample, looping
    SFilterNode node;
    CEAXABLKDecf *decoder;           // +0x1c
    const uint8_t *data;             // +0x20
    int frames;                      // +0x24
    int position;                    // +0x28
    int stateSaved;                  // +0x2c the predictor state at the loop start is kept
    int fed;                         // +0x30 the first part has been fed to the decoder
    int loopStart;                   // +0x34
    int loopEnd;                     // +0x38
    float loopState[2];              // +0x3c the predictor state at the loop start
};
static_assert(sizeof(SFilterXALF) == 0x44, "SFilterXALF");

// MicroTalk nodes (0xd74 and smaller): a header, the decoder's state, a 432-sample buffer whose end is at +0xd70
// (packet variant) or +0xd68 (bank variant). Reached as bytes.

struct SFilterPF {                   // 0x34, SFILTER_unpackpf: PCM16 from stream packets
    SFilterNode node;
    const int16_t *packet;           // +0x1c
    int packetFrames;                // +0x20
    int position;                    // +0x24
    int packetPlayer;                // +0x28
    int pending;                     // +0x2c
    uint8_t decode;                  // +0x30 0: leave the output alone
    uint8_t channel;                 // +0x31
    uint8_t pad32[2];
};
static_assert(sizeof(SFilterPF) == 0x34, "SFilterPF");

struct SFilterLF {                   // 0x30, SFILTER_unpacklf: PCM16 bank sample, looping
    SFilterNode node;
    const int16_t *data;             // +0x1c
    uint32_t position;               // +0x20
    uint32_t loopStart;              // +0x24
    uint32_t loopEnd;                // +0x28 the last frame of the loop
    uint32_t decode;                 // +0x2c
};
static_assert(sizeof(SFilterLF) == 0x30, "SFilterLF");

struct SFilterF {                    // 0x2c, SFILTER_unpackf: PCM16 bank sample, one shot
    SFilterNode node;
    const int16_t *data;             // +0x1c
    uint32_t position;               // +0x20
    uint32_t frames;                 // +0x24
    uint32_t decode;                 // +0x28
};
static_assert(sizeof(SFilterF) == 0x2c, "SFilterF");

struct SFilterStretch {              // SFILTER_timestretch: 0x1828 (MIX_playinit's), 0x838 + 1020 output floats
    SFilterNode node;
    const uint8_t *data;             // +0x1c the patch's stretch data (tag 0x98 blob + 6): one byte per segment
    int packetPlayer;                // +0x20 -1 for a bank voice
    float ratio;                     // +0x24 0.5..2
    float error;                     // +0x28 the accumulated timing error
    int window;                      // +0x2c twice the blob's byte 1
    int remaining;                   // +0x30 input frames left (the blob's 4-byte count)
    int pending;                     // +0x34 a window of input kept in 'held' for the next crossfade
    int available;                   // +0x38 frames ready in the output buffer
    int readPosition;                // +0x3c the next of them
    float held[510];                 // +0x40
    // +0x838 the output buffer (up to two windows)
};
// The inits' results (EAX: the decoder, Feed's answer, initmut's ...) are left out: MIX_playinit and SFILTER_add,
// their only callers, ignore them.
static_assert(sizeof(SFilterStretch) == 0x838, "SFilterStretch");

}  // namespace SND

// EA-XA from bank samples
int SFILTER_unpackxaf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);   // 0x00145ed0
void SFILTER_unpackxafinit(SND::SFilterXAF *node, SND::UnpackInfo *info);                                // 0x00145f50
int SFILTER_unpackgetframexaf(SND::SFilterXAF *node);                                                    // 0x00145f40
int SFILTER_unpackxalf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);  // 0x00145cc0
void SFILTER_unpackxalfinit(SND::SFilterXALF *node, SND::UnpackInfo *info);                              // 0x00145e40

// MicroTalk
int SFILTER_unpackmtpf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);  // 0x00145830
void SFILTER_unpackmtpfinit(SND::SFilterNode *node, const SND::UnpackInfo *info);                         // 0x00145970
int SFILTER_unpackmtf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);   // 0x001459c0
void SFILTER_unpackmtfinit(SND::SFilterNode *node, SND::UnpackInfo *info);                                // 0x00145a70

// PCM16
int SFILTER_unpackpf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);    // 0x00146000
void SFILTER_unpackpfinit(SND::SFilterPF *node, const SND::UnpackInfo *info);                             // 0x001460e0
int SFILTER_unpacklf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);    // 0x00146130
void SFILTER_unpacklfinit(SND::SFilterLF *node, SND::UnpackInfo *info);                                   // 0x001461a0
int SFILTER_unpackfgetframe_unpacklfgetframe(SND::SFilterNode *node);                                     // 0x001461e0
int SFILTER_unpackf(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester);     // 0x001461f0
void SFILTER_unpackfinit(SND::SFilterF *node, SND::UnpackInfo *info);                                     // 0x00146280

// Time stretch
int SFILTER_timestretch(SND::SFilterNode *node, int frames, float *scratch, float *out, int requester); // 0x00144340
int SFILTER_timestretchinit(SND::SFilterStretch *node, const uint8_t *blob, int voice);                   // 0x001443f0
void SFILTER_timestretchsetratio(SND::SFilterStretch *node, int ratio);                                   // 0x00144300
int stretch(SND::SFilterStretch *node, int frames, float *in, float *out);                                // 0x001441a0
int stretchframesneeded(SND::SFilterStretch *node, int frames);                                           // 0x001440b0
// The register-argument helpers: adaptors under Ghidra's names (AUTOLTCG), the cores beside them in the .cpp
void FUN_00143be0();   // EAX = shift, (a, b, out, window): the crossfade
void FUN_00143e10();   // EBX = &data, ESI = &error, (window, ratio): the next segment's shift
void FUN_00143f20();   // EBX = node, (&in, shift): one segment into the output buffer
void FUN_00144050();   // EDX = node, (&frames, &out): drain the output buffer

// The cores, for the shadow test
void SndStretch_Crossfade(int shift, const float *a, const float *b, float *out, int window);
int SndStretch_NextShift(const uint8_t **data, float *error, int window, float ratio);
int SndStretch_Segment(SND::SFilterStretch *node, const float **in, int shift);
int SndStretch_Drain(SND::SFilterStretch *node, int *frames, float **out);

#endif // DRIVING_SOUND_SND_FILTERSUNUSED_H_
