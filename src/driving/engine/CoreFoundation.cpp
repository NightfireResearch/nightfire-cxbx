#include "CoreFoundation.h"

#include "../data/StdStreams.h"
#include "../../helpers.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// The foundation layer's odds and ends (0x00117d80-0x00117df0, and the empty functions), ported from the listings.
// ---------------------------------------------------------------------------------------------------------------

// The C runtime's vsprintf, at its own address: its formatting is the original's
#define CRT_vsprintf ((int (*)(char *buffer, const char *format, va_list arguments))0x00133aee)

// The STL's exception machinery, the game's
#define StdString_Assign ((GameStd::String *(__fastcall *)(GameStd::String *, int, const char *, unsigned))0x00013630)
#define LogicError_Construct ((void *(__fastcall *)(void *, int, const GameStd::String *))0x00013700)
#define CxxThrowException ((void (__stdcall *)(void *, const void *))0x001325ad)
#define LengthError_vtable ((void *)0x00189eec)
#define LengthError_ThrowInfo ((const void *)0x001a89bc)

typedef void (*AssertHandler)(const char *message);

#define FoundationVideoMode I32_AT(0x00243540)
#define AssertMessageHandler (*(AssertHandler *)0x00243544)

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
    CRT_vsprintf(message, format, arguments);
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
    StdString_Assign(&text, 0, message, (unsigned)strlen(message));
    struct {
        void *vtable;
        uint32_t words[(0x28 - 4) / 4];
    } error;   // std::length_error
    LogicError_Construct(&error, 0, &text);
    error.vtable = LengthError_vtable;
    CxxThrowException(&error, LengthError_ThrowInfo);
}
