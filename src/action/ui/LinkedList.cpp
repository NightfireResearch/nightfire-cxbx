#include "../actionhelpers.h"
#include "LinkedList.h"

// A link's words: the next element's link, then the previous one's
#define NEXT(link) (((void **)(link))[0])
#define PREV(link) (((void **)(link))[1])

// AUTOINJECT
void LinkedList_Init(LINKEDLIST *list, uint offset) {
    if (list == NULL)
        return;
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->offset = offset;
}

// Empties the list, keeping its offset
// AUTOINJECT
void LinkedList_ReInit(LINKEDLIST *list) {
    if (list == NULL)
        return;
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
}

// Puts an element at the tail: its link, and the address of its link's previous word
// AUTOINJECT
bool LinkedList_Add(LINKEDLIST *list, void *prevLink, void *link) {
    if (list == NULL)
        return false;
    if (prevLink == NULL || link == NULL)
        return false;
    if (list->tail != NULL)
        NEXT(list->tail) = link;
    NEXT(link) = NULL;
    *(void **)prevLink = list->tail;
    if (list->count == 0)
        list->head = link;
    list->tail = link;
    list->count++;
    return true;
}

// Puts an element after another's link (NULL: at the head)
// AUTOINJECT
bool LinkedList_Insert(LINKEDLIST *list, void *after, void *prevLink, void *link) {
    if (list == NULL)
        return false;
    if (prevLink == NULL || link == NULL)
        return false;
    if (after == NULL) {
        if (list->head != NULL)
            PREV(list->head) = link;
        NEXT(link) = list->head;
        *(void **)prevLink = NULL;
        list->head = link;
    } else {
        void *next = NEXT(after);
        if (next != NULL)
            PREV(next) = link;
        NEXT(after) = link;
        NEXT(link) = next;
        *(void **)prevLink = after;
    }
    if (NEXT(link) == NULL)
        list->tail = link;
    list->count++;
    return true;
}

// AUTOINJECT
bool LinkedList_Remove(LINKEDLIST *list, void *prevLink, void *link) {
    if (list == NULL || prevLink == NULL || link == NULL)
        return false;
    void *prev = *(void **)prevLink;
    void *next = NEXT(link);
    if (prev != NULL)
        NEXT(prev) = next;
    else
        list->head = next;
    if (next != NULL)
        PREV(NEXT(link)) = prev;
    else
        list->tail = prev;
    if (--list->count == 0) {
        list->head = NULL;
        list->tail = NULL;
    }
    return true;
}

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
