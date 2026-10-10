#ifndef DRIVING_GAME_HELICOPTER_H_
#define DRIVING_GAME_HELICOPTER_H_

#include <stddef.h>
#include <stdint.h>

#include "Vehicle.h"                    // DamageZone
#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3
#include "../physics/PhysicsObject.h"

// ---------------------------------------------------------------------------------------------------------------
// PHelicopter (0x140 bytes, vtable 0x0018f8bc: PhysicsObject's seven slots): a "pvehicle" PhysicsObject of type 10
// on a simple body (kSimpleHelicopter) - an RVehicle model, an AHelicopter sound and 16 damage zones. The AI flies it
// through the four controls (or, with heliClass 0, the action queue does), and Simulate turns them into the body's
// acceleration and orientation. See Helicopter.cpp.
// ---------------------------------------------------------------------------------------------------------------

class ActionQueue;
struct WTargetable;

// AIHelicopter (not ported): what PHelicopter uses of it, under Ghidra's names. Provisional, until the AI's own
// class replaces it.
struct HelicopterAI {
    uint8_t unknown00[0x84];
    int32_t navigateMode;           // +0x84
    int32_t targetMode;             // +0x88
    int32_t attackMode;             // +0x8c
    uint8_t unknown90[8];
    WTargetable *targetBeacon;      // +0x98
};
static_assert(offsetof(HelicopterAI, attackMode) == 0x8c && offsetof(HelicopterAI, targetBeacon) == 0x98,
              "AIHelicopter layout");

struct PHelicopter : PhysicsObject {
    int32_t heliClass;              // +0x6c 1 from the constructor; 0: Simulate reads the controls from actionQueue
    float roll;                     // +0x70 eased towards controlGas
    float unknown74;                // +0x74 eased towards controlStrafe
    float unknown78;                // +0x78 1 from the constructor
    Coord3 targetPos;               // +0x7c the constructor's position
    Coord3 destPos;                 // +0x88 the same
    float controlGas;               // +0x94
    float controlStrafe;            // +0x98
    float controlSteer;             // +0x9c
    float controlAltitude;          // +0xa0
    ActionQueue *actionQueue;       // +0xa4 freed by the destructor
    uint8_t unknownA8;              // +0xa8 1: Simulate flies it
    uint8_t unknownA9[3];
    DamageZone damageZones[kDamageZoneCount];   // +0xac
    int32_t sleepStep;              // +0x12c the step after which Simulate stops it (name ours); 0 none
    float damageScale;              // +0x130 32 / hit points, set by the first ApplyDamage; -1 before
    HelicopterAI *ai;               // +0x134
    bool scoreable;                 // +0x138 its hits and kill count for the mission
    uint8_t proximityDestructEnabled;   // +0x139 PROXIMITY_DESTRUCT (LookupBool's byte, kept raw)
    uint8_t unknown13A[2];
    float destructDistance;         // +0x13c DESTRUCT_DIST, 2 without one

    // `direction` times `speed` is its first velocity
    PHelicopter* Construct(const char *name, float speed, Coord3 direction, Coord3 position);   // 0x0006d8f0
    void Destruct();                                                                            // 0x0006e290
    // The controls from the action queue's actions (the four DEBUGACTION_AXIS* ids)
    void GetControllerInput();                                                                  // 0x0006dc30

    // ---- virtual (the slot in the vtable)
    PHelicopter* Delete(unsigned flags);                                                        // 0  0x0006e300
    // Damage where the segment `from`-`to` meets its box: 0 if it misses, else 0x10, or 0x70 (half the time, in
    // some zones)
    int ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                    const uint32_t *sourceSig);                                                 // 2  0x0006e330
    DamageZone* GetDamageZones(uint32_t *count);                                                // 3  0x0006dd10
    void Simulate();                                                                            // 4  0x0006dd30
};
static_assert(sizeof(PHelicopter) == 0x140, "a helicopter is 0x140 bytes");
static_assert(offsetof(PHelicopter, targetPos) == 0x7c && offsetof(PHelicopter, controlGas) == 0x94 &&
              offsetof(PHelicopter, actionQueue) == 0xa4 && offsetof(PHelicopter, damageZones) == 0xac &&
              offsetof(PHelicopter, sleepStep) == 0x12c && offsetof(PHelicopter, ai) == 0x134 &&
              offsetof(PHelicopter, destructDistance) == 0x13c, "PHelicopter layout");

constexpr uint32_t kPHelicopterVtable = 0x0018f8bc;

#endif // DRIVING_GAME_HELICOPTER_H_
