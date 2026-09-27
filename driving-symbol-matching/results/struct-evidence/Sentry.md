# Sentry

FastAlloc/constructed sizes under its tag: {'allocated': [200], 'constructed': []}
deleting destructor 0x735d0 frees/deletes with size 0xc8 (call to UMemory::FastFree)
Xbox vtable 0x00190034 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Sentry virtual table']
constructor 0x73a90 first calls: ['PhysicsObject::PhysicsObject', 'FUN_00133040', '__ftol2']

Xbox methods (14):
  0x73150 undefined ~Sentry(void)
  0x731e0 undefined ComputeTargetAngle(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x73330 undefined GetTarget(void)
  0x733f0 undefined Shoot(void)
  0x734a0 undefined AddMuzzleFlash(undefined4 param_1, undefined4 param_2)
  0x73500 void default DrawMuzzleFlashes(void)
  0x735d0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x73600 undefined ComputeNewRotation(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x73730 undefined RotateSRB(undefined4 param_1)
  0x738b0 undefined RotateAnim(undefined4 param_1)
  0x73a90 undefined Sentry(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 pa
  0x73f80 undefined ApplyDamage(undefined param_1, undefined param_2, undefined4 param_3, undefined param_4, undefined4 
  0x74280 undefined TrackTarget(undefined4 param_1)
  0x744d0 undefined Simulate(void)

PS2 methods (17):
  0x19aeb8 Sentry::Sentry
  0x19b520 Sentry::~Sentry
  0x19b5a8 Sentry::ApplyDamage
  0x19b9d0 Sentry::Simulate
  0x19bb50 Sentry::ComputeTargetAngle
  0x19bd58 Sentry::ComputeNewRotation
  0x19bea0 Sentry::GetTarget
  0x19bf30 Sentry::AcquireTarget
  0x19bf70 Sentry::TrackTarget
  0x19c398 Sentry::RotateSRB
  0x19c550 Sentry::RotateAnim
  0x19c7a8 Sentry::Shoot
  0x19c890 Sentry::AddMuzzleFlash
  0x19c908 Sentry::DrawMuzzleFlashes
  0x19cd30 Sentry::operator_new
  0x19cd50 Sentry::operator_delete
  0x19cd70 Sentry::fFlashCount_global_ctors

Sheet rows:
  Sentry::Sentry(CARP::Instance *, Sentry::Axis, float, float, Se
  Sentry::~Sentry(void)
  Sentry::ApplyDamage(COORD3 &, COORD3 &, float, float, DamageTyp
  Sentry::Simulate(void)
  Sentry::ComputeTargetAngle(COORD3 &, COORD3 &, COORD3 &, Sentry
  Sentry::ComputeNewRotation(float, float, float, float) const
  Sentry::GetTarget(void) const
  Sentry::AcquireTarget(void)
  Sentry::TrackTarget(bool &)
  Sentry::RotateSRB(float)
  Sentry::RotateAnim(float)
  Sentry::Shoot(void)
  Sentry::AddMuzzleFlash(COORD4 &, COORD4 &)
  Sentry::DrawMuzzleFlashes(void)
  Sentry::Debug(void)
  Sentry type_info function
  Sentry::operator new(unsigned int)
  Sentry::operator delete(void *, unsigned int)
  Sentry ** find<Sentry **, Sentry *>(Sentry **, Sentry **, Sentr
  Sentry ** remove_copy<Sentry **, Sentry **, Sentry *>(Sentry **
  Sentry ** remove<Sentry **, Sentry *>(Sentry **, Sentry **, Sen
  Sentry::fFlashCount
  Sentry virtual table
  Sentry::fFlash
  Sentry type_info node

Xbox methods treated as members (11 of 14; untyped ones count when ECX is read before it is written): ApplyDamage, ComputeNewRotation, GetTarget, RotateAnim, RotateSRB, Sentry, Shoot, Simulate, TrackTarget, scalar_deleting_destructor, ~Sentry

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Sentry@73a90, ~Sentry@73150]
  +0x04a  w[2] R [1: Sentry@73a90]
  +0x04c  w[4] R -> RSceneObj::GetBoundingRadius, RSceneObj::GetInstancePosition [7: ApplyDamage@73f80, RotateAnim@738b0, RotateSRB@73730, Sentry@73a90, Shoot@733f0, Simulate@744d0…]
  +0x050  w[4] R [2: Sentry@73a90, Simulate@744d0]
  +0x06c  w[4] R/W [2: Sentry@73a90, TrackTarget@74280]
  +0x070  w[4] R/W [2: Sentry@73a90, TrackTarget@74280]
  +0x074  w[4] W float [1: Sentry@73a90]
  +0x078  w[4] W float [1: Sentry@73a90]
  +0x07c  w[4] W [1: Sentry@73a90]
  +0x080  w[4] W float [1: Sentry@73a90]
  +0x084  w[4] W float [2: Sentry@73a90, TrackTarget@74280]
  +0x088  w[4] LEA/W float addr-taken [3: ApplyDamage@73f80, Sentry@73a90, Simulate@744d0]
  +0x08c  w[4] W float [1: Sentry@73a90]
  +0x090  w[4] R/W float [1: Sentry@73a90]
  +0x094  w[4] R/W float [1: Sentry@73a90]
  +0x098  w[4] W [1: Sentry@73a90]
  +0x09c  w[1] W [1: Sentry@73a90]
  +0x09d  w[1] R/W [2: ApplyDamage@73f80, Sentry@73a90]
  +0x0a0  w[4] R/RW/W [3: Sentry@73a90, Shoot@733f0, Simulate@744d0]
  +0x0a4  w[4] R/W [5: RotateAnim@738b0, RotateSRB@73730, Sentry@73a90, Shoot@733f0, Simulate@744d0]
  +0x0a8  w[4] R/W [2: Sentry@73a90, Shoot@733f0]
  +0x0ac  w[4] R/W [2: Sentry@73a90, TrackTarget@74280]
  +0x0b0  w[4] R/W [2: Sentry@73a90, Simulate@744d0]
  +0x0b4  w[4] R/W [2: Sentry@73a90, Simulate@744d0]
  +0x0b8  w[4] R/W [2: Sentry@73a90, Shoot@733f0]
  +0x0bc  w[4] R/W [3: Sentry@73a90, Shoot@733f0, Simulate@744d0]
  +0x0c0  w[4] R/W [4: GetTarget@73330, Sentry@73a90, Simulate@744d0, TrackTarget@74280]
  +0x0c4  w[4] R/W -> WTargetable::RemoveReference [3: Sentry@73a90, Simulate@744d0, ~Sentry@73150]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R [1: AddMuzzleFlash@19c890]
  +0x008  w[8] R [1: AddMuzzleFlash@19c890]
  +0x046  w[2] R [2: RotateSRB@19c398, Sentry@19aeb8]
  +0x048  w[4] R -> RSceneObj::GetInstancePosition [7: ApplyDamage@19b5a8, RotateAnim@19c550, RotateSRB@19c398, Sentry@19aeb8, Shoot@19c7a8, Simulate@19b9d0…]
  +0x04c  w[4] R [4: RotateAnim@19c550, RotateSRB@19c398, Sentry@19aeb8, Simulate@19b9d0]
  +0x068  w[4] W [2: Sentry@19aeb8, ~Sentry@19b520]
  +0x06c  w[4] R/W [3: RotateSRB@19c398, Sentry@19aeb8, TrackTarget@19bf70]
  +0x070  w[4] R/W [3: RotateAnim@19c550, Sentry@19aeb8, TrackTarget@19bf70]
  +0x074  w[4] R/W float [2: RotateSRB@19c398, Sentry@19aeb8]
  +0x078  w[4] R/W float [2: RotateAnim@19c550, Sentry@19aeb8]
  +0x07c  w[4] R/W float [2: RotateSRB@19c398, Sentry@19aeb8]
  +0x080  w[4] R/W float [2: RotateAnim@19c550, Sentry@19aeb8]
  +0x084  w[4] R/W float [2: Sentry@19aeb8, TrackTarget@19bf70]
  +0x088  w[4] LEA/R/W float addr-taken [3: ApplyDamage@19b5a8, Sentry@19aeb8, Simulate@19b9d0]
  +0x08c  w[4] R/W float [2: ApplyDamage@19b5a8, Sentry@19aeb8]
  +0x090  w[4] R/W float [2: RotateSRB@19c398, Sentry@19aeb8]
  +0x094  w[4] R/W float [2: RotateAnim@19c550, Sentry@19aeb8]
  +0x098  w[4] R/W float [2: RotateAnim@19c550, Sentry@19aeb8]
  +0x09c  w[4] R/W [2: RotateAnim@19c550, Sentry@19aeb8]
  +0x0a0  w[4] R/W [2: ApplyDamage@19b5a8, Sentry@19aeb8]
  +0x0a4  w[4] R/W [3: Sentry@19aeb8, Shoot@19c7a8, Simulate@19b9d0]
  +0x0a8  w[4] R/W [5: RotateAnim@19c550, RotateSRB@19c398, Sentry@19aeb8, Shoot@19c7a8, Simulate@19b9d0]
  +0x0ac  w[4] R/W [2: Sentry@19aeb8, Shoot@19c7a8]
  +0x0b0  w[4] R/W [2: Sentry@19aeb8, TrackTarget@19bf70]
  +0x0b4  w[4] R/W [2: Sentry@19aeb8, Simulate@19b9d0]
  +0x0b8  w[4] R/W [2: Sentry@19aeb8, Simulate@19b9d0]
  +0x0bc  w[4] R/W [2: Sentry@19aeb8, Shoot@19c7a8]
  +0x0c0  w[4] R/W [3: Sentry@19aeb8, Shoot@19c7a8, Simulate@19b9d0]
  +0x0c4  w[4] R/W [5: AcquireTarget@19bf30, GetTarget@19bea0, Sentry@19aeb8, Simulate@19b9d0, TrackTarget@19bf70]
  +0x0c8  w[4] R/W -> PhysicsObject::~PhysicsObject [4: ApplyDamage@19b5a8, Sentry@19aeb8, Simulate@19b9d0, ~Sentry@19b520]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
