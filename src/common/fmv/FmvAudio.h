#ifndef COMMON_FMV_FMVAUDIO_H_
#define COMMON_FMV_FMVAUDIO_H_

#include <stdint.h>
#include <deque>

#include "Ffmpeg.h"

// The device a movie plays on: XAudio2's, or with NIGHTFIRE_SDL (the driving engine's SDL3 build) an SDL audio
// device id, whose stream of the game's own sound the movie's stream joins (FmvAudioSdl.cpp).
#ifdef NIGHTFIRE_SDL
struct SDL_AudioStream;
typedef uint32_t FmvAudioDevice;
#else
struct IXAudio2;
struct IXAudio2SourceVoice;
typedef IXAudio2 *FmvAudioDevice;
#endif

// A movie's audio for both engines' players: decoded frames queued to an XAudio2 voice (or SDL audio stream) of its
// own, a fraction of a second ahead, and the movie's clock, which follows the samples played. Without audio, or once the audio has
// played out (a track shorter than its video), the clock runs on the wall clock, so the picture never waits for
// sound that is not coming.
class FmvAudio {
public:
    // A voice on the device for `channels` channels of 16-bit PCM at `sampleRate`. outputMatrix, if not null, is
    // XAudio2's: outputChannels rows of `channels` levels (each output's level of each input). False if there is no
    // device or the voice cannot be made, in which case the movie plays silently on the wall clock - except in a time
    // demo (SetVirtualClock), where it opens without a voice, counting: the decoded frames are counted and dropped, so
    // that the movie still ends when its audio would, whether the host has a sound device or not.
    bool Open(FmvAudioDevice device, int channels, int sampleRate, const float *outputMatrix, int outputChannels);
    void Close();
    bool IsOpen() const { return voice_ != nullptr || counting_; }

    // Appends a decoded frame (16-bit, planar or interleaved); full chunks are submitted as they fill
    void Queue(const AVFrame *frame);
    // The stream has ended: submits what is left
    void Finish();
    // Seconds queued and not yet played
    double QueuedSeconds();
    // Whether more audio is wanted to stay the lead ahead
    bool WantsMore();
    // Finished and all played (always true without a voice, unless counting)
    bool PlayedOut();

    void SetVolume(float amplitude);

    // Starts the voice and the clock. Call once the first frame is ready.
    void Start();
    // The movie's time in seconds
    double Clock();

    // For a time demo (src/action/devtools/Demo.h): the clock follows `clock`, in seconds, instead of
    // the samples played, and the audio has played out once that clock passes its length
    static void SetVirtualClock(double (*clock)(void));

private:
    void Submit();
    void Reap();

#ifdef NIGHTFIRE_SDL
    SDL_AudioStream *voice_ = nullptr;
    uint32_t device_ = 0;
    float matrix_[2 * 8] = {};          // the output matrix, when there is one
    bool folding_ = false;
    int outputChannels_ = 0;
    int64_t framesQueued_ = 0;          // put into the stream since Open
#else
    IXAudio2SourceVoice *voice_ = nullptr;
#endif
    static double (*virtualClock_)(void);
    double startVirtual_ = 0.0;         // virtualClock_ at Start
    int64_t framesTotal_ = 0;           // queued since Open, for the virtual clock's PlayedOut
    bool counting_ = false;             // open without a voice in a time demo: frames counted, not played
    std::deque<int16_t *> chunks_;      // submitted, in order; freed once played
    int16_t *pending_ = nullptr;        // the chunk being filled
    int pendingFrames_ = 0;
    int channels_ = 0, sampleRate_ = 0;
    bool started_ = false, finished_ = false;
    bool drained_ = false;              // played out: the clock carries on from the wall clock
    double drainedAt_ = 0.0;
    int64_t drainedWall_ = 0, startWall_ = 0;
};

#endif // COMMON_FMV_FMVAUDIO_H_
