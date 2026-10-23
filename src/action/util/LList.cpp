#include "LList.h"

// for NULL
#include <stddef.h>

#include <string.h>
#include "../memory.h"

// numEntries nodes of entrySize bytes, zeroed, in one block
// AUTOINJECT
bool LList_Init(LLISTINFO_tag *list, unsigned short entrySize, unsigned short numEntries) {

    if (list == NULL)
        return false;

    char *nodes = numEntries != 0 ? (char *)Mem_Malloc(numEntries * entrySize, 0x1B04, 0) : NULL;

    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->elementSize = entrySize;

    if (numEntries != 0) {
        memset(nodes, 0, numEntries * entrySize);
        for (int i = 0; i < numEntries; i++) {
            LList_Add(list, (LLNODE_tag *)nodes);
            nodes += entrySize;
        }
    }

    return true;
}

// AUTOINJECT
bool LList_Add(LLISTINFO_tag *list, LLNODE_tag *newElement) {

    if(list == NULL || newElement == NULL)
        return false;

    if (list->tail == NULL) {
        list->head = newElement;
        newElement->prev = NULL;
        newElement->next = NULL;
        list->count++;
        list->tail = newElement;
    } else {
        list->tail->next = newElement;
        newElement->prev = list->tail;
        newElement->next = NULL;
        list->count++;
        list->tail = newElement;
    }
    
    return true;

}


// AUTOINJECT
void LList_Insert(LLISTINFO_tag *list, LLNODE_tag *insertionPoint, LLNODE_tag *newElement) {
  
  if(list == NULL || newElement == NULL)
    return;
  
    if (insertionPoint == NULL) {
        // insert at the head of the list
        newElement->prev = NULL;
        newElement->next = list->head;
        list->head = newElement;
        if (list->tail == NULL) {
            list->tail = newElement;
        }
        if (newElement->next != NULL) {
            newElement->next->prev = newElement;
        }
    } else {
        // Insert after insertionPoint
        LLNODE_tag* tmp = insertionPoint->next;
        newElement->prev = insertionPoint;
        newElement->next = tmp;
        if (tmp != NULL) {
            tmp->prev = newElement;
        }
        insertionPoint->next = newElement;
        if (tmp == NULL) {
            list->tail = newElement;
        }
    }

    list->count++;
 
}

// AUTOINJECT
LLNODE_tag* LList_Cut(LLISTINFO_tag *list) {

    if(list == NULL)
        return NULL;

    LLNODE_tag* value = list->tail;
    if(value != NULL) {
        list->tail = value->prev;
        if(list->tail == NULL)
            list->head = NULL;
        else
            list->tail->next = NULL;
        list->count--;
    }

    return value;
        
}

// AUTOINJECT
void LList_AllocnNodes(LLISTINFO_tag *list, int n) {

    if(list == NULL)
        return;

    if(n == 0)
        return;

    void* mem = Mem_Malloc(n * list->elementSize, 0x1B04, 0);
    memset(mem, 0, n * list->elementSize);
    
    for(int i = 0; i < n; i++) {
        LLNODE_tag* node = (LLNODE_tag*)((char*)mem + i * list->elementSize);
        LList_Add(list, node);
    }
}

// An end that was the element passes to its neighbour, the one on the other side if it has none
// AUTOINJECT
LLNODE_tag* LList_Remove(LLISTINFO_tag *list, LLNODE_tag *element) {

    if (list == NULL || element == NULL || list->count == 0)
        return NULL;

    if (list->head == element)
        list->head = element->prev != NULL ? element->prev : element->next;
    if (list->tail == element)
        list->tail = element->next != NULL ? element->next : element->prev;

    if (element->prev != NULL)
        element->prev->next = element->next;
    if (element->next != NULL)
        element->next->prev = element->prev;

    element->next = NULL;
    element->prev = NULL;
    list->count--;

    return element;
}

// Inserts by compare(&element, &node). From the head: before the first node it compares 0 with, else before the
// last it compares -1 with. From the tail: after the last node it compares 0 or 1 with, else at the head. Returns
// the node it went after.
// AUTOINJECT
LLNODE_tag* LList_InsertAt(LLISTINFO_tag *list, LLNODE_tag *element, char fromHead, CompareFunc compare) {

    if (list == NULL || element == NULL || compare == NULL)
        return NULL;

    LLNODE_tag *after = NULL;

    if (fromHead) {
        LLNODE_tag *before = NULL;
        for (LLNODE_tag *node = list->head; node != NULL; node = node->next) {
            int order = compare(&element, &node);
            if (order == 0) {
                before = node;
                break;
            }
            if (order == -1)
                before = node;
        }
        // With neither (an empty list too) the original reads through NULL
        after = before->prev;
    } else {
        for (LLNODE_tag *node = list->tail; node != NULL; node = node->prev) {
            int order = compare(&element, &node);
            if (order == 0 || order == 1) {
                after = node;
                break;
            }
        }
    }

    LList_Insert(list, after, element);
    return after;
}
