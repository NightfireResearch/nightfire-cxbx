#include "CrashTest.h"

#include <stdio.h>
#include <stdlib.h>

#include "../platform/Host.h"

// The fault is a write to address 0x10, two calls down on a thread of its own, so that the report has a frame chain
// and return addresses in the executable to name.

namespace {

unsigned long g_delayMs;

#if defined(_MSC_VER)
#define CRASHTEST_NOINLINE __declspec(noinline)
#else
#define CRASHTEST_NOINLINE __attribute__((noinline))
#endif

CRASHTEST_NOINLINE void Fault(int value) {
    *(volatile int *)(uintptr_t)0x10 = value;
}

CRASHTEST_NOINLINE void FaultCaller(int value) {
    Fault(value + 1);
    printf("[crashtest] no fault\n");
}

unsigned long __stdcall CrashThread(void *) {
    HostSleep(g_delayMs);
    printf("[crashtest] writing to 0x10 after %lu ms\n", g_delayMs);
    fflush(stdout);
    FaultCaller(0x1234);
    return 0;
}

} // namespace

void CrashTest_Start(void) {
    char text[16];
    if (HostGetEnvironment("NIGHTFIRE_CRASHTEST", text, sizeof(text)) == 0)
        return;
    g_delayMs = strtoul(text, NULL, 10);
    HostHandle thread = HostCreateThread(0x10000, CrashThread, NULL, 0, NULL);
    if (thread != NULL)
        HostCloseHandle(thread);
}
