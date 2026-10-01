#include "drivinghelpers.h"
#include "EventManager.hpp"
#include "engine/UMemory.hpp"

// The event manager's state, all at file scope in the original (Ghidra's names). The two points are addresses in
// the buffer: events live from the deletion point, which is the next to run, up to the creation point.
#define gMemoryBuffer (*(char **)0x001e47d4)
#define gCreationPoint (*(char **)0x001e47d8)
#define gDeletionPoint (*(char **)0x001e47dc)
#define fgCurrentEvent (*(Event **)0x001e47e0)   // the event running; written, never read by the game

static const unsigned int kBufferSize = 0x8000;

// Events are laid out on 16-byte boundaries.
static size_t EventBytes(size_t size) {
    return (size + 0xf) & ~(size_t)0xf;
}

// AUTOINJECT
void EventManager::Init() {
    gMemoryBuffer = (char *)UMemory::Alloc(kBufferSize, 0, "EventBuffer");
    gCreationPoint = gMemoryBuffer;
    gDeletionPoint = gMemoryBuffer;
}

// AUTOINJECT
void EventManager::Shutdown() {
    UMemory::Free(gMemoryBuffer);
    gMemoryBuffer = nullptr;
    gCreationPoint = nullptr;
    gDeletionPoint = nullptr;
}

// Runs every event in the queue, oldest first, then empties it. An event that raises another while it runs (in
// its destructor) puts it at the creation point, so the new one runs in this same call - the loop re-reads the
// creation point each time round.
//
// As in the original, nothing checks the buffer's size: a tick that raised more than 32 KB of events would write
// past it. None is known to.
// AUTOINJECT
void EventManager::RunEvents() {
    Event *event = (Event *)gDeletionPoint;
    if (gDeletionPoint < gCreationPoint) {
        do {
            if (event != nullptr) {
                fgCurrentEvent = event;
                // Runs the event, and its operator delete moves the deletion point past it.
                event->DeletingDestructor(1);
                event = (Event *)gDeletionPoint;
            }
            fgCurrentEvent = nullptr;
        } while ((char *)event < gCreationPoint);
    }
    gCreationPoint = gMemoryBuffer;
    gDeletionPoint = gMemoryBuffer;
}

// AUTOINJECT
void *Event::operator new(size_t size) {
    char *event = gCreationPoint;
    gCreationPoint += EventBytes(size);
    return event;
}

// Called by every event's destructor with the event's own size, which is how the deletion point knows how far to
// move. The original ignores the pointer too: it assumes the event being deleted is the one at the deletion
// point, which holds while events are only ever destroyed by RunEvents, in order.
// AUTOINJECT
void Event::operator delete(void *event, size_t size) {
    (void)event;
    gDeletionPoint += EventBytes(size);
}
