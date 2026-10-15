#ifndef DRIVING_GAME_SENTRY_H_
#define DRIVING_GAME_SENTRY_H_

#include <stddef.h>
#include "../Vtables.h"   // our copies of the game's vtables
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../physics/PhysicsObject.h"

// ---------------------------------------------------------------------------------------------------------------
// Sentry (0xc8 bytes, vtable 0x00190034: PhysicsObject's seven slots): a "sentry" PhysicsObject of type 11 on a
// simple body (kSimpleSentry), drawn by the scene object of the instance it is placed at. It turns towards its
// target (the player's object unless it is given one) in two ways - its body about one axis (the SRB rotation), its
// aim animation's frame (the anim rotation) - and fires its fire animation while both are on target and the target
// is in range. An ASentry sound plays the turning and the firing. The static muzzle flashes (eight, drawn and faded
// by the world camera) are the events' (ESentryMuzzleFlash). See Sentry.cpp.
// ---------------------------------------------------------------------------------------------------------------

struct WTargetable;
namespace CARP {
class Instance;
}

// A muzzle flash (0x20 bytes)
struct SentryMuzzleFlash {
    Coord4 position;                // +0x00
    Coord3 direction;               // +0x10
    float size;                     // +0x1c 5 when added, a third smaller at each draw; gone below 0.1
};
static_assert(sizeof(SentryMuzzleFlash) == 0x20, "a sentry's muzzle flash is 32 bytes");

constexpr int kSentryMuzzleFlashes = 8;

// Sentry::Construct's arguments from the second to the twelfth, as ESpawnSentry's record holds them
struct SentrySetup {
    int32_t srbAxis;
    float srbSpeed;
    float srbLimit;
    int32_t animAxis;
    float animSpeed;
    float animLimit;
    uint32_t aimSystem;
    uint32_t fireSystem;
    uint32_t shots;
    float shotSeconds;
    float range;
};
static_assert(sizeof(SentrySetup) == 0x2c, "a sentry's setup is 11 words");

class Sentry : public PhysicsObject {
public:
    // The axes the rotations turn about (and ComputeTargetAngle's)
    enum Axis : int32_t {
        kAxisX = 0,
        kAxisY = 1,                 // a turn about it is sideways (ASceneObj::turningSideways), else up and down
        kAxisZ = 2,
    };

    int32_t srbAxis;                // +0x6c Axis
    int32_t animAxis;               // +0x70 Axis
    float srbRate;                  // +0x74 turns a step
    float animRate;                 // +0x78
    float srbLimit;                 // +0x7c ComputeNewRotation's limit
    float animLimit;                // +0x80 the constructor's (absolute) value
    float rangeSq;                  // +0x84 the range squared; 0: it neither aims nor turns at the start
    float hitPoints;                // +0x88
    float inverseHitPoints;         // +0x8c 1 / the first hit points
    float srbAngle;                 // +0x90 turns
    float animAngle;                // +0x94 turns
    float animFrames;               // +0x98 the aim animation's frames (less 60 if more); -1 until RotateAnim counts
    uint8_t animReversed;           // +0x9c the constructor's anim limit was negative: the frame runs with the angle
    uint8_t scoreable;              // +0x9d its hits and kill count for the mission
    uint8_t unknown9e[2];
    uint32_t shotsLeft;             // +0xa0 shots with stimulus 0xb, counted down
    uint32_t aimSystem;             // +0xa4 the aim animation's system id
    uint32_t fireSystem;            // +0xa8 the fire animation's system id
    int32_t aimInstance;            // +0xac the aim system's best instance (where the target is measured from)
    uint32_t holdSteps;             // +0xb0 the steps a target must be held before firing (names ours)
    uint32_t holdCountdown;         // +0xb4 counted down while on target, back up while out of range
    int32_t shotInterval;           // +0xb8 steps between shots
    int32_t nextShotStep;           // +0xbc
    uint32_t targetSig;             // +0xc0 its target's signature; 0: the player's, taken at the next Simulate
    WTargetable *targetable;        // +0xc4 the sentry as a target, registered once its aim system is in state 2

    Sentry* Construct(CARP::Instance *instance, int srbAxis, float srbSpeed, float srbLimit, int animAxis,
                      float animSpeed, float animLimit, uint32_t aimSystem, uint32_t fireSystem,
                      uint32_t shots, float shotSeconds, float range, uint32_t targetSig, float hitPoints,
                      uint32_t holdSteps, uint8_t scoreable);                                      // 0x00073a90
    void Destruct();                                                                            // 0x00073150

    // A new temporary target of the target's object; NULL if there is none
    WTargetable* GetTarget();                                                                   // 0x00073330
    void Shoot();                                                                               // 0x000733f0
    // The angle in turns, 0 to 1, between `direction` and `axis` with both flattened along `which`; the other way
    // round when `side`'s component along `which` is positive. Left unrounded on the x87.
    double ComputeTargetAngle(const Coord4 *axis, const Coord4 *direction, const Coord4 *side,
                              int which);                                                       // 0x000731e0
    // `current` turned by at most `rate` the shorter way towards `target`, kept out of (limit, 1 - limit). Left
    // unrounded on the x87.
    double ComputeNewRotation(float current, float target, float rate, float limit);           // 0x00073600
    // A step of the rotation towards `target`; true once within 0.01 of it
    bool RotateSRB(float target);                                                               // 0x00073730
    bool RotateAnim(float target);                                                              // 0x000738b0
    // Turns towards the target; true when both rotations are on it. `inRange`: the target is in range.
    bool TrackTarget(bool *inRange);                                                            // 0x00074280

    // The muzzle flashes
    static void AddMuzzleFlash(const Coord4 *position, const Coord3 *direction);               // 0x000734a0
    static void DrawMuzzleFlashes();                                                            // 0x00073500

    // ---- virtual (the slot in the vtable)
    Sentry* Delete(unsigned flags);                                                             // 0  0x000735d0
    // Answers 0x10
    int ApplyDamage(const Coord3 *from, const Coord3 *to, float amount, float split, int kind,
                    const uint32_t *sourceSig);                                                 // 2  0x00073f80
    void Simulate();                                                                            // 4  0x000744d0
};
LAYOUT_32(sizeof(Sentry) == 0xc8, "a sentry is 0xc8 bytes");
LAYOUT_32(offsetof(Sentry, srbLimit) == 0x7c && offsetof(Sentry, hitPoints) == 0x88 &&
          offsetof(Sentry, srbAngle) == 0x90 && offsetof(Sentry, animReversed) == 0x9c &&
          offsetof(Sentry, shotsLeft) == 0xa0 && offsetof(Sentry, holdSteps) == 0xb0 &&
          offsetof(Sentry, targetSig) == 0xc0 && offsetof(Sentry, targetable) == 0xc4, "Sentry layout");

void **const kSentryVtable = Vtable_Sentry;

extern SentryMuzzleFlash SentryMuzzleFlashes[kSentryMuzzleFlashes];
extern int32_t SentryMuzzleFlashCount;

#endif // DRIVING_GAME_SENTRY_H_
