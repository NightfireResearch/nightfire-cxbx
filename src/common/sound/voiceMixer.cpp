#include "voiceMixer.h"

#include "xadpcm.h"
#include "../../driving/platform/Host.h"

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// The stream asks for audio on SDL's audio thread; the mix is made there, four milliseconds (one chunk) at a time,
// under the lock the games' calls take. A voice's play cursor is where its current chunk began, the write cursor
// where it ended. The driving engine's mixer writes its rings twenty milliseconds ahead of the play cursor, and the
// output is asked for SDL's smallest block, so the mix never reads what has not been written.

#define CHUNK_FRAMES    192u     // four milliseconds at 48 kHz
#define OUTPUT_CHANNELS 2
#define OUTPUT_RATE     48000
#define MAX_VOICES      256

#ifndef WAVE_FORMAT_PCM
#define WAVE_FORMAT_PCM 0x0001
#endif
#define WAVE_FORMAT_XBOX_ADPCM 0x0069

static void AudioLog(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

struct MixerVoice {
    uint32_t sampleRate;
    uint16_t formatTag, channels, blockAlign, bitsPerSample;
    uint32_t frameBytes;              // PCM: one frame; ADPCM: one block
    uint32_t blockFrames;             // frames per frameBytes: 1, or 64 for ADPCM

    const uint8_t *data;
    uint32_t dataBytes;

    bool playing;
    bool looping;
    double position;                  // frames into the buffer, with the fraction resampling leaves
    uint32_t playPos;                 // bytes: where the chunk mixed last began
    uint32_t readPos;                 // bytes: where it ended
    uint32_t stoppedAt;               // where a Stop left the cursor; Play resumes from it
    uint32_t loopStart, loopLength;   // bytes; length 0 = the whole buffer

    float volume;                     // amplitude
    float left, right;                // the gains onto the output's two sides
    double step;                      // frames of the buffer per output frame
    uint32_t frequency;

    int32_t decodedBlock;             // the ADPCM block in `decoded`, or -1
    int16_t decoded[XADPCM_SAMPLES_PER_BLOCK * 2];

    uint32_t statChunks, statSeeks, statPlays;   // since the last report
};

static SDL_AudioDeviceID Device;
static SDL_AudioStream *Stream;
static HostLock Lock;
static bool StartAttempted;
static MixerVoice *Voices[MAX_VOICES];
static int VoiceCount;
static uint32_t StatChunks, StatChunksWithSound;

// ---- reading a buffer

static uint32_t FramesOf(const MixerVoice *v, uint32_t bytes) {
    return bytes / v->frameBytes * v->blockFrames;
}

static uint32_t BytesOf(const MixerVoice *v, uint32_t frame) {
    return frame / v->blockFrames * v->frameBytes;
}

// The frames a voice plays through: its loop region when looping with one set, else the buffer's whole frames
static void Region(const MixerVoice *v, uint32_t *start, uint32_t *end) {
    *start = 0;
    *end = FramesOf(v, v->dataBytes);
    if (v->looping && v->loopLength != 0 && v->loopStart < v->dataBytes) {
        uint32_t loopEnd = v->loopStart + v->loopLength;
        *start = FramesOf(v, v->loopStart);
        *end = FramesOf(v, loopEnd < v->dataBytes ? loopEnd : v->dataBytes);
    }
}

// One channel's sample of a frame, -1..1. Lock held.
static float Sample(MixerVoice *v, uint32_t frame, uint32_t channel) {
    if (v->formatTag == WAVE_FORMAT_XBOX_ADPCM) {
        int32_t block = (int32_t)(frame / XADPCM_SAMPLES_PER_BLOCK);
        if (block != v->decodedBlock) {
            XAdpcm_Decode(v->data + (uint32_t)block * v->frameBytes, v->frameBytes, v->channels, v->decoded,
                          sizeof(v->decoded) / sizeof(v->decoded[0]));
            v->decodedBlock = block;
        }
        return v->decoded[frame % XADPCM_SAMPLES_PER_BLOCK * v->channels + channel] * (1.0f / 32768.0f);
    }
    if (v->bitsPerSample == 8)
        return (v->data[frame * v->channels + channel] - 128) * (1.0f / 128.0f);
    int16_t value;
    memcpy(&value, v->data + (frame * v->channels + channel) * 2, sizeof(value));
    return value * (1.0f / 32768.0f);
}

// Adds a chunk of the voice into the mix. A one-shot that reaches its end stops, and plays from the start next time.
// Lock held.
static void MixVoice(MixerVoice *v, float *mix, uint32_t frames) {
    uint32_t start, end;
    Region(v, &start, &end);
    if (v->data == NULL || end <= start) {
        v->playing = false;
        return;
    }
    if (v->position >= end)
        v->position = v->looping ? start : end;
    v->playPos = BytesOf(v, (uint32_t)v->position);

    bool audible = false;
    for (uint32_t i = 0; i < frames; i++) {
        if (v->position >= end) {
            if (!v->looping) {
                v->playing = false;
                v->stoppedAt = 0;
                v->position = 0.0;
                break;
            }
            v->position -= end - start;
            if (v->position < start)
                v->position = start;
        }
        uint32_t frame = (uint32_t)v->position;
        float fraction = (float)(v->position - frame);
        uint32_t next = frame + 1;
        if (next >= end)
            next = v->looping ? start : frame;
        float l = Sample(v, frame, 0), r = l;
        float nl = Sample(v, next, 0), nr = nl;
        if (v->channels == 2) {
            r = Sample(v, frame, 1);
            nr = Sample(v, next, 1);
        }
        l += (nl - l) * fraction;
        r += (nr - r) * fraction;
        if (l > 0.002f || l < -0.002f)
            audible = true;
        mix[i * 2] += l * v->volume * v->left;
        mix[i * 2 + 1] += r * v->volume * v->right;
        v->position += v->step;
    }
    v->readPos = BytesOf(v, (uint32_t)v->position);
    v->statChunks++;
    StatChunks++;
    if (audible)
        StatChunksWithSound++;
}

// SDL's audio thread: whole chunks, until the stream has what it asked for.
static void SDLCALL FeedStream(void *userdata, SDL_AudioStream *stream, int additionalAmount, int totalAmount) {
    (void)userdata;
    (void)totalAmount;
    float mix[CHUNK_FRAMES * OUTPUT_CHANNELS];
    for (int given = 0; given < additionalAmount; given += (int)sizeof(mix)) {
        memset(mix, 0, sizeof(mix));
        HostEnterLock(&Lock);
        for (int i = 0; i < VoiceCount; i++)
            if (Voices[i]->playing)
                MixVoice(Voices[i], mix, CHUNK_FRAMES);
        HostLeaveLock(&Lock);
        for (float &sample : mix)
            sample = sample > 1.0f ? 1.0f : sample < -1.0f ? -1.0f : sample;
        SDL_PutAudioStreamData(stream, mix, sizeof(mix));
    }
}

// ---- the device

bool VoiceMixer_Start(void) {
    if (StartAttempted)
        return Stream != NULL;
    StartAttempted = true;
    HostInitLock(&Lock);

    // The window opened SDL's audio already (platform/HostWindowSdl.cpp); this keeps a count of its own.
    SDL_SetHint(SDL_HINT_AUDIO_DEVICE_SAMPLE_FRAMES, "192");
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        AudioLog("[sdl-audio] SDL audio could not start: %s - no audio\n", SDL_GetError());
        return false;
    }
    // A device of our own, with the mix's stream bound to it, rather than SDL_OpenAudioDeviceStream's: the movies
    // bind a stream of theirs to the same device (common/fmv/FmvAudioSdl.cpp), which that kind refuses.
    SDL_AudioSpec spec = { SDL_AUDIO_F32, OUTPUT_CHANNELS, OUTPUT_RATE };
    Device = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (Device == 0) {
        AudioLog("[sdl-audio] no audio device: %s - no audio\n", SDL_GetError());
        return false;
    }
    Stream = SDL_CreateAudioStream(&spec, &spec);
    if (Stream == NULL || !SDL_SetAudioStreamGetCallback(Stream, FeedStream, NULL) ||
        !SDL_BindAudioStream(Device, Stream)) {
        AudioLog("[sdl-audio] the mix's stream could not be made: %s - no audio\n", SDL_GetError());
        SDL_DestroyAudioStream(Stream);
        Stream = NULL;
        SDL_CloseAudioDevice(Device);
        Device = 0;
        return false;
    }
    SDL_ResumeAudioDevice(Device);
    int deviceFrames = 0;
    SDL_AudioSpec device;
    SDL_GetAudioDeviceFormat(Device, &device, &deviceFrames);
    AudioLog("[sdl-audio] audio device open (%s): %d channels at %d Hz, %d-frame blocks; mixing %u-frame chunks\n",
             SDL_GetCurrentAudioDriver(), device.channels, device.freq, deviceFrames, CHUNK_FRAMES);
    return true;
}

uint32_t VoiceMixer_Device(void) {
    return Device;
}

// ---- voices

MixerVoice *VoiceMixer_CreateVoice(uint32_t sampleRate, uint16_t formatTag, uint16_t channels,
                                   uint16_t blockAlign, uint16_t bitsPerSample) {
    if (Stream == NULL)
        return NULL;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    if (formatTag != WAVE_FORMAT_PCM && formatTag != WAVE_FORMAT_XBOX_ADPCM) {
        AudioLog("[sdl-audio] buffer format 0x%04x is not PCM or Xbox ADPCM; it will be silent\n", formatTag);
        return NULL;
    }

    MixerVoice *v = new MixerVoice();
    v->sampleRate = sampleRate;
    v->formatTag = formatTag;
    v->channels = channels;
    v->blockAlign = blockAlign;
    v->bitsPerSample = bitsPerSample != 0 ? bitsPerSample : 16;
    if (formatTag == WAVE_FORMAT_XBOX_ADPCM) {
        v->frameBytes = (uint32_t)XAdpcm_BlockBytes(channels);
        v->blockFrames = XADPCM_SAMPLES_PER_BLOCK;
    } else {
        v->frameBytes = channels * (v->bitsPerSample / 8);
        v->blockFrames = 1;
    }
    v->frequency = sampleRate;
    v->step = (double)sampleRate / OUTPUT_RATE;
    v->volume = 1.0f;
    v->left = v->right = 1.0f;
    v->decodedBlock = -1;
    HostEnterLock(&Lock);
    if (VoiceCount < MAX_VOICES)
        Voices[VoiceCount++] = v;
    HostLeaveLock(&Lock);
    return v;
}

void VoiceMixer_Release(MixerVoice *v) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    for (int i = 0; i < VoiceCount; i++) {
        if (Voices[i] == v) {
            Voices[i] = Voices[--VoiceCount];
            break;
        }
    }
    HostLeaveLock(&Lock);
    delete v;
}

void VoiceMixer_SetData(MixerVoice *v, const void *data, uint32_t bytes) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->data = static_cast<const uint8_t *>(data);
    v->dataBytes = data != NULL ? bytes : 0;
    v->position = 0.0;
    v->readPos = v->playPos = v->stoppedAt = 0;
    v->loopStart = v->loopLength = 0;
    v->playing = false;
    v->decodedBlock = -1;
    HostLeaveLock(&Lock);
}

void VoiceMixer_Play(MixerVoice *v, bool looping) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->statPlays++;
    v->playing = true;
    v->looping = looping;
    uint32_t from = v->stoppedAt < v->dataBytes ? v->stoppedAt : 0;
    v->position = FramesOf(v, from);
    v->readPos = v->playPos = from;
    v->decodedBlock = -1;
    HostLeaveLock(&Lock);
}

void VoiceMixer_Stop(MixerVoice *v) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    if (v->playing)
        v->stoppedAt = v->playPos;
    v->playing = false;
    HostLeaveLock(&Lock);
}

bool VoiceMixer_IsPlaying(MixerVoice *v) {
    if (v == NULL)
        return false;
    HostEnterLock(&Lock);
    bool playing = v->playing;
    HostLeaveLock(&Lock);
    return playing;
}

void VoiceMixer_GetPosition(MixerVoice *v, uint32_t *playCursor, uint32_t *writeCursor) {
    uint32_t play = 0, write = 0;
    if (v != NULL) {
        HostEnterLock(&Lock);
        play = v->playing ? v->playPos : v->stoppedAt;
        write = v->playing ? v->readPos : v->stoppedAt;
        HostLeaveLock(&Lock);
    }
    if (playCursor != NULL) *playCursor = play;
    if (writeCursor != NULL) *writeCursor = write;
}

void VoiceMixer_SetPosition(MixerVoice *v, uint32_t bytes) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    bytes -= bytes % v->frameBytes;
    v->stoppedAt = bytes;
    v->statSeeks++;
    if (v->playing) {
        v->position = FramesOf(v, bytes);
        v->readPos = v->playPos = bytes;
        v->decodedBlock = -1;
    }
    HostLeaveLock(&Lock);
}

void VoiceMixer_SetLoopRegion(MixerVoice *v, uint32_t startBytes, uint32_t lengthBytes) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->loopStart = startBytes;
    v->loopLength = lengthBytes;
    HostLeaveLock(&Lock);
}

void VoiceMixer_SetVolume(MixerVoice *v, float amplitude) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->volume = amplitude;
    HostLeaveLock(&Lock);
}

void VoiceMixer_SetFrequency(MixerVoice *v, uint32_t hz) {
    if (v == NULL || hz == 0)
        return;
    HostEnterLock(&Lock);
    v->frequency = hz;
    double ratio = (double)hz / v->sampleRate;
    if (ratio > 4.0) ratio = 4.0;
    if (ratio < 0.0005) ratio = 0.0005;
    v->step = ratio * v->sampleRate / OUTPUT_RATE;
    HostLeaveLock(&Lock);
}

void VoiceMixer_SetGains(MixerVoice *v, float left, float right) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->left = left;
    v->right = right;
    HostLeaveLock(&Lock);
}

void VoiceMixer_Report(void) {
    if (Stream == NULL)
        return;
    HostEnterLock(&Lock);
    int playing = 0;
    for (int i = 0; i < VoiceCount; i++)
        if (Voices[i]->playing)
            playing++;
    AudioLog("[sdl-audio] %u chunks mixed, %u with sound; %d voices, %d playing\n", StatChunks, StatChunksWithSound,
             VoiceCount, playing);
    StatChunks = StatChunksWithSound = 0;
    // The three busiest voices
    for (int n = 0; n < 3; n++) {
        MixerVoice *top = NULL;
        for (int i = 0; i < VoiceCount; i++)
            if (Voices[i]->statChunks > 0 && (top == NULL || Voices[i]->statChunks > top->statChunks))
                top = Voices[i];
        if (top == NULL)
            break;
        AudioLog("[sdl-audio]   voice %p: %u chunks, %u plays, %u seeks; %s %u bytes, %s, loop %u+%u, %u Hz, read %u\n",
                 (void *)top, top->statChunks, top->statPlays, top->statSeeks,
                 top->formatTag == WAVE_FORMAT_XBOX_ADPCM ? "adpcm" : "pcm", top->dataBytes,
                 top->looping ? "looping" : "one-shot", top->loopStart, top->loopLength, top->frequency, top->readPos);
        top->statChunks = 0;
    }
    for (int i = 0; i < VoiceCount; i++)
        Voices[i]->statChunks = Voices[i]->statSeeks = Voices[i]->statPlays = 0;
    HostLeaveLock(&Lock);
}
