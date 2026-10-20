#include "CRuntime.h"
#include "XboxStartup.h"
#include "../../common/xbeOriginal.h"
#include "../../driving/platform/CRuntimeInternal.h"

#include <windows.h>
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// What the shared runtime (src/driving/platform/CRuntime*.cpp) stands on in this engine, and the two helpers only
// this engine's code still calls. See CRuntime.h.
// ---------------------------------------------------------------------------------------------------------------

// The kernel's calls, through the XBE's import table (the loader fills it in)
#define KernelRtlLeaveCriticalSection      (*(void (__stdcall **)(void *section))0x0015d21cu)   // ordinal 294
#define KernelRtlEnterCriticalSection      (*(void (__stdcall **)(void *section))0x0015d220u)   // 277
#define KernelRtlInitializeCriticalSection (*(void (__stdcall **)(void *section))0x0015d228u)   // 291
#define KernelKeBugCheck                   (*(void (__stdcall **)(ULONG code))0x0015d2dcu)      // 95

namespace Crt {

// ---- the per-thread block, the process heap, the kernel's calls

PerThreadData* PerThread() {                                                // _getptd
    return (PerThreadData *)Xbox_getptd();
}

// The process heap is Win32's (XboxStartup.cpp), as XAPI's heap calls are.
void* ProcessHeapAlloc(uint32_t flags, size_t size) {
    return HeapAlloc(ProcessHeapHandle, flags, size);
}

void ProcessHeapFree(void *block) {
    HeapFree(ProcessHeapHandle, 0, block);
}

void* ProcessHeapReAlloc(void *block, size_t size) {
    return HeapReAlloc(ProcessHeapHandle, 0, block, size);
}

size_t ProcessHeapSize(void *block) {
    return HeapSize(ProcessHeapHandle, 0, block);
}

void InitialiseSection(CriticalSection *section) {
    KernelRtlInitializeCriticalSection(section);
}

void EnterSection(CriticalSection *section) {
    KernelRtlEnterCriticalSection(section);
}

void LeaveSection(CriticalSection *section) {
    KernelRtlLeaveCriticalSection(section);
}

void BugCheck(uint32_t code) {
    KernelKeBugCheck(code);
}

// OutputDebugStringA (0x000f53f5) hands the text to the kernel debugger (INT 2D), which a run under the loader does
// not have; the console is where this project's logging goes.
void DebugOutput(const char *text) {
    fputs(text, stdout);
    fflush(stdout);
}

// ---- the C locale's character classes

// _pctype points one entry into the table, so that EOF indexes the first; a negative signed char reads the words
// before it, in the original the runtime's constants and printf's state table, kept here as the block they are.
struct CtypeTableBlock {
    unsigned short before[127];
    unsigned short eof;
    unsigned short classes[256];
};
static_assert(sizeof(CtypeTableBlock) == 0x300, "the C locale's class block");

constexpr CtypeTableBlock MakeCtypeTableBlock() {
    CtypeTableBlock block = {
        {
        0xffff, 0x0000, 0x0000, 0xebcf, 0x000e, 0x2b65, 0x3030, 0x0030, 0x0000, 0x0000, 0x0000, 0x0000, 0x3ff0,
        0x027f, 0xc235, 0x2168, 0xdaa2, 0xc90f, 0x403e, 0xffff, 0xffff, 0xffff, 0x7fef, 0x0000, 0x0000, 0x0000,
        0x0010, 0x0000, 0x0000, 0x0000, 0xc098, 0x0000, 0x0000, 0x0000, 0x4098, 0x0000, 0x0000, 0x0000, 0x7ff0,
        0x0000, 0x0000, 0x0000, 0x0000, 0xffff, 0xffff, 0xffff, 0x7fef, 0x0000, 0x0000, 0x0000, 0x0010, 0x0000,
        0x0000, 0x0000, 0xc098, 0x0000, 0x0000, 0x0000, 0x4098, 0x0000, 0x0000, 0x0000, 0x7ff0, 0x0000, 0x0000,
        0x0000, 0x0000, 0x0006, 0x0600, 0x0100, 0x0000, 0x0010, 0x0603, 0x0600, 0x1002, 0x4504, 0x4545, 0x0505,
        0x0505, 0x3505, 0x0030, 0x0050, 0x0000, 0x2000, 0x3828, 0x5850, 0x0807, 0x3700, 0x3030, 0x5057, 0x0007,
        0x2000, 0x0820, 0x0000, 0x0000, 0x6008, 0x6068, 0x6060, 0x0060, 0x7000, 0x7870, 0x7878, 0x0878, 0x0807,
        0x0000, 0x0007, 0x0808, 0x0008, 0x0800, 0x0800, 0x0700, 0x0008, 0x0000, 0x6e28, 0x6c75, 0x296c, 0x0000,
        0x0028, 0x006e, 0x0075, 0x006c, 0x006c, 0x0029, 0x0000, 0x0000, 0x0000, 0x0000,
        },
        0,
        {},
    };
    for (int c = 0; c < 256; c++)
        block.classes[c] = CLocaleClass(c);
    return block;
}

// XBE_GLOBAL(0x00162762, 0x300) readonly
static const CtypeTableBlock CLocaleClasses = MakeCtypeTableBlock();
// _pctype (the game's, 0x001d5d90, points at its own copy, for the runtime's originals)
const unsigned short *CtypeTable = CLocaleClasses.classes;
// XBE_GLOBAL(0x001d5d9c, 1)
extern const char DecimalPoint = '.';                         // __decimal_point

} // namespace Crt

// ---- the helpers

// ST0 truncated towards zero, as a 64-bit integer in EDX:EAX, ST0 popped: rounded by FISTP (to nearest), then
// corrected by one where the rounding went away from zero - the sign of the difference, stored as a float, says
// which. A value that rounds to 0, or to the integer indefinite (NaN, out of range), is given as rounded.
// FUNC_AT(0x000ee3dc)
__declspec(naked) void Crt::_ftol2() {
    __asm {
        push ebp
        mov ebp, esp
        sub esp, 0x20
        and esp, 0xfffffff0
        fld st(0)
        fst dword ptr [esp + 0x18]
        fistp qword ptr [esp + 0x10]
        fild qword ptr [esp + 0x10]
        mov edx, dword ptr [esp + 0x18]      // the value's sign, as a float
        mov eax, dword ptr [esp + 0x10]
        test eax, eax
        je lowWordZero
    correct:
        fsubp st(1), st                      // the value less its rounding
        test edx, edx
        jns positive
        fstp dword ptr [esp]                 // negative: one up if the rounding went below
        mov ecx, dword ptr [esp]
        xor ecx, 0x80000000
        add ecx, 0x7fffffff
        adc eax, 0
        mov edx, dword ptr [esp + 0x14]
        adc edx, 0
        jmp done
    positive:
        fstp dword ptr [esp]                 // positive: one down if the rounding went above
        mov ecx, dword ptr [esp]
        add ecx, 0x7fffffff
        sbb eax, 0
        mov edx, dword ptr [esp + 0x14]
        sbb edx, 0
        jmp done
    lowWordZero:
        mov edx, dword ptr [esp + 0x14]
        test edx, 0x7fffffff
        jne correct
        fstp dword ptr [esp + 0x18]
        fstp dword ptr [esp + 0x18]
    done:
        leave
        ret
    }
}

// The runtime's memcpy copies overlapping blocks as memmove does: backwards when the destination starts inside the
// source.
// FUNC_AT(0x000ee4b0)
void* Crt::memcpy(void *destination, const void *source, size_t count) {
    return memmove(destination, source, count);
}

// ---------------------------------------------------------------------------------------------------------------

static void WriteJump(unsigned address, const void *target) {
    unsigned char *site = (unsigned char *)address;
    DWORD previous = 0;
    if (!VirtualProtect(site, 5, PAGE_EXECUTE_READWRITE, &previous)) {
        printf("[crt] could not make 0x%08x writable (error %lu)\n", address, GetLastError());
        return;
    }
    XbeOriginal_Record(address);   // so a shadow test can still run the original (common/xbeOriginal.h)
    site[0] = 0xE9;                                             // jmp rel32
    *(int *)(site + 1) = (int)((const unsigned char *)target - (site + 5));
}

void Inject_CRuntime(void) {
    WriteJump(0x000ee382, (const void *)&Crt::sprintf);
}
