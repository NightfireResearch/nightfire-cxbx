#ifndef DRIVING_GAME_EVENTSA_H_
#define DRIVING_GAME_EVENTSA_H_

// ---------------------------------------------------------------------------------------------------------------
// The mission events from E007Logo to EEndMission (game.events, package A): the AI orders, the camera's, the
// player's controls, the mission's counters and messages, the per-tick update events and the collision event.
//
// An event is a small object in the event buffer (EventManager.hpp): the scripts' records (a trigger's event list,
// an animation frame's) are turned into events by each class's MakeEvent callback, which the registry finds by the
// event's number (RegisterEvent::LookupEvent) and hands the record; the record's references are resolved when the
// track loads, by the class's ResolveEvent callback (RegisterEvent::ResolveEvent). An event does its work when it
// is destroyed: the event manager runs every event through its scalar deleting destructor, the vtable's one slot,
// and the destructor calls the class's Process and puts back Event's vtable. A few events do their work in the
// constructor instead (EChangeMaxTraffic, EChangeStage), or none (their destructor is Event's).
//
// Every vtable here has that one slot: slot 0, the scalar deleting destructor (Delete). See EventsA.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../EventManager.hpp"          // Event
#include "../data/Carp.h"               // CARP::AIElement
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../physics/RigidBodyResolve.h"    // CollisionImpact
#include "AIVehicle.h"
#include "../platform/Ptr32.h"
#include "../../common/xbeClass.h"          // LAYOUT_32

namespace CARP {
class AISpline;
class Instance;
}
class ActWeapon;
class RSceneObj;
class UGroup;
class WTrigger;

// ---- the per-tick events (the scheduler's tasks raise them)

class EAIUpdate : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003e2b0
    EAIUpdate* Delete(unsigned flags);                                                  // 0x00047470
};
LAYOUT_32(sizeof(EAIUpdate) == 4, "EAIUpdate is 4 bytes");

class EAnimUpdate : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003e620
    EAnimUpdate* Delete(unsigned flags);                                                // 0x00047510
};
LAYOUT_32(sizeof(EAnimUpdate) == 4, "EAnimUpdate is 4 bytes");

class EAudioUpdate : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x00049790
    EAudioUpdate* Delete(unsigned flags);                                               // 0x0004eaa0
};
LAYOUT_32(sizeof(EAudioUpdate) == 4, "EAudioUpdate is 4 bytes");

class ECameraUpdate : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003ed70
    ECameraUpdate* Delete(unsigned flags);                                              // 0x00047650
};
LAYOUT_32(sizeof(ECameraUpdate) == 4, "ECameraUpdate is 4 bytes");

// ---- the AI

// Orders an element's vehicle or character, or the player's AI vehicle. With a trigger, only when the trigger's
// toucher is the element's vehicle (or the player's car).
class EAICommand : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<AICommand> command;
        Ptr32<CARP::AIElement> element;
        Ptr32<CARP::AIElement> target;
        Ptr32<AIVehicle> vehicle;
    };

    AICommand *command;   // +0x04
    CARP::AIElement *element;   // +0x08 the element whose vehicle must be the toucher; NULL: the player's
    CARP::AIElement *target;    // +0x0c the element ordered, when `vehicle` is NULL; NULL: the player's AI
    AIVehicle *vehicle;   // +0x10 the vehicle ordered
    // The event data's: the trigger, whether a simple body touched it, and that body's slot (names ours)
    WTrigger *trigger;                  // +0x14
    uint8_t bySimpleBody;               // +0x18 (the AUF build's bool; copied as a byte)
    int32_t bodySlot;                   // +0x1c

    // AIElementController::WakeUp's
    EAICommand* Construct(AICommand *command, CARP::AIElement *element,
                          CARP::AIElement *target, AIVehicle *vehicle, WTrigger *trigger,
                          uint8_t bySimpleBody, int bodySlot);                          // 0x0003dcd0
    void Process();
    void Destruct();                                                                    // 0x00048ec0
    EAICommand* Delete(unsigned flags);                                                 // 0x0004ea60

private:
    void CommandGroundVehicle(AIGroundVehicle *vehicle);
};
LAYOUT_32(sizeof(EAICommand) == 0x20 && offsetof(EAICommand, trigger) == 0x14 &&
          offsetof(EAICommand, bodySlot) == 0x1c, "EAICommand layout");

class EAIElementFireOff : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
    };

    CARP::AIElement *element;   // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003dd70
    EAIElementFireOff* Delete(unsigned flags);                                          // 0x00047390
};
LAYOUT_32(sizeof(EAIElementFireOff) == 8, "EAIElementFireOff is 8 bytes");

class EAIElementFireOn : public Event {
public:
    typedef EAIElementFireOff::Record Record;

    CARP::AIElement *element;   // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003de30
    EAIElementFireOn* Delete(unsigned flags);                                           // 0x000473b0
};
LAYOUT_32(sizeof(EAIElementFireOn) == 8, "EAIElementFireOn is 8 bytes");

class EAIElementSetAccuracy : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        int32_t accuracy;
    };

    CARP::AIElement *element;   // +0x04
    int32_t accuracy;                   // +0x08

    void Process();
    void Destruct();                                                                    // 0x0003def0
    EAIElementSetAccuracy* Delete(unsigned flags);                                      // 0x000473d0
};
LAYOUT_32(sizeof(EAIElementSetAccuracy) == 0xc, "EAIElementSetAccuracy is 12 bytes");

class EAIElementSetCharParams : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        int32_t unknown08;
        int32_t accuracy;
        float unknown10;
    };

    CARP::AIElement *element;   // +0x04
    int32_t unknown08;                  // +0x08 the element's unknown64
    int32_t accuracy;                   // +0x0c
    float unknown10;                    // +0x10 the element's unknown6c and unknown4c

    void Process();
    void Destruct();                                                                    // 0x0003dfb0
    EAIElementSetCharParams* Delete(unsigned flags);                                    // 0x000473f0
};
LAYOUT_32(sizeof(EAIElementSetCharParams) == 0x14, "EAIElementSetCharParams is 20 bytes");

class EAIElementSetNonScoreable : public Event {
public:
    typedef EAIElementFireOff::Record Record;

    CARP::AIElement *element;   // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003e0a0
    EAIElementSetNonScoreable* Delete(unsigned flags);                                  // 0x00047410
};
LAYOUT_32(sizeof(EAIElementSetNonScoreable) == 8, "EAIElementSetNonScoreable is 8 bytes");

class EAIElementSetVehicleParams : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        float unknown08;
    };

    CARP::AIElement *element;   // +0x04
    float unknown08;                    // +0x08 the element's unknown4c

    void Process();
    void Destruct();                                                                    // 0x0003e180
    EAIElementSetVehicleParams* Delete(unsigned flags);                                 // 0x00047430
};
LAYOUT_32(sizeof(EAIElementSetVehicleParams) == 0xc, "EAIElementSetVehicleParams is 12 bytes");

class EAIElementSetWakeRange : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        float wakeRange;
    };

    CARP::AIElement *element;   // +0x04
    float wakeRange;                    // +0x08

    void Process();
    void Destruct();                                                                    // 0x0003e210
    EAIElementSetWakeRange* Delete(unsigned flags);                                     // 0x00047450
};
LAYOUT_32(sizeof(EAIElementSetWakeRange) == 0xc, "EAIElementSetWakeRange is 12 bytes");

// Wakes an element; with a trigger, only when the trigger's toucher is `toucherElement`'s vehicle (or the player's
// car without one).
class EActivateAIElement : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        Ptr32<CARP::AIElement> toucherElement;
    };

    CARP::AIElement *element;           // +0x04
    CARP::AIElement *toucherElement;    // +0x08
    WTrigger *trigger;                          // +0x0c the event data's, as EAICommand's
    uint8_t bySimpleBody;                       // +0x10
    int32_t bodySlot;                           // +0x14

    void Process();
    void Destruct();                                                                    // 0x000495d0
    EActivateAIElement* Delete(unsigned flags);                                         // 0x0004ea80
};
LAYOUT_32(sizeof(EActivateAIElement) == 0x18 && offsetof(EActivateAIElement, bodySlot) == 0x14,
          "EActivateAIElement layout");

class EDeactivateAIElement : public Event {
public:
    typedef EAIElementFireOff::Record Record;

    CARP::AIElement *element;   // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003f470
    EDeactivateAIElement* Delete(unsigned flags);                                       // 0x00047930
};
LAYOUT_32(sizeof(EDeactivateAIElement) == 8, "EDeactivateAIElement is 8 bytes");

// Sends a vehicle element's object two animation stimuli
class EDropMine : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        uint32_t unknown08;
    };

    CARP::AIElement *element;   // +0x04
    uint32_t unknown08;                 // +0x08 a reference, not read

    void Process();
    void Destruct();                                                                    // 0x0003f720
    EDropMine* Delete(unsigned flags);                                                  // 0x000479d0
};
LAYOUT_32(sizeof(EDropMine) == 0xc, "EDropMine is 12 bytes");

// A point a muzzle flash starts from (a CARP record; name ours): in the frame of one of the scene object's instances
struct MuzzleFlashPoint {
    Coord4 position;                    // +0x00
    uint8_t unknown10[5];
    uint8_t instance;                   // +0x15 the scene object's instance
};

class EActorMuzzleFlash : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const MuzzleFlashPoint> point;
    };

    const MuzzleFlashPoint *point;      // +0x04
    ActWeapon *weapon;                  // +0x08 the event data's
    RSceneObj *sceneObj;                // +0x0c the event data's

    void Process();
    void Destruct();                                                                    // 0x0003e410
    EActorMuzzleFlash* Delete(unsigned flags);                                          // 0x000474b0
};
LAYOUT_32(sizeof(EActorMuzzleFlash) == 0x10, "EActorMuzzleFlash is 16 bytes");

// ---- the player's car and its controls

class EAutoDrive : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t on;
    };

    int32_t on;                         // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003e730
    EAutoDrive* Delete(unsigned flags);                                                 // 0x00047530
};
LAYOUT_32(sizeof(EAutoDrive) == 8, "EAutoDrive is 8 bytes");

class EAutoDriveSpeedChange : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float speed;
    };

    float speed;                        // +0x04

    void Process();
    void Destruct();                                                                    // 0x00049a80
    EAutoDriveSpeedChange* Delete(unsigned flags);                                      // 0x0004eac0
};
LAYOUT_32(sizeof(EAutoDriveSpeedChange) == 8, "EAutoDriveSpeedChange is 8 bytes");

class EAutoDriveSteering : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t on;
    };

    int32_t on;                         // +0x04 set: the hands shown, no forward lock

    void Process();
    void Destruct();                                                                    // 0x00049ae0
    EAutoDriveSteering* Delete(unsigned flags);                                         // 0x0004eae0
};
LAYOUT_32(sizeof(EAutoDriveSteering) == 8, "EAutoDriveSteering is 8 bytes");

// Pushes the player's car along the scene object's y axis, and damages it
class EBashCarAlongObjectAxis : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float damage;
    };

    RSceneObj *sceneObj;                // +0x04 the event data's
    float damage;                       // +0x08

    void Process();
    void Destruct();                                                                    // 0x00049b90
    EBashCarAlongObjectAxis* Delete(unsigned flags);                                    // 0x0004eb00
};
LAYOUT_32(sizeof(EBashCarAlongObjectAxis) == 0xc, "EBashCarAlongObjectAxis is 12 bytes");

class EControlToCPU : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0004a820
    EControlToCPU* Delete(unsigned flags);                                              // 0x0004eb60
};
LAYOUT_32(sizeof(EControlToCPU) == 4, "EControlToCPU is 4 bytes");

class EControlToPlayer : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0004a890
    EControlToPlayer* Delete(unsigned flags);                                           // 0x0004eb80
};
LAYOUT_32(sizeof(EControlToPlayer) == 4, "EControlToPlayer is 4 bytes");

// Puts a vehicle element (or, without one, the player's car) on an AI spline
class EControlToSpline : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        Ptr32<CARP::AISpline> spline;
    };

    CARP::AIElement *element;   // +0x04
    CARP::AISpline *spline;             // +0x08

    void Process();
    void Destruct();                                                                    // 0x0004a920
    EControlToSpline* Delete(unsigned flags);                                           // 0x0004eba0
};
LAYOUT_32(sizeof(EControlToSpline) == 0xc, "EControlToSpline is 12 bytes");

class EEndControlToSpline : public Event {
public:
    typedef EAIElementFireOff::Record Record;

    CARP::AIElement *element;   // +0x04

    void Process();
    void Destruct();                                                                    // 0x0004abc0
    EEndControlToSpline* Delete(unsigned flags);                                        // 0x0004ec20
};
LAYOUT_32(sizeof(EEndControlToSpline) == 8, "EEndControlToSpline is 8 bytes");

class EEndCarStop : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0004ab60
    EEndCarStop* Delete(unsigned flags);                                                // 0x0004ec00
};
LAYOUT_32(sizeof(EEndCarStop) == 4, "EEndCarStop is 4 bytes");

class EDamagePlayer : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float damage;
        float unknown08;
    };

    float damage;                       // +0x04
    float unknown08;                    // +0x08 ApplyDamage's fourth argument

    void Process();
    void Destruct();                                                                    // 0x0004aad0
    EDamagePlayer* Delete(unsigned flags);                                              // 0x0004ebe0
};
LAYOUT_32(sizeof(EDamagePlayer) == 0xc, "EDamagePlayer is 12 bytes");

// Swaps the player's car for its secondary type and reloads the weapons (only for the transition "paris")
class ECustomTransition : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const char> transition;
    };

    const char *transition;             // +0x04

    void Process();
    void Destruct();                                                                    // 0x0004a9d0
    ECustomTransition* Delete(unsigned flags);                                          // 0x0004ebc0
};
LAYOUT_32(sizeof(ECustomTransition) == 8, "ECustomTransition is 8 bytes");

// The collision event: what a collision did, for the sounds, rumble, effects and AI of the bodies on both sides
class ECollision : public Event {
public:
    // Where the collision was, how fast it went and its normal, as Process keeps them on its stack for the two sides
    // (the corner moves the position; AIZoneController::RegisterZone reads the position's x, y and z)
    struct Zone {
        Coord4 position;
        Coord4 velocity;
        Coord4 unknown20;
        Coord4 normal;
    };

    CollisionImpact impact;             // +0x04 (the PS2 symbols' COLLISION_INFO)

    // The physics' (ECollision::ECollision(COLLISION_INFO), by value)
    ECollision* Construct(CollisionImpact impact);                                      // 0x0003f370
    void Process();
    void Destruct();                                                                    // 0x00049eb0
    ECollision* Delete(unsigned flags);                                                 // 0x0004eb40

private:
    // Each side's sound, rumble, effects and AI (the two differ in places), then the incidental effect near the
    // camera
    void ProcessSideA(Zone *zone, float strength);
    void ProcessSideB(Zone *zone, float strength);
    void TriggerIncidental(Zone *zone);
};
LAYOUT_32(sizeof(ECollision) == 0x58, "ECollision is 0x58 bytes");

// ---- the camera

class EAbortCinematic : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003e340
    EAbortCinematic* Delete(unsigned flags);                                            // 0x00047490
};
LAYOUT_32(sizeof(EAbortCinematic) == 4, "EAbortCinematic is 4 bytes");

// Locks the camera on to a point, an element's character or vehicle, a vehicle element's member or one of its
// object's wheels
class ECameraLockOn : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        Ptr32<const Coord4> point;
        int32_t wheel;
        int32_t member;
        float targetY;
        float targetZ;
        float targetX;
        int32_t param;
        int32_t quiet;
    };

    CARP::AIElement *element;   // +0x04
    const Coord4 *point;                // +0x08 the point locked on to, when not NULL
    int32_t wheel;                      // +0x0c from 1: the vehicle element's object's wheel
    int32_t member;                     // +0x10 from 1: the vehicle element's member
    float targetY;                      // +0x14 CameraLockOn's target: y, z, x (and w 0)
    float targetZ;                      // +0x18
    float targetX;                      // +0x1c
    int32_t param;                      // +0x20
    int32_t quiet;                      // +0x24

    void Process();
    void Destruct();                                                                    // 0x0003eb50
    ECameraLockOn* Delete(unsigned flags);                                              // 0x00047610
};
LAYOUT_32(sizeof(ECameraLockOn) == 0x28, "ECameraLockOn is 0x28 bytes");

class ECameraShake : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t duration;
        uint32_t range;
        int32_t keepOrigin;
    };

    int32_t duration;                   // +0x04
    uint32_t range;                     // +0x08
    int32_t keepOrigin;                 // +0x0c

    void Process();
    void Destruct();                                                                    // 0x0003ecc0
    ECameraShake* Delete(unsigned flags);                                               // 0x00047630
};
LAYOUT_32(sizeof(ECameraShake) == 0x10, "ECameraShake is 16 bytes");

class ECameraZoom : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t percent;
        uint32_t time;
    };

    uint32_t percent;                   // +0x04
    uint32_t time;                      // +0x08 in sixtieths of a second; made steps (at least 1)

    void Process();
    void Destruct();                                                                    // 0x0003ee00
    ECameraZoom* Delete(unsigned flags);                                                // 0x00047670
};
LAYOUT_32(sizeof(ECameraZoom) == 0xc, "ECameraZoom is 12 bytes");

class EChangeCarCameraView : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t id;
        uint32_t b;
        uint32_t delay;
        int32_t unused;
    };

    uint32_t id;                        // +0x04
    uint32_t b;                         // +0x08 in sixtieths of a second; made steps (at least 1)
    uint32_t delay;                     // +0x0c in sixtieths of a second; made steps
    int32_t unused;                     // +0x10

    void Process();
    void Destruct();                                                                    // 0x0003ef10
    EChangeCarCameraView* Delete(unsigned flags);                                       // 0x00047690
};
LAYOUT_32(sizeof(EChangeCarCameraView) == 0x14, "EChangeCarCameraView is 20 bytes");

class ECinematicCamera : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t cinematic;
        int32_t unknown08;
        int32_t delay;
        Ptr32<CARP::AIElement> anchor;
        Coord3 place;
    };

    int32_t cinematic;                  // +0x04 zero: back to the last selectable mode, anchored on the player
    int32_t unknown08;                  // +0x08 in sixtieths of a second; made steps
    int32_t delay;                      // +0x0c in sixtieths of a second; made steps
    CARP::AIElement *anchor;    // +0x10 a vehicle element whose object the camera anchors on
    Coord3 place;                       // +0x14

    void Process();
    void Destruct();                                                                    // 0x00049d40
    ECinematicCamera* Delete(unsigned flags);                                           // 0x0004eb20
};
LAYOUT_32(sizeof(ECinematicCamera) == 0x20, "ECinematicCamera is 32 bytes");

class EEndCameraAnim : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t b;
        uint32_t delay;
        int32_t c;
        uint32_t id;
    };

    uint32_t b;                         // +0x04 in sixtieths of a second; made steps (at least 1)
    uint32_t delay;                     // +0x08 in sixtieths of a second; made steps
    int32_t c;                          // +0x0c
    uint32_t id;                        // +0x10

    void Process();
    void Destruct();                                                                    // 0x0003f900
    EEndCameraAnim* Delete(unsigned flags);                                             // 0x00047a30
};
LAYOUT_32(sizeof(EEndCameraAnim) == 0x14, "EEndCameraAnim is 20 bytes");

// ---- the world's power-ups and triggers

class EDisablePowerUp : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::Instance> instance;
        Ptr32<WTrigger> trigger;
    };

    CARP::Instance *instance;           // +0x04
    WTrigger *trigger;                  // +0x08

    void Process();
    void Destruct();                                                                    // 0x0003f550
    EDisablePowerUp* Delete(unsigned flags);                                            // 0x00047950
};
LAYOUT_32(sizeof(EDisablePowerUp) == 0xc, "EDisablePowerUp is 12 bytes");

class EEnablePowerUp : public Event {
public:
    typedef EDisablePowerUp::Record Record;

    CARP::Instance *instance;           // +0x04
    WTrigger *trigger;                  // +0x08

    void Process();
    void Destruct();                                                                    // 0x0003f7f0
    EEnablePowerUp* Delete(unsigned flags);                                             // 0x000479f0
};
LAYOUT_32(sizeof(EEnablePowerUp) == 0xc, "EEnablePowerUp is 12 bytes");

class EDisableTrigger : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<WTrigger> trigger;
    };

    WTrigger *trigger;                  // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003f610
    EDisableTrigger* Delete(unsigned flags);                                            // 0x00047970
};
LAYOUT_32(sizeof(EDisableTrigger) == 8, "EDisableTrigger is 8 bytes");

class EEnableTrigger : public Event {
public:
    typedef EDisableTrigger::Record Record;

    WTrigger *trigger;                  // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003f8c0
    EEnableTrigger* Delete(unsigned flags);                                             // 0x00047a10
};
LAYOUT_32(sizeof(EEnableTrigger) == 8, "EEnableTrigger is 8 bytes");

// ---- the mission

class E007Logo : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003db80
    E007Logo* Delete(unsigned flags);                                                   // 0x000472c0
};
LAYOUT_32(sizeof(E007Logo) == 4, "E007Logo is 4 bytes");

// No work of its own (its destructor is Event's)
class EAddObjective : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t unknown04;
        int32_t unknown08;
        int32_t unknown0c;
        int32_t unknown10;
    };

    int32_t unknown04;                  // +0x04
    int32_t unknown08;                  // +0x08
    int32_t unknown0c;                  // +0x0c
    int32_t unknown10;                  // +0x10

    EAddObjective* Delete(unsigned flags);                                              // 0x000474d0
};
LAYOUT_32(sizeof(EAddObjective) == 0x14, "EAddObjective is 20 bytes");

class EAddScoreAction : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t points;
    };

    int32_t points;                     // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003e520
    EAddScoreAction* Delete(unsigned flags);                                            // 0x000474f0
};
LAYOUT_32(sizeof(EAddScoreAction) == 8, "EAddScoreAction is 8 bytes");

// No work of its own (its destructor is Event's; its scalar deleting destructor is EBeginDownloadCode's)
class EAddScoreObjective : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t unknown04;
    };

    int32_t unknown04;                  // +0x04
};
LAYOUT_32(sizeof(EAddScoreObjective) == 8, "EAddScoreObjective is 8 bytes");

class EAwardHitToPlayer : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t unknown04;
    };

    int32_t unknown04;                  // +0x04 not read

    void Process();
    void Destruct();                                                                    // 0x0003e800
    EAwardHitToPlayer* Delete(unsigned flags);                                          // 0x00047550
};
LAYOUT_32(sizeof(EAwardHitToPlayer) == 8, "EAwardHitToPlayer is 8 bytes");

class EAwardKillToPlayer : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t kills;
    };

    int32_t kills;                      // +0x04 zero counts as one

    void Process();
    void Destruct();                                                                    // 0x0003e870
    EAwardKillToPlayer* Delete(unsigned flags);                                         // 0x00047570
};
LAYOUT_32(sizeof(EAwardKillToPlayer) == 8, "EAwardKillToPlayer is 8 bytes");

// No work of its own (its destructor is Event's). Its scalar deleting destructor is the linker's one copy for every
// 8-byte event whose destructor is Event's: EAddScoreObjective, EBeginDownloadCode, EChangeMaxTraffic, EChangeStage,
// EFailMissionObjective, EPassMissionObjective, EPowerUpInvulnerable and the class of vtable 0x0018c80c.
class EBeginDownloadCode : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t unknown04;
    };

    int32_t unknown04;                  // +0x04

    EBeginDownloadCode* Delete(unsigned flags);                                         // 0x00048680
};
LAYOUT_32(sizeof(EBeginDownloadCode) == 8, "EBeginDownloadCode is 8 bytes");

class EButtonMsgOff : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003e940
    EButtonMsgOff* Delete(unsigned flags);                                              // 0x00047590
};
LAYOUT_32(sizeof(EButtonMsgOff) == 4, "EButtonMsgOff is 4 bytes");

class EButtonMsgOn : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t message;
        float time;
        int32_t unknown0c;
        int32_t unknown10;
        int32_t unknown14;
    };

    int32_t message;                    // +0x04
    float time;                         // +0x08
    int32_t unknown0c;                  // +0x0c
    int32_t unknown10;                  // +0x10
    int32_t unknown14;                  // +0x14

    void Process();
    void Destruct();                                                                    // 0x0003e9b0
    EButtonMsgOn* Delete(unsigned flags);                                               // 0x000475b0
};
LAYOUT_32(sizeof(EButtonMsgOn) == 0x18, "EButtonMsgOn is 24 bytes");

class ECall911 : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003ea60
    ECall911* Delete(unsigned flags);                                                   // 0x000475d0
};
LAYOUT_32(sizeof(ECall911) == 4, "ECall911 is 4 bytes");

class ECallStage : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t stage;
    };

    int32_t stage;                      // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003eae0
    ECallStage* Delete(unsigned flags);                                                 // 0x000475f0
};
LAYOUT_32(sizeof(ECallStage) == 8, "ECallStage is 8 bytes");

// Works in its constructor (its destructor is Event's)
class EChangeMaxTraffic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t maxTraffic;
    };

    int32_t maxTraffic;                 // +0x04

    EChangeMaxTraffic* Construct(int maxTraffic);                                       // 0x0003f060
};
LAYOUT_32(sizeof(EChangeMaxTraffic) == 8, "EChangeMaxTraffic is 8 bytes");

// Works in its constructor (its destructor is Event's)
class EChangeStage : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t stage;
    };

    int32_t stage;                      // +0x04

    EChangeStage* Construct(int stage);                                                 // 0x0003f110
};
LAYOUT_32(sizeof(EChangeStage) == 8, "EChangeStage is 8 bytes");

class EClearBitMagicCounter : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t counter;
        uint32_t bits;
    };

    int32_t counter;                    // +0x04
    uint32_t bits;                      // +0x08 cleared in the counter

    void Process();
    void Destruct();                                                                    // 0x0003f260
    EClearBitMagicCounter* Delete(unsigned flags);                                      // 0x000476b0
};
LAYOUT_32(sizeof(EClearBitMagicCounter) == 0xc, "EClearBitMagicCounter is 12 bytes");

class EClearProgrammerEvent : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t event;
    };

    int32_t event;                      // +0x04

    void Process();
    void Destruct();                                                                    // 0x0003f300
    EClearProgrammerEvent* Delete(unsigned flags);                                      // 0x000476d0
};
LAYOUT_32(sizeof(EClearProgrammerEvent) == 8, "EClearProgrammerEvent is 8 bytes");

// No work of its own (its destructor is Event's)
class EDisplayObjectiveText : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t unknown04;
        int32_t unknown08;
        int32_t unknown0c;
    };

    int32_t unknown04;                  // +0x04
    int32_t unknown08;                  // +0x08
    int32_t unknown0c;                  // +0x0c

    EDisplayObjectiveText* Delete(unsigned flags);                                      // 0x00047990
};
LAYOUT_32(sizeof(EDisplayObjectiveText) == 0x10, "EDisplayObjectiveText is 16 bytes");

class EDisplayText : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t message;
        float time;
    };

    int32_t message;                    // +0x04
    float time;                         // +0x08

    void Process();
    void Destruct();                                                                    // 0x0003f690
    EDisplayText* Delete(unsigned flags);                                               // 0x000479b0
};
LAYOUT_32(sizeof(EDisplayText) == 0xc, "EDisplayText is 12 bytes");

class EEndMission : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0003fab0
    EEndMission* Delete(unsigned flags);                                                // 0x00047a50
};
LAYOUT_32(sizeof(EEndMission) == 4, "EEndMission is 4 bytes");

// ECollision's steps of its last incidental effects: by anything, and by the player's car (names ours)
extern uint32_t LastIncidentalStep;
extern uint32_t LastPlayerIncidentalStep;

// ---- the MakeEvent callbacks (RegisterEvent::LookupEvent's): a new event from the record. Ghidra names them
// GetEventName; the names are the AUF GameCube build's.

void E007Logo_MakeEvent_Callback(const void *record);                                           // 0x0003dc00
void EAICommand_MakeEvent_Callback(const EAICommand::Record *record);                           // 0x0003dd10
void EAIElementFireOff_MakeEvent_Callback(const EAIElementFireOff::Record *record);             // 0x0003de10
void EAIElementFireOn_MakeEvent_Callback(const EAIElementFireOn::Record *record);               // 0x0003ded0
void EAIElementSetAccuracy_MakeEvent_Callback(const EAIElementSetAccuracy::Record *record);     // 0x0003df80
void EAIElementSetCharParams_MakeEvent_Callback(const EAIElementSetCharParams::Record *record); // 0x0003e060
void EAIElementSetNonScoreable_MakeEvent_Callback(const EAIElementSetNonScoreable::Record *record);     // 0x0003e160
void EAIElementSetVehicleParams_MakeEvent_Callback(const EAIElementSetVehicleParams::Record *record);   // 0x0003e1e0
void EAIElementSetWakeRange_MakeEvent_Callback(const EAIElementSetWakeRange::Record *record);   // 0x0003e280
void EAIUpdate_MakeEvent_Callback(const void *record);                                          // 0x0003e320
void EAbortCinematic_MakeEvent_Callback(const void *record);                                    // 0x0003e390
void EActivateAIElement_MakeEvent_Callback(const EActivateAIElement::Record *record);           // 0x0003e3b0
void EActorMuzzleFlash_MakeEvent_Callback(const EActorMuzzleFlash::Record *record);             // 0x0003e4a0
void EAddObjective_MakeEvent_Callback(const EAddObjective::Record *record);                     // 0x0003e4e0
void EAddScoreAction_MakeEvent_Callback(const EAddScoreAction::Record *record);                 // 0x0003e5e0
void EAddScoreObjective_MakeEvent_Callback(const EAddScoreObjective::Record *record);           // 0x0003e600
void EAnimUpdate_MakeEvent_Callback(const void *record);                                        // 0x0003e690
void EAudioUpdate_MakeEvent_Callback(const void *record);                                       // 0x0003e6d0
void EAutoDrive_MakeEvent_Callback(const EAutoDrive::Record *record);                           // 0x0003e7a0
void EAutoDriveSpeedChange_MakeEvent_Callback(const EAutoDriveSpeedChange::Record *record);     // 0x0003e7c0
void EAutoDriveSteering_MakeEvent_Callback(const EAutoDriveSteering::Record *record);           // 0x0003e7e0
void EAwardHitToPlayer_MakeEvent_Callback(const EAwardHitToPlayer::Record *record);             // 0x0003e850
void EAwardKillToPlayer_MakeEvent_Callback(const EAwardKillToPlayer::Record *record);           // 0x0003e8d0
void EBashCarAlongObjectAxis_MakeEvent_Callback(const EBashCarAlongObjectAxis::Record *record); // 0x0003e8f0
void EBeginDownloadCode_MakeEvent_Callback(const EBeginDownloadCode::Record *record);           // 0x0003e920
void EButtonMsgOff_MakeEvent_Callback(const void *record);                                      // 0x0003e990
void EButtonMsgOn_MakeEvent_Callback(const EButtonMsgOn::Record *record);                       // 0x0003ea20
void ECall911_MakeEvent_Callback(const void *record);                                           // 0x0003eac0
void ECallStage_MakeEvent_Callback(const ECallStage::Record *record);                           // 0x0003eb30
void ECameraShake_MakeEvent_Callback(const ECameraShake::Record *record);                       // 0x0003ed40
void ECameraUpdate_MakeEvent_Callback(const void *record);                                      // 0x0003ede0
void ECameraZoom_MakeEvent_Callback(const ECameraZoom::Record *record);                         // 0x0003eee0
void EChangeCarCameraView_MakeEvent_Callback(const EChangeCarCameraView::Record *record);       // 0x0003f020
void EChangeMaxTraffic_MakeEvent_Callback(const EChangeMaxTraffic::Record *record);             // 0x0003f0c0
void EChangeStage_MakeEvent_Callback(const EChangeStage::Record *record);                       // 0x0003f170
void ECinematicCamera_MakeEvent_Callback(const ECinematicCamera::Record *record);               // 0x0003f1c0
void EClearBitMagicCounter_MakeEvent_Callback(const EClearBitMagicCounter::Record *record);     // 0x0003f2d0
void EClearProgrammerEvent_MakeEvent_Callback(const EClearProgrammerEvent::Record *record);     // 0x0003f350
void EControlToCPU_MakeEvent_Callback(const void *record);                                      // 0x0003f390
void EControlToPlayer_MakeEvent_Callback(const void *record);                                   // 0x0003f3b0
void EControlToSpline_MakeEvent_Callback(const EControlToSpline::Record *record);               // 0x0003f3d0
void ECustomTransition_MakeEvent_Callback(const ECustomTransition::Record *record);             // 0x0003f420
void EDamagePlayer_MakeEvent_Callback(const EDamagePlayer::Record *record);                     // 0x0003f440
void EDeactivateAIElement_MakeEvent_Callback(const EDeactivateAIElement::Record *record);       // 0x0003f4e0
void EDisablePowerUp_MakeEvent_Callback(const EDisablePowerUp::Record *record);                 // 0x0003f5e0
void EDisableTrigger_MakeEvent_Callback(const EDisableTrigger::Record *record);                 // 0x0003f640
void EDisplayObjectiveText_MakeEvent_Callback(const EDisplayObjectiveText::Record *record);     // 0x0003f660
void EDisplayText_MakeEvent_Callback(const EDisplayText::Record *record);                       // 0x0003f6f0
void EDropMine_MakeEvent_Callback(const EDropMine::Record *record);                             // 0x0003f7c0
void EEnablePowerUp_MakeEvent_Callback(const EEnablePowerUp::Record *record);                   // 0x0003f890
void EEnableTrigger_MakeEvent_Callback(const EEnableTrigger::Record *record);                   // 0x0003f8e0
void EEndCameraAnim_MakeEvent_Callback(const EEndCameraAnim::Record *record);                   // 0x0003fa30
void EEndCarStop_MakeEvent_Callback(const void *record);                                        // 0x0003fa70
void EEndControlToSpline_MakeEvent_Callback(const EEndControlToSpline::Record *record);         // 0x0003fa90
void EEndMission_MakeEvent_Callback(const void *record);                                        // 0x0003fb50

// ---- the ResolveEvent callbacks (RegisterEvent::ResolveEvent's): the record's references resolved when the track
// loads (CARP::EventListResolver). The names are the AUF GameCube build's pattern; Ghidra has them unnamed.

void EAICommand_ResolveEvent_Callback(EAICommand::Record *record, UGroup *shared, UGroup *parent);         // 0x000472e0
void ECinematicCamera_ResolveEvent_Callback(ECinematicCamera::Record *record, UGroup *shared, UGroup *parent);     // 0x0003f210
// The linker's one copy for every event whose only reference is its record's first field (37 entries of the
// table: EActorMuzzleFlash, the AI element events, the triggers, EButtonMsgOn, ECustomTransition and others)
void EDeactivateAIElement_ResolveEvent_Callback(EDeactivateAIElement::Record *record, UGroup *shared,
                                                UGroup *parent);                                           // 0x0003f500

#endif // DRIVING_GAME_EVENTSA_H_
