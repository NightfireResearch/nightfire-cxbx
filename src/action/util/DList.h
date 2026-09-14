#ifndef DLIST_H
#define DLIST_H

#include "../actionhelpers.h"

typedef struct DLISTINFO_tag {
    LLISTINFO_tag freeList;
    LLISTINFO_tag activeList;
} DLISTINFO_tag;

static_assert(sizeof(DLISTINFO_tag) == 0x18, "Bad size for DLISTINFO_tag");

bool DList_Init(DLISTINFO_tag* dlist, ushort entrySize, ushort param_3);
bool DList_RemoveFromInUse2Free(DLISTINFO_tag* list, LLNODE_tag* node);
void* DList_MoveFromFree2InUse(DLISTINFO_tag* param_1);

#endif // DLIST_H