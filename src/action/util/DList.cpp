#include "DList.h"

// AUTOINJECT
bool DList_Init(DLISTINFO_tag* dlist, ushort entrySize, ushort numEntries) {

    if (dlist == NULL)
        return false;

    if (!LList_Init(&dlist->freeList, entrySize, numEntries))
        return false;

    return LList_Init(&dlist->activeList, entrySize, 0);
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
