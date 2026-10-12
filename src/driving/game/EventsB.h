#ifndef DRIVING_GAME_EVENTSB_H_
#define DRIVING_GAME_EVENTSB_H_

// ---------------------------------------------------------------------------------------------------------------
// Mission events EExit .. EPathSFX (alphabetically; EFireWeapon has FireWeaponEvent.h): the HUD, objective,
// trigger, AI, object and path events the mission scripts and triggers raise. See EventsB.cpp.
//
// An event class is an Event (EventManager.hpp) with its data after the vtable. Its vtable has one slot, the
// scalar deleting destructor (Delete here), and the event manager runs an event by destroying it: the destructor
// (Destruct) does the event's work, Process, then puts back Event's vtable - Event's destructor, inlined. Process
// is inlined into the destructor in the original, as it is in the PS2 build; the name is ours (no build's symbols
// have one). Each class's MakeEvent callback (Ghidra: GetEventName; named as the AUF build's) is what
// RegisterEvent's table holds for its event number: it news the event from the event's static data - a record of
// the CARP data, whose first word the events do not read - and, for some, from what raised it (gEventDynamicData,
// world/Trigger.h). A few events do their work in the constructor and have Event's destructor (the linker folded
// theirs into it); a few have nothing to do at all.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../EventManager.hpp"            // Event
#include "../data/Carp.h"                 // CARP::Instance, CARP::TagReference
#include "../world/CollisionTypes.h"      // Coord4, WindowHit
#include "../../common/xbeClass.h"        // offsetof on a derived class without clang's warning

class RSceneObj;
class UGroup;
class WTrigger;
struct ArticleEffect;
struct TriggerEvents;

// ---- the HUD and the mission's end

// (4 bytes; vtable 0x0018c4fc) Unpauses the HUD, ends the game loop and tallies the scores into the launch page.
class EExit : public Event {
public:
    EExit* Construct();                         // (only GOrthoHudView::PostRender calls it) // 0x0003fb70
    void Destruct();                                                                    // 0x0003fb80
    EExit* Delete(unsigned flags);                                                      // 0x00047a70

private:
    void Process();
};
XBE_CLASS_SIZE(EExit, 4);

// (4 bytes; vtable 0x0018c500) Advances the Simulation's step.
class EFESimFrameUpdate : public Event {
public:
    void Destruct();                                                                    // 0x0003fc10
    EFESimFrameUpdate* Delete(unsigned flags);                                          // 0x00047a90

private:
    void Process();
};
XBE_CLASS_SIZE(EFESimFrameUpdate, 4);

// (4 bytes; vtable 0x0018c52c)
class EGadgetOff : public Event {
public:
    void Destruct();                                                                    // 0x00040200
    EGadgetOff* Delete(unsigned flags);                                                 // 0x00047b10

private:
    void Process();
};
XBE_CLASS_SIZE(EGadgetOff, 4);

// (0xc bytes; vtable 0x0018c530)
class EGadgetOn : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t gadget;
        float value;
    };

    int32_t gadget;                     // +0x04 GHud::GadgetOn's arguments
    float value;                        // +0x08

    void Destruct();                                                                    // 0x00040270
    EGadgetOn* Delete(unsigned flags);                                                  // 0x00047b30

private:
    void Process();
};
XBE_CLASS_SIZE(EGadgetOn, 0xc);

// (8 bytes; vtable 0x0018c56c) The HUD's lava damage effect on or off.
class ELavaHaze : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t on;
    };

    int32_t on;                         // +0x04

    void Destruct();                                                                    // 0x00040ac0
    ELavaHaze* Delete(unsigned flags);                                                  // 0x00047f50

private:
    void Process();
};
XBE_CLASS_SIZE(ELavaHaze, 8);

// (8 bytes; vtable 0x0018c570) The HUD's letterbox off, the cinematic mode cleared, force feedback allowed again.
class ELetterBoxOff : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t flag;
    };

    int32_t flag;                       // +0x04 GHud::LetterBoxOff's argument, as a bool

    void Destruct();                                                                    // 0x00040b40
    ELetterBoxOff* Delete(unsigned flags);                                              // 0x00047f70

private:
    void Process();
};
XBE_CLASS_SIZE(ELetterBoxOff, 8);

// (8 bytes; vtable 0x0018c574) Force feedback off, the HUD's letterbox on, the cinematic mode set, no motion blur.
class ELetterBoxOn : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t flag;
    };

    int32_t flag;                       // +0x04 GHud::LetterBoxOn's argument, as a bool

    void Destruct();                                                                    // 0x00040bd0
    ELetterBoxOn* Delete(unsigned flags);                                               // 0x00047f90

private:
    void Process();
};
XBE_CLASS_SIZE(ELetterBoxOn, 8);

// (4 bytes; vtable 0x0018c548) RColorize's mode off.
class EInfraRedOff : public Event {
public:
    void Destruct();                                                                    // 0x000405f0
    EInfraRedOff* Delete(unsigned flags);                                               // 0x00047e90

private:
    void Process();
};
XBE_CLASS_SIZE(EInfraRedOff, 4);

// (4 bytes; vtable 0x0018c54c) RColorize's infrared mode.
class EInfraRedOn : public Event {
public:
    void Destruct();                                                                    // 0x00040660
    EInfraRedOn* Delete(unsigned flags);                                                // 0x00047eb0

private:
    void Process();
};
XBE_CLASS_SIZE(EInfraRedOn, 4);

// ---- the mission manager: objectives and counters

// (8 bytes; vtable 0x0018c504, whose destructor is EBeginDownloadCode's: Event's) Nothing to do.
class EFailMissionObjective : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t objective;
    };

    int32_t objective;                  // +0x04

};
XBE_CLASS_SIZE(EFailMissionObjective, 8);

// (8 bytes; vtable 0x0018c5a0, whose destructor is EBeginDownloadCode's: Event's) Nothing to do.
class EPassMissionObjective : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t objective;
    };

    int32_t objective;                  // +0x04

};
XBE_CLASS_SIZE(EPassMissionObjective, 8);

// (0xc bytes; vtable 0x0018c5a4, whose destructor is EForceNearPedRespawn's: Event's) Nothing to do.
class EPassObjective : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t objective;
        uint32_t unknown08;
    };

    int32_t objective;                  // +0x04
    uint32_t unknown08;

};
XBE_CLASS_SIZE(EPassObjective, 0xc);

// (0x18 bytes; vtable 0x0018c550) Nothing to do: its destructor is Event's.
class EInsertObjective : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t unknown04[5];
    };

    uint32_t unknown04[5];              // +0x04 the record's

    // Event's destructor, then the bytes given back
    EInsertObjective* Delete(unsigned flags);                                           // 0x00047ed0
};
XBE_CLASS_SIZE(EInsertObjective, 0x18);

// (8 bytes; vtable 0x0018c540)
class EIncMagicCounter : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t counter;
    };

    int32_t counter;                    // +0x04

    void Destruct();                                                                    // 0x000404c0
    EIncMagicCounter* Delete(unsigned flags);                                           // 0x00047e50

private:
    void Process();
};
XBE_CLASS_SIZE(EIncMagicCounter, 8);

// (0x14 bytes; vtable 0x0018c584) SMissionManager::AddObjective with the record's four values.
class EObjectiveAdd : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t id;
        int32_t unknown08;
        int32_t score;
        int32_t unknown10;
    };

    int32_t id;                         // +0x04 (AddObjective's names)
    int32_t unknown08;
    int32_t score;                      // +0x0c
    int32_t unknown10;

    void Destruct();                                                                    // 0x00040f10
    EObjectiveAdd* Delete(unsigned flags);                                              // 0x00047ff0

private:
    void Process();
};
XBE_CLASS_SIZE(EObjectiveAdd, 0x14);

// (0x18 bytes; vtable 0x0018c590) SMissionManager::InsertObjective with the record's five values.
class EObjectiveInsert : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t beforeId;
        int32_t id;
        int32_t unknown0c;
        int32_t score;
        int32_t unknown14;
    };

    int32_t beforeId;                   // +0x04 (InsertObjective's names)
    int32_t id;                         // +0x08
    int32_t unknown0c;
    int32_t score;                      // +0x10
    int32_t unknown14;

    void Destruct();                                                                    // 0x000410e0
    EObjectiveInsert* Delete(unsigned flags);                                           // 0x00048050

private:
    void Process();
};
XBE_CLASS_SIZE(EObjectiveInsert, 0x18);

// (0xc bytes; vtable 0x0018c588) The HUD's objective message for the objective.
class EObjectiveDisplayText : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t id;
        int32_t flag;
    };

    int32_t id;                         // +0x04
    int32_t flag;                       // +0x08 TriggerObjectiveMsg's last argument, as a bool

    void Destruct();                                                                    // 0x00040fb0
    EObjectiveDisplayText* Delete(unsigned flags);                                      // 0x00048010

private:
    void Process();
};
XBE_CLASS_SIZE(EObjectiveDisplayText, 0xc);

// The record and the fields of the three objective events that change an objective and may show a message.
struct ObjectiveChangeRecord {
    uint32_t unknown00;
    int32_t id;
    int32_t showMessage;
};

// (0xc bytes; vtable 0x0018c58c)
class EObjectiveFail : public Event {
public:
    int32_t id;                         // +0x04
    int32_t showMessage;                // +0x08 non-zero: the HUD's "failed" message too

    void Destruct();                                                                    // 0x00041040
    EObjectiveFail* Delete(unsigned flags);                                             // 0x00048030

private:
    void Process();
};
XBE_CLASS_SIZE(EObjectiveFail, 0xc);

// (0xc bytes; vtable 0x0018c594)
class EObjectivePass : public Event {
public:
    int32_t id;                         // +0x04
    int32_t showMessage;                // +0x08 non-zero: the HUD's "passed" message too

    void Destruct();                                                                    // 0x00041180
    EObjectivePass* Delete(unsigned flags);                                             // 0x00048070

private:
    void Process();
};
XBE_CLASS_SIZE(EObjectivePass, 0xc);

// (0xc bytes; vtable 0x0018c598)
class EObjectiveSetCurrent : public Event {
public:
    int32_t id;                         // +0x04
    int32_t showMessage;                // +0x08 non-zero: the HUD's message too

    void Destruct();                                                                    // 0x00041220
    EObjectiveSetCurrent* Delete(unsigned flags);                                       // 0x00048090

private:
    void Process();
};
XBE_CLASS_SIZE(EObjectiveSetCurrent, 0xc);

// (8 bytes; vtable 0x0018c59c)
class EObjectiveSetIncomplete : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t id;
    };

    int32_t id;                         // +0x04

    void Destruct();                                                                    // 0x000412c0
    EObjectiveSetIncomplete* Delete(unsigned flags);                                    // 0x000480b0

private:
    void Process();
};
XBE_CLASS_SIZE(EObjectiveSetIncomplete, 8);

// ---- triggers

// (8 bytes; vtable 0x0018c508) Runs a list of events now, each by its number's callback.
class EFireEventList : public Event {
public:
    struct Record {
        uint32_t unknown00;
        TriggerEvents *events;
    };

    TriggerEvents *events;              // +0x04

    void Destruct();                                                                    // 0x0003fca0
    EFireEventList* Delete(unsigned flags);                                             // 0x00047ab0

private:
    void Process();
};
XBE_CLASS_SIZE(EFireEventList, 8);

// (0x2c bytes; vtable 0x0018c50c) Fires one of four triggers at random, by weight; past the weights' sum, none.
class EFireRandomTrigger : public Event {
public:
    struct Choice {
        WTrigger *trigger;              // NULL: nothing fires
        float weight;
    };
    struct Record {
        struct Choice {
            CARP::TagReference trigger; // a WTrigger once resolved
            float weight;
        };
        uint32_t unknown00;
        Choice choices[4];
    };

    Choice choices[4];                  // +0x04
    uint8_t flag;                       // +0x24 what raised it: WTrigger::FireEvents' arguments
    uint8_t unknown25[3];
    int32_t index;                      // +0x28

    void Destruct();                                                                    // 0x0003fd40
    EFireRandomTrigger* Delete(unsigned flags);                                         // 0x00047ad0

private:
    void Process();
};
XBE_CLASS_SIZE(EFireRandomTrigger, 0x2c);

// (0x18 bytes; vtable 0x0018c510) Fires the trigger if the rigid body that raised the event is moving slower (or,
// with `below` zero, faster) than the speed.
class EFireTriggerSpeedCondition : public Event {
public:
    struct Record {
        uint32_t unknown00;
        WTrigger *trigger;
        float speed;
        int32_t below;
    };

    WTrigger *trigger;                  // +0x04 NULL: nothing to do
    float speed;                        // +0x08
    int32_t below;                      // +0x0c (name ours)
    uint8_t flag;                       // +0x10 what raised it: WTrigger::FireEvents' arguments
    uint8_t unknown11[3];
    int32_t index;                      // +0x14 also the rigid body's slot

    void Destruct();                                                                    // 0x0003ff60
    EFireTriggerSpeedCondition* Delete(unsigned flags);                                 // 0x00047af0

private:
    void Process();
};
XBE_CLASS_SIZE(EFireTriggerSpeedCondition, 0x18);

// (0x14 bytes; vtable 0x0018c544) Damages the physics object owning the rigid body that raised the event.
class EInflictDamage : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float amount;
    };

    uint32_t index;                     // +0x04 what raised it: the rigid body's slot
    uint8_t flag;                       // +0x08 ... set: nothing is done
    uint8_t unknown09[3];
    WTrigger *trigger;                  // +0x0c ... the trigger
    float amount;                       // +0x10

    void Destruct();                                                                    // 0x00040530
    EInflictDamage* Delete(unsigned flags);                                             // 0x00047e70

private:
    void Process();
};
XBE_CLASS_SIZE(EInflictDamage, 0x14);

// ---- world objects

// (8 bytes; vtable 0x0018c534) Sets the instance that raised the event hidden (flag bit 0).
class EHideInstance : public Event {
public:
    CARP::Instance *instance;           // +0x04

    void Destruct();                                                                    // 0x00040300
    EHideInstance* Delete(unsigned flags);                                              // 0x00047b50

private:
    void Process();
};
XBE_CLASS_SIZE(EHideInstance, 8);

// (0xc bytes; vtable 0x0018c538) Hides the instance's scene object, and sets the instance hidden - the scene
// object's own copy of it, if the world has no scene object for it.
class EHideInstanceStatic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::Instance *instance;
    };

    CARP::Instance *instance;           // +0x04 NULL: nothing to do
    RSceneObj *sceneObj;                // +0x08 what raised it

    void Destruct();                                                                    // 0x00040340
    EHideInstanceStatic* Delete(unsigned flags);                                        // 0x00047b70

private:
    void Process();
};
XBE_CLASS_SIZE(EHideInstanceStatic, 0xc);

// (0x54 bytes; vtable 0x0018c53c) A window pane hit (WCollisionMgr::CheckHitWindow raises it): on the first hit a
// shatter sound, "SFX_StoreAlarm" on every eighth first hit below a height of 10, and the "Wd_Glass" effect, with
// "Wd_Wsmk" at the pane's middle if `flag`; on hits one to four "SFX_Headlight".
class EHitWindow : public Event {
public:
    Coord4 point;                       // +0x04
    Coord4 normal;                      // +0x14
    WindowHit hit;                      // +0x24
    int32_t flag;                       // +0x50

    EHitWindow* Construct(Coord4 point, Coord4 normal, WindowHit hit, int flag);       // 0x000403f0
    // Its record has the event's own layout
    void Destruct();                                                                    // 0x00047b90
    EHitWindow* Delete(unsigned flags);                                                 // 0x0004c7d0

private:
    void Process();
};
XBE_CLASS_SIZE(EHitWindow, 0x54);
static_assert(offsetof(EHitWindow, hit) == 0x24 && offsetof(EHitWindow, flag) == 0x50, "EHitWindow layout");

// (0x14 bytes; vtable 0x0018c55c) Kicks the rigid body that raised the event: an impulse straight up at one of
// its first four levers, scaled at random, and a damage of 1000000 to its owner. Nothing for a body of kind 1.
class EKickObject : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float impulse;
        float impulseRandom;
    };

    uint8_t flag;                       // +0x04 what raised it (not read)
    uint8_t unknown05[3];
    int32_t index;                      // +0x08 ... the rigid body's slot
    float impulse;                      // +0x0c the push: impulse + a random 0..1 * impulseRandom
    float impulseRandom;                // +0x10

    void Destruct();                                                                    // 0x0004c7f0
    EKickObject* Delete(unsigned flags);                                                // 0x0004ec80

private:
    void Process();
};
XBE_CLASS_SIZE(EKickObject, 0x14);

// (0xc bytes; vtable 0x0018c564) Puts the scene object's vehicle to sleep, if the AI drives it, or deletes its
// physics object.
class EKillObject : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::Instance *instance;
    };

    CARP::Instance *instance;           // +0x04
    RSceneObj *sceneObj;                // +0x08 what raised it; NULL: the instance's

    void Destruct();                                                                    // 0x00040960
    EKillObject* Delete(unsigned flags);                                                // 0x00047f10

private:
    void Process();
};
XBE_CLASS_SIZE(EKillObject, 0xc);

// (8 bytes; vtable 0x0018c568) Deletes the physics object of the instance's scene object.
class EKillSentry : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::Instance *instance;
    };

    CARP::Instance *instance;           // +0x04 NULL: nothing to do

    void Destruct();                                                                    // 0x00040a30
    EKillSentry* Delete(unsigned flags);                                                // 0x00047f30

private:
    void Process();
};
XBE_CLASS_SIZE(EKillSentry, 8);

// (0x10 bytes; vtable 0x0018c57c) The effect's muzzle flash: TriggerFX type 8 on the scene object that raised the
// event, at the effect's place in the instance's frame, pointing along the effect's rotation.
class EMuzzleFlash : public Event {
public:
    struct Record {
        uint32_t unknown00;
        ArticleEffect *effect;
    };

    ArticleEffect *effect;              // +0x04
    RSceneObj *sceneObj;                // +0x08 what raised it
    uint32_t instanceIndex;             // +0x0c ... its instance

    void Destruct();                                                                    // 0x00040cb0
    EMuzzleFlash* Delete(unsigned flags);                                               // 0x00047fb0

private:
    void Process();
};
XBE_CLASS_SIZE(EMuzzleFlash, 0x10);

// (0xc bytes; vtable 0x0018c580) SWeaponManager::StartNuclearSequence at a trigger's position (a negative
// strength is taken as 1).
class ENuclearBlast : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float strength;
        const Coord3 *trigger;
    };

    float strength;                     // +0x04
    const Coord3 *trigger;              // +0x08 a CARP trigger: its position first

    void Destruct();                                                                    // 0x00040e50
    ENuclearBlast* Delete(unsigned flags);                                              // 0x00047fd0

private:
    void Process();
};
XBE_CLASS_SIZE(ENuclearBlast, 0xc);

// ---- the AI and the player's car

// (8 bytes; vtable 0x0018c524) ForceStopOn on the player's car: flags 3 if `hard`, else 1.
class EForceCarStop : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t hard;
    };

    int32_t hard;                       // +0x04 (name ours)

    void Destruct();                                                                    // 0x0004c770
    EForceCarStop* Delete(unsigned flags);                                              // 0x0004ec60

private:
    void Process();
};
XBE_CLASS_SIZE(EForceCarStop, 8);

// (0xc bytes; vtable 0x0018c528) Its work is done when it is made: the road spawner's spawn data refreshed with
// its two values, and every pedestrian recycled. Running it does nothing (its destructor is Event's).
class EForceNearPedRespawn : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float unknown04;
        float unknown08;
    };

    float unknown04;                    // +0x04 AIRoadSpawn::RefreshSpawnData's second and third arguments
    float unknown08;                    // +0x08

    EForceNearPedRespawn* Construct(float unknown04, float unknown08);                 // 0x00040100
    // Event's destructor, then the bytes given back (also EPassObjective's)
    EForceNearPedRespawn* Delete(unsigned flags);                                       // 0x00048700
};
XBE_CLASS_SIZE(EForceNearPedRespawn, 0xc);

// (0x10 bytes; vtable 0x0018c554) Pushes the AI element's vehicle or character away from a trigger's position,
// with a random spin.
class EKickAIElement : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::AIElement *element;
        const Coord3 *trigger;
        float force;
    };

    CARP::AIElement *element;           // +0x04 a CARP AI element (AIElementController's key)
    const Coord3 *trigger;              // +0x08 a CARP trigger: its position first
    float force;                        // +0x0c

    void Destruct();                                                                    // 0x00040710
    EKickAIElement* Delete(unsigned flags);                                             // 0x00047ef0

private:
    void Process();
};
XBE_CLASS_SIZE(EKickAIElement, 0x10);

// (0xc bytes; vtable 0x0018c560) Kills the AI element's vehicle or character - a damage of 1000000 - and, if
// `scores`, counts a kill for a scoreable one (clearing its flag).
class EKillAIElement : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::AIElement *element;
        int32_t scores;
    };

    CARP::AIElement *element;           // +0x04 a CARP AI element; NULL: nothing to do
    int32_t scores;                     // +0x08 (name ours)

    void Destruct();                                                                    // 0x0004ca20
    EKillAIElement* Delete(unsigned flags);                                             // 0x0004eca0

private:
    void Process();
};
XBE_CLASS_SIZE(EKillAIElement, 0xc);

// (0x1c bytes; vtable 0x0018c578) Activates the AI element (clears its flag bit 1) with a chance in a hundred,
// within the hench limit. Raised by a trigger, it first disables the trigger when the body that touched it is the
// vehicle element's (or, without one, the player's), and when it is another vehicle's, does nothing.
class EMaybeActivateAIElement : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::AIElement *element;
        CARP::AIElement *vehicleElement;
        int32_t chance;
    };

    CARP::AIElement *element;           // +0x04 a CARP AI element
    CARP::AIElement *vehicleElement;    // +0x08 another; NULL: the player's car is looked for
    int32_t chance;                     // +0x0c out of 100
    WTrigger *trigger;                  // +0x10 what raised it; NULL: not a trigger
    uint8_t flag;                       // +0x14 ... set: the body is a simple body
    uint8_t unknown15[3];
    int32_t index;                      // +0x18 ... the body's slot

    void Destruct();                                                                    // 0x0004cbc0
    EMaybeActivateAIElement* Delete(unsigned flags);                                    // 0x0004ecc0

private:
    void Process();
};
XBE_CLASS_SIZE(EMaybeActivateAIElement, 0x1c);

// ---- paths

// (0x14 bytes; vtable 0x0018c5a8) Sets the path of the instance that raised the event running, with a new
// acceleration delay, acceleration and desired throttle.
class EPathChangeThrottleDynamic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t accelDelay;
        float acceleration;
        float throttle;
    };

    CARP::Instance *instance;           // +0x04 what raised it
    uint32_t accelDelay;                // +0x08 (RPathHandle's names)
    float acceleration;                 // +0x0c
    float throttle;                     // +0x10

    void Destruct();                                                                    // 0x00041380
    EPathChangeThrottleDynamic* Delete(unsigned flags);                                 // 0x000480d0

private:
    void Process();
};
XBE_CLASS_SIZE(EPathChangeThrottleDynamic, 0x14);

// (0x18 bytes; vtable 0x0018c5ac) The same for the path of the record's instance, AI element or the player's car
// (EPathUtil_GetFromInstanceOrAI).
class EPathChangeThrottleStatic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::Instance *instance;
        CARP::AIElement *element;
        uint32_t accelDelay;
        float acceleration;
        float throttle;
    };

    CARP::Instance *instance;           // +0x04
    CARP::AIElement *element;           // +0x08 a CARP AI element
    uint32_t accelDelay;                // +0x0c
    float acceleration;                 // +0x10
    float throttle;                     // +0x14

    void Destruct();                                                                    // 0x00041450
    EPathChangeThrottleStatic* Delete(unsigned flags);                                  // 0x000480f0

private:
    void Process();
};
XBE_CLASS_SIZE(EPathChangeThrottleStatic, 0x18);

// (0x14 bytes; vtable 0x0018c5b0) As EPathChangeThrottleDynamic, the desired throttle the path's throttle scaled.
class EPathMultiplyThrottleDynamic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t accelDelay;
        float acceleration;
        float scale;
    };

    CARP::Instance *instance;           // +0x04 what raised it
    uint32_t accelDelay;                // +0x08
    float acceleration;                 // +0x0c
    float scale;                        // +0x10

    void Destruct();                                                                    // 0x00041520
    EPathMultiplyThrottleDynamic* Delete(unsigned flags);                               // 0x00048110

private:
    void Process();
};
XBE_CLASS_SIZE(EPathMultiplyThrottleDynamic, 0x14);

// (0x18 bytes; vtable 0x0018c5b4) As EPathChangeThrottleStatic, the desired throttle the path's throttle scaled.
class EPathMultiplyThrottleStatic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::Instance *instance;
        CARP::AIElement *element;
        uint32_t accelDelay;
        float acceleration;
        float scale;
    };

    CARP::Instance *instance;           // +0x04
    CARP::AIElement *element;           // +0x08 a CARP AI element
    uint32_t accelDelay;                // +0x0c
    float acceleration;                 // +0x10
    float scale;                        // +0x14

    void Destruct();                                                                    // 0x00041600
    EPathMultiplyThrottleStatic* Delete(unsigned flags);                                // 0x00048130

private:
    void Process();
};
XBE_CLASS_SIZE(EPathMultiplyThrottleStatic, 0x18);

// What EPathSFX hands the EPathSFXUpdate task it schedules (0x18 bytes, operator new; the name is ours)
struct PathSFXUpdateData {
    uint32_t unknown00;                 // not written
    PathSFXUpdateData *self;            // +0x04
    struct RPathHandle *path;           // +0x08
    class ASound *sound;                // +0x0c
    const char *unknown10;              // +0x10 the event's
    uint32_t taskNum;                   // +0x14 Schedule::AddTask's
};
static_assert(sizeof(PathSFXUpdateData) == 0x18, "the path sound's task data is 0x18 bytes");

// (0x18 bytes; vtable 0x0018c5b8) With sound on, a sound of the patch from bank slot 0 for the instance's running
// path, and an EPathSFXUpdate task (event 0x10f) every tick to follow it.
class EPathSFX : public Event {
public:
    struct Record {
        uint32_t unknown00;
        CARP::Instance *instance;
        const char *patch;
        const char *unknown0c;
        float minDistance;
        float maxDistance;
    };

    CARP::Instance *instance;           // +0x04
    const char *patch;                  // +0x08 in the "Object Sounds" mix
    const char *unknown0c;              // +0x0c handed to the task
    float minDistance;                  // +0x10 the sound's
    float maxDistance;                  // +0x14

    void Destruct();                                                                    // 0x000416e0
    EPathSFX* Delete(unsigned flags);                                                   // 0x00048150

private:
    void Process();
};
XBE_CLASS_SIZE(EPathSFX, 0x18);

// EHitWindow's sound counters (EventsB.cpp; names ours): which shatter sound, and the alarm on every eighth low pane
extern uint32_t gGlassShatterCount;
extern uint32_t gStoreAlarmCount;

// ---- the MakeEvent callbacks (RegisterEvent::LookupEvent's): a new event from the record. Ghidra names them
// GetEventName; the names are the AUF GameCube build's.

void EExit_MakeEvent_Callback(const void *record);                                              // 0x0003fbf0
void EFESimFrameUpdate_MakeEvent_Callback(const void *record);                                  // 0x0003fc60
void EFailMissionObjective_MakeEvent_Callback(const EFailMissionObjective::Record *record);     // 0x0003fc80
void EFireEventList_MakeEvent_Callback(const EFireEventList::Record *record);                   // 0x0003fd20
void EFireRandomTrigger_MakeEvent_Callback(const EFireRandomTrigger::Record *record);           // 0x0003fe40
void EFireTriggerSpeedCondition_MakeEvent_Callback(
    const EFireTriggerSpeedCondition::Record *record);                                          // 0x00040020
void EForceCarStop_MakeEvent_Callback(const EForceCarStop::Record *record);                     // 0x000400e0
void EForceNearPedRespawn_MakeEvent_Callback(const EForceNearPedRespawn::Record *record);       // 0x000401a0
void EGadgetOff_MakeEvent_Callback(const void *record);                                         // 0x00040250
void EGadgetOn_MakeEvent_Callback(const EGadgetOn::Record *record);                             // 0x000402d0
void EHideInstance_MakeEvent_Callback(const void *record);                                      // 0x00040320
void EHideInstanceStatic_MakeEvent_Callback(const EHideInstanceStatic::Record *record);         // 0x000403c0
void EHitWindow_MakeEvent_Callback(const EHitWindow *record);                                   // 0x00040450
void EIncMagicCounter_MakeEvent_Callback(const EIncMagicCounter::Record *record);               // 0x00040510
void EInflictDamage_MakeEvent_Callback(const EInflictDamage::Record *record);                   // 0x000405b0
void EInfraRedOff_MakeEvent_Callback(const void *record);                                       // 0x00040640
void EInfraRedOn_MakeEvent_Callback(const void *record);                                        // 0x000406b0
void EInsertObjective_MakeEvent_Callback(const EInsertObjective::Record *record);               // 0x000406d0
void EKickAIElement_MakeEvent_Callback(const EKickAIElement::Record *record);                   // 0x000408c0
void EKickObject_MakeEvent_Callback(const EKickObject::Record *record);                         // 0x000408f0
void EKillAIElement_MakeEvent_Callback(const EKillAIElement::Record *record);                   // 0x00040930
void EKillObject_MakeEvent_Callback(const EKillObject::Record *record);                         // 0x00040a00
void EKillSentry_MakeEvent_Callback(const EKillSentry::Record *record);                         // 0x00040aa0
void ELavaHaze_MakeEvent_Callback(const ELavaHaze::Record *record);                             // 0x00040b20
void ELetterBoxOff_MakeEvent_Callback(const ELetterBoxOff::Record *record);                     // 0x00040bb0
void ELetterBoxOn_MakeEvent_Callback(const ELetterBoxOn::Record *record);                       // 0x00040c40
void EMaybeActivateAIElement_MakeEvent_Callback(const EMaybeActivateAIElement::Record *record); // 0x00040c60
void EMuzzleFlash_MakeEvent_Callback(const EMuzzleFlash::Record *record);                       // 0x00040e10
void ENuclearBlast_MakeEvent_Callback(const ENuclearBlast::Record *record);                     // 0x00040ee0
void EObjectiveAdd_MakeEvent_Callback(const EObjectiveAdd::Record *record);                     // 0x00040f70
void EObjectiveDisplayText_MakeEvent_Callback(const EObjectiveDisplayText::Record *record);     // 0x00041010
void EObjectiveFail_MakeEvent_Callback(const ObjectiveChangeRecord *record);                    // 0x000410b0
void EObjectiveInsert_MakeEvent_Callback(const EObjectiveInsert::Record *record);               // 0x00041140
void EObjectivePass_MakeEvent_Callback(const ObjectiveChangeRecord *record);                    // 0x000411f0
void EObjectiveSetCurrent_MakeEvent_Callback(const ObjectiveChangeRecord *record);              // 0x00041290
void EObjectiveSetIncomplete_MakeEvent_Callback(const EObjectiveSetIncomplete::Record *record); // 0x00041310
void EPassMissionObjective_MakeEvent_Callback(const EPassMissionObjective::Record *record);     // 0x00041330
void EPassObjective_MakeEvent_Callback(const EPassObjective::Record *record);                   // 0x00041350
void EPathChangeThrottleDynamic_MakeEvent_Callback(
    const EPathChangeThrottleDynamic::Record *record);                                          // 0x00041410
void EPathChangeThrottleStatic_MakeEvent_Callback(
    const EPathChangeThrottleStatic::Record *record);                                           // 0x000414e0
void EPathMultiplyThrottleDynamic_MakeEvent_Callback(
    const EPathMultiplyThrottleDynamic::Record *record);                                        // 0x000415c0
void EPathMultiplyThrottleStatic_MakeEvent_Callback(
    const EPathMultiplyThrottleStatic::Record *record);                                         // 0x000416a0
void EPathSFX_MakeEvent_Callback(const EPathSFX::Record *record);                               // 0x00041810

// ---- the ResolveEvent callbacks (RegisterEvent::ResolveEvent's): the record's references resolved when the track
// loads (CARP::EventListResolver). The names are the AUF GameCube build's pattern; Ghidra has them unnamed.

void EFireRandomTrigger_ResolveEvent_Callback(EFireRandomTrigger::Record *record, UGroup *shared,
                                              UGroup *parent);                                  // 0x0003feb0

#endif // DRIVING_GAME_EVENTSB_H_
