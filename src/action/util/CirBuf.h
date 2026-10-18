#ifndef CIRBUF_H_
#define CIRBUF_H_

#include "../actionhelpers.h"

#pragma pack(push, 1)
// A ring of words: CirBuf_Add puts each at the head and hands back the one it replaces
typedef struct CIRBUF_INFO {
    int *buffer;
    ushort tail;        // 0x04 the oldest
    ushort head;        // 0x06 where the next goes
    ushort count;       // 0x08 how many it holds
    ushort unknown0a;
    ushort size;        // 0x0c
    ushort pad;
} CIRBUF_INFO;
#pragma pack(pop)
static_assert(sizeof(CIRBUF_INFO) == 0x10, "CIRBUF_INFO is 0x10 bytes");

bool CirBuf_Init(CIRBUF_INFO *info, ushort size, int *data);
int CirBuf_Add(CIRBUF_INFO *info, int value);

#endif // CIRBUF_H_
