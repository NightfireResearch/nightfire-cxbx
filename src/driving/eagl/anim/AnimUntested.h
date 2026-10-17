#ifndef DRIVING_EAGL_ANIM_ANIMUNTESTED_H_
#define DRIVING_EAGL_ANIM_ANIMUNTESTED_H_

// The warning beside a provisional port: code no shipped data reaches (the never-built FnAnim types and their
// helpers, docs/driving/eagl.md 3.4), ported from the listing without a test. It says so once, the first time it
// runs, so that whatever first reaches it gets checked against the original.

#include <stdio.h>

inline void EaglUntested(const char *what) {
    printf("[eagl] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define EAGL_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            EaglUntested(what); \
        } \
    } while (0)

#endif // DRIVING_EAGL_ANIM_ANIMUNTESTED_H_
