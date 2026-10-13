#include "XbeData.h"

#include <string.h>

XbeInitialiser::XbeInitialiser(void *global, uint32_t size, const XbeBlob &blob) {
    memcpy(global, blob.words, size);
    for (uint32_t i = 0; i < blob.numRelocations; i++)
        memcpy((char *)global + blob.relocations[i].offset, &blob.relocations[i].target, sizeof(void *));
}
