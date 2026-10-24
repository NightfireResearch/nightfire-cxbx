#ifndef DRIVING_SOUND_SND_DECODEUNUSED_H_
#define DRIVING_SOUND_SND_DECODEUNUSED_H_

// The sound library's decoders nothing on the disc reaches (docs/driving/sound.md 3.8, 4.9): EA MicroTalk
// (initmut, decodemut, readsamples and the two filters under them) and 16-bit PCM (decode16x87). Ported from the
// listing, provisional and untested against shipped data. See DecodeUnused.cpp.

#include <stdint.h>
#include "../../../common/xbeClass.h"   // LAYOUT_32

namespace SND {

// A MicroTalk decoder's state (the unpackers keep it at +0x28 or +0x30 of their 0xd74-byte node). The bit reader
// is LSB first: `bits` holds `count` unread bits, topped up a byte at a time whenever fewer than 8 remain.
struct MutState {                      // 0xd40
    const uint8_t *ptr;                // +0x000 the next byte of the stream
    uint32_t bits;                     // +0x004
    int32_t count;                     // +0x008
    int32_t threshold;                 // +0x00c 32 - the header's first 4-bit field: picks readsamples' mode
    float gainTable[64];               // +0x010 the fixed-codebook gains: g0 x step^i (initmut)
    float coefs[12];                   // +0x110 the reflection coefficients, interpolated per subframe
    float synth[12];                   // +0x140 the synthesis filter's memory (FUN_00146f00)
    float signal[756];                 // +0x170 324 samples of excitation history, then the frame's 432 outputs
};
LAYOUT_32(sizeof(MutState) == 0xd40, "a MicroTalk state is 0xd40 bytes");

// The MicroTalk decoder's tables (in the original's order): the reflection coefficients' levels - the first
// four's 64, the other eight's 32 being levels 16-47 of the same table - and the fixed codebook's two-state
// Huffman code: the code for each state and next 8 bits, and each code's next state, length and sample
struct MutCode {                       // 12 bytes
    int32_t next;                      // +0 the state for the next code
    int32_t length;                    // +4 bits the code takes
    float value;                       // +8 the sample (codes 4 and up)
};
static_assert(sizeof(MutCode) == 12, "a MicroTalk code is 12 bytes");

struct MutTables {                     // 0x45c
    float coefs[64];                   // +0x000
    uint8_t huffStart[2][256];         // +0x100 [state][next 8 bits] -> code
    MutCode huffCodes[29];             // +0x300
};
static_assert(sizeof(MutTables) == 0x45c, "the MicroTalk tables are 0x45c bytes");

}   // namespace SND

extern const SND::MutTables MicroTalkTables;

int initmut(const uint8_t *src, SND::MutState *state);                     // 0x001493e0: 0
void decodemut(SND::MutState *state);                                      // 0x00149500: 432 samples
void readsamples(SND::MutState *state, int mode, float *out);              // 0x00146bb0: 54 samples, stride 2
void decode16x87(uint32_t count, const int16_t *in, float *out);           // 0x0014a1e0

// The two register-argument helpers: the adaptors (AUTOLTCG, Ghidra's names) and the C++ under them.
void MutLpcCoefficients(float *out, const float *coefs);                   // FUN_00146e00's work
void MutSynthesise(int first, SND::MutState *state, int groups);           // FUN_00146f00's work

#endif // DRIVING_SOUND_SND_DECODEUNUSED_H_
