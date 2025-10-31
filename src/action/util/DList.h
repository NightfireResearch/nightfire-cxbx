#ifndef DLIST_H
#define DLIST_H

#include "../actionhelpers.h"

bool DList_RemoveFromInUse2Free(DLISTINFO_tag* list, LLNODE_tag* node);
void* DList_MoveFromFree2InUse(DLISTINFO_tag* param_1);

#endif // DLIST_H