// Checks RunStaticInitialisers (src/driving/engine/StaticInitTable.cpp) against the XBE's own initialiser table,
// by what each leaves in memory: two boots, one of each, dumped at the same point and compared offline.
//
//   NIGHTFIRE_STATICDUMP=<file>   after the C++ static initialisers, write .data and .bss and the atexit list to
//                                 <file> and exit
//   NIGHTFIRE_STATICORIG=1        run the XBE's table (0x001b3db0) instead of ours, as _cinit walked it
//
// The file is the region's bytes (0x001b3da0-0x0024b5bc), then the number of atexit entries and the entries.

#include "StaticInitDump.h"

#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

#define CppInitTableBegin ((void (**)(void))0x001b3db0u)
#define CppInitTableEnd   ((void (**)(void))0x001b4904u)
#define DSoundSectionBegin 0x0017ac40u
#define DSoundSectionEnd   0x00183aa4u
#define DataBegin 0x001b3da0u
#define DataEnd   0x0024b5bcu
#define OnExitBegin (*(uint32_t **)0x0024b5b4u)   // the C runtime's atexit table
#define OnExitEnd   (*(uint32_t **)0x0024b5b0u)

bool StaticInitDump_RunOriginalTable(void) {
    const char *flag = getenv("NIGHTFIRE_STATICORIG");
    if (flag == NULL || *flag != '1')
        return false;
    printf("[staticdump] running the XBE's own C++ initialiser table\n");
    for (void (**entry)(void) = CppInitTableBegin; entry < CppInitTableEnd; entry++) {
        uintptr_t target = (uintptr_t)*entry;
        if (target == 0 || target == (uintptr_t)-1 || (target >= DSoundSectionBegin && target < DSoundSectionEnd))
            continue;
        (*entry)();
    }
    return true;
}

void StaticInitDump_Write(void) {
    const char *path = getenv("NIGHTFIRE_STATICDUMP");
    if (path == NULL || *path == 0)
        return;
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        printf("[staticdump] cannot write %s\n", path);
    } else {
        fwrite((const void *)DataBegin, 1, DataEnd - DataBegin, file);
        uint32_t count = (uint32_t)(OnExitEnd - OnExitBegin);
        fwrite(&count, 4, 1, file);
        fwrite(OnExitBegin, 4, count, file);
        fclose(file);
        printf("[staticdump] wrote %s (%u atexit entries)\n", path, count);
    }
    fflush(stdout);
    ExitProcess(0);
}
