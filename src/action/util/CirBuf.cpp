#include "CirBuf.h"

#include "../memory.h"

#include <string.h>

// A ring of size words, emptied, in data or (without it) on the heap
// AUTOINJECT
bool CirBuf_Init(CIRBUF_INFO *info, ushort size, intptr_t *data) {
    if (info == NULL)
        return false;
    info->unknown0a = 0;
    info->count = 0;
    info->tail = 0;
    info->head = 0;
    info->buffer = data;
    info->size = size;
    if (data == NULL && size != 0)
        info->buffer = (intptr_t *)Mem_Malloc(size * sizeof(intptr_t), 0x1b04, 0);
    memset(info->buffer, 0, size * sizeof(intptr_t));
    return true;
}

// Puts value at the head and returns what was there: once the ring is full, the oldest
// AUTOINJECT
intptr_t CirBuf_Add(CIRBUF_INFO *info, intptr_t value) {
    if (info == NULL)
        return 0;
    info->count = info->count + 1 < info->size ? info->count + 1 : info->size;
    intptr_t *slot = &info->buffer[info->head];
    intptr_t old = *slot;
    *slot = value;
    info->head++;
    if (info->head >= info->size)
        info->head = 0;
    if (info->tail == info->head)
        info->tail++;
    if (info->tail >= info->size)
        info->tail = 0;
    return old;
}
