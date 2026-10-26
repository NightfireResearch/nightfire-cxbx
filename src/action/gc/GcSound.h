#ifndef GC_GCSOUND_H_
#define GC_GCSOUND_H_

// The GameCube's sound (docs/gc-data.md) as the Xbox's sound code reads it. No game state: the offline checker uses
// it too.
//
// - A bank's samples are Nintendo DSP ADPCM in the .SBF, each with the DSP's 96-byte header (coefficients, initial
//   history, loop) in the .SHG and a big-endian sample header in the .SHF. They are decoded to 16-bit PCM, which the
//   sound backends play as it is (no re-encoding): the bank the game loads holds, for each sample, a stand-in as
//   long as the Xbox ADPCM of the same samples would be (sound/PcmBanks.h, which keeps the PCM for GcFs.cpp).
// - A bank's sounds (.SFX) are the Xbox's, big-endian.
// - The music (.SSD) is headerless IMA ADPCM, stereo, one byte (two samples) of each channel in turn, at 32 kHz. The
//   same codes, given the decoder's state every 64 samples as block headers, are the Xbox's ADPCM blocks: nothing is
//   re-encoded. Its markers (.SMF) are the Xbox's, big-endian, with positions four times the byte offset.

#include <stdint.h>
#include <vector>

#define GC_DSP_HEADER_BYTES 0x60
#define GC_MUSIC_FREQUENCY 32000

// One sample decoded: its PCM (mono), and where its loop starts (in samples; -1 for none)
struct GcSoundSample {
    std::vector<int16_t> pcm;
    int loopStart;
    int frequency;
    bool loops;
};

// A DSP ADPCM sample to PCM, from its DSP header (big-endian) and its frames; false when the header does not read
bool GcSound_DecodeDsp(const uint8_t *dspHeader, const uint8_t *frames, uint32_t frameBytes, GcSoundSample *out);

// A bank's three files to its samples: the .SHF (sample headers), .SHG (DSP headers), .SBF (frames)
bool GcSound_DecodeBank(const uint8_t *shf, uint32_t shfBytes, const uint8_t *shg, uint32_t shgBytes,
                        const uint8_t *sbf, uint32_t sbfBytes, std::vector<GcSoundSample> &samples);

// The Xbox's .SHF and the stand-in .SBF for decoded samples (bank: the bank's number, which the stand-ins carry)
void GcSound_XboxBank(const std::vector<GcSoundSample> &samples, int bank, std::vector<uint8_t> &shf,
                      std::vector<uint8_t> &sbf);

// The .SFX turned round field by field, in place
void GcSound_ConvertSfx(uint8_t *file, uint32_t bytes);

// A music track as the Xbox's: stereo Xbox ADPCM blocks of the same codes
void GcSound_XboxMusic(const uint8_t *ssd, uint32_t bytes, std::vector<uint8_t> &out);

// The markers turned round and their positions made the Xbox track's, in place
void GcSound_ConvertMarkers(uint8_t *smf, uint32_t bytes);

#endif // GC_GCSOUND_H_
