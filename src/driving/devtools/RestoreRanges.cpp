// NIGHTFIRE_RESTORE_RANGES=lo-hi[,lo-hi...] (hex addresses, end exclusive): after the patches are written, puts the
// original code back at every patched entry in those ranges, so the game runs the originals there. For bisecting a
// misbehaving port without rebuilding. Our code that calls a port directly still reaches the port, unless
// NIGHTFIRE_RESTORE_DIRECT=1 is set too: then each port's own entry jumps to its original as well (a port shorter
// than the five-byte jump would have its neighbour overwritten - a bisecting aid only).

#include "RestoreRanges.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "../../common/xbeOriginal.h"

namespace {

// Every patched entry in [lo, hi) back to the original, and its port's entry a jump to it; the number restored
int RestoreDirect(unsigned long lo, unsigned long hi, int *redirected) {
    int n = 0;
    for (unsigned long at = lo; at < hi; at++) {
        const unsigned char *code = (const unsigned char *)at;
        if (code[0] != 0xe9)
            continue;
        long rel;
        memcpy(&rel, code + 1, 4);
        unsigned char *port = (unsigned char *)(at + 5 + rel);
        if (!XbeOriginal_Restore((unsigned)at, true))
            continue;
        n++;
        DWORD old;
        if (!VirtualProtect(port, 5, PAGE_EXECUTE_READWRITE, &old))
            continue;
        long back = (long)(at - ((unsigned long)port + 5));
        port[0] = 0xe9;
        memcpy(port + 1, &back, 4);
        VirtualProtect(port, 5, old, &old);
        FlushInstructionCache(GetCurrentProcess(), port, 5);
        (*redirected)++;
    }
    return n;
}

}  // namespace

void RestoreRanges_Apply(void) {
    const char *spec = getenv("NIGHTFIRE_RESTORE_RANGES");
    if (spec == NULL || *spec == 0)
        return;
    const char *directSpec = getenv("NIGHTFIRE_RESTORE_DIRECT");
    bool direct = directSpec != NULL && atoi(directSpec) != 0;
    while (*spec) {
        char *end;
        unsigned long lo = strtoul(spec, &end, 16);
        if (*end != '-')
            break;
        unsigned long hi = strtoul(end + 1, &end, 16);
        int redirected = 0;
        int n = direct ? RestoreDirect(lo, hi, &redirected) : XbeOriginal_RestoreRange((unsigned)lo, (unsigned)hi, true);
        printf("[restore] 0x%08lx-0x%08lx: %d originals back in place, %d ports sent to them\n", lo, hi, n, redirected);
        spec = end;
        while (*spec == ',' || *spec == ';' || *spec == ' ')
            spec++;
    }
    fflush(stdout);
}
