#include "FmvAudio.h"

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <string.h>

// A movie's audio on SDL3 (NIGHTFIRE_SDL): an audio stream of its own, bound to the device the game's sound plays on
// when the movie starts - SDL mixes the two. The decoded frames go into it interleaved, folded through the output
// matrix where there is one (the XAudio2 voice's SetOutputMatrix), and the clock is the frames the device has taken.

#define FMV_AUDIO_AHEAD 0.5           // seconds kept queued

double (*FmvAudio::virtualClock_)(void) = nullptr;

void FmvAudio::SetVirtualClock(double (*clock)(void)) {
    virtualClock_ = clock;
}

static double SecondsSince(uint64_t start) {
    return (double)(SDL_GetPerformanceCounter() - start) / (double)SDL_GetPerformanceFrequency();
}

bool FmvAudio::Open(FmvAudioDevice device, int channels, int sampleRate, const float *outputMatrix, int outputChannels) {
    Close();
    if (device == 0 || channels <= 0 || channels > 8 || sampleRate <= 0)
        return false;
    folding_ = outputMatrix != nullptr && outputChannels > 0 && outputChannels <= 2;
    outputChannels_ = folding_ ? outputChannels : channels;
    if (folding_)
        memcpy(matrix_, outputMatrix, sizeof(float) * outputChannels * channels);
    SDL_AudioSpec spec = { SDL_AUDIO_S16, outputChannels_, sampleRate };
    voice_ = SDL_CreateAudioStream(&spec, &spec);   // the output side becomes the device's when it is bound
    if (voice_ == nullptr)
        return false;
    device_ = device;
    channels_ = channels;
    sampleRate_ = sampleRate;
    framesQueued_ = 0;
    return true;
}

void FmvAudio::Close() {
    if (voice_ != nullptr) {
        SDL_DestroyAudioStream(voice_);   // unbinds it first
        voice_ = nullptr;
    }
    free(pending_);
    pending_ = nullptr;
    pendingFrames_ = channels_ = sampleRate_ = outputChannels_ = 0;
    device_ = 0;
    started_ = finished_ = drained_ = false;
}

// Nothing is held back here: SDL keeps what is queued
void FmvAudio::Reap() {
}

void FmvAudio::Submit() {
}

void FmvAudio::Queue(const AVFrame *frame) {
    if (voice_ == nullptr)
        return;
    bool planar = frame->format == AV_SAMPLE_FMT_S16P;
    int channels = frame->ch_layout.nb_channels < channels_ ? frame->ch_layout.nb_channels : channels_;
    int16_t *out = (int16_t *)malloc((size_t)frame->nb_samples * outputChannels_ * sizeof(int16_t));
    if (out == nullptr)
        return;
    float in[8] = {};
    for (int i = 0; i < frame->nb_samples; i++) {
        for (int c = 0; c < channels; c++)
            in[c] = planar ? ((const int16_t *)frame->extended_data[c])[i]
                           : ((const int16_t *)frame->extended_data[0])[i * frame->ch_layout.nb_channels + c];
        for (int o = 0; o < outputChannels_; o++) {
            float value = 0.0f;
            if (folding_) {
                for (int c = 0; c < channels_; c++)
                    value += matrix_[o * channels_ + c] * in[c];   // XAudio2's layout: each output's row of inputs
            } else {
                value = in[o];
            }
            out[i * outputChannels_ + o] = (int16_t)(value > 32767.0f ? 32767 : value < -32768.0f ? -32768 : value);
        }
    }
    SDL_PutAudioStreamData(voice_, out, frame->nb_samples * outputChannels_ * (int)sizeof(int16_t));
    framesQueued_ += frame->nb_samples;
    free(out);
}

void FmvAudio::Finish() {
    if (voice_ != nullptr)
        SDL_FlushAudioStream(voice_);
    finished_ = true;
}

double FmvAudio::QueuedSeconds() {
    if (voice_ == nullptr)
        return 1e9;
    int queued = SDL_GetAudioStreamQueued(voice_);
    return (double)queued / (outputChannels_ * (int)sizeof(int16_t)) / sampleRate_;
}

bool FmvAudio::WantsMore() {
    return voice_ != nullptr && !finished_ && QueuedSeconds() < FMV_AUDIO_AHEAD;
}

bool FmvAudio::PlayedOut() {
    if (voice_ == nullptr)
        return true;
    if (virtualClock_ != nullptr)
        return finished_ && Clock() >= (double)framesQueued_ / sampleRate_;
    return finished_ && SDL_GetAudioStreamQueued(voice_) == 0;
}

void FmvAudio::SetVolume(float amplitude) {
    if (voice_ != nullptr)
        SDL_SetAudioStreamGain(voice_, amplitude);
}

void FmvAudio::Start() {
    // Should it not bind, the movie plays silently on the wall clock rather than wait for a clock that never moves
    if (voice_ != nullptr && !SDL_BindAudioStream(device_, voice_)) {
        SDL_DestroyAudioStream(voice_);
        voice_ = nullptr;
    }
    started_ = true;
    startWall_ = (int64_t)SDL_GetPerformanceCounter();
    startVirtual_ = virtualClock_ != nullptr ? virtualClock_() : 0.0;
}

double FmvAudio::Clock() {
    if (virtualClock_ != nullptr)
        return started_ ? virtualClock_() - startVirtual_ : 0.0;
    if (voice_ == nullptr || !started_)
        return started_ ? SecondsSince((uint64_t)startWall_) : 0.0;
    if (drained_)
        return drainedAt_ + SecondsSince((uint64_t)drainedWall_);
    int queued = SDL_GetAudioStreamQueued(voice_) / (outputChannels_ * (int)sizeof(int16_t));
    double t = (double)(framesQueued_ - queued) / sampleRate_;
    if (finished_ && queued == 0) {
        drained_ = true;
        drainedAt_ = t;
        drainedWall_ = (int64_t)SDL_GetPerformanceCounter();
    }
    return t;
}
