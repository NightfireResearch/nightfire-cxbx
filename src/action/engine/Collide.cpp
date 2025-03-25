#include "Collide.h"

#include <string.h>

#define HitHeap (*(LLISTINFO_tag*)0x001ddc60)

// AUTOGEN
bool Collide_RayIntersect(_VECTOR *position, _VECTOR *endPosition, cel_tag *cel, obj_tag* obj, obj_tag* param_5, HITDATA_tag** hitList, char param_7, uint param_8, ushort param_9);

// AUTOINJECT
void Collide_FreeHitList(HITDATA_tag **hitList) {

    // If the list has already been freed, return
    if(hitList == NULL)
        return;

    // Iterate through the list and add each element to the HitHeap
    LLNODE_tag* head = (LLNODE_tag*)*hitList;
    while(head != NULL) {
        LLNODE_tag* next = (head->next);
        LList_Add(&HitHeap, (LLNODE_tag*)head);
        head = next;
    }

    // Discard all entries from the list
    *hitList = NULL;

    // ?? don't update the count?!

}

#define HitAllocCnt U32_AT(0x001dec24)

// AUTOINJECT
HITDATA_tag* Coll_GetFreeHit(void) {

    // Try to obtain from the heap
    HITDATA_tag* node = (HITDATA_tag*)LList_Cut(&HitHeap);
    if(node == NULL) {

        // Heap is empty and the maximum number of allocations has been reached  
        if(HitAllocCnt > 1000)
            return NULL;

        // Heap is empty but we haven't reached the maximum number of allocations yet
        HitAllocCnt += 0x40;
        LList_AllocnNodes(&HitHeap, 0x40);
        node = (HITDATA_tag*)LList_Cut(&HitHeap);
    }

    memset(node, 0, sizeof(HITDATA_tag)); 
    return node;
}