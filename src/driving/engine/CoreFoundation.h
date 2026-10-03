#ifndef DRIVING_ENGINE_COREFOUNDATION_H_
#define DRIVING_ENGINE_COREFOUNDATION_H_

// ---------------------------------------------------------------------------------------------------------------
// Odds and ends of the engine's foundation layer: the video mode the game chose at startup and its refresh rate,
// the assertion message hook, and the empty functions the compiler left several copies of. See CoreFoundation.cpp.
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

#endif // DRIVING_ENGINE_COREFOUNDATION_H_
