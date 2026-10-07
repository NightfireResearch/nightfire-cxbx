#ifndef DRIVING_WORLD_TRIGGER_H_
#define DRIVING_WORLD_TRIGGER_H_

#include <stddef.h>
#include <stdint.h>

#include "CollisionInstance.h"   // WTrigger
#include "CollisionTypes.h"
#include "../data/Carp.h"         // CARP::Instance

// ---------------------------------------------------------------------------------------------------------------
// The triggers' own methods (WTrigger, CollisionInstance.h): running their events, the direction test, following a
// moving object. See Trigger.cpp; WTriggerManager (TriggerManager.h) finds what touches them.
// ---------------------------------------------------------------------------------------------------------------

// The event numbers the triggers' code looks for (RegisterEvent's table: EStreamEvent, EStartMission)
enum TriggerEventType : int32_t {
    kEventStream = 0x14,
    kEventStartMission = 0x16,
};

// One event of a trigger (or of a mission rule): its number and where its data is.
struct TriggerEvent {
    int32_t type;               // +0x00 RegisterEvent's event number
    uint32_t unknown04;
    int32_t dataOffset;         // +0x08 from this record
    uint32_t unknown0c;

    uint8_t *Data() { return reinterpret_cast<uint8_t *>(this) + dataOffset; }
};
static_assert(sizeof(TriggerEvent) == 0x10, "a trigger's event is 16 bytes");

// A trigger's events: the count, then the events.
struct TriggerEvents {
    int32_t count;              // +0x00
    uint8_t unknown04[0xc];
    // +0x10 TriggerEvent events[count]

    TriggerEvent *Events() { return reinterpret_cast<TriggerEvent *>(this + 1); }

    bool Has(int32_t type) {
        TriggerEvent *event = Events();
        for (int i = 0; i < count; i++, event++)
            if (event->type == type)
                return true;
        return false;
    }
};
static_assert(sizeof(TriggerEvents) == 0x10, "the events' head is 16 bytes");

// The data of an EStreamEvent, as far as WTriggerManager::Restart reads it.
struct StreamEventData {
    uint32_t unknown00;
    const char *type;           // +0x04 "music", "speech", "nis" (Restart checks those three)
    const char *name;           // +0x08 the file's, without ".asf"
    uint8_t unknown0c[0xc];
    int32_t preBuffer;          // +0x18
};

// A ray shell queued for the trigger test (RayShell's table at 0x001e8570, 0x30 bytes each; RayShell::
// GetActiveRayShell answers one).
struct ActiveRayShell {
    uint32_t ownerSignature;    // +0x00 Simulation::FindPhysicsObjectSignature's argument
    uint8_t unknown04[0xc];
    Coord3 start;               // +0x10
    float unknown1c;            // FireEvents passes it on, at least 50
    Coord3 end;                 // +0x20
    float radius;               // +0x2c added to the triggers' radius
};
static_assert(sizeof(ActiveRayShell) == 0x30, "an active ray shell is 48 bytes");

// What an event's code may read about what raised it (0x64 bytes at 0x001e47e8; RSceneObj::SetEventDynamicData,
// SMissionRule::RunEvents and FireEvents fill it). The name is ours, after SetEventDynamicData.
struct EventDynamicData {
    CARP::Instance *instance;   // +0x00
    uint8_t unknown04[8];
    int32_t unknown0c;          // +0x0c FireEvents: -1
    Coord4 position;            // +0x10 the trigger's, w 1
    uint8_t unknown20[0x10];
    WTrigger *trigger;          // +0x30
    uint32_t unknown34;
    uint8_t flag;               // +0x38 FireEvents' first argument
    uint8_t unknown39[3];
    int32_t index;              // +0x3c its second
    uint32_t unknown40;
    uint8_t hasRayShell;        // +0x44 a ray shell is hitting the trigger
    uint8_t unknown45[3];
    Coord3 rayStart;            // +0x48 its start
    Coord3 rayEnd;              // +0x54 its end
    float rayValue;             // +0x60 its unknown1c, at least 50
};
static_assert(sizeof(EventDynamicData) == 0x64, "the event data is 0x64 bytes");
static_assert(offsetof(EventDynamicData, trigger) == 0x30 && offsetof(EventDynamicData, hasRayShell) == 0x44,
              "event data layout");

#define gEventDynamicData (*(EventDynamicData *)0x001e47e8)

#endif // DRIVING_WORLD_TRIGGER_H_
