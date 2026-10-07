#ifndef DRIVING_PHYSICS_PHYSICSOBJECT_H_
#define DRIVING_PHYSICS_PHYSICSOBJECT_H_

#include <stddef.h>
#include <stdint.h>

#include "../../common/xbeOverload.h"   // XbeVirtual
#include "../data/AttributeSet.h"
#include "../data/CoordConvert.h"       // Coord3, Coord4
#include "../world/WorldPos.h"

// ---------------------------------------------------------------------------------------------------------------
// PhysicsObject (0x6c bytes): the game object that owns one of the Simulation's bodies - a RigidBody, or with
// kSimpleBody a SimpleRigidBody, by its slot - with its attributes, hit points, and the render, audio and feedback
// objects and collider it is linked to. The base of the cars, helicopters, smackables, humans, explosions and the
// rest; its vtable (0x0018f9a0, seven slots) is overridden by each. See PhysicsObject.cpp.
// ---------------------------------------------------------------------------------------------------------------

class RSceneObj;
class WCollider;
struct ABaseSound;
struct DamageZone;
struct IFeedback;
struct RigidBody;
struct SimpleRigidBody;

struct PhysicsObject {
    enum Flag : uint16_t {
        kSimpleBody = 0x1,          // its body is a SimpleRigidBody
        kFlag2 = 0x2,               // the six-argument constructor's fifth argument
        kFlag4 = 0x4,               // its sixth
    };

    void **vtable;              // +0x00
    WWorldPos worldPos;         // +0x04
    int32_t type;               // +0x44
    uint16_t flags;             // +0x48 Flag
    int16_t rigidBodySlot;      // +0x4a its body, in the Simulation's rigid or simple tables
    RSceneObj *renderObject;    // +0x4c
    ABaseSound *audioObject;    // +0x50
    IFeedback *feedbackObject;  // +0x54
    float *hitPointLoc;         // +0x58 where its hit points are kept; NULL: none
    uint32_t ownerSig;          // +0x5c its owner's signature (Simulation::FindPhysicsObjectSignature); 0 none
    AttributeSet attributes;    // +0x60
    WCollider *collider;        // +0x64
    uint32_t unknown68;

    // The three constructors: a copy of an attribute set, or the set of `name` in `className`
    PhysicsObject* Construct(const AttributeSet &attributes, int type);                         // 0x0006f070
    PhysicsObject* Construct(const char *className, const char *name, int type);                // 0x0006f100
    // ... with a simple body, an owner and flags
    PhysicsObject* Construct(const char *className, const char *name, int type, uint32_t ownerSig, bool flag2,
                             bool flag4);                                                       // 0x0006f190
    void Destruct();                                                                            // 0x0006f680
    void DestructThunk();       // a second entry to the destructor (a jump to it)               // 0x00062110
    PhysicsObject* Delete(unsigned flags);      // the scalar deleting destructor, vtable slot 0 // 0x0006f7f0

    RigidBody* GetRigidBody();                                                                  // 0x0001c180
    float GetMass();                                                                            // 0x0006f240
    float GetRadius();                                                                          // 0x0006f270
    void ApplyForces(const Coord3 *force, const Coord3 *torque);                                // 0x0006f2a0
    Coord3* GetPosition();                                                                      // 0x0006f330
    Coord3* GetLinearVelocity();                                                                // 0x0006f360
    // Its body's signature (a structure the original answers through the caller's result pointer)
    uint32_t* GetSig(uint32_t *result);                                                         // 0x0006f390
    void SetRenderObject(RSceneObj *renderObject);                                              // 0x0006f3f0
    void SetAudioObject(ABaseSound *audioObject);                                               // 0x0006f430
    void SetFeedbackObject(IFeedback *feedbackObject);                                          // 0x0006f440
    float GetHitPoints();       // 1 without hit points                                         // 0x0006f450
    void LoseHitPoints(float amount);                                                           // 0x0006f470
    void Simulate();                                                                            // 0x0006f4c0
    // The render object's collision geometry and its count, and its render offset negated; all zero without one
    void* GetCollisionGeometry(uint32_t *count, float *offset);                                 // 0x0006f4d0
    bool GetCollisionBounds(Coord4 *bounds);                                                    // 0x0006f520
    void PlayAnimation(uint32_t stimulus);                                                      // 0x0006f560
    void SetOwnerObject(PhysicsObject *owner);                                                  // 0x0006f580
    // Whether `owner` is its owner, or its owner's owner, and so on
    bool IsOwnedBy(PhysicsObject *owner);                                                       // 0x0006f5b0
    void SetHitPointLoc(float *hitPoints);                                                      // 0x0006f7e0

    // ---- virtual (the slot in the vtable)
    // 2: takes `amount` off the hit points; answers 0x20
    int ApplyDamage(const void *unknown1, const void *unknown2, float amount, float unknown4, int kind,
                    const uint32_t *sourceSig);                                                 // 0x0006f780
    DamageZone* GetDamageZones(uint32_t *count);    // 3: none                                  // 0x0006f3e0
    void DebugObject();                             // 5                                         // 0x0006f660
    void ComputeImpulse(const Coord3 *unknown1, const Coord3 *unknown2);   // 6: nothing         // 0x00097a90

    // ---- calls through the vtable, reaching the object's own override (SetInShock is PBondCar's name for its)
    void SetInShock(float shock) {                                                              // slot 1
        typedef void (PhysicsObject::*SetInShockMethod)(float shock);
        (this->*XbeVirtual<SetInShockMethod>(this, 1))(shock);
    }
    int ApplyDamageVirtual(const void *unknown1, const void *unknown2, float amount, float unknown4, int kind,
                           const uint32_t *sourceSig) {                                         // slot 2
        return (this->*XbeVirtual<decltype(&PhysicsObject::ApplyDamage)>(this, 2))(unknown1, unknown2, amount,
                                                                                   unknown4, kind, sourceSig);
    }
};
static_assert(sizeof(PhysicsObject) == 0x6c, "a physics object is 108 bytes");
static_assert(offsetof(PhysicsObject, worldPos) == 0x04 && offsetof(PhysicsObject, type) == 0x44 &&
              offsetof(PhysicsObject, flags) == 0x48 && offsetof(PhysicsObject, rigidBodySlot) == 0x4a &&
              offsetof(PhysicsObject, renderObject) == 0x4c && offsetof(PhysicsObject, hitPointLoc) == 0x58 &&
              offsetof(PhysicsObject, attributes) == 0x60 && offsetof(PhysicsObject, collider) == 0x64,
              "physics object layout");

#endif // DRIVING_PHYSICS_PHYSICSOBJECT_H_
