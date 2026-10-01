#ifndef COMMON_FMV_FMVAUDIO_H_
#define COMMON_FMV_FMVAUDIO_H_

#include <stdint.h>
#include <deque>

#include "Ffmpeg.h"

struct IXAudio2;
struct IXAudio2SourceVoice;

// A movie's audio for both engines' players: decoded frames queued to an XAudio2 voice of its own, a fraction of
// a second ahead, and the movie's clock, which follows the samples played. Without audio, or once the audio has
// played out (a track shorter than its video), the clock runs on the wall clock, so the picture never waits for
// sound that is not coming.
class FmvAudio {
public:
    // A voice on the device for `channels` channels of 16-bit PCM at `sampleRate`. outputMatrix, if not null, is
    // XAudio2's: outputChannels rows of `channels` levels (each output's level of each input). False if there is no
    // device or the voice cannot be made, in which case the movie plays silently on the wall clock.
    bool Open(IXAudio2 *device, int channels, int sampleRate, const float *outputMatrix, int outputChannels);
    void Close();
    bool IsOpen() const { return voice_ != nullptr; }

    // Appends a decoded frame (16-bit, planar or interleaved); full chunks are submitted as they fill
    void Queue(const AVFrame *frame);
    // The stream has ended: submits what is left
    void Finish();
    // Seconds queued and not yet played
    double QueuedSeconds();
    // Whether more audio is wanted to stay the lead ahead
    bool WantsMore();
    // Finished and all played (always true without a voice)
    bool PlayedOut();

    void SetVolume(float amplitude);

    // Starts the voice and the clock. Call once the first frame is ready.
    void Start();
    // The movie's time in seconds
    double Clock();

private:
    void Submit();
    void Reap();

    IXAudio2SourceVoice *voice_ = nullptr;
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
