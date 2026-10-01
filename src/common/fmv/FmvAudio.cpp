#include "FmvAudio.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <xaudio2.h>
#include <stdlib.h>
#include <string.h>

#define FMV_AUDIO_CHUNK 4096          // sample frames per XAudio2 buffer
#define FMV_AUDIO_AHEAD 0.5           // seconds kept queued

static int64_t Now(void) {
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return t.QuadPart;
}

static double SecondsSince(int64_t start) {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    return (double)(Now() - start) / freq.QuadPart;
}

bool FmvAudio::Open(IXAudio2 *device, int channels, int sampleRate, const float *outputMatrix, int outputChannels) {
    Close();
    if (device == nullptr || channels <= 0 || sampleRate <= 0)
        return false;
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = (WORD)channels;
    wfx.nSamplesPerSec = (DWORD)sampleRate;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (WORD)(channels * 2);
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
    if (FAILED(device->CreateSourceVoice(&voice_, &wfx))) {
        voice_ = nullptr;
        return false;
    }
    if (outputMatrix != nullptr)
        voice_->SetOutputMatrix(nullptr, (UINT32)channels, (UINT32)outputChannels, outputMatrix);
    channels_ = channels;
    sampleRate_ = sampleRate;
    return true;
}

void FmvAudio::Close() {
    if (voice_ != nullptr) {
        voice_->DestroyVoice();           // synchronous: no buffer is read after this returns
        voice_ = nullptr;
    }
    for (int16_t *c : chunks_)
        free(c);
    chunks_.clear();
    free(pending_);
    pending_ = nullptr;
    pendingFrames_ = channels_ = sampleRate_ = 0;
    started_ = finished_ = drained_ = false;
}

void FmvAudio::Reap() {
    if (voice_ == nullptr)
        return;
    XAUDIO2_VOICE_STATE state;
    voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    while (chunks_.size() > state.BuffersQueued) {
        free(chunks_.front());
        chunks_.pop_front();
    }
}

void FmvAudio::Submit() {
    if (pending_ == nullptr || pendingFrames_ == 0)
        return;
    XAUDIO2_BUFFER buffer = {};
    buffer.AudioBytes = (UINT32)(pendingFrames_ * channels_ * sizeof(int16_t));
    buffer.pAudioData = (const BYTE *)pending_;
    if (SUCCEEDED(voice_->SubmitSourceBuffer(&buffer)))
        chunks_.push_back(pending_);
    else
        free(pending_);
    pending_ = nullptr;
    pendingFrames_ = 0;
}

void FmvAudio::Queue(const AVFrame *frame) {
    if (voice_ == nullptr)
        return;
    bool planar = frame->format == AV_SAMPLE_FMT_S16P;
    int channels = frame->ch_layout.nb_channels < channels_ ? frame->ch_layout.nb_channels : channels_;
    for (int i = 0; i < frame->nb_samples; i++) {
        if (pending_ == nullptr) {
            pending_ = (int16_t *)calloc((size_t)FMV_AUDIO_CHUNK * channels_, sizeof(int16_t));
            pendingFrames_ = 0;
        }
        int16_t *out = pending_ + pendingFrames_ * channels_;
        for (int c = 0; c < channels; c++)
            out[c] = planar ? ((const int16_t *)frame->extended_data[c])[i]
                            : ((const int16_t *)frame->extended_data[0])[i * frame->ch_layout.nb_channels + c];
        if (++pendingFrames_ == FMV_AUDIO_CHUNK)
            Submit();
    }
}

void FmvAudio::Finish() {
    if (voice_ != nullptr)
        Submit();
    finished_ = true;
}

double FmvAudio::QueuedSeconds() {
    if (voice_ == nullptr)
        return 1e9;
    Reap();
    XAUDIO2_VOICE_STATE state;
    voice_->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return (double)state.BuffersQueued * FMV_AUDIO_CHUNK / sampleRate_;
}

bool FmvAudio::WantsMore() {
    return voice_ != nullptr && !finished_ && QueuedSeconds() < FMV_AUDIO_AHEAD;
}

bool FmvAudio::PlayedOut() {
    if (voice_ == nullptr)
        return true;
    Reap();
    return finished_ && pending_ == nullptr && chunks_.empty();
}

void FmvAudio::SetVolume(float amplitude) {
    if (voice_ != nullptr)
        voice_->SetVolume(amplitude);
}

void FmvAudio::Start() {
    if (voice_ != nullptr)
        voice_->Start();
    started_ = true;
    startWall_ = Now();
}

double FmvAudio::Clock() {
    if (voice_ == nullptr || !started_)
        return started_ ? SecondsSince(startWall_) : 0.0;
    if (drained_)
        return drainedAt_ + SecondsSince(drainedWall_);
    XAUDIO2_VOICE_STATE state;
    voice_->GetState(&state);
    double t = (double)state.SamplesPlayed / sampleRate_;
    if (finished_ && state.BuffersQueued == 0 && pending_ == nullptr) {
        drained_ = true;
        drainedAt_ = t;
        drainedWall_ = Now();
    }
    return t;
}
