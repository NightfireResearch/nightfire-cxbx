#ifndef DRIVING_WORLD_TRIGGERMANAGER_H_
#define DRIVING_WORLD_TRIGGERMANAGER_H_

#include <stddef.h>
#include <stdint.h>

#include "CollisionInstance.h"   // WTrigger
#include "CollisionTypes.h"
#include "Trigger.h"
#include "../data/Carp.h"         // CARP::Instance
#include "../data/UData.h"
#include "../render/RPathHandle.hpp"

// ---------------------------------------------------------------------------------------------------------------
// WTriggerManager (one, fgTriggerManager): the track's triggers ('Trig' data, copied at Init so that Restart can put
// them back as they were) and, every frame, the tests of what touches them - the awake rigid bodies, the simple
// bodies, the path engine's instances and the queued ray shells, each against the triggers in the grid cells round
// it. See TriggerManager.cpp; the triggers' own methods are in Trigger.cpp.
// ---------------------------------------------------------------------------------------------------------------

// The physics objects the tests read: only the fields read here (the physics is not ported).
struct RigidBodyInfo {
    MATRIX4 orientation;        // +0x000 the OBB's axes
    uint8_t unknown040[0x480];
    Coord4 halfExtents;         // +0x4c0
};

struct RigidBody {
    uint8_t unknown00[0x10];
    Coord3 position;            // +0x10
    uint32_t unknown1c;
    Coord3 velocity;            // +0x20
    uint8_t unknown2c[0x30];
    RigidBodyInfo *info;        // +0x5c
    uint8_t unknown60[0xd];
    uint8_t unknown6d;          // +0x6d 1, 2 or another: the trigger flag it touches
    uint8_t sleepState;         // +0x6e 2: Update tests it
    uint8_t unknown6f[0xd];
    float radius;               // +0x7c
};
static_assert(sizeof(RigidBody) == 0x80, "a rigid body is 128 bytes");
static_assert(offsetof(RigidBody, info) == 0x5c && offsetof(RigidBody, unknown6d) == 0x6d, "rigid body layout");

struct SimpleRigidBody {
    enum Flag : uint16_t {
        kTouchesTriggers = 0x0100,  // tested against the triggers (the name is ours)
    };

    uint8_t unknown00[0x10];
    Coord3 position;            // +0x10
    uint8_t bodyType;           // +0x1c
    int8_t ownerIndex;          // +0x1d
    uint16_t flags;             // +0x1e Flag
    Coord3 velocity;            // +0x20
    float radius;               // +0x2c
    uint8_t unknown30[0x10];
};
static_assert(sizeof(SimpleRigidBody) == 0x40, "a simple rigid body is 64 bytes");
static_assert(offsetof(SimpleRigidBody, flags) == 0x1e && offsetof(SimpleRigidBody, radius) == 0x2c,
              "simple rigid body layout");

class WTriggerManager {
public:
    int count;                  // +0x00
    WTrigger *triggers;         // +0x04 the track's data, put back by Restart
    bool active;                // +0x08 set by Restart, cleared by EPlayerWin and EPlayerLose (kWhileActive)
    uint8_t unknown09[3];

    static void Init(UData *triggerData);                                                     // 0x000d03b0
    static void Restart();                                                                     // 0x000cff90
    void Update();                                                                             // 0x000d0c90

    // What touches a trigger. A segment (two Coord4s) of a ray shell with the given radius
    bool CheckCollide(const Coord4 *segment, float radius, WTrigger *trigger);                  // 0x000cf520
    bool CheckCollide(RigidBody *body, WTrigger *trigger);                                      // 0x000cfae0
    bool CheckCollide(SimpleRigidBody *body, WTrigger *trigger);                                // 0x000cfdf0
    // An upright cylinder from point.y - radius to point.y + radius
    bool CheckCollide(const Coord3 *point, float radius, WTrigger *trigger);                    // 0x000cfeb0
    // An upright cylinder from point.y - below to point.y + above
    bool CheckCollide(const Coord3 *point, float radius, float above, float below, WTrigger *trigger);   // 0x000cff20

    void Process(int index, RigidBody *body);                                                  // 0x000d04a0
    void Process(CARP::Instance *instance);                                                    // 0x000d0630
    void Process(int index, SimpleRigidBody *body);                                            // 0x000d0810
    void Process(int rayShell);                                                                // 0x000d0a30

    bool Considers(WTrigger *trigger, unsigned touch);   // inlined in the game
};
static_assert(sizeof(WTriggerManager) == 0xc, "the trigger manager is 12 bytes");

#define fgTriggerManager (*(WTriggerManager **)0x0023e260)

#endif // DRIVING_WORLD_TRIGGERMANAGER_H_
