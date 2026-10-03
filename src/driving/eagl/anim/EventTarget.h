#ifndef DRIVING_EAGL_ANIM_EVENTTARGET_H_
#define DRIVING_EAGL_ANIM_EVENTTARGET_H_

// EAGLAnim::EventTarget: event names ("event.<name>" in the anim data) to ids, sorted for a binary search, and the
// per-id handler lists the game hangs off a table. See EventTarget.cpp.

#include <stdint.h>

struct EventMapping {                // 8
    char *name;                      // +0x00
    int32_t id;                      // +0x04
};
static_assert(sizeof(EventMapping) == 8, "an event mapping is 8 bytes");

// What the handler table holds per event id: a list of objects called through their game vtable's slot 0 (see
// RawEventData::Eval, AnimChannels.cpp).
struct EventHandler {
    const void *vtable;              // +0x00
    EventHandler *next;              // +0x04
};

class EventTarget {                  // 0x18
public:
    uint16_t unknown00;
    uint16_t count;                  // +0x02
    uint32_t unknown04;
    EventMapping *mapping;           // +0x08 sorted by name
    char **names;                    // +0x0c by id
    int32_t capacity;                // +0x10
    int32_t grow;                    // +0x14

    void Destruct();                                             // 0x000f8310
    int Find(const char *name, int *position);                   // 0x000f8390
    bool ResolveEventId(const char *name, int *id);              // 0x000f8430
    int GetEventId(const char *name);                            // 0x000f8670
    bool AddHandler(EventHandler **table, const char *name, EventHandler *handler);      // 0x000f8690 (invented)
    bool RemoveHandler(EventHandler **table, const char *name, EventHandler *handler);   // 0x000f86d0 (invented)
    bool ClearHandlers(EventHandler **table, const char *name);                          // 0x000f8740 (invented)
};
static_assert(sizeof(EventTarget) == 0x18, "an EventTarget is 0x18 bytes");

#endif // DRIVING_EAGL_ANIM_EVENTTARGET_H_
