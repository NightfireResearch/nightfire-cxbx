#include "dsndSeam.h"

#include "../../common/standalone.h"
#include "../../common/xbeEntrySeam.h"

#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The driving engine's sound seam - docs/driving-engine-plan.md section 6.2.
//
// WHY THIS EXISTS AT ALL. The XBE statically links Microsoft's DirectSound, whose lower half programs the
// MCPX audio registers at 0xfe80xxxx and spins on them. On the console that is the hardware; here it is
// unmapped memory, and the first thing "Init Sound" does is read 0xfe801100 and fault. Nothing above it is
// at fault - the game is doing exactly what it was built to do - so the library has to stop being reached.
//
// WHERE THE SEAM IS. The plan proposes seaming EA's SNDPLATFORM_* layer, which sits above DirectSound, and
// warns that such a seam has to be *complete*: anything that slips through reaches the real library. The
// action engine's experience is the warning - its seam covered every game-side call and still missed the
// video decoder, which called DirectSound directly. Seaming DirectSound's own 63 entry points is that
// completeness by construction, and it is the same technique the graphics seam uses one library along.
//
// The layering inside the library makes it a natural boundary. `IDirectSound*` and `IDirectSoundBuffer*` are
// what EA's code calls; `CDirectSound*` is the implementation; `CMcpx*` is the hardware. Replacing the first
// group is enough to keep the other two from ever running, and the rest of the table is stubbed so that
// anything unexpected is reported rather than reaching the registers.
//
// WHAT IT DOES SO FAR: silence, but bookkeeping that is true. Buffers are created, given their data, played
// and stopped, and - this is the part that matters - a buffer that has played for as long as its data lasts
// reports itself stopped. EA's voice allocator reclaims buffers only when GetStatus says they have finished
// (SNDDRV's 100 Hz thread polls it), so a seam that reported everything as permanently playing would drain
// the free lists in minutes and crash exactly where section 4 of the plan says it crashes. Timing the
// playback is what keeps that healthy, and it is the same bookkeeping a real backend needs underneath.
//
// The next step is the action engine's XAudio2 backend behind these same entry points: it already has the
// voice, mixbin, I3DL2 and Xbox-ADPCM decoding work done (src/action/sound/), and the formats here are the
// ones it handles - 48 kHz mono, 16-bit PCM or Xbox ADPCM.
// ---------------------------------------------------------------------------------------------------------------

static XbeEntry g_entries[] = {
#define XBE_ENTRY(name, address, stack)        { #name, address, stack, 0, 0, false },
#define XBE_ENTRY_UNKNOWN_STACK(name, address) { #name, address, XBE_ENTRY_STACK_UNKNOWN, 0, 0, false },
#include "dsoundEntries.inc"
#undef XBE_ENTRY
#undef XBE_ENTRY_UNKNOWN_STACK
};

static XbeEntrySeam g_seam = { g_entries, sizeof(g_entries) / sizeof(g_entries[0]), "dsnd" };

// ---------------------------------------------------------------------------------------------------------------
// The objects the game is handed.
//
// Opaque to it: every function that takes one is replaced here, so the only thing it ever does with a buffer
// pointer is give it back. That is what lets these be plain structures rather than something shaped like a
// DirectSound object.
// ---------------------------------------------------------------------------------------------------------------

// Xbox DSBUFFERDESC and WAVEFORMATEX, at the offsets the game fills in (dsndCreateBufferAndMixBins,
// 0x0013d430, and dsndMixInit, 0x0013d5e0).
#pragma pack(push, 1)
struct XboxWaveFormat {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
};

struct XboxBufferDesc {
    uint32_t        dwSize;
    uint32_t        dwFlags;
    uint32_t        dwBufferBytes;
    XboxWaveFormat *lpwfxFormat;
    void           *lpMixBins;
    uint32_t        dwInputMixBin;
};
#pragma pack(pop)

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM         0x0001
#endif
#define WAVE_FORMAT_XBOX_ADPCM  0x0069   // the console's own, 64 samples to a 36-byte block

#define DSBSTATUS_PLAYING  0x00000001
#define DSBSTATUS_LOOPING  0x00000004
#define DSBPLAY_LOOPING    0x00000001

struct SeamBuffer {
    uint32_t sampleRate;
    uint16_t formatTag;
    uint16_t channels;
    uint16_t blockAlign;
    uint16_t bitsPerSample;

    const void *data;
    uint32_t    dataBytes;

    bool     playing;
    bool     looping;
    uint32_t startedAtMs;      // timeGetTime when Play was called, adjusted by SetCurrentPosition
    uint32_t durationMs;       // how long its data lasts, 0 if it has none yet
    uint32_t position;         // where SetCurrentPosition last put it
};

// A device object, to hand back from DirectSoundCreate. Nothing reads it.
static uint32_t g_device = 0;

// How long a block of this format lasts. Xbox ADPCM packs 64 samples into each 36-byte block; PCM is the
// obvious division. Both are per channel, and the game's buffers are mono.
static uint32_t DurationMsOf(const SeamBuffer *buffer, uint32_t bytes) {
    if (buffer->sampleRate == 0 || bytes == 0)
        return 0;

    uint64_t samples;
    if (buffer->formatTag == WAVE_FORMAT_XBOX_ADPCM) {
        uint32_t blockAlign = buffer->blockAlign != 0 ? buffer->blockAlign : 36;
        samples = (uint64_t)(bytes / blockAlign) * 64;
    } else {
        uint32_t bytesPerSample = (buffer->bitsPerSample != 0 ? buffer->bitsPerSample : 16) / 8;
        uint32_t frame = bytesPerSample * (buffer->channels != 0 ? buffer->channels : 1);
        samples = (frame != 0) ? bytes / frame : 0;
    }
    return (uint32_t)((samples * 1000) / buffer->sampleRate);
}

// True while the buffer still has sound left to play. A looping buffer never runs out; a one-shot stops
// itself when its data has been played through, which is what EA's voice allocator waits to see.
static bool StillPlaying(SeamBuffer *buffer) {
    if (!buffer->playing)
        return false;
    if (buffer->looping || buffer->durationMs == 0)
        return true;
    if (timeGetTime() - buffer->startedAtMs < buffer->durationMs)
        return true;
    buffer->playing = false;
    return false;
}

static SeamBuffer *CreateBuffer(const XboxBufferDesc *desc) {
    SeamBuffer *buffer = (SeamBuffer *)calloc(1, sizeof(SeamBuffer));
    if (buffer == NULL)
        return NULL;

    // Defaults for a description without a format: the rate everything in this game uses.
    buffer->sampleRate = 48000;
    buffer->formatTag = WAVE_FORMAT_PCM;
    buffer->channels = 1;
    buffer->bitsPerSample = 16;
    buffer->blockAlign = 2;

    if (desc != NULL && desc->lpwfxFormat != NULL) {
        const XboxWaveFormat *format = desc->lpwfxFormat;
        if (format->nSamplesPerSec >= 4000 && format->nSamplesPerSec <= 96000)
            buffer->sampleRate = format->nSamplesPerSec;
        buffer->formatTag = format->wFormatTag;
        buffer->channels = format->nChannels != 0 ? format->nChannels : 1;
        buffer->bitsPerSample = format->wBitsPerSample;
        buffer->blockAlign = format->nBlockAlign;
    }
    if (desc != NULL && desc->dwBufferBytes != 0) {
        buffer->dataBytes = desc->dwBufferBytes;
        buffer->durationMs = DurationMsOf(buffer, desc->dwBufferBytes);
    }
    return buffer;
}

// ---------------------------------------------------------------------------------------------------------------
// The entry points.
//
// Everything returns DS_OK (zero). The EA layer checks for failure and gives up on a voice when it sees one,
// and there is nothing here that can fail in a way it could do anything about.
// ---------------------------------------------------------------------------------------------------------------

static uint32_t __stdcall Seam_DirectSoundCreate(void *guid, void **returnedDevice, void *outer) {
    (void)guid; (void)outer;
    if (returnedDevice != NULL)
        *returnedDevice = &g_device;
    printf("[dsnd] DirectSound opened (silent: no backend behind the seam yet)\n");
    fflush(stdout);
    return 0;
}

static uint32_t __stdcall Seam_IDirectSound_CreateSoundBuffer(void *device, const XboxBufferDesc *desc,
                                                              void **returnedBuffer, void *outer) {
    (void)device; (void)outer;
    if (returnedBuffer == NULL)
        return 0;
    *returnedBuffer = CreateBuffer(desc);
    return 0;
}

// The same thing without a device, which is what EA's buffer pool calls.
static uint32_t __stdcall Seam_DirectSoundCreateBuffer(const XboxBufferDesc *desc, void **returnedBuffer) {
    if (returnedBuffer == NULL)
        return 0;
    *returnedBuffer = CreateBuffer(desc);
    return 0;
}

// On the Xbox a buffer is created empty and given its samples afterwards, by pointer - there is no lock and
// no copy, the hardware reads the game's own memory. So this is where a buffer learns how long it is.
static uint32_t __stdcall Seam_IDirectSoundBuffer_SetBufferData(SeamBuffer *buffer, const void *data,
                                                                uint32_t bytes) {
    if (buffer == NULL)
        return 0;
    buffer->data = data;
    buffer->dataBytes = bytes;
    buffer->durationMs = DurationMsOf(buffer, bytes);
    return 0;
}

static uint32_t __stdcall Seam_IDirectSoundBuffer_Play(SeamBuffer *buffer, uint32_t reserved1,
                                                       uint32_t reserved2, uint32_t flags) {
    (void)reserved1; (void)reserved2;
    if (buffer == NULL)
        return 0;
    buffer->playing = true;
    buffer->looping = (flags & DSBPLAY_LOOPING) != 0;
    buffer->startedAtMs = timeGetTime();
    return 0;
}

static uint32_t __stdcall Seam_IDirectSoundBuffer_Stop(SeamBuffer *buffer) {
    if (buffer != NULL) {
        buffer->playing = false;
        buffer->position = 0;
    }
    return 0;
}

static uint32_t __stdcall Seam_IDirectSoundBuffer_GetStatus(SeamBuffer *buffer, uint32_t *status) {
    if (status == NULL)
        return 0;
    if (buffer == NULL) {
        *status = 0;
        return 0;
    }
    *status = StillPlaying(buffer) ? (DSBSTATUS_PLAYING | (buffer->looping ? DSBSTATUS_LOOPING : 0)) : 0;
    return 0;
}

// Where the buffer has got to, in bytes. The read cursor is the play cursor: nothing is being written ahead
// of it, so reporting them at the same place is as true as anything here.
static uint32_t __stdcall Seam_IDirectSoundBuffer_GetCurrentPosition(SeamBuffer *buffer, uint32_t *playPosition,
                                                                     uint32_t *writePosition) {
    uint32_t position = 0;
    if (buffer != NULL && buffer->dataBytes != 0 && StillPlaying(buffer) && buffer->durationMs != 0) {
        uint32_t elapsed = timeGetTime() - buffer->startedAtMs;
        if (buffer->looping)
            elapsed %= buffer->durationMs;
        position = (uint32_t)(((uint64_t)elapsed * buffer->dataBytes) / buffer->durationMs);
        if (position > buffer->dataBytes)
            position = buffer->dataBytes;
    } else if (buffer != NULL) {
        position = buffer->position;
    }

    if (playPosition != NULL)
        *playPosition = position;
    if (writePosition != NULL)
        *writePosition = position;
    return 0;
}

static uint32_t __stdcall Seam_IDirectSoundBuffer_SetCurrentPosition(SeamBuffer *buffer, uint32_t position) {
    if (buffer == NULL)
        return 0;
    buffer->position = position;
    // Playing from part-way through means the rest is what is left to play, so the clock starts back there.
    if (buffer->dataBytes != 0 && buffer->durationMs != 0) {
        uint32_t into = (uint32_t)(((uint64_t)position * buffer->durationMs) / buffer->dataBytes);
        buffer->startedAtMs = timeGetTime() - into;
    }
    return 0;
}

static uint32_t __stdcall Seam_IDirectSoundBuffer_Release(SeamBuffer *buffer) {
    free(buffer);
    return 0;
}

static uint32_t __stdcall Seam_IDirectSound_Release(void *device) {
    (void)device;   // the device is a static, not an allocation
    return 0;
}

static const struct { const char *name; void *replacement; unsigned stackBytes; } g_replacements[] = {
    { "DirectSoundCreate",                     (void *)Seam_DirectSoundCreate, 12 },
    { "IDirectSound_CreateSoundBuffer",        (void *)Seam_IDirectSound_CreateSoundBuffer, 16 },
    { "DirectSoundCreateBuffer",               (void *)Seam_DirectSoundCreateBuffer, 8 },
    { "IDirectSoundBuffer_SetBufferData",      (void *)Seam_IDirectSoundBuffer_SetBufferData, 12 },
    { "IDirectSoundBuffer_Play",               (void *)Seam_IDirectSoundBuffer_Play, 16 },
    { "IDirectSoundBuffer_Stop",               (void *)Seam_IDirectSoundBuffer_Stop, 4 },
    { "IDirectSoundBuffer_GetStatus",          (void *)Seam_IDirectSoundBuffer_GetStatus, 8 },
    { "IDirectSoundBuffer_GetCurrentPosition", (void *)Seam_IDirectSoundBuffer_GetCurrentPosition, 12 },
    { "IDirectSoundBuffer_SetCurrentPosition", (void *)Seam_IDirectSoundBuffer_SetCurrentPosition, 8 },
    { "IDirectSoundBuffer_Release",            (void *)Seam_IDirectSoundBuffer_Release, 4 },
    { "IDirectSound_Release",                  (void *)Seam_IDirectSound_Release, 4 },
};

void DsndSeam_ReportMissing(void) {
    XbeSeam_ReportMissing(&g_seam);
}

void Inject_DsndSeam(void) {
    // Under CXBX, DirectSound is CXBX's HLE and already plays sound; patching it would silence a working
    // path and fight whatever it has bound to.
    if (!Xbox_RunningStandalone())
        return;

    for (size_t i = 0; i < sizeof(g_replacements) / sizeof(g_replacements[0]); i++)
        XbeSeam_Replace(&g_seam, g_replacements[i].name, g_replacements[i].replacement,
                        g_replacements[i].stackBytes);

    XbeSeam_Install(&g_seam);
}
