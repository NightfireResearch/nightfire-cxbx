#pragma fp_contract(off)

#include "PhysicsObject.h"

#include <bit>

#include "RigidBody.h"
#include "SimpleRigidBody.h"
#include "../../common/xbeOverload.h"     // XbeVirtual
#include "../../helpers.h"
#include "../engine/CoreFoundation.h"     // NullFunction
#include "../engine/UMemory.hpp"
#include "../platform/RealMath.h"
#include "../render/RSceneObj.hpp"
#include "../world/Collider.h"
#include "../world/SoundGroup.h"          // ABaseSound

// ---------------------------------------------------------------------------------------------------------------
// PhysicsObject (0x0006f070-0x0006f810, and three entry points elsewhere), ported from the listing. Most methods
// ask the Simulation for the object's body by its slot - a RigidBody unless kSimpleBody.
// ---------------------------------------------------------------------------------------------------------------

// ---- the game's code not ported yet
#define Simulation_AssignRigidBodySlot ((int (__fastcall *)(void *, int, PhysicsObject *object, int simple))0x000b2630)
#define Simulation_GetRigidBody ((RigidBody *(__fastcall *)(void *, int, int slot))0x000b2700)
#define Simulation_GetSimpleRigidBody ((SimpleRigidBody *(__fastcall *)(void *, int, int slot))0x000b2730)
#define Simulation_FindPhysicsObjectSignature ((PhysicsObject *(__fastcall *)(void *, int, uint32_t sig))0x000b27d0)
#define Simulation_ReleaseRigidBodySlot ((void (__fastcall *)(void *, int, int slot, int simple))0x000b2980)
#define RSceneObj_SetPhysics ((void (__fastcall *)(RSceneObj *, int, PhysicsObject *physics))0x0008f580)
#define RSceneObj_GetBoundingDimensions ((void (__fastcall *)(RSceneObj *, int, Coord4 *bounds))0x0008dfd0)
#define RSceneObj_GetCollisionGeometry ((void *(__fastcall *)(RSceneObj *, int, uint32_t *count))0x0008e060)
#define Handle_Stop ((void (__fastcall *)(Handle *, int))0x00077be0)
#define Handle_ProcessStimuli ((void (__fastcall *)(Handle *, int, uint32_t stimulus, uint32_t step, int unknown))0x00077e00)
#define IFeedback_Destruct ((void (__fastcall *)(IFeedback *, int))0x0004fb10)

// ---- globals
#define PhysicsObjectVtable ((void **)0x0018f9a0)
#define Sim ((void *)0x00233ff0)                    // the Simulation
#define RigidBodySigs ((uint32_t *)0x00234020)      // [64] the Simulation's signatures of its bodies, by slot
#define SimpleBodySigs ((uint32_t *)0x00234120)     // [96]
#define SimStepCount U32_AT(0x00234e34)             // the Simulation's steps so far

namespace {

constexpr float kForceScale = 1.0f / 300.0f;
static_assert(std::bit_cast<uint32_t>(kForceScale) == 0x3b5a740e, "the original's 1/300");

constexpr uint32_t kFeedbackSize = 4;   // an IFeedback

SimpleRigidBody *SimpleBody(const PhysicsObject *object) {
    return Simulation_GetSimpleRigidBody(Sim, 0, object->rigidBodySlot);
}

// A render object's offset (its vtable's slot 9), answered in ST0 unrounded
double RenderOffset(RSceneObj *object) {
    typedef double (RSceneObj::*GetRenderOffsetMethod)();
    return (object->*XbeVirtual<GetRenderOffsetMethod>(object, 9))();
}

// RSceneObj::UpdatePosition (its vtable's slot 14)
void UpdateRenderPosition(RSceneObj *object) {
    typedef void (RSceneObj::*UpdatePositionMethod)(int);
    (object->*XbeVirtual<UpdatePositionMethod>(object, 14))(1);
}

// A scalar deleting destructor (the vtable's slot 0)
template <class T> void DeleteObject(T *object) {
    typedef T *(T::*DeleteMethod)(unsigned flags);
    (object->*XbeVirtual<DeleteMethod>(object, 0))(1);
}

} // namespace

// FUNC_AT(0x0006f070)
PhysicsObject* PhysicsObject::Construct(const AttributeSet &attributes, int type) {
    vtable = PhysicsObjectVtable;
    worldPos.Construct();
    this->type = type;
    flags = 0;
    rigidBodySlot = Simulation_AssignRigidBodySlot(Sim, 0, this, false);
    renderObject = NULL;
    audioObject = NULL;
    feedbackObject = NULL;
    hitPointLoc = NULL;
    ownerSig = 0;
    this->attributes.ConstructCopy(attributes);
    collider = NULL;
    unknown68 = 0;
    return this;
}

// FUNC_AT(0x0006f100)
PhysicsObject* PhysicsObject::Construct(const char *className, const char *name, int type) {
    vtable = PhysicsObjectVtable;
    worldPos.Construct();
    this->type = type;
    flags = 0;
    rigidBodySlot = Simulation_AssignRigidBodySlot(Sim, 0, this, false);
    renderObject = NULL;
    audioObject = NULL;
    feedbackObject = NULL;
    hitPointLoc = NULL;
    ownerSig = 0;
    attributes.Construct(className, name);
    collider = NULL;
    unknown68 = 0;
    return this;
}

// FUNC_AT(0x0006f190)
PhysicsObject* PhysicsObject::Construct(const char *className, const char *name, int type, uint32_t ownerSig, bool flag2, bool flag4) {
    vtable = PhysicsObjectVtable;
    worldPos.Construct();
    this->type = type;
    flags = kSimpleBody;
    rigidBodySlot = Simulation_AssignRigidBodySlot(Sim, 0, this, true);
    renderObject = NULL;
    audioObject = NULL;
    feedbackObject = NULL;
    hitPointLoc = NULL;
    this->ownerSig = ownerSig;
    attributes.Construct(className, name);
    collider = NULL;
    unknown68 = 0;
    if (flag2)
        flags |= kFlag2;
    if (flag4)
        flags |= kFlag4;
    return this;
}

// FUNC_AT(0x0006f680)
void PhysicsObject::Destruct() {
    vtable = PhysicsObjectVtable;
    if (collider != NULL) {
        collider->Destruct();
        UMemory::FastFree(collider, sizeof(WCollider));
    }
    collider = NULL;
    RSceneObj *render = renderObject;
    if (render != NULL)
        Handle_Stop(render->animHandle, 0);
    SetRenderObject(NULL);
    if (render != NULL)
        DeleteObject(render);
    if (audioObject != NULL) {
        ABaseSound *audio = audioObject;
        audioObject = NULL;
        DeleteObject(audio);
    }
    if (feedbackObject != NULL) {
        IFeedback *feedback = feedbackObject;
        feedbackObject = NULL;
        IFeedback_Destruct(feedback, 0);
        UMemory::FastFree(feedback, kFeedbackSize);
    }
    Simulation_ReleaseRigidBodySlot(Sim, 0, rigidBodySlot, flags & kSimpleBody);
    attributes.Destruct();
    NullFunction();   // ~WWorldPos
}

// FUNC_AT(0x00062110)
void PhysicsObject::DestructThunk() {
    Destruct();
}

// FUNC_AT(0x0006f7f0)
PhysicsObject* PhysicsObject::Delete(unsigned flags) {
    Destruct();
    if (flags & 1)
        UMemory::FastFree(this, sizeof(PhysicsObject));
    return this;
}

// FUNC_AT(0x0001c180)
RigidBody* PhysicsObject::GetRigidBody() {
    return Simulation_GetRigidBody(Sim, 0, rigidBodySlot);
}

// FUNC_AT(0x0006f240)
float PhysicsObject::GetMass() {
    if (!(flags & kSimpleBody))
        return GetRigidBody()->mass;
    return SimpleBody(this)->mass;
}

// FUNC_AT(0x0006f270)
float PhysicsObject::GetRadius() {
    if (!(flags & kSimpleBody))
        return GetRigidBody()->radius;
    return SimpleBody(this)->radius;
}

// A simple body takes the force as an acceleration, force x (1/300) / mass, and only with kFlag04; the torque is
// ignored.
// FUNC_AT(0x0006f2a0)
void PhysicsObject::ApplyForces(const Coord3 *force, const Coord3 *torque) {
    if (!(flags & kSimpleBody)) {
        RigidBody *body = GetRigidBody();
        body->ResolveForce(force);
        body->ResolveTorque(torque);
        body->flags |= RigidBody::kFlag0;
    } else {
        SimpleRigidBody *body = SimpleBody(this);
        if (body->flags & SimpleRigidBody::kFlag04) {
            Coord3 acceleration;
            VU0_v4scale(force, kForceScale / body->mass, &acceleration);
            body->Accelerate(&acceleration);
        }
    }
}

// FUNC_AT(0x0006f330)
Coord3* PhysicsObject::GetPosition() {
    if (!(flags & kSimpleBody))
        return &GetRigidBody()->position;
    return &SimpleBody(this)->position;
}

// FUNC_AT(0x0006f360)
Coord3* PhysicsObject::GetLinearVelocity() {
    if (!(flags & kSimpleBody))
        return &GetRigidBody()->velocity;
    return &SimpleBody(this)->velocity;
}

// FUNC_AT(0x0006f390)
uint32_t* PhysicsObject::GetSig(uint32_t *result) {
    if (!(flags & kSimpleBody))
        *result = RigidBodySigs[GetRigidBody()->ownerIndex];
    else
        *result = SimpleBodySigs[SimpleBody(this)->slot];
    return result;
}

// FUNC_AT(0x0006f3e0)
DamageZone* PhysicsObject::GetDamageZones(uint32_t *count) {
    *count = 0;
    return NULL;
}

// FUNC_AT(0x0006f3f0)
void PhysicsObject::SetRenderObject(RSceneObj *object) {
    RSceneObj *previous = renderObject;
    if (previous == object)
        return;
    renderObject = object;
    if (previous != NULL && previous->physics == this)
        RSceneObj_SetPhysics(previous, 0, NULL);
    if (renderObject != NULL)
        RSceneObj_SetPhysics(renderObject, 0, this);
}

// FUNC_AT(0x0006f430)
void PhysicsObject::SetAudioObject(ABaseSound *object) {
    if (audioObject != object)
        audioObject = object;
}

// FUNC_AT(0x0006f440)
void PhysicsObject::SetFeedbackObject(IFeedback *object) {
    if (feedbackObject != object)
        feedbackObject = object;
}

// FUNC_AT(0x0006f450)
float PhysicsObject::GetHitPoints() {
    if (hitPointLoc != NULL)
        return *hitPointLoc;
    return 1.0f;
}

// FUNC_AT(0x0006f470)
void PhysicsObject::LoseHitPoints(float amount) {
    if (hitPointLoc == NULL)
        return;
    if (amount < 0.0f)
        amount = 0.0f;
    *hitPointLoc = *hitPointLoc - amount;
    if (*hitPointLoc < 0.0f)
        *hitPointLoc = 0.0f;
}

// FUNC_AT(0x0006f4c0)
void PhysicsObject::Simulate() {
    if (renderObject != NULL)
        UpdateRenderPosition(renderObject);
}

// FUNC_AT(0x0006f4d0)
void* PhysicsObject::GetCollisionGeometry(uint32_t *count, float *offset) {
    if (renderObject == NULL) {
        *offset = 0.0f;
        *count = 0;
        return NULL;
    }
    *offset = (float)(RenderOffset(renderObject) * -1.0);
    return RSceneObj_GetCollisionGeometry(renderObject, 0, count);
}

// FUNC_AT(0x0006f520)
bool PhysicsObject::GetCollisionBounds(Coord4 *bounds) {
    if (!(flags & kSimpleBody) || !(SimpleBody(this)->flags & SimpleRigidBody::kFlag01)) {
        if (renderObject != NULL) {
            RSceneObj_GetBoundingDimensions(renderObject, 0, bounds);
            return true;
        }
    }
    return false;
}

// FUNC_AT(0x0006f560)
void PhysicsObject::PlayAnimation(uint32_t stimulus) {
    if (renderObject != NULL)
        Handle_ProcessStimuli(renderObject->animHandle, 0, stimulus, SimStepCount, 2);
}

// FUNC_AT(0x0006f580)
void PhysicsObject::SetOwnerObject(PhysicsObject *owner) {
    if (owner != NULL) {
        uint32_t sig;
        ownerSig = *owner->GetSig(&sig);
    } else {
        ownerSig = 0;
    }
}

// FUNC_AT(0x0006f5b0)
bool PhysicsObject::IsOwnedBy(PhysicsObject *owner) {
    if (owner == NULL)
        return false;
    uint32_t sig;
    owner->GetSig(&sig);
    if (ownerSig == 0)
        return false;
    PhysicsObject *object = Simulation_FindPhysicsObjectSignature(Sim, 0, ownerSig);
    while (object != NULL) {
        uint32_t objectSig;
        if (*object->GetSig(&objectSig) == sig)
            return true;
        PhysicsObject *next = Simulation_FindPhysicsObjectSignature(Sim, 0, object->ownerSig);
        if (next == object)
            return false;
        object = next;
    }
    return false;
}

// FUNC_AT(0x0006f660)
void PhysicsObject::DebugObject() {
    if (!(flags & kSimpleBody)) {
        GetRigidBody();
        NullFunction();   // the body's debug call: an empty function in this build
    }
}

// FUNC_AT(0x0006f780)
int PhysicsObject::ApplyDamage(const void *unknown1, const void *unknown2, float amount, float unknown4, int kind,
                               const uint32_t *sourceSig) {
    LoseHitPoints(amount);
    return 0x20;
}

// FUNC_AT(0x0006f7e0)
void PhysicsObject::SetHitPointLoc(float *hitPoints) {
    hitPointLoc = hitPoints;
}

// FUNC_AT(0x00097a90)
void PhysicsObject::ComputeImpulse(const Coord3 *unknown1, const Coord3 *unknown2) {
}
