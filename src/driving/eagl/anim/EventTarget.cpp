#include "EventTarget.h"
#include "../EaglGlobals.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGLAnim::EventTarget (docs/driving/eagl.md 4.10): the anim data names its events "event.<name>"; ResolveEventId
// gives each name an id, kept in a name-sorted mapping (binary searched) and an id-ordered name array that grow by
// 'grow' entries when full. A handler table indexed by id holds singly linked handler lists. Each
// function is the original at the same address, allocations through EAGL's hooks with the original's names.
// ---------------------------------------------------------------------------------------------------------------


// The allocations' names: the original's strings (two copies of "eventName")
#define AllocEventName ((const char *)0x001ceafc)     // "eventName"
#define AllocEventMapping ((const char *)0x001ceb08)  // "eventMapping"
#define AllocEventNames ((const char *)0x001ceb18)    // "eventNames"
#define AllocEventName2 ((const char *)0x001ceb24)    // "eventName"

// FUNC_AT(0x000f8310)
void EventTarget::Destruct() {
    if (mapping != NULL) {
        for (int i = 0; i < count; i++)
            if (mapping[i].name != NULL)
                EaglFree(mapping[i].name, strlen(mapping[i].name) + 1);
        EaglFree(mapping, capacity * sizeof(EventMapping));
    }
    if (names != NULL)
        EaglFree(names, capacity * sizeof(char *));
}

// The id, or -1 with *position where the name would go.
// FUNC_AT(0x000f8390)
int EventTarget::Find(const char *name, int *position) {
    int hi = count - 1;
    *position = 0;
    while (*position <= hi) {
        int mid = (*position + hi) >> 1;
        int c = strcmp(name, mapping[mid].name);
        if (c > 0)
            *position = mid + 1;
        else if (c < 0)
            hi = mid - 1;
        else
            return mapping[mid].id;
    }
    return -1;
}

// "event.<name>" to an id, a new one if the name is new.
// FUNC_AT(0x000f8430)
bool EventTarget::ResolveEventId(const char *name, int *id) {
    if (name == NULL || strstr(name, "event.") != name)
        return false;
    name += 6;
    int position;
    int found = Find(name, &position);
    *id = found;
    if (found >= 0)
        return true;
    int n = count;
    int oldCapacity = capacity;
    size_t length = strlen(name) + 1;
    if (n + 1 < oldCapacity) {
        for (int c = n; c >= position; c--)
            if (c > 0)
                mapping[c] = mapping[c - 1];
        mapping[position].name = static_cast<char *>(EaglMalloc(length, AllocEventName));
        memcpy(mapping[position].name, name, length);
        names[count] = mapping[position].name;
        mapping[position].id = count;
    } else {
        EventMapping *oldMapping = mapping;
        char **oldNames = names;
        capacity = oldCapacity + grow;
        mapping = static_cast<EventMapping *>(EaglMalloc(capacity * sizeof(EventMapping), AllocEventMapping));
        names = static_cast<char **>(EaglMalloc(capacity * sizeof(char *), AllocEventNames));
        for (int c = count; c >= position; c--)
            if (c > 0)
                mapping[c] = oldMapping[c - 1];
        for (int c = position - 1; c >= 0; c--)
            mapping[c] = oldMapping[c];
        mapping[position].name = static_cast<char *>(EaglMalloc(length, AllocEventName2));
        memcpy(mapping[position].name, name, length);
        names[count] = mapping[position].name;
        memcpy(names, oldNames, count * sizeof(char *));
        mapping[position].id = count;
        if (oldMapping != NULL)
            EaglFree(oldMapping, oldCapacity * sizeof(EventMapping));
        if (oldNames != NULL)
            EaglFree(oldNames, oldCapacity * sizeof(char *));
    }
    count++;
    *id = count - 1;
    return true;
}

// FUNC_AT(0x000f8670)
int EventTarget::GetEventId(const char *name) {
    int position;
    return Find(name, &position);
}

// FUNC_AT(0x000f8690)
bool EventTarget::AddHandler(EventHandler **table, const char *name, EventHandler *handler) {
    int position;
    int id = Find(name, &position);
    if (id < 0)
        return false;
    EventHandler *head = table[id];
    if (head != NULL)
        handler->next = head;
    table[id] = handler;
    return true;
}

// FUNC_AT(0x000f86d0)
bool EventTarget::RemoveHandler(EventHandler **table, const char *name, EventHandler *handler) {
    int position;
    int id = Find(name, &position);
    if (id < 0)
        return false;
    EventHandler *h = table[id];
    if (h == NULL)
        return true;
    if (h == handler) {
        table[id] = handler->next;
        return true;
    }
    while (h->next != NULL && h->next != handler)
        h = h->next;
    if (h->next == handler)
        h->next = handler->next;
    return true;
}

// FUNC_AT(0x000f8740)
bool EventTarget::ClearHandlers(EventHandler **table, const char *name) {
    int position;
    int id = Find(name, &position);
    if (id < 0)
        return false;
    EventHandler *h = table[id];
    while (h != NULL) {
        EventHandler *next = h->next;
        h->next = NULL;
        h = next;
    }
    table[id] = NULL;
    return true;
}
