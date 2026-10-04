#ifndef DRIVING_ENGINE_COREFOUNDATION_H_
#define DRIVING_ENGINE_COREFOUNDATION_H_

#include <stdio.h>

// ---------------------------------------------------------------------------------------------------------------
// Odds and ends of the engine's foundation layer: the video mode the game chose at startup and its refresh rate,
// the assertion message hook, the empty functions the compiler left several copies of, the STL's length_error
// throw and the core's warning for provisional ports. See CoreFoundation.cpp.
// ---------------------------------------------------------------------------------------------------------------

// The video mode (1 NTSC, 2 PAL 50 Hz, 3 PAL 60 Hz, as ApplicationMemoryHeapConfig sets it) and its rate in Hz.
void SetFoundationVideoMode(int mode);       // 0x00117d80
int GetFoundationVideoMode();                // 0x00117d90
float GetFoundationVideoModeRate();          // 0x00117da0

// Formats the message into a 1 KB buffer and hands it to the assertion hook, if one is set (0x00117db0).
void AssertMessage(const char *format, ...);

// The empty functions (Ghidra: dummyNullFunction, dummyGetNullValue)
void NullFunction();                         // 0x000d3580
void NullFunctionThunk();                    // 0x000112b0, a jump to the one above
void __stdcall NullFunctionPop4(int unused); // 0x00017550, ret 4
int GetNullValue();                          // 0x000f7330

// The STL's length_error throw, as the game's _Xlen functions build it: a std::string of the message,
// logic_error's constructor, length_error's vtable, _CxxThrowException. Nothing the game does reaches it.
void ThrowLengthError(const char *message);

// The warning beside a provisional port in the core: code no shipped data reaches, ported from the listing
// without a test. It says so once, the first time it runs.
inline void CoreUntested(const char *what) {
    printf("[core] WARNING: %s ran - a provisional port that no shipped data reaches, UNTESTED. Check what it "
           "computes against the original.\n", what);
    fflush(stdout);
}

#define CORE_UNTESTED(what) \
    do { \
        static bool warned_; \
        if (!warned_) { \
            warned_ = true; \
            CoreUntested(what); \
        } \
    } while (0)

#endif // DRIVING_ENGINE_COREFOUNDATION_H_
