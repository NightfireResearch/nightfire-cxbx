#ifndef DRIVING_GC_GCPCM_H_
#define DRIVING_GC_GCPCM_H_

// The GameCube's bank samples as the PCM their DSP ADPCM was decoded to as the bank loaded (docs/gc-driving.md,
// "Sound"). A converted bank holds, in place of each sample, a stand-in as long as the sample's Xbox ADPCM would be, so
// the bank and everything the game reckons in bytes (sizes, loop regions, play cursors) are as the Xbox's. Each 36-byte
// block of a stand-in names its bank, sample and block; the sound outputs take the block's 64 samples from here in
// place of decoding it (GcPcm_Decode), so nothing is encoded again.

#include <stddef.h>
#include <stdint.h>
#include <vector>

#define GC_PCM_TAG 0x4d435047u   // "GPCM", a stand-in block's first word: a real block's fourth byte is 0

// A sample's PCM (mono), padded to whole blocks, and its own length
struct GcPcmSample {
    std::vector<int16_t> pcm;
    uint32_t frames;
};

// A bank's number by its contents, the same for the same bank loaded again; 0 for a file that is not a BNKb
uint32_t GcPcm_BankNumber(const uint8_t *file, uint32_t size);

// Pads to whole blocks: from loopStart round when the sample loops (loopStart >= 0), else with silence
void GcPcm_Pad(std::vector<int16_t> *pcm, int32_t loopStart);

// A sample's stand-in at data, blocks of 36 bytes: the tag, (bank << 16 | sample), the block's number, then zeros
void GcPcm_WriteStandIn(uint8_t *data, uint32_t blocks, uint32_t bank, uint32_t sample);

// Keeps a bank's samples, unless the number has them already (NIGHTFIRE_GC_PCM_SHOW: says so, with the path)
void GcPcm_Keep(uint32_t bank, const char *path, std::vector<GcPcmSample> &&samples);

// Decodes Xbox ADPCM as XAdpcm_Decode does, a stand-in's blocks taken from its sample (NIGHTFIRE_GC_PCM_SHOW: each
// sample's fingerprint the first time its first block is taken)
size_t GcPcm_Decode(const void *adpcm, size_t bytes, int channels, int16_t *pcm, size_t values);

// FNV-1a over PCM's bytes, to check what the outputs are given against a decode offline
uint32_t GcPcm_Fingerprint(const int16_t *pcm, size_t values);

#endif // DRIVING_GC_GCPCM_H_
