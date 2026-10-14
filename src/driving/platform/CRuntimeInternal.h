#ifndef DRIVING_PLATFORM_CRUNTIMEINTERNAL_H_
#define DRIVING_PLATFORM_CRUNTIMEINTERNAL_H_

#include <stdint.h>

#include "CRuntime.h"

// What the C runtime's own files share and nothing else uses: the per-thread block, the character classes, and
// the floating-point conversions of CRuntimeFloat.cpp behind printf's %e/%f/%g and scanf's, atof's and strtod's
// parsing.

namespace Crt {

// ---- the per-thread block (_tiddata), 0x84 bytes, made by XboxStartup.cpp's Xbox_getptd with the game's calloc

struct PerThreadData {
    uint32_t threadId;
    uint32_t threadHandle;
    int errorNumber;       // errno
    uint32_t dosError;     // _doserrno
    uint32_t fpds;
    uint32_t randomSeed;   // rand's: 1 when the block is made
    uint8_t unknown18[0x3c];
    void *multibyteInfo;   // +0x54 the multibyte code page's, the runtime's default when the block is made
    uint8_t unknown58[0x24];
    int processingThrow;   // +0x7c _ProcessingThrow: a thrown object is on its way to a handler
    uint32_t unknown80;
};
static_assert(sizeof(PerThreadData) == 0x84, "_tiddata");

PerThreadData* PerThread();

// The exit list (CRuntime.cpp), for the shadow test.
extern AtExitFn *OnExitBegin;
extern AtExitFn *OnExitEnd;

// errno values the runtime sets.
enum {
    kNoMemory = 12,       // ENOMEM
    kDomainError = 33,    // EDOM
    kRangeError = 34,     // ERANGE
    kIllegalSequence = 42 // EILSEQ
};

// ---- the character classes

// The runtime's _pctype points one entry into its C-locale table (the entry before is EOF's), and every class test
// indexes it with whatever int it is given, so out-of-range values read what the original reads. __mb_cur_max is 1
// and nothing in the game changes the locale, so the multibyte branch each test also has is never taken.
extern const unsigned short *CtypeTable;                   // _pctype (CRuntime.cpp)
extern const char DecimalPoint;                            // __decimal_point (CRuntime.cpp)

enum CtypeClass {
    kUpper = 0x1,
    kLower = 0x2,
    kDigit = 0x4,
    kSpace = 0x8,
    kPunct = 0x10,
    kControl = 0x20,
    kBlank = 0x40,
    kHexDigit = 0x80,
    kAlpha = 0x103,       // _ALPHA with _UPPER and _LOWER
    kLeadByte = 0x8000
};

inline int Ctype(int c) {
    return CtypeTable[c];
}

int isspace(int c);                                                          // 0x001339c6

// ---- floating point (CRuntimeFloat.cpp)

// printf's %e, %f and %g of *value into buffer, precision digits; caps for 'E'.
void FormatFloat(const double *value, char *buffer, int format, int precision, int caps);   // _cfltcvt
// %g's trailing zeros removed (without the '#' flag), and the point forced in (%e/%f/%g with '#' at precision 0).
void CropZeros(char *buffer);                                                             // _cropzeros
void ForceDecimalPoint(char *buffer);                                                     // _forcdecpt
// scanf's store of a parsed float: a double where asDouble is non-zero, a float otherwise.
void AssignFloat(int asDouble, void *argument, const char *number);                       // _fassign

// _fltin2's result: what the parse found, how far it read, and the value.
struct FloatIn {
    int flags;            // kFloatOverflow, kFloatUnderflow, kFloatNoDigits
    int length;
    long unused;
    double value;
};
enum {
    kFloatOverflow = 0x80,
    kFloatUnderflow = 0x100,
    kFloatNoDigits = 0x200
};
FloatIn* ParseFloat(FloatIn *result, const char *text);                                   // _fltin2

} // namespace Crt

#endif // DRIVING_PLATFORM_CRUNTIMEINTERNAL_H_
