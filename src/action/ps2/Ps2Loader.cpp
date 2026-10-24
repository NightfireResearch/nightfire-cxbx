#include "Ps2Loader.h"
#include "../engine/FS.h"
#include "../engine/Loader.h"

#include <stdio.h>

// LoaderLoad's variant that makes bits 20-23 0xF (it adds 7)
#define LOADER_VARIANT_SEQUENCES 8
#define SEQUENCE_BUNDLE_BITS 0x00f00000

void Ps2_LoadLevelSequences(uint32_t level) {
    char name[16];
    snprintf(name, sizeof(name), "%8.8x.bin", (level & ~SEQUENCE_BUNDLE_BITS) | SEQUENCE_BUNDLE_BITS);
    if (FS_MatchFilenameToHeader(name) < 0)
        return;
    while (LoaderLoad(1, level, 0, LOADER_VARIANT_SEQUENCES))
        ;
}
