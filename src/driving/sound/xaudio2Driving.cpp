#include "xaudio2Driving.h"

#include "../../common/sound/xadpcm.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// MSVC acts on this; other linkers get the library from the CMake target instead. Guarded because
// clang emits the directive into .drectve regardless, and GNU ld then warns that it cannot read it.
#ifdef _MSC_VER
#pragma comment(lib, "xaudio2.lib")
#endif

// See the header for the shape of this. The numbers that matter:
//
//  - a chunk is 192 samples per channel: four milliseconds at 48 kHz, and exactly three Xbox ADPCM blocks,
//    so an ADPCM chunk is 108 bytes of the game's memory and never straddles a block;
//  - two chunks are kept queued, so the read cursor runs about eight milliseconds ahead of the play cursor.
//    EA's mixer writes twenty milliseconds ahead, from a 100 Hz thread; a deeper queue would read what it
//    has not written yet and play last time's audio.
//
// And the rule that took longest to learn: NEVER FLUSH A VOICE THAT WILL BE FED AGAIN. The game plays a
// sound, sets its frequency and plays it again within one tick; the first version of this stopped and
// flushed the voice on the second Play and refilled it. From then on XAudio2 ended every chunk the voice
// was given the instant it was submitted, without playing it - a million chunks a second, its own thread
// saturated, every other sound frozen for as long as it lasted, which was the sound dropping out while
// steering and going for good once a looping voice got into that state. Waiting for the flush's own ends
// before refilling made no difference. So FlushSourceBuffers is used once, when a voice is destroyed. A
// restart or a seek changes the generation and where the next chunk is read from, and the two chunks
// already queued - eight milliseconds - play out first; a Stop leaves them queued for the resume. That is
// a shorter delay than the console's own hardware voices had, and it cannot spin.

#define CHUNK_SAMPLES   192u
#define CHUNK_QUEUE     2
#define CHUNK_SLOTS     4
#define OUTPUT_CHANNELS 2
#define OUTPUT_RATE     48000
#define MAX_MIXBINS     8

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

// ---------------------------------------------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------------------------------------------

static IXAudio2 *g_xaudio = NULL;
static IXAudio2MasteringVoice *g_master = NULL;
static bool g_startAttempted = false;
static CRITICAL_SECTION g_lock;

static uint32_t g_statChunks = 0, g_statChunksWithSound = 0;   // since the last report
static int g_voicesAlive = 0;
#define MAX_VOICES 256
static DrivingVoice *g_voices[MAX_VOICES];
static int g_voiceCount = 0;

bool DrivingAudio_Start(void) {
    if (g_startAttempted)
        return g_xaudio != NULL;
    g_startAttempted = true;
    InitializeCriticalSection(&g_lock);

    // XAudio2 is COM underneath; standalone, nobody has initialised it on this thread. RPC_E_CHANGED_MODE
    // only means it was already up in the other model, which XAudio2 does not mind.
    HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        AudioLog("[xa2] CoInitializeEx failed: 0x%08lx\n", com);

    HRESULT hr = XAudio2Create(&g_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        AudioLog("[xa2] XAudio2Create failed: 0x%08lx - no audio\n", hr);
        g_xaudio = NULL;
        return false;
    }
    hr = g_xaudio->CreateMasteringVoice(&g_master, OUTPUT_CHANNELS, OUTPUT_RATE);
    if (FAILED(hr)) {
        AudioLog("[xa2] CreateMasteringVoice failed: 0x%08lx - no audio\n", hr);
        g_xaudio->Release();
        g_xaudio = NULL;
        g_master = NULL;
        return false;
    }
    AudioLog("[xa2] audio device open: %d channels at %d Hz, streaming %u-sample chunks\n",
             OUTPUT_CHANNELS, OUTPUT_RATE, CHUNK_SAMPLES);
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Voices
// ---------------------------------------------------------------------------------------------------------------

// A chunk slot belongs to XAudio2 from the moment it is submitted until OnBufferEnd hands it back - and
// that includes a chunk discarded by a flush, whose end arrives later on the audio thread. Only the callback
// frees a slot; a restart may briefly have the old two and the new two in flight together, which is what
// the four slots are for.
struct Chunk {
    int16_t pcm[CHUNK_SAMPLES * 2];   // room for stereo, though every buffer here is mono
    uint32_t srcOffset;               // where in the buffer it came from, in the buffer's own bytes
    uint32_t srcBytes;
    uint32_t generation;              // the Play it belongs to; an older one's cursor is not reported
    bool queued;                      // held by XAudio2
};

struct DrivingVoice : public IXAudio2VoiceCallback {
    IXAudio2SourceVoice *voice;
    uint32_t sampleRate;
    uint16_t formatTag, channels, blockAlign, bitsPerSample;
    uint32_t chunkSrcBytes;           // bytes of the game's buffer one chunk consumes

    const uint8_t *data;
    uint32_t dataBytes;

    bool playing;
    bool looping;
    bool ending;                      // the last chunk of a one-shot has been submitted
    uint32_t generation;
    uint32_t readPos;                 // next byte to read from the buffer
    uint32_t playPos;                 // start of the chunk sounding now
    uint32_t stoppedAt;               // where a Stop left the cursor; Play resumes from it
    uint32_t loopStart, loopLength;   // bytes; length 0 = the whole buffer
    int queued;                       // chunks XAudio2 holds, by our count

    Chunk chunks[CHUNK_SLOTS];

    int32_t volume;                   // hundredths of a dB
    uint32_t frequency;               // Hz
    uint32_t binCount;
    uint32_t bins[MAX_MIXBINS];
    int32_t binVolumes[MAX_MIXBINS];
    uint32_t statChunks, statSeeks, statPlays;   // since the last report

    // XAudio2's callbacks, on its thread.
    void __stdcall OnBufferStart(void *context) override;
    void __stdcall OnBufferEnd(void *context) override;
    void __stdcall OnStreamEnd() override;
    void __stdcall OnVoiceProcessingPassStart(UINT32) override {}
    void __stdcall OnVoiceProcessingPassEnd() override {}
    void __stdcall OnLoopEnd(void *) override {}
    void __stdcall OnVoiceError(void *, HRESULT error) override {
        static int said = 0;
        if (said++ < 8) AudioLog("[xa2] voice %p error 0x%08lx\n", (void*)this, error);
    }
};

static float HundredthsDbToAmplitude(int32_t hundredths) {
    if (hundredths <= -10000) return 0.0f;
    if (hundredths >= 0) return 1.0f;
    return powf(10.0f, (float)hundredths / 2000.0f);
}

// Where each Xbox mixbin lands on a stereo pair (DSMIXBIN_*: 0 FL, 1 FR, 2 C, 3 LFE, 4 BL, 5 BR, then the
// cross-talk bins and the effect sends). The centre and LFE bins go to both sides at -3 dB: the mixer puts
// dialogue on the centre and bass on the LFE, and a stereo listener wants both.
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

// The output matrix from the bins: each bin's level, placed on the pair. Lock held.
static void ComputeMatrix(const DrivingVoice *v, float matrix[OUTPUT_CHANNELS]) {
    float left = 0.0f, right = 0.0f;
    for (uint32_t i = 0; i < v->binCount; i++) {
        float l, r;
        MixBinToStereo(v->bins[i], &l, &r);
        float g = HundredthsDbToAmplitude(v->binVolumes[i]);
        left += l * g;
        right += r * g;
    }
    if (v->binCount == 0) { left = 1.0f; right = 1.0f; }   // no bins set: plain mono to both
    matrix[0] = left > 1.0f ? 1.0f : left;
    matrix[1] = right > 1.0f ? 1.0f : right;
}

static void ApplyMatrix(DrivingVoice *v) {
    float matrix[OUTPUT_CHANNELS];
    EnterCriticalSection(&g_lock);
    ComputeMatrix(v, matrix);
    IXAudio2SourceVoice *voice = v->voice;
    LeaveCriticalSection(&g_lock);
    if (voice != NULL)
        voice->SetOutputMatrix(NULL, v->channels, OUTPUT_CHANNELS, matrix);
}

DrivingVoice *DrivingAudio_CreateVoice(uint32_t sampleRate, uint16_t formatTag, uint16_t channels,
                                       uint16_t blockAlign, uint16_t bitsPerSample) {
    if (g_xaudio == NULL)
        return NULL;
    if (channels < 1) channels = 1;
    if (channels > 2) channels = 2;
    if (formatTag != WAVE_FORMAT_PCM && formatTag != WAVE_FORMAT_XBOX_ADPCM) {
        AudioLog("[xa2] buffer format 0x%04x is not PCM or Xbox ADPCM; it will be silent\n", formatTag);
        return NULL;
    }

    DrivingVoice *v = new DrivingVoice();   // value-initialised: every member zero, and the vtable in place
    v->sampleRate = sampleRate;
    v->formatTag = formatTag;
    v->channels = channels;
    v->blockAlign = blockAlign;
    v->bitsPerSample = bitsPerSample != 0 ? bitsPerSample : 16;
    v->frequency = sampleRate;
    v->volume = 0;
    if (formatTag == WAVE_FORMAT_XBOX_ADPCM)
        v->chunkSrcBytes = (uint32_t)XAdpcm_BlockBytes(channels) * (CHUNK_SAMPLES / XADPCM_SAMPLES_PER_BLOCK);
    else
        v->chunkSrcBytes = CHUNK_SAMPLES * channels * (v->bitsPerSample / 8);

    // The source voice takes decoded 16-bit PCM at the buffer's rate; SetFrequency becomes a ratio against
    // it. Four is the largest ratio the game could reasonably ask for.
    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = channels;
    wfx.nSamplesPerSec = sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(channels * 2);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    HRESULT hr = g_xaudio->CreateSourceVoice(&v->voice, &wfx, 0, 4.0f, v, NULL, NULL);
    if (FAILED(hr)) {
        AudioLog("[xa2] CreateSourceVoice failed: 0x%08lx\n", hr);
        delete v;
        return NULL;
    }
    ApplyMatrix(v);
    EnterCriticalSection(&g_lock);
    if (g_voiceCount < MAX_VOICES) g_voices[g_voiceCount++] = v;
    g_voicesAlive++;
    LeaveCriticalSection(&g_lock);
    return v;
}

void DrivingAudio_Release(DrivingVoice *v) {
    if (v == NULL)
        return;
    IXAudio2SourceVoice *voice;
    EnterCriticalSection(&g_lock);
    v->playing = false;
    v->generation++;
    voice = v->voice;
    v->voice = NULL;
    for (int i = 0; i < g_voiceCount; i++)
        if (g_voices[i] == v) { g_voices[i] = g_voices[--g_voiceCount]; break; }
    g_voicesAlive--;
    LeaveCriticalSection(&g_lock);
    if (voice != NULL) {
        voice->Stop(0);
        voice->FlushSourceBuffers();
        voice->DestroyVoice();   // waits for any callback in flight
    }
    delete v;
}

void DrivingAudio_SetData(DrivingVoice *v, const void *data, uint32_t bytes) {
    if (v == NULL)
        return;
    IXAudio2SourceVoice *voice;
    EnterCriticalSection(&g_lock);
    v->data = (const uint8_t *)data;
    v->dataBytes = (data != NULL) ? bytes : 0;
    v->readPos = v->playPos = v->stoppedAt = 0;
    v->loopStart = v->loopLength = 0;
    bool wasPlaying = v->playing;
    v->playing = false;
    v->generation++;
    voice = v->voice;
    LeaveCriticalSection(&g_lock);
    // Whatever is queued is the old sound, and plays out - at most eight milliseconds of it - before the
    // new one, rather than being flushed (see the note at the top).
    if (wasPlaying && voice != NULL)
        voice->Stop(0);
}

// Fills a chunk slot from the buffer at readPos and advances readPos, wrapping or ending as the mode says.
// Lock held. Returns false when there is nothing left to submit (a one-shot at its end, or no data).
static bool PrepareChunk(DrivingVoice *v, Chunk *c, bool *lastOfStream) {
    *lastOfStream = false;
    if (v->data == NULL || v->dataBytes == 0 || v->ending)
        return false;

    uint32_t regionStart = 0, regionEnd = v->dataBytes;
    if (v->looping && v->loopLength != 0 && v->loopStart < v->dataBytes) {
        regionStart = v->loopStart;
        regionEnd = v->loopStart + v->loopLength;
        if (regionEnd > v->dataBytes) regionEnd = v->dataBytes;
    }
    if (v->readPos >= regionEnd) {
        if (!v->looping) return false;
        v->readPos = regionStart;
    }
    uint32_t avail = regionEnd - v->readPos;
    uint32_t take = v->chunkSrcBytes;
    if (take > avail) take = avail;
    if (v->formatTag == WAVE_FORMAT_XBOX_ADPCM) {
        uint32_t block = (uint32_t)XAdpcm_BlockBytes(v->channels);
        take -= take % block;
        if (take == 0) {   // a trailing partial block: the hardware would not play it either
            if (!v->looping) return false;
            v->readPos = regionStart;
            return PrepareChunk(v, c, lastOfStream);
        }
    } else {
        uint32_t frame = (uint32_t)v->channels * (v->bitsPerSample / 8);
        take -= take % frame;
        if (take == 0) return false;
    }

    c->srcOffset = v->readPos;
    c->srcBytes = take;
    c->generation = v->generation;
    const uint8_t *src = v->data + v->readPos;
    size_t values;
    if (v->formatTag == WAVE_FORMAT_XBOX_ADPCM) {
        values = XAdpcm_Decode(src, take, v->channels, c->pcm, sizeof(c->pcm) / sizeof(c->pcm[0]));
    } else if (v->bitsPerSample == 8) {
        for (uint32_t i = 0; i < take; i++) c->pcm[i] = (int16_t)((src[i] - 128) << 8);
        values = take;
    } else {
        memcpy(c->pcm, src, take);
        values = take / 2;
    }
    g_statChunks++;
    v->statChunks++;
    for (size_t i = 0; i < values; i += 16)
        if (c->pcm[i] > 64 || c->pcm[i] < -64) { g_statChunksWithSound++; break; }
    v->readPos += take;
    if (!v->looping && v->readPos >= regionEnd) {
        v->ending = true;
        *lastOfStream = true;
    }
    return true;
}

static uint32_t ChunkSampleCount(const DrivingVoice *v, const Chunk *c) {
    if (v->formatTag == WAVE_FORMAT_XBOX_ADPCM)
        return (uint32_t)XAdpcm_DecodedSamplesPerChannel(c->srcBytes, v->channels);
    return c->srcBytes / ((uint32_t)v->channels * (v->bitsPerSample / 8));
}

// Takes a free slot, fills it, and submits it. Lock must NOT be held by the caller: it is taken here for the
// bookkeeping and released before XAudio2 is called. Returns false if nothing was submitted.
static bool SubmitNextChunk(DrivingVoice *v) {
    Chunk *c = NULL;
    bool last = false;
    IXAudio2SourceVoice *voice = NULL;
    uint32_t samples = 0;
    EnterCriticalSection(&g_lock);
    if (v->playing && v->voice != NULL && v->queued < CHUNK_QUEUE) {
        for (int i = 0; i < CHUNK_SLOTS; i++)
            if (!v->chunks[i].queued) { c = &v->chunks[i]; break; }
        if (c != NULL && PrepareChunk(v, c, &last)) {
            c->queued = true;
            v->queued++;
            voice = v->voice;
            samples = ChunkSampleCount(v, c);
        } else {
            c = NULL;
        }
    }
    LeaveCriticalSection(&g_lock);
    if (c == NULL)
        return false;

    XAUDIO2_BUFFER buffer;
    memset(&buffer, 0, sizeof(buffer));
    buffer.AudioBytes = samples * v->channels * 2;
    buffer.pAudioData = (const BYTE *)c->pcm;
    buffer.pContext = c;
    buffer.Flags = last ? XAUDIO2_END_OF_STREAM : 0;
    HRESULT hr = voice->SubmitSourceBuffer(&buffer);
    if (FAILED(hr)) {
        EnterCriticalSection(&g_lock);
        c->queued = false;
        v->queued--;
        LeaveCriticalSection(&g_lock);
        return false;
    }
    return true;
}

static void FillQueue(DrivingVoice *v) {
    for (int i = 0; i < CHUNK_QUEUE; i++)
        if (!SubmitNextChunk(v)) break;
}

void __stdcall DrivingVoice::OnBufferStart(void *context) {
    Chunk *c = (Chunk *)context;
    EnterCriticalSection(&g_lock);
    if (c != NULL && c->generation == generation)
        playPos = c->srcOffset;
    LeaveCriticalSection(&g_lock);
}

void __stdcall DrivingVoice::OnBufferEnd(void *context) {
    Chunk *c = (Chunk *)context;
    EnterCriticalSection(&g_lock);
    if (c != NULL && c->queued) { c->queued = false; queued--; }
    bool alive = voice != NULL;
    LeaveCriticalSection(&g_lock);
    if (alive)
        FillQueue(this);   // does nothing unless the voice is playing
}

void __stdcall DrivingVoice::OnStreamEnd() {
    EnterCriticalSection(&g_lock);
    playing = false;
    ending = false;
    stoppedAt = 0;
    LeaveCriticalSection(&g_lock);
}

void DrivingAudio_Play(DrivingVoice *v, bool looping) {
    if (v == NULL)
        return;
    IXAudio2SourceVoice *voice;
    bool restart;
    EnterCriticalSection(&g_lock);
    restart = v->playing;
    v->statPlays++;
    v->playing = true;
    v->looping = looping;
    v->ending = false;
    v->generation++;
    v->readPos = v->stoppedAt;
    if (v->readPos >= v->dataBytes) v->readPos = 0;
    v->playPos = v->readPos;
    voice = v->voice;
    LeaveCriticalSection(&g_lock);
    if (voice == NULL)
        return;
    (void)restart;       // the chunks already queued play out first; nothing is flushed
    FillQueue(v);
    voice->Start(0);
}

void DrivingAudio_Stop(DrivingVoice *v) {
    if (v == NULL)
        return;
    IXAudio2SourceVoice *voice;
    EnterCriticalSection(&g_lock);
    v->stoppedAt = v->playing ? v->playPos : v->stoppedAt;
    v->playing = false;
    v->ending = false;
    v->generation++;
    voice = v->voice;
    LeaveCriticalSection(&g_lock);
    if (voice != NULL)
        voice->Stop(0);   // what is queued stays queued, for the resume
}

bool DrivingAudio_IsPlaying(DrivingVoice *v) {
    if (v == NULL)
        return false;
    EnterCriticalSection(&g_lock);
    bool playing = v->playing;
    LeaveCriticalSection(&g_lock);
    return playing;
}

void DrivingAudio_GetPosition(DrivingVoice *v, uint32_t *playCursor, uint32_t *writeCursor) {
    uint32_t play = 0, write = 0;
    if (v != NULL) {
        EnterCriticalSection(&g_lock);
        play = v->playing ? v->playPos : v->stoppedAt;
        write = v->playing ? v->readPos : v->stoppedAt;
        LeaveCriticalSection(&g_lock);
    }
    if (playCursor != NULL) *playCursor = play;
    if (writeCursor != NULL) *writeCursor = write;
}

void DrivingAudio_SetPosition(DrivingVoice *v, uint32_t bytes) {
    if (v == NULL)
        return;
    bool playing;
    IXAudio2SourceVoice *voice;
    EnterCriticalSection(&g_lock);
    if (v->formatTag == WAVE_FORMAT_XBOX_ADPCM) {
        uint32_t block = (uint32_t)XAdpcm_BlockBytes(v->channels);
        bytes -= bytes % block;
    }
    v->stoppedAt = bytes;
    v->statSeeks++;
    playing = v->playing;
    voice = v->voice;
    if (playing) {
        v->readPos = v->playPos = bytes;
        v->ending = false;
        v->generation++;
    }
    LeaveCriticalSection(&g_lock);
    if (playing && voice != NULL)
        FillQueue(v);   // the new position follows the chunks already queued
}

void DrivingAudio_SetLoopRegion(DrivingVoice *v, uint32_t startBytes, uint32_t lengthBytes) {
    if (v == NULL)
        return;
    EnterCriticalSection(&g_lock);
    v->loopStart = startBytes;
    v->loopLength = lengthBytes;
    LeaveCriticalSection(&g_lock);
}

void DrivingAudio_SetVolume(DrivingVoice *v, int32_t hundredthsDb) {
    if (v == NULL)
        return;
    IXAudio2SourceVoice *voice;
    EnterCriticalSection(&g_lock);
    bool changed = v->volume != hundredthsDb;
    v->volume = hundredthsDb;
    voice = v->voice;
    LeaveCriticalSection(&g_lock);
    if (changed && voice != NULL)
        voice->SetVolume(HundredthsDbToAmplitude(hundredthsDb));
}

void DrivingAudio_SetFrequency(DrivingVoice *v, uint32_t hz) {
    if (v == NULL || hz == 0)
        return;
    IXAudio2SourceVoice *voice;
    EnterCriticalSection(&g_lock);
    bool changed = v->frequency != hz;
    v->frequency = hz;
    voice = v->voice;
    float ratio = (float)hz / (float)v->sampleRate;
    LeaveCriticalSection(&g_lock);
    if (ratio > 4.0f) ratio = 4.0f;
    if (ratio < 0.0005f) ratio = 0.0005f;
    if (changed && voice != NULL)
        voice->SetFrequencyRatio(ratio);
}

void DrivingAudio_SetMixBins(DrivingVoice *v, const uint32_t *bins, const int32_t *volumes, uint32_t count) {
    if (v == NULL)
        return;
    EnterCriticalSection(&g_lock);
    v->binCount = 0;
    for (uint32_t i = 0; i < count && v->binCount < MAX_MIXBINS; i++) {
        v->bins[v->binCount] = bins[i];
        v->binVolumes[v->binCount] = volumes != NULL ? volumes[i] : 0;
        v->binCount++;
    }
    LeaveCriticalSection(&g_lock);
    ApplyMatrix(v);
}

void DrivingAudio_SetMixBinVolumes(DrivingVoice *v, const uint32_t *bins, const int32_t *volumes, uint32_t count) {
    if (v == NULL)
        return;
    bool changed = false;
    EnterCriticalSection(&g_lock);
    for (uint32_t i = 0; i < count; i++) {
        bool found = false;
        for (uint32_t k = 0; k < v->binCount; k++) {
            if (v->bins[k] == bins[i]) {
                found = true;
                if (v->binVolumes[k] != volumes[i]) { v->binVolumes[k] = volumes[i]; changed = true; }
            }
        }
        if (!found && v->binCount < MAX_MIXBINS) {   // a bin not assigned yet: take the assignment too
            v->bins[v->binCount] = bins[i];
            v->binVolumes[v->binCount] = volumes[i];
            v->binCount++;
            changed = true;
        }
    }
    LeaveCriticalSection(&g_lock);
    if (changed)
        ApplyMatrix(v);
}

void DrivingAudio_Report(void) {
    if (g_xaudio == NULL)
        return;
    int playing = 0;
    EnterCriticalSection(&g_lock);
    for (int i = 0; i < g_voiceCount; i++) if (g_voices[i]->playing) playing++;
    uint32_t chunks = g_statChunks, withSound = g_statChunksWithSound;
    g_statChunks = g_statChunksWithSound = 0;
    LeaveCriticalSection(&g_lock);
    AudioLog("[xa2] %u chunks streamed, %u with sound; %d voices, %d playing\n", chunks, withSound, g_voicesAlive, playing);
    // The three busiest voices: a voice churning through chunks names itself here.
    EnterCriticalSection(&g_lock);
    for (int n = 0; n < 3; n++) {
        DrivingVoice *top = NULL;
        for (int i = 0; i < g_voiceCount; i++)
            if (g_voices[i]->statChunks > 0 && (top == NULL || g_voices[i]->statChunks > top->statChunks)) top = g_voices[i];
        if (top == NULL) break;
        AudioLog("[xa2]   voice %p: %u chunks, %u plays, %u seeks; %s %u bytes, %s, loop %u+%u, %u Hz, read %u\n",
                 (void*)top, top->statChunks, top->statPlays, top->statSeeks,
                 top->formatTag == WAVE_FORMAT_XBOX_ADPCM ? "adpcm" : "pcm", top->dataBytes,
                 top->looping ? "looping" : "one-shot", top->loopStart, top->loopLength, top->frequency, top->readPos);
        top->statChunks = 0;
    }
    for (int i = 0; i < g_voiceCount; i++) g_voices[i]->statChunks = g_voices[i]->statSeeks = g_voices[i]->statPlays = 0;
    LeaveCriticalSection(&g_lock);
}
