#ifndef DRIVING_GAME_SHELL_H_
#define DRIVING_GAME_SHELL_H_

// ---------------------------------------------------------------------------------------------------------------
// The guns' rounds (game tier). See Shell.cpp.
//   RayShell (0xe0 bytes, vtable 0x0018ffe4: its scalar deleting destructor only): an instant shot. The
//     constructor does all of it - the objects along the ray, then the world, in up to `passes` passes (the later
//     ones perturbed, from just short of the last hit) - damages what it hit, lays the tracer, queues the ray for
//     the triggers, and deletes itself. The tracers, the muzzle flashes' lights and the trigger queue are static
//     tables.
//   Shell (0x90 bytes, "Shell", physics object type 7, vtable 0x0019006c): a moving round on a simple body
//     (kSimpleShell): each step, the first body it touches or the world along its step takes the hit, and the
//     shell is deleted; its lifetime runs out otherwise. ApplyWorldEffects (the impact's particles, incidental
//     effect and sound) and ApplyWorldDamage (the scorch mark, the scene object's stimuli) serve both, the missiles
//     and the EMP bolts.
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeClass.h"      // offsetof on a derived class without clang's warning
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../physics/PhysicsObject.h"
#include "../world/CollisionTypes.h"    // WorldCollisionInfo
#include "../world/Trigger.h"           // ActiveRayShell

class RSceneObj;
class WWorldPos;
struct ArticleEffect;
struct RParticleSystemData;

// ---- RayShell

// What the ray met in an object: where it entered the object's sphere, the point on the object it struck, whose,
// and how far along the ray (0x30 bytes)
struct RayShellVictim {
    Coord3 hitPoint;                    // +0x00 where the ray meets the object's sphere
    uint32_t unknown0c;
    Coord3 impactPoint;                 // +0x10 on the object: its box, or its sphere's surface
    uint32_t sig;                       // +0x1c the object's signature
    float distance;                     // +0x20 from the origin to hitPoint
    uint8_t unknown24[0xc];
};
static_assert(sizeof(RayShellVictim) == 0x30, "a ray shell victim is 0x30 bytes");

// A tracer: the streak from the muzzle to where the shot ended, drawn for 60 steps (0x20 bytes)
struct RayShellTracer {
    Coord3 start;                       // +0x00 the muzzle (moved to the gun's effect each draw, when it has one)
    uint32_t time;                      // +0x0c the simulation step it was laid
    Coord3 end;                         // +0x10
    int32_t kind;                       // +0x1c BulletStreakKinds' index
};
static_assert(sizeof(RayShellTracer) == 0x20, "a tracer is 0x20 bytes");

// The scene object and effect a tracer starts from (8 bytes)
struct RayShellTracerSource {
    RSceneObj *object;                  // +0x00 NULL: the tracer keeps its start
    const ArticleEffect *effect;        // +0x04
};
static_assert(sizeof(RayShellTracerSource) == 8, "a tracer source is 8 bytes");

// A shooter's muzzle flash, lit at the next draw (0x10 bytes)
struct RayShellFlash {
    Coord3 position;                    // +0x00 the shot's origin
    uint32_t ownerSig;                  // +0x0c the shooter's signature: one flash each
};
static_assert(sizeof(RayShellFlash) == 0x10, "a flash is 0x10 bytes");

constexpr int kRayShellTracers = 64;
constexpr int kRayShellFlashes = 8;
constexpr int kActiveRayShells = 64;

class RayShell {
public:
    void **vtable;                      // +0x00
    uint8_t unknown04[0xc];
    Coord4 origin;                      // +0x10 the ray's start (w not used)
    Coord3 position;                    // +0x20 the first pass's origin
    float damage;                       // +0x2c
    Coord4 direction;                   // +0x30 unit (w not used)
    Coord3 startDirection;              // +0x40 the first pass's direction
    float range;                        // +0x4c at most 220; 3 for the later passes
    Coord3 muzzle;                      // +0x50 where the tracers start
    int32_t shellType;                  // +0x5c
    int32_t numVictims;                 // +0x60 0-2: the nearest objects hit, nearest first
    uint32_t ownerSig;                  // +0x64 the shooter's signature
    uint8_t unknown68[8];
    RayShellVictim victims[2];          // +0x70
    int32_t streakKind;                 // +0xd0 the tracer's (negative: none)
    RSceneObj *muzzleObject;            // +0xd4 the tracers' source
    const ArticleEffect *muzzleEffect;  // +0xd8
    uint32_t unknownDC;

    // RayShell::RayShell: the whole shot, then the shell deletes itself (through its vtable) and answers itself
    RayShell* Construct(int shellType, uint32_t ownerSig, const Coord3 *origin, const Coord3 *direction,
                        const Coord3 *muzzle, RSceneObj *muzzleObject, const ArticleEffect *muzzleEffect,
                        float range, float damage, int passes, float spread, int streakKind);   // 0x00072e90
    RayShell* Delete(unsigned flags);   // the scalar deleting destructor, vtable slot 0         // 0x00072160

    // The nearest two objects whose spheres (a human's, shot by the player, stretched; a rigid body's box, which
    // must also overlap the ray) the ray meets, not the shooter's own; true if any
    bool CollideWithObjects();                                                                  // 0x00072190
    // An object met at `distance`: the nearest, else the second - a human with its unknown8c set (the test that
    // would take any other, nearer than the nearest and within its radius behind it, never passes)
    void ConsiderVictim(uint32_t sig, const Coord3 *hitPoint, const Coord3 *impactPoint,
                        float distance);                                                        // 0x00071e40
    // The world along the ray in steps of a quarter of the range (55 at most), up to the nearest victim: at the
    // first hit nearer than *distance (or with no victim) the impact's effects and damage, the tracer and the flash;
    // *distance becomes the hit's. True on a hit.
    bool CollideWithWorld(float *distance);                                                     // 0x00072790
    // The victims' damage, or the tracer to the full range: *distance is how far the shot went
    void Fire(float *distance);                                                                 // 0x00072d60
    // The object with the signature hit at hitPoint: its damage, the impact's effects at impactPoint, the tracer,
    // the flash; the player's shots add to a car's damage by the player
    void DamagePhysicsObject(uint32_t sig, const Coord3 *hitPoint, const Coord3 *impactPoint);  // 0x00072ba0
    // The ray, `distance` long, queued for the triggers (63 at most a step)
    void AddNewRayShellForTriggerCheck(float distance);                                         // 0x00071d80
    // A flash at `position` for a human's or a sentry's shot (not the player's), one per shooter, eight at most.
    // It does not read the shell.
    void AddFlash(uint32_t ownerSig, const Coord3 *position);                                   // 0x000720d0

    // A tracer of `kind` (none if negative) from start to end, from the effect of the object (if any); kind 0
    // starts two units along when the ends are more than two apart
    static void StaticStoreTracer(const Coord3 *start, const Coord3 *end, int kind, RSceneObj *object,
                                  const ArticleEffect *effect);                                 // 0x00071fb0
    // The tracers laid before step `before` dropped, the last moved into each gap
    static void ClearTracers(uint32_t before);                                                  // 0x000719a0
    // Each tracer's streak, moving along it from the start by its kind's speed (by the square of its age when its
    // start follows a gun); dropped once the streak passes the end, and after 60 steps
    static void DrawTracers();                                                                  // 0x00071a60
    // The oldest flash lit as a positional light, and taken off the list
    static void DrawFlashes();                                                                  // 0x00071c80
    // Every tracer, flash and queued ray dropped
    static void Reset();                                                                        // 0x00071d40

    // ---- the trigger queue (WTriggerManager's)
    static int GetNumActiveRayShells();                                                         // 0x00071930
    static ActiveRayShell* GetActiveRayShell(int index);                                        // 0x00071940
    static void ClearActiveRayShells();                                                         // 0x00071960
    static void SetTriggerHittingRayShell(int index);   // -1: none                              // 0x00071970
    static ActiveRayShell* GetTriggerHittingRayShell(); // NULL: none                            // 0x00071980
};
static_assert(sizeof(RayShell) == 0xe0, "a ray shell is 0xe0 bytes");
static_assert(offsetof(RayShell, origin) == 0x10 && offsetof(RayShell, damage) == 0x2c &&
              offsetof(RayShell, direction) == 0x30 && offsetof(RayShell, range) == 0x4c &&
              offsetof(RayShell, shellType) == 0x5c && offsetof(RayShell, ownerSig) == 0x64 &&
              offsetof(RayShell, victims) == 0x70 && offsetof(RayShell, streakKind) == 0xd0, "RayShell layout");

// The tables (names ours)
extern RayShellTracerSource RayShellTracerSources[kRayShellTracers];
extern int32_t RayShellTracerCount;
extern RayShellTracer RayShellTracers[kRayShellTracers];
extern int32_t RayShellFlashCount;
extern int32_t ActiveRayShellCount;
extern int32_t RayShellExplosionStep;           // the step of the last cheat explosion
extern RayShellFlash RayShellFlashes[kRayShellFlashes];
extern ActiveRayShell ActiveRayShells[kActiveRayShells];
extern int32_t TriggerHittingRayShell;

// ---- Shell

class Shell : public PhysicsObject {
public:
    int32_t shellType;                  // +0x6c 0x10: slower, and lives a second
    float damage;                       // +0x70
    float explosionSize;                // +0x74 above 1: an explosion where it hits
    int32_t stepsLeft;                  // +0x78
    uint32_t unknown7c;
    Coord3 origin;                      // +0x80 where it was fired from
    int32_t streakKind;                 // +0x8c its speed is the kind's (negative: random)

    // Shell::Shell: fired from `origin` along `direction` (a unit vector) by the owner with the signature
    Shell* Construct(int shellType, const Coord3 *direction, const Coord3 *origin, uint32_t ownerSig, float damage,
                     float explosionSize, int streakKind);                                      // 0x00074770
    void Destruct();                                                                            // 0x00074660
    Shell* Delete(unsigned flags);      // the scalar deleting destructor, vtable slot 0         // 0x00074980
    // Vtable slot 4
    void Simulate();                                                                            // 0x00074e80

    // The scorch mark for a world hit (not on a hit flagged flag52, a broken window or a face of sub-type 2), and
    // stimuli 0x13-0x16 by the damage (10, 50, 250, 500) for the scene object of the face's instance
    static void ApplyWorldDamage(float damage, WWorldPos *place, float scorchSize, int scorchType,
                                 const WorldCollisionInfo *info);                               // 0x00074670
    // An impact at `position` against `normal` (its w counts in the test for straight up): the sparks (not on
    // snow, nor from the player's car when its physics' subPhysics is set), the incidental effect for the face,
    // the shooter or what was hit, the AI's zone 3, and the impact or ricochet sound. `damageResult` is the
    // victim's ApplyDamage answer (0x80: "Henbult"). The third argument is not read.
    static void ApplyWorldEffects(const Coord3 *position, const Coord4 *normal, int unused, int shellType,
                                  const uint8_t *faceType, int damageResult, PhysicsObject *object);  // 0x000749b0
};
static_assert(sizeof(Shell) == 0x90, "a shell is 0x90 bytes");
static_assert(offsetof(Shell, shellType) == 0x6c && offsetof(Shell, stepsLeft) == 0x78 &&
              offsetof(Shell, origin) == 0x80 && offsetof(Shell, streakKind) == 0x8c, "Shell layout");

// ApplyWorldEffects' state (names ours)
extern uint32_t ShellConcreteSounds;            // alternates the two concrete sounds
extern int32_t ShellRicochetSound;              // the next of the three
extern RParticleSystemData *ShellSparks;        // the sparks' particle system ('SPKB'), a function-local static
extern uint32_t ShellSparksGuard;               // ... and its guard

#endif // DRIVING_GAME_SHELL_H_
