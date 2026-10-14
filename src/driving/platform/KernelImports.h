#ifndef DRIVING_PLATFORM_KERNELIMPORTS_H_
#define DRIVING_PLATFORM_KERNELIMPORTS_H_

// The kernel exports our code uses, by name. KernelImports.cpp reaches them through the XBE's import table, which
// the loader binds to its kernel (src/loader/kernel.cpp); the standalone build implements them there over the host
// layer (docs/driving/standalone.md). The two data exports are answered by address.

#include <stdint.h>

void __stdcall KernelRtlInitializeCriticalSection(void *section);                                // ordinal 291
void __stdcall KernelRtlEnterCriticalSection(void *section);                                     // 277
void __stdcall KernelRtlLeaveCriticalSection(void *section);                                     // 294
void __stdcall KernelKeBugCheck(uint32_t code);                                                  // 95
void *__stdcall KernelMmAllocateContiguousMemoryEx(unsigned long size, unsigned long lowest, unsigned long highest,
                                                   unsigned long alignment, unsigned long protect);   // 166
void __stdcall KernelMmFreeContiguousMemory(void *base);                                         // 171
long __stdcall KernelExQueryNonVolatileSetting(unsigned long index, unsigned long *type, void *value,
                                               unsigned long length, unsigned long *resultLength);   // 24
volatile int *KernelKeTickCount();                                                               // 156
unsigned long *KernelHalBootSMCVideoMode();                                                      // 356

// What the kernel does before the game starts: the standalone build's starts its millisecond count (the loader's
// kernel has started its own)
void KernelStart(void);

#endif // DRIVING_PLATFORM_KERNELIMPORTS_H_
