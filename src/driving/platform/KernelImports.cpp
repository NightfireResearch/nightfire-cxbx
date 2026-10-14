#include "KernelImports.h"

#ifndef NIGHTFIRE_STANDALONE   // the injected build: the loader's kernel

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

void KernelStart(void) {
}

#else   // NIGHTFIRE_STANDALONE

// ---------------------------------------------------------------------------------------------------------------
// The standalone build's kernel: what src/loader/kernel.cpp answers for these ordinals, over the host layer.
// ---------------------------------------------------------------------------------------------------------------

#include "Host.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ---- critical sections. The Xbox's are 28 bytes, built in place in the game's records; a host lock fits. A
// static Xbox section is laid out unlocked by the compiler and entered with no initialiser ever run, so each is
// initialised on its first entry unless it was initialised before (kernel.cpp has the reasoning).

static_assert(sizeof(HostLock) <= 0x1c, "a host lock fits where the game keeps an Xbox critical section");

static const unsigned kKnownSectionMax = 1024;
static HostLock KnownSectionsLock;
static void *KnownSections[kKnownSectionMax];
static unsigned KnownSectionCount;

static bool IsNewSection(void *section) {
    bool isNew = true;
    HostEnterLock(&KnownSectionsLock);
    for (unsigned i = 0; i < KnownSectionCount; i++) {
        if (KnownSections[i] == section) {
            isNew = false;
            break;
        }
    }
    if (isNew) {
        if (KnownSectionCount < kKnownSectionMax) {
            KnownSections[KnownSectionCount++] = section;
        } else {
            printf("[kernel] more than %u critical sections - no longer tracking them\n", kKnownSectionMax);
            isNew = false;
        }
    }
    HostLeaveLock(&KnownSectionsLock);
    return isNew;
}

void __stdcall KernelRtlInitializeCriticalSection(void *section) {
    IsNewSection(section);
    HostInitLock(static_cast<HostLock *>(section));
}

void __stdcall KernelRtlEnterCriticalSection(void *section) {
    if (IsNewSection(section))
        HostInitLock(static_cast<HostLock *>(section));
    HostEnterLock(static_cast<HostLock *>(section));
}

void __stdcall KernelRtlLeaveCriticalSection(void *section) {
    HostLeaveLock(static_cast<HostLock *>(section));
}

// ---- the console's panic: say which code it was and stop

void __stdcall KernelKeBugCheck(uint32_t code) {
    printf("\n[kernel] the game called KeBugCheck(0x%x) - it has decided it cannot continue\n", code);
    fflush(stdout);
    HostExitProcess(1);
}

// ---- contiguous memory: pages of their own, below 0x10000000 where there is room, as the loader places them

void *__stdcall KernelMmAllocateContiguousMemoryEx(unsigned long size, unsigned long lowest, unsigned long highest,
                                                   unsigned long alignment, unsigned long protect) {
    (void)protect;
    uintptr_t limit = 0x10000000;
    if (uintptr_t(highest) < limit - 1)
        limit = uintptr_t(highest) + 1;
    void *block = HostAllocatePages(size, lowest, limit);
    if (block == NULL)
        printf("[kernel] MmAllocateContiguousMemoryEx: %lu bytes refused\n", size);
    else if (alignment > 1 && (uintptr_t(block) & (alignment - 1)) != 0)
        printf("[kernel] MmAllocateContiguousMemoryEx: %p is not aligned to 0x%lx\n", block, alignment);
    return block;
}

void __stdcall KernelMmFreeContiguousMemory(void *base) {
    HostFreePages(base);
}

// ---- the EEPROM's settings: PAL-I, English, 4:3 unless settings.ini says Widescreen, stereo, no parental control

long __stdcall KernelExQueryNonVolatileSetting(unsigned long index, unsigned long *type, void *value,
                                               unsigned long length, unsigned long *resultLength) {
    const long kSuccess = 0, kInvalidParameter = long(0xc000000d), kNotFound = long(0xc0000034);
    const unsigned long kRegBinary = 3, kRegDword = 4;
    unsigned long setting = 0;
    switch (index) {
    case 0x07: setting = 1; break;                          // XC_LANGUAGE: English
    case 0x08: {                                            // XC_VIDEO: bit 0 widescreen
        char text[16];
        HostGetProfileString("Settings", "Widescreen", "0", text, sizeof(text), ".\\settings.ini");
        setting = strtol(text, NULL, 10) != 0 ? 1 : 0;
        break;
    }
    case 0x09: setting = 0; break;                          // XC_AUDIO: stereo
    case 0x0a: setting = 0; break;                          // XC_P_CONTROL_GAMES
    case 0x11: setting = 0; break;                          // XC_MISC
    case 0x12: setting = 2; break;                          // XC_DVD_REGION: Europe
    case 0x103: setting = 0x300; break;                     // XC_FACTORY_AV_REGION: PAL-I
    case 0x104: setting = 2; break;                         // XC_FACTORY_GAME_REGION
    case 0xff:                                              // XC_MAX_OS: the whole block, zero (UTC)
        if (value == NULL)
            return kInvalidParameter;
        memset(value, 0, length);
        if (type != NULL)
            *type = kRegBinary;
        if (resultLength != NULL)
            *resultLength = length;
        return kSuccess;
    default:
        printf("[kernel] ExQueryNonVolatileSetting: no answer for setting 0x%lx\n", index);
        fflush(stdout);
        return kNotFound;
    }
    if (value == NULL || length < sizeof(unsigned long))
        return kInvalidParameter;
    memcpy(value, &setting, sizeof(setting));
    if (type != NULL)
        *type = kRegDword;
    if (resultLength != NULL)
        *resultLength = sizeof(setting);
    return kSuccess;
}

// ---- the data exports. KeTickCount counts milliseconds, one thread advancing it; the console did not boot into
// an HD-capable mode.

static volatile int TickCount;
static unsigned long BootSMCVideoMode;

static unsigned long __stdcall TickCountThread(void *) {
    HostRaiseTimerResolution();
    unsigned long start = HostTimeMs();
    for (;;) {
        TickCount = int(HostTimeMs() - start);
        HostSleep(1);
    }
}

volatile int *KernelKeTickCount() {
    return &TickCount;
}

unsigned long *KernelHalBootSMCVideoMode() {
    return &BootSMCVideoMode;
}

void KernelStart(void) {
    HostInitLock(&KnownSectionsLock);
    HostCloseHandle(HostCreateThread(0, TickCountThread, NULL, 0, NULL));
}

#endif   // NIGHTFIRE_STANDALONE
