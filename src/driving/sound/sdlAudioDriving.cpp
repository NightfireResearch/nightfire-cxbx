#include "DrivingAudio.h"

#include "../../common/sound/voiceMixer.h"
#include "../platform/Host.h"

#include <math.h>

// The sound output on SDL3 (NIGHTFIRE_SDL): the voice mixer (common/sound/voiceMixer.h), which reads each buffer in
// place at its own rate, with the volume and the stereo pair the voice's mixbins make, as the XAudio2 body's source
// voices do.

#define MAX_MIXBINS 8

struct DrivingVoice {
    MixerVoice *voice;
    uint32_t binCount;
    uint32_t bins[MAX_MIXBINS];
    int32_t binVolumes[MAX_MIXBINS];
};

// The bins' lock: the game's threads all set them
static HostLock BinLock;

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

// The stereo pair from the bins: each bin's level, placed on the pair
static void ApplyPair(DrivingVoice *v) {
    float left = 0.0f, right = 0.0f;
    for (uint32_t i = 0; i < v->binCount; i++) {
        float l, r;
        MixBinToStereo(v->bins[i], &l, &r);
        float g = HundredthsDbToAmplitude(v->binVolumes[i]);
        left += l * g;
        right += r * g;
    }
    if (v->binCount == 0) { left = 1.0f; right = 1.0f; }   // no bins set: plain mono to both
    VoiceMixer_SetGains(v->voice, left > 1.0f ? 1.0f : left, right > 1.0f ? 1.0f : right);
}

void DrivingAudio_StereoFold(float matrix[12]) {
    for (uint32_t speaker = 0; speaker < 6; speaker++)
        MixBinToStereo(speaker, &matrix[speaker * 2], &matrix[speaker * 2 + 1]);
}

bool DrivingAudio_Start(void) {
    static bool locked = false;
    if (!locked) {
        locked = true;
        HostInitLock(&BinLock);
    }
    return VoiceMixer_Start();
}

uint32_t DrivingAudio_GetDevice(void) {
    return VoiceMixer_Device();
}

DrivingVoice *DrivingAudio_CreateVoice(uint32_t sampleRate, uint16_t formatTag, uint16_t channels,
                                       uint16_t blockAlign, uint16_t bitsPerSample) {
    MixerVoice *voice = VoiceMixer_CreateVoice(sampleRate, formatTag, channels, blockAlign, bitsPerSample);
    if (voice == NULL)
        return NULL;
    DrivingVoice *v = new DrivingVoice();
    v->voice = voice;
    ApplyPair(v);
    return v;
}

void DrivingAudio_Release(DrivingVoice *v) {
    if (v == NULL)
        return;
    VoiceMixer_Release(v->voice);
    delete v;
}

void DrivingAudio_SetData(DrivingVoice *v, const void *data, uint32_t bytes) {
    if (v != NULL)
        VoiceMixer_SetData(v->voice, data, bytes);
}

void DrivingAudio_Play(DrivingVoice *v, bool looping) {
    if (v != NULL)
        VoiceMixer_Play(v->voice, looping);
}

void DrivingAudio_Stop(DrivingVoice *v) {
    if (v != NULL)
        VoiceMixer_Stop(v->voice);
}

bool DrivingAudio_IsPlaying(DrivingVoice *v) {
    return v != NULL && VoiceMixer_IsPlaying(v->voice);
}

void DrivingAudio_GetPosition(DrivingVoice *v, uint32_t *playCursor, uint32_t *writeCursor) {
    VoiceMixer_GetPosition(v != NULL ? v->voice : NULL, playCursor, writeCursor);
}

void DrivingAudio_SetPosition(DrivingVoice *v, uint32_t bytes) {
    if (v != NULL)
        VoiceMixer_SetPosition(v->voice, bytes);
}

void DrivingAudio_SetLoopRegion(DrivingVoice *v, uint32_t startBytes, uint32_t lengthBytes) {
    if (v != NULL)
        VoiceMixer_SetLoopRegion(v->voice, startBytes, lengthBytes);
}

void DrivingAudio_SetVolume(DrivingVoice *v, int32_t hundredthsDb) {
    if (v != NULL)
        VoiceMixer_SetVolume(v->voice, HundredthsDbToAmplitude(hundredthsDb));
}

// The XAudio2 body's limits on the ratio: four times up, 1/2000 down (the mixer's)
void DrivingAudio_SetFrequency(DrivingVoice *v, uint32_t hz) {
    if (v != NULL)
        VoiceMixer_SetFrequency(v->voice, hz);
}

void DrivingAudio_SetMixBins(DrivingVoice *v, const uint32_t *bins, const int32_t *volumes, uint32_t count) {
    if (v == NULL)
        return;
    HostEnterLock(&BinLock);
    v->binCount = 0;
    for (uint32_t i = 0; i < count && v->binCount < MAX_MIXBINS; i++) {
        v->bins[v->binCount] = bins[i];
        v->binVolumes[v->binCount] = volumes != NULL ? volumes[i] : 0;
        v->binCount++;
    }
    ApplyPair(v);
    HostLeaveLock(&BinLock);
}

void DrivingAudio_SetMixBinVolumes(DrivingVoice *v, const uint32_t *bins, const int32_t *volumes, uint32_t count) {
    if (v == NULL)
        return;
    HostEnterLock(&BinLock);
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
    ApplyPair(v);
    HostLeaveLock(&BinLock);
}

void DrivingAudio_Report(void) {
    VoiceMixer_Report();
}
