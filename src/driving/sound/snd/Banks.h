#ifndef DRIVING_SOUND_SND_BANKS_H_
#define DRIVING_SOUND_SND_BANKS_H_

// EA's sound library, module C: sample banks (docs/driving/sound.md 3.4, 4.3), the PT tag reader both banks and
// streams use, and the stream side's header parser SNDI_patchtohdr (4.4). See Banks.cpp.

#include "Voices.h"

#include <stdint.h>

namespace SND {

// The PT tags (sound.md 4.3): a tag byte, a length byte (0xff: a 4-byte big-endian length follows), the data - for
// lengths up to 4 also a big-endian value. Brackets in the PatchHeader/Attributes layouts give each field's tag.
enum PtTag : uint8_t {
    // SNDI_parsetimbre's low fields (0x00..0x25)
    kTagVelocityLow = 0x01,
    kTagVelocityHigh = 0x02,
    kTagKeyLow = 0x03,
    kTagKeyHigh = 0x04,
    kTagPriority = 0x06,
    kTagRootKey = 0x07,
    kTagEnvRelease = 0x08,
    kTagEnvCount = 0x09,
    kTagBendRange = 0x0a,
    kTagPan = 0x0c,
    kTagPanRandom = 0x0d,
    kTagVolume = 0x0e,
    kTagVolumeRandom = 0x0f,
    kTagTune = 0x10,
    kTagTuneRandom = 0x11,
    kTagVolTable = 0x12,         // value + data pointer
    kTagFxLevel = 0x13,
    kTagUserData = 0x14,         // up to four, each a block for the user-data clients
    kTagBendTable = 0x17,        // value + data pointer
    kTagEnvTable = 0x19,         // value + data pointer
    kTag1a = 0x1a,
    kTagEnvStart = 0x1c,
    kTagVolLfo = 0x1d,           // value + data pointer
    kTagVolLfoLength = 0x1e,
    kTagVolLfoRandom = 0x1f,
    kTagPitchLfo = 0x20,         // value + data pointer
    kTagPitchLfoLength = 0x21,
    kTagPitchLfoDepth = 0x22,
    kTagPitchLfoRandom = 0x23,
    kTagDetuneRandom = 0x24,
    kTagPanMult = 0x25,
    kTagLowEnd = 0x26,           // (the low range's end)

    // the sample fields (0x80..0xa7)
    kTagPlatformVersion = 0x80,
    kTagSampleFirst = 0x80,      // (the sample range's start)
    kTagChannels = 0x82,
    kTagSampleRate = 0x84,
    kTagFrames = 0x85,
    kTagLoopStart = 0x86,
    kTagLoopEnd = 0x87,
    kTagSampleOffset0 = 0x88,    // per channel: 0x88, 0x89, 0x94, 0x95, 0xa2, 0xa3
    kTagSampleOffset1 = 0x89,
    kTagSampleData = 0x8a,       // the data pointer
    kTagRenderMode = 0x8c,
    kTagSampleOffset2 = 0x94,
    kTagSampleOffset3 = 0x95,
    kTagStretchData0 = 0x98,     // per channel: 0x98..0x9b, 0xa4, 0xa5 (time-stretch data)
    kTagStretchData1 = 0x99,
    kTagStretchData2 = 0x9a,
    kTagStretchData3 = 0x9b,
    kTagAzimuth0 = 0x9c,         // per channel: 0x9c..0x9f, 0xa6, 0xa7 (offsets from the default azimuth)
    kTagAzimuth1 = 0x9d,
    kTagAzimuth2 = 0x9e,
    kTagAzimuth3 = 0x9f,
    kTagSampleRep = 0xa0,
    kTagSampleOffset4 = 0xa2,
    kTagSampleOffset5 = 0xa3,
    kTagStretchData4 = 0xa4,
    kTagStretchData5 = 0xa5,
    kTagAzimuth4 = 0xa6,
    kTagAzimuth5 = 0xa7,
    kTagSampleEnd = 0xa8,        // (the sample range's end)

    // markers
    kTagPadding = 0xfc,          // skipped
    kTagMarker = 0xfd,           // bare
    kTagNextTimbre = 0xfe,       // bare: another timbre follows
    kTagEnd = 0xff,
};

// A BNKl version 5 bank's header, in the game's bank memory (MW: BANKVER5)
struct BankHeader {
    char magic[4];               // +0x00 "BNKl"
    uint16_t version;            // +0x04 5
    uint16_t patchCount;         // +0x06
    uint32_t headerSize;         // +0x08 } SNDbankheadersize: their sum
    uint32_t extraSize;          // +0x0c } (MW: spusize)
    uint32_t total;              // +0x10
    uint32_t patchOffsets[1];    // +0x14 [patchCount] offsets to the "PT" headers from their own word, 0 = empty
};

// sndbanki_buffer (0x00244f40): one per bank (NUM_BANKS at 0x00244cf0)
struct BankSlot {
    BankHeader *header;          // +0x00 NULL = free
    uint8_t flag;                // +0x04 set: the bank is not playable (SNDBANKI_valid answers -18)
    uint8_t pad05[3];
};
static_assert(sizeof(BankSlot) == 8, "a bank slot is 8 bytes");

// What a user-data client gets (MW: SNDUSERDATACBINFO); none is ever registered
struct UserDataInfo {
    int32_t operation;           // +0x00 1 played, 2 bank removed (MW: SND_UD_BANK_PLAY, _UNLOADED)
    uint8_t *data;               // +0x04
    int32_t size;                // +0x08
    int32_t handle;              // +0x0c } the streams' (SNDSTRMI_parseheader); the bank module leaves them unset
    int32_t request;             // +0x10 }
};
static_assert(sizeof(UserDataInfo) == 0x14, "SNDUSERDATACBINFO is 0x14 bytes");
typedef void (*UserDataClient)(UserDataInfo *info);

// The PT tag stream reader's state (SNDI_gettag)
struct TagReader {
    uint8_t *cursor;             // +0x00
    int32_t tag;                 // +0x04
    int32_t value;               // +0x08 big-endian, sign-extended; set only for lengths 0..4
    uint8_t *data;               // +0x0c
    int32_t length;              // +0x10
};
static_assert(sizeof(TagReader) == 0x14, "the tag reader is 0x14 bytes");

// SNDI_patchtohdr's format (4 bytes, MW: SNDSAMPLEFORMAT) and sample layout (0x1c, MW: SNDSAMPLEDESC)
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
