#include "FileSysTrace.h"

#include "../platform/FileSys.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------------------------------------------
// NIGHTFIRE_FSTRACE=1 prints the FILESYS ops the worker executes (platform/FileSys.cpp): every open, exists,
// archive registration and close as it happens, and the reads as a running tally per priority, since the sound
// streamer reads continuously. Priority 99/100 is a FILE_ helper's sync traffic; anything else is the
// streamer's (its normal and greedy priorities), so a run that shows both has exercised the asynchronous path
// against the synchronous one.
// ---------------------------------------------------------------------------------------------------------------

static const char *const kTypes[] = { "open", "close", "read", "write", "size", "nop", "exists", "delete", "null",
                                      "addbig", "delbig" };

static LONG g_reads[256];
static LONG g_readBytes[256];
static LONG g_failures;

static void Trace(int type, int priority, const char *name, int status, int bytes) {
    priority &= 0xff;
    if (status != 1)
        InterlockedIncrement(&g_failures);
    if (type == 2) {
        LONG n = InterlockedIncrement(&g_reads[priority]);
        InterlockedExchangeAdd(&g_readBytes[priority], bytes);
        if (n % 500 == 0)
            printf("[fstrace] %ld reads at priority %d, %ld bytes\n", n, priority, g_readBytes[priority]);
        return;
    }
    // Opens and existence checks are a few hundred per load, so only the streamer's and failures are named
    if (type == 0 || type == 6) {
        if (priority < 99 || status != 1)
            printf("[fstrace] %s %s at priority %d: status %d\n", kTypes[type], name ? name : "", priority, status);
        return;
    }
    printf("[fstrace] %s %s at priority %d: status %d, %d\n", type >= 0 && type <= 10 ? kTypes[type] : "?",
           name ? name : "", priority, status, bytes);
}

void FileSysTrace_Install(void) {
    if (getenv("NIGHTFIRE_FSTRACE") == NULL)
        return;
    FsTrace = Trace;
    printf("[fstrace] tracing FILESYS ops\n");
}
