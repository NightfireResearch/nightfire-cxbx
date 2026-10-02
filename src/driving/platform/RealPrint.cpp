#include "RealPrint.h"

#include "../../common/launchInfo.h"

#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// More of EA's portable system library ("REAL", see RealSystem.cpp): PRINT (its debug channels and output
// devices), the exit and abort path, the block memory helpers and two small leftovers. Each function is the
// original at the same address, ported from it.
// ---------------------------------------------------------------------------------------------------------------

// ---- PRINT
//
// Sixty-four channels, each a name and a flags word (bit 0: on; bit 1: in the group that channel 1 switches), and
// eight output devices, each a callback(channel, text), a flags word (bit 0: on) and a word nothing here uses. A
// line goes out when its channel is on, to every device that is on. The tables are the game's, initialised in the
// XBE, so they are used where they are.

struct PrintChannel { const char *name; uint32_t flags; };
struct PrintDevice { void (*callback)(int channel, const char *text); uint32_t flags; uint32_t unused; };

#define PrintChannels    ((PrintChannel *)0x001d1990u)   // 64, to 0x001d1b90
#define PrintDevices     ((PrintDevice *)0x001d1ba4u)    // 8, to 0x001d1c04
#define PrintInitialised (*(int *)0x002422d8u)
static const int kPrintChannels = 64, kPrintDevices = 8;

// AUTOINJECT
void PRINT_init() {
    if (PrintInitialised != 0)
        return;
    for (int d = 0; d < kPrintDevices; d++)
        PrintDevices[d].flags &= ~1u;
    PrintInitialised = 1;
    for (int c = 0; c < kPrintChannels; c++)
        PrintChannels[c].flags &= ~1u;
    // Devices four to seven lose their callbacks, and with them the word before each (the previous device's
    // unused word), exactly as the original's loop over 0x001d1bd0..0x001d1c04 clears them.
    for (int d = 4; d < kPrintDevices; d++) {
        PrintDevices[d - 1].unused = 0;
        PrintDevices[d].callback = NULL;
    }
    for (int c = 14; c < kPrintChannels; c++) {
        PrintChannels[c].name = NULL;
        PrintChannels[c].flags &= ~2u;
    }
    PrintChannels[2].flags |= 1;
    if (PrintDevices[2].callback != NULL)
        PrintDevices[2].flags |= 1;
}

// AUTOINJECT
void PRINT_restore() {
    if (PrintInitialised != 0)
        PrintInitialised = 0;
}

// AUTOINJECT
void PRINT_setdevicestate(int device, unsigned state) {
    if (PrintDevices[device].callback == NULL)
        return;
    if (PrintInitialised == 0)
        PRINT_init();
    PrintDevices[device].flags = (PrintDevices[device].flags & ~1u) | (state & 1);
}

// Channel 0 sets every channel, channel 1 every channel in the group, anything else just that one. (The
// original passes these to its helper at 0x0010aea0 in EAX and ESI.)
// AUTOINJECT
void PRINT_setchannelstate(int channel, int state) {
    if (PrintInitialised == 0)
        PRINT_init();
    uint32_t on = state != 0 ? 1u : 0u;
    if (channel == 0) {
        for (int c = 0; c < kPrintChannels; c++)
            PrintChannels[c].flags = (PrintChannels[c].flags & ~1u) | on;
    } else if (channel == 1) {
        for (int c = 0; c < kPrintChannels; c++) {
            if ((PrintChannels[c].flags & 2) != 0)
                PrintChannels[c].flags = (PrintChannels[c].flags & ~1u) | on;
        }
    } else {
        PrintChannels[channel].flags = (PrintChannels[channel].flags & ~1u) | on;
    }
}

// AUTOINJECT
void PRINT_setchannelname(int channel, const char *name) {
    if (PrintInitialised == 0)
        PRINT_init();
    PrintChannels[channel].name = name;
}

// The original formats into an 8 KB buffer on its stack (its helper at 0x0010b110, the channel in EDI).
static void PrintLine(int channel, const char *format, va_list arguments) {
    if (PrintInitialised == 0)
        PRINT_init();
    if ((PrintChannels[channel].flags & 1) == 0)
        return;
    char text[0x2000];
    vsnprintf(text, sizeof(text), format, arguments);
    for (int d = 0; d < kPrintDevices; d++) {
        if ((PrintDevices[d].flags & 1) != 0 && PrintDevices[d].callback != NULL)
            PrintDevices[d].callback(channel, text);
    }
}

// AUTOINJECT
void PRINT_string(int channel, const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    PrintLine(channel, format, arguments);
    va_end(arguments);
}

// The console device (0x0010b3a0, the callback in the device table at 0x001d1bb0): printf("%s", text).
// FUNC_AT(0x0010b3a0)
void PrintToConsole(int channel, const char *text) {
    (void)channel;
    fputs(text, stdout);
    fflush(stdout);
}

// ---- exit and abort

#define ExitCallbacks    ((ExitCallback *)0x00242300u)   // 64, to 0x00242400
#define AbortHandler     (*(void (**)(const char *, ...))0x001d1b94u)   // SYSTEM_abortmessage, unless replaced
#define AbortFile        (*(const char **)0x002422f8u)
#define AbortLine        (*(int *)0x002422fcu)
static const int kExitCallbacks = 64;

// The memory manager's "are we under a debugger" query, a virtual call (0x001143f0, through the object at
// 0x001d48c4); the original breaks into the debugger when it says yes.
typedef int (*DebugQueryFn)(void);
#define dummyGetNullValue ((DebugQueryFn)0x001143f0u)

// AUTOINJECT
void REAL_addexit(ExitCallback callback) {
    for (int i = 0; i < kExitCallbacks; i++) {
        if (ExitCallbacks[i] == callback)
            return;
    }
    for (int i = 0; i < kExitCallbacks; i++) {
        if (ExitCallbacks[i] == NULL) {
            ExitCallbacks[i] = callback;
            return;
        }
    }
}

// AUTOINJECT
void REAL_removeexit(ExitCallback callback) {
    for (int i = 0; i < kExitCallbacks; i++) {
        if (ExitCallbacks[i] == callback) {
            ExitCallbacks[i] = NULL;
            return;
        }
    }
}

// Runs the exit callbacks last added first, then goes back to the dashboard (XLaunchNewImageA with no image:
// src/common/launchInfo.cpp ends the process).
// AUTOINJECT
void REAL_exit() {
    for (int i = kExitCallbacks - 1; i >= 0; i--) {
        if (ExitCallbacks[i] != NULL)
            ExitCallbacks[i]();
        ExitCallbacks[i] = NULL;
    }
    if (dummyGetNullValue() != 0) {
        DebugBreak();
        return;
    }
    XLaunchNewImageA(NULL, NULL);
}

// AUTOINJECT
void SYSTEM_abortmessage(const char *format, ...) {
    char text[512];
    if (format == NULL) {
        text[0] = '\0';
    } else {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(text, sizeof(text), format, arguments);
        va_end(arguments);
    }
    PRINT_string(2, "ERROR: %s", text);
    if (AbortFile != NULL)
        PRINT_string(2, "FILE %s LINE %d\n", AbortFile, AbortLine);
    REAL_exit();
}

// AUTOINJECT
void REAL_abortmessage(const char *format, ...) {
    char text[512];
    if (format == NULL) {
        text[0] = '\0';
    } else {
        va_list arguments;
        va_start(arguments, format);
        vsnprintf(text, sizeof(text), format, arguments);
        va_end(arguments);
    }
    AbortHandler("%s", text);
}

// ---- block memory
//
// MEM_copy and MEM_fill go a byte, a short, a word, eight and sixteen bytes at a time until the destination is
// 32-byte aligned, then 32 and 8 at a time, then the tail. For a copy that is memmove's result whenever the
// original's is defined (it copies forwards, so a destination above an overlapping source smears; MEM_move is
// what callers use for that). A fill is not: it writes the 32-bit value in pieces sized by the alignment, so
// MEM_fill is ported as it is. Zero fills are memset.

// Returns the end of the destination in EAX, as the original leaves it.
// AUTOINJECT
void* MEM_copy(void *destination, const void *source, int bytes) {
    if (bytes > 0)
        memmove(destination, source, (size_t)bytes);
    return (uint8_t *)destination + (bytes > 0 ? bytes : 0);
}

static void *FillAligned(void *destination, uint32_t value, int bytes) {   // 0x0010ad20, and 0x0010b460
    uint8_t *p = (uint8_t *)destination;
    if (((uintptr_t)p & 0x1f) != 0) {
        if (((uintptr_t)p & 1) && bytes > 0)  { *p = (uint8_t)value; p += 1; bytes -= 1; }
        if (((uintptr_t)p & 2) && bytes > 1)  { *(uint16_t *)p = (uint16_t)value; p += 2; bytes -= 2; }
        if (((uintptr_t)p & 4) && bytes > 3)  { *(uint32_t *)p = value; p += 4; bytes -= 4; }
        if (((uintptr_t)p & 8) && bytes > 7)  { ((uint32_t *)p)[0] = value; ((uint32_t *)p)[1] = value; p += 8; bytes -= 8; }
        if (((uintptr_t)p & 16) && bytes > 15) {
            for (int i = 0; i < 4; i++) ((uint32_t *)p)[i] = value;
            p += 16; bytes -= 16;
        }
    }
    while (bytes - 32 >= 0) {
        for (int i = 0; i < 8; i++) ((uint32_t *)p)[i] = value;
        p += 32; bytes -= 32;
    }
    while (bytes - 8 >= 0) {
        ((uint32_t *)p)[0] = value; ((uint32_t *)p)[1] = value;
        p += 8; bytes -= 8;
    }
    if (bytes == 0)
        return p;
    if ((unsigned)bytes > 3) { *(uint32_t *)p = value; p += 4; bytes -= 4; }
    if ((unsigned)bytes > 1) { *(uint16_t *)p = (uint16_t)value; p += 2; bytes -= 2; }
    if (bytes != 0)          { *p = (uint8_t)value; p += 1; }
    return p;
}

// AUTOINJECT
void* MEM_fill(void *destination, uint32_t value, int bytes) {
    return FillAligned(destination, value, bytes);
}

// AUTOINJECT
void MEM_clear(void *destination, int bytes) {
    if (bytes > 0)
        memset(destination, 0, (size_t)bytes);
}

// AUTOINJECT
void memclr(void *destination, unsigned bytes) {
    if ((int)bytes > 0)
        memset(destination, 0, bytes);
}

// Backwards a byte at a time when the destination overlaps the source from above, MEM_copy otherwise: memmove.
// AUTOINJECT
void MEM_move(void *destination, const void *source, int bytes) {
    if (bytes > 0)
        memmove(destination, source, (size_t)bytes);
}

// ---- leftovers

// The third argument is unused; the others go where the original puts them.
// AUTOINJECT
void MOUSE_setbounds(uint32_t a, uint32_t b, uint32_t unused, uint32_t d, uint32_t e) {
    (void)unused;
    *(uint32_t *)0x002420bcu = a;
    *(uint32_t *)0x002420c0u = b;
    *(uint32_t *)0x002420dcu = d;
    *(uint32_t *)0x002420d4u = e;
}

// 0x00108780: FLD the float, FSQRT, return - the root left on the x87 stack at full precision, not rounded to a
// float, which a C body would do. Name invented.
// FUNC_AT(0x00108780)
__declspec(naked) float REAL_sqrtf(float value) {
    (void)value;
    __asm {
        fld dword ptr [esp + 4]
        fsqrt
        ret
    }
}
