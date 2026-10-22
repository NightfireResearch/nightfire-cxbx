#ifndef DLIST_H
#define DLIST_H

#include "../actionhelpers.h"
#include "../../common/xbeClass.h"

typedef struct DLISTINFO_tag {
    LLISTINFO_tag freeList;
    LLISTINFO_tag activeList;
} DLISTINFO_tag;

LAYOUT_32(sizeof(DLISTINFO_tag) == 0x18, "Bad size for DLISTINFO_tag");

bool DList_Init(DLISTINFO_tag* dlist, ushort entrySize, ushort numEntries);
// DList_Init of nodes that are xboxEntrySize bytes on the Xbox (XboxHeap.h; XBOX_SIZEOF)
#if XBOX_SIZES_DIFFER
bool DList_InitXbox(DLISTINFO_tag* dlist, ushort entrySize, ushort xboxEntrySize, ushort numEntries);
#else
#define DList_InitXbox(dlist, entrySize, xboxEntrySize, numEntries) DList_Init(dlist, entrySize, numEntries)
#endif
bool DList_RemoveFromInUse2Free(DLISTINFO_tag* list, LLNODE_tag* node);
void* DList_MoveFromFree2InUse(DLISTINFO_tag* list);

#endif // DLIST_H