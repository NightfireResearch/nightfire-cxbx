# RigidBody

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xb0b20 first calls: ['VU0_m4toquat', 'Simulation::GetRigidBodyInfo', 'UMemory::FastAlloc']

Xbox methods (38):
  0xacde0 undefined __stdcall InitRigidBodySystem(void)
  0xad0e0 undefined ResetRigidBodySP(void)
  0xad100 undefined GetOwner(void)
  0xad110 undefined GetOrientToGround(void)
  0xad130 undefined ApplyAngularDamping(void)
  0xad170 undefined ApplyHeavyFriction(void)
  0xad1a0 undefined ModifyAngularMomentum(undefined4 param_1)
  0xad1d0 void __thiscall GetLocalAngularMomentum(RigidBody * this, float * param_1)
  0xad220 void __thiscall GetLocalVelocity(RigidBody * this, float * param_1)
  0xad270 void __thiscall GetLocalAngularVelocity(RigidBody * this, float * param_1)
  0xad2c0 undefined ConvertWorldToLocal(undefined4 param_1)
  0xad2f0 undefined ConvertLocalToWorld(undefined4 param_1)
  0xad310 undefined SetAngularMomentum(undefined4 param_1)
  0xad330 undefined SetOrientation(undefined4 param_1)
  0xad3f0 undefined ResolveForce(undefined4 param_1)
  0xad420 undefined ResolveTorque(undefined4 param_1)
  0xad470 undefined ResolveMassScaledTorque4(undefined4 param_1)
  0xad4a0 undefined UpdateZonalInfo(void)
  0xad520 undefined CalculateAndApplyWorldDamage(undefined param_1, undefined4 param_2)
  0xad600 undefined ForceToSleep(void)
  0xad690 undefined TempGetHeightInformation(undefined param_1, undefined4 param_2, undefined4 param_3, undefined4 param
  0xad840 undefined InitLevers(undefined4 param_1, undefined4 param_2)
  0xadc80 undefined ~RigidBody(void)
  0xadcd0 undefined ResetObject(undefined4 param_1, undefined4 param_2)
  0xade40 undefined ApplyInitialForcesAndTorques(void)
  0xae150 undefined ResolveLeverForces(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xae4c0 undefined CollideWithGround(void)
  0xae830 undefined ModifyLevers(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefin
  0xaea40 undefined ScaleObjObjForces(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, un
  0xaec10 undefined ResolveCollision(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, und
  0xaf960 undefined ResolveWorldOBBCollision(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 para
  0xafdb0 undefined GenerateImpulse(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, unde
  0xb0510 undefined CollideWithObject(undefined4 param_1)
  0xb09d0 undefined ControlSleep(void)
  0xb0b20 undefined RigidBody(undefined1 param_1, undefined1 param_2, undefined4 param_3, undefined4 param_4, undefined4
  0xb0ec0 undefined UpdatePositionAndOrientation(void)
  0xb1420 undefined CollideWithWorld(void)
  0xb2810 undefined ResetRigidBodySP(void)

PS2 methods (54):
  0x1f6600 RigidBody::InitRigidBodySystem
  0x1f6a60 RigidBody::ShutdownRigidBodySystem
  0x1f6a68 RigidBody::DebugSystem
  0x1f6ac8 RigidBody::ResetRigidBodySP
  0x1f6ae0 RigidBody::TempGetHeightInformation
  0x1f6e98 RigidBody::RigidBody
  0x1f72d8 RigidBody::InitLevers
  0x1f77e0 RigidBody::~RigidBody
  0x1f7860 RigidBody::GetOwner
  0x1f7880 RigidBody::ResetObject
  0x1f7a80 RigidBody::GetBodyTensorMatrix4
  0x1f7ac0 RigidBody::SetOrientMat
  0x1f7ae8 RigidBody::StoreBodyMatrix
  0x1f7b70 RigidBody::StoreInvWorldTensor
  0x1f7c00 RigidBody::GetOrientToGround
  0x1f7c60 RigidBody::ApplyInitialForcesAndTorques
  0x1f8088 RigidBody::ApplyAngularDamping
  0x1f80d8 RigidBody::ApplyHeavyFriction
  0x1f8168 RigidBody::ModifyAngularMomentum
  0x1f81b8 RigidBody::GetLocalMomentum
  0x1f8250 RigidBody::GetLocalAngularMomentum
  0x1f82e8 RigidBody::GetLocalVelocity
  0x1f8380 RigidBody::GetLocalAngularVelocity
  0x1f8418 RigidBody::ConvertToLocalAngularMomentum
  0x1f8498 RigidBody::ConvertFromLocalAngularMomentum
  0x1f84d0 RigidBody::ConvertLocalToWorld
  0x1f8508 RigidBody::ConvertWorldToLocal
  0x1f8588 RigidBody::SetAngularMomentum
  0x1f85c0 RigidBody::SetOrientation
  0x1f86e8 RigidBody::ResolveForce
  0x1f8778 RigidBody::ResolveTorque
  0x1f87a8 RigidBody::ResolveTorque4
  0x1f87d8 RigidBody::ResolveMassScaledForce
  0x1f8830 RigidBody::ResolveMassScaledForce4
  0x1f8888 RigidBody::ResolveMassScaledTorque
  0x1f88c8 RigidBody::ResolveMassScaledTorque4
  0x1f8908 RigidBody::UpdateZonalInfo
  0x1f8cf0 RigidBody::Timestep_CollectVU0data
  0x1f8fc8 RigidBody::Debug
  0x1f8fd0 RigidBody::UpdatePositionAndOrientation
  0x1f8ff0 RigidBody::Resolve_CollectVU0data
  0x1f91e8 RigidBody::ResolveLeverForces
  0x1f9500 RigidBody::CollideWithGround
  0x1f98e8 RigidBody::ModifyLevers
  0x1f9b40 RigidBody::ScaleObjObjForces
  0x1f9d98 RigidBody::ResolveCollision
  0x1fade8 RigidBody::ResolveWorldOBBCollision
  0x1fb318 RigidBody::GenerateImpulse
  0x1fbbd8 RigidBody::CollideWithObject
  0x1fc198 RigidBody::CalculateAndApplyWorldDamage
  0x1fc2f0 RigidBody::CollideWithWorld
  0x1fcfc0 RigidBody::ControlSleep
  0x1fd170 RigidBody::ForceToSleep
  0x1fd408 RigidBody::RecalcOrientMat4

Sheet rows:
  RigidBody::InitRigidBodySystem(void)
  RigidBody::ShutdownRigidBodySystem(void)
  RigidBody::DebugSystem(void)
  RigidBody::ResetRigidBodySP(void)
  RigidBody::TempGetHeightInformation(WCollider *, COORD3 &, COOR
  RigidBody::RigidBody(int, int, COORD3 &, COORD3 &, COORD3 &, MA
  RigidBody::InitLevers(PhysicsObject *, COORD4 &)
  RigidBody::~RigidBody(void)
  RigidBody::GetOwner(void) const
  RigidBody::ResetObject(MATRIX4 *, COORD3 *)
  RigidBody::GetBodyTensorMatrix4(MATRIX4 &)
  RigidBody::SetOrientMat(MATRIX4 *)
  RigidBody::StoreBodyMatrix(MATRIX4 *)
  RigidBody::StoreInvWorldTensor(MATRIX4 *)
  RigidBody::GetOrientToGround(void)
  RigidBody::ApplyInitialForcesAndTorques(void)
  RigidBody::ApplyAngularDamping(void)
  RigidBody::ApplyHeavyFriction(void)
  RigidBody::AddMomentum(COORD3 &)
  RigidBody::ModifyAngularMomentum(COORD3 &)
  RigidBody::GetLocalMomentum(void)
  RigidBody::GetLocalAngularMomentum(void)
  RigidBody::GetLocalVelocity(void)
  RigidBody::GetLocalAngularVelocity(void)
  RigidBody::ConvertToLocalAngularMomentum(COORD3 &)
  RigidBody::ConvertFromLocalAngularMomentum(COORD3 &)
  RigidBody::ConvertLocalToWorld(COORD3 &)
  RigidBody::ConvertWorldToLocal(COORD3 &)
  RigidBody::SetAngularMomentum(COORD3 &)
  RigidBody::SetOrientation(COORD4 &)
  RigidBody::ResolveForce(COORD3 *)
  RigidBody::ResolveForce4(COORD4 *)
  RigidBody::ResolveTorque(COORD3 *)
  RigidBody::ResolveTorque4(COORD4 *)
  RigidBody::ResolveMassScaledForce(COORD3 *)
  RigidBody::ResolveMassScaledForce4(COORD4 *)
  RigidBody::ResolveMassScaledTorque(COORD3 *)
  RigidBody::ResolveMassScaledTorque4(COORD4 *)
  RigidBody::UpdateZonalInfo(void)
  RigidBody::Timestep_CollectVU0data(void)
  RigidBody::Debug(void)
  RigidBody::UpdatePositionAndOrientation(float)
  RigidBody::Resolve_CollectVU0data(void)
  RigidBody::ResolveLeverForces(COORD4 *, COORD4 *, COORD4 *, MAT
  RigidBody::CollideWithGround(void)
  RigidBody::ModifyLevers(RigidBody &, RigidBody &, COORD3 &, COO
  RigidBody::ScaleObjObjForces(RigidBody &, RigidBody &, COORD3 &
  RigidBody::ResolveCollision(RigidBody &, RigidBody &, COORD4 *,
  RigidBody::ResolveWorldOBBCollision(COORD4 *, COORD4 *, float,
  RigidBody::GenerateImpulse(COLLISION_INFO *, COORD4 *, COORD4 *
  RigidBody::CollideWithObject(int, int)
  RigidBody::CalculateAndApplyWorldDamage(COORD3 &, float)
  RigidBody::CollideWithWorld(void)
  RigidBody::ControlSleep(void)
  RigidBody::ForceToSleep(void)
  RigidBody::RecalcOrientMat4(MATRIX4 &)
  RigidBody::fgPlayerJumpGravity

Xbox methods treated as members (34 of 38; untyped ones count when ECX is read before it is written): ApplyAngularDamping, ApplyHeavyFriction, ApplyInitialForcesAndTorques, CalculateAndApplyWorldDamage, CollideWithGround, CollideWithObject, CollideWithWorld, ControlSleep, ConvertLocalToWorld, ConvertWorldToLocal, ForceToSleep, GenerateImpulse, GetLocalAngularMomentum, GetLocalAngularVelocity, GetLocalVelocity, GetOrientToGround, GetOwner, InitLevers, ModifyAngularMomentum, ResetObject, ResetRigidBodySP, ResolveCollision, ResolveForce, ResolveLeverForces, ResolveMassScaledTorque4, ResolveTorque, ResolveWorldOBBCollision, RigidBody, SetAngularMomentum, SetOrientation, TempGetHeightInformation, UpdatePositionAndOrientation, UpdateZonalInfo, ~RigidBody

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W float [2: SetOrientation@ad330, UpdatePositionAndOrientation@b0ec0]
  +0x004  w[4] R/W float [2: SetOrientation@ad330, UpdatePositionAndOrientation@b0ec0]
  +0x008  w[4] R/W float [2: SetOrientation@ad330, UpdatePositionAndOrientation@b0ec0]
  +0x00c  w[4] R/W float [2: SetOrientation@ad330, UpdatePositionAndOrientation@b0ec0]
  +0x010  w[4] LEA/W float addr-taken -> VU0_v4sub [8: CollideWithObject@b0510, CollideWithWorld@b1420, GenerateImpulse@afdb0, ResetObject@adcd0, ResolveWorldOBBCollision@af960, RigidBody@b0b20…]
  +0x01c  w[4] R/W float [4: ResetObject@adcd0, RigidBody@b0b20, SetOrientation@ad330, UpdatePositionAndOrientation@b0ec0]
  +0x020  w[4] LEA/W float addr-taken [10: ApplyInitialForcesAndTorques@ade40, CollideWithWorld@b1420, ControlSleep@b09d0, ForceToSleep@ad600, GenerateImpulse@afdb0, ResetObject@adcd0…]
  +0x024  w[4] W float [4: ForceToSleep@ad600, ResetObject@adcd0, ResolveLeverForces@ae150, RigidBody@b0b20]
  +0x028  w[4] W float [4: ForceToSleep@ad600, ResetObject@adcd0, ResolveLeverForces@ae150, RigidBody@b0b20]
  +0x02c  w[4] R/W float [4: ResetObject@adcd0, RigidBody@b0b20, SetOrientation@ad330, UpdatePositionAndOrientation@b0ec0]
  +0x030  w[4] LEA/W float addr-taken [8: ControlSleep@b09d0, ForceToSleep@ad600, GenerateImpulse@afdb0, ResetObject@adcd0, ResolveLeverForces@ae150, ResolveWorldOBBCollision@af960…]
  +0x034  w[4] R/W float [4: ForceToSleep@ad600, ResetObject@adcd0, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x038  w[4] R/W float [4: ForceToSleep@ad600, ResetObject@adcd0, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x03c  w[4] R/W float [4: ResetObject@adcd0, RigidBody@b0b20, SetOrientation@ad330, UpdatePositionAndOrientation@b0ec0]
  +0x040  w[4] LEA/W float addr-taken [7: ApplyHeavyFriction@ad170, ApplyInitialForcesAndTorques@ade40, ForceToSleep@ad600, ResetObject@adcd0, ResolveLeverForces@ae150, RigidBody@b0b20…]
  +0x044  w[4] R/W float [5: ForceToSleep@ad600, ResetObject@adcd0, ResolveLeverForces@ae150, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x048  w[4] R/W float [5: ForceToSleep@ad600, ResetObject@adcd0, ResolveLeverForces@ae150, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x04c  w[4] R/W float [6: ApplyInitialForcesAndTorques@ade40, InitLevers@ad840, ResolveLeverForces@ae150, ResolveMassScaledTorque4@ad470, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x050  w[4] LEA/W float addr-taken [9: ApplyAngularDamping@ad130, ApplyHeavyFriction@ad170, ForceToSleep@ad600, ModifyAngularMomentum@ad1a0, ResetObject@adcd0, ResolveLeverForces@ae150…]
  +0x054  w[4] W float [5: ApplyAngularDamping@ad130, ForceToSleep@ad600, ResetObject@adcd0, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x058  w[4] W float [5: ApplyAngularDamping@ad130, ForceToSleep@ad600, ResetObject@adcd0, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x05c  w[4] R/W -> VU0_MATRIX4_vect3mult [20: ApplyInitialForcesAndTorques@ade40, CollideWithGround@ae4c0, CollideWithObject@b0510, CollideWithWorld@b1420, ControlSleep@b09d0, ConvertLocalToWorld@ad2f0…]
  +0x060  w[4] LEA/W addr-taken [5: ApplyInitialForcesAndTorques@ade40, ResolveForce@ad3f0, ResolveLeverForces@ae150, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x064  w[4] W [2: ApplyInitialForcesAndTorques@ade40, RigidBody@b0b20]
  +0x068  w[4] W [2: ApplyInitialForcesAndTorques@ade40, RigidBody@b0b20]
  +0x06c  w[1] R/W [5: ApplyAngularDamping@ad130, ApplyInitialForcesAndTorques@ade40, ControlSleep@b09d0, ResetObject@adcd0, RigidBody@b0b20]
  +0x06d  w[1] R/W [14: ApplyAngularDamping@ad130, ApplyInitialForcesAndTorques@ade40, CalculateAndApplyWorldDamage@ad520, CollideWithGround@ae4c0, CollideWithObject@b0510, CollideWithWorld@b1420…]
  +0x06e  w[1] R/W [8: ApplyInitialForcesAndTorques@ade40, CollideWithObject@b0510, ForceToSleep@ad600, ResetObject@adcd0, ResolveForce@ad3f0, ResolveMassScaledTorque4@ad470…]
  +0x06f  w[1] R/W [11: ApplyInitialForcesAndTorques@ade40, CalculateAndApplyWorldDamage@ad520, CollideWithGround@ae4c0, CollideWithObject@b0510, CollideWithWorld@b1420, GenerateImpulse@afdb0…]
  +0x070  w[4] LEA/W addr-taken -> VU0_v4scaleadd [5: ApplyInitialForcesAndTorques@ade40, ResolveMassScaledTorque4@ad470, ResolveTorque@ad420, RigidBody@b0b20, UpdatePositionAndOrientation@b0ec0]
  +0x074  w[4] W [2: ApplyInitialForcesAndTorques@ade40, RigidBody@b0b20]
  +0x078  w[4] W [2: ApplyInitialForcesAndTorques@ade40, RigidBody@b0b20]
  +0x07c  w[1, 4] R/W float [6: ApplyInitialForcesAndTorques@ade40, CollideWithObject@b0510, CollideWithWorld@b1420, ResetObject@adcd0, ResolveForce@ad3f0, RigidBody@b0b20]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: SetOrientation@1f85c0]
  +0x008  w[8] W [1: SetOrientation@1f85c0]
  +0x010  w[4, 8] LEA/R/W float addr-taken [7: CalculateAndApplyWorldDamage@1fc198, CollideWithGround@1f9500, CollideWithObject@1fbbd8, CollideWithWorld@1fc2f0, ResetObject@1f7880, RigidBody@1f6e98…]
  +0x014  w[4] R/W float [2: CollideWithGround@1f9500, UpdateZonalInfo@1f8908]
  +0x018  w[4, 8] R/W float [5: CollideWithGround@1f9500, CollideWithWorld@1fc2f0, ResetObject@1f7880, RigidBody@1f6e98, UpdateZonalInfo@1f8908]
  +0x01c  w[4] R/W float [2: GetBodyTensorMatrix4@1f7a80, RigidBody@1f6e98]
  +0x020  w[4, 8] R/W float [6: ApplyInitialForcesAndTorques@1f7c60, CollideWithGround@1f9500, ControlSleep@1fcfc0, ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x024  w[4] R float [2: CollideWithGround@1f9500, RigidBody@1f6e98]
  +0x028  w[4] R/W float [6: ApplyInitialForcesAndTorques@1f7c60, CollideWithGround@1f9500, ControlSleep@1fcfc0, ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x02c  w[4] R/W float [2: GetBodyTensorMatrix4@1f7a80, RigidBody@1f6e98]
  +0x030  w[4, 8] R/W float [4: ControlSleep@1fcfc0, ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x034  w[4] R float [1: RigidBody@1f6e98]
  +0x038  w[4] R/W float [4: ControlSleep@1fcfc0, ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x03c  w[4] R/W float [2: GetBodyTensorMatrix4@1f7a80, RigidBody@1f6e98]
  +0x040  w[4, 8] LEA/W float addr-taken [4: ApplyHeavyFriction@1f80d8, ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x044  w[4] W float [1: RigidBody@1f6e98]
  +0x048  w[4] W float [3: ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x04c  w[4] R/W float [10: ApplyInitialForcesAndTorques@1f7c60, GenerateImpulse@1fb318, InitLevers@1f72d8, ResolveMassScaledForce4@1f8830, ResolveMassScaledForce@1f87d8, ResolveMassScaledTorque4@1f88c8…]
  +0x050  w[4, 8] R/W float [4: ApplyAngularDamping@1f8088, ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x054  w[4] R/W float [2: ApplyAngularDamping@1f8088, RigidBody@1f6e98]
  +0x058  w[4] R/W float [4: ApplyAngularDamping@1f8088, ForceToSleep@1fd170, ResetObject@1f7880, RigidBody@1f6e98]
  +0x05c  w[4] R/W [27: ApplyInitialForcesAndTorques@1f7c60, CollideWithGround@1f9500, CollideWithObject@1fbbd8, CollideWithWorld@1fc2f0, ControlSleep@1fcfc0, ConvertFromLocalAngularMomentum@1f8498…]
  +0x060  w[8] W [2: ApplyInitialForcesAndTorques@1f7c60, RigidBody@1f6e98]
  +0x068  w[4] W [2: ApplyInitialForcesAndTorques@1f7c60, RigidBody@1f6e98]
  +0x06c  w[1] R/W [6: ApplyAngularDamping@1f8088, ApplyInitialForcesAndTorques@1f7c60, CollideWithGround@1f9500, ControlSleep@1fcfc0, ResetObject@1f7880, RigidBody@1f6e98]
  +0x06d  w[1] R/W [17: ApplyAngularDamping@1f8088, ApplyInitialForcesAndTorques@1f7c60, CalculateAndApplyWorldDamage@1fc198, CollideWithGround@1f9500, CollideWithObject@1fbbd8, CollideWithWorld@1fc2f0…]
  +0x06e  w[1] R/W [11: CollideWithObject@1fbbd8, ForceToSleep@1fd170, ResetObject@1f7880, ResolveForce@1f86e8, ResolveMassScaledForce4@1f8830, ResolveMassScaledForce@1f87d8…]
  +0x06f  w[1] R/W [6: CollideWithObject@1fbbd8, CollideWithWorld@1fc2f0, GenerateImpulse@1fb318, GetOwner@1f7860, ResolveWorldOBBCollision@1fade8, RigidBody@1f6e98]
  +0x070  w[8] W [2: ApplyInitialForcesAndTorques@1f7c60, RigidBody@1f6e98]
  +0x078  w[4] W [2: ApplyInitialForcesAndTorques@1f7c60, RigidBody@1f6e98]
  +0x07c  w[4] R/W float [8: CollideWithGround@1f9500, CollideWithObject@1fbbd8, CollideWithWorld@1fc2f0, ResetObject@1f7880, ResolveForce@1f86e8, ResolveMassScaledForce4@1f8830…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetOwner: R +0x6f w1
  GetOrientToGround: R +0x5c w4
  ConvertLocalToWorld: R +0x5c w4
