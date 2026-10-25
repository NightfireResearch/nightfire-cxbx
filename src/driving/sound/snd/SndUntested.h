#ifndef DRIVING_SOUND_SND_SNDUNTESTED_H_
#define DRIVING_SOUND_SND_SNDUNTESTED_H_

// The warning beside a provisional port of the sound library (the pattern of eagl/anim/AnimUntested.h): said once,
// the first time the code runs, so whatever first reaches it gets checked against the original. Every sound module
// takes SND_UNTESTED from here.

#include <stdio.h>

inline void SndUntested(const char *what) {
    printf("[snd] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define SND_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            SndUntested(what); \
        } \
    } while (0)

#endif // DRIVING_SOUND_SND_SNDUNTESTED_H_
