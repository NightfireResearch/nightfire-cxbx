#ifndef DRIVING_SOUND_SND_DECODE_H_
#define DRIVING_SOUND_SND_DECODE_H_

// EA's EA-XA decoder in the sound library (docs/driving/sound.md 3.7, 4.9): SND::CEAXABLKDecf, the object the
// mixer's EA-XA unpackers (SFILTER_unpackxapf on the disc's streams) feed one packet channel at a time, and
// SND::decodexac, its hand-written SSE block decoder. See Decode.cpp. The data-dead decoders (MicroTalk, PCM16)
// are in DecodeUnused.h.

#include <stddef.h>
#include <stdint.h>

namespace SND {

// What decodexac works on, at CEAXABLKDecf +0x94.
struct DecodeXacParams {               // 0x14
    int32_t frames;                    // +0x00 frames wanted: a block of 28 per 28 or part of it (ends at <= 0)
    float s1;                          // +0x04 predictor history: the newest sample
    float s2;                          // +0x08 the one before
    const uint8_t *src;                // +0x0c 15-byte blocks; decodexac reads but does not advance it
    float *dst;                        // +0x10 output; decodexac advances it by 28 per block
};
static_assert(sizeof(DecodeXacParams) == 0x14, "decodexac's argument block is 0x14 bytes");

// The decoder (thiscall; 0xa8 bytes from CODA_New). Feed gives it a packet channel's blocks, SetState the two
// history samples (the packet's two leading shorts as floats), Decode hands out frames: what is left of a block
// decoded earlier, then whole blocks straight into the output, then one more block into `buffer` for the rest.
struct CEAXABLKDecf {                  // 0xa8
    uint32_t unknown00;                // +0x00 never touched
    int32_t buffered;                  // +0x04 frames still in `buffer`
    int32_t framesLeft;                // +0x08 frames left of the fed packet (Feed refuses while non-zero)
    int32_t bytes;                     // +0x0c Feed's byte count: kept, never read
    float *bufferRead;                 // +0x10 the next frame in `buffer`
    uint8_t unknown14[0x10];           // +0x14 never touched
    float buffer[28];                  // +0x24 the last block decoded, when only part of it was wanted
    DecodeXacParams xac;               // +0x94 decodexac's block: s1/s2 are the state, src the next block

    CEAXABLKDecf* Construct();                                     // 0x00149e70 (the constructor)
    static void* operator new(size_t size);                        // 0x00149e50 (jmp [CODA_New])
    static void operator delete(void *block);                      // 0x00149e60 (jmp [CODA_Delete])
    int Feed(const void *data, int bytes, int frames);             // 0x00149e90
    int Decode(float **out, int frames);                           // 0x00149ec0
    void* GetState(void *state);                                   // 0x0014a190 (data-dead: bank EA-XA only)
    void SetState(const void *state);                              // 0x0014a1c0
};
static_assert(sizeof(CEAXABLKDecf) == 0xa8, "a CEAXABLKDecf is 0xa8 bytes");

// 0x00149d80: one stack argument, cdecl (Ghidra's __fastcall is wrong); the original saves every register.
void decodexac(DecodeXacParams *params);

}   // namespace SND

#endif // DRIVING_SOUND_SND_DECODE_H_
