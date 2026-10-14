#include "DrivingAudio.h"

#include "../../common/sound/xadpcm.h"
#include "../platform/Host.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// The sound output on SDL3 (NIGHTFIRE_SDL): a voice mixer of ours feeding one SDL audio stream. See the header for
// what the voices are. Each voice reads the game's buffer in place - converting PCM or decoding Xbox ADPCM a block
// at a time - at its own rate (SetFrequency) by linear interpolation, and adds it to a stereo float mix with its
// volume and the stereo pair its mixbins make, as the XAudio2 body's source voices do.
//
// The stream asks for audio on SDL's audio thread; the mix is made there, four milliseconds (one chunk) at a time,
// under the lock the game's calls take. A voice's play cursor is where its current chunk began, the write cursor
// where it ended. EA's mixer writes its rings twenty milliseconds ahead of the play cursor, and the output is asked
// for SDL's smallest block, so the mix never reads what has not been written.

#define CHUNK_FRAMES    192u     // four milliseconds at 48 kHz
#define OUTPUT_CHANNELS 2
#define OUTPUT_RATE     48000
#define MAX_MIXBINS     8
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

struct DrivingVoice {
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
    float left, right;                // the mixbins' stereo pair
    double step;                      // frames of the buffer per output frame
    uint32_t frequency;
    uint32_t binCount;
    uint32_t bins[MAX_MIXBINS];
    int32_t binVolumes[MAX_MIXBINS];

    int32_t decodedBlock;             // the ADPCM block in `decoded`, or -1
    int16_t decoded[XADPCM_SAMPLES_PER_BLOCK * 2];

    uint32_t statChunks, statSeeks, statPlays;   // since the last report
};

static SDL_AudioDeviceID Device;
static SDL_AudioStream *Stream;
static HostLock Lock;
static bool StartAttempted;
static DrivingVoice *Voices[MAX_VOICES];
static int VoiceCount;
static uint32_t StatChunks, StatChunksWithSound;

static float HundredthsDbToAmplitude(int32_t hundredths) {
    if (hundredths <= -10000) return 0.0f;
    if (hundredths >= 0) return 1.0f;
    return powf(10.0f, (float)hundredths / 2000.0f);
}

// Where each Xbox mixbin lands on a stereo pair, as in the XAudio2 body (DSMIXBIN_*: 0 FL, 1 FR, 2 C, 3 LFE, 4 BL,
// 5 BR, then the cross-talk bins and the effect sends). The centre and LFE go to both sides at -3 dB.
static void MixBinToStereo(uint32_t bin, float *left, float *right) {
    const float c = 0.7071068f;
    switch (bin) {
        case 0: *left = 1.0f; *right = 0.0f; return;
        case 1: *left = 0.0f; *right = 1.0f; return;
        case 2: *left = c;    *right = c;    return;
        case 3: *left = c;    *right = c;    return;
        case 4: *left = c;    *right = 0.0f; return;
        case 5: *left = 0.0f; *right = c;    return;
        case 6: *left = 1.0f; *right = 0.0f; return;
        case 7: *left = 0.0f; *right = 1.0f; return;
        case 8: *left = c;    *right = 0.0f; return;
        case 9: *left = 0.0f; *right = c;    return;
        default: *left = 0.0f; *right = 0.0f; return;   // effect sends: no dry contribution
    }
}

// The stereo pair from the bins: each bin's level, placed on the pair. Lock held.
static void ComputePair(DrivingVoice *v) {
    float left = 0.0f, right = 0.0f;
    for (uint32_t i = 0; i < v->binCount; i++) {
        float l, r;
        MixBinToStereo(v->bins[i], &l, &r);
        float g = HundredthsDbToAmplitude(v->binVolumes[i]);
        left += l * g;
        right += r * g;
    }
    if (v->binCount == 0) { left = 1.0f; right = 1.0f; }   // no bins set: plain mono to both
    v->left = left > 1.0f ? 1.0f : left;
    v->right = right > 1.0f ? 1.0f : right;
}

void DrivingAudio_StereoFold(float matrix[12]) {
    for (uint32_t speaker = 0; speaker < 6; speaker++)
        MixBinToStereo(speaker, &matrix[speaker * 2], &matrix[speaker * 2 + 1]);
}

// ---- reading a buffer

static uint32_t FramesOf(const DrivingVoice *v, uint32_t bytes) {
    return bytes / v->frameBytes * v->blockFrames;
}

static uint32_t BytesOf(const DrivingVoice *v, uint32_t frame) {
    return frame / v->blockFrames * v->frameBytes;
}

// The frames a voice plays through: its loop region when looping with one set, else the buffer's whole frames
static void Region(const DrivingVoice *v, uint32_t *start, uint32_t *end) {
    *start = 0;
    *end = FramesOf(v, v->dataBytes);
    if (v->looping && v->loopLength != 0 && v->loopStart < v->dataBytes) {
        uint32_t loopEnd = v->loopStart + v->loopLength;
        *start = FramesOf(v, v->loopStart);
        *end = FramesOf(v, loopEnd < v->dataBytes ? loopEnd : v->dataBytes);
    }
}

// One channel's sample of a frame, -1..1. Lock held.
static float Sample(DrivingVoice *v, uint32_t frame, uint32_t channel) {
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
static void MixVoice(DrivingVoice *v, float *mix, uint32_t frames) {
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

bool DrivingAudio_Start(void) {
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

uint32_t DrivingAudio_GetDevice(void) {
    return Device;
}

// ---- voices

DrivingVoice *DrivingAudio_CreateVoice(uint32_t sampleRate, uint16_t formatTag, uint16_t channels,
                                       uint16_t blockAlign, uint16_t bitsPerSample) {
    if (Stream == NULL)
        return NULL;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    if (formatTag != WAVE_FORMAT_PCM && formatTag != WAVE_FORMAT_XBOX_ADPCM) {
        AudioLog("[sdl-audio] buffer format 0x%04x is not PCM or Xbox ADPCM; it will be silent\n", formatTag);
        return NULL;
    }

    DrivingVoice *v = new DrivingVoice();
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
    v->decodedBlock = -1;
    ComputePair(v);
    HostEnterLock(&Lock);
    if (VoiceCount < MAX_VOICES)
        Voices[VoiceCount++] = v;
    HostLeaveLock(&Lock);
    return v;
}

void DrivingAudio_Release(DrivingVoice *v) {
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

void DrivingAudio_SetData(DrivingVoice *v, const void *data, uint32_t bytes) {
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

void DrivingAudio_Play(DrivingVoice *v, bool looping) {
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

void DrivingAudio_Stop(DrivingVoice *v) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    if (v->playing)
        v->stoppedAt = v->playPos;
    v->playing = false;
    HostLeaveLock(&Lock);
}

bool DrivingAudio_IsPlaying(DrivingVoice *v) {
    if (v == NULL)
        return false;
    HostEnterLock(&Lock);
    bool playing = v->playing;
    HostLeaveLock(&Lock);
    return playing;
}

void DrivingAudio_GetPosition(DrivingVoice *v, uint32_t *playCursor, uint32_t *writeCursor) {
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

void DrivingAudio_SetPosition(DrivingVoice *v, uint32_t bytes) {
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

void DrivingAudio_SetLoopRegion(DrivingVoice *v, uint32_t startBytes, uint32_t lengthBytes) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->loopStart = startBytes;
    v->loopLength = lengthBytes;
    HostLeaveLock(&Lock);
}

void DrivingAudio_SetVolume(DrivingVoice *v, int32_t hundredthsDb) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->volume = HundredthsDbToAmplitude(hundredthsDb);
    HostLeaveLock(&Lock);
}

// The XAudio2 body's limits on the ratio: four times up, 1/2000 down
void DrivingAudio_SetFrequency(DrivingVoice *v, uint32_t hz) {
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

void DrivingAudio_SetMixBins(DrivingVoice *v, const uint32_t *bins, const int32_t *volumes, uint32_t count) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    v->binCount = 0;
    for (uint32_t i = 0; i < count && v->binCount < MAX_MIXBINS; i++) {
        v->bins[v->binCount] = bins[i];
        v->binVolumes[v->binCount] = volumes != NULL ? volumes[i] : 0;
        v->binCount++;
    }
    ComputePair(v);
    HostLeaveLock(&Lock);
}

void DrivingAudio_SetMixBinVolumes(DrivingVoice *v, const uint32_t *bins, const int32_t *volumes, uint32_t count) {
    if (v == NULL)
        return;
    HostEnterLock(&Lock);
    for (uint32_t i = 0; i < count; i++) {
        bool found = false;
        for (uint32_t k = 0; k < v->binCount; k++) {
            if (v->bins[k] == bins[i]) {
                found = true;
                v->binVolumes[k] = volumes[i];
            }
        }
        if (!found && v->binCount < MAX_MIXBINS) {   // a bin not assigned yet: take the assignment too
            v->bins[v->binCount] = bins[i];
            v->binVolumes[v->binCount] = volumes[i];
            v->binCount++;
        }
    }
    ComputePair(v);
    HostLeaveLock(&Lock);
}

void DrivingAudio_Report(void) {
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
        DrivingVoice *top = NULL;
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
