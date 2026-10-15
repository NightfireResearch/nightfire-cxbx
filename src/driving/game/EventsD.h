#ifndef DRIVING_GAME_EVENTSD_H_
#define DRIVING_GAME_EVENTSD_H_

// ---------------------------------------------------------------------------------------------------------------
// The mission events from ESetRecordedGameTime to EWakeupSmackable (game.events, package D): the mission's settings
// and timers, the subtitles and the movies, the per-tick simulation events, the spawns (explosions, smackables,
// newtons, sentries, traffic), the scene objects' effects, the streams, and Event's own destructor.
//
// The event model is EventsA.h's: an event does its work when it is destroyed. Every vtable here has one slot, slot
// 0, the scalar deleting destructor (Delete), which runs the destructor (Destruct: the class's vtable, Process, then
// Event's vtable) and gives the bytes back to the event buffer. Each class's MakeEvent callback (Ghidra:
// GetEventName) news the event from its record and, for some, from what raised it (gEventDynamicData,
// world/Trigger.h); the ResolveEvent callbacks resolve a record's references when the track loads. EStartTimedAction,
// EStartTimer and EStopTimer work in their constructors; their destructors are Event's (other packages' scalar
// deleting destructors). See EventsD.cpp.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../EventManager.hpp"          // Event
#include "../data/Carp.h"               // CARP::AIElement, CARP::Instance
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "Sentry.h"                     // SentrySetup
#include "../platform/Ptr32.h"
#include "../../common/xbeClass.h"      // LAYOUT_32

class RSceneObj;
class UGroup;
class WTrigger;
struct ArticleEffect;
struct ObjectiveKind2Target;
struct ObjectiveMarker;
struct PhysicsObject;

// What the effect events' and EWakeupSmackable's third reference leads to (a CARP record; the name and the layout
// are ours): flags, and the scene object placed for it
struct EventSceneRef {
    enum Flag : uint32_t {
        kSmackable = 0x0200,            // EWakeupSmackable spawns a smackable of the scene object
        kNewton = 0x4000,               // ... else a newton of the instance, without the ray shell's damage
    };

    uint32_t unknown00;
    uint32_t flags;                     // +0x04 Flag
    RSceneObj *sceneObj;                // +0x08
};

// ---- the mission's settings

// The mission's end time: the simulation's time now, in seconds
class ESetRecordedGameTime : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x00044400
    ESetRecordedGameTime* Delete(unsigned flags);                                       // 0x00048ac0
};
LAYOUT_32(sizeof(ESetRecordedGameTime) == 4, "ESetRecordedGameTime is 4 bytes");

// The Simulation's reflective object: the AI element's object (its car, or its character's body), or the player's
class ESetReflectiveObject : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
    };

    CARP::AIElement *element;           // +0x04 the CARP element, not redirected; NULL: the player's
    void Process();
    void Destruct();                                                                    // 0x00044450
    ESetReflectiveObject* Delete(unsigned flags);                                       // 0x00048ae0
};
LAYOUT_32(sizeof(ESetReflectiveObject) == 8, "ESetReflectiveObject is 8 bytes");

// An instance's article variation: its scene object uses the model's instance list of that variation; without a
// scene object, the instance takes the first instance's article
class ESetRenderVariation : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::Instance> instance;
        uint32_t variation;
    };

    CARP::Instance *instance;           // +0x04
    uint32_t variation;                 // +0x08 -1: the list without an index
    void Process();
    void Destruct();                                                                    // 0x00044510
    ESetRenderVariation* Delete(unsigned flags);                                        // 0x00048b00
};
LAYOUT_32(sizeof(ESetRenderVariation) == 0xc, "ESetRenderVariation is 12 bytes");

// The scheduler's time scale, at most 4
class ESetSimRate : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float rate;
    };

    float rate;                         // +0x04
    void Process();
    void Destruct();                                                                    // 0x00044610
    ESetSimRate* Delete(unsigned flags);                                                // 0x00048b20
};
LAYOUT_32(sizeof(ESetSimRate) == 8, "ESetSimRate is 8 bytes");

// The weapons' special weapon
class ESetSpecialWeapon : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t weapon;
        Ptr32<const Coord3> trigger;
        float range;
    };

    int32_t weapon;                     // +0x04
    const Coord3 *trigger;              // +0x08 (the weapons' names)
    float range;                        // +0x0c
    void Process();
    void Destruct();                                                                    // 0x00044660
    ESetSpecialWeapon* Delete(unsigned flags);                                          // 0x00048b40
};
LAYOUT_32(sizeof(ESetSpecialWeapon) == 0x10, "ESetSpecialWeapon is 16 bytes");

// The player's car's roll direction (PVehicle's ForceRollDirection)
class ESetSubRollDirection : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t direction;
    };

    uint32_t direction;                 // +0x04 passed on as a byte
    void Process();
    void Destruct();                                                                    // 0x0004db10
    ESetSubRollDirection* Delete(unsigned flags);                                       // 0x0004ee80
};
LAYOUT_32(sizeof(ESetSubRollDirection) == 8, "ESetSubRollDirection is 8 bytes");

// A subtitle, with the subtitles on, unless a cinematic is being skipped or there is no HUD
class ESetSubtitle : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t text;
        float time;
    };

    uint32_t text;                      // +0x04
    float time;                         // +0x08
    void Process();
    void Destruct();                                                                    // 0x00044760
    ESetSubtitle* Delete(unsigned flags);                                               // 0x00048b60
};
LAYOUT_32(sizeof(ESetSubtitle) == 0xc, "ESetSubtitle is 12 bytes");

// The player's car's sound's terrain
class ESetTerrain : public Event {
public:
    struct Record {
        uint32_t unknown00;
        uint32_t terrain;
    };

    uint32_t terrain;                   // +0x04 its low half is stored
    void Process();
    void Destruct();                                                                    // 0x0004db70
    ESetTerrain* Delete(unsigned flags);                                                // 0x0004eea0
};
LAYOUT_32(sizeof(ESetTerrain) == 8, "ESetTerrain is 8 bytes");

// The car's visual damage, by percent: the AI element's car (a vehicle element whose object is a car), or the
// player's
class ESetVisualDamage : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        int32_t damage[4];
    };

    CARP::AIElement *element;           // +0x04 NULL: the player's car
    int32_t damage[4];                  // +0x08 percents, passed on as fractions
    void Process();
    void Destruct();                                                                    // 0x0004dbd0
    ESetVisualDamage* Delete(unsigned flags);                                           // 0x0004eec0
};
LAYOUT_32(sizeof(ESetVisualDamage) == 0x18, "ESetVisualDamage is 24 bytes");

// The HUD's waypoint (objective slot 1): the first of the three references there is, or none
class ESetWaypoint : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const ObjectiveMarker> marker;
        Ptr32<const ObjectiveKind2Target> target;
        Ptr32<CARP::AIElement> element;
    };

    const ObjectiveMarker *marker;      // +0x04
    const ObjectiveKind2Target *target;     // +0x08
    CARP::AIElement *element;           // +0x0c
    void Process();
    void Destruct();                                                                    // 0x00044c20
    ESetWaypoint* Delete(unsigned flags);                                               // 0x00048bc0
};
LAYOUT_32(sizeof(ESetWaypoint) == 0x10, "ESetWaypoint is 16 bytes");

// ---- the timers (the mission manager's ten)

// The timer reset to the seconds; shown on the HUD when it is run, if `show`
class ESetTimer : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t timer;
        float seconds;
        int32_t show;
    };

    int32_t timer;                      // +0x04
    float seconds;                      // +0x08
    int32_t show;                       // +0x0c
    ESetTimer* Construct(int timer, float seconds, int show);                           // 0x00044830
    void Process();
    void Destruct();                                                                    // 0x000448b0
    ESetTimer* Delete(unsigned flags);                                                  // 0x00048b80
};
LAYOUT_32(sizeof(ESetTimer) == 0x10, "ESetTimer is 16 bytes");

// (0xc bytes; vtable 0x0018c7fc, whose destructor is EForceNearPedRespawn's: Event's) The timer started when the
// event is made, ticking if it is timer 1
class EStartTimer : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t timer;
        int32_t rate;
    };

    int32_t timer;                      // +0x04
    int32_t rate;                       // +0x08 SMissionTimer::Start's
    EStartTimer* Construct(int timer, int rate);                                        // 0x00046290
};
LAYOUT_32(sizeof(EStartTimer) == 0xc, "EStartTimer is 12 bytes");

// (8 bytes; vtable 0x0018c80c, whose destructor is EBeginDownloadCode's: Event's) The timer stopped when the event
// is made
class EStopTimer : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t timer;
    };

    int32_t timer;                      // +0x04
    EStopTimer* Construct(int timer);                                                   // 0x00046620
};
LAYOUT_32(sizeof(EStopTimer) == 8, "EStopTimer is 8 bytes");

// (0x18 bytes; vtable 0x0018c7f8, whose destructor is EInsertObjective's: Event's) A timed action started when the
// event is made
class EStartTimedAction : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t action;
        float timeLimit;
        float fullPointsTime;
        int32_t minPoints;
        int32_t maxPoints;
    };

    int32_t action;                     // +0x04
    float timeLimit;                    // +0x08
    float fullPointsTime;               // +0x0c
    int32_t minPoints;                  // +0x10
    int32_t maxPoints;                  // +0x14
    EStartTimedAction* Construct(int action, float timeLimit, float fullPointsTime, int minPoints,
                                 int maxPoints);                                        // 0x000461b0
};
LAYOUT_32(sizeof(EStartTimedAction) == 0x18, "EStartTimedAction is 24 bytes");

class EStopTimedAction : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t action;
    };

    int32_t action;                     // +0x04
    void Process();
    void Destruct();                                                                    // 0x000465b0
    EStopTimedAction* Delete(unsigned flags);                                           // 0x00048d80
};
LAYOUT_32(sizeof(EStopTimedAction) == 8, "EStopTimedAction is 8 bytes");

class ETimerDrawOff : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t timer;
    };

    int32_t timer;                      // +0x04
    void Process();
    void Destruct();                                                                    // 0x00046ae0
    ETimerDrawOff* Delete(unsigned flags);                                              // 0x00048e40
};
LAYOUT_32(sizeof(ETimerDrawOff) == 8, "ETimerDrawOff is 8 bytes");

class ETimerDrawOn : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t timer;
    };

    int32_t timer;                      // +0x04
    void Process();
    void Destruct();                                                                    // 0x00046b60
    ETimerDrawOn* Delete(unsigned flags);                                               // 0x00048e60
};
LAYOUT_32(sizeof(ETimerDrawOn) == 8, "ETimerDrawOn is 8 bytes");

// ---- movies, streams and stingers

// A movie, if the movies are on: input, rumble and sound paused, every action queue flushed before and after,
// with its subtitles if it has them and the subtitles are on
class ESetVideo : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const char> movie;
        Ptr32<const char> subtitles;
    };

    const char *movie;                  // +0x04 under the locale's directory
    const char *subtitles;              // +0x08 under data\loading; NULL: none
    void Process();
    void Destruct();                                                                    // 0x00044970
    ESetVideo* Delete(unsigned flags);                                                  // 0x00048ba0
};
LAYOUT_32(sizeof(ESetVideo) == 0xc, "ESetVideo is 12 bytes");

// A stream's event, with the streams on: speech through the "SpeechVsAmbience" fader (unless a cinematic is being
// skipped), "nis" likewise at once, any other stream directly; nothing for a sound that failed to play before, or
// with `onlyAlive` once the player's health is gone
class EStreamEvent : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const char> stream;
        Ptr32<const char> name;
        int32_t fadeTime;
        int32_t loop;
        int32_t onlyAlive;
        int32_t held;
    };

    const char *stream;                 // +0x04
    const char *name;                   // +0x08
    int32_t fadeTime;                   // +0x0c
    int32_t loop;                       // +0x10
    int32_t onlyAlive;                  // +0x14
    int32_t held;                       // +0x18
    void Process();
    void Destruct();                                                                    // 0x0004ef60
    EStreamEvent* Delete(unsigned flags);                                               // 0x0004f150
};
LAYOUT_32(sizeof(EStreamEvent) == 0x1c, "EStreamEvent is 28 bytes");

// The stinger channel ("music" or "speech"; any other name is ignored)
class ESwitchStingerChannel : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const char> channel;
    };

    const char *channel;                // +0x04
    void Process();
    void Destruct();                                                                    // 0x00046990
    ESwitchStingerChannel* Delete(unsigned flags);                                      // 0x00048e00
};
LAYOUT_32(sizeof(ESwitchStingerChannel) == 8, "ESwitchStingerChannel is 8 bytes");

class EStopSoundPos : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const Coord3> position;
    };

    const Coord3 *position;             // +0x04 every sound exactly there is deleted
    void Process();
    void Destruct();                                                                    // 0x00046540
    EStopSoundPos* Delete(unsigned flags);                                              // 0x00048d60
};
LAYOUT_32(sizeof(EStopSoundPos) == 8, "EStopSoundPos is 8 bytes");

// ---- the player's car

class EShake : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0004dcb0
    EShake* Delete(unsigned flags);                                                     // 0x0004eee0
};
LAYOUT_32(sizeof(EShake) == 4, "EShake is 4 bytes");

class ETwoWheelsOff : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0004e9c0
    ETwoWheelsOff* Delete(unsigned flags);                                              // 0x0004f110
};
LAYOUT_32(sizeof(ETwoWheelsOff) == 4, "ETwoWheelsOff is 4 bytes");

class ETwoWheelsOn : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0004ea10
    ETwoWheelsOn* Delete(unsigned flags);                                               // 0x0004f130
};
LAYOUT_32(sizeof(ETwoWheelsOn) == 4, "ETwoWheelsOn is 4 bytes");

class EUnloadWeapon : public Event {
public:
    struct Record {
        uint32_t unknown00;
        int32_t weapon;
    };

    int32_t weapon;                     // +0x04
    void Process();
    void Destruct();                                                                    // 0x00046c20
    EUnloadWeapon* Delete(unsigned flags);                                              // 0x00048e80
};
LAYOUT_32(sizeof(EUnloadWeapon) == 8, "EUnloadWeapon is 8 bytes");

// The player's car put at the trigger, facing along it, and pushed along it at `speed`
class EStartMission : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float speed;
    };

    WTrigger *trigger;                  // +0x04 the event data's
    float speed;                        // +0x08
    void Process();
    void Destruct();                                                                    // 0x0004e860
    EStartMission* Delete(unsigned flags);                                              // 0x0004ef40
};
LAYOUT_32(sizeof(EStartMission) == 0xc, "EStartMission is 12 bytes");

// ---- the simulation's frame

// The simulation's frame: its step, the AI, the actors, the missions, the weapons, the cameras' input, the
// triggers, the grid and the audio's frame count
class ESimFrameUpdate : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x0004dd10
    ESimFrameUpdate* Delete(unsigned flags);                                            // 0x0004ef00
};
LAYOUT_32(sizeof(ESimFrameUpdate) == 4, "ESimFrameUpdate is 4 bytes");

// The Simulation's end of frame (an empty function)
class ESimEndFrame : public Event {
public:
    void Process();
    void Destruct();                                                                    // 0x00044df0
    ESimEndFrame* Delete(unsigned flags);                                               // 0x00048c20
};
LAYOUT_32(sizeof(ESimEndFrame) == 4, "ESimEndFrame is 4 bytes");

// ---- instances

// The instance that raised the event is drawn by its scene object no more (its flag 0x01 cleared)
class EShowInstance : public Event {
public:
    CARP::Instance *instance;           // +0x04 the event data's
    void Process();
    void Destruct();                                                                    // 0x00044d00
    EShowInstance* Delete(unsigned flags);                                              // 0x00048be0
};
LAYOUT_32(sizeof(EShowInstance) == 8, "EShowInstance is 8 bytes");

// The record's instance shown: its scene object's, else the instance among the raising scene object's; then its flag
// 0x01 cleared
class EShowInstanceStatic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::Instance> instance;
    };

    CARP::Instance *instance;           // +0x04
    RSceneObj *sceneObj;                // +0x08 the event data's
    void Process();
    void Destruct();                                                                    // 0x00044d40
    EShowInstanceStatic* Delete(unsigned flags);                                        // 0x00048c00
};
LAYOUT_32(sizeof(EShowInstanceStatic) == 0xc, "EShowInstanceStatic is 12 bytes");

// ---- the scene objects' effects

// What EStartEffect, EStopEffect and ESuppressEffect share (the base is ours; the three have the same layout and
// code but for the type they hand RSceneObj::TriggerFX): an effect by its id, else by its zone, on the scene object
// that raised the event, else the third reference's, else the owner of the body that raised it
class EEffectEvent : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<ArticleEffect> effect;
        Ptr32<void> unknown08;
        Ptr32<EventSceneRef> ref;
        int32_t zone;
    };

    ArticleEffect *effect;              // +0x04 NULL: by zone
    RSceneObj *sceneObj;                // +0x08 the event data's; NULL: found when the event runs
    void *unknown0c;                    // +0x0c a CARP reference (only tested)
    EventSceneRef *ref;                 // +0x10
    uint8_t bySimpleBody;               // +0x14 the event data's flag and index: the raising body
    uint8_t unknown15[3];
    int32_t bodySlot;                   // +0x18
    int32_t zone;                       // +0x1c

    void Trigger(int typeById, int typeByZone);
};
LAYOUT_32(sizeof(EEffectEvent) == 0x20, "the effect events are 32 bytes");

class EStartEffect : public EEffectEvent {
public:
    void Process();
    void Destruct();                                                                    // 0x00046050
    EStartEffect* Delete(unsigned flags);                                               // 0x00048d20
};

class EStopEffect : public EEffectEvent {
public:
    void Process();
    void Destruct();                                                                    // 0x00046380
    EStopEffect* Delete(unsigned flags);                                                // 0x00048d40
};

class ESuppressEffect : public EEffectEvent {
public:
    void Process();
    void Destruct();                                                                    // 0x00046720
    ESuppressEffect* Delete(unsigned flags);                                            // 0x00048da0
};

// One of the mission editor's switches of the AI element's car (PVehicle::MissionEditorSwitch)
class ESwitchEffectOff : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        int32_t which;
    };

    CARP::AIElement *element;           // +0x04
    int32_t which;                      // +0x08
    void Process();
    void Destruct();                                                                    // 0x00046850
    ESwitchEffectOff* Delete(unsigned flags);                                           // 0x00048dc0
};
LAYOUT_32(sizeof(ESwitchEffectOff) == 0xc, "ESwitchEffectOff is 12 bytes");

class ESwitchEffectOn : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        int32_t which;
    };

    CARP::AIElement *element;           // +0x04
    int32_t which;                      // +0x08
    void Process();
    void Destruct();                                                                    // 0x000468f0
    ESwitchEffectOn* Delete(unsigned flags);                                            // 0x00048de0
};
LAYOUT_32(sizeof(ESwitchEffectOn) == 0xc, "ESwitchEffectOn is 12 bytes");

// The target beacon of the AI element's vehicle on or off; for an element without one there to command, its flag
// 0x08
class ETargetBeaconOnOff : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::AIElement> element;
        int32_t on;
    };

    CARP::AIElement *element;           // +0x04
    int32_t on;                         // +0x08
    void Process();
    void Destruct();                                                                    // 0x00046a10
    ETargetBeaconOnOff* Delete(unsigned flags);                                         // 0x00048e20
};
LAYOUT_32(sizeof(ETargetBeaconOnOff) == 0xc, "ETargetBeaconOnOff is 12 bytes");

// ---- the spawns

// An explosion: its effect (the record's, the article effect's, or one picked by its kind and surface) placed at the
// position, on the raising scene object's instance if there is one; the jolt for the player, the explosion's
// physics object, a scorch on the ground, a light, an AI zone and, at most every 30 ticks for each of four slots, its
// two sounds. Also made by Simulation::SpawnExplosion and ESpawnExplosionStatic.
class ESpawnExplosion : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<PhysicsObject> source;    // a CARP reference
        uint32_t kind;
        uint32_t damageKind;
        float force;
        uint32_t surface;
        Ptr32<ArticleEffect> effect;
        uint32_t unknown1c;
        Ptr32<const char> effectName;
    };

    Coord4 position;                    // +0x04
    Coord4 direction;                   // +0x14
    RSceneObj *sceneObj;                // +0x24
    uint8_t bySimpleBody;               // +0x28 the body that raised it
    uint8_t unknown29[3];
    int32_t bodySlot;                   // +0x2c -1: none
    PhysicsObject *source;              // +0x30 the player's explosions get their own effects
    uint32_t kind;                      // +0x34 the explosion object's type
    uint32_t damageKind;                // +0x38 0x1e: "FODHitEffect"
    float force;                        // +0x3c
    uint32_t surface;                   // +0x40
    ArticleEffect *effect;              // +0x44 NULL: none
    uint32_t unknown48;
    const char *effectName;             // +0x4c NULL: picked

    ESpawnExplosion* Construct(Coord4 position, Coord4 direction, RSceneObj *sceneObj, uint8_t bySimpleBody,
                               int bodySlot, PhysicsObject *source, uint32_t kind, uint32_t damageKind,
                               float force, uint32_t surface, ArticleEffect *effect, uint32_t unknown48,
                               const char *effectName);                                 // 0x00044f40
    void Process();
    void Destruct();                                                                    // 0x0004de70
    ESpawnExplosion* Delete(unsigned flags);                                            // 0x0004ef20
};
LAYOUT_32(sizeof(ESpawnExplosion) == 0x50, "ESpawnExplosion is 0x50 bytes");
LAYOUT_32(offsetof(ESpawnExplosion, sceneObj) == 0x24 && offsetof(ESpawnExplosion, source) == 0x30 &&
          offsetof(ESpawnExplosion, effectName) == 0x4c, "ESpawnExplosion layout");

// An ESpawnExplosion at the record's position, or its instance's, facing (0, 0, 1)
class ESpawnExplosionStatic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<const Coord3> position;
        Ptr32<CARP::Instance> instance;
        uint32_t kind;
        uint32_t damageKind;
        float force;
        uint32_t surface;
        Ptr32<const char> effectName;
    };

    const Coord3 *position;             // +0x04 NULL: the instance's
    CARP::Instance *instance;           // +0x08
    uint32_t kind;                      // +0x0c
    uint32_t damageKind;                // +0x10
    float force;                        // +0x14
    uint32_t surface;                   // +0x18
    const char *effectName;             // +0x1c
    void Process();
    void Destruct();                                                                    // 0x00045120
    ESpawnExplosionStatic* Delete(unsigned flags);                                      // 0x00048c60
};
LAYOUT_32(sizeof(ESpawnExplosionStatic) == 0x20, "ESpawnExplosionStatic is 32 bytes");

// A force explosion (type 6): the article effect's GFX on the raising scene object's instance, or "Ex_Snow" or
// "Ex_Small" at the position
class ESpawnForceEffect : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float force;
        uint32_t surface;
        Ptr32<ArticleEffect> effect;
    };

    Coord4 position;                    // +0x04 the event data's
    Coord4 direction;                   // +0x14
    RSceneObj *sceneObj;                // +0x24
    float force;                        // +0x28
    uint32_t surface;                   // +0x2c 9: "Ex_Snow"
    ArticleEffect *effect;              // +0x30 NULL: none (the destructor points it at its own)

    ESpawnForceEffect* Construct(Coord4 position, Coord4 direction, RSceneObj *sceneObj, float force,
                                 uint32_t surface, ArticleEffect *effect);              // 0x00045330
    void Process();
    void Destruct();                                                                    // 0x000453a0
    ESpawnForceEffect* Delete(unsigned flags);                                          // 0x00048c80
};
LAYOUT_32(sizeof(ESpawnForceEffect) == 0x34, "ESpawnForceEffect is 0x34 bytes");

// A sentry on the instance, owned by the raising body
class ESpawnSentry : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<CARP::Instance> instance;
        SentrySetup setup;
        float hitPoints;
        uint32_t holdSteps;
        uint32_t scoreable;
    };

    CARP::Instance *instance;           // +0x04 NULL: none
    SentrySetup setup;                  // +0x08 (the sentry's, as are the next three)
    float hitPoints;                    // +0x34
    uint32_t holdSteps;                 // +0x38
    uint32_t scoreable;                 // +0x3c passed on as a flag
    uint8_t bySimpleBody;               // +0x40 the event data's flag and index
    uint8_t unknown41[3];
    int32_t bodySlot;                   // +0x44 -1: no owner

    ESpawnSentry* Construct(CARP::Instance *instance, SentrySetup setup, float hitPoints, uint32_t holdSteps,
                            uint32_t scoreable, uint8_t bySimpleBody, int bodySlot);    // 0x000457f0
    void Process();
    void Destruct();                                                                    // 0x00045880
    ESpawnSentry* Delete(unsigned flags);                                               // 0x00048ca0
};
LAYOUT_32(sizeof(ESpawnSentry) == 0x48, "ESpawnSentry is 0x48 bytes");

// A newton of the instance (Newton::SpawnFromEvent), thrown towards the view
class ESpawnDramaticSmackable : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float mass;
        Ptr32<CARP::Instance> instance;
        float lifetime;
    };

    float mass;                         // +0x04
    CARP::Instance *instance;           // +0x08
    float lifetime;                     // +0x0c
    uint8_t bySimpleBody;               // +0x10 the event data's flag, index and scene object
    uint8_t unknown11[3];
    int32_t bodySlot;                   // +0x14
    RSceneObj *sceneObj;                // +0x18
    void Process();
    void Destruct();                                                                    // 0x00044e80
    ESpawnDramaticSmackable* Delete(unsigned flags);                                    // 0x00048c40
};
LAYOUT_32(sizeof(ESpawnDramaticSmackable) == 0x1c, "ESpawnDramaticSmackable is 28 bytes");

// A newton of the instance (Newton::SpawnFromEvent)
class ESpawnSimplePhysics : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float mass;
        Ptr32<CARP::Instance> instance;
        float lifetime;
    };

    float mass;                         // +0x04
    CARP::Instance *instance;           // +0x08
    float lifetime;                     // +0x0c
    uint8_t bySimpleBody;               // +0x10 the event data's flag, index and scene object
    uint8_t unknown11[3];
    int32_t bodySlot;                   // +0x14
    RSceneObj *sceneObj;                // +0x18
    void Process();
    void Destruct();                                                                    // 0x000459b0
    ESpawnSimplePhysics* Delete(unsigned flags);                                        // 0x00048cc0
};
LAYOUT_32(sizeof(ESpawnSimplePhysics) == 0x1c, "ESpawnSimplePhysics is 28 bytes");

// A smackable of the model's article, placed at the raising scene object's instance (moving with it) or at the
// record's instance, given a random spin
class ESpawnSmackable : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<UGroup> model;
        uint32_t article;
        Ptr32<CARP::Instance> instance;
    };

    UGroup *model;                      // +0x04 NULL: none
    uint32_t article;                   // +0x08
    CARP::Instance *instance;           // +0x0c
    RSceneObj *sceneObj;                // +0x10 the event data's scene object and its instance
    uint32_t instanceIndex;             // +0x14
    void Process();
    void Destruct();                                                                    // 0x00045a70
    ESpawnSmackable* Delete(unsigned flags);                                            // 0x00048ce0
};
LAYOUT_32(sizeof(ESpawnSmackable) == 0x18, "ESpawnSmackable is 24 bytes");

// The first frozen traffic car of the twenty put at the trigger, facing along it, set to attack mode 12 and
// navigate mode 3, its damage and EMP cleared, and pushed along its heading at `speed`
class ESpawnTraffic : public Event {
public:
    struct Record {
        uint32_t unknown00;
        float speed;
    };

    WTrigger *trigger;                  // +0x04 the event data's
    float speed;                        // +0x08 0 or less: no push
    void Process();
    void Destruct();                                                                    // 0x00045d70
    ESpawnTraffic* Delete(unsigned flags);                                              // 0x00048d00
};
LAYOUT_32(sizeof(ESpawnTraffic) == 0xc, "ESpawnTraffic is 12 bytes");

// The scene object woken: a smackable (with the third reference's kSmackable) or a newton of the instance (with
// kNewton, moving as the raising body moved); a smackable a ray shell raised the event for is damaged if the
// shell's box meets the body's. When nothing is spawned, the trigger is enabled again.
class EWakeupSmackable : public Event {
public:
    struct Record {
        uint32_t unknown00;
        Ptr32<RSceneObj> sceneObj;
        Ptr32<CARP::Instance> instance;
        Ptr32<EventSceneRef> ref;
    };

    RSceneObj *sceneObj;                // +0x04 NULL: the reference's
    CARP::Instance *instance;           // +0x08
    EventSceneRef *ref;                 // +0x0c
    uint8_t hasRayShell;                // +0x10 the event data's from here on
    uint8_t unknown11[3];
    Coord3 rayStart;                    // +0x14
    Coord3 rayEnd;                      // +0x20
    float damage;                       // +0x2c the ray shell's value
    WTrigger *trigger;                  // +0x30
    uint8_t bySimpleBody;               // +0x34
    uint8_t unknown35[3];
    int32_t bodySlot;                   // +0x38

    EWakeupSmackable* Construct(RSceneObj *sceneObj, CARP::Instance *instance, EventSceneRef *ref,
                                uint8_t hasRayShell, Coord3 rayStart, Coord3 rayEnd, float damage, WTrigger *trigger,
                                uint8_t bySimpleBody, int bodySlot);                    // 0x00046c90
    void Process();
    void Destruct();                                                                    // 0x00046d10
    EWakeupSmackable* Delete(unsigned flags);                                           // 0x00048ea0
};
LAYOUT_32(sizeof(EWakeupSmackable) == 0x3c, "EWakeupSmackable is 0x3c bytes");
LAYOUT_32(offsetof(EWakeupSmackable, damage) == 0x2c && offsetof(EWakeupSmackable, bodySlot) == 0x38,
          "EWakeupSmackable layout");

// The ticks each of the explosions' four sound slots may play again at, the slot to try first, and the next of the
// three small explosion effects (EventsD.cpp; written only by ESpawnExplosion; the names are ours)
// The stinger's channel, "Speech" at first: ESwitchStingerChannel's, which EAddScoreAction reads (EventsD.cpp; the
// name is ours)
extern char StingerChannel[12];
extern int32_t ExplosionSoundDue[4];
extern uint32_t ExplosionSoundSlot;
extern uint32_t ExplosionEffectSlot;

// ---- the MakeEvent callbacks (RegisterEvent::LookupEvent's): a new event from the record. Ghidra names them
// GetEventName; the names are the AUF GameCube build's.

void ESetRecordedGameTime_MakeEvent_Callback(const void *record);                               // 0x00044430
void ESetReflectiveObject_MakeEvent_Callback(const ESetReflectiveObject::Record *record);       // 0x000444f0
void ESetRenderVariation_MakeEvent_Callback(const ESetRenderVariation::Record *record);         // 0x000445e0
void ESetSimRate_MakeEvent_Callback(const ESetSimRate::Record *record);                         // 0x00044640
void ESetSpecialWeapon_MakeEvent_Callback(const ESetSpecialWeapon::Record *record);             // 0x000446c0
void ESetSubRollDirection_MakeEvent_Callback(const ESetSubRollDirection::Record *record);       // 0x00044740
void ESetSubtitle_MakeEvent_Callback(const ESetSubtitle::Record *record);                       // 0x000447e0
void ESetTerrain_MakeEvent_Callback(const ESetTerrain::Record *record);                         // 0x00044810
void ESetTimer_MakeEvent_Callback(const ESetTimer::Record *record);                             // 0x00044910
// (events 0x4e and 0x4f)
void ESetVideo_MakeEvent_Callback(const ESetVideo::Record *record);                             // 0x00044bb0
void ESetVisualDamage_MakeEvent_Callback(const ESetVisualDamage::Record *record);               // 0x00044be0
void ESetWaypoint_MakeEvent_Callback(const ESetWaypoint::Record *record);                       // 0x00044cb0
void EShake_MakeEvent_Callback(const void *record);                                             // 0x00044ce0
void EShowInstance_MakeEvent_Callback(const void *record);                                      // 0x00044d20
void EShowInstanceStatic_MakeEvent_Callback(const EShowInstanceStatic::Record *record);         // 0x00044dc0
void ESimEndFrame_MakeEvent_Callback(const void *record);                                       // 0x00044e40
void ESimFrameUpdate_MakeEvent_Callback(const void *record);                                    // 0x00044e60
void ESpawnDramaticSmackable_MakeEvent_Callback(const ESpawnDramaticSmackable::Record *record); // 0x00044ef0
void ESpawnExplosion_MakeEvent_Callback(const ESpawnExplosion::Record *record);                 // 0x00044fe0
void ESpawnExplosionStatic_MakeEvent_Callback(const ESpawnExplosionStatic::Record *record);     // 0x00045250
void ESpawnForceEffect_MakeEvent_Callback(const ESpawnForceEffect::Record *record);             // 0x00045720
// (events 0x94 and 0x97)
void ESpawnSentry_MakeEvent_Callback(const ESpawnSentry::Record *record);                       // 0x00045940
void ESpawnSimplePhysics_MakeEvent_Callback(const ESpawnSimplePhysics::Record *record);         // 0x00045a20
void ESpawnSmackable_MakeEvent_Callback(const ESpawnSmackable::Record *record);                 // 0x00045cb0
void ESpawnTraffic_MakeEvent_Callback(const ESpawnTraffic::Record *record);                     // 0x00046020
void EStartEffect_MakeEvent_Callback(const EEffectEvent::Record *record);                       // 0x00046120
void EStartMission_MakeEvent_Callback(const EStartMission::Record *record);                     // 0x00046180
// (events 0xaf and 0xb1)
void EStartTimedAction_MakeEvent_Callback(const EStartTimedAction::Record *record);             // 0x00046230
void EStartTimer_MakeEvent_Callback(const EStartTimer::Record *record);                         // 0x00046320
void EStopEffect_MakeEvent_Callback(const EEffectEvent::Record *record);                        // 0x00046450
// (events 0xa6 and 0xa7)
void EStopSoundPos_MakeEvent_Callback(const EStopSoundPos::Record *record);                     // 0x00046590
void EStopTimedAction_MakeEvent_Callback(const EStopTimedAction::Record *record);               // 0x00046600
void EStopTimer_MakeEvent_Callback(const EStopTimer::Record *record);                           // 0x00046680
void EStreamEvent_MakeEvent_Callback(const EStreamEvent::Record *record);                       // 0x000466d0
// (events 0xad and 0xae)
void ESuppressEffect_MakeEvent_Callback(const EEffectEvent::Record *record);                    // 0x000467f0
void ESwitchEffectOff_MakeEvent_Callback(const ESwitchEffectOff::Record *record);               // 0x000468c0
void ESwitchEffectOn_MakeEvent_Callback(const ESwitchEffectOn::Record *record);                 // 0x00046960
void ESwitchStingerChannel_MakeEvent_Callback(const ESwitchStingerChannel::Record *record);     // 0x000469f0
void ETargetBeaconOnOff_MakeEvent_Callback(const ETargetBeaconOnOff::Record *record);           // 0x00046ab0
void ETimerDrawOff_MakeEvent_Callback(const ETimerDrawOff::Record *record);                     // 0x00046b40
void ETimerDrawOn_MakeEvent_Callback(const ETimerDrawOn::Record *record);                       // 0x00046bc0
void ETwoWheelsOff_MakeEvent_Callback(const void *record);                                      // 0x00046be0
void ETwoWheelsOn_MakeEvent_Callback(const void *record);                                       // 0x00046c00
void EUnloadWeapon_MakeEvent_Callback(const EUnloadWeapon::Record *record);                     // 0x00046c70
void EWakeupSmackable_MakeEvent_Callback(const EWakeupSmackable::Record *record);               // 0x00047230

// ---- the ResolveEvent callbacks (RegisterEvent::ResolveEvent's): the record's references resolved when the track
// loads. The names are the AUF GameCube build's pattern; Ghidra has them unnamed.

// The linker's one copy for every record whose reference is its third word (ESetSpecialWeapon, ESpawnSimplePhysics,
// ESpawnDramaticSmackable, ENuclearBlast)
void ESetSpecialWeapon_ResolveEvent_Callback(ESetSpecialWeapon::Record *record, UGroup *shared,
                                             UGroup *parent);                                   // 0x000446f0
void ESpawnExplosion_ResolveEvent_Callback(ESpawnExplosion::Record *record, UGroup *shared,
                                           UGroup *parent);                                     // 0x00045090
void ESpawnExplosionStatic_ResolveEvent_Callback(ESpawnExplosionStatic::Record *record, UGroup *shared,
                                                 UGroup *parent);                               // 0x000452a0
void ESpawnForceEffect_ResolveEvent_Callback(ESpawnForceEffect::Record *record, UGroup *shared,
                                             UGroup *parent);                                   // 0x000457a0
void ESpawnSmackable_ResolveEvent_Callback(ESpawnSmackable::Record *record, UGroup *shared,
                                           UGroup *parent);                                     // 0x00045d00
// The linker's one copy for every record whose references are its second, third and fourth words (11 entries of the
// table: the effect events, EWakeupSmackable, EPlayEffect, EPlaySoundPos, ESetObjective, ESetWaypoint, EPathSFX)
void EStopEffect_ResolveEvent_Callback(EEffectEvent::Record *record, UGroup *shared, UGroup *parent);  // 0x000464b0


#endif // DRIVING_GAME_EVENTSD_H_
