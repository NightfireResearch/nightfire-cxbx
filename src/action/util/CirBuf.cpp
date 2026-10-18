#include "CirBuf.h"

#include "../memory.h"

#include <string.h>

// A ring of size words, emptied, in data or (without it) on the heap
// AUTOINJECT
bool CirBuf_Init(CIRBUF_INFO *info, ushort size, int *data) {
    if (info == NULL)
        return false;
    info->unknown0a = 0;
    info->count = 0;
    info->tail = 0;
    info->head = 0;
    info->buffer = data;
    info->size = size;
    if (data == NULL && size != 0)
        info->buffer = (int *)Mem_Malloc(size * 4, 0x1b04, 0);
    memset(info->buffer, 0, size * 4);
    return true;
}

// Puts value at the head and returns what was there: once the ring is full, the oldest
// AUTOINJECT
int CirBuf_Add(CIRBUF_INFO *info, int value) {
    if (info == NULL)
        return 0;
    info->count = info->count + 1 < info->size ? info->count + 1 : info->size;
    int *slot = &info->buffer[info->head];
    int old = *slot;
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
