// Shadow test for the C runtime: ours (engine/CRuntime.cpp, and the driving engine's src/driving/platform/
// CRuntimeCore.cpp, CRuntimeX87.cpp, CRuntimeFormat.cpp and CRuntimeFloat.cpp, which this engine shares) against
// the XBE's own, on the same inputs:
//  - sprintf: random formats over every conversion, flag, width, precision and size, with edge values (zeros of
//    both signs, denormals, the largest doubles, halfway ties, NaNs and infinities, null and long strings, wide
//    characters past 255, precisions past the 163 that take a heap buffer). The return value and the whole buffer.
//  - __ftol2: doubles and 80-bit values in ST0 - halves, values past 2^32 and 2^63, NaNs. EDX:EAX and the x87
//    status (the stack popped).
//  - memcpy: random blocks, overlapping both ways. The block and the return value.
//  - toupper, tolower: -128 to 255. The whole return register.
//  - _CIacos, _CIasin: edge values, -1.25 to 1.25, NaNs. The 80-bit result.
//  - _fpmath against the original: control words of every precision and rounding, with exceptions raised. The control
//    and status words after.
//  - malloc, calloc, free: an allocation script of awkward sizes and overflowing products, each runtime on a fresh
//    heap of its own. Each block by its size and (calloc) its zeroing; where Win32 puts it differs between heaps.
//  - _mtinitlocks, _mtdeletelocks: which slots point at which static section, and which are freed.
// The process heap does not exist yet when this runs: MenuProbe.cpp lends one.
// One summary line: "[crtshadow] C runtime: N cases, M checks, D differ".

#include "CRuntimeShadow.h"
#include "ShadowHarness.h"

#include "../../common/xbeOriginal.h"
#include "../../driving/platform/CRuntimeInternal.h"
#include "../engine/CRuntime.h"
#include "../engine/XboxStartup.h"

#include <windows.h>
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <string>

namespace {

const char kTag[] = "[crtshadow]";

int g_cases, g_checks, g_differ;
std::map<std::string, int> g_differBy;   // differences by function, for the summary

void Check(bool same, const char *what, int trial, const char *detail) {
    g_checks++;
    if (same)
        return;
    g_differBy[what]++;
    if (g_differ++ < 16)
        printf("%s %s, case %d: %s\n", kTag, what, trial, detail);
}

uint32_t Word(void) {
    return ShadowRand(0x10000) | (ShadowRand(0x10000) << 16);
}

double FromBits(uint64_t bits) {
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

const double kEdgeDoubles[] = {
    0.0, -0.0, 1.0, -1.0, 0.5, 1.5, 2.5, -2.5, 0.125, 0.1, 1e-5, 123456789.125, 9.9999999999, 0.99999999999,
    1e15, 1e16, 1e17, 1e21, 1e300, 1e-300, DBL_MAX, -DBL_MAX, DBL_MIN, 4.9406564584124654e-324, 2.2250738585072009e-308,
    4294967295.5, 4294967296.0, 4294967295.75, -4294967296.25, 2147483647.5, -2147483648.5, 9.2233720368547758e18,
    -9.2233720368547758e18, 1.8446744073709552e19, 0.05, 0.15, 0.25, 0.35, 1.005, 2.675,
};

double EdgeDouble(void) {
    switch (ShadowRand(6)) {
    case 0:
        return FromBits(0x7ff0000000000000ull | (ShadowRand(4) == 0 ? 0 : Word()));   // infinity or a NaN
    case 1:
        return FromBits(0xfff8000000000000ull);                                   // the x87 indefinite
    case 2:
        return FromBits(((uint64_t)Word() << 32) | Word());                         // any bits
    case 3:
        return (double)(int32_t)Word() / (double)(1 + ShadowRand(10000));
    default:
        return kEdgeDoubles[ShadowRand(sizeof(kEdgeDoubles) / sizeof(kEdgeDoubles[0]))];
    }
}

// ---- sprintf

typedef int (__cdecl *SprintfFn)(char *, const char *, ...);
const unsigned kSprintf = 0x000ee382;

// The variable arguments as the words cdecl pushes, whatever their types.
struct Arguments {
    uint32_t words[24];
    int count;
    void Add(uint32_t w) {
        if (count < 24)
            words[count++] = w;
    }
    void AddDouble(double d) {
        uint32_t w[2];
        memcpy(w, &d, 8);
        Add(w[0]);
        Add(w[1]);
    }
};

int CallSprintf(SprintfFn fn, char *buffer, const char *format, const Arguments &a) {
    const uint32_t *w = a.words;
    return fn(buffer, format, w[0], w[1], w[2], w[3], w[4], w[5], w[6], w[7], w[8], w[9], w[10], w[11], w[12],
              w[13], w[14], w[15], w[16], w[17], w[18], w[19], w[20], w[21], w[22], w[23]);
}

char g_longString[700];
const wchar_t kWideSmiley[] = { 'h', 'i', 0x263a, 0 };
const char *const kStrings[] = { "", "a", "hello world", "%d", "Bond, James Bond", "\xe9t\xe9", g_longString, NULL };
const wchar_t *const kWideStrings[] = { L"wide", L"", kWideSmiley, NULL };

void AddConversion(char *format, Arguments *a) {
    char *p = format + strlen(format);
    *p++ = '%';
    if (ShadowRand(25) == 0) {
        *p++ = '%';
        *p = '\0';
        return;
    }
    const char *flags = "-+ #0";
    for (int i = 0; i < 5; i++) {
        if (ShadowRand(5) == 0)
            *p++ = flags[i];
    }
    switch (ShadowRand(5)) {
    case 0:
        p += sprintf(p, "%u", 1 + ShadowRand(30));
        break;
    case 1:
        *p++ = '*';
        a->Add((uint32_t)(ShadowRand(81) - 40));
        break;
    default:
        break;
    }
    switch (ShadowRand(7)) {
    case 0:
        *p++ = '.';
        break;
    case 1:
    case 2:
        p += sprintf(p, ".%u", ShadowRand(21));
        break;
    case 3:
        p += sprintf(p, ".%u", ShadowRand(8) == 0 ? 150 + ShadowRand(150) : ShadowRand(40));
        break;
    case 4:
        *p++ = '.';
        *p++ = '*';
        a->Add((uint32_t)(ShadowRand(61) - 20));
        break;
    default:
        break;
    }
    static const char kIntegers[] = "diuoxX";
    static const char kFloats[] = "eEfgG";
    switch (ShadowRand(12)) {
    case 0: case 1: case 2: case 3: {                 // integers
        static const char *const kSizes[] = { "", "", "h", "l", "I64", "L", "w" };
        const char *size = kSizes[ShadowRand(7)];
        p += sprintf(p, "%s%c", size, kIntegers[ShadowRand(6)]);
        uint32_t w = ShadowRand(3) == 0 ? (uint32_t)(ShadowRand(201) - 100) : Word();
        if (ShadowRand(10) == 0)
            w = ShadowRand(2) ? 0x80000000u : 0;
        a->Add(w);
        if (strcmp(size, "I64") == 0)
            a->Add(ShadowRand(2) ? Word() : (w & 0x80000000u ? 0xffffffffu : 0));
        break;
    }
    case 4: case 5: case 6: case 7: {                 // floats
        static const char *const kSizes[] = { "", "", "", "l", "L" };
        p += sprintf(p, "%s%c", kSizes[ShadowRand(5)], kFloats[ShadowRand(5)]);
        a->AddDouble(EdgeDouble());
        break;
    }
    case 8:                                            // characters
        if (ShadowRand(3) == 0) {
            p += sprintf(p, "%s", ShadowRand(2) ? "C" : "lc");
            a->Add(ShadowRand(2) ? 'W' : (uint32_t)(0x100 + ShadowRand(0x200)) | (Word() & 0xffff0000u));
        } else {
            p += sprintf(p, "%s", ShadowRand(2) ? "c" : "hc");
            a->Add(Word());
        }
        break;
    case 9:                                            // strings
        if (ShadowRand(3) == 0) {
            p += sprintf(p, "%s", ShadowRand(2) ? "S" : "ls");
            a->Add((uint32_t)(uintptr_t)kWideStrings[ShadowRand(4)]);
        } else {
            p += sprintf(p, "%s", ShadowRand(2) ? "s" : "hs");
            a->Add((uint32_t)(uintptr_t)kStrings[ShadowRand(8)]);
        }
        break;
    case 10:
        *p++ = 'p';
        a->Add(Word());
        break;
    default:
        *p++ = "dfsg"[ShadowRand(4)];
        if (p[-1] == 'd')
            a->Add(Word());
        else if (p[-1] == 's')
            a->Add((uint32_t)(uintptr_t)kStrings[ShadowRand(7)]);
        else
            a->AddDouble(EdgeDouble());
        break;
    }
    *p = '\0';
}

char g_theirs[4096], g_ours[4096];

void SprintfCase(int trial, const char *format, const Arguments &a) {
    g_cases++;
    memset(g_theirs, 0xab, sizeof(g_theirs));
    memset(g_ours, 0xab, sizeof(g_ours));
    int theirs, ours;
    {
        XbeOriginalScope original(kSprintf);
        theirs = CallSprintf((SprintfFn)kSprintf, g_theirs, format, a);
    }
    ours = CallSprintf(&Crt::sprintf, g_ours, format, a);
    char detail[300];
    _snprintf(detail, sizeof(detail), "\"%s\": returns %d, the original %d; \"%.60s\" / \"%.60s\"", format, ours,
              theirs, g_ours, g_theirs);
    Check(ours == theirs && memcmp(g_ours, g_theirs, sizeof(g_ours)) == 0, "sprintf", trial, detail);
}

void SprintfCases(void) {
    for (size_t i = 0; i < sizeof(g_longString) - 1; i++)
        g_longString[i] = (char)('a' + i % 26);
    static const char *const kFixed[] = { "%d", "%5.2f", "%s", "%08X", "%-10s|", "%e", "%g", "%.0f", "%#.0e",
                                          "%#x", "%+d", "% d", "%.3s", "%c", "%p", "%%", "%i%%", "%.10g", "%G" };
    int trial = 0;
    for (size_t i = 0; i < sizeof(kFixed) / sizeof(kFixed[0]); i++) {
        for (int k = 0; k < 40; k++) {
            Arguments a = {};
            a.AddDouble(EdgeDouble());
            a.Add((uint32_t)(uintptr_t)kStrings[ShadowRand(7)]);
            if (strchr(kFixed[i], 's') != NULL) {
                a.count = 0;
                a.Add((uint32_t)(uintptr_t)kStrings[ShadowRand(8)]);
            } else if (strpbrk(kFixed[i], "efgG") == NULL) {
                a.count = 0;
                a.Add(Word());
            }
            SprintfCase(trial++, kFixed[i], a);
        }
    }
    for (int k = 0; k < 6000; k++) {
        char format[256] = "";
        Arguments a = {};
        int conversions = 1 + ShadowRand(3);
        for (int c = 0; c < conversions; c++) {
            if (ShadowRand(3) == 0)
                strcat(format, ShadowRand(2) ? "x=" : " ");
            AddConversion(format, &a);
        }
        SprintfCase(trial++, format, a);
    }
}

// ---- __ftol2

const unsigned kFtol2 = 0x000ee3dc;

// ST0 loaded from an 80-bit value, the function called; its EDX:EAX, and the status word after.
__declspec(naked) uint64_t CallFtol2(unsigned function, const uint8_t *value, uint16_t *status) {
    __asm {
        fnclex
        mov eax, dword ptr [esp + 8]
        fld tbyte ptr [eax]
        mov eax, dword ptr [esp + 4]
        call eax
        mov ecx, dword ptr [esp + 12]
        fnstsw word ptr [ecx]
        ret
    }
}

void Extended80(double d, uint8_t *out) {
    __asm {
        mov eax, out
        fld d
        fstp tbyte ptr [eax]
    }
}

void Ftol2Cases(void) {
    for (int trial = 0; trial < 4000; trial++) {
        uint8_t value[10];
        if (ShadowRand(3) == 0) {
            uint64_t mantissa = ((uint64_t)Word() << 32 | Word()) | 0x8000000000000000ull;
            uint16_t exponent = (uint16_t)(0x3fff - 4 + ShadowRand(70)) | (ShadowRand(2) ? 0x8000 : 0);
            if (ShadowRand(20) == 0)
                exponent |= 0x7fff;
            memcpy(value, &mantissa, 8);
            memcpy(value + 8, &exponent, 2);
        } else {
            double d = ShadowRand(2) ? EdgeDouble() : (double)(int32_t)Word() * (1.0 / (1 << ShadowRand(31)));
            if (ShadowRand(4) == 0)
                d = (double)(int64_t)(((uint64_t)Word() << 32) | Word()) + (ShadowRand(2) ? 0.5 : -0.25);
            Extended80(d, value);
        }
        g_cases++;
        uint16_t theirStatus, ourStatus;
        uint64_t theirs;
        {
            XbeOriginalScope original(kFtol2);
            theirs = CallFtol2(kFtol2, value, &theirStatus);
        }
        uint64_t ours = CallFtol2((unsigned)(uintptr_t)&Crt::_ftol2, value, &ourStatus);
        char detail[160];
        _snprintf(detail, sizeof(detail), "%02x%02x:%02x%02x%02x%02x%02x%02x%02x%02x gives %016llx (status %04x), "
                  "the original %016llx (%04x)", value[9], value[8], value[7], value[6], value[5], value[4],
                  value[3], value[2], value[1], value[0], ours, ourStatus, theirs, theirStatus);
        Check(ours == theirs && ourStatus == theirStatus, "__ftol2", trial, detail);
    }
}

// ---- memcpy

typedef void *(__cdecl *MemcpyFn)(void *, const void *, size_t);
const unsigned kMemcpy = 0x000ee4b0;

void MemcpyCases(void) {
    uint8_t start[512], theirs[512], ours[512];
    for (int trial = 0; trial < 3000; trial++) {
        for (size_t i = 0; i < sizeof(start); i++)
            start[i] = (uint8_t)ShadowRand(256);
        size_t to = ShadowRand(256), from = ShadowRand(256), count = ShadowRand(4) == 0 ? ShadowRand(9) : ShadowRand(257);
        if (ShadowRand(3) == 0) {                     // overlapping, either way
            int nearby = (int)to + (int)ShadowRand(17) - 8;
            from = nearby >= 0 ? nearby : 0;
        }
        memcpy(theirs, start, sizeof(start));
        memcpy(ours, start, sizeof(start));
        g_cases++;
        void *theirResult;
        {
            XbeOriginalScope original(kMemcpy);
            theirResult = ((MemcpyFn)kMemcpy)(theirs + to, theirs + from, count);
        }
        void *ourResult = Crt::memcpy(ours + to, ours + from, count);
        char detail[100];
        _snprintf(detail, sizeof(detail), "%u bytes from %u to %u", (unsigned)count, (unsigned)from, (unsigned)to);
        Check(memcmp(theirs, ours, sizeof(ours)) == 0 &&
              (uint8_t *)theirResult - theirs == (uint8_t *)ourResult - ours, "memcpy", trial, detail);
    }
}

// ---- toupper, tolower

typedef int (__cdecl *CharFn)(int);

void CaseCases(void) {
    struct {
        const char *name;
        unsigned original;
        CharFn ours;
    } const kFunctions[] = { { "toupper", 0x000ee96b, &Crt::toupper }, { "tolower", 0x000eed6b, &Crt::tolower } };
    for (const auto &f : kFunctions) {
        for (int c = -128; c < 256; c++) {
            g_cases++;
            int theirs = ((CharFn)f.original)(c), ours = f.ours(c);
            char detail[80];
            _snprintf(detail, sizeof(detail), "%d gives %08x, the original %08x", c, ours, theirs);
            Check(ours == theirs, f.name, c, detail);
        }
    }
}

// ---- _CIacos, _CIasin

// The function on ST0 = x; the 80-bit result.
__declspec(naked) void CallUnaryX87(unsigned function, double x, uint8_t *out) {
    __asm {
        fld qword ptr [esp + 8]
        mov eax, dword ptr [esp + 4]
        call eax
        mov eax, dword ptr [esp + 16]
        fstp tbyte ptr [eax]
        ret
    }
}

void ArcCases(void) {
    struct {
        const char *name;
        unsigned original;
        void (*ours)();
    } const kFunctions[] = { { "_CIacos", 0x000ee294, &Crt::CIacos }, { "_CIasin", 0x000ee8a0, &Crt::CIasin } };
    for (const auto &f : kFunctions) {
        for (int trial = 0; trial < 3000; trial++) {
            double x;
            switch (ShadowRand(4)) {
            case 0:
                x = EdgeDouble();
                break;
            case 1: {
                static const double kEdges[] = { 1.0, -1.0, 0.0, -0.0, 0.5, -0.5, 1.0000000000000002,
                                                 -1.0000000000000002, 0.9999999999999999, 1e-300, -1e-300 };
                x = kEdges[ShadowRand(sizeof(kEdges) / sizeof(kEdges[0]))];
                break;
            }
            default:
                x = ShadowRandFloat(-1.25f, 1.25f) + (double)ShadowRand(1000) * 1e-12;
                break;
            }
            g_cases++;
            uint8_t theirs[10], ours[10];
            CallUnaryX87(f.original, x, theirs);
            CallUnaryX87((unsigned)(uintptr_t)f.ours, x, ours);
            char detail[100];
            _snprintf(detail, sizeof(detail), "%.17g", x);
            Check(memcmp(theirs, ours, 10) == 0, f.name, trial, detail);
        }
    }
}

// ---- _fpmath against the original

const unsigned kSetupLibs = 0x000ee285;

// The control word set and an exception raised, the function called; the control and status words after.
__declspec(naked) void CallWithControl(unsigned function, uint16_t control, uint16_t *after) {
    __asm {
        fnclex
        fldcw word ptr [esp + 8]
        fldz
        fld1
        fdivrp st(1), st                 // 1 / 0: divide-by-zero, masked
        fstp st(0)
        mov eax, dword ptr [esp + 4]
        call eax
        mov eax, dword ptr [esp + 12]
        fnstcw word ptr [eax]
        fnstsw word ptr [eax + 2]
        ret
    }
}

void ControlCases(void) {
    uint16_t saved;
    __asm fnstcw saved
    for (int trial = 0; trial < 64; trial++) {
        // every precision (bits 8-9) and rounding (10-11), the infinity bit, and each exception masked or not
        uint16_t control = (uint16_t)(0x0040 | (trial & 0xf) << 8 | (trial & 0x10 ? 0x1000 : 0) |
                                      (trial & 0x20 ? 0x3f : ShadowRand(0x40) | 0x04));
        g_cases++;
        uint16_t theirs[2], ours[2];
        CallWithControl(kSetupLibs, control, theirs);
        __asm fnclex
        __asm fldcw saved
        CallWithControl((unsigned)(uintptr_t)&Crt::_fpmath, control, ours);
        __asm fnclex
        __asm fldcw saved
        char detail[100];
        _snprintf(detail, sizeof(detail), "control %04x: %04x %04x, the original %04x %04x", control, ours[0], ours[1],
                  theirs[0], theirs[1]);
        Check(memcmp(theirs, ours, sizeof(ours)) == 0, "_fpmath", trial, detail);
    }
}

// ---- malloc, calloc, free

typedef void *(__cdecl *MallocFn)(size_t);
typedef void *(__cdecl *CallocFn)(size_t, size_t);
typedef void (__cdecl *FreeFn)(void *);
const unsigned kMalloc = 0x000f34d4, kCalloc = 0x000f3a4f, kFree = 0x000f0b2f;

struct Heap {
    HANDLE heap;
    void *blocks[64];
    void *Malloc(size_t size, bool original) {
        return original ? ((MallocFn)kMalloc)(size) : Crt::malloc(size);
    }
    void *Calloc(size_t count, size_t size, bool original) {
        return original ? ((CallocFn)kCalloc)(count, size) : Crt::calloc(count, size);
    }
    void Free(void *block, bool original) {
        if (original)
            ((FreeFn)kFree)(block);
        else
            Crt::free(block);
    }
};

// One step of the script on a heap; what it gave, as text.
void HeapStep(Heap *h, bool original, uint32_t op, uint32_t a, uint32_t b, uint32_t slot, char *out, size_t room) {
    void *block = NULL;
    if (op == 2) {
        h->Free(h->blocks[slot], original);
        h->blocks[slot] = NULL;
        _snprintf(out, room, "free");
        return;
    }
    if (h->blocks[slot] != NULL) {
        h->Free(h->blocks[slot], original);
        h->blocks[slot] = NULL;
    }
    block = op == 0 ? h->Malloc(a, original) : h->Calloc(a, b, original);
    h->blocks[slot] = block;
    if (block == NULL) {
        _snprintf(out, room, "null");
        return;
    }
    size_t size = HeapSize(h->heap, 0, block);
    bool zeroed = true;
    if (op == 1) {
        for (size_t i = 0; i < size && i < 0x10000; i++)
            zeroed = zeroed && ((uint8_t *)block)[i] == 0;
    }
    memset(block, 0x5a, size < 0x10000 ? size : 0x10000);   // so a later calloc of the same memory must zero it
    _snprintf(out, room, "size %x%s", (unsigned)size, zeroed ? "" : " not zeroed");
}

void HeapCases(void) {
    static const uint32_t kSizes[] = { 0, 1, 2, 15, 16, 17, 31, 32, 33, 100, 0x84, 0x1c, 4095, 4096, 0x10000,
                                       0x12345, 0xffffffe0u, 0xffffffe1u, 0xffffffffu };
    const int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);
    HANDLE processHeap = ProcessHeapHandle;
    Heap theirs = {}, ours = {};
    theirs.heap = HeapCreate(0, 0x1000, 0);
    ours.heap = HeapCreate(0, 0x1000, 0);
    for (int trial = 0; trial < 1500; trial++) {
        uint32_t op = ShadowRand(3), slot = ShadowRand(64);
        uint32_t a = ShadowRand(2) ? kSizes[ShadowRand(kSizeCount)] : ShadowRand(0x400);
        uint32_t b = ShadowRand(2) ? kSizes[ShadowRand(kSizeCount)] : ShadowRand(0x40);
        if (op == 1 && ShadowRand(6) == 0) {         // a product that overflows
            a = 0x10000 + ShadowRand(4);
            b = 0x10001;
        }
        char theirText[80], ourText[80];
        ProcessHeapHandle = theirs.heap;
        HeapStep(&theirs, true, op, a, b, slot, theirText, sizeof(theirText));
        ProcessHeapHandle = ours.heap;
        HeapStep(&ours, false, op, a, b, slot, ourText, sizeof(ourText));
        g_cases++;
        char detail[220];
        _snprintf(detail, sizeof(detail), "%s(%x, %x): %s, the original %s", op == 0 ? "malloc" : op == 1 ? "calloc" : "free",
                  a, b, ourText, theirText);
        Check(strcmp(theirText, ourText) == 0, "heap", trial, detail);
    }
    HeapDestroy(theirs.heap);
    HeapDestroy(ours.heap);
    ProcessHeapHandle = processHeap;
}

// ---- _mtinitlocks, _mtdeletelocks

typedef int (__cdecl *IntFn)(void);
typedef void (__cdecl *VoidFn)(void);
const unsigned kMtInitLocks = 0x000f35b5, kMtDeleteLocks = 0x000f35fe;
#define GameLockTable ((Crt::LockSlot *)0x001d6380u)
#define GameStaticLocks ((Crt::CriticalSection *)0x00300640u)

// Each slot as text: "-" empty, a number for a static section, "h" for one off the heap.
void DescribeLocks(const Crt::LockSlot *table, const Crt::CriticalSection *statics, char *out) {
    for (int i = 0; i < Crt::kLockCount; i++) {
        const Crt::CriticalSection *s = table[i].section;
        if (s == NULL)
            out += sprintf(out, "- ");
        else if (s >= statics && s < statics + Crt::kPreallocatedLocks)
            out += sprintf(out, "%d ", (int)(s - statics));
        else
            out += sprintf(out, "h ");
    }
}

void LockCases(void) {
    Crt::LockSlot gameTable[Crt::kLockCount], ourTable[Crt::kLockCount];
    ::memcpy(gameTable, GameLockTable, sizeof(gameTable));
    ::memcpy(ourTable, Crt::LockTable, sizeof(ourTable));

    g_cases++;
    int theirs = ((IntFn)kMtInitLocks)();
    int ours = Crt::_mtinitlocks();
    char theirText[200], ourText[200], detail[480];
    DescribeLocks(GameLockTable, GameStaticLocks, theirText);
    DescribeLocks(Crt::LockTable, Crt::StaticLocks, ourText);
    _snprintf(detail, sizeof(detail), "returns %d: %s; the original %d: %s", ours, ourText, theirs, theirText);
    Check(ours == theirs && strcmp(ourText, theirText) == 0, "_mtinitlocks", 0, detail);

    // Locks made on first use (the runtime's own _malloc and ours), then freed
    for (int i = 0; i < Crt::kLockCount; i++) {
        if (ShadowRand(2) && GameLockTable[i].section == NULL) {
            GameLockTable[i].section = (Crt::CriticalSection *)((MallocFn)kMalloc)(sizeof(Crt::CriticalSection));
            Crt::LockTable[i].section = (Crt::CriticalSection *)Crt::malloc(sizeof(Crt::CriticalSection));
        }
    }
    g_cases++;
    ((VoidFn)kMtDeleteLocks)();
    Crt::_mtdeletelocks();
    DescribeLocks(GameLockTable, GameStaticLocks, theirText);
    DescribeLocks(Crt::LockTable, Crt::StaticLocks, ourText);
    _snprintf(detail, sizeof(detail), "%s; the original %s", ourText, theirText);
    Check(strcmp(ourText, theirText) == 0, "_mtdeletelocks", 0, detail);

    ::memcpy(GameLockTable, gameTable, sizeof(gameTable));
    ::memcpy(Crt::LockTable, ourTable, sizeof(ourTable));
}

} // namespace

void CRuntimeShadow_Run(void) {
    g_cases = g_checks = g_differ = 0;
    // Its own random numbers, so the suites after it draw what they drew before it was added
    uint32_t seed = ShadowRand_Seed();
    ShadowRand_SetSeed(0x43525431);
    // The original printf's float conversions are reached through pointers its startup sets (_cfltcvt_init)
    ((VoidFn)0x000ee24d)();

    SprintfCases();
    Ftol2Cases();
    MemcpyCases();
    CaseCases();
    ArcCases();
    ControlCases();
    HeapCases();
    LockCases();
    for (const auto &by : g_differBy)
        printf("%s %s: %d differ\n", kTag, by.first.c_str(), by.second);
    printf("%s C runtime: %d cases, %d checks, %d differ\n", kTag, g_cases, g_checks, g_differ);
    ShadowRand_SetSeed(seed);
    fflush(stdout);
}
