#include "MemTrace.h"
#include "../game.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

// See MemTrace.h. The heap's block headers, as memory.cpp lays them out.
#pragma pack(push, 1)
typedef struct TraceBlock {
    TraceBlock *prev;
    uint32_t size;
    uint16_t flags;     // high byte the allocation type
    uint8_t slack;
    uint8_t isFree;
} TraceBlock;
#pragma pack(pop)

extern uint32_t PtrHeap, HeapByteSize;

void MemTrace_Frame(void) {
    static FILE *file;
    static bool checked;
    static uint32_t frames;
    if (!checked) {
        checked = true;
        const char *name = getenv("NIGHTFIRE_MEM_TRACE");
        if (name != NULL && name[0] != 0)
            file = fopen(name, "w");
    }
    if (file == NULL || PtrHeap == 0 || frames++ % 300 != 0)
        return;
    uint32_t used = 0, largest = 0, blocks = 0, byType[256] = {0};
    char *end = (char *)(uintptr_t)PtrHeap + HeapByteSize - sizeof(TraceBlock);
    for (char *p = (char *)(uintptr_t)PtrHeap; p < end; p += ((TraceBlock *)p)->size) {
        TraceBlock *block = (TraceBlock *)p;
        if (block->size == 0)
            break;
        blocks++;
        if (block->isFree) {
            if (block->size > largest)
                largest = block->size;
        } else {
            used += block->size;
            byType[block->flags >> 8] += block->size;
        }
    }
    fprintf(file, "f=%u used=%u largestfree=%u blocks=%u", (unsigned)GameState.NumFrames, used, largest, blocks);
    for (int t = 0; t < 256; t++)
        if (byType[t] != 0)
            fprintf(file, " %02x:%u", t, byType[t]);
    fputc('\n', file);
    fflush(file);
}
