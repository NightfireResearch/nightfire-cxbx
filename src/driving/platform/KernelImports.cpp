#include "KernelImports.h"

// The kernel exports through the XBE's import table (0x00189be0): each slot holds what the loader bound its ordinal
// to - a function, or for a data export the variable's address.

typedef void (__stdcall *CriticalSectionCall)(void *section);

#define ImportRtlInitializeCriticalSection (*(CriticalSectionCall *)0x00189cf0u)                   // slot 68
#define ImportRtlEnterCriticalSection (*(CriticalSectionCall *)0x00189c40u)                        // slot 24
#define ImportRtlLeaveCriticalSection (*(CriticalSectionCall *)0x00189c3cu)                        // slot 23
#define ImportKeBugCheck (*(void (__stdcall **)(uint32_t))0x00189d54u)                             // slot 93
#define ImportMmAllocateContiguousMemoryEx (*(void *(__stdcall **)(unsigned long, unsigned long, unsigned long, \
                                                                   unsigned long, unsigned long))0x00189c04u)   // slot 9
#define ImportMmFreeContiguousMemory (*(void (__stdcall **)(void *))0x00189c10u)                   // slot 12
#define ImportExQueryNonVolatileSetting (*(long (__stdcall **)(unsigned long, unsigned long *, void *, \
                                                               unsigned long, unsigned long *))0x00189becu)   // slot 3
#define ImportKeTickCount (*(volatile int **)0x00189bfcu)                                          // slot 7
#define ImportHalBootSMCVideoMode (*(unsigned long **)0x00189bf0u)                                 // slot 4

void __stdcall KernelRtlInitializeCriticalSection(void *section) {
    ImportRtlInitializeCriticalSection(section);
}

void __stdcall KernelRtlEnterCriticalSection(void *section) {
    ImportRtlEnterCriticalSection(section);
}

void __stdcall KernelRtlLeaveCriticalSection(void *section) {
    ImportRtlLeaveCriticalSection(section);
}

void __stdcall KernelKeBugCheck(uint32_t code) {
    ImportKeBugCheck(code);
}

void *__stdcall KernelMmAllocateContiguousMemoryEx(unsigned long size, unsigned long lowest, unsigned long highest,
                                                   unsigned long alignment, unsigned long protect) {
    return ImportMmAllocateContiguousMemoryEx(size, lowest, highest, alignment, protect);
}

void __stdcall KernelMmFreeContiguousMemory(void *base) {
    ImportMmFreeContiguousMemory(base);
}

long __stdcall KernelExQueryNonVolatileSetting(unsigned long index, unsigned long *type, void *value,
                                               unsigned long length, unsigned long *resultLength) {
    return ImportExQueryNonVolatileSetting(index, type, value, length, resultLength);
}

volatile int *KernelKeTickCount() {
    return ImportKeTickCount;
}

unsigned long *KernelHalBootSMCVideoMode() {
    return ImportHalBootSMCVideoMode;
}
