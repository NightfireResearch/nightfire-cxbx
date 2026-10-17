#ifndef DRIVING_PLATFORM_REALPRINT_H_
#define DRIVING_PLATFORM_REALPRINT_H_

// EA's portable system library, continued: PRINT, the exit and abort path, block memory, two leftovers. See
// RealPrint.cpp.

#include <stdint.h>

typedef void (*ExitCallback)(void);

void PRINT_init();
void PRINT_restore();
void PRINT_setdevicestate(int device, unsigned state);
void PRINT_setchannelstate(int channel, int state);
void PRINT_setchannelname(int channel, const char *name);
void PRINT_string(int channel, const char *format, ...);
void PrintToConsole(int channel, const char *text);
void PrintToDebugger(int channel, const char *text);
void PRINTFILE_print(int channel, const char *text);

void REAL_addexit(ExitCallback callback);
void REAL_removeexit(ExitCallback callback);
void REAL_exit();
void SYSTEM_abortmessage(const char *format, ...);
void REAL_abortmessage(const char *format, ...);

void *MEM_copy(void *destination, const void *source, int bytes);
void *MEM_fill(void *destination, uint32_t value, int bytes);
void MEM_clear(void *destination, int bytes);
void memclr(void *destination, unsigned bytes);
void MEM_move(void *destination, const void *source, int bytes);

// The mouse driver's bounds, which nothing else reads (name ours)
struct MouseBounds {
    uint32_t unknown00;
    uint32_t unknown04;
    uint32_t unknown08[3];
    uint32_t unknown14;
    uint32_t unknown18;
    uint32_t unknown1c;
    uint32_t unknown20;
};
static_assert(sizeof(MouseBounds) == 0x24, "the mouse bounds are 9 words");
extern MouseBounds gMouseBounds;

void MOUSE_setbounds(uint32_t a, uint32_t b, uint32_t unused, uint32_t d, uint32_t e);
float REAL_sqrtf(float value);

#endif // DRIVING_PLATFORM_REALPRINT_H_
