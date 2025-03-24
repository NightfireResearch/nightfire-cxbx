#include "LList.h"

// for NULL
#include <stddef.h>


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