#include "dsndSeam.h"
#include "DirectSound.h"
#include "xaudio2Driving.h"

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
// WHAT IS BEHIND IT. XAudio2, through xaudio2Driving.cpp, which streams every buffer out of the game's own
// memory a few milliseconds at a time - the only arrangement that works for EA's software mixer, whose six
// looping 50 ms rings (one per 5.1 speaker) are rewritten twenty milliseconds at a time by its 100 Hz thread
// with nothing said to DirectSound, because the console's hardware simply read the memory. The pooled
// one-shot and looping buffers go the same way. If XAudio2 cannot start, the bookkeeping below stands on
// its own: buffers are created, given their data, played and stopped, and a buffer that has played for as
// long as its data lasts reports itself stopped. EA's voice allocator reclaims buffers only when GetStatus
// says they have finished (SNDDRV's 100 Hz thread polls it), so a seam that reported everything as
// permanently playing would drain the free lists in minutes and crash exactly where section 4 of the plan
// says it crashes.
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

// The descriptions the game fills in (dsndCreateBufferAndMixBins, 0x0013d430, and dsndMixInit, 0x0013d5e0) are
// DirectSound.h's DsBufferDesc, DsWaveFormat and DsMixBins.

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM         0x0001
#endif
#define WAVE_FORMAT_XBOX_ADPCM  0x0069   // the console's own, 64 samples to a 36-byte block

#define DSBSTATUS_PLAYING  0x00000001
#define DSBSTATUS_LOOPING  0x00000004
#define DSBPLAY_LOOPING    0x00000001

struct IDirectSoundBuffer {   // the seam's buffer
    uint32_t sampleRate;
    uint16_t formatTag;
    uint16_t channels;
    uint16_t blockAlign;
    uint16_t bitsPerSample;

    const void *data;
    uint32_t    dataBytes;

    DrivingVoice *voice;       // the backend's, or NULL when there is no audio device

    // The silent bookkeeping, used only when there is no voice.
    bool     playing;
    bool     looping;
    uint32_t startedAtMs;      // timeGetTime when Play was called, adjusted by SetCurrentPosition
    uint32_t durationMs;       // how long its data lasts, 0 if it has none yet
    uint32_t position;         // where SetCurrentPosition last put it
};

// A device object, to hand back from DirectSoundCreate. Nothing reads it.
struct IDirectSound {
    uint32_t unused;
};
static IDirectSound g_device = {};
static bool g_audio = false;   // XAudio2 started

// How long a block of this format lasts. Xbox ADPCM packs 64 samples into each 36-byte block; PCM is the
// obvious division. Both are per channel, and the game's buffers are mono.
static uint32_t DurationMsOf(const IDirectSoundBuffer *buffer, uint32_t bytes) {
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

// True while the buffer still has sound left to play. With a voice, the backend knows; without one, a
// looping buffer never runs out and a one-shot stops itself when its data has been played through, which
// is what EA's voice allocator waits to see.
static bool StillPlaying(IDirectSoundBuffer *buffer) {
    if (buffer->voice != NULL)
        return DrivingAudio_IsPlaying(buffer->voice);
    if (!buffer->playing)
        return false;
    if (buffer->looping || buffer->durationMs == 0)
        return true;
    if (timeGetTime() - buffer->startedAtMs < buffer->durationMs)
        return true;
    buffer->playing = false;
    return false;
}

static IDirectSoundBuffer *CreateBuffer(const DsBufferDesc *desc) {
    IDirectSoundBuffer *buffer = (IDirectSoundBuffer *)calloc(1, sizeof(IDirectSoundBuffer));
    if (buffer == NULL)
        return NULL;

    // Defaults for a description without a format: the rate everything in this game uses.
    buffer->sampleRate = 48000;
    buffer->formatTag = WAVE_FORMAT_PCM;
    buffer->channels = 1;
    buffer->bitsPerSample = 16;
    buffer->blockAlign = 2;

    if (desc != NULL && desc->format != NULL) {
        const DsWaveFormat *format = desc->format;
        if (format->samplesPerSec >= 4000 && format->samplesPerSec <= 96000)
            buffer->sampleRate = format->samplesPerSec;
        buffer->formatTag = format->formatTag;
        buffer->channels = format->channels != 0 ? format->channels : 1;
        buffer->bitsPerSample = format->bitsPerSample;
        buffer->blockAlign = format->blockAlign;
    }
    if (desc != NULL && desc->bufferBytes != 0) {
        buffer->dataBytes = desc->bufferBytes;
        buffer->durationMs = DurationMsOf(buffer, desc->bufferBytes);
    }
    if (g_audio)
        buffer->voice = DrivingAudio_CreateVoice(buffer->sampleRate, buffer->formatTag, buffer->channels,
                                                 buffer->blockAlign, buffer->bitsPerSample);
    return buffer;
}

// The pairs of a DSMIXBINS, split for the backend.
static void UnpackMixBins(const DsMixBins *mixBins, uint32_t bins[8], int32_t volumes[8], uint32_t *count) {
    *count = 0;
    if (mixBins == NULL || mixBins->pairs == NULL)
        return;
    for (uint32_t i = 0; i < mixBins->count && *count < 8; i++) {
        bins[*count] = mixBins->pairs[i].bin;
        volumes[*count] = mixBins->pairs[i].volume;
        (*count)++;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// The entry points (DirectSound.h): what the game's calls reach through the seam's jumps, and what EA's platform
// driver calls directly.
//
// Everything returns DS_OK (zero). The EA layer checks for failure and gives up on a voice when it sees one,
// and there is nothing here that can fail in a way it could do anything about.
// ---------------------------------------------------------------------------------------------------------------

uint32_t __stdcall DirectSoundCreate(void *guid, IDirectSound **returnedDevice, void *outer) {
    (void)guid; (void)outer;
    if (returnedDevice != NULL)
        *returnedDevice = &g_device;
    g_audio = DrivingAudio_Start();
    printf(g_audio ? "[dsnd] DirectSound opened, XAudio2 behind it\n"
                   : "[dsnd] DirectSound opened (silent: XAudio2 could not start)\n");
    fflush(stdout);
    return 0;
}

uint32_t __stdcall IDirectSound_CreateSoundBuffer(IDirectSound *device, const DsBufferDesc *desc,
                                                  IDirectSoundBuffer **returnedBuffer, void *outer) {
    (void)device; (void)outer;
    if (returnedBuffer == NULL)
        return 0;
    *returnedBuffer = CreateBuffer(desc);
    return 0;
}

// The same thing without a device, which is what EA's buffer pool calls.
uint32_t __stdcall DirectSoundCreateBuffer(const DsBufferDesc *desc, IDirectSoundBuffer **returnedBuffer) {
    if (returnedBuffer == NULL)
        return 0;
    *returnedBuffer = CreateBuffer(desc);
    return 0;
}

// On the Xbox a buffer is created empty and given its samples afterwards, by pointer - there is no lock and
// no copy, the hardware reads the game's own memory. So this is where a buffer learns how long it is, and
// where the backend learns what to stream.
uint32_t __stdcall IDirectSoundBuffer_SetBufferData(IDirectSoundBuffer *buffer, const void *data, uint32_t bytes) {
    if (buffer == NULL)
        return 0;
    buffer->data = data;
    buffer->dataBytes = bytes;
    buffer->durationMs = DurationMsOf(buffer, bytes);
    DrivingAudio_SetData(buffer->voice, data, bytes);
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_Play(IDirectSoundBuffer *buffer, uint32_t reserved1, uint32_t reserved2,
                                           uint32_t flags) {
    (void)reserved1; (void)reserved2;
    if (buffer == NULL)
        return 0;
    buffer->playing = true;
    buffer->looping = (flags & DSBPLAY_LOOPING) != 0;
    buffer->startedAtMs = timeGetTime();
    DrivingAudio_Play(buffer->voice, buffer->looping);
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_Stop(IDirectSoundBuffer *buffer) {
    if (buffer != NULL) {
        buffer->playing = false;
        buffer->position = 0;
        DrivingAudio_Stop(buffer->voice);
    }
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_GetStatus(IDirectSoundBuffer *buffer, uint32_t *status) {
    if (status == NULL)
        return 0;
    if (buffer == NULL) {
        *status = 0;
        return 0;
    }
    *status = StillPlaying(buffer) ? (DSBSTATUS_PLAYING | (buffer->looping ? DSBSTATUS_LOOPING : 0)) : 0;
    return 0;
}

// Where the buffer has got to, in bytes. With a voice these are the backend's cursors, which is what EA's
// mixer paces its refills by. Without one the read cursor is the play cursor: nothing is being written ahead
// of it, so reporting them at the same place is as true as anything here.
uint32_t __stdcall IDirectSoundBuffer_GetCurrentPosition(IDirectSoundBuffer *buffer, uint32_t *playPosition,
                                                         uint32_t *writePosition) {
    if (buffer != NULL && buffer->voice != NULL) {
        DrivingAudio_GetPosition(buffer->voice, playPosition, writePosition);
        return 0;
    }
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

uint32_t __stdcall IDirectSoundBuffer_SetCurrentPosition(IDirectSoundBuffer *buffer, uint32_t position) {
    if (buffer == NULL)
        return 0;
    buffer->position = position;
    // Playing from part-way through means the rest is what is left to play, so the clock starts back there.
    if (buffer->dataBytes != 0 && buffer->durationMs != 0) {
        uint32_t into = (uint32_t)(((uint64_t)position * buffer->durationMs) / buffer->dataBytes);
        buffer->startedAtMs = timeGetTime() - into;
    }
    DrivingAudio_SetPosition(buffer->voice, position);
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_SetLoopRegion(IDirectSoundBuffer *buffer, uint32_t start, uint32_t length) {
    if (buffer != NULL)
        DrivingAudio_SetLoopRegion(buffer->voice, start, length);
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_SetVolume(IDirectSoundBuffer *buffer, int32_t volume) {
    if (buffer != NULL)
        DrivingAudio_SetVolume(buffer->voice, volume);
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_SetFrequency(IDirectSoundBuffer *buffer, uint32_t hz) {
    if (buffer != NULL)
        DrivingAudio_SetFrequency(buffer->voice, hz);
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_SetMixBins(IDirectSoundBuffer *buffer, const DsMixBins *mixBins) {
    if (buffer == NULL)
        return 0;
    uint32_t bins[8], count; int32_t volumes[8];
    UnpackMixBins(mixBins, bins, volumes, &count);
    DrivingAudio_SetMixBins(buffer->voice, bins, volumes, count);
    return 0;
}

// The same structure; the "_8" is the entry point's name for the eight-bin form of the call, and the game
// makes it for every active voice on every mixer tick - it is how EA positions a sound.
uint32_t __stdcall IDirectSoundBuffer_SetMixBinVolumes(IDirectSoundBuffer *buffer, const DsMixBins *mixBins) {
    if (buffer == NULL)
        return 0;
    uint32_t bins[8], count; int32_t volumes[8];
    UnpackMixBins(mixBins, bins, volumes, &count);
    DrivingAudio_SetMixBinVolumes(buffer->voice, bins, volumes, count);
    return 0;
}

// The per-voice low-pass filter, which the console's DSP applied. Accepted and not applied: EA sets it on a
// few dozen voices a session, for distance muffling, and the sound is right without it before it is right
// with it.
uint32_t __stdcall IDirectSoundBuffer_SetFilter(IDirectSoundBuffer *buffer, const DsFilterDesc *filter) {
    (void)buffer; (void)filter;
    return 0;
}

// The DSP effects image and the I3DL2 room: the reverb path, which has no host behind it yet. The image
// call wants a descriptor back; an empty one keeps the caller's pointer valid.
uint32_t __stdcall IDirectSound_DownloadEffectsImage(IDirectSound *device, const void *image, uint32_t bytes,
                                                     void *imageLocation, void **descriptor) {
    (void)device; (void)image; (void)bytes; (void)imageLocation;
    static uint32_t emptyDescriptor[8];
    if (descriptor != NULL)
        *descriptor = emptyDescriptor;
    return 0;
}

uint32_t __stdcall IDirectSound_SetI3DL2Listener(IDirectSound *device, const DsI3dl2Listener *listener,
                                                 uint32_t apply) {
    (void)device; (void)listener; (void)apply;
    return 0;
}

uint32_t __stdcall IDirectSoundBuffer_Release(IDirectSoundBuffer *buffer) {
    if (buffer != NULL)
        DrivingAudio_Release(buffer->voice);
    free(buffer);
    return 0;
}

uint32_t __stdcall IDirectSound_Release(IDirectSound *device) {
    (void)device;   // the device is a static, not an allocation
    return 0;
}

// DirectSound's reference-counted base class: AddRef (0x0017acb3) bumps the count at +4 and returns it. It has
// no direct callers - it is reached through a vtable - so it is ported as it is rather than left to a stub.
static uint32_t __stdcall Seam_DSound_CRefCount_AddRef(uint32_t *object) {
    return ++object[1];
}

static const struct { const char *name; void *replacement; unsigned stackBytes; } g_replacements[] = {
    { "DSound_CRefCount_AddRef",               (void *)Seam_DSound_CRefCount_AddRef, 4 },
    { "DirectSoundCreate",                     (void *)DirectSoundCreate, 12 },
    { "IDirectSound_CreateSoundBuffer",        (void *)IDirectSound_CreateSoundBuffer, 16 },
    { "DirectSoundCreateBuffer",               (void *)DirectSoundCreateBuffer, 8 },
    { "IDirectSoundBuffer_SetBufferData",      (void *)IDirectSoundBuffer_SetBufferData, 12 },
    { "IDirectSoundBuffer_Play",               (void *)IDirectSoundBuffer_Play, 16 },
    { "IDirectSoundBuffer_Stop",               (void *)IDirectSoundBuffer_Stop, 4 },
    { "IDirectSoundBuffer_GetStatus",          (void *)IDirectSoundBuffer_GetStatus, 8 },
    { "IDirectSoundBuffer_GetCurrentPosition", (void *)IDirectSoundBuffer_GetCurrentPosition, 12 },
    { "IDirectSoundBuffer_SetCurrentPosition", (void *)IDirectSoundBuffer_SetCurrentPosition, 8 },
    { "IDirectSoundBuffer_SetLoopRegion",      (void *)IDirectSoundBuffer_SetLoopRegion, 12 },
    { "IDirectSoundBuffer_SetVolume",          (void *)IDirectSoundBuffer_SetVolume, 8 },
    { "IDirectSoundBuffer_SetFrequency",       (void *)IDirectSoundBuffer_SetFrequency, 8 },
    { "IDirectSoundBuffer_SetMixBins",         (void *)IDirectSoundBuffer_SetMixBins, 8 },
    { "IDirectSoundBuffer_SetMixBinVolumes_8", (void *)IDirectSoundBuffer_SetMixBinVolumes, 8 },
    { "IDirectSoundBuffer_SetFilter",          (void *)IDirectSoundBuffer_SetFilter, 8 },
    { "IDirectSound_DownloadEffectsImage",     (void *)IDirectSound_DownloadEffectsImage, 20 },
    { "IDirectSound_SetI3DL2Listener",         (void *)IDirectSound_SetI3DL2Listener, 12 },
    { "IDirectSoundBuffer_Release",            (void *)IDirectSoundBuffer_Release, 4 },
    { "IDirectSound_Release",                  (void *)IDirectSound_Release, 4 },
};

void DsndSeam_ReportMissing(void) {
    XbeSeam_ReportMissing(&g_seam);
    DrivingAudio_Report();
}

void Inject_DsndSeam(void) {
    for (size_t i = 0; i < sizeof(g_replacements) / sizeof(g_replacements[0]); i++)
        XbeSeam_Replace(&g_seam, g_replacements[i].name, g_replacements[i].replacement,
                        g_replacements[i].stackBytes);

    XbeSeam_Install(&g_seam);
}
