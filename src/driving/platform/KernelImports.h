#ifndef DRIVING_PLATFORM_KERNELIMPORTS_H_
#define DRIVING_PLATFORM_KERNELIMPORTS_H_

// The kernel exports our code uses, by name. KernelImports.cpp reaches them through the XBE's import table, which
// the loader binds to its kernel (src/loader/kernel.cpp); the standalone build implements them there over the host
// layer (docs/driving/standalone.md). The two data exports are answered by address.

#include <stdint.h>

#include "Host.h"   // HostDword, HostLong: the kernel's ULONG and LONG

void __stdcall KernelRtlInitializeCriticalSection(void *section);                                // ordinal 291
void __stdcall KernelRtlEnterCriticalSection(void *section);                                     // 277
void __stdcall KernelRtlLeaveCriticalSection(void *section);                                     // 294
void __stdcall KernelKeBugCheck(uint32_t code);                                                  // 95
void *__stdcall KernelMmAllocateContiguousMemoryEx(HostDword size, HostDword lowest, HostDword highest,
                                                   HostDword alignment, HostDword protect);   // 166
void __stdcall KernelMmFreeContiguousMemory(void *base);                                         // 171
HostLong __stdcall KernelExQueryNonVolatileSetting(HostDword index, HostDword *type, void *value,
                                                   HostDword length, HostDword *resultLength);   // 24
volatile int *KernelKeTickCount();                                                               // 156
HostDword *KernelHalBootSMCVideoMode();                                                      // 356

// What the kernel does before the game starts: the standalone build's starts its millisecond count (the loader's
// kernel has started its own)
void KernelStart(void);

#endif // DRIVING_PLATFORM_KERNELIMPORTS_H_
