#include "LList.h"

// for NULL
#include <stddef.h>

#include <string.h>
#include "../memory.h"


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
        if(list->tail == NULL) {
            list->head = NULL;
            list->count = 0;
            return value;
        } else {
            list->tail->next = NULL;
            list->count--;
        }
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

// AUTOGEN
LLNODE_tag* LList_Remove(LLISTINFO_tag *list, LLNODE_tag *element);
