#include "Collide.h"

#define HitHeap (*(LLISTINFO_tag*)0x001ddc60)

// AUTOGEN
bool Collide_RayIntersect(_VECTOR *position, _VECTOR *endPosition, cel_tag *cel, obj_tag* obj, int param_5, HITLIST* hitList, int param_7, int param_8, int param_9);


// AUTOINJECT
void Collide_FreeHitList(HITDATA_tag **hitList) {

    // If the list has already been freed, return
    if(hitList == NULL)
        return;

    // Iterate through the list and add each element to the HitHeap
    LLNODE_tag* head = hitList->head;    
    while(head != NULL) {
        LLNODE_tag* next = (head->next);
        LList_Add(&HitHeap, (LLNODE_tag*)head);
        head = next;
    }

    // Discard all entries from the list
    hitList->head = NULL;

    // ?? don't update the count?!

}