#ifndef DRIVING_ANIM_EVENTS_H_
#define DRIVING_ANIM_EVENTS_H_

// ---------------------------------------------------------------------------------------------------------------
// ActEvents (0x94, "ActEvents", ActManager's): the handler table the anims' event channels fire through, an entry
// per event id of the resolver's EventTarget. Every entry is the default handler (which does nothing) but the
// character events' four, whose handlers act on the current actor. Also the handlers and ActEventResolver (the
// EventTarget the actors' event names are resolved by). See Events.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../eagl/anim/EventTarget.h"   // EventHandler

class ActActor;
struct RawEvent;

// ActEvents' handlers (8 bytes, operator new; their vtables are one slot each). Slot 0 gets the time, the anim's
// event and the evaluation's data (eagl/anim/AnimChannels.cpp). The names are ours but the first's (the PS2
// build's DefaultEventHandler).
struct DefaultEventHandler : EventHandler {     // vtable 0x0018a050
    // Nothing; the same code serves every empty three-argument virtual of the game
    void Handle(float time, RawEvent *event, void *data);                       // 0x00015770
};
struct FireEventHandler : EventHandler {        // vtable 0x0018a054, "Character_WeaponEvents_Fire_fireValue"
    void Handle(float time, RawEvent *event, void *data);                       // 0x00015780
};
struct DropWeaponEventHandler : EventHandler {  // vtable 0x0018a058, "..._DropWeapon_..." and "..._ThrowWeapon_..."
    void Handle(float time, RawEvent *event, void *data);                       // 0x000157b0
};
struct PhysicsOffEventHandler : EventHandler {  // vtable 0x0018a05c, "Character_PhysicsOffEvents_..."
    void Handle(float time, RawEvent *event, void *data);                       // 0x000157d0
};

class ActEventResolver {                // 4
public:
    EventTarget *target;

    ActEventResolver* Construct();                                              // 0x00015710
    void Destruct();                                                            // 0x00015750
};
static_assert(sizeof(ActEventResolver) == 4, "an ActEventResolver is 4 bytes");

class ActEvents {                       // 0x94
public:
    enum { kMaxEvents = 32 };

    EventHandler *handlers[kMaxEvents]; // +0x00 by event id
    DefaultEventHandler *defaultHandler;    // +0x80
    DropWeaponEventHandler *dropWeapon; // +0x84
    PhysicsOffEventHandler *physicsOff; // +0x88
    FireEventHandler *fire;             // +0x8c
    ActActor *currentActor;             // +0x90

    ActEvents* Construct(ActEventResolver *resolver);                           // 0x000157e0
    // The physics-off handler is not deleted
    void Destruct();                                                            // 0x000156c0
    void SetCurrentActor(ActActor *actor);                                      // 0x000156f0
};
static_assert(offsetof(ActEvents, defaultHandler) == 0x80 && offsetof(ActEvents, currentActor) == 0x90,
              "ActEvents layout");
static_assert(sizeof(ActEvents) == 0x94, "an ActEvents is 0x94 bytes");

#endif // DRIVING_ANIM_EVENTS_H_
