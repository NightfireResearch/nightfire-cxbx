#include "xbeProfiler.h"

#include <windows.h>
#include <tlhelp32.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Samples are bucketed by address so that a run of instructions inside one function collapses into one line.
// 16 bytes is fine enough to tell two loops in a function apart and coarse enough that a hot loop is one row.
enum { BUCKET_SHIFT = 4, MAX_BUCKETS = 1024, MAX_THREADS = 16 };

struct Bucket {
    uint32_t address;
    uint32_t hits;
};

// One histogram per thread. The game runs on more than one - the file system has a worker, and the seams
// have their own - and a stall on the game thread is usually explained by what another thread is not doing,
// so all of them are sampled and reported separately.
struct SampledThread {
    uint32_t id;
    HANDLE   handle;
    uint32_t samples;
    uint32_t bucketCount;
    Bucket   buckets[MAX_BUCKETS];
};

static SampledThread g_threads[MAX_THREADS];
static uint32_t g_threadCount;
static uint32_t g_gameThreadId;
static volatile LONG g_running;

static void Record(SampledThread *thread, uint32_t eip) {
    uint32_t key = eip >> BUCKET_SHIFT;
    for (uint32_t i = 0; i < thread->bucketCount; i++) {
        if (thread->buckets[i].address == key) { thread->buckets[i].hits++; return; }
    }
    if (thread->bucketCount < MAX_BUCKETS) {
        thread->buckets[thread->bucketCount].address = key;
        thread->buckets[thread->bucketCount].hits = 1;
        thread->bucketCount++;
    }
}

// Threads come and go, so the list is refreshed from time to time rather than taken once at the start.
static void RefreshThreadList(uint32_t samplerThreadId) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return;

    THREADENTRY32 entry;
    entry.dwSize = sizeof(entry);
    uint32_t self = GetCurrentProcessId();
    for (BOOL more = Thread32First(snapshot, &entry); more; more = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID != self || entry.th32ThreadID == samplerThreadId)
            continue;

        bool known = false;
        for (uint32_t i = 0; i < g_threadCount; i++)
            known = known || g_threads[i].id == entry.th32ThreadID;
        if (known || g_threadCount >= MAX_THREADS)
            continue;

        HANDLE handle = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT, FALSE, entry.th32ThreadID);
        if (handle == NULL)
            continue;
        g_threads[g_threadCount].id = entry.th32ThreadID;
        g_threads[g_threadCount].handle = handle;
        g_threadCount++;
    }
    CloseHandle(snapshot);
}

static DWORD WINAPI SamplerThread(LPVOID) {
    uint32_t self = GetCurrentThreadId();
    timeBeginPeriod(1);
    for (uint32_t pass = 0; g_running; pass++) {
        if (pass % 500 == 0)
            RefreshThreadList(self);

        for (uint32_t i = 0; i < g_threadCount; i++) {
            SampledThread *thread = &g_threads[i];
            if (SuspendThread(thread->handle) == (DWORD)-1)
                continue;
            CONTEXT context;
            memset(&context, 0, sizeof(context));
            context.ContextFlags = CONTEXT_CONTROL;
            uint32_t eip = GetThreadContext(thread->handle, &context) ? context.Eip : 0;
            ResumeThread(thread->handle);
            if (eip != 0) { Record(thread, eip); thread->samples++; }
        }
        Sleep(1);
    }
    return 0;
}

static void Profiler_Start(void) {
    if (g_running)
        return;
    g_gameThreadId = GetCurrentThreadId();
    g_running = 1;
    CloseHandle(CreateThread(NULL, 0, SamplerThread, NULL, 0, NULL));
}

// Samples that land outside the XBE are in a Windows DLL - most often a wait deep inside ntdll - and the
// module plus nearest exported name is enough to tell which. The export table is walked by hand rather than
// through dbghelp, since the nearest-preceding export is all that is wanted and system DLLs export plenty.
static const char *DescribeAddress(uint32_t address, char *buffer, size_t bufferSize) {
    HMODULE module = NULL;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCSTR)(uintptr_t)address, &module) || module == NULL) {
        return "";  // the XBE itself, which has no exports - the address is the one Ghidra shows
    }

    char moduleName[MAX_PATH] = "";
    GetModuleFileNameA(module, moduleName, sizeof(moduleName));
    const char *leaf = strrchr(moduleName, '\\');
    leaf = leaf != NULL ? leaf + 1 : moduleName;

    const IMAGE_DOS_HEADER *dos = (const IMAGE_DOS_HEADER *)module;
    const IMAGE_NT_HEADERS *nt = (const IMAGE_NT_HEADERS *)((const char *)module + dos->e_lfanew);
    uint32_t exportRva = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT].VirtualAddress;
    const char *best = NULL;
    uint32_t bestRva = 0;
    if (exportRva != 0) {
        const IMAGE_EXPORT_DIRECTORY *exports = (const IMAGE_EXPORT_DIRECTORY *)((const char *)module + exportRva);
        const uint32_t *functions = (const uint32_t *)((const char *)module + exports->AddressOfFunctions);
        const uint32_t *names = (const uint32_t *)((const char *)module + exports->AddressOfNames);
        const uint16_t *ordinals = (const uint16_t *)((const char *)module + exports->AddressOfNameOrdinals);
        uint32_t target = address - (uint32_t)(uintptr_t)module;
        for (uint32_t i = 0; i < exports->NumberOfNames; i++) {
            uint32_t rva = functions[ordinals[i]];
            if (rva <= target && rva >= bestRva) {
                bestRva = rva;
                best = (const char *)module + names[i];
            }
        }
    }

    if (best != NULL)
        _snprintf(buffer, bufferSize, "  %s!%s+0x%x", leaf, best, address - (uint32_t)(uintptr_t)module - bestRva);
    else
        _snprintf(buffer, bufferSize, "  %s+0x%x", leaf, address - (uint32_t)(uintptr_t)module);
    buffer[bufferSize - 1] = 0;
    return buffer;
}

static int ByHits(const void *a, const void *b) {
    return (int)((const Bucket *)b)->hits - (int)((const Bucket *)a)->hits;
}

static void Profiler_Report(const char *what) {
    for (uint32_t t = 0; t < g_threadCount; t++) {
        SampledThread *thread = &g_threads[t];
        uint32_t samples = thread->samples;
        uint32_t bucketCount = thread->bucketCount;
        if (samples == 0)
            continue;

        Bucket sorted[MAX_BUCKETS];
        memcpy(sorted, thread->buckets, bucketCount * sizeof(sorted[0]));
        thread->samples = 0;
        thread->bucketCount = 0;
        qsort(sorted, bucketCount, sizeof(sorted[0]), ByHits);

        printf("[profile] %s: thread %u%s, %u samples:\n", what, thread->id,
               thread->id == g_gameThreadId ? " (the game)" : "", samples);
        uint32_t shown = bucketCount < 8 ? bucketCount : 8;
        for (uint32_t i = 0; i < shown; i++) {
            char where[160] = "";
            printf("[profile]   0x%08x  %5.1f%%  (%u)%s\n", sorted[i].address << BUCKET_SHIFT,
                   sorted[i].hits * 100.0 / samples, sorted[i].hits,
                   DescribeAddress(sorted[i].address << BUCKET_SHIFT, where, sizeof(where)));
        }
    }
    fflush(stdout);
}

// The one entry point the rest of the code uses. The environment variable keeps the cost of having this
// here at a getenv per frame and nothing else, so it can sit in the frame loop permanently.
void Profiler_Frame(const char *what) {
    static int enabled = -1;
    if (enabled < 0) {
        const char *setting = getenv("NIGHTFIRE_PROFILE");
        enabled = (setting != NULL && *setting != '0') ? 1 : 0;
        if (enabled)
            Profiler_Start();
    }
    if (!enabled)
        return;

    static uint32_t frames = 0;
    if (++frames % 250 == 0)
        Profiler_Report(what);
}
