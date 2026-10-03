#ifndef DRIVING_DATA_DEBUGVARUNTESTED_H_
#define DRIVING_DATA_DEBUGVARUNTESTED_H_

// The warning beside a provisional port in the debug-variable, tuning and ini code (DebugVariables.cpp,
// StdStreams.cpp, Tuning.cpp, IniFiles.cpp): code neither the disc's data nor the shadow test (DebugVarShadow.cpp)
// reaches, ported from the listing without a test. It says so once, the first time it runs, so that whatever first
// reaches it gets checked against the original.

#include <stdio.h>

inline void DebugVarUntested(const char *what) {
    printf("[data] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define DEBUGVAR_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            DebugVarUntested(what); \
        } \
    } while (0)

#endif // DRIVING_DATA_DEBUGVARUNTESTED_H_
