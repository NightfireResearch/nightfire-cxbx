#include "xaudio2Backend.h"
#include "pcmCache.h"
#include "../../common/sound/voiceMixer.h"
#include "../../common/sound/xadpcm.h"
#include "../devtools/Demo.h"
#include "../../driving/platform/Ptr32.h"
#include "../math/math.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The audio seam's backend on SDL3 (NIGHTFIRE_SDL): the XAudio2 backend's buffers (xaudio2Backend.cpp, which says
// what the game asks of them) on the voice mixer (common/sound/voiceMixer.h). A buffer's ADPCM is decoded once into
// the PCM cache and a voice plays the PCM; positions cross the seam in ADPCM bytes, as the game speaks them.
//
// What X3DAudio does for the XAudio2 backend is done here: a 3D voice's gains are the game's rolloff curve over its
// distance (flat to the minimum distance, as BuildDistanceCurve there builds it) times a constant-power pan between
// speakers at 45 degrees either side of the listener's front, across the front arc or the back one. There is no
// reverb: the voices' I3DL2 sends are kept and not used.
// ---------------------------------------------------------------------------------------------------------------

// The rate the game creates every one of its buffers at (see xboxCreateSoundBuffers)
#define XBOX_SAMPLE_RATE 44032u

#define SDL_BUFFER_MAGIC 0x4c445342u // 'BSDL'

struct SdlBuffer {
    uint32_t magic;
    int index;                      // in creation order, which is the game's: how a time demo names it
    int channels;
    bool is3d;

    MixerVoice *voice;              // made on the first bind of real data
    const int16_t *pcm;             // owned by the cache
    size_t pcmSamples;              // per channel

    uint32_t frequency;
    int32_t volume;                 // hundredths of a dB, <= 0
    uint32_t headroom;              // hundredths of a dB of extra attenuation
    uint32_t loopStartBytes;        // ADPCM bytes
    uint32_t loopLengthBytes;
    uint32_t startPositionBytes;
    float mixBinDb[11];             // per mixbin, hundredths of a dB
    bool mixBinsSet;

    float posX, posY, posZ;
    float minDistance, maxDistance;
    int32_t i3dl2Room;              // the reverb send the game asks for

    bool playing;
    bool hasResume;                 // a Stop left the cursor where the next Play goes on from
};

static SdlBuffer *AsBuffer(DSoundBuffer *p) {
    SdlBuffer *b = (SdlBuffer *)p;
    if (b == NULL || b->magic != SDL_BUFFER_MAGIC) {
        DSound_BackendMissing("buffer pointer that did not come from this backend");
        return NULL;
    }
    return b;
}

static SdlBuffer *g_buffers[256];
static int g_bufferCount = 0;
static bool g_started = false;
static uint32_t g_deviceObject = 0xD5000000u;

bool PcmCache_InUse(const int16_t *pcm) {
    if (pcm == NULL)
        return false;
    for (int i = 0; i < g_bufferCount; i++)
        if (g_buffers[i] != NULL && g_buffers[i]->pcm == pcm)
            return true;
    return false;
}

// The listener: where it is, and its front, top and right, orthonormal
static float g_listenerPos[3];
static float g_front[3] = {0.0f, 0.0f, 1.0f};
static float g_top[3] = {0.0f, 1.0f, 0.0f};
static float g_right[3] = {1.0f, 0.0f, 0.0f};

// The game's own rolloff curve, as handed to SetRolloffCurve
static float g_rolloffCurve[8];
static uint32_t g_rolloffPoints = 0;

// ---- units

static float HundredthsDbToAmplitude(float hundredths) {
    if (hundredths <= -10000.0f)
        return 0.0f;
    if (hundredths >= 0.0f)
        return 1.0f;
    return powf(10.0f, hundredths / 2000.0f);
}

// trueVolume = mixbinVolume + 3DVolume + volume - headroom (xboxCreateSoundBuffers): the per-voice part
static float VoiceAmplitude(const SdlBuffer *b) {
    return HundredthsDbToAmplitude((float)b->volume - (float)b->headroom);
}

// Where each Xbox mixbin lands on a stereo pair: the XAudio2 backend's (MixBinToStereo there says why)
static void MixBinToStereo(uint32_t mixBin, float *outLeft, float *outRight) {
    const float c = 0.7071068f;
    switch (mixBin) {
        case 0:  *outLeft = 1.0f; *outRight = 0.0f; return; // FRONT_LEFT
        case 1:  *outLeft = 0.0f; *outRight = 1.0f; return; // FRONT_RIGHT
        case 2:  *outLeft = c;    *outRight = c;    return; // FRONT_CENTER
        case 3:  *outLeft = 0.0f; *outRight = 0.0f; return; // LOW_FREQUENCY
        case 4:  *outLeft = c;    *outRight = 0.0f; return; // BACK_LEFT
        case 5:  *outLeft = 0.0f; *outRight = c;    return; // BACK_RIGHT
        case 6:  *outLeft = 1.0f; *outRight = 0.0f; return; // XTLK_FRONT_LEFT
        case 7:  *outLeft = 0.0f; *outRight = 1.0f; return; // XTLK_FRONT_RIGHT
        case 8:  *outLeft = c;    *outRight = 0.0f; return; // XTLK_BACK_LEFT
        case 9:  *outLeft = 0.0f; *outRight = c;    return; // XTLK_BACK_RIGHT
        default: *outLeft = 0.0f; *outRight = 0.0f; return; // I3DL2 (10)
    }
}

// The mixer's bytes (16-bit PCM) for an ADPCM byte offset, and back
static uint32_t PcmBytes(const SdlBuffer *b, uint32_t adpcmBytes) {
    return (uint32_t)(XAdpcm_ByteOffsetToSample(adpcmBytes, b->channels) * (size_t)b->channels * 2);
}

static uint32_t AdpcmBytes(const SdlBuffer *b, uint32_t pcmBytes) {
    return (uint32_t)XAdpcm_SampleToByteOffset(pcmBytes / ((uint32_t)b->channels * 2), b->channels);
}

// ---- device

static bool EnsureDevice(void) {
    if (!g_started) {
        g_started = true;
        if (VoiceMixer_Start())
            printf("[sound] SDL output; no reverb, so 3D voices are dry\n");
    }
    return VoiceMixer_Device() != 0;
}

uint32_t XA2_GetDevice(void) {
    return EnsureDevice() ? VoiceMixer_Device() : 0;
}

void XA2_DirectSoundCreate(void *lpGuid, DSoundObject **ppDS, void *pUnknown) {
    (void)lpGuid;
    (void)pUnknown;
    EnsureDevice();
    if (ppDS != NULL)
        *ppDS = (DSoundObject *)&g_deviceObject;
}

void XA2_DirectSoundUseFullHRTF(void) {
    DSound_BackendMissing("DirectSoundUseFullHRTF (no HRTF)");
}

void XA2_IDirectSound_DownloadEffectsImage(DSoundObject *thisPtr, const void *pvImageBuffer,
                                           uint32_t dwImageSize, void *pImageLoc, void **ppImageDesc) {
    (void)thisPtr;
    (void)pvImageBuffer;
    (void)dwImageSize;
    // The game stores both outputs and reads back neither
    if (pImageLoc != NULL)
        *(uint32_t *)pImageLoc = 0;
    if (ppImageDesc != NULL)
        *ppImageDesc = NULL;
    DSound_BackendMissing("IDirectSound_DownloadEffectsImage (no I3DL2 reverb)");
}

// ---- listener

void XA2_IDirectSound_SetPosition(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)thisPtr;
    (void)dwApply;
    g_listenerPos[0] = x;
    g_listenerPos[1] = y;
    g_listenerPos[2] = z;
}

void XA2_IDirectSound_SetVelocity(DSoundObject *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)thisPtr; (void)x; (void)y; (void)z; (void)dwApply;
    // Always zero in this game: no Doppler
}

// The front normalised, the top made perpendicular to it (Gram-Schmidt), and right = top x front (left-handed, as
// the game's coordinates are)
void XA2_IDirectSound_SetOrientation(DSoundObject *thisPtr, float xFront, float yFront, float zFront,
                                     float xTop, float yTop, float zTop, uint32_t dwApply) {
    (void)thisPtr;
    (void)dwApply;
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
        if (fabsf(fy) < 0.9f) { tx = 0.0f; ty = 1.0f; tz = 0.0f; }
        else                  { tx = 1.0f; ty = 0.0f; tz = 0.0f; }
        dot = tx * fx + ty * fy + tz * fz;
        tx -= fx * dot; ty -= fy * dot; tz -= fz * dot;
        tlen = sqrtf(tx * tx + ty * ty + tz * tz);
    }
    tx /= tlen; ty /= tlen; tz /= tlen;
    g_front[0] = fx; g_front[1] = fy; g_front[2] = fz;
    g_top[0] = tx; g_top[1] = ty; g_top[2] = tz;
    g_right[0] = ty * fz - tz * fy;
    g_right[1] = tz * fx - tx * fz;
    g_right[2] = tx * fy - ty * fx;
}

// ---- buffers

void XA2_IDirectSound_CreateSoundBuffer(DSoundObject *thisPtr, DSBUFFERDESC_Xbox *pdsbd,
                                        uint32_t *ppBuffer, uint32_t *ppUnknown) {
    (void)thisPtr;
    (void)ppUnknown;
    if (ppBuffer == NULL)
        return;
    *ppBuffer = 0;
    if (pdsbd == NULL || pdsbd->lpwfxFormat == NULL)
        return;

    // The game keeps the buffer as a word: from new, which a 64-bit build serves from the game window
    SdlBuffer *b = new SdlBuffer();
    b->magic = SDL_BUFFER_MAGIC;
    int channels = pdsbd->lpwfxFormat->nChannels;
    b->channels = channels < 1 ? 1 : channels > 2 ? 2 : channels;
    b->is3d = (pdsbd->dwFlags & 0x10u) != 0; // DSBCAPS_CTRL3D
    b->frequency = pdsbd->lpwfxFormat->nSamplesPerSec ? pdsbd->lpwfxFormat->nSamplesPerSec : XBOX_SAMPLE_RATE;
    b->minDistance = 1.0f;
    b->maxDistance = 1000.0f;

    b->index = g_bufferCount;
    if (g_bufferCount < (int)(sizeof(g_buffers) / sizeof(g_buffers[0])))
        g_buffers[g_bufferCount++] = b;
    else
        DSound_BackendMissing("more sound buffers than the backend tracks");

    *ppBuffer = PtrTo32(b);
}

static void ApplyAmplitude(SdlBuffer *b);
static void ApplyMixMatrix(SdlBuffer *b);
static void ApplyFrequency(SdlBuffer *b);
static void Apply3D(SdlBuffer *b);

// The voice, once there is a device, with what the game set before it existed (the mixbins and headroom are set for
// every buffer at boot, the frequency before anything plays)
static bool EnsureVoice(SdlBuffer *b) {
    if (b->voice != NULL)
        return true;
    if (!EnsureDevice())
        return false;
    b->voice = VoiceMixer_CreateVoice(XBOX_SAMPLE_RATE, 0x0001, (uint16_t)b->channels, (uint16_t)(b->channels * 2),
                                      16);
    if (b->voice == NULL)
        return false;
    ApplyFrequency(b);
    ApplyAmplitude(b);
    if (b->is3d)
        VoiceMixer_SetGains(b->voice, 0.0f, 0.0f);   // until its first Apply3D
    else
        ApplyMixMatrix(b);
    return true;
}

void XA2_IDirectSoundBuffer_SetBufferData(DSoundBuffer *thisPtr, void *pvBufferData, uint32_t dwBufferBytes) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    // Unbound or rebound, the buffer stops playing what it held
    if (b->voice != NULL)
        VoiceMixer_SetData(b->voice, NULL, 0);
    b->pcm = NULL;
    b->pcmSamples = 0;
    b->playing = false;
    b->hasResume = false;
    if (pvBufferData == NULL || dwBufferBytes == 0)
        return;

    size_t values = 0;
    const int16_t *pcm = PcmCache_Decode(pvBufferData, dwBufferBytes, b->channels, &values);
    if (pcm == NULL || !EnsureVoice(b))
        return;
    b->pcm = pcm;
    b->pcmSamples = values / (size_t)b->channels;
    b->startPositionBytes = 0;
    VoiceMixer_SetData(b->voice, pcm, (uint32_t)(values * sizeof(int16_t)));
}

void XA2_NotifyBufferDataWritten(const void *pvBufferData, uint32_t offset, uint32_t length) {
    PcmCache_Written(pvBufferData, offset, length);
}

// ---- playback

void XA2_IDirectSoundBuffer_SetLoopRegion(DSoundBuffer *thisPtr, uint32_t dwLoopStart, uint32_t dwLoopLength) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->loopStartBytes = dwLoopStart;
    b->loopLengthBytes = dwLoopLength;
}

void XA2_IDirectSoundBuffer_SetCurrentPosition(DSoundBuffer *thisPtr, uint32_t dwPlayCursor) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->startPositionBytes = dwPlayCursor;
    b->hasResume = false;
}

void XA2_IDirectSoundBuffer_Play(DSoundBuffer *thisPtr, uint32_t dwReserved1, uint32_t dwReserved2, uint32_t dwFlags) {
    (void)dwReserved1;
    (void)dwReserved2;
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL || b->pcm == NULL || b->pcmSamples == 0 || b->voice == NULL)
        return;

    uint32_t pcmTotal = (uint32_t)(b->pcmSamples * (size_t)b->channels * 2);
    bool looping = (dwFlags & 0x1u) != 0; // DSBPLAY_LOOPING
    if (looping) {
        // A zero length means to the end, as it does on the Xbox
        uint32_t start = PcmBytes(b, b->loopStartBytes);
        if (start >= pcmTotal)
            start = 0;
        uint32_t length = PcmBytes(b, b->loopLengthBytes);
        if (length == 0 || start + length > pcmTotal)
            length = pcmTotal - start;
        VoiceMixer_SetLoopRegion(b->voice, start, length);
    }
    // Resume where a Stop left off (psiSamplePause/psiSampleUnPause), unless SetCurrentPosition has since said
    // otherwise
    if (!b->hasResume) {
        uint32_t start = PcmBytes(b, b->startPositionBytes);
        VoiceMixer_SetPosition(b->voice, start < pcmTotal ? start : 0);
    }
    b->hasResume = false;

    // Positioned before it makes a sound, not on the next frame's DoWork, which runs before the play pass
    if (b->is3d)
        Apply3D(b);
    VoiceMixer_Play(b->voice, looping);
    b->playing = true;
}

void XA2_IDirectSoundBuffer_Stop(DSoundBuffer *thisPtr) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    if (b->voice != NULL && b->playing) {
        VoiceMixer_Stop(b->voice);
        b->hasResume = true;
    }
    b->playing = false;
}

static void BufferStatus(SdlBuffer *b, uint32_t *pdwStatus) {
    *pdwStatus = 0;
    if (b == NULL || b->voice == NULL || !b->playing)
        return;
    if (VoiceMixer_IsPlaying(b->voice))
        *pdwStatus = 0x1u; // DSBSTATUS_PLAYING
    else
        b->playing = false;
}

// What the game learns of the device's progress: the status and play cursor. A time demo records each answer, and
// its playback gives the recorded one in its place (devtools/Demo.h).
void XA2_IDirectSoundBuffer_GetStatus(DSoundBuffer *thisPtr, uint32_t *pdwStatus) {
    if (pdwStatus == NULL)
        return;
    SdlBuffer *b = AsBuffer(thisPtr);
    BufferStatus(b, pdwStatus);
    Demo_SoundAnswer(b != NULL ? b->index : -1, DEMO_SOUND_STATUS, pdwStatus);
}

void XA2_IDirectSoundBuffer_GetCurrentPosition(DSoundBuffer *thisPtr, uint32_t *pdwPlayCursor, uint32_t *pdwWriteCursor) {
    if (pdwPlayCursor != NULL)
        *pdwPlayCursor = 0;
    if (pdwWriteCursor != NULL)
        *pdwWriteCursor = 0;
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b != NULL && b->voice != NULL && b->pcmSamples > 0 && pdwPlayCursor != NULL) {
        uint32_t play = 0;
        VoiceMixer_GetPosition(b->voice, &play, NULL);
        *pdwPlayCursor = AdpcmBytes(b, play);
    }
    if (pdwPlayCursor != NULL)
        Demo_SoundAnswer(b != NULL ? b->index : -1, DEMO_SOUND_PLAY_CURSOR, pdwPlayCursor);
}

// ---- volume, frequency and routing

static void ApplyAmplitude(SdlBuffer *b) {
    VoiceMixer_SetVolume(b->voice, VoiceAmplitude(b));
}

void XA2_IDirectSoundBuffer_SetVolume(DSoundBuffer *thisPtr, int32_t lVolume) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->volume = lVolume;
    ApplyAmplitude(b);
}

void XA2_IDirectSoundBuffer_SetHeadroom(DSoundBuffer *thisPtr, uint32_t dwHeadroom) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->headroom = dwHeadroom;
    ApplyAmplitude(b);
}

// The XAudio2 backend's limits on the ratio
static void ApplyFrequency(SdlBuffer *b) {
    if (b->voice == NULL || b->frequency == 0)
        return;
    float ratio = (float)b->frequency / (float)XBOX_SAMPLE_RATE;
    if (ratio < 0.03125f) ratio = 0.03125f;
    if (ratio > 2.0f) ratio = 2.0f;
    VoiceMixer_SetFrequency(b->voice, (uint32_t)(ratio * XBOX_SAMPLE_RATE + 0.5f));
}

void XA2_IDirectSoundBuffer_SetFrequency(DSoundBuffer *thisPtr, uint32_t dwFrequency) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL || dwFrequency == 0)
        return;
    b->frequency = dwFrequency;
    ApplyFrequency(b);
}

// A 2D voice's gains from its mixbins, normalised rather than clipped (a 3D voice's are Apply3D's)
static void ApplyMixMatrix(SdlBuffer *b) {
    if (b->voice == NULL || b->is3d)
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
    float peak = left > right ? left : right;
    if (peak > 1.0f) {
        left /= peak;
        right /= peak;
    }
    VoiceMixer_SetGains(b->voice, left, right);
}

void XA2_IDirectSoundBuffer_SetMixBins(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins) {
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL || pMixBins == NULL || pMixBins->lpMixBinVolumePairs == NULL)
        return;
    for (int i = 0; i < 11; i++)
        b->mixBinDb[i] = -10000.0f;
    for (uint32_t i = 0; i < pMixBins->dwMixBinCount; i++) {
        uint32_t bin = pMixBins->lpMixBinVolumePairs[i].dwMixBin;
        if (bin < 11)
            b->mixBinDb[bin] = (float)pMixBins->lpMixBinVolumePairs[i].lVolume;
    }
    b->mixBinsSet = true;
    ApplyMixMatrix(b);
}

void XA2_IDirectSoundBuffer_SetMixBinVolumes(DSoundBuffer *thisPtr, DSMIXBINS_Xbox *pMixBins) {
    SdlBuffer *b = AsBuffer(thisPtr);
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

// ---- per-voice 3D

void XA2_IDirectSoundBuffer_SetPosition(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)dwApply;
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b == NULL)
        return;
    b->posX = x;
    b->posY = y;
    b->posZ = z;
}

void XA2_IDirectSoundBuffer_SetVelocity(DSoundBuffer *thisPtr, float x, float y, float z, uint32_t dwApply) {
    (void)thisPtr; (void)x; (void)y; (void)z; (void)dwApply;
}

void XA2_IDirectSoundBuffer_SetMinDistance(DSoundBuffer *thisPtr, float flMinDistance, uint32_t dwApply) {
    (void)dwApply;
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b != NULL)
        b->minDistance = flMinDistance;
}

void XA2_IDirectSoundBuffer_SetMaxDistance(DSoundBuffer *thisPtr, float flMaxDistance, uint32_t dwApply) {
    (void)dwApply;
    SdlBuffer *b = AsBuffer(thisPtr);
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

void XA2_IDirectSoundBuffer_SetI3DL2Source(DSoundBuffer *thisPtr, DSI3DL2BUFFER_Xbox *pds3db, uint32_t dwApply) {
    (void)dwApply;
    SdlBuffer *b = AsBuffer(thisPtr);
    if (b != NULL && pds3db != NULL)
        b->i3dl2Room = pds3db->lRoom;
}

// The rolloff curve's level at a distance: full out to the minimum distance, then the game's points spread evenly
// to the maximum, and the last point beyond it (linear between two points if the game has set none)
static float DistanceGain(const SdlBuffer *b, float distance) {
    static const float kLinear[2] = {1.0f, 0.0f};
    const float *curve = g_rolloffPoints >= 2 ? g_rolloffCurve : kLinear;
    uint32_t points = g_rolloffPoints >= 2 ? g_rolloffPoints : 2;
    float maxD = b->maxDistance > 0.0f ? b->maxDistance : 1.0f;
    float minD = b->minDistance >= 0.0f && b->minDistance < maxD ? b->minDistance : 0.0f;
    float flat = minD / maxD;
    if (flat > 0.9f)
        flat = 0.9f;
    float d = distance / maxD;
    if (d <= flat)
        return curve[0];
    if (d >= 1.0f)
        return curve[points - 1];
    float t = (d - flat) / (1.0f - flat) * (float)(points - 1);
    uint32_t i = (uint32_t)t;
    if (i >= points - 1)
        return curve[points - 1];
    return curve[i] + (curve[i + 1] - curve[i]) * (t - (float)i);
}

// A 3D voice's gains: its distance's level, panned by its direction in the listener's horizontal plane between
// speakers at -45 (left) and +45 degrees (right), at constant power; behind, across the back arc from right to left
static void Apply3D(SdlBuffer *b) {
    if (b->voice == NULL)
        return;
    float vx = b->posX - g_listenerPos[0], vy = b->posY - g_listenerPos[1], vz = b->posZ - g_listenerPos[2];
    float distance = sqrtf(vx * vx + vy * vy + vz * vz);
    float level = DistanceGain(b, distance);

    float x = vx * g_right[0] + vy * g_right[1] + vz * g_right[2];
    float z = vx * g_front[0] + vy * g_front[1] + vz * g_front[2];
    float left = 0.7071068f, right = 0.7071068f;
    if (x * x + z * z > 1e-8f) {
        float azimuth = atan2f(x, z);   // 0 ahead, positive to the right
        const float quarter = PI / 4.0f;
        if (azimuth >= -quarter && azimuth <= quarter) {
            float t = (azimuth + quarter) / (PI / 2.0f) * (PI / 2.0f);
            left = cosf(t);
            right = sinf(t);
        } else {
            float around = azimuth < 0.0f ? azimuth + 2.0f * PI : azimuth;   // quarter .. 7 quarters
            float t = (around - quarter) / (1.5f * PI) * (PI / 2.0f);
            right = cosf(t);
            left = sinf(t);
        }
    }
    VoiceMixer_SetGains(b->voice, left * level, right * level);
}

// ---- per-frame work

void XA2_IDirectSound_CommitDeferredSettings(DSoundObject *thisPtr) {
    (void)thisPtr;
}

// Once a frame: the 3D voices, whose gains depend on where the listener ended up
void XA2_DirectSoundDoWork(void) {
    for (int i = 0; i < g_bufferCount; i++) {
        SdlBuffer *b = g_buffers[i];
        if (b != NULL && b->is3d && b->voice != NULL && b->playing)
            Apply3D(b);
    }
}
