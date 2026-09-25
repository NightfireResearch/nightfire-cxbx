#include "xaudio2Backend.h"
#include "../../common/sound/xadpcm.h"
#include "../engine/XboxSettings.h" // Settings_GetReverbEnabled

#include <windows.h>
#include <xaudio2.h>
#include <xaudio2fx.h>
#include <x3daudio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#pragma comment(lib, "xaudio2.lib")

// ---------------------------------------------------------------------------------------------------------------
// Checkpoint 1: the 2D voice path. See xaudio2Backend.h for the scope, and docs/audio-inventory.md for the
// measured behaviour this is built to satisfy. The shape of the thing follows directly from that inventory:
//
//  - the game creates its 192 buffers once at boot and never releases them, so a source voice per buffer can be
//    created lazily on the first bind and kept for the life of the process;
//  - it re-pushes every parameter of every active voice every frame whether or not anything changed (~45
//    redundant calls a frame), so every setter here compares against the last value it applied and only talks
//    to XAudio2 on a real change;
//  - velocity is always zero, for voices and listener alike, so there is no Doppler to implement;
//  - frequencies only ever span 0.5x to 1.0x the buffers' own 44032 Hz, well inside XAudio2's default maximum
//    frequency ratio of 2.0, so source voices need no special creation flag;
//  - GetStatus is the hottest call in the seam and drives voice-slot recycling, so it must be cheap and must
//    flip to "stopped" on the right frame.
//
// No XAudio2 callbacks are used anywhere: the game's own model is to poll GetStatus/GetCurrentPosition once a
// frame, which maps onto IXAudio2SourceVoice::GetState directly and keeps everything on the game thread. That
// avoids needing any locking against XAudio2's own worker thread.
// ---------------------------------------------------------------------------------------------------------------

static void XA2Log(const char *fmt, ...) {
    static FILE *logFile = NULL;
    static bool opened = false;
    if (!opened) { opened = true; logFile = fopen("xaudio2_backend.log", "w"); }
    va_list ap;
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    if (logFile != NULL) { va_start(ap, fmt); vfprintf(logFile, fmt, ap); va_end(ap); fflush(logFile); }
}

// The rate the game creates every one of its buffers at (see xboxCreateSoundBuffers). The decoded PCM is at
// this rate, so it is the source voices' format rate and the denominator of every frequency ratio.
#define XBOX_SAMPLE_RATE 44032u

// Our own mastering voice runs in stereo: the game's mixbins are 5.1 positions, but collapsing them to two
// channels is both what most people listening to this will have and far simpler to get right. MixBinToStereo
// below is the only place that assumption is encoded.
#define OUTPUT_CHANNELS 2

static IXAudio2 *g_xaudio = NULL;
static IXAudio2MasteringVoice *g_master = NULL;
static bool g_initAttempted = false;

// ---------------------------------------------------------------------------------------------------------------
// Reverb
//
// The Xbox ran I3DL2 reverb on its audio DSP, from the effects image xboxInitSound downloads. That image is
// stock XDK output rather than anything Eurocom wrote (see docs/audio-inventory.md and
// tools/dsp_image_dump.py), so what it implements is the documented I3DL2 model - there is no bespoke
// algorithm to reproduce.
//
// More usefully, the game never calls IDirectSound_SetI3DL2Listener: it is not among the entry points the
// trace saw at all. So the DSP is running one fixed room for the whole game, and the only reverb input the
// game ever supplies is a per-voice lRoom send through SetI3DL2Source, 5.6 times a frame. A fixed room plus
// those sends is therefore complete at the interface the game actually uses.
//
// That leaves the room's character as the one genuine unknown - decay, density, HF damping - which is tuning
// rather than correctness, and which nothing available locally can settle: CXBX never implemented reverb
// either, so there is no baseline to compare against. The I3DL2 "generic" preset is the starting point; if it
// ever needs to be exact, an impulse response captured under xemu (which does emulate the APU DSP) is the way
// to get it.
//
// Structurally this is a send bus: 3D voices output to both the mastering voice (dry, carrying the X3DAudio
// matrix) and this submix (wet, carrying the lRoom send). The submix is fully wet, since the dry path already
// reaches the master directly. Note the wet send deliberately does not get the distance attenuation the dry
// path does - a more distant sound having proportionally more reverb is the effect, not a bug.
// ---------------------------------------------------------------------------------------------------------------
static IXAudio2SubmixVoice *g_reverb = NULL;

// ---------------------------------------------------------------------------------------------------------------
// Decoded PCM cache
//
// The same sound bank data is bound to a buffer many times over a session (the inventory counted ~10 rebinds per
// buffer), and decoding a 3.3 MB ADPCM bank is not something to do per bind. Keyed on (data pointer, byte
// count); decoding every distinct buffer seen in a whole session came to about 17 MB, so nothing is evicted.
//
// Note the game can also overwrite a bound buffer's sample data in place through dsndWriteVoiceData - the
// streaming path - which this key cannot see. That is a known hole, called out in docs/audio-inventory.md; it
// affects streamed music rather than ordinary effects, and is for the stream work rather than checkpoint 1.
// ---------------------------------------------------------------------------------------------------------------

struct PcmCacheEntry {
    const void *adpcm;
    uint32_t adpcmBytes;
    int channels;
    uint32_t fingerprint;   // see FingerprintAdpcm - guards against the address being reused
    int16_t *pcm;
    size_t pcmValues;       // total int16 values, i.e. samples * channels
    uint64_t lastUsed;
};

#define PCM_CACHE_MAX_ENTRIES 512
#define PCM_CACHE_MAX_BYTES   (96u * 1024u * 1024u)

static PcmCacheEntry g_pcmCache[PCM_CACHE_MAX_ENTRIES];
static int g_pcmCacheCount = 0;
static size_t g_pcmCacheBytes = 0;
static uint64_t g_pcmCacheClock = 0;

static bool PcmInUse(const int16_t *pcm); // defined once the buffer list below exists

// A cheap content fingerprint, because (pointer, size) alone is not a safe identity for this data. The game
// loads sounds into a pool and reuses the same addresses for different samples, so the same key can name
// completely different audio later in a session - which sounds like the wrong effect playing. Sampling a
// couple of hundred bytes spread across the buffer distinguishes any two real samples; hashing all of a 3 MB
// bank would cost as much as decoding it.
static uint32_t FingerprintAdpcm(const void *data, uint32_t bytes) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t h = 2166136261u;
    h ^= bytes;
    h *= 16777619u;
    const uint32_t chunks = 16, chunkBytes = 16;
    for (uint32_t c = 0; c < chunks; c++) {
        uint64_t start = ((uint64_t)bytes * c) / chunks;
        for (uint32_t i = 0; i < chunkBytes && start + i < bytes; i++) {
            h ^= p[start + i];
            h *= 16777619u;
        }
    }
    return h;
}

static void ReleaseCacheEntry(PcmCacheEntry *e) {
    g_pcmCacheBytes -= e->pcmValues * sizeof(int16_t);
    free(e->pcm);
    memset(e, 0, sizeof(*e));
}

// Frees the least recently used entry that no live buffer is still playing from. Returns false if every entry
// is in use, which would need more than 512 buffers bound at once and so should not happen.
static bool EvictOneCacheEntry(void) {
    int oldest = -1;
    for (int i = 0; i < g_pcmCacheCount; i++) {
        if (g_pcmCache[i].pcm == NULL || PcmInUse(g_pcmCache[i].pcm))
            continue;
        if (oldest < 0 || g_pcmCache[i].lastUsed < g_pcmCache[oldest].lastUsed)
            oldest = i;
    }
    if (oldest < 0)
        return false;
    ReleaseCacheEntry(&g_pcmCache[oldest]);
    g_pcmCache[oldest] = g_pcmCache[--g_pcmCacheCount];
    memset(&g_pcmCache[g_pcmCacheCount], 0, sizeof(g_pcmCache[g_pcmCacheCount]));
    return true;
}

static const PcmCacheEntry *DecodeAndCache(const void *adpcm, uint32_t adpcmBytes, int channels) {
    uint32_t fingerprint = FingerprintAdpcm(adpcm, adpcmBytes);
    for (int i = 0; i < g_pcmCacheCount; i++) {
        if (g_pcmCache[i].adpcm == adpcm && g_pcmCache[i].adpcmBytes == adpcmBytes &&
            g_pcmCache[i].channels == channels) {
            if (g_pcmCache[i].fingerprint == fingerprint) {
                g_pcmCache[i].lastUsed = ++g_pcmCacheClock;
                return &g_pcmCache[i];
            }
            // Same address and size, different audio: the pool slot has been reused. Drop the stale decode
            // (unless something is still playing it, in which case leave it be and decode a second copy).
            if (!PcmInUse(g_pcmCache[i].pcm)) {
                ReleaseCacheEntry(&g_pcmCache[i]);
                g_pcmCache[i] = g_pcmCache[--g_pcmCacheCount];
                memset(&g_pcmCache[g_pcmCacheCount], 0, sizeof(g_pcmCache[g_pcmCacheCount]));
            }
            break;
        }
    }

    size_t wantBytes = XAdpcm_DecodedValueCount(adpcmBytes, channels) * sizeof(int16_t);
    while ((g_pcmCacheCount >= PCM_CACHE_MAX_ENTRIES || g_pcmCacheBytes + wantBytes > PCM_CACHE_MAX_BYTES) &&
           EvictOneCacheEntry()) {
        // keep evicting until there is room
    }
    if (g_pcmCacheCount >= PCM_CACHE_MAX_ENTRIES) {
        DSound_BackendMissing("decoded PCM cache full (every entry still in use)");
        return NULL;
    }

    size_t values = XAdpcm_DecodedValueCount(adpcmBytes, channels);
    if (values == 0)
        return NULL;

    int16_t *pcm = (int16_t *)malloc(values * sizeof(int16_t));
    if (pcm == NULL) {
        DSound_BackendMissing("out of memory decoding ADPCM");
        return NULL;
    }
    LARGE_INTEGER before, after, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&before);
    size_t got = XAdpcm_Decode(adpcm, adpcmBytes, channels, pcm, values);
    QueryPerformanceCounter(&after);
    if (got == 0) {
        free(pcm);
        return NULL;
    }
    // Decoding happens on the game thread, so a big bank stalls the frame. Timed because a stutter as a level
    // loads is exactly what that would sound like, and guessing at it is worse than measuring it.
    double decodeMs = 1000.0 * (double)(after.QuadPart - before.QuadPart) / (double)freq.QuadPart;
    if (decodeMs > 2.0)
        XA2Log("[xa2] SLOW decode: %.1f ms for %u bytes\n", decodeMs, adpcmBytes);

    PcmCacheEntry *e = &g_pcmCache[g_pcmCacheCount++];
    e->adpcm = adpcm;
    e->adpcmBytes = adpcmBytes;
    e->channels = channels;
    e->fingerprint = fingerprint;
    e->pcm = pcm;
    e->pcmValues = got;
    e->lastUsed = ++g_pcmCacheClock;
    g_pcmCacheBytes += got * sizeof(int16_t);
    XA2Log("[xa2] decoded %u bytes of %d-channel ADPCM at 0x%08x -> %u samples (cache now %u entries, %.1f MB)\n",
           adpcmBytes, channels, (unsigned)(uintptr_t)adpcm, (unsigned)(got / (size_t)channels),
           g_pcmCacheCount, g_pcmCacheBytes / 1048576.0);
    return e;
}

// Re-decodes the part of a cached buffer the game has just overwritten in place. Every ADPCM block carries its
// own predictor and step index, so a block range can be decoded in isolation and the result is bit-identical
// to decoding the whole buffer - which is what makes patching the cache correct rather than approximate.
//
// The write is rounded out to whole blocks: SFXUpdateStreams works in multiples of 0x48 (one stereo block) and
// caps each transfer at 0x2400, but the last chunk of a file read need not be aligned.
void XA2_NotifyBufferDataWritten(const void *pvBufferData, uint32_t offset, uint32_t length) {
    if (pvBufferData == NULL || length == 0)
        return;

    for (int i = 0; i < g_pcmCacheCount; i++) {
        PcmCacheEntry *e = &g_pcmCache[i];
        if (e->adpcm != pvBufferData)
            continue;

        size_t blockBytes = XAdpcm_BlockBytes(e->channels);
        size_t totalBlocks = XAdpcm_BlockCount(e->adpcmBytes, e->channels);
        size_t firstBlock = (size_t)offset / blockBytes;
        size_t lastBlock = ((size_t)offset + (size_t)length + blockBytes - 1) / blockBytes; // exclusive
        if (lastBlock > totalBlocks)
            lastBlock = totalBlocks;
        if (firstBlock >= lastBlock)
            continue;

        const uint8_t *src = (const uint8_t *)e->adpcm + firstBlock * blockBytes;
        size_t srcBytes = (lastBlock - firstBlock) * blockBytes;
        size_t dstValue = firstBlock * XADPCM_SAMPLES_PER_BLOCK * (size_t)e->channels;
        if (dstValue >= e->pcmValues)
            continue;
        XAdpcm_Decode(src, srcBytes, e->channels, e->pcm + dstValue, e->pcmValues - dstValue);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Buffer objects
//
// IDirectSound_CreateSoundBuffer hands the game back a pointer it treats as completely opaque - the seam only
// ever stores it and passes it straight back to us - so these are simply our own structs. The magic word is
// here because everything the game gives back arrives as a bare pointer with no way to validate it otherwise,
// and a stray one would be an immediate crash rather than a diagnosable problem.
// ---------------------------------------------------------------------------------------------------------------

#define XA2_BUFFER_MAGIC 0x32414258u // 'XBA2'

struct XA2Buffer {
    uint32_t magic;
    int channels;
    bool is3d;

    IXAudio2SourceVoice *voice;     // created on the first bind of real data

    const int16_t *pcm;             // owned by the cache, not by us
    size_t pcmSamples;              // per channel
    uint32_t adpcmBytes;

    // Last values the game asked for
    uint32_t frequency;
    int32_t volume;                 // hundredths of a dB, <= 0
    uint32_t headroom;              // hundredths of a dB of extra attenuation
    uint32_t loopStartBytes;
    uint32_t loopLengthBytes;
    uint32_t startPositionBytes;
    float mixBinDb[11];             // per mixbin, hundredths of a dB
    bool mixBinsSet;

    // 3D state
    float posX, posY, posZ;
    float minDistance, maxDistance;
    int32_t i3dl2Room;        // hundredths of a dB, the reverb send level the game asks for
    bool reverbSend;          // this voice outputs to the reverb submix as well as the master
    float appliedWet;
    X3DAUDIO_DISTANCE_CURVE_POINT curvePoints[12];
    X3DAUDIO_DISTANCE_CURVE curve;
    bool  curveValid;
    float curveMinDistance, curveMaxDistance; // what the cached curve was built for

    // What we last pushed to XAudio2, so the per-frame re-sends collapse to nothing
    float appliedAmplitude;
    float appliedFreqRatio;
    float appliedMatrix[OUTPUT_CHANNELS * 2];
    bool  appliedMatrixValid;

    // Playback bookkeeping
    bool playing;
    bool looping;
    uint64_t samplesPlayedAtStart;
    // What the last submit asked for. SamplesPlayed counts from the start of playback, not from the start of
    // the buffer, so the actual cursor is playBeginSample + SamplesPlayed folded back into the loop region.
    size_t playBeginSample;
    size_t firstSegmentSamples; // samples played before the looping region takes over
    size_t loopBeginSample;
    size_t loopLengthSamples;   // 0 when not looping
    // Where a Stop left the play cursor. DirectSound's Play resumes from there rather than restarting, which
    // is what psiSamplePause/psiSampleUnPause rely on to pause and resume streamed music.
    size_t resumeSample;
    bool hasResume;
};

static XA2Buffer *AsBuffer(DSoundBuffer *p) {
    XA2Buffer *b = (XA2Buffer *)p;
    if (b == NULL || b->magic != XA2_BUFFER_MAGIC) {
        DSound_BackendMissing("buffer pointer that did not come from this backend");
        return NULL;
    }
    return b;
}

// Every buffer we hand out, so DoWork's 3D pass has something to walk. The game keeps the only other
// references, in its own AudioSystem tables, and it creates exactly 192 of these once at boot.
static XA2Buffer *g_buffers[256];
static int g_bufferCount = 0;

// Whether any buffer is currently bound to this decoded PCM, so the cache never frees memory XAudio2 could
// still be reading from.
static bool PcmInUse(const int16_t *pcm) {
    if (pcm == NULL)
        return false;
    for (int i = 0; i < g_bufferCount; i++) {
        if (g_buffers[i] != NULL && g_buffers[i]->pcm == pcm)
            return true;
    }
    return false;
}

// A single object standing in for the one DirectSound instance the game creates.
static uint32_t g_deviceObject = 0xD5000000u;

// Listener state, fed to X3DAudio in DoWork. The front/top pair arrives already normalised and very nearly
// orthogonal from SetListenerOrientation, but X3DAudio requires strict orthonormality, so it is re-established
// below rather than trusted.
static X3DAUDIO_LISTENER g_listener;
static X3DAUDIO_HANDLE g_x3d;
static bool g_x3dReady = false;

// The game's own rolloff curve, as handed to SetRolloffCurve (5 points, 1.0 down to 0.0). Captured rather than
// assumed, since it is just a pointer into the XBE image and could in principle differ per buffer.
static float g_rolloffCurve[8];
static uint32_t g_rolloffPoints = 0;

// ---------------------------------------------------------------------------------------------------------------
// Units
// ---------------------------------------------------------------------------------------------------------------

// Hundredths of a decibel (DirectSound's unit, 0 = unattenuated, -10000 = silence) to a linear amplitude.
static float HundredthsDbToAmplitude(float hundredths) {
    if (hundredths <= -10000.0f)
        return 0.0f;
    if (hundredths >= 0.0f)
        return 1.0f;
    return powf(10.0f, hundredths / 2000.0f);
}

// The Xbox mixer's own formula, straight from the comment in xboxCreateSoundBuffers:
//     trueVolume = mixbinVolume + 3DVolume + volume - headroom
// The mixbin part is applied through the output matrix instead, so this is the per-voice part.
static float VoiceAmplitude(const XA2Buffer *b) {
    return HundredthsDbToAmplitude((float)b->volume - (float)b->headroom);
}

// Where each Xbox mixbin lands on a stereo pair. The four XTLK bins are the cross-talk-cancelled positions a
// 3D voice is routed to; treating them as plain left/right is a simplification that costs the HRTF widening the
// Xbox did, which is X3DAudio's job later. I3DL2 is the reverb send and contributes nothing dry.
static void MixBinToStereo(uint32_t mixBin, float *outLeft, float *outRight) {
    const float c = 0.7071068f; // -3 dB, for bins that feed both sides
    switch (mixBin) {
        case 0:  *outLeft = 1.0f; *outRight = 0.0f; return; // FRONT_LEFT
        case 1:  *outLeft = 0.0f; *outRight = 1.0f; return; // FRONT_RIGHT
        case 2:  *outLeft = c;    *outRight = c;    return; // FRONT_CENTER
        // LOW_FREQUENCY carries duplicated bass on the Xbox rather than distinct content, and the game routes
        // every 2D voice to it alongside the front pair. Folding it into both sides would add a mono component
        // that partly defeats dsndSetPan's panning, so it contributes nothing here.
        case 3:  *outLeft = 0.0f; *outRight = 0.0f; return; // LOW_FREQUENCY
        case 4:  *outLeft = c;    *outRight = 0.0f; return; // BACK_LEFT
        case 5:  *outLeft = 0.0f; *outRight = c;    return; // BACK_RIGHT
        case 6:  *outLeft = 1.0f; *outRight = 0.0f; return; // XTLK_FRONT_LEFT
        case 7:  *outLeft = 0.0f; *outRight = 1.0f; return; // XTLK_FRONT_RIGHT
        case 8:  *outLeft = c;    *outRight = 0.0f; return; // XTLK_BACK_LEFT
        case 9:  *outLeft = 0.0f; *outRight = c;    return; // XTLK_BACK_RIGHT
        default: *outLeft = 0.0f; *outRight = 0.0f; return; // I3DL2 (10), and anything unexpected
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------------------------------------------

static bool EnsureDevice(void);

IXAudio2 *XA2_GetDevice(void) {
    return EnsureDevice() ? g_xaudio : NULL;
}

static bool EnsureDevice(void) {
    if (g_initAttempted)
        return g_xaudio != NULL && g_master != NULL;
    g_initAttempted = true;

    // XAudio2 is COM underneath, and this thread has to have initialised COM before it will hand out a
    // device. Under CXBX the host process had already done it; standalone nobody has, and the
    // symptom is CreateMasteringVoice returning CO_E_NOTINITIALIZED (0x800401f0) with no audio at all.
    // RPC_E_CHANGED_MODE means COM is already up in the other threading model, which is fine for XAudio2 -
    // it only means this call did not do the initialising.
    HRESULT com = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(com) && com != RPC_E_CHANGED_MODE)
        XA2Log("[xa2] CoInitializeEx failed: 0x%08lx - audio may not start\n", com);

    HRESULT hr = XAudio2Create(&g_xaudio, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (FAILED(hr)) {
        XA2Log("[xa2] XAudio2Create failed: 0x%08lx - no audio\n", hr);
        g_xaudio = NULL;
        return false;
    }

    hr = g_xaudio->CreateMasteringVoice(&g_master, OUTPUT_CHANNELS, 44100);
    if (FAILED(hr)) {
        XA2Log("[xa2] CreateMasteringVoice failed: 0x%08lx - no audio\n", hr);
        g_master = NULL;
        g_xaudio->Release();
        g_xaudio = NULL;
        return false;
    }

    XAUDIO2_VOICE_DETAILS details;
    memset(&details, 0, sizeof(details));
    g_master->GetVoiceDetails(&details);
    XA2Log("[xa2] mastering voice: %u channels at %u Hz\n", details.InputChannels, details.InputSampleRate);

    // X3DAudio needs the real speaker layout of the mastering voice, not an assumed one.
    DWORD channelMask = 0;
    if (SUCCEEDED(g_master->GetChannelMask(&channelMask)) && channelMask != 0) {
        if (SUCCEEDED(X3DAudioInitialize(channelMask, X3DAUDIO_SPEED_OF_SOUND, g_x3d))) {
            g_x3dReady = true;
        } else {
            XA2Log("[xa2] X3DAudioInitialize failed (channel mask 0x%08lx) - 3D voices will not pan\n", channelMask);
        }
    } else {
        XA2Log("[xa2] GetChannelMask failed - 3D voices will not pan\n");
    }

    // A sane listener until the game sets one, so a sound that plays before the first update is not silent.
    memset(&g_listener, 0, sizeof(g_listener));
    g_listener.OrientFront.z = 1.0f;
    g_listener.OrientTop.y = 1.0f;

    if (Settings_GetReverbEnabled()) {
        IUnknown *reverbApo = NULL;
        hr = XAudio2CreateReverb(&reverbApo);
        if (SUCCEEDED(hr)) {
            XAUDIO2_EFFECT_DESCRIPTOR effect;
            effect.pEffect = reverbApo;
            effect.InitialState = TRUE;
            effect.OutputChannels = OUTPUT_CHANNELS;
            XAUDIO2_EFFECT_CHAIN chain;
            chain.EffectCount = 1;
            chain.pEffectDescriptors = &effect;

            // The reverb requires its sample rate to be between 20 kHz and 48 kHz, which the mastering
            // voice's 44.1 kHz satisfies.
            hr = g_xaudio->CreateSubmixVoice(&g_reverb, OUTPUT_CHANNELS, 44100, 0, 0, NULL, &chain);
            if (SUCCEEDED(hr)) {
                XAUDIO2FX_REVERB_I3DL2_PARAMETERS i3dl2 = XAUDIO2FX_I3DL2_PRESET_GENERIC;
                XAUDIO2FX_REVERB_PARAMETERS params;
                ReverbConvertI3DL2ToNative(&i3dl2, &params, FALSE); // FALSE: this is a stereo bus, not 7.1
                params.WetDryMix = 100.0f;                          // a send bus - the dry path bypasses it
                g_reverb->SetEffectParameters(0, &params, sizeof(params));
                XA2Log("[xa2] reverb submix created (I3DL2 generic preset)\n");
            } else {
                XA2Log("[xa2] CreateSubmixVoice for reverb failed: 0x%08lx - 3D voices will be dry\n", hr);
                g_reverb = NULL;
            }
            reverbApo->Release();
        } else {
            XA2Log("[xa2] XAudio2CreateReverb failed: 0x%08lx - 3D voices will be dry\n", hr);
        }
    } else {
        XA2Log("[xa2] reverb disabled by settings.ini\n");
    }
    return true;
}

void XA2_DirectSoundCreate(void *lpGuid, DSoundObject **ppDS, void *pUnknown) {
    (void)lpGuid;
    (void)pUnknown;
    EnsureDevice();
    if (ppDS != NULL)
        *ppDS = (DSoundObject *)&g_deviceObject;
}

void XA2_DirectSoundUseFullHRTF(void) {
    // The Xbox's own head-related transfer function for its cross-talk-cancelled 3D bins. Nothing to do until
    // the 3D path goes through X3DAudio; noted rather than silently ignored.
    DSound_BackendMissing("DirectSoundUseFullHRTF (no HRTF yet)");
}

void XA2_IDirectSound_DownloadEffectsImage(DSoundObject *thisPtr, const void *pvImageBuffer,
                                           uint32_t dwImageSize, void *pImageLoc, void **ppImageDesc) {
    (void)thisPtr;
    (void)pvImageBuffer;
    (void)dwImageSize;
    // The I3DL2 reverb DSP program for the Xbox's own audio processor. There is no equivalent to hand it to;
    // the reverb XAPO will stand in for it later. The game reads back neither output, but it does store them,
    // so give it zeroes rather than leaving whatever was there.
    if (pImageLoc != NULL)
        *(uint32_t *)pImageLoc = 0;
    if (ppImageDesc != NULL)
        *ppImageDesc = NULL;
    DSound_BackendMissing("IDirectSound_DownloadEffectsImage (no I3DL2 reverb yet)");
}

// ---------------------------------------------------------------------------------------------------------------
// Listener
// ---------------------------------------------------------------------------------------------------------------

void XA2_IDirectSound_SetPosition(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)thisPtr;
    (void)dwApply;
    g_listener.Position.x = x;
    g_listener.Position.y = y;
    g_listener.Position.z = z;
}

void XA2_IDirectSound_SetVelocity(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)thisPtr; (void)x; (void)y; (void)z; (void)dwApply;
    // Always zero in this game - there is no Doppler to model. See docs/audio-inventory.md.
}

void XA2_IDirectSound_SetOrientation(DSoundObject *thisPtr, float xFront, float yFront, float zFront,
                                     float xTop, float yTop, float zTop, uint32_t dwApply) {
    (void)thisPtr;
    (void)dwApply;

    // X3DAudio asserts that front and top are orthonormal and misbehaves if they are not. The game's own pair
    // measures orthogonal to about 6e-4, which is close but not close enough to rely on, so normalise the front
    // vector and then project the top vector perpendicular to it (Gram-Schmidt).
    float fx = xFront, fy = yFront, fz = zFront;
    float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    if (flen <= 1e-6f) {
        fx = 0.0f; fy = 0.0f; fz = 1.0f;
    } else {
        fx /= flen; fy /= flen; fz /= flen;
    }

    float tx = xTop, ty = yTop, tz = zTop;
    float dot = tx * fx + ty * fy + tz * fz;
    tx -= fx * dot; ty -= fy * dot; tz -= fz * dot;
    float tlen = sqrtf(tx * tx + ty * ty + tz * tz);
    if (tlen <= 1e-6f) {
        // Top parallel to front: pick any perpendicular axis rather than hand X3DAudio a degenerate basis.
        if (fabsf(fy) < 0.9f) { tx = 0.0f; ty = 1.0f; tz = 0.0f; }
        else                  { tx = 1.0f; ty = 0.0f; tz = 0.0f; }
        dot = tx * fx + ty * fy + tz * fz;
        tx -= fx * dot; ty -= fy * dot; tz -= fz * dot;
        tlen = sqrtf(tx * tx + ty * ty + tz * tz);
    }
    tx /= tlen; ty /= tlen; tz /= tlen;

    g_listener.OrientFront.x = fx;
    g_listener.OrientFront.y = fy;
    g_listener.OrientFront.z = fz;
    g_listener.OrientTop.x = tx;
    g_listener.OrientTop.y = ty;
    g_listener.OrientTop.z = tz;
}

// ---------------------------------------------------------------------------------------------------------------
// Buffer creation
// ---------------------------------------------------------------------------------------------------------------

void XA2_IDirectSound_CreateSoundBuffer(DSoundObject *thisPtr, DSBUFFERDESC_Xbox *pdsbd,
                                        uint32_t *ppBuffer, uint32_t *ppUnknown) {
    (void)thisPtr;
    (void)ppUnknown;
    if (ppBuffer == NULL)
        return;
    *ppBuffer = 0;
    if (pdsbd == NULL || pdsbd->lpwfxFormat == NULL)
        return;

    XA2Buffer *b = (XA2Buffer *)calloc(1, sizeof(XA2Buffer));
    if (b == NULL) {
        DSound_BackendMissing("out of memory creating a sound buffer");
        return;
    }

    b->magic = XA2_BUFFER_MAGIC;
    b->channels = pdsbd->lpwfxFormat->nChannels < 1 ? 1 : (int)pdsbd->lpwfxFormat->nChannels;
    b->is3d = (pdsbd->dwFlags & 0x10u) != 0; // DSBCAPS_CTRL3D
    b->frequency = pdsbd->lpwfxFormat->nSamplesPerSec ? pdsbd->lpwfxFormat->nSamplesPerSec : XBOX_SAMPLE_RATE;
    b->volume = 0;
    b->headroom = 0;
    b->minDistance = 1.0f;
    b->maxDistance = 1000.0f;
    b->appliedAmplitude = -1.0f;   // force the first apply
    b->appliedFreqRatio = -1.0f;

    if (g_bufferCount < (int)(sizeof(g_buffers) / sizeof(g_buffers[0])))
        g_buffers[g_bufferCount++] = b;
    else
        DSound_BackendMissing("more sound buffers than the backend tracks");

    *ppBuffer = (uint32_t)(uintptr_t)b;
}

static void ApplyAmplitude(XA2Buffer *b, float extraAttenuation);
static void ApplyMixMatrix(XA2Buffer *b);
static void ApplyFrequency(XA2Buffer *b);
static void Apply3D(XA2Buffer *b);
static void ApplyReverbSend(XA2Buffer *b);

// Which output voice an ordinary SetOutputMatrix refers to. NULL means "the only one", which is right until a
// voice acquires a second destination - after that XAudio2 needs to be told explicitly.
static IXAudio2Voice *DryDestination(const XA2Buffer *b) {
    return b->reverbSend ? (IXAudio2Voice *)g_master : NULL;
}

// Creates the source voice for a buffer once its format is known. The format rate is the Xbox rate, so
// SetFrequency turns into a plain ratio against it.
//
// Everything the game has already told us about this buffer has to be pushed here, because the ordering puts
// all of it before the voice exists: the mixbins and headroom are set for all 192 buffers at boot, and
// dsndGetVoice sets the frequency before anything plays. The setters themselves can only cache in that state.
static bool EnsureVoice(XA2Buffer *b) {
    if (b->voice != NULL)
        return true;
    if (!EnsureDevice())
        return false;

    WAVEFORMATEX wfx;
    memset(&wfx, 0, sizeof(wfx));
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)b->channels;
    wfx.nSamplesPerSec = XBOX_SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(b->channels * 2);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    // 2.0 is the maximum frequency ratio: the inventory puts every rate the game asks for between 0.5x and
    // 1.0x of the format rate, so this is ample and costs nothing.
    HRESULT hr = g_xaudio->CreateSourceVoice(&b->voice, &wfx, 0, 2.0f, NULL, NULL, NULL);
    if (FAILED(hr)) {
        XA2Log("[xa2] CreateSourceVoice (%d channel) failed: 0x%08lx\n", b->channels, hr);
        b->voice = NULL;
        return false;
    }

    // A 3D voice feeds the reverb bus as well as the master. This has to happen before any output matrix is
    // set, because SetOutputVoices resets them - and once there are two destinations every SetOutputMatrix
    // has to name which one it means, hence DryDestination below.
    if (b->is3d && g_reverb != NULL) {
        XAUDIO2_SEND_DESCRIPTOR sends[2];
        sends[0].Flags = 0; sends[0].pOutputVoice = g_master;
        sends[1].Flags = 0; sends[1].pOutputVoice = g_reverb;
        XAUDIO2_VOICE_SENDS sendList;
        sendList.SendCount = 2;
        sendList.pSends = sends;
        if (SUCCEEDED(b->voice->SetOutputVoices(&sendList)))
            b->reverbSend = true;
    }

    // Nothing has been pushed to this voice yet, so invalidate the diff state and apply everything cached.
    b->appliedAmplitude = -1.0f;
    b->appliedFreqRatio = -1.0f;
    b->appliedMatrixValid = false;
    b->appliedWet = -1.0f;
    ApplyFrequency(b);
    ApplyMixMatrix(b);   // no-op for 3D voices, whose matrix belongs to X3DAudio
    ApplyAmplitude(b, 1.0f);

    // Belt and braces for the same hazard: never leave a 3D voice sitting on XAudio2's default unity matrix
    // between creation and its first Apply3D. Only when X3DAudio is actually available - if it is not, the
    // default matrix is the one thing keeping 3D sounds audible at all.
    if (b->is3d && g_x3dReady) {
        float silent[OUTPUT_CHANNELS] = { 0.0f, 0.0f };
        b->voice->SetOutputMatrix(DryDestination(b), 1, OUTPUT_CHANNELS, silent);
        memcpy(b->appliedMatrix, silent, sizeof(silent));
        b->appliedMatrixValid = true;
    }
    return true;
}

void XA2_IDirectSoundBuffer_SetBufferData(DSoundBuffer *thisPtr, void *pvBufferData, uint32_t dwBufferBytes) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;

    // The unbind case: maybeSoundShutdown drops every buffer's reference to a level's sound bank before it is
    // freed, so this has to actually stop the voice rather than leave it reading memory that is about to go.
    if (pvBufferData == NULL || dwBufferBytes == 0) {
        if (b->voice != NULL) {
            b->voice->Stop(0);
            b->voice->FlushSourceBuffers();
        }
        b->pcm = NULL;
        b->pcmSamples = 0;
        b->adpcmBytes = 0;
        b->playing = false;
        return;
    }

    // Whatever happens next, this buffer must not keep playing the sound it held before. Stopping and
    // unbinding first means a failure below leaves it silent rather than playing the previous occupant of the
    // slot - which is what "the watch laser sounds like clanging" looks like from here.
    if (b->voice != NULL) {
        b->voice->Stop(0);
        b->voice->FlushSourceBuffers();
        b->playing = false;
    }
    b->pcm = NULL;
    b->pcmSamples = 0;

    const PcmCacheEntry *entry = DecodeAndCache(pvBufferData, dwBufferBytes, b->channels);
    if (entry == NULL)
        return;
    b->pcm = entry->pcm;
    b->pcmSamples = entry->pcmValues / (size_t)b->channels;
    b->adpcmBytes = dwBufferBytes;
    b->startPositionBytes = 0;
    // New sample data: any cursor saved from the previous binding means nothing now. dsndGetVoice happens to
    // call SetCurrentPosition(0) straight after this, which would also clear it, but a slot recycled without
    // that would otherwise start a brand new sound part-way through.
    b->hasResume = false;
}

// ---------------------------------------------------------------------------------------------------------------
// Playback
// ---------------------------------------------------------------------------------------------------------------

void XA2_IDirectSoundBuffer_SetLoopRegion(DSoundBuffer *thisPtr, uint32_t dwLoopStart, uint32_t dwLoopLength) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->loopStartBytes = dwLoopStart;
    b->loopLengthBytes = dwLoopLength;
}

void XA2_IDirectSoundBuffer_SetCurrentPosition(DSoundBuffer *thisPtr, uint32_t dwPlayCursor) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    // Always 0 in practice, and always immediately before a Play, so this only needs to decide where the next
    // submit starts rather than seek a running voice. An explicit position also overrides any pending resume.
    b->startPositionBytes = dwPlayCursor;
    b->hasResume = false;
}

void XA2_IDirectSoundBuffer_Play(DSoundBuffer *thisPtr, uint32_t dwReserved1, uint32_t dwReserved2, uint32_t dwFlags) {
    (void)dwReserved1;
    (void)dwReserved2;
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL || b->pcm == NULL || b->pcmSamples == 0)
        return;
    if (!EnsureVoice(b))
        return;

    b->voice->Stop(0);
    b->voice->FlushSourceBuffers();

    XAUDIO2_BUFFER xb;
    memset(&xb, 0, sizeof(xb));
    xb.Flags = XAUDIO2_END_OF_STREAM;
    xb.AudioBytes = (UINT32)(b->pcmSamples * (size_t)b->channels * sizeof(int16_t));
    xb.pAudioData = (const BYTE *)b->pcm;

    // Resume where a Stop left off, unless SetCurrentPosition has since said otherwise. Restarting a paused
    // music stream from the top would be audible and wrong.
    size_t startSample;
    if (b->hasResume) {
        startSample = b->resumeSample;
        b->hasResume = false;
    } else {
        startSample = XAdpcm_ByteOffsetToSample(b->startPositionBytes, b->channels);
    }
    if (startSample >= b->pcmSamples)
        startSample = 0;
    b->playBeginSample = startSample;
    b->loopBeginSample = 0;
    b->loopLengthSamples = 0;
    b->firstSegmentSamples = b->pcmSamples - startSample;

    b->looping = (dwFlags & 0x1u) != 0; // DSBPLAY_LOOPING
    size_t loopBegin = 0, loopLength = 0;
    if (b->looping) {
        loopBegin = XAdpcm_ByteOffsetToSample(b->loopStartBytes, b->channels);
        if (loopBegin >= b->pcmSamples)
            loopBegin = 0;
        loopLength = XAdpcm_ByteOffsetToSample(b->loopLengthBytes, b->channels);
        if (loopLength == 0 || loopBegin + loopLength > b->pcmSamples)
            loopLength = b->pcmSamples - loopBegin; // a zero length means "to the end", as it does on Xbox
        if (loopLength == 0)
            b->looping = false; // XAudio2 rejects a zero-length loop outright
    }

    // Resuming part-way into a loop cannot be expressed as one XAudio2 buffer: it insists the loop region sit
    // inside the play region, so a buffer that starts at the resume point can only loop back to the resume
    // point, not to the real loop start. Clamping the loop start up to meet it - which is what this used to do
    // - shrinks the looped region on every resume until the music is repeating a fragment.
    //
    // Two queued buffers express it exactly. XAudio2 plays them in order: first the remainder of the ring from
    // the resume point, once, then the real loop region for ever.
    HRESULT hr;
    if (b->looping && startSample != loopBegin) {
        size_t loopEnd = loopBegin + loopLength;
        if (startSample >= loopEnd)          // outside the loop region entirely; nothing sensible to resume to
            startSample = loopBegin;
        b->playBeginSample = startSample;
        b->firstSegmentSamples = loopEnd - startSample;

        XAUDIO2_BUFFER tail = xb;
        tail.Flags = 0;                       // not the end of the stream - the looping buffer follows it
        tail.PlayBegin = (UINT32)startSample;
        tail.PlayLength = (UINT32)b->firstSegmentSamples;
        hr = b->voice->SubmitSourceBuffer(&tail, NULL);
        if (FAILED(hr)) {
            XA2Log("[xa2] SubmitSourceBuffer (resume segment) failed: 0x%08lx\n", hr);
            return;
        }
        xb.PlayBegin = (UINT32)loopBegin;
        xb.PlayLength = (UINT32)loopLength;
        xb.LoopBegin = (UINT32)loopBegin;
        xb.LoopLength = (UINT32)loopLength;
        xb.LoopCount = XAUDIO2_LOOP_INFINITE;
    } else {
        xb.PlayBegin = (UINT32)startSample;
        xb.PlayLength = 0; // to the end of the buffer
        if (b->looping) {
            xb.LoopBegin = (UINT32)loopBegin;
            xb.LoopLength = (UINT32)loopLength;
            xb.LoopCount = XAUDIO2_LOOP_INFINITE;
            b->firstSegmentSamples = 0; // playback is inside the loop region from the very first sample
        }
    }
    if (b->looping) {
        b->loopBeginSample = loopBegin;
        b->loopLengthSamples = loopLength;
    }

    hr = b->voice->SubmitSourceBuffer(&xb, NULL);
    if (FAILED(hr)) {
        XA2Log("[xa2] SubmitSourceBuffer failed: 0x%08lx (%u samples, loop %u..+%u)\n",
               hr, (unsigned)b->pcmSamples, xb.LoopBegin, xb.LoopLength);
        return;
    }

    // Baseline for GetCurrentPosition. SamplesPlayed counts from voice creation and FlushSourceBuffers does
    // not reset it, so the position within this buffer is the difference. Note the flags must be 0 here:
    // XAUDIO2_VOICE_NOSAMPLESPLAYED is precisely the flag that leaves SamplesPlayed unfilled.
    XAUDIO2_VOICE_STATE state;
    memset(&state, 0, sizeof(state));
    b->voice->GetState(&state, 0);
    b->samplesPlayedAtStart = state.SamplesPlayed;

    // Position a 3D voice before it makes a sound, not on the next frame's DoWork. dsndUpdateVoices runs
    // DoWork *before* its play/stop pass, so a voice starting this frame would otherwise be skipped by the 3D
    // pass and begin with no output matrix at all - which XAudio2 takes to mean unity into both channels, i.e.
    // full volume with no distance attenuation. One frame of that on every 3D sound is very audible when a
    // level starts and a batch of distant ambience triggers at once.
    if (b->is3d)
        Apply3D(b);

    if (SUCCEEDED(b->voice->Start(0)))
        b->playing = true;
}

// Where the voice actually is in its buffer right now, in samples.
//
// XAudio2's SamplesPlayed counts source samples consumed since playback started, which is NOT the same as the
// position in the buffer: a submit can begin part-way in (PlayBegin) and can wrap round a loop region. Getting
// this wrong is not a cosmetic error - SFXUpdateStreams computes where to write the next chunk of streamed
// music from the position psiStreamGetPlayPos reports, so an offset cursor makes the game overwrite the audio
// about to be played and leave stale ring-buffer content elsewhere. That sounds like doubling or echo on
// music, and like a glitch on a resumed speech stream.
static size_t CurrentSample(const XA2Buffer *b) {
    if (b->pcmSamples == 0)
        return 0;

    XAUDIO2_VOICE_STATE state;
    memset(&state, 0, sizeof(state));
    b->voice->GetState(&state, 0);

    uint64_t played = state.SamplesPlayed - b->samplesPlayedAtStart;

    // Up to firstSegmentSamples the voice is working through the one-shot segment that starts at
    // playBeginSample; after that it is inside the looping region. When playback started inside the loop
    // region already, firstSegmentSamples is 0 and the second branch applies from the first sample.
    if (b->loopLengthSamples > 0 && played >= (uint64_t)b->firstSegmentSamples) {
        uint64_t intoLoop = (played - (uint64_t)b->firstSegmentSamples) % (uint64_t)b->loopLengthSamples;
        return (size_t)((uint64_t)b->loopBeginSample + intoLoop);
    }

    uint64_t position = (uint64_t)b->playBeginSample + played;
    if (position >= (uint64_t)b->pcmSamples)
        position = (uint64_t)b->pcmSamples - 1;
    return (size_t)position;
}

void XA2_IDirectSoundBuffer_Stop(DSoundBuffer *thisPtr) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    if (b->voice != NULL) {
        // Remember the cursor before flushing, so a following Play resumes rather than restarts.
        if (b->playing && b->pcmSamples > 0) {
            b->resumeSample = CurrentSample(b);
            b->hasResume = true;
        }
        b->voice->Stop(0);
        b->voice->FlushSourceBuffers();
    }
    b->playing = false;
}

void XA2_IDirectSoundBuffer_GetStatus(DSoundBuffer *thisPtr, uint32_t *pdwStatus) {
    if (pdwStatus == NULL)
        return;
    *pdwStatus = 0;
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL || b->voice == NULL || !b->playing)
        return;

    // The hottest call in the seam (7.7 per frame) and the thing dsndUpdateVoices uses to decide a slot is
    // free, so it has to go false exactly when the sound really has finished. A non-looping buffer runs out of
    // queued buffers when it ends; a looping one never does.
    XAUDIO2_VOICE_STATE state;
    memset(&state, 0, sizeof(state));
    b->voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    if (state.BuffersQueued > 0)
        *pdwStatus = 0x1u; // DSBSTATUS_PLAYING
    else
        b->playing = false;
}

void XA2_IDirectSoundBuffer_GetCurrentPosition(DSoundBuffer *thisPtr, uint32_t *pdwPlayCursor, uint32_t *pdwWriteCursor) {
    if (pdwPlayCursor != NULL)
        *pdwPlayCursor = 0;
    if (pdwWriteCursor != NULL)
        *pdwWriteCursor = 0;
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL || b->voice == NULL || b->pcmSamples == 0)
        return;

    // The game speaks ADPCM byte offsets on this interface - psiStreamGetPlayPos feeds the result straight
    // back into the same units it gave SetLoopRegion.
    if (pdwPlayCursor != NULL)
        *pdwPlayCursor = (uint32_t)XAdpcm_SampleToByteOffset(CurrentSample(b), b->channels);
}

// ---------------------------------------------------------------------------------------------------------------
// Volume, frequency and routing
// ---------------------------------------------------------------------------------------------------------------

// Pushes the voice's amplitude if it has changed. Called from the setters and from DoWork (for 3D voices, whose
// amplitude also depends on the listener).
static void ApplyAmplitude(XA2Buffer *b, float extraAttenuation) {
    if (b->voice == NULL)
        return;
    float amplitude = VoiceAmplitude(b) * extraAttenuation;
    if (fabsf(amplitude - b->appliedAmplitude) < 0.0001f)
        return;
    b->appliedAmplitude = amplitude;
    b->voice->SetVolume(amplitude);
}

void XA2_IDirectSoundBuffer_SetVolume(DSoundBuffer *thisPtr, int32_t lVolume) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->volume = lVolume;
    if (!b->is3d)
        ApplyAmplitude(b, 1.0f);
}

void XA2_IDirectSoundBuffer_SetHeadroom(DSoundBuffer *thisPtr, uint32_t dwHeadroom) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->headroom = dwHeadroom;
    if (!b->is3d)
        ApplyAmplitude(b, 1.0f);
}

static void ApplyFrequency(XA2Buffer *b) {
    if (b->voice == NULL || b->frequency == 0)
        return;
    float ratio = (float)b->frequency / (float)XBOX_SAMPLE_RATE;
    if (ratio < 0.03125f) ratio = 0.03125f; // XAudio2's own minimum
    if (ratio > 2.0f) ratio = 2.0f;         // the ceiling the voices were created with
    if (fabsf(ratio - b->appliedFreqRatio) < 0.0001f)
        return;
    b->appliedFreqRatio = ratio;
    b->voice->SetFrequencyRatio(ratio);
}

void XA2_IDirectSoundBuffer_SetFrequency(DSoundBuffer *thisPtr, uint32_t dwFrequency) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL || dwFrequency == 0)
        return;
    b->frequency = dwFrequency;
    ApplyFrequency(b);
}

// Turns the per-mixbin gains into an XAudio2 output matrix and pushes it if it changed.
static void ApplyMixMatrix(XA2Buffer *b) {
    if (b->voice == NULL || g_master == NULL)
        return;
    // A 3D voice's output matrix belongs to X3DAudio, which computes direction and distance together in
    // DoWork. Its mixbin assignment (the four cross-talk bins plus centre plus I3DL2) describes how the Xbox
    // hardware performed that same job, so applying both would fight.
    if (b->is3d)
        return;

    float left = 0.0f, right = 0.0f;
    if (b->mixBinsSet) {
        for (uint32_t bin = 0; bin < 11; bin++) {
            float binLeft, binRight;
            MixBinToStereo(bin, &binLeft, &binRight);
            if (binLeft == 0.0f && binRight == 0.0f)
                continue;
            float gain = HundredthsDbToAmplitude(b->mixBinDb[bin]);
            left += binLeft * gain;
            right += binRight * gain;
        }
    } else {
        left = right = 0.7071068f;
    }

    // Several bins can feed the same side, so this can exceed unity - normalise rather than clip.
    float peak = left > right ? left : right;
    if (peak > 1.0f) {
        left /= peak;
        right /= peak;
    }

    // XAudio2 indexes the level matrix as pLevelMatrix[SourceChannels * destination + source].
    float matrix[OUTPUT_CHANNELS * 2];
    if (b->channels == 1) {
        matrix[0] = left;   // mono -> left
        matrix[1] = right;  // mono -> right
    } else {
        matrix[0] = left;   // source left  -> left
        matrix[1] = 0.0f;   // source right -> left
        matrix[2] = 0.0f;   // source left  -> right
        matrix[3] = right;  // source right -> right
    }

    int count = (b->channels == 1) ? OUTPUT_CHANNELS : OUTPUT_CHANNELS * 2;
    if (b->appliedMatrixValid && memcmp(matrix, b->appliedMatrix, count * sizeof(float)) == 0)
        return;
    memcpy(b->appliedMatrix, matrix, count * sizeof(float));
    b->appliedMatrixValid = true;
    b->voice->SetOutputMatrix(NULL, b->channels, OUTPUT_CHANNELS, matrix);
}

// Records a set of mixbin assignments. SetMixBins says which bins a voice feeds (all at unity); the volumes
// arrive separately through SetMixBinVolumes.
void XA2_IDirectSoundBuffer_SetMixBins(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL || pMixBins == NULL || pMixBins->lpMixBinVolumePairs == NULL)
        return;

    for (int i = 0; i < 11; i++)
        b->mixBinDb[i] = -10000.0f; // bins this voice does not feed
    for (uint32_t i = 0; i < pMixBins->dwMixBinCount; i++) {
        uint32_t bin = pMixBins->lpMixBinVolumePairs[i].dwMixBin;
        if (bin < 11)
            b->mixBinDb[bin] = (float)pMixBins->lpMixBinVolumePairs[i].lVolume;
    }
    b->mixBinsSet = true;
    ApplyMixMatrix(b);
}

// The 2D panning path: dsndSetPan recomputes all six 5.1 bin volumes and sends them as a set.
void XA2_IDirectSoundBuffer_SetMixBinVolumes(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins) {
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL || pMixBins == NULL || pMixBins->lpMixBinVolumePairs == NULL)
        return;

    for (uint32_t i = 0; i < pMixBins->dwMixBinCount; i++) {
        uint32_t bin = pMixBins->lpMixBinVolumePairs[i].dwMixBin;
        if (bin < 11)
            b->mixBinDb[bin] = (float)pMixBins->lpMixBinVolumePairs[i].lVolume;
    }
    b->mixBinsSet = true;
    ApplyMixMatrix(b);
}

// ---------------------------------------------------------------------------------------------------------------
// Per-voice 3D - checkpoint 1 keeps the state and applies distance attenuation only
// ---------------------------------------------------------------------------------------------------------------

void XA2_IDirectSoundBuffer_SetPosition(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)dwApply;
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->posX = x;
    b->posY = y;
    b->posZ = z;
}

void XA2_IDirectSoundBuffer_SetVelocity(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)thisPtr; (void)x; (void)y; (void)z; (void)dwApply;
    // Always zero in this game; no Doppler to model.
}

void XA2_IDirectSoundBuffer_SetMinDistance(DSoundBuffer *thisPtr, float flMinDistance, uint32_t dwApply) {
    (void)dwApply;
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b != NULL)
        b->minDistance = flMinDistance;
}

void XA2_IDirectSoundBuffer_SetMaxDistance(DSoundBuffer *thisPtr, float flMaxDistance, uint32_t dwApply) {
    (void)dwApply;
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b != NULL)
        b->maxDistance = flMaxDistance;
}

void XA2_IDirectSoundBuffer_SetRolloffCurve(DSoundBuffer *thisPtr, const float *pflPoints,
                                            uint32_t dwPointCount, uint32_t dwApply) {
    (void)thisPtr;
    (void)dwApply;
    if (pflPoints == NULL || dwPointCount == 0)
        return;
    uint32_t count = dwPointCount;
    if (count > sizeof(g_rolloffCurve) / sizeof(g_rolloffCurve[0]))
        count = sizeof(g_rolloffCurve) / sizeof(g_rolloffCurve[0]);
    for (uint32_t i = 0; i < count; i++)
        g_rolloffCurve[i] = pflPoints[i];
    g_rolloffPoints = count;
}

// Pushes the reverb send level if it has changed. The voice's own volume already scales everything it
// outputs, so this matrix carries only the lRoom send - which matches the Xbox, where the true level of a
// mixbin is the voice volume plus that bin's own volume.
static void ApplyReverbSend(XA2Buffer *b) {
    if (!b->reverbSend || b->voice == NULL)
        return;
    float wet = HundredthsDbToAmplitude((float)b->i3dl2Room);
    if (fabsf(wet - b->appliedWet) < 0.0001f)
        return;
    b->appliedWet = wet;
    float matrix[OUTPUT_CHANNELS] = { wet, wet };
    b->voice->SetOutputMatrix(g_reverb, 1, OUTPUT_CHANNELS, matrix);
}

// The game's only reverb control: a per-voice send level, which dsndSetI3DL2Source fills in from the volume
// lookup table. Everything else in the DSI3DL2BUFFER stays at the zero dsndGetVoice memsets it to.
void XA2_IDirectSoundBuffer_SetI3DL2Source(DSoundBuffer *thisPtr, DSI3DL2BUFFER_Xbox *pds3db, uint32_t dwApply) {
    (void)dwApply;
    XA2Buffer *b = AsBuffer(thisPtr);
    if (b == NULL || pds3db == NULL)
        return;
    b->i3dl2Room = pds3db->lRoom;
    ApplyReverbSend(b);
}

// Builds the X3DAudio volume curve for one voice out of the game's own rolloff curve and this voice's min/max
// distance pair.
//
// The two models do not line up on their own. DirectSound says "full volume out to the minimum distance, then
// follow the rolloff curve until the maximum distance, then silence". X3DAudio takes a single curve over a
// normalised distance where 1.0 means CurveDistanceScaler. So the scaler is the maximum distance and the curve
// gets a flat segment out to minDistance/maxDistance before the game's own points are spread over what is left.
//
// X3DAudio requires the points to start at distance 0, end at distance 1, and strictly increase.
static void BuildDistanceCurve(XA2Buffer *b) {
    uint32_t sourcePoints = g_rolloffPoints;
    static const float kLinearFallback[2] = { 1.0f, 0.0f };
    const float *curve = g_rolloffCurve;
    if (sourcePoints < 2) {
        curve = kLinearFallback; // no SetRolloffCurve seen yet; linear beats nothing
        sourcePoints = 2;
    }

    float minD = b->minDistance;
    float maxD = b->maxDistance;
    if (!(maxD > 0.0f))
        maxD = 1.0f;
    if (!(minD >= 0.0f) || minD >= maxD)
        minD = 0.0f;
    float flat = minD / maxD; // where the rolloff starts, normalised
    if (flat > 0.9f)
        flat = 0.9f;          // leave room for the curve itself

    uint32_t n = 0;
    b->curvePoints[n].Distance = 0.0f;
    b->curvePoints[n].DSPSetting = curve[0];
    n++;
    if (flat > 0.0f) {
        b->curvePoints[n].Distance = flat;
        b->curvePoints[n].DSPSetting = curve[0];
        n++;
    }
    uint32_t maxPoints = (uint32_t)(sizeof(b->curvePoints) / sizeof(b->curvePoints[0]));
    for (uint32_t i = 1; i < sourcePoints && n < maxPoints; i++) {
        float t = (float)i / (float)(sourcePoints - 1);
        float distance = flat + (1.0f - flat) * t;
        if (distance <= b->curvePoints[n - 1].Distance)
            continue; // never let rounding produce a non-increasing point
        b->curvePoints[n].Distance = (i == sourcePoints - 1) ? 1.0f : distance;
        b->curvePoints[n].DSPSetting = curve[i];
        n++;
    }
    // Guarantee the curve reaches 1.0, whatever the arithmetic above did.
    if (b->curvePoints[n - 1].Distance < 1.0f) {
        if (n < maxPoints) {
            b->curvePoints[n].Distance = 1.0f;
            b->curvePoints[n].DSPSetting = curve[sourcePoints - 1];
            n++;
        } else {
            b->curvePoints[n - 1].Distance = 1.0f;
        }
    }

    b->curve.pPoints = b->curvePoints;
    b->curve.PointCount = n;
    b->curveValid = true;
    b->curveMinDistance = b->minDistance;
    b->curveMaxDistance = b->maxDistance;
}

// Runs X3DAudio for one playing 3D voice and pushes the result. The matrix X3DAudio returns already carries
// distance attenuation as well as direction, so the voice's own volume stays the plain
// (volume - headroom) amplitude and must not be attenuated again here.
static void Apply3D(XA2Buffer *b) {
    if (!g_x3dReady || b->voice == NULL)
        return;

    if (!b->curveValid || b->curveMinDistance != b->minDistance || b->curveMaxDistance != b->maxDistance)
        BuildDistanceCurve(b);

    X3DAUDIO_EMITTER emitter;
    memset(&emitter, 0, sizeof(emitter));
    emitter.ChannelCount = 1;                  // every 3D buffer the game creates is mono
    emitter.CurveDistanceScaler = (b->maxDistance > 0.0f) ? b->maxDistance : 1.0f;
    emitter.DopplerScaler = 0.0f;              // no Doppler in this game
    emitter.Position.x = b->posX;
    emitter.Position.y = b->posY;
    emitter.Position.z = b->posZ;
    emitter.OrientFront.z = 1.0f;              // no cone, but X3DAudio still wants a basis
    emitter.OrientTop.y = 1.0f;
    emitter.pVolumeCurve = &b->curve;

    float matrix[OUTPUT_CHANNELS] = { 0.0f, 0.0f };
    X3DAUDIO_DSP_SETTINGS dsp;
    memset(&dsp, 0, sizeof(dsp));
    dsp.SrcChannelCount = 1;
    dsp.DstChannelCount = OUTPUT_CHANNELS;
    dsp.pMatrixCoefficients = matrix;

    X3DAudioCalculate(g_x3d, &g_listener, &emitter, X3DAUDIO_CALCULATE_MATRIX, &dsp);

    if (!b->appliedMatrixValid || memcmp(matrix, b->appliedMatrix, sizeof(matrix)) != 0) {
        memcpy(b->appliedMatrix, matrix, sizeof(matrix));
        b->appliedMatrixValid = true;
        b->voice->SetOutputMatrix(DryDestination(b), 1, OUTPUT_CHANNELS, matrix);
    }
    ApplyAmplitude(b, 1.0f);
    ApplyReverbSend(b);
}

// ---------------------------------------------------------------------------------------------------------------
// Per-frame work
// ---------------------------------------------------------------------------------------------------------------

void XA2_IDirectSound_CommitDeferredSettings(DSoundObject *thisPtr) {
    (void)thisPtr;
    // Everything here is applied as it arrives, so there is nothing deferred to commit. DoWork does the 3D
    // pass, since that is the one thing that depends on voice and listener state together.
}

// The game calls this once a frame. All we need it for is the 3D voices, whose attenuation depends on where the
// listener ended up this frame.
void XA2_DirectSoundDoWork(void) {
    if (g_xaudio == NULL)
        return;

    for (int i = 0; i < g_bufferCount; i++) {
        XA2Buffer *b = g_buffers[i];
        if (b == NULL || !b->is3d || b->voice == NULL || !b->playing)
            continue;
        Apply3D(b);
    }
}
