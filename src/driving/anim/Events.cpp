#include "Events.h"

#include "Actor.h"
#include "../eagl/anim/AnimChannels.h"  // RawEvent
#include "../engine/UMemory.hpp"

#include <bit>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------------------------------------------
// ActEvents, its handlers and ActEventResolver (0x000156c0-0x00015900). See Events.h.
// ---------------------------------------------------------------------------------------------------------------

#define DefaultEventHandlerVtable ((const void *)0x0018a050)
#define FireEventHandlerVtable ((const void *)0x0018a054)
#define DropWeaponEventHandlerVtable ((const void *)0x0018a058)
#define PhysicsOffEventHandlerVtable ((const void *)0x0018a05c)

#define CurrentActor (*(ActActor **)0x001dd9c4)         // SetCurrentActor's

namespace {

template <class Handler>
Handler* NewHandler(const void *vtable) {
    Handler *handler = static_cast<Handler *>(OperatorNew(sizeof(Handler)));
    if (handler != NULL) {
        handler->next = NULL;
        handler->vtable = vtable;
    }
    return handler;
}

} // namespace

// FUNC_AT(0x000157e0)
ActEvents* ActEvents::Construct(ActEventResolver *resolver) {
    defaultHandler = NewHandler<DefaultEventHandler>(DefaultEventHandlerVtable);
    for (int i = 0; i < resolver->target->count; i++)
        handlers[i] = defaultHandler;
    fire = NewHandler<FireEventHandler>(FireEventHandlerVtable);
    dropWeapon = NewHandler<DropWeaponEventHandler>(DropWeaponEventHandlerVtable);
    physicsOff = NewHandler<PhysicsOffEventHandler>(PhysicsOffEventHandlerVtable);

    int id = resolver->target->GetEventId("Character_WeaponEvents_Fire_fireValue");
    if (id != -1)
        handlers[id] = fire;
    id = resolver->target->GetEventId("Character_GotShotEvents_DropWeapon_dropWeaponValue");
    if (id != -1)
        handlers[id] = dropWeapon;
    id = resolver->target->GetEventId("Character_GotShotEvents_ThrowWeapon_throwWeaponValue");
    if (id != -1)
        handlers[id] = dropWeapon;
    id = resolver->target->GetEventId("Character_PhysicsOffEvents_PhysicsOff_physicsOffValue");
    if (id != -1)
        handlers[id] = physicsOff;
    currentActor = NULL;
    return this;
}

// FUNC_AT(0x00015770)
void DefaultEventHandler::Handle(float time, RawEvent *event, void *data) {
}

// The event's words are passed on as they are.
// FUNC_AT(0x00015780)
void FireEventHandler::Handle(float time, RawEvent *event, void *data) {
    CurrentActor->Fire(time, event->time, std::bit_cast<float>(event->data[0]), std::bit_cast<float>(event->data[1]));
}

// FUNC_AT(0x000157b0)
void DropWeaponEventHandler::Handle(float time, RawEvent *event, void *data) {
    CurrentActor->DropWeapon(std::bit_cast<float>(event->data[1]));
}

// FUNC_AT(0x000157d0)
void PhysicsOffEventHandler::Handle(float time, RawEvent *event, void *data) {
    CurrentActor->followGround = false;
}

// FUNC_AT(0x000156c0)
void ActEvents::Destruct() {
    OperatorDelete(dropWeapon);
    OperatorDelete(fire);
    OperatorDelete(defaultHandler);
}

// FUNC_AT(0x000156f0)
void ActEvents::SetCurrentActor(ActActor *actor) {
    currentActor = actor;
    CurrentActor = actor;
}

// FUNC_AT(0x00015710)
ActEventResolver* ActEventResolver::Construct() {
    EventTarget *made = static_cast<EventTarget *>(OperatorNew(sizeof(EventTarget)));
    if (made != NULL) {
        made->count = 0;
        made->mapping = NULL;
        made->names = NULL;
        made->capacity = 0;
        made->grow = 50;
    }
    target = made;
    return this;
}

// FUNC_AT(0x00015750)
void ActEventResolver::Destruct() {
    EventTarget *events = target;
    if (events != NULL) {
        events->Destruct();
        OperatorDelete(events);
    }
}
