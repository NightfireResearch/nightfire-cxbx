#pragma fp_contract(off)

#include "SimulationObjects.h"

#include "Mine.h"
#include "Smackable.h"
#include "Vehicle.h"                    // PVehicle
#include "WeaponManager.h"              // kWeaponRemMine
#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../../helpers.h"
#include "../data/AttributeContainers.h"   // StringSet::Iterator
#include "../engine/PhysicsUtil.h"      // Util_GenerateMatrix
#include "../engine/UMemory.hpp"
#include "../physics/PhysicsObject.h"
#include "../physics/RigidBody.h"
#include "../physics/SimpleRigidBody.h"
#include "../render/RenderHigh.h"       // fgRenderHigh, RPlayerCamera
#include "../world/CollisionManager.h"  // fgCollisionMgr

// ---------------------------------------------------------------------------------------------------------------
// The Simulation's smaller methods (0x000b25e0-0x000b4080): the pause state, the body slots and signatures, the
// simple bodies' collision pass, the objects' lookups, the tracked instances and the deletion of dead objects.
// Ported from the listing.
// ---------------------------------------------------------------------------------------------------------------

// ---- globals
// The counter in the low 24 bits of the next signature (the name is ours)
// XBE_GLOBAL(0x001c8fbc, 4)
uint32_t NextSignature = 1;
// XBE_GLOBAL(0x00233fd0, 1)
bool SimDeleteInProgress;
// The maths header's constants, this unit's copies as the static initialisers made them (1 / FLT_MIN, 2 pi,
// 1 / 2 pi, 180 / pi and its reciprocal, pi being 3.1415925); nothing reads them
// XBE_GLOBAL(0x00233fd4, 4)
float SimulationInverseFloatMin = 8.507059e+37f;
// XBE_GLOBAL(0x00233fd8, 4)
float SimulationTwoPi = 6.283185f;
// XBE_GLOBAL(0x00233fdc, 4)
float SimulationInverseTwoPi = 0.15915495f;
// XBE_GLOBAL(0x00233fe0, 4)
float SimulationDegreesPerRadian = 57.295784f;
// XBE_GLOBAL(0x00233fe4, 4)
float SimulationRadiansPerDegree = 0.01745329f;
// The signature the world and the events' damage names as its source (nothing writes it; the name is ours)
// XBE_GLOBAL(0x00233fe8, 4)
uint32_t DamageSourceSig;
// XBE_GLOBAL(0x00233ff0, 0xf24)
alignas(16) Simulation fgSimulation;
// The scratch pad's free zone, after the Simulation (the name is ours)
// XBE_GLOBAL(0x00234f20, 0x4000)
alignas(16) uint8_t ScratchPadFreeZone[0x4000];

namespace {

constexpr uint32_t kSignatureCounterMax = 0xffffff;
constexpr uint32_t kSignatureCounterRestart = 0x100;   // where the counter starts again after its maximum

constexpr int32_t kRigidBodyLimit = 52;                // CanSpawnRigidBody's, kMaxRigidBodies when forced

// The dead objects' set's ++ is the attribute system's string set's (the code every set with 0x14-byte nodes
// shares)
void NextDeadObject(DeadObjectSet::Iterator *it) {
    reinterpret_cast<StringSet::Iterator *>(it)->Inc();
}

// The vectors' compiled code, on a typed vector
template <class T>
SimPointerVector *Code(SimVector<T> *vector) {
    return reinterpret_cast<SimPointerVector *>(vector);
}

}  // namespace

// FUNC_AT(0x000b25e0)
void Simulation::PauseSimState() {
    if (simState != kSimPaused) {
        savedSimState = simState;
        simState = kSimPaused;
    }
}

// FUNC_AT(0x000b2600)
void Simulation::UnPauseSimState() {
    if (simState == kSimPaused)
        simState = savedSimState;
}

// FUNC_AT(0x000b2620)
void Simulation::AdvanceStep() {
    if (simState != kSimPaused)
        stepCount++;
}

// FUNC_AT(0x000b2630)
int Simulation::AssignRigidBodySlot(PhysicsObject *owner, bool simple) {
    if (simple) {
        for (int slot = 0; slot < kMaxSimpleBodies; slot++) {
            if (simpleRigidBodyOwners[slot] != NULL)
                continue;
            uint32_t signature = uint32_t(slot) << kSignatureSlotShift | NextSignature | kSignatureSimple;
            if (++NextSignature > kSignatureCounterMax)
                NextSignature = kSignatureCounterRestart;
            simpleRigidBodySigs[slot] = signature;
            simpleRigidBodyOwners[slot] = owner;
            numSimpleRigidBodies++;
            return slot;
        }
        return -1;
    }
    for (int slot = 0; slot < kMaxRigidBodies; slot++) {
        if (rigidBodyOwners[slot] != NULL)
            continue;
        uint32_t signature = uint32_t(slot) << kSignatureSlotShift | NextSignature;
        if (++NextSignature > kSignatureCounterMax)
            NextSignature = kSignatureCounterRestart;
        rigidBodySigs[slot] = signature;
        rigidBodyOwners[slot] = owner;
        numRigidBodies++;
        return slot;
    }
    return -1;
}

// FUNC_AT(0x000b2700)
RigidBody* Simulation::GetRigidBody(int slot) {
    return (useScratchPad ? rigidBodiesScratch : rigidBodies) + slot;
}

// FUNC_AT(0x000b2730)
SimpleRigidBody* Simulation::GetSimpleRigidBody(int slot) {
    return (useScratchPad ? simpleRigidBodiesScratch : simpleRigidBodies) + slot;
}

// FUNC_AT(0x000b2760)
RigidBodyInfo* Simulation::GetRigidBodyInfo(int slot) {
    return rigidBodyInfo + slot;
}

// FUNC_AT(0x000b2780)
int Simulation::FindSignature(uint32_t signature, bool *simple) {
    *simple = (signature & kSignatureSimple) != 0;
    int slot = signature >> kSignatureSlotShift & kSignatureSlotMask;
    if (signature != 0 && (*simple ? simpleRigidBodySigs : rigidBodySigs)[slot] == signature)
        return slot;
    return -1;
}

// FUNC_AT(0x000b27d0)
PhysicsObject* Simulation::FindPhysicsObjectSignature(uint32_t signature) {
    if (signature == 0)
        return NULL;
    bool simple;
    int slot = FindSignature(signature, &simple);
    if (slot < 0)
        return NULL;
    return simple ? simpleRigidBodyOwners[slot] : rigidBodyOwners[slot];
}

// FUNC_AT(0x000b2820)
void* Simulation::GetScratchPadFreeZone() {
    return ScratchPadFreeZone;
}

// FUNC_AT(0x000b2830)
void Simulation::DetectCollisionsSRB() {
    for (int slot = 0; slot < kMaxSimpleBodies; slot++) {
        if (simpleRigidBodyOwners[slot] == NULL)
            continue;
        SimpleRigidBody *body = GetSimpleRigidBody(slot);
        if (!body->NeedsCollisionCheck())
            continue;
        simpleBodyHits[slot] = BitSet192();
        body->CheckCollisions(simpleBodyHits[slot].bits);
    }
}

// FUNC_AT(0x000b28d0)
PhysicsObject* Simulation::GetOrderedBody(uint32_t index) {
    if (index < kMaxRigidBodies)
        return rigidBodyOwners[index];
    return simpleRigidBodyOwners[index - kMaxRigidBodies];
}

// FUNC_AT(0x000b2900)
SimVector<TrackedInstance>* Simulation::GetTrackedInstanceList() {
    return &trackedInstances;
}

// FUNC_AT(0x000b2910)
void Simulation::SetReflectiveObject(uint32_t signature) {
    reflectiveObjectSig = signature;
}

// FUNC_AT(0x000b2980)
void Simulation::ReleaseRigidBodySlot(int slot, bool simple) {
    if (simple) {
        GetSimpleRigidBody(slot)->Destruct();
        numSimpleRigidBodies--;
        simpleRigidBodyOwners[slot] = NULL;
        simpleRigidBodySigs[slot] = 0;
    } else {
        GetRigidBody(slot)->Destruct();
        numRigidBodies--;
        rigidBodyOwners[slot] = NULL;
        rigidBodySigs[slot] = 0;
    }
}

// FUNC_AT(0x000b2b50)
void Simulation::SetStartConditions(const Coord3 *position, const Coord3 *direction) {
    Coord3 point = *position;
    float height;
    if (!fgCollisionMgr->GetWorldHeightAtPoint(&point, &height, false))
        height = 0.0f;
    point.y = height + 1.0f;
    MATRIX4 generated;
    MATRIX4 orientation = *Util_GenerateMatrix(&generated, direction);
    GetRigidBody(GetPlayerObject()->rigidBodySlot)->ResetObject(&orientation, &point);
}

// FUNC_AT(0x000b2c10)
void Simulation::DetonateRemoteMines() {
    for (Mine **it = mines.first; it != mines.last; it++) {
        if ((*it)->kind == kWeaponRemMine)
            (*it)->detonate = 1;
    }
}

// The bodies are the global Simulation's, not this one's (there is only one). Only bodies not awake are candidates,
// and at exactly the limit nothing is put to sleep and the answer is no.
// FUNC_AT(0x000b2c40)
bool Simulation::CanSpawnRigidBody(const Coord3 *position, bool force) {
    int32_t limit = force ? kMaxRigidBodies : kRigidBodyLimit;
    if (numRigidBodies > limit) {
        float farthest = 0.0f;
        Smackable *sleeper = NULL;
        for (Smackable **it = smackables.first; it != smackables.last; it++) {
            RigidBody *body = fgSimulation.GetRigidBody((*it)->rigidBodySlot);
            if (body->sleepState == RigidBody::kAwake)
                continue;
            double dx = double(body->position.x) - position->x;
            double dy = double(body->position.y) - position->y;
            double dz = double(body->position.z) - position->z;
            double distance = dz * dz + dy * dy + dx * dx;
            if (distance > farthest) {
                farthest = float(distance);
                sleeper = *it;
            }
        }
        if (sleeper != NULL)
            sleeper->GoToSleep();
    }
    return numRigidBodies < limit;
}

// FUNC_AT(0x000b2d30)
PhysicsObject* Simulation::GetPlayerObject() {
    return cars.first[0];
}

// FUNC_AT(0x000b2d40)
Coord3* Simulation::GetReflectiveObjectPosition() {
    if (reflectiveObjectSig == 0) {
        uint32_t signature;
        reflectiveObjectSig = *GetPlayerObject()->GetSig(&signature);
    }
    int slot = reflectiveObjectSig >> kSignatureSlotShift & kSignatureSlotMask;
    if (reflectiveObjectSig & kSignatureSimple)
        return &GetSimpleRigidBody(slot)->position;
    return &GetRigidBody(slot)->position;
}

// std::remove then erase(iterator), inlined: one instance is tracked once
// FUNC_AT(0x000b3000)
void Simulation::UntrackInstance(CARP::Instance *instance) {
    TrackedInstance **first = trackedInstances.first;
    TrackedInstance **last = trackedInstances.last;
    for (TrackedInstance **it = first; it != trackedInstances.last; it++) {
        TrackedInstance *found = *it;
        if (found->instance != instance)
            continue;
        void **newLast;
        PointerRemove(&newLast, reinterpret_cast<void **>(first), reinterpret_cast<void **>(last),
                      reinterpret_cast<void *const *>(&found));
        void **ignored;
        Code(&trackedInstances)->Erase(&ignored, newLast);
        OperatorDelete(found);
        return;
    }
}

// FUNC_AT(0x000b30c0)
void Simulation::UntrackAllInstances() {
    for (TrackedInstance **it = trackedInstances.first; it != trackedInstances.last; it++)
        OperatorDelete(*it);
    if (trackedInstances.first != NULL)
        UMemory::FastFree(trackedInstances.first, trackedInstances.Capacity() * sizeof(TrackedInstance *));
    trackedInstances.first = NULL;
    trackedInstances.last = NULL;
    trackedInstances.end = NULL;
}

// FUNC_AT(0x000b3fc0)
void Simulation::DeleteDeadObjects() {
    SimDeleteInProgress = true;
    DeadObjectNode *head = deadObjects->head;
    for (DeadObjectSet::Iterator it = {head->left}; it.node != head; NextDeadObject(&it)) {
        PhysicsObjNode *entry = it.node->value;
        PhysicsObject *object = static_cast<PhysicsObject *>(entry->value.object);
        if (fgRenderHigh != NULL) {
            RPlayerCamera *camera = fgRenderHigh->views[0].camera;
            if (camera != NULL && object->type == kObjectMissile && object == camera->anchor)
                camera->EndMissileCamera();
        }
        PhysicsObjIterator ignored;
        objects->Erase(&ignored, PhysicsObjIterator{entry});
        if (object != NULL)
            (object->*XbeVirtual<decltype(&PhysicsObject::Delete)>(object, 0))(1);
    }
    DeadObjectSet *dead = deadObjects;
    dead->EraseSubtree(dead->head->parent);
    dead->head->parent = dead->head;
    dead->size = 0;
    dead->head->left = dead->head;
    dead->head->right = dead->head;
    SimDeleteInProgress = false;
}
