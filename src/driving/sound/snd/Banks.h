#ifndef DRIVING_SOUND_SND_BANKS_H_
#define DRIVING_SOUND_SND_BANKS_H_

// EA's sound library, module C: sample banks (docs/driving/sound.md 3.4, 4.3), the PT tag reader both banks and
// streams use, and the stream side's header parser SNDI_patchtohdr (4.4). See Banks.cpp.

#include "Voices.h"

#include <stdint.h>

namespace SND {

// A BNKl version 5 bank's header, in the game's bank memory
struct BankHeader {
    char magic[4];               // +0x00 "BNKl"
    uint16_t version;            // +0x04 5
    uint16_t patchCount;         // +0x06
    uint32_t sizeA;              // +0x08 } SNDbankheadersize: their sum
    uint32_t sizeB;              // +0x0c }
    uint32_t total;              // +0x10
    uint32_t patchOffsets[1];    // +0x14 self-relative offsets to the "PT" headers, 0 = empty
};

// sndbanki_buffer (0x00244f40): one per bank (NUM_BANKS at 0x00244cf0)
struct BankSlot {
    BankHeader *header;          // +0x00 NULL = free
    uint8_t flag;                // +0x04 set: the bank is not playable (SNDBANKI_valid answers -18)
    uint8_t pad05[3];
};
static_assert(sizeof(BankSlot) == 8, "a bank slot is 8 bytes");

// The PT tag stream reader's state (SNDI_gettag)
struct TagReader {
    uint8_t *cursor;             // +0x00
    int32_t tag;                 // +0x04
    int32_t value;               // +0x08 big-endian, sign-extended; set only for lengths 0..4
    uint8_t *data;               // +0x0c
    int32_t length;              // +0x10
};
static_assert(sizeof(TagReader) == 0x14, "the tag reader is 0x14 bytes");

// SNDI_patchtohdr's format (4 bytes) and sample layout (0x1c)
struct StreamFormat {
    uint16_t sampleRate;         // +0x00 [0x84] (default 24000)
    uint8_t channels;            // +0x02 [0x82] (default 1)
    uint8_t sampleRep;           // +0x03 [0xa0] (default 8)
};
static_assert(sizeof(StreamFormat) == 4, "the stream format is 4 bytes");

struct StreamLayout {
    int32_t frames;              // +0x00 [0x85]
    int32_t offsets[6];          // +0x04 [0x88] [0x89] [0x94] [0x95] [0xa2] [0xa3]; per channel, + base for the mixer
};
static_assert(sizeof(StreamLayout) == 0x1c, "the stream layout is 0x1c bytes");

}  // namespace SND

int SNDBANK_play(int bank, int patch, SND::PlayOpts *opts);                   // 0x0013cc20
int SNDbankheadercopy(SND::BankHeader *destination, int bank);               // 0x0013ce20
int SNDbankheadersize(int bank);                                             // 0x0013ce60
void SNDBANKI_userdatacallback(SND::PatchHeader *header, int operation);    // 0x0013ce80
int SNDbankadd(int *bank, SND::BankHeader *data);                            // 0x0013cef0
int SNDbankpatpresent(int bank, int patch);                                  // 0x0013cf20
int SNDbankremove(int bank);                                                 // 0x0013cf60
int SNDBANKI_findfreekey(void);                                              // 0x00140200
int SNDBANKI_playtimbre(int bank, int patch, SND::BankHeader *bankHeader, SND::PlayOpts *opts,
                        SND::PatchHeader *header, int key, int velocity, int detuneRandom);   // 0x00140260
int SNDBANKI_playpatch(SND::BankHeader *bankHeader, uint8_t *patch, int bank, int patchIndex,
                       SND::PlayOpts *opts);                                 // 0x001405d0
int SNDBANKI_alloc(void);                                                    // 0x001407e0
uint8_t* SNDBANKI_getppatch(SND::BankHeader *bankHeader, int patch);          // 0x00140810
int SNDBANKI_valid(int bank);                                                // 0x00140840

int SNDI_parsetimbre(uint8_t **cursor, SND::PatchHeader *header);            // 0x00140aa0
int SNDI_gettag(SND::TagReader *reader);                                     // 0x001428d0
int SNDI_getb(const uint8_t *data, int length);                              // 0x00144a90
void SNDI_patchtohdr(int base, uint8_t *pt, SND::StreamFormat *format, SND::Attributes *attributes,
                     SND::StreamLayout *layout);                             // 0x0013f1c0

#endif // DRIVING_SOUND_SND_BANKS_H_
