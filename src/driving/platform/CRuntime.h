#ifndef DRIVING_PLATFORM_CRUNTIME_H_
#define DRIVING_PLATFORM_CRUNTIME_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include "../../common/xbeClass.h"   // LAYOUT_32

// ---------------------------------------------------------------------------------------------------------------
// The game's C runtime: the multithreaded CRT the XDK linked into Driving.xbe in 2002, as far as our code and
// the C++ library (CRuntimeStd*, the streams' Dinkumware pieces) use it. Our DLL links a modern runtime of its
// own, which prints and parses floats differently, sorts differently and allocates elsewhere, so these are the
// game's algorithms, not ours under its names. In a namespace of their own because the names are the same.
//
// Files: CRuntimeCore.cpp (the heap, the locks and the fatal stop, errno, character tests), CRuntime.cpp (what
// those stand on in this engine, the exit list, rand, localeconv, qsort), CRuntimeX87.cpp (the x87 helpers),
// CRuntimeFormat.cpp (the printf family), CRuntimeParse.cpp (the scanf family, atof, atol, strtod, strtol),
// CRuntimeFloat.cpp (the 12-byte long double arithmetic both conversions go through, see CRuntimeInternal.h).
//
// The action engine's runtime is the same library: it compiles CRuntimeCore.cpp, CRuntimeX87.cpp,
// CRuntimeFormat.cpp and CRuntimeFloat.cpp too, over its own heap, kernel calls and class table
// (src/action/engine/CRuntime.cpp), which patches its entry points to them.
// ---------------------------------------------------------------------------------------------------------------

namespace Crt {

typedef void (*AtExitFn)(void);
typedef int (*CompareFn)(const void *, const void *);

// The C locale's numeric and monetary conventions, as localeconv hands them out (the 2002 layout).
struct Lconv {
    char *decimalPoint;
    char *thousandsSep;
    char *grouping;
    char *intCurrencySymbol;
    char *currencySymbol;
    char *monDecimalPoint;
    char *monThousandsSep;
    char *monGrouping;
    char *positiveSign;
    char *negativeSign;
    char intFracDigits;
    char fracDigits;
    char pCsPrecedes;
    char pSepBySpace;
    char nCsPrecedes;
    char nSepBySpace;
    char pSignPosn;
    char nSignPosn;
};
LAYOUT_32(sizeof(Lconv) == 0x30, "lconv");

// ---- the heap, on the process heap XAPI made (CRuntimeCore.cpp)

void* malloc(size_t size);                                                   // 0x001340e3
void free(void *block);                                                      // 0x001331dc
void* calloc(size_t count, size_t size);                                     // 0x0013439d
void* realloc(void *block, size_t size);                                     // 0x00134815
size_t _msize(void *block);                                                  // 0x001369cc

// ---- the runtime's initialisers, the exit list (CRuntime.cpp), its locks and its fatal stop (CRuntimeCore.cpp)

int _onexitinit();                                                           // 0x00132a1b
void _fpmath();                                                              // 0x00131d28 (CRuntimeX87.cpp)
int atexit(AtExitFn function);                                               // 0x00132a7b
int _mtinitlocks();                                                          // 0x001363d8
void _mtdeletelocks();                                                       // 0x00136421
void _lock(int lock);                                                        // 0x001364d8
void _unlock(int lock);                                                      // 0x0013644a
void _amsg_exit(int code);                                                   // 0x0013566d
void _exit(int code);                                                        // 0x0013438b
void _purecall();                                                            // 0x00131d35 (Ghidra: __pure_virtual)

// ---- per-thread state, in the block XboxStartup.cpp's Xbox_getptd hands out (rand: CRuntime.cpp; _errno:
//      CRuntimeCore.cpp)

int rand();                                                                  // 0x00133ee0
int* _errno();                                                               // 0x00133664

// ---- characters (CRuntimeCore.cpp; localeconv: CRuntime.cpp)

int isdigit(int c);                                                          // 0x0013399d
int isalnum(int c);                                                          // 0x001339ef
int tolower(int c);                                                          // 0x001327f9
int toupper(int c);                                                          // the action engine's (0x000ee96b)
Lconv* localeconv();                                                         // 0x001333d6

// ---- sorting (CRuntime.cpp)

void qsort(void *base, size_t count, size_t width, CompareFn compare);       // 0x00132db0

// ---- the x87 (CRuntimeX87.cpp). The _CI functions take their operands and answer on the FPU stack: acos and asin
//      the value in ST0, pow the base in ST1 and the exponent in ST0. Without NIGHTFIRE_X87 they are C functions
//      taking and answering doubles. _clearfp answers the x87 status, abstracted.

#if NIGHTFIRE_X87
void CIacos();                                                               // 0x001328d0
void CIasin();                                                               // 0x00133f94
void CIpow();                                                                // 0x00132a94
#else
double CIacos(double x);
double CIasin(double x);
double CIpow(double x, double y);
#endif
unsigned _clearfp();                                                         // 0x00133c9e

// ---- formatting (CRuntimeFormat.cpp)

int sprintf(char *buffer, const char *format, ...);                          // 0x00132767
int vsprintf(char *buffer, const char *format, va_list arguments);           // 0x00133aee
int _vsnprintf(char *buffer, size_t count, const char *format, va_list arguments);   // 0x001340f5

// ---- parsing (CRuntimeParse.cpp). errno is the game's, through _errno.

int sscanf(const char *text, const char *format, ...);                       // 0x00133234
double atof(const char *text);                                               // 0x00133e84
long atol(const char *text);                                                 // 0x00133d51
double strtod(const char *text, char **end);                                 // 0x00135196
long strtol(const char *text, char **end, int base);                         // 0x00133636
unsigned long strtoul(const char *text, char **end, int base);               // 0x0013364d
int64_t _strtoi64(const char *text, char **end, int base);                   // 0x0013396f
uint64_t _strtoui64(const char *text, char **end, int base);                 // 0x00133986

} // namespace Crt

#endif // DRIVING_PLATFORM_CRUNTIME_H_
