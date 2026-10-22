#include "DList.h"

static bool DList_InitSized(DLISTINFO_tag* dlist, ushort entrySize, ushort xboxEntrySize, ushort numEntries);

// AUTOINJECT
bool DList_Init(DLISTINFO_tag* dlist, ushort entrySize, ushort numEntries) {
    return DList_InitSized(dlist, entrySize, entrySize, numEntries);
}

#if XBOX_SIZES_DIFFER
bool DList_InitXbox(DLISTINFO_tag* dlist, ushort entrySize, ushort xboxEntrySize, ushort numEntries) {
    return DList_InitSized(dlist, entrySize, xboxEntrySize, numEntries);
}
#endif

static bool DList_InitSized(DLISTINFO_tag* dlist, ushort entrySize, ushort xboxEntrySize, ushort numEntries) {

    if (dlist == NULL)
        return false;

    if (!LList_InitXbox(&dlist->freeList, entrySize, xboxEntrySize, numEntries))
        return false;

    return LList_InitXbox(&dlist->activeList, entrySize, xboxEntrySize, 0);
}

// The node goes to the head of the free list
// AUTOINJECT
bool DList_RemoveFromInUse2Free(DLISTINFO_tag *list, LLNODE_tag *node) {

    LLNODE_tag *removed = LList_Remove(&list->activeList, node);
    if (removed == NULL)
        return false;

    LList_Insert(&list->freeList, NULL, removed);
    return true;
}

// The free list's last node, put at the end of the list in use
// AUTOINJECT
void* DList_MoveFromFree2InUse(DLISTINFO_tag* list) {

    if (list == NULL)
        return NULL;

    LLNODE_tag *node = LList_Cut(&list->freeList);
    if (node == NULL)
        return NULL;

    return LList_Add(&list->activeList, node) ? node : NULL;
}
