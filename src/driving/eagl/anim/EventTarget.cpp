#include "EventTarget.h"

#include <string.h>

// ---------------------------------------------------------------------------------------------------------------
// EAGLAnim::EventTarget (docs/driving/eagl.md 4.10): the anim data names its events "event.<name>"; ResolveEventId
// gives each name an id, kept in a name-sorted mapping (binary searched) and an id-ordered name array that grow by
// 'grow' entries when full. A handler table indexed by id holds singly linked handler lists (next at +4). Each
// function is the original at the same address, allocations through EAGL's hooks with the original's names.
// ---------------------------------------------------------------------------------------------------------------

#define EaglMalloc   (*(void *(**)(uint32_t size, const char *name))0x001caf68u)
#define EaglFree     (*(void (**)(void *data, uint32_t size))0x001caf6cu)

// FUNC_AT(0x000f8310)
void EventTarget::Destruct() {
    if (mapping != NULL) {
        for (int i = 0; i < count; i++)
            if (mapping[i].name != NULL)
                EaglFree(mapping[i].name, (uint32_t)strlen(mapping[i].name) + 1);
        EaglFree(mapping, (uint32_t)capacity * 8);
    }
    if (names != NULL)
        EaglFree(names, (uint32_t)capacity * 4);
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
        mapping[position].name = (char *)EaglMalloc((uint32_t)length, (const char *)0x001ceafcu);   // "eventName"
        memcpy(mapping[position].name, name, length);
        names[count] = mapping[position].name;
        mapping[position].id = count;
    } else {
        EventMapping *oldMapping = mapping;
        char **oldNames = names;
        capacity = oldCapacity + grow;
        mapping = (EventMapping *)EaglMalloc((uint32_t)capacity * 8, (const char *)0x001ceb08u);   // "eventMapping"
        names = (char **)EaglMalloc((uint32_t)capacity * 4, (const char *)0x001ceb18u);           // "eventNames"
        for (int c = count; c >= position; c--)
            if (c > 0)
                mapping[c] = oldMapping[c - 1];
        for (int c = position - 1; c >= 0; c--)
            mapping[c] = oldMapping[c];
        mapping[position].name = (char *)EaglMalloc((uint32_t)length, (const char *)0x001ceb24u);   // "eventName"
        memcpy(mapping[position].name, name, length);
        names[count] = mapping[position].name;
        memcpy(names, oldNames, (size_t)count * 4);
        mapping[position].id = count;
        if (oldMapping != NULL)
            EaglFree(oldMapping, (uint32_t)oldCapacity * 8);
        if (oldNames != NULL)
            EaglFree(oldNames, (uint32_t)oldCapacity * 4);
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
bool EventTarget::AddHandler(void **table, const char *name, void *handler) {
    int position;
    int id = Find(name, &position);
    if (id < 0)
        return false;
    void *head = table[id];
    if (head != NULL)
        ((void **)handler)[1] = head;
    table[id] = handler;
    return true;
}

// FUNC_AT(0x000f86d0)
bool EventTarget::RemoveHandler(void **table, const char *name, void *handler) {
    int position;
    int id = Find(name, &position);
    if (id < 0)
        return false;
    void **h = (void **)table[id];
    if (h == NULL)
        return true;
    if (h == handler) {
        table[id] = ((void **)handler)[1];
        return true;
    }
    while (h[1] != NULL && h[1] != handler)
        h = (void **)h[1];
    if (h[1] == handler)
        h[1] = ((void **)handler)[1];
    return true;
}

// FUNC_AT(0x000f8740)
bool EventTarget::ClearHandlers(void **table, const char *name) {
    int position;
    int id = Find(name, &position);
    if (id < 0)
        return false;
    void **h = (void **)table[id];
    while (h != NULL) {
        void **next = (void **)h[1];
        h[1] = NULL;
        h = next;
    }
    table[id] = NULL;
    return true;
}
