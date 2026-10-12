#ifndef DRIVING_GAME_SIMULATION_H_
#define DRIVING_GAME_SIMULATION_H_

// ---------------------------------------------------------------------------------------------------------------
// Simulation (0xf24 bytes, one, at 0x00233ff0): the world of game objects. It owns the rigid and simple rigid
// bodies (slots in one pool, each with a signature and its owner), the objects it has spawned - in a multimap by
// type and in a vector per kind - the set of objects waiting to be deleted, the instances it tracks for the HUD,
// the step counter and the pause state, and its random numbers. See SimulationObjects.cpp (the bodies, the
// objects, the tracked instances) and SimulationContainers.h (the containers' compiled code).
// ---------------------------------------------------------------------------------------------------------------

#include <stddef.h>
#include <stdint.h>

#include "SimulationContainers.h"
#include "../data/CoordConvert.h"       // Coord3
#include "../engine/SimRandom.h"        // SimRandom, BitSet192

namespace CARP {
class Instance;
}
class Explosion;
class Grenade;
class Human;
class Mine;
class Missile;
class RSceneObj;
class Sentry;
class Shell;
class Smackable;
class WTrigger;
struct Newton;
struct PHelicopter;
struct PhysicsObject;
struct PVehicle;
struct RigidBody;
struct RigidBodyInfo;
struct SimpleRigidBody;
struct WTargetable;

// Simulation::simState
enum SimulationState : int32_t {
    kSimFinished = 2,                   // the game loop ends
    kSimPaused = 3,                     // PauseSimState's; the step count stands still
};

// An instance the HUD's targets follow (new'd by TrackInstance; the name is ours)
struct TrackedInstance {
    int32_t unknown00;                  // +0x00 TrackInstance's second argument
    CARP::Instance *instance;           // +0x04
    bool visible;                       // +0x08 UpdateTrackedInstanceVisibility's
    uint8_t unknown09[3];
};
static_assert(sizeof(TrackedInstance) == 0xc, "a tracked instance is 12 bytes");

// A body's signature: the counter's low 24 bits, the slot in bits 24-30, and bit 31 set for a simple body
constexpr uint32_t kSignatureSimple = 0x80000000;
constexpr int kSignatureSlotShift = 24;
constexpr uint32_t kSignatureSlotMask = 0x7f;

class Simulation {
public:
    static constexpr int kMaxRigidBodies = 64;
    static constexpr int kMaxSimpleBodies = 96;

    SimRandom *random;                              // +0x000
    bool useScratchPad;                             // +0x004 the bodies are read from the scratch pad copies
    uint8_t unknown005[3];
    int32_t numRigidBodies;                         // +0x008
    int32_t numSimpleRigidBodies;                   // +0x00c
    uint32_t bodyPoolSize;                          // +0x010 0x17800
    uint32_t scratchPadSize;                        // +0x014 0x3800: the bodies' share of the pool
    void *bodyPool;                                 // +0x018 UMemory::Alloc's "RigidBodyTable"
    RigidBody *rigidBodies;                         // +0x01c [64] in the pool
    SimpleRigidBody *simpleRigidBodies;             // +0x020 [96] after them
    RigidBodyInfo *rigidBodyInfo;                   // +0x024 [64] after them
    RigidBody *rigidBodiesScratch;                  // +0x028
    SimpleRigidBody *simpleRigidBodiesScratch;      // +0x02c
    uint32_t rigidBodySigs[kMaxRigidBodies];        // +0x030 by slot; 0 free
    uint32_t simpleRigidBodySigs[kMaxSimpleBodies]; // +0x130
    PhysicsObject *rigidBodyOwners[kMaxRigidBodies];        // +0x2b0 by slot; NULL free
    PhysicsObject *simpleRigidBodyOwners[kMaxSimpleBodies]; // +0x3b0
    BitSet192 simpleBodyHits[kMaxSimpleBodies];             // +0x530 the bodies each simple body hit
                                                            //        (DetectCollisionsSRB's), by its slot
    int32_t savedSimState;                          // +0xe30 the state PauseSimState replaced
    int32_t simState;                               // +0xe34 SimulationState
    uint32_t unknownE38;
    int32_t stepsPerSecond;                         // +0xe3c
    float timeStep;                                 // +0xe40 seconds a step
    int32_t stepCount;                              // +0xe44
    uint32_t reflectiveObjectSig;                   // +0xe48 0: the player's
    SimVector<PVehicle> cars;                       // +0xe4c the awake cars, the player's first
    SimVector<PVehicle> inactiveCars;               // +0xe5c cars spawned asleep
    SimVector<Smackable> smackables;                // +0xe6c
    SimVector<Human> humans;                        // +0xe7c
    SimVector<Missile> missiles;                    // +0xe8c
    SimVector<Mine> mines;                          // +0xe9c
    SimVector<Grenade> grenades;                    // +0xeac
    SimVector<Shell> shells;                        // +0xebc
    SimVector<Explosion> explosions;                // +0xecc
    SimVector<Newton> newtons;                      // +0xedc
    SimVector<PHelicopter> helicopters;             // +0xeec
    SimVector<Sentry> sentries;                     // +0xefc
    SimVector<TrackedInstance> trackedInstances;    // +0xf0c
    PhysicsObjMap *objects;                         // +0xf1c "PhysicsObjMap": every spawned object, by type
    DeadObjectSet *deadObjects;                     // +0xf20 "DeadObjList"

    // ---- SimulationObjects.cpp
    void PauseSimState();                                                                       // 0x000b25e0
    void UnPauseSimState();                                                                     // 0x000b2600
    void AdvanceStep();                                                                         // 0x000b2620
    // A free slot for `owner`'s body and a new signature; -1 if none is free
    int AssignRigidBodySlot(PhysicsObject *owner, bool simple);                                 // 0x000b2630
    RigidBody* GetRigidBody(int slot);                                                          // 0x000b2700
    SimpleRigidBody* GetSimpleRigidBody(int slot);                                              // 0x000b2730
    RigidBodyInfo* GetRigidBodyInfo(int slot);                                                  // 0x000b2760
    // The slot of the body with `signature`, and whether it is simple; -1 if none has it
    int FindSignature(uint32_t signature, bool *simple);                                        // 0x000b2780
    PhysicsObject* FindPhysicsObjectSignature(uint32_t signature);                              // 0x000b27d0
    static void* GetScratchPadFreeZone();                                                       // 0x000b2820
    void DetectCollisionsSRB();                                                                 // 0x000b2830
    // The owner of body `index`: the rigid bodies' slots, then the simple ones'
    PhysicsObject* GetOrderedBody(uint32_t index);                                              // 0x000b28d0
    SimVector<TrackedInstance>* GetTrackedInstanceList();                                     // 0x000b2900
    void SetReflectiveObject(uint32_t signature);                                               // 0x000b2910
    void ReleaseRigidBodySlot(int slot, bool simple);                                           // 0x000b2980
    // The player's car at `position`, on the ground below it, facing `direction`
    void SetStartConditions(const Coord3 *position, const Coord3 *direction);                   // 0x000b2b50
    void DetonateRemoteMines();                                                                 // 0x000b2c10
    // Whether another rigid body fits (64 with `force`, else 52); past that, the farthest awake smackable is put
    // to sleep
    bool CanSpawnRigidBody(const Coord3 *position, bool force);                                 // 0x000b2c40
    PhysicsObject* GetPlayerObject();                                                           // 0x000b2d30
    Coord3* GetReflectiveObjectPosition();                                                      // 0x000b2d40
    void UntrackInstance(CARP::Instance *instance);                                             // 0x000b3000
    void UntrackAllInstances();                                                                 // 0x000b30c0
    void DeleteDeadObjects();                                                                   // 0x000b3fc0

    // ---- SimulationSpawn.cpp
    // Takes the object out of its kind's vector and puts it in the set DeleteDeadObjects deletes
    void DeletePhysicsObject(PhysicsObject *object);                                            // 0x000b4250
    // A jump to DeletePhysicsObject, which most callers call through (Smackable calls the body directly)
    void DeletePhysicsObjectThunk(PhysicsObject *object);                                       // 0x000b4740
    void CleanUpObjects();                                                                      // 0x000b4750
    // Whether another simple body fits (96 with `force`, else 72); past 72 the newtons are deleted first.
    // `position` is not read.
    bool CanSpawnSimpleRigidBody(const Coord3 *position, bool force);                           // 0x000b47a0
    void TrackInstance(CARP::Instance *instance, int unknown);                                  // 0x000b4810
    // The front tracked instance's visibility from the first view's camera, then it goes to the back
    void UpdateTrackedInstanceVisibility();                                                     // 0x000b4870
    void Reset();                                                                               // 0x000b49f0
    void SimulateGame();                                                                        // 0x000b4a60
    // An ESpawnExplosion event: an explosion of the type (Explosion.h's ExplosionType) by the weapon (or shell
    // type) at `position`, set off on `directHit` (or none). `ownerSig` is not read.
    void SpawnExplosion(int type, int weapon, const Coord3 *direction, const Coord3 *position, float size,
                        int faceType, PhysicsObject *directHit, const uint32_t *ownerSig);      // 0x000b4cd0
    void SpawnPhysicsObject(PhysicsObject *object);                                             // 0x000b4e10
    Simulation* Construct();                                                                    // 0x000b4fe0
    void Destruct();                                                                            // 0x000b5310
    // The spawns: NULL when there is no room for the object's body (the arguments are the constructors')
    PhysicsObject* SpawnCarObject(int carClass, const char *carType, uint32_t colour, const Coord3 *direction,
                                  const Coord3 *position);                                      // 0x000b5700
    PhysicsObject* SpawnSmackableObject(RSceneObj *render, WTrigger *trigger, const Coord3 *velocity,
                                        uint32_t sleepDelay);                                   // 0x000b57b0
    PhysicsObject* SpawnHumanObject(int unknown6c, const Coord3 *direction, const Coord3 *position,
                                    uint32_t ownerSig);                                         // 0x000b5890
    PhysicsObject* SpawnMissileObject(int weapon, float speed, const Coord3 *direction, const Coord3 *position,
                                      uint32_t ownerSig, float damage, WTargetable *target,
                                      int variant);                                             // 0x000b5950
    PhysicsObject* SpawnMineObject(int kind, const Coord3 *direction, const Coord3 *position, float speed,
                                   uint32_t ownerSig);                                          // 0x000b5a70
    PhysicsObject* SpawnGrenadeObject(int weaponType, const Coord3 *direction, const Coord3 *position, float speed,
                                      uint32_t ownerSig);                                       // 0x000b5b80
    PhysicsObject* SpawnShellObject(int shellType, const Coord3 *direction, const Coord3 *origin, uint32_t ownerSig,
                                    float damage, float explosionSize, int streakKind);         // 0x000b5c90
    // `unused` is not read; the direction is the constructor's (which does not read it either)
    PhysicsObject* SpawnExplosionObject(int type, uint32_t unused, const Coord3 *direction, const Coord3 *position,
                                        float size, PhysicsObject *directHit,
                                        const uint32_t *ownerSig);                              // 0x000b5d50
    PhysicsObject* SpawnNewtonObject(const Coord3 *direction, const Coord3 *position, const Coord3 *momentum,
                                     const Coord3 *spin, CARP::Instance *instances, int instanceCount,
                                     float mass, float lifetime);                               // 0x000b5e20
    PhysicsObject* SpawnHelicopterObject(const char *name, const Coord3 *direction, const Coord3 *position,
                                         float speed);                                          // 0x000b5ee0
    PhysicsObject* SpawnSentryObject(CARP::Instance *instance, int srbAxis, float srbSpeed, float srbLimit,
                                     int animAxis, float animSpeed, float animLimit, uint32_t aimSystem,
                                     uint32_t fireSystem, uint32_t shots, float shotSeconds, float range,
                                     uint32_t targetSig, float hitPoints, uint32_t holdSteps,
                                     uint8_t scoreable);                                        // 0x000b5fb0
};
static_assert(sizeof(Simulation) == 0xf24, "the Simulation is 0xf24 bytes");
static_assert(offsetof(Simulation, rigidBodySigs) == 0x030 && offsetof(Simulation, simpleRigidBodySigs) == 0x130 &&
              offsetof(Simulation, rigidBodyOwners) == 0x2b0 && offsetof(Simulation, simpleRigidBodyOwners) == 0x3b0 &&
              offsetof(Simulation, simpleBodyHits) == 0x530 && offsetof(Simulation, savedSimState) == 0xe30 &&
              offsetof(Simulation, stepCount) == 0xe44 && offsetof(Simulation, cars) == 0xe4c &&
              offsetof(Simulation, missiles) == 0xe8c && offsetof(Simulation, helicopters) == 0xeec &&
              offsetof(Simulation, trackedInstances) == 0xf0c && offsetof(Simulation, objects) == 0xf1c,
              "Simulation layout");

// The one Simulation (constructed by the static initialisers; the name is ours)
#define fgSimulation (*(Simulation *)0x00233ff0)

// Set while DeleteDeadObjects runs (the PS2 build's Simulation::DeleteInProgress reads it; the name is ours)
extern bool SimDeleteInProgress;

#endif // DRIVING_GAME_SIMULATION_H_
