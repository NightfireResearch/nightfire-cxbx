#pragma fp_contract(off)

#include "SimulationSpawn.h"

#include <stddef.h>
#include <stdint.h>

#include "BondCar.h"                        // PBondCar
#include "EventsD.h"                        // ESpawnExplosion
#include "Explosion.h"
#include "Grenade.h"
#include "Helicopter.h"                     // PHelicopter
#include "Human.h"
#include "Mine.h"
#include "Missile.h"
#include "Sentry.h"
#include "Shell.h"
#include "Smackable.h"
#include "../../common/xbeOverload.h"       // XbeVirtual
#include "../logging.h"                     // dbg_wprintf, the game's printf
#include "../EventManager.hpp"              // Event::operator new
#include "../camera/PlayerCamera.h"
#include "../data/AttributeContainers.h"    // StringSet::BuyHead
#include "../data/Carp.h"                   // CARP::Instance, CARP::ResolverMap::Find
#include "../engine/CoreFoundation.h"       // GetFoundationVideoModeRate, NullFunction
#include "../engine/GameLoop.h"             // LaunchPage
#include "../engine/SimRandom.h"
#include "../engine/UMemory.hpp"
#include "../physics/Newton.h"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../platform/RealMath.h"           // vec3distance
#include "../platform/RealPrint.h"          // MEM_fill
#include "../platform/X87.h"                // Ftol
#include "../render/RenderHigh.h"           // fgRenderHigh
#include "../render/RSceneObj.hpp"
#include "../world/CollisionManager.h"      // fgCollisionMgr
#include "../world/CollisionQueries.h"      // PointerUninitializedCopy
#include "../world/SoundMap.h"              // MapBuyHead

// ---------------------------------------------------------------------------------------------------------------
// Simulation part 2 (0x000b4080-0x000b60a0), ported from the listing: the constructor and destructor, Reset and the
// step, spawning and deleting objects, the tracked instances, and the vector reserve and dead set insert they use.
// ---------------------------------------------------------------------------------------------------------------

static_assert(sizeof(PBondCar) == 0x420 && sizeof(PHelicopter) == 0x140 && sizeof(Newton) == 0x90,
              "the spawned objects' allocations");

namespace {

constexpr uint32_t kMaxPointers = 0x3fffffff;           // vector<T *>::max_size()
constexpr int kSimpleBodiesBeforeNewtons = 72;          // past this the newtons make room
constexpr int kForcedSimpleBodies = 96;
constexpr float kBigExplosionForce = 500.0f;            // a bigger explosion may take the forced bodies
constexpr float kTrackedInstanceRange = 100.0f;
constexpr float kTrackedInstanceStep = 16.0f;           // StepCheckHitWorld's step (not read)
constexpr uint32_t kRigidBodyBytes = 0x2000;            // the pool: 64 RigidBodies, 96 SimpleRigidBodies, then infos
constexpr uint32_t kSimpleBodyBytes = 0x1800;
constexpr uint32_t kScratchPadSize = 0x3800;
constexpr uint32_t kBodyPoolSize = 0x17800;

const char kStlName[] = "STL";

// The reserves the constructor makes, the vectors' first allocations
constexpr uint32_t kReserveCars = 0x40;
constexpr uint32_t kReserveObjects = 0x60;
constexpr uint32_t kReserveHelicopters = 4;

// The object a list's vector code handles as a vector of void pointers (one compiled copy serves them all)
template <class T>
SimPointerVector &AsPointerVector(SimVector<T> &list) {
    return reinterpret_cast<SimPointerVector &>(list);
}

// vector.erase(std::remove(...)): the second step regardless of whether the object was there
template <class T>
void EraseObject(SimVector<T> &list, PhysicsObject *object) {
    void **end;
    PointerRemove(&end, reinterpret_cast<void **>(list.first), reinterpret_cast<void **>(list.last),
                  reinterpret_cast<void *const *>(&object));
    void **erased;
    AsPointerVector(list).Erase(&erased, end);
}

// ... and with the test of the end the other vectors' removal leaves out (the cars')
template <class T>
void EraseObjectIfThere(SimVector<T> &list, PhysicsObject *object) {
    void **end;
    PointerRemove(&end, reinterpret_cast<void **>(list.first), reinterpret_cast<void **>(list.last),
                  reinterpret_cast<void *const *>(&object));
    if (end != reinterpret_cast<void **>(list.last)) {
        void **erased;
        AsPointerVector(list).Erase(&erased, end);
    }
}

template <class T>
void Append(SimVector<T> &list, PhysicsObject *object) {
    AsPointerVector(list).PushBack(reinterpret_cast<void *const *>(&object));
}

template <class T>
void ClearVector(SimVector<T> &list) {
    list.first = NULL;
    list.last = NULL;
    list.end = NULL;
}

template <class T>
void FreeVector(SimVector<T> &list) {
    if (list.first != NULL)
        UMemory::FastFree(list.first, (list.end - list.first) * sizeof(void *));
    list.first = NULL;
    list.last = NULL;
    list.end = NULL;
}

// The first node of the objects of `type`, or the end. The find of every map with 0x18-byte nodes keyed by a word
// is the CARP resolver map's (the linker folded them).
PhysicsObjNode *FindFirstOfType(PhysicsObjMap *map, const uint32_t *type) {
    TreeNode *node;
    reinterpret_cast<CARP::ResolverMap *>(map)->Find(&node, type);
    return reinterpret_cast<PhysicsObjNode *>(node);
}

// std::set::find on the dead objects, inlined in DeletePhysicsObject
DeadObjectNode *FindDead(DeadObjectSet *set, PhysicsObjNode *entry) {
    DeadObjectNode *where = set->head;
    for (DeadObjectNode *node = set->head->parent; !node->isNil;) {
        if (node->value->value.object < entry->value.object) {
            node = node->right;
        } else {
            where = node;
            node = node->left;
        }
    }
    if (where == set->head || entry->value.object < where->value->value.object)
        return set->head;
    return where;
}

// The map's ++, inlined in SimulateGame
PhysicsObjNode *Next(PhysicsObjNode *node) {
    if (node->isNil)
        return node;
    if (!node->right->isNil) {
        node = node->right;
        while (!node->left->isNil)
            node = node->left;
        return node;
    }
    PhysicsObjNode *parent = node->parent;
    while (!parent->isNil && node == parent->right) {
        node = parent;
        parent = parent->parent;
    }
    return parent;
}

// Simulation::FindPhysicsObjectSignature on the one Simulation, inlined in the weapons' spawns
PhysicsObject *FindOwner(uint32_t signature) {
    if (signature != 0) {
        int slot = signature >> kSignatureSlotShift & kSignatureSlotMask;
        bool simple = (signature & kSignatureSimple) != 0;
        uint32_t held = simple ? fgSimulation.simpleRigidBodySigs[slot] : fgSimulation.rigidBodySigs[slot];
        if (held == signature)
            return simple ? fgSimulation.simpleRigidBodyOwners[slot] : fgSimulation.rigidBodyOwners[slot];
    }
    return NULL;
}

// Whether `signature`'s owner is the player's car (GetPlayerObject inlined: the first awake car)
bool OwnedByPlayer(uint32_t signature) {
    return FindOwner(signature) == fgSimulation.cars.first[0];
}

// Vtable slot 4: the object's step
void SimulateObject(PhysicsObject *object) {
    (object->*XbeVirtual<decltype(&PhysicsObject::Simulate)>(object, 4))();
}

}  // namespace

// ---- the containers

// FUNC_AT(0x000b4080)
void SimPointerVector::Reserve(uint32_t count) {
    if (count > kMaxPointers) {
        Xlen();
        return;
    }
    uint32_t capacity = first == NULL ? 0 : end - first;
    if (capacity < count) {
        uint32_t bytes = count * sizeof(void *);
        void **storage = static_cast<void **>(UMemory::FastAlloc(bytes, kStlName));
        PointerUninitializedCopy(first, last, storage);
        uint32_t size = first == NULL ? 0 : last - first;
        if (first != NULL)
            UMemory::FastFree(first, (end - first) * sizeof(void *));
        first = storage;
        end = storage + count;
        last = storage + size;
    }
}

// FUNC_AT(0x000b4190)
DeadObjectSet::InsertResult* DeadObjectSet::InsertUnique(InsertResult *result, PhysicsObjNode *const &value) {
    DeadObjectNode *where = head;
    bool addLeft = true;
    for (DeadObjectNode *node = head->parent; !node->isNil; node = addLeft ? node->left : node->right) {
        where = node;
        addLeft = value->value.object < node->value->value.object;
    }
    Iterator previous = { where };
    if (addLeft) {
        if (where == head->left) {
            result->position = *Insert(&previous, true, where, value);
            result->inserted = true;
            return result;
        }
        previous.Dec();
    }
    if (previous.node->value->value.object < value->value.object) {
        result->position = *Insert(&previous, addLeft, where, value);
        result->inserted = true;
        return result;
    }
    result->position = previous;
    result->inserted = false;
    return result;
}

// ---- deleting

// The object's entry is looked for among the first `count` of its type, count being the number of that type; if it
// is not among them the search stops on the node after them, and that node (perhaps the end) is what goes into the
// dead set. The vectors other than the cars' have the object erased whether or not it was in them.
// FUNC_AT(0x000b4250)
void Simulation::DeletePhysicsObject(PhysicsObject *object) {
    uint32_t type = object->type;
    PhysicsObjMap *map = objects;
    PhysicsObjIterator entry = { FindFirstOfType(map, &type) };
    if (entry.node == map->head || entry.node->value.key != type)
        return;
    PhysicsObjMap::Range range;
    map->EqualRange(&range, &type);
    uint32_t count = 0;
    PhysicsObjDistance(range.first, range.last, &count);
    for (int i = 0; i < int(count); i++) {
        if (entry.node->value.object == object)
            break;
        if (entry.node == map->head || entry.node->value.key != type)
            return;
        entry.Inc();
    }

    if (FindDead(deadObjects, entry.node) != deadObjects->head)
        return;                                 // already waiting
    DeadObjectSet::InsertResult inserted;
    deadObjects->InsertUnique(&inserted, entry.node);

    switch (type) {
    case kObjectVehicle:
        EraseObjectIfThere(cars, object);
        EraseObjectIfThere(inactiveCars, object);
        break;
    case kObjectSmackable:
        EraseObject(smackables, object);
        break;
    case kObjectExplosion:
        EraseObject(explosions, object);
        break;
    case kObjectHuman:
        EraseObject(humans, object);
        break;
    case kObjectMine:
        EraseObject(mines, object);
        break;
    case kObjectMissile:
        EraseObject(missiles, object);
        break;
    case kObjectShell:
        EraseObject(shells, object);
        break;
    case kObjectGrenade:
        EraseObject(grenades, object);
        break;
    case kObjectNewton:
        EraseObject(newtons, object);
        break;
    case kObjectHelicopter:
        EraseObject(helicopters, object);
        break;
    case kObjectSentry:
        EraseObject(sentries, object);
        break;
    }
}

// FUNC_AT(0x000b4740)
void Simulation::DeletePhysicsObjectThunk(PhysicsObject *object) {
    DeletePhysicsObject(object);
}

// FUNC_AT(0x000b4750)
void Simulation::CleanUpObjects() {
    DeleteDeadObjects();
    PhysicsObjNode *end = objects->head;
    for (PhysicsObjIterator it = { end->left }; it.node != end; it.Inc())
        DeletePhysicsObject(static_cast<PhysicsObject *>(it.node->value.object));
    DeleteDeadObjects();
}

// The newtons go to the one Simulation's dead set while this one's count is tested (they are the same object in
// the game); the count only falls when DeleteDeadObjects runs, so every newton goes.
// FUNC_AT(0x000b47a0)
bool Simulation::CanSpawnSimpleRigidBody(const Coord3 *position, bool force) {
    int limit = force ? kForcedSimpleBodies : kSimpleBodiesBeforeNewtons;
    while (uint32_t(numSimpleRigidBodies) > uint32_t(kSimpleBodiesBeforeNewtons) && newtons.Size() != 0)
        fgSimulation.DeletePhysicsObject(newtons.first[0]);
    return numSimpleRigidBodies < limit;
}

// ---- the tracked instances

// FUNC_AT(0x000b4810)
void Simulation::TrackInstance(CARP::Instance *instance, int unknown) {
    for (TrackedInstance **p = trackedInstances.first; p != trackedInstances.last; p++)
        if ((*p)->instance == instance)
            return;
    TrackedInstance *tracked = static_cast<TrackedInstance *>(OperatorNew(sizeof(TrackedInstance)));
    if (tracked != NULL) {
        tracked->instance = instance;
        tracked->unknown00 = unknown;
        tracked->visible = false;
        AsPointerVector(trackedInstances).PushBack(reinterpret_cast<void *const *>(&tracked));
    }
}

// FUNC_AT(0x000b4870)
void Simulation::UpdateTrackedInstanceVisibility() {
    TrackedInstance **front = trackedInstances.first;
    if (front == trackedInstances.last)
        return;
    TrackedInstance *tracked = *front;
    const float *eye = fgRenderHigh->views[0].camera->matrix.mtx[3];
    const float *at = tracked->instance->position;
    Coord4 segment[2] = {
        { eye[0], eye[1], eye[2], 1.0f },
        { at[0], at[1], at[2], 1.0f },
    };
    if (vec3distance(&segment[0], &segment[1]) <= kTrackedInstanceRange)
        tracked->visible = !fgCollisionMgr->StepCheckHitWorld(segment, kTrackedInstanceStep);
    else
        tracked->visible = false;
    // to the back
    for (TrackedInstance **p = front + 1; p != trackedInstances.last; p++)
        p[-1] = *p;
    trackedInstances.last--;
    AsPointerVector(trackedInstances).PushBack(reinterpret_cast<void *const *>(&tracked));
}

// ---- the step

// FUNC_AT(0x000b49f0)
void Simulation::Reset() {
    stepsPerSecond = Ftol(GetFoundationVideoModeRate());
    timeStep = float(1.0 / stepsPerSecond);
    Launch.unknown978 = 3;
    CleanUpObjects();
    UntrackAllInstances();
    simState = 0;
    savedSimState = 0;
    stepCount = 0;
    numRigidBodies = 0;
    numSimpleRigidBodies = 0;
    useScratchPad = false;
    random->Reset();
}

// FUNC_AT(0x000b4a60)
void Simulation::SimulateGame() {
    if (simState == kSimPaused)
        return;
    for (int i = 0; i < kMaxRigidBodies; i++) {
        if (rigidBodyOwners[i] != NULL) {
            RigidBody *body = &(useScratchPad ? rigidBodiesScratch : rigidBodies)[i];
            if (body->sleepState != RigidBody::kFrozen && body->sleepState != 0)
                body->CollideWithObject(i, 1);
        }
    }
    fgCollisionMgr->unknown30 = 1;
    for (int i = 0; i < kMaxRigidBodies; i++) {
        if (rigidBodyOwners[i] != NULL) {
            RigidBody *body = &(useScratchPad ? rigidBodiesScratch : rigidBodies)[i];
            if (body->sleepState != RigidBody::kFrozen) {
                RigidBodyInfo *info = body->info;
                if (info->unknown4fc && !info->unknown4ff)
                    info->unknown4fc = 0;
                if (body->sleepState == RigidBody::kAwake)
                    body->CollideWithWorld();
            }
        }
    }
    fgCollisionMgr->unknown30 = 0;
    for (int i = 0; i < kMaxRigidBodies; i++) {
        if (rigidBodyOwners[i] != NULL) {
            RigidBody *body = &(useScratchPad ? rigidBodiesScratch : rigidBodies)[i];
            if (body->sleepState == RigidBody::kAwake)
                body->CollideWithGround();
        }
    }
    for (int i = 0; i < kMaxRigidBodies; i++) {
        if (rigidBodyOwners[i] != NULL) {
            RigidBody *body = &(useScratchPad ? rigidBodiesScratch : rigidBodies)[i];
            if (body->sleepState == RigidBody::kAwake) {
                body->ApplyAngularDamping();
                body->UpdatePositionAndOrientation(1.0f);
            }
        }
    }
    for (int i = 0; i < kMaxRigidBodies; i++) {
        if (rigidBodyOwners[i] != NULL) {
            RigidBody *body = &(useScratchPad ? rigidBodiesScratch : rigidBodies)[i];
            if (body->sleepState == RigidBody::kAwake)
                body->ApplyInitialForcesAndTorques();
        }
    }
    for (int i = 0; i < kMaxSimpleBodies; i++)
        if (simpleRigidBodyOwners[i] != NULL)
            (useScratchPad ? simpleRigidBodiesScratch : simpleRigidBodies)[i].UpdatePosition();
    DetectCollisionsSRB();
    fgCollisionMgr->unknown30 = 1;
    PhysicsObjNode *end = objects->head;
    for (PhysicsObjNode *node = end->left; node != end; node = Next(node))
        SimulateObject(static_cast<PhysicsObject *>(node->value.object));
    UpdateTrackedInstanceVisibility();
}

// ---- spawning

// FUNC_AT(0x000b4cd0)
void Simulation::SpawnExplosion(int type, int weapon, const Coord3 *direction, const Coord3 *position, float size,
                                int faceType, PhysicsObject *directHit, const uint32_t *ownerSig) {
    if (!CanSpawnSimpleRigidBody(position, size > kBigExplosionForce))
        return;
    Coord4 at = { position->x, position->y, position->z, 1.0f };
    Coord4 heading = { direction->x, direction->y, direction->z, 0.0f };
    void *event = Event::operator new(sizeof(ESpawnExplosion));
    if (event == NULL)
        return;
    // the object hit's body, by slot and kind; the kind's upper bytes are left over without one (only its low
    // byte is read)
    int slot = directHit != NULL ? directHit->rigidBodySlot : -1;
    bool simple = directHit != NULL ? (directHit->flags & PhysicsObject::kSimpleBody) != 0 : false;
    static_cast<ESpawnExplosion *>(event)->Construct(at, heading, NULL, simple, slot, directHit, type, weapon, size,
                                                     faceType, NULL, 0, NULL);
}

// FUNC_AT(0x000b4e10)
void Simulation::SpawnPhysicsObject(PhysicsObject *object) {
    int32_t type = object->type;
    SimObjectValue value = { uint32_t(type), object };
    SimObjectInsert inserted;
    objects->InsertMulti(&inserted, &value);
    switch (type) {
    case kObjectVehicle:
        if (object->GetRigidBody()->sleepState == RigidBody::kAwake)
            Append(cars, object);
        else
            Append(inactiveCars, object);
        break;
    case kObjectSmackable:
        Append(smackables, object);
        break;
    case kObjectExplosion:
        Append(explosions, object);
        break;
    case kObjectHuman:
        Append(humans, object);
        break;
    case kObjectMine:
        Append(mines, object);
        break;
    case kObjectMissile:
        Append(missiles, object);
        break;
    case kObjectShell:
        Append(shells, object);
        break;
    case kObjectGrenade:
        Append(grenades, object);
        break;
    case kObjectNewton:
        Append(newtons, object);
        break;
    case kObjectHelicopter:
        Append(helicopters, object);
        break;
    case kObjectSentry:
        Append(sentries, object);
        break;
    }
}

// The owners' slots are not cleared (the one Simulation is a static, zero already). The maps' allocator bytes are
// written from an uninitialised stack byte in the original; here they are zero.
// FUNC_AT(0x000b4fe0)
Simulation* Simulation::Construct() {
    for (int i = 0; i < kMaxRigidBodies; i++)
        rigidBodySigs[i] = 0;
    for (int i = 0; i < kMaxSimpleBodies; i++)
        simpleRigidBodySigs[i] = 0;
    for (int i = 0; i < kMaxSimpleBodies; i++)
        simpleBodyHits[i] = BitSet192();
    reflectiveObjectSig = 0;
    ClearVector(cars);
    ClearVector(inactiveCars);
    ClearVector(smackables);
    ClearVector(humans);
    ClearVector(missiles);
    ClearVector(mines);
    ClearVector(grenades);
    ClearVector(shells);
    ClearVector(explosions);
    ClearVector(newtons);
    ClearVector(helicopters);
    ClearVector(sentries);
    ClearVector(trackedInstances);
    timeStep = 0.0f;
    stepsPerSecond = 0;

    // The map's and the set's heads come from the head allocators the linker shared out among the maps with
    // 0x18-byte nodes and the sets with 0x14-byte ones
    PhysicsObjMap *map = static_cast<PhysicsObjMap *>(UMemory::FastAlloc(sizeof(PhysicsObjMap), "PhysicsObjMap"));
    if (map != NULL) {
        map->allocator = 0;
        map->head = reinterpret_cast<PhysicsObjNode *>(MapBuyHead());
        map->head->isNil = 1;
        map->head->parent = map->head;
        map->head->left = map->head;
        map->head->right = map->head;
        map->size = 0;
        dbg_wprintf("PhysicsObjMap: Default constructor should not be called\n");
    }
    objects = map;
    DeadObjectSet *dead = static_cast<DeadObjectSet *>(UMemory::FastAlloc(sizeof(DeadObjectSet), "DeadObjList"));
    if (dead != NULL) {
        dead->allocator = 0;
        dead->head = reinterpret_cast<DeadObjectNode *>(StringSet::BuyHead());
        dead->head->isNil = 1;
        dead->head->parent = dead->head;
        dead->head->left = dead->head;
        dead->head->right = dead->head;
        dead->size = 0;
    }
    deadObjects = dead;

    scratchPadSize = kScratchPadSize;
    bodyPoolSize = kBodyPoolSize;
    bodyPool = UMemory::Alloc(kBodyPoolSize, 0, "RigidBodyTable");
    MEM_fill(bodyPool, 0, bodyPoolSize);
    uint8_t *pool = static_cast<uint8_t *>(bodyPool);
    rigidBodies = reinterpret_cast<RigidBody *>(pool);
    simpleRigidBodies = reinterpret_cast<SimpleRigidBody *>(pool + kRigidBodyBytes);
    rigidBodyInfo = reinterpret_cast<RigidBodyInfo *>(pool + kRigidBodyBytes + kSimpleBodyBytes);
    rigidBodiesScratch = NULL;
    simpleRigidBodiesScratch = NULL;

    AsPointerVector(cars).Reserve(kReserveCars);
    AsPointerVector(inactiveCars).Reserve(kReserveCars);
    AsPointerVector(smackables).Reserve(kReserveCars);
    AsPointerVector(humans).Reserve(kReserveObjects);
    AsPointerVector(missiles).Reserve(kReserveObjects);
    AsPointerVector(mines).Reserve(kReserveObjects);
    AsPointerVector(shells).Reserve(kReserveObjects);
    AsPointerVector(explosions).Reserve(kReserveObjects);
    AsPointerVector(newtons).Reserve(kReserveObjects);
    AsPointerVector(helicopters).Reserve(kReserveHelicopters);
    AsPointerVector(sentries).Reserve(kReserveObjects);

    SimRandom *made = static_cast<SimRandom *>(UMemory::FastAlloc(sizeof(SimRandom), "SimRandom"));
    random = made != NULL ? made->Construct() : NULL;
    return this;
}

// FUNC_AT(0x000b5310)
void Simulation::Destruct() {
    UntrackAllInstances();
    NullFunction();                             // an empty destructor
    if (random != NULL) {
        NullFunction();                         // SimRandom's (empty) destructor
        UMemory::FastFree(random, sizeof(SimRandom));
    }
    UMemory::Free(bodyPool);

    if (PhysicsObjMap *map = objects) {
        PhysicsObjIterator erased;
        map->EraseRange(&erased, PhysicsObjIterator{ map->head->left }, PhysicsObjIterator{ map->head });
        if (map->head != NULL)
            UMemory::FastFree(map->head, sizeof(PhysicsObjNode));
        map->head = NULL;
        map->size = 0;
        UMemory::FastFree(map, sizeof(PhysicsObjMap));
    }
    if (DeadObjectSet *dead = deadObjects) {
        DeadObjectSet::Iterator erased;
        dead->EraseRange(&erased, DeadObjectSet::Iterator{ dead->head->left }, DeadObjectSet::Iterator{ dead->head });
        if (dead->head != NULL)
            UMemory::FastFree(dead->head, sizeof(DeadObjectNode));
        dead->head = NULL;
        dead->size = 0;
        UMemory::FastFree(dead, sizeof(DeadObjectSet));
    }

    // the vectors, last first
    FreeVector(trackedInstances);
    FreeVector(sentries);
    FreeVector(helicopters);
    FreeVector(newtons);
    FreeVector(explosions);
    FreeVector(shells);
    FreeVector(grenades);
    FreeVector(mines);
    FreeVector(missiles);
    FreeVector(humans);
    FreeVector(smackables);
    FreeVector(inactiveCars);
    FreeVector(cars);
}

// Each spawn: room for the body, the object from the fast allocator (UMemory::Alloc for a Newton), constructed,
// then filed. A failed allocation files NULL (SpawnPhysicsObject then faults, as the original does).

// FUNC_AT(0x000b5700)
PhysicsObject* Simulation::SpawnCarObject(int carClass, const char *carType, uint32_t colour, const Coord3 *direction,
                                          const Coord3 *position) {
    if (!CanSpawnRigidBody(position, true))
        return NULL;
    PBondCar *car = static_cast<PBondCar *>(UMemory::FastAlloc(sizeof(PBondCar), "PBondCar"));
    PhysicsObject *object = car != NULL ? car->Construct(carClass, carType, colour, direction, position) : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b57b0)
PhysicsObject* Simulation::SpawnSmackableObject(RSceneObj *render, WTrigger *trigger, const Coord3 *velocity,
                                                uint32_t sleepDelay) {
    alignas(16) MATRIX4 transform;
    render->GetTransform(&transform);
    Coord3 position = { transform.mtx[3][0], transform.mtx[3][1], transform.mtx[3][2] };
    if (!CanSpawnRigidBody(&position, false))
        return NULL;
    Smackable *smackable = static_cast<Smackable *>(UMemory::FastAlloc(sizeof(Smackable), "Smackable"));
    PhysicsObject *object = smackable != NULL ? smackable->Construct(render, trigger, velocity, sleepDelay) : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5890)
PhysicsObject* Simulation::SpawnHumanObject(int unknown6c, const Coord3 *direction, const Coord3 *position,
                                            uint32_t ownerSig) {
    if (!CanSpawnSimpleRigidBody(position, unknown6c == 0))
        return NULL;
    Human *human = static_cast<Human *>(UMemory::FastAlloc(sizeof(Human), "Human"));
    PhysicsObject *object = human != NULL ? human->Construct(unknown6c, direction, position, ownerSig) : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5950)
PhysicsObject* Simulation::SpawnMissileObject(int weapon, float speed, const Coord3 *direction, const Coord3 *position,
                                              uint32_t ownerSig, float damage, WTargetable *target, int variant) {
    if (!CanSpawnSimpleRigidBody(position, OwnedByPlayer(ownerSig)))
        return NULL;
    Missile *missile = static_cast<Missile *>(UMemory::FastAlloc(sizeof(Missile), "Missile"));
    PhysicsObject *object = missile != NULL ? missile->Construct(weapon, speed, direction, position, ownerSig, damage,
                                                                 target, variant)
                                            : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5a70)
PhysicsObject* Simulation::SpawnMineObject(int kind, const Coord3 *direction, const Coord3 *position, float speed,
                                           uint32_t ownerSig) {
    if (!CanSpawnSimpleRigidBody(position, OwnedByPlayer(ownerSig)))
        return NULL;
    Mine *mine = static_cast<Mine *>(UMemory::FastAlloc(sizeof(Mine), "Mine"));
    PhysicsObject *object = mine != NULL ? mine->Construct(kind, direction, position, speed, ownerSig) : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5b80)
PhysicsObject* Simulation::SpawnGrenadeObject(int weaponType, const Coord3 *direction, const Coord3 *position,
                                              float speed, uint32_t ownerSig) {
    if (!CanSpawnSimpleRigidBody(position, OwnedByPlayer(ownerSig)))
        return NULL;
    Grenade *grenade = static_cast<Grenade *>(UMemory::FastAlloc(sizeof(Grenade), "Grenade"));
    PhysicsObject *object = grenade != NULL ? grenade->Construct(weaponType, direction, position, speed, ownerSig)
                                            : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5c90)
PhysicsObject* Simulation::SpawnShellObject(int shellType, const Coord3 *direction, const Coord3 *origin,
                                            uint32_t ownerSig, float damage, float explosionSize, int streakKind) {
    if (!CanSpawnSimpleRigidBody(origin, false))
        return NULL;
    Shell *shell = static_cast<Shell *>(UMemory::FastAlloc(sizeof(Shell), "Shell"));
    PhysicsObject *object = shell != NULL ? shell->Construct(shellType, direction, origin, ownerSig, damage,
                                                             explosionSize, streakKind)
                                          : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5d50)
PhysicsObject* Simulation::SpawnExplosionObject(int type, uint32_t unused, const Coord3 *direction,
                                                const Coord3 *position, float size, PhysicsObject *directHit,
                                                const uint32_t *ownerSig) {
    if (!CanSpawnSimpleRigidBody(position, size > kBigExplosionForce))
        return NULL;
    Explosion *explosion = static_cast<Explosion *>(UMemory::FastAlloc(sizeof(Explosion), "Explosion"));
    PhysicsObject *object = explosion != NULL ? explosion->Construct(type, direction, position, size, directHit, ownerSig)
                                              : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5e20)
PhysicsObject* Simulation::SpawnNewtonObject(const Coord3 *direction, const Coord3 *position, const Coord3 *momentum,
                                             const Coord3 *spin, CARP::Instance *instances, int instanceCount,
                                             float mass, float lifetime) {
    if (!CanSpawnSimpleRigidBody(position, false))
        return NULL;
    Newton *newton = static_cast<Newton *>(UMemory::Alloc(sizeof(Newton), 0, "Newton"));
    PhysicsObject *object = newton != NULL ? newton->Construct(direction, position, momentum, spin, instances,
                                                               instanceCount, mass, lifetime)
                                           : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5ee0)
PhysicsObject* Simulation::SpawnHelicopterObject(const char *name, const Coord3 *direction, const Coord3 *position,
                                                 float speed) {
    if (!CanSpawnSimpleRigidBody(position, true))
        return NULL;
    PHelicopter *helicopter = static_cast<PHelicopter *>(UMemory::FastAlloc(sizeof(PHelicopter), "PHelicopter"));
    PhysicsObject *object = helicopter != NULL ? helicopter->Construct(name, speed, *direction, *position) : NULL;
    SpawnPhysicsObject(object);
    return object;
}

// FUNC_AT(0x000b5fb0)
PhysicsObject* Simulation::SpawnSentryObject(CARP::Instance *instance, int srbAxis, float srbSpeed, float srbLimit,
                                             int animAxis, float animSpeed, float animLimit, uint32_t aimSystem,
                                             uint32_t fireSystem, uint32_t shots, float shotSeconds, float range,
                                             uint32_t targetSig, float hitPoints, uint32_t holdSteps,
                                             uint8_t scoreable) {
    if (!CanSpawnSimpleRigidBody(reinterpret_cast<const Coord3 *>(instance->position), true))
        return NULL;
    Sentry *sentry = static_cast<Sentry *>(UMemory::FastAlloc(sizeof(Sentry), "Sentry"));
    PhysicsObject *object = sentry != NULL ? sentry->Construct(instance, srbAxis, srbSpeed, srbLimit, animAxis,
                                                               animSpeed, animLimit, aimSystem, fireSystem, shots,
                                                               shotSeconds, range, targetSig, hitPoints, holdSteps,
                                                               scoreable)
                                           : NULL;
    SpawnPhysicsObject(object);
    return object;
}
