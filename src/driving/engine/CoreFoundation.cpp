#include "CoreFoundation.h"

#include "../data/StdStreams.h"
#include "../../helpers.h"
#include "../platform/CRuntime.h"
#include "../platform/CRuntimeStd.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The foundation layer's odds and ends (0x00117d80-0x00117df0, and the empty functions), ported from the listings.
// ---------------------------------------------------------------------------------------------------------------

// The STL's exception machinery, the game's
#define LengthError_vtable ((void *)0x00189eec)
#define LengthError_ThrowInfo ((const Crt::ThrowInfo *)0x001a89bc)

typedef void (*AssertHandler)(const char *message);

// XBE_GLOBAL(0x00243540, 0x4)
static int32_t FoundationVideoMode;
// XBE_GLOBAL(0x00243544, 0x4)
static AssertHandler AssertMessageHandler;

// The rate of each video mode (the original's table at 0x001a204c; mode 0 is never set)
static const float kVideoModeRates[4] = {60.0f, 60.0f, 50.0f, 60.0f};

// FUNC_AT(0x00117d80)
void SetFoundationVideoMode(int mode) {
    FoundationVideoMode = mode;
}

// FUNC_AT(0x00117d90)
int GetFoundationVideoMode() {
    return FoundationVideoMode;
}

// FUNC_AT(0x00117da0)
float GetFoundationVideoModeRate() {
    return kVideoModeRates[FoundationVideoMode];
}

// FUNC_AT(0x00117db0)
void AssertMessage(const char *format, ...) {
    char message[0x400];
    va_list arguments;
    va_start(arguments, format);
    Crt::vsprintf(message, format, arguments);
    va_end(arguments);
    if (AssertMessageHandler != NULL)
        AssertMessageHandler(message);
}

// FUNC_AT(0x000d3580)
void NullFunction() {
}

// FUNC_AT(0x000112b0)
void NullFunctionThunk() {
    NullFunction();
}

// FUNC_AT(0x00017550)
void __stdcall NullFunctionPop4(int unused) {
    (void)unused;
}

// FUNC_AT(0x000f7330)
int GetNullValue() {
    return 0;
}

void ThrowLengthError(const char *message) {
    GameStd::String text;
    text.capacity = 15;
    text.size = 0;
    text.text.buffer[0] = 0;
    text.AssignText(message, (unsigned)strlen(message));
    GameStd::LengthError error;
    error.Construct(&text);
    error.vtable = LengthError_vtable;
    Crt::CxxThrowException(&error, LengthError_ThrowInfo);
}

// FUNC_AT(0x00022770)
void __stdcall VectorConstructorIterator(void *objects, uint32_t size, int count, VectorConstructorFn construct) {
    uint8_t *object = static_cast<uint8_t *>(objects);
    while (--count >= 0) {
        construct(object);
        object += size;
    }
}
