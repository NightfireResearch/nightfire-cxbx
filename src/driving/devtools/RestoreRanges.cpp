// NIGHTFIRE_RESTORE_RANGES=lo-hi[,lo-hi...] (hex addresses, end exclusive): after the patches are written, puts the
// original code back at every patched entry in those ranges, so the game runs the originals there. For bisecting a
// misbehaving port without rebuilding. Our code that calls a port directly still reaches the port.

#include "RestoreRanges.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../common/xbeOriginal.h"

void RestoreRanges_Apply(void) {
    const char *spec = getenv("NIGHTFIRE_RESTORE_RANGES");
    if (spec == NULL || *spec == 0)
        return;
    while (*spec) {
        char *end;
        unsigned long lo = strtoul(spec, &end, 16);
        if (*end != '-')
            break;
        unsigned long hi = strtoul(end + 1, &end, 16);
        int n = XbeOriginal_RestoreRange((unsigned)lo, (unsigned)hi, true);
        printf("[restore] 0x%08lx-0x%08lx: %d originals back in place\n", lo, hi, n);
        spec = end;
        while (*spec == ',' || *spec == ';' || *spec == ' ')
            spec++;
    }
    fflush(stdout);
}
