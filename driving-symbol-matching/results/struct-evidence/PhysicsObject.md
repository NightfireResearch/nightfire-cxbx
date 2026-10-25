# PhysicsObject

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x6f7f0 frees/deletes with size 0x6c (call to UMemory::FastFree)
Xbox vtable 0x0018f9a0 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['PhysicsObject virtual table']
constructor 0x6f070 first calls: ['WWorldPos::WWorldPos', 'Simulation::AssignRigidBodySlot', 'AttributeSet::AttributeSet']
constructor 0x6f100 first calls: ['WWorldPos::WWorldPos', 'Simulation::AssignRigidBodySlot', 'AttributeSet::AttributeSet']
constructor 0x6f190 first calls: ['WWorldPos::WWorldPos', 'Simulation::AssignRigidBodySlot', 'AttributeSet::AttributeSet']

Xbox methods (28):
  0x62110 undefined __thiscall ~PhysicsObject(PhysicsObject * this)
  0x6f070 undefined PhysicsObject(undefined4 param_1, undefined4 param_2)
  0x6f100 undefined PhysicsObject(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x6f190 undefined PhysicsObject(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefi
  0x6f240 undefined __thiscall GetMass(PhysicsObject * this)
  0x6f270 undefined __thiscall GetRadius(PhysicsObject * this)
  0x6f2a0 undefined __thiscall ApplyForces(PhysicsObject * this, COORD3 * param_1, COORD3 * param_2)
  0x6f330 _VECTOR * __thiscall GetPosition(PhysicsObject * this)
  0x6f360 _VECTOR * __thiscall GetLinearVelocity(PhysicsObject * this)
  0x6f390 undefined8 __thiscall GetSig(PhysicsObject * this)
  0x6f3e0 undefined __thiscall GetDamageZones(PhysicsObject * this, uint * param_1)
  0x6f3f0 void __thiscall SetRenderObject(PhysicsObject * this, RSceneObj * param_1)
  0x6f430 undefined __thiscall SetAudioObject(PhysicsObject * this, void * param_1)
  0x6f440 undefined __thiscall SetFeedbackObject(PhysicsObject * this, IFeedback * param_1)
  0x6f450 undefined __thiscall GetHitPoints(PhysicsObject * this)
  0x6f470 undefined __thiscall LoseHitPoints(PhysicsObject * this, float param_1)
  0x6f4c0 undefined __thiscall Simulate(PhysicsObject * this)
  0x6f4d0 undefined GetCollisionGeometry(undefined4 param_1, undefined4 param_2)
  0x6f520 undefined __thiscall GetCollisionBounds(PhysicsObject * this, COORD4 * param_1)
  0x6f560 undefined PlayAnimation(undefined4 param_1)
  0x6f580 void __thiscall SetOwnerObject(PhysicsObject * this, PhysicsObject * param_1)
  0x6f5b0 undefined __thiscall IsOwnedBy(PhysicsObject * this, PhysicsObject * param_1)
  0x6f660 undefined __thiscall DebugObject(PhysicsObject * this)
  0x6f680 undefined __thiscall ~PhysicsObject(PhysicsObject * this)
  0x6f780 undefined ApplyDamage(undefined param_1, undefined param_2, undefined4 param_3)
  0x6f7e0 undefined __thiscall SetHitPointLoc(PhysicsObject * this, float * param_1)
  0x6f7f0 PhysicsObject * __thiscall scalar_deleting_destructor(PhysicsObject * this, uint flags)
  0x97a90 undefined __thiscall ComputeImpulse(PhysicsObject * this, COORD3 * param_1, COORD3 * param_2)

PS2 methods (36):
  0x195330 PhysicsObject::PhysicsObject
  0x1953c8 PhysicsObject::PhysicsObject
  0x195470 PhysicsObject::PhysicsObject
  0x195568 PhysicsObject::~PhysicsObject
  0x195688 PhysicsObject::GetMass
  0x1956d8 PhysicsObject::GetRadius
  0x195728 PhysicsObject::ApplyForces
  0x195808 PhysicsObject::GetPosition
  0x195858 PhysicsObject::GetLinearVelocity
  0x1958a8 PhysicsObject::GetSig
  0x195938 PhysicsObject::SetInShock
  0x195940 PhysicsObject::ApplyDamage
  0x195960 PhysicsObject::GetDamageZones
  0x195970 PhysicsObject::ComputeImpulse
  0x195978 PhysicsObject::SetRenderObject
  0x1959d8 PhysicsObject::SetAudioObject
  0x1959f0 PhysicsObject::SetFeedbackObject
  0x195a08 PhysicsObject::SetHitPointLoc
  0x195a28 PhysicsObject::ValidateHitPoints
  0x195a30 PhysicsObject::GetHitPoints
  0x195a58 PhysicsObject::LoseHitPoints
  0x195aa8 PhysicsObject::Simulate
  0x195ae0 PhysicsObject::GetCollisionGeometry
  0x195b58 PhysicsObject::GetCollisionBounds
  0x195bd0 PhysicsObject::PlayAnimation
  0x195c00 PhysicsObject::PlayAnimation
  0x195c30 PhysicsObject::SetOwnerObject
  0x195c70 PhysicsObject::IsOwnedBy
  0x195d28 PhysicsObject::DebugObject
  0x195d68 PhysicsObject::Debug
  0x195f80 PhysicsObject::operator_new
  0x195fa0 PhysicsObject::operator_delete
  0x195ff0 PhysicsObject::GetSimpleRigidBody
  0x196018 PhysicsObject::GetSimpleRigidBodyPtr
  0x196040 PhysicsObject::GetRigidBody
  0x196068 PhysicsObject::GetRigidBodyPtr

Sheet rows:
  PhysicsObject::PhysicsObject(AttributeSet &, PhysicsObjectType)
  PhysicsObject::PhysicsObject(char *, char *, PhysicsObjectType)
  PhysicsObject::PhysicsObject(char *, char *, PhysicsObjectType,
  PhysicsObject::~PhysicsObject(void)
  PhysicsObject::GetMass(void) const
  PhysicsObject::GetRadius(void) const
  PhysicsObject::ApplyForces(COORD3 &, COORD3 &)
  PhysicsObject::GetPosition(void) const
  PhysicsObject::GetLinearVelocity(void) const
  PhysicsObject::GetSig(void) const
  PhysicsObject::SetInShock(float)
  PhysicsObject::ApplyDamage(COORD3 &, COORD3 &, float, float, Da
  PhysicsObject::GetDamageZones(unsigned int &)
  PhysicsObject::ComputeImpulse(COORD3 &, COORD3 &)
  PhysicsObject::SetRenderObject(RSceneObj *)
  PhysicsObject::SetAudioObject(ASceneObj *)
  PhysicsObject::SetFeedbackObject(IFeedback *)
  PhysicsObject::SetHitPointLoc(float *)
  PhysicsObject::ValidateHitPoints(void) const
  PhysicsObject::GetHitPoints(void)
  PhysicsObject::LoseHitPoints(float)
  PhysicsObject::Simulate(void)
  PhysicsObject::GetCollisionGeometry(unsigned int &, float &) co
  PhysicsObject::GetCollisionBounds(COORD4 &) const
  PhysicsObject::PlayAnimation(unsigned int)
  PhysicsObject::PlayAnimation(unsigned int, unsigned int)
  PhysicsObject::SetOwnerObject(PhysicsObject *)
  PhysicsObject::IsOwnedBy(PhysicsObject *)
  PhysicsObject::DebugObject(void)
  PhysicsObject::Debug(void)
  PhysicsObject type_info function
  PhysicsObject::operator new(unsigned int)
  PhysicsObject::operator delete(void *, unsigned int)
  PhysicsObject::GetPhysicsObjectType(void) const
  PhysicsObject::GetRigidBodyIndex(void) const
  PhysicsObject::IsSimpleRigidBody(void) const
  PhysicsObject::IsRigidBody(void) const
  PhysicsObject::GetSimpleRigidBody(void) const
  PhysicsObject::GetSimpleRigidBodyPtr(void) const
  PhysicsObject::GetRigidBody(void) const
  PhysicsObject::GetRigidBodyPtr(void) const
  PhysicsObject::GetAttributes(void) const
  PhysicsObject::GetCollider(void)
  PhysicsObject::GetRenderObject(void) const
  PhysicsObject::GetAudioObject(void) const
  PhysicsObject::GetFeedbackObject(void) const
  PhysicsObject::DetachRenderObject(void)
  PhysicsObject::DetachAudioObject(void)
  PhysicsObject::DetachFeedbackObject(void)
  PhysicsObject::GetWPos(void)
  PhysicsObject::GetWPos(void) const
  PhysicsObject::NotTracer(int)
  PhysicsObject::GetModifiableAttributes(void)
  PhysicsObject::kDamageThreshHighImpact
  PhysicsObject::kDamageThreshMedImpact
  PhysicsObject::kDamageThreshLowImpact
  PhysicsObject::kDamageThreshAnyImpact
  PhysicsObject::kDamageThreshExplosion
  PhysicsObject::kDamageThreshDamage
  PhysicsObject::kDamageThreshEffect
  PhysicsObject::kDamageThreshSplash
  PhysicsObject virtual table
  PhysicsObject type_info node

Xbox methods treated as members (28 of 28; untyped ones count when ECX is read before it is written): ApplyDamage, ApplyForces, ComputeImpulse, DebugObject, GetCollisionBounds, GetCollisionGeometry, GetDamageZones, GetHitPoints, GetLinearVelocity, GetMass, GetPosition, GetRadius, GetSig, IsOwnedBy, LoseHitPoints, PhysicsObject, PlayAnimation, SetAudioObject, SetFeedbackObject, SetHitPointLoc, SetOwnerObject, SetRenderObject, Simulate, scalar_deleting_destructor, ~PhysicsObject

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [4: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, ~PhysicsObject@6f680]
  +0x004  w- LEA addr-taken -> WWorldPos::WWorldPos, dummyNullFunction [4: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, ~PhysicsObject@6f680]
  +0x044  w[4] W [3: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190]
  +0x048  w[1, 2] R/RW/W [12: ApplyForces@6f2a0, DebugObject@6f660, GetCollisionBounds@6f520, GetLinearVelocity@6f360, GetMass@6f240, GetPosition@6f330…]
  +0x04a  w[2] R/W [12: ApplyForces@6f2a0, DebugObject@6f660, GetCollisionBounds@6f520, GetLinearVelocity@6f360, GetMass@6f240, GetPosition@6f330…]
  +0x04c  w[4] R/W -> RSceneObj::GetBoundingDimensions, RSceneObj::GetCollisionGeometry, RSceneObj::SetPhysics [9: GetCollisionBounds@6f520, GetCollisionGeometry@6f4d0, PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, PlayAnimation@6f560…]
  +0x050  w[4] R/W [5: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, SetAudioObject@6f430, ~PhysicsObject@6f680]
  +0x054  w[4] R/W -> IFeedback::~IFeedback [5: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, SetFeedbackObject@6f440, ~PhysicsObject@6f680]
  +0x058  w[4] R/W [7: ApplyDamage@6f780, GetHitPoints@6f450, LoseHitPoints@6f470, PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190…]
  +0x05c  w[4] R/W [5: IsOwnedBy@6f5b0, PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, SetOwnerObject@6f580]
  +0x060  w- LEA addr-taken -> AttributeSet::AttributeSet, AttributeSet::~AttributeSet [4: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, ~PhysicsObject@6f680]
  +0x064  w[4] R/W [4: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190, ~PhysicsObject@6f680]
  +0x068  w[4] W [3: PhysicsObject@6f070, PhysicsObject@6f100, PhysicsObject@6f190]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: GetSig@1958a8]
  +0x040  w[4] W [3: PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470]
  +0x044  w[2] R/W [11: ApplyForces@195728, DebugObject@195d28, GetCollisionBounds@195b58, GetLinearVelocity@195858, GetMass@195688, GetPosition@195808…]
  +0x046  w[2] R/W [15: ApplyForces@195728, DebugObject@195d28, GetCollisionBounds@195b58, GetLinearVelocity@195858, GetMass@195688, GetPosition@195808…]
  +0x048  w[4] R/W -> RSceneObj::GetBoundingDimensions, RSceneObj::SetPhysics [10: GetCollisionBounds@195b58, GetCollisionGeometry@195ae0, PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470, PlayAnimation@195bd0…]
  +0x04c  w[4] R/W [5: PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470, SetAudioObject@1959d8, ~PhysicsObject@195568]
  +0x050  w[4] R/W [5: PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470, SetFeedbackObject@1959f0, ~PhysicsObject@195568]
  +0x054  w[4] R/W [5: GetHitPoints@195a30, LoseHitPoints@195a58, PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470]
  +0x058  w[4] R/W [5: IsOwnedBy@195c70, PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470, SetOwnerObject@195c30]
  +0x05c  w- LEA addr-taken [4: PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470, ~PhysicsObject@195568]
  +0x060  w[4] R/W -> WCollider::~WCollider [4: PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470, ~PhysicsObject@195568]
  +0x064  w[4] W [3: PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470]
  +0x068  w[4] W [4: PhysicsObject@195330, PhysicsObject@1953c8, PhysicsObject@195470, ~PhysicsObject@195568]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SetAudioObject: R +0x50 w4
  SetFeedbackObject: R +0x54 w4
  GetHitPoints: R +0x58 w4
  Simulate: R +0x4c w4
  SetHitPointLoc: W +0x58 w4
