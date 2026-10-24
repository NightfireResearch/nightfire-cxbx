# SimpleRigidBody

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xb1e10 first calls: ['VU0_m4toquat']

Xbox methods (14):
  0xb1e10 SimpleRigidBody * __thiscall SimpleRigidBody(SimpleRigidBody * param_1_00, undefined1 param_2, undefined1 para
  0xb1e90 undefined ~SimpleRigidBody(void)
  0xb1ea0 undefined GetOwner(void)
  0xb1eb0 undefined RecalcOrientMat(undefined4 param_1)
  0xb1ec0 undefined GetForwardVector(undefined4 param_1)
  0xb1ee0 undefined GetRightVector(undefined4 param_1)
  0xb1f00 undefined SetOrientMat(undefined4 param_1)
  0xb1f10 undefined SetOrientation(undefined4 param_1)
  0xb1f30 float __thiscall GetScalarVelocity(SimpleRigidBody * this)
  0xb1f40 undefined Accelerate(undefined4 param_1)
  0xb1f80 undefined UpdatePosition(void)
  0xb1fe0 undefined NeedsCollisionCheck(void)
  0xb1ff0 undefined CheckCollisions(undefined4 param_1)
  0xb25c0 undefined SetCanHitTrigger(undefined1 param_1)

PS2 methods (18):
  0x1fd448 SimpleRigidBody::SimpleRigidBody
  0x1fd4d8 SimpleRigidBody::~SimpleRigidBody
  0x1fd528 SimpleRigidBody::GetOwner
  0x1fd548 SimpleRigidBody::RecalcOrientMat
  0x1fd598 SimpleRigidBody::GetForwardVector
  0x1fd5b8 SimpleRigidBody::GetRightVector
  0x1fd5d8 SimpleRigidBody::GetUpVector
  0x1fd5f8 SimpleRigidBody::SetOrientMat
  0x1fd620 SimpleRigidBody::SetOrientation
  0x1fd648 SimpleRigidBody::GetScalarVelocity
  0x1fd680 SimpleRigidBody::GetLinearVelocityCH
  0x1fd6d8 SimpleRigidBody::Accelerate
  0x1fd730 SimpleRigidBody::ApplyFriction
  0x1fd798 SimpleRigidBody::UpdatePosition
  0x1fd820 SimpleRigidBody::NeedsCollisionCheck
  0x1fd830 SimpleRigidBody::CheckCollisions
  0x1fde20 SimpleRigidBody::SetCanHitTrigger
  0x1fdf40 SimpleRigidBody::SimpleRigidBody_global_ctors

Sheet rows:
  SimpleRigidBody::SimpleRigidBody(int, int, COORD3 &, COORD3 &,
  SimpleRigidBody::~SimpleRigidBody(void)
  SimpleRigidBody::GetOwner(void) const
  SimpleRigidBody::RecalcOrientMat(MATRIX4 &) const
  SimpleRigidBody::GetOrientAxis(MATRIX4 *, int, COORD3 &)
  SimpleRigidBody::GetForwardVector(COORD3 &)
  SimpleRigidBody::GetRightVector(COORD3 &)
  SimpleRigidBody::GetUpVector(COORD3 &)
  SimpleRigidBody::SetOrientMat(MATRIX4 *)
  SimpleRigidBody::SetOrientation(COORD4 &)
  SimpleRigidBody::GetScalarVelocity(void)
  SimpleRigidBody::GetLinearVelocityCH(int)
  SimpleRigidBody::Accelerate(COORD3 *)
  SimpleRigidBody::ApplyFriction(void)
  SimpleRigidBody::UpdatePosition(void)
  SimpleRigidBody::NeedsCollisionCheck(void) const
  SimpleRigidBody::CheckCollisions(SimCollisionMap &)
  SimpleRigidBody::SetCanHitTrigger(bool)

Xbox methods treated as members (14 of 14; untyped ones count when ECX is read before it is written): Accelerate, CheckCollisions, GetForwardVector, GetOwner, GetRightVector, GetScalarVelocity, NeedsCollisionCheck, RecalcOrientMat, SetCanHitTrigger, SetOrientMat, SetOrientation, SimpleRigidBody, UpdatePosition, ~SimpleRigidBody

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: SetOrientation@b1f10]
  +0x004  w[4] W [1: SetOrientation@b1f10]
  +0x008  w[4] W [1: SetOrientation@b1f10]
  +0x00c  w[4] W [1: SetOrientation@b1f10]
  +0x010  w[4] LEA/W float addr-taken [3: CheckCollisions@b1ff0, SimpleRigidBody@b1e10, UpdatePosition@b1f80]
  +0x014  w[4] W float [1: UpdatePosition@b1f80]
  +0x018  w[4] W float [1: UpdatePosition@b1f80]
  +0x01c  w[1] W [1: SimpleRigidBody@b1e10]
  +0x01d  w[1] R/W [3: CheckCollisions@b1ff0, GetOwner@b1ea0, SimpleRigidBody@b1e10]
  +0x01e  w[1, 2] R/W [5: Accelerate@b1f40, CheckCollisions@b1ff0, NeedsCollisionCheck@b1fe0, SimpleRigidBody@b1e10, UpdatePosition@b1f80]
  +0x01f  w[1] RW [1: SetCanHitTrigger@b25c0]
  +0x020  w[4] LEA/W float addr-taken -> VU0_v3length [4: Accelerate@b1f40, CheckCollisions@b1ff0, SimpleRigidBody@b1e10, UpdatePosition@b1f80]
  +0x024  w[4] W float [2: Accelerate@b1f40, UpdatePosition@b1f80]
  +0x028  w[4] W float [2: Accelerate@b1f40, UpdatePosition@b1f80]
  +0x02c  w[4] R/W float [2: CheckCollisions@b1ff0, SimpleRigidBody@b1e10]
  +0x030  w- LEA addr-taken [2: SimpleRigidBody@b1e10, UpdatePosition@b1f80]
  +0x03c  w[4] W [1: SimpleRigidBody@b1e10]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] W [1: SetOrientation@1fd620]
  +0x008  w[8] W [1: SetOrientation@1fd620]
  +0x010  w[4, 8] LEA/R/W float addr-taken [3: CheckCollisions@1fd830, SimpleRigidBody@1fd448, UpdatePosition@1fd798]
  +0x014  w[4] R/W float [1: UpdatePosition@1fd798]
  +0x018  w[4] R/W float [2: SimpleRigidBody@1fd448, UpdatePosition@1fd798]
  +0x01c  w[1, 4] R/W [2: CheckCollisions@1fd830, SimpleRigidBody@1fd448]
  +0x01d  w[1] R/W [3: CheckCollisions@1fd830, GetOwner@1fd528, SimpleRigidBody@1fd448]
  +0x01e  w[2] R/W [7: Accelerate@1fd6d8, ApplyFriction@1fd730, CheckCollisions@1fd830, NeedsCollisionCheck@1fd820, SetCanHitTrigger@1fde20, SimpleRigidBody@1fd448…]
  +0x020  w[4, 8] R/W float [3: Accelerate@1fd6d8, SimpleRigidBody@1fd448, UpdatePosition@1fd798]
  +0x024  w[4] R/W float [2: Accelerate@1fd6d8, UpdatePosition@1fd798]
  +0x028  w[4] R/W float [3: Accelerate@1fd6d8, SimpleRigidBody@1fd448, UpdatePosition@1fd798]
  +0x02c  w[4] R/W float [2: CheckCollisions@1fd830, SimpleRigidBody@1fd448]
  +0x030  w[8] W [1: SimpleRigidBody@1fd448]
  +0x038  w[4] W [1: SimpleRigidBody@1fd448]
  +0x03c  w[4] W float [1: SimpleRigidBody@1fd448]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetOwner: R +0x1d w1
  NeedsCollisionCheck: R +0x1e w1
  SetCanHitTrigger: RW +0x1f w1
