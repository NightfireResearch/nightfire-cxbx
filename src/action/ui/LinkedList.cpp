#include "../actionhelpers.h"
#include "LinkedList.h"

// The element at index, counting from the head
// AUTOINJECT
void* LinkedList_GetIndex(LINKEDLIST *list, uint index) {
    if (list == NULL)
        return NULL;
    if (index > list->count - 1)
        return NULL;
    uint i = 0;
    for (void **link = (void **)list->head; link != NULL; link = (void **)link[list->offset]) {
        if (i == index)
            return (char *)link - list->offset * 4;
        i++;
    }
    return NULL;
}
