#ifndef CIRBUF_H_
#define CIRBUF_H_

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

#pragma pack(push, 1)
// A ring of words, each an integer or an address: CirBuf_Add puts each at the head and hands back the one it replaces
typedef struct CIRBUF_INFO {
    intptr_t *buffer;
    ushort tail;        // 0x04 the oldest
    ushort head;        // 0x06 where the next goes
    ushort count;       // 0x08 how many it holds
    ushort unknown0a;
    ushort size;        // 0x0c
    ushort pad;
} CIRBUF_INFO;
#pragma pack(pop)
LAYOUT_32(sizeof(CIRBUF_INFO) == 0x10, "CIRBUF_INFO is 0x10 bytes");

bool CirBuf_Init(CIRBUF_INFO *info, ushort size, intptr_t *data);
intptr_t CirBuf_Add(CIRBUF_INFO *info, intptr_t value);

#endif // CIRBUF_H_
