#ifndef UI_LINKEDLIST_H
#define UI_LINKEDLIST_H

#include <stddef.h>

// The menus' linked lists (LinkedList_*; not the engine's LList). The head, the tail and each element's links point at
// an element's link, 'offset' words into it: the link's first word is the next element's link, its second the
// previous one's.
#pragma pack(push, 1)
typedef struct LINKEDLIST {
    void *head;     // 0x00
    void *tail;     // 0x04
    unsigned int count;  // 0x08
    unsigned int offset; // 0x0c of the link in its element, in words
} LINKEDLIST;
#pragma pack(pop)
static_assert(sizeof(LINKEDLIST) == 0x10, "LINKEDLIST is 0x10 bytes");

inline void *LinkedList_First(LINKEDLIST *list) {
    return list->head != NULL ? (char *)list->head - list->offset * 4 : NULL;
}

inline void *LinkedList_Last(LINKEDLIST *list) {
    return list->tail != NULL ? (char *)list->tail - list->offset * 4 : NULL;
}

// An element's link
inline void *LinkedList_Link(LINKEDLIST *list, void *element) {
    return (char *)element + list->offset * 4;
}

void LinkedList_Init(LINKEDLIST *list, unsigned int offset);
void LinkedList_ReInit(LINKEDLIST *list);
bool LinkedList_Add(LINKEDLIST *list, void *prevLink, void *link);
bool LinkedList_Insert(LINKEDLIST *list, void *after, void *prevLink, void *link);
bool LinkedList_Remove(LINKEDLIST *list, void *prevLink, void *link);
void* LinkedList_GetIndex(LINKEDLIST *list, unsigned int index);

#endif // UI_LINKEDLIST_H
