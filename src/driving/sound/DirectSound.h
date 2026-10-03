#ifndef DRIVING_SOUND_DIRECTSOUND_H_
#define DRIVING_SOUND_DIRECTSOUND_H_

// The Xbox DirectSound API as the driving engine calls it - the entry points our seam (dsndSeam.cpp) implements,
// under the XDK's names and conventions (__stdcall, the interface first), and the structures they take at the
// layouts the game fills in. EA's platform driver (snd/Platform.cpp) and voice server (snd/Voices.cpp) call these
// directly; the game's own calls reach the same functions through the five-byte jumps the seam writes over the
// original entry points.

#include <stdint.h>

// The objects: opaque to the game, the seam's own (dsndSeam.cpp)
struct IDirectSound;
struct IDirectSoundBuffer;

// DSMIXBINVOLUMEPAIR and DSMIXBINS
struct DsMixBinPair {
    uint32_t bin;                // DSMIXBIN_*
    int32_t volume;              // hundredths of a dB, -10000 (silence) to 0
};
struct DsMixBins {
    uint32_t count;
    DsMixBinPair *pairs;
};

// DSFILTERDESC
struct DsFilterDesc {
    uint32_t mode;               // 1
    uint32_t q;
    uint32_t coefficients[4];
};

#pragma pack(push, 1)
struct DsWaveFormat {            // WAVEFORMATEX + the ADPCM samples-per-block word (0x14 cleared)
    uint16_t formatTag;          // +0x00 0x69 Xbox ADPCM, 1 PCM
    uint16_t channels;           // +0x02
    uint32_t samplesPerSec;      // +0x04
    uint32_t avgBytesPerSec;     // +0x08
    uint16_t blockAlign;         // +0x0c
    uint16_t bitsPerSample;      // +0x0e
    uint16_t cbSize;             // +0x10
    uint16_t samplesPerBlock;    // +0x12
};
#pragma pack(pop)
static_assert(sizeof(DsWaveFormat) == 0x14, "the wave format is 0x14 bytes here");

// DSBUFFERDESC (0x18 cleared)
struct DsBufferDesc {
    uint32_t size;               // +0x00
    uint32_t flags;              // +0x04
    uint32_t bufferBytes;        // +0x08
    DsWaveFormat *format;        // +0x0c
    DsMixBins *mixBins;          // +0x10
    uint32_t inputMixBin;        // +0x14
};
static_assert(sizeof(DsBufferDesc) == 0x18, "the buffer description is 0x18 bytes here");

// DSI3DL2LISTENER (0x30): an I3DL2 reverb's listener properties
struct DsI3dl2Listener {
    int32_t room;                // +0x00 hundredths of a dB
    int32_t roomHF;              // +0x04
    float roomRolloffFactor;     // +0x08
    float decayTime;             // +0x0c seconds
    float decayHFRatio;          // +0x10
    int32_t reflections;         // +0x14
    float reflectionsDelay;      // +0x18
    int32_t reverb;              // +0x1c
    float reverbDelay;           // +0x20
    float diffusion;             // +0x24 percent
    float density;               // +0x28 percent
    float hfReference;           // +0x2c Hz
};
static_assert(sizeof(DsI3dl2Listener) == 0x30, "DSI3DL2LISTENER is 0x30 bytes");

// Every one answers DS_OK (0).
uint32_t __stdcall DirectSoundCreate(void *guid, IDirectSound **device, void *outer);                 // 0x0017c259
uint32_t __stdcall DirectSoundCreateBuffer(const DsBufferDesc *desc, IDirectSoundBuffer **buffer);    // 0x0017c2a0
uint32_t __stdcall IDirectSound_CreateSoundBuffer(IDirectSound *device, const DsBufferDesc *desc,
                                                  IDirectSoundBuffer **buffer, void *outer);          // 0x0017c09f
uint32_t __stdcall IDirectSound_DownloadEffectsImage(IDirectSound *device, const void *image, uint32_t bytes,
                                                     void *imageLocation, void **descriptor);         // 0x0017b59d
uint32_t __stdcall IDirectSound_SetI3DL2Listener(IDirectSound *device, const DsI3dl2Listener *listener,
                                                 uint32_t apply);                                     // 0x0017be1b
uint32_t __stdcall IDirectSound_Release(IDirectSound *device);                                        // 0x0017ad34

uint32_t __stdcall IDirectSoundBuffer_Release(IDirectSoundBuffer *buffer);                            // 0x0017ad4a
uint32_t __stdcall IDirectSoundBuffer_SetBufferData(IDirectSoundBuffer *buffer, const void *data,
                                                    uint32_t bytes);                                  // 0x0017be3b
uint32_t __stdcall IDirectSoundBuffer_Play(IDirectSoundBuffer *buffer, uint32_t reserved1, uint32_t reserved2,
                                           uint32_t flags);                                           // 0x0017b634
uint32_t __stdcall IDirectSoundBuffer_Stop(IDirectSoundBuffer *buffer);                               // 0x0017b658
uint32_t __stdcall IDirectSoundBuffer_GetStatus(IDirectSoundBuffer *buffer, uint32_t *status);        // 0x0017b690
uint32_t __stdcall IDirectSoundBuffer_GetCurrentPosition(IDirectSoundBuffer *buffer, uint32_t *playPosition,
                                                         uint32_t *writePosition);                    // 0x0017b6ac
uint32_t __stdcall IDirectSoundBuffer_SetCurrentPosition(IDirectSoundBuffer *buffer, uint32_t position);  // 0x0017b6cc
uint32_t __stdcall IDirectSoundBuffer_SetLoopRegion(IDirectSoundBuffer *buffer, uint32_t start,
                                                    uint32_t length);                                 // 0x0017b670
uint32_t __stdcall IDirectSoundBuffer_SetVolume(IDirectSoundBuffer *buffer, int32_t volume);          // 0x0017b5c4
uint32_t __stdcall IDirectSoundBuffer_SetFrequency(IDirectSoundBuffer *buffer, uint32_t hz);          // 0x0017b996
uint32_t __stdcall IDirectSoundBuffer_SetMixBins(IDirectSoundBuffer *buffer, const DsMixBins *mixBins);   // 0x0017b5fc
// The entry table calls this one IDirectSoundBuffer_SetMixBinVolumes_8 (the eight-bin form)
uint32_t __stdcall IDirectSoundBuffer_SetMixBinVolumes(IDirectSoundBuffer *buffer,
                                                       const DsMixBins *mixBins);                     // 0x0017b618
uint32_t __stdcall IDirectSoundBuffer_SetFilter(IDirectSoundBuffer *buffer, const DsFilterDesc *filter);  // 0x0017b5e0

#endif // DRIVING_SOUND_DIRECTSOUND_H_
