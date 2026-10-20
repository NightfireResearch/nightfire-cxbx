#include "DataWrites.h"
#include "../game.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// See DataWrites.h
static const uint32_t DATA_START = 0x0015d160;
static const uint32_t DATA_END = 0x00300cc8;
static const uint32_t SIZE = DATA_END - DATA_START;
static const uint32_t BLOCK = 4096;
static const uint32_t NOT_CHANGED = 0xffffffff;

static char s_path[260];
static uint8_t *s_start;         // the data as Inject found it
static uint32_t *s_firstFrame;   // per byte: the first frame it differed, or NOT_CHANGED
static uint32_t s_frames;

static void Write(void) {
    FILE *file = fopen(s_path, "w");
    if (file == NULL)
        return;
    fprintf(file, "# NIGHTFIRE_DATA_WRITES: start end first-frame, %u frames\n", s_frames);
    for (uint32_t i = 0; i < SIZE;) {
        if (s_firstFrame[i] == NOT_CHANGED) {
            i++;
            continue;
        }
        uint32_t j = i, first = s_firstFrame[i];
        while (j < SIZE && s_firstFrame[j] != NOT_CHANGED) {
            if (s_firstFrame[j] < first)
                first = s_firstFrame[j];
            j++;
        }
        fprintf(file, "%08x %08x %u\n", DATA_START + i, DATA_START + j, first);
        i = j;
    }
    fclose(file);
}

void DataWrites_Init(void) {
    const char *name = getenv("NIGHTFIRE_DATA_WRITES");
    if (name == NULL || name[0] == 0)
        return;
    strncpy(s_path, name, sizeof(s_path) - 1);
    s_start = (uint8_t *)malloc(SIZE);
    s_firstFrame = (uint32_t *)malloc(SIZE * sizeof(uint32_t));
    memcpy(s_start, (const void *)DATA_START, SIZE);
    memset(s_firstFrame, 0xff, SIZE * sizeof(uint32_t));
    atexit(Write);
}

void DataWrites_Frame(void) {
    if (s_start == NULL)
        return;
    const uint8_t *now = (const uint8_t *)DATA_START;
    for (uint32_t block = 0; block < SIZE; block += BLOCK) {
        uint32_t count = SIZE - block < BLOCK ? SIZE - block : BLOCK;
        if (memcmp(now + block, s_start + block, count) == 0)
            continue;
        for (uint32_t i = block; i < block + count; i++) {
            if (now[i] != s_start[i] && s_firstFrame[i] == NOT_CHANGED)
                s_firstFrame[i] = s_frames;
        }
    }
    if (++s_frames % 600 == 0)
        Write();
}
