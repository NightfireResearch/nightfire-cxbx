#ifndef DRIVING_ENGINE_INPUTUNTESTED_H_
#define DRIVING_ENGINE_INPUTUNTESTED_H_

// The warning beside a provisional port in the input layer: code no shipped data reaches, ported from the listing
// without a test. It says so once, the first time it runs, so that whatever first reaches it gets checked against
// the original.

#include <stdio.h>

inline void InputUntested(const char *what) {
    printf("[input] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define INPUT_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            InputUntested(what); \
        } \
    } while (0)

#endif // DRIVING_ENGINE_INPUTUNTESTED_H_
