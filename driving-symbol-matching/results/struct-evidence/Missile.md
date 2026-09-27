# Missile

FastAlloc/constructed sizes under its tag: {'allocated': [1840], 'constructed': []}
deleting destructor 0x5f6a0 frees/deletes with size 0x730 (call to UMemory::FastFree)
Xbox vtable 0x0018a398 (19 slots) stored by its constructor
Xbox vtable 0x0018eef8 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Missile virtual table']
constructor 0x5e8c0 first calls: ['PhysicsObject::PhysicsObject', 'RMissileStreak::RMissileStreak', 'Util_GenerateMatrix']

Xbox methods (10):
  0x5e8c0 undefined Missile(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 p
  0x5f350 undefined ~Missile(void)
  0x5f3d0 undefined Render(undefined1 param_1)
  0x5f600 undefined SetUserSteerX(undefined4 param_1)
  0x5f640 undefined SetUserSteerY(undefined4 param_1)
  0x5f680 undefined SetTarget(undefined4 param_1)
  0x5f6a0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x5f6d0 undefined CheckForCollision(void)
  0x60110 undefined SteerMissile(void)
  0x60a30 undefined Simulate(void)

PS2 methods (15):
  0x180bc8 Missile::Missile
  0x1818c8 Missile::~Missile
  0x181960 Missile::Render
  0x181c80 Missile::Simulate
  0x181df0 Missile::SetUserSteerX
  0x181e30 Missile::SetUserSteerY
  0x181e70 Missile::SteerMissile
  0x182978 Missile::CheckForCollision
  0x183888 Missile::SetTarget
  0x183b20 Missile::GetWeaponType
  0x183b28 Missile::Detonate
  0x183b38 Missile::GetTarget
  0x183b40 Missile::GetIsDecaying
  0x183b48 Missile::GetAutoDriveLaunchPos
  0x183c50 Missile::fUserMissileLaunchPos_global_ctors

Sheet rows:
  Missile::Missile(WeaponType, float, COORD3 &, COORD3 &, SimObjS
  Missile::~Missile(void)
  Missile::Render(bool)
  Missile::Simulate(void)
  Missile::SetUserSteerX(float)
  Missile::SetUserSteerY(float)
  Missile::SteerMissile(void)
  Missile::CheckForCollision(void)
  Missile::SetTarget(WTargetable *)
  Missile::Debug(void)
  Missile type_info function
  Missile::operator new(unsigned int)
  Missile::operator delete(void *, unsigned int)
  Missile::GetWeaponType(void)
  Missile::Detonate(void)
  Missile::GetTarget(void)
  Missile::GetIsDecaying(void)
  Missile::GetAutoDriveLaunchPos(void)
  Missile ** find<Missile **, Missile *>(Missile **, Missile **,
  Missile ** remove_copy<Missile **, Missile **, Missile *>(Missi
  Missile ** remove<Missile **, Missile *>(Missile **, Missile **
  Missile::fUserMissileLaunchPos
  Missile::fUserMissileSteeringFac
  Missile::fUserMissileSteeringSpeed
  Missile::fUserMissileMaxSpeed
  Missile::fUserMissileAccel
  Missile::fUserMissileInitSpeedEx
  Missile::fUserMissileHomingEx
  Missile::fUserMissileTimer
  Missile virtual table
  Missile type_info node

Xbox methods treated as members (10 of 10; untyped ones count when ECX is read before it is written): CheckForCollision, Missile, Render, SetTarget, SetUserSteerX, SetUserSteerY, Simulate, SteerMissile, scalar_deleting_destructor, ~Missile

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Missile@5e8c0, ~Missile@5f350]
  +0x004  w- LEA addr-taken [1: CheckForCollision@5f6d0]
  +0x034  w[1] R [1: CheckForCollision@5f6d0]
  +0x04a  w[2] R [5: CheckForCollision@5f6d0, Missile@5e8c0, Render@5f3d0, Simulate@60a30, SteerMissile@60110]
  +0x04c  w[4] R [1: Render@5f3d0]
  +0x06c  w[4] R/W -> WTargetable::RemoveReference [3: Missile@5e8c0, SetTarget@5f680, ~Missile@5f350]
  +0x070  w[4] W [1: Missile@5e8c0]
  +0x074  w[4] W float [1: Missile@5e8c0]
  +0x078  w[4] W float [1: Missile@5e8c0]
  +0x07c  w[4] W [1: Missile@5e8c0]
  +0x080  w[4] W float [3: Missile@5e8c0, SetUserSteerX@5f600, SetUserSteerY@5f640]
  +0x084  w[4] R/W float [2: Missile@5e8c0, Render@5f3d0]
  +0x088  w[4] R/RW/W float [2: CheckForCollision@5f6d0, Missile@5e8c0]
  +0x08c  w[4] W float [2: Missile@5e8c0, Render@5f3d0]
  +0x090  w[4] R/W [2: CheckForCollision@5f6d0, Missile@5e8c0]
  +0x094  w[4] R/W [2: Missile@5e8c0, Render@5f3d0]
  +0x0a0  w- LEA addr-taken -> RMissileStreak::RMissileStreak, dummyNullFunction [2: Missile@5e8c0, ~Missile@5f350]
  +0x0a8  w[4] W [1: Missile@5e8c0]
  +0x6f0  w[4] R/W [3: Missile@5e8c0, Simulate@60a30, ~Missile@5f350]
  +0x6f4  w[4] R/W [3: CheckForCollision@5f6d0, Missile@5e8c0, Render@5f3d0]
  +0x6f8  w[4] W [1: Missile@5e8c0]
  +0x6fc  w[4] W [1: Missile@5e8c0]
  +0x700  w[4] W float [2: Missile@5e8c0, SetUserSteerX@5f600]
  +0x704  w[4] W float [2: Missile@5e8c0, SetUserSteerY@5f640]
  +0x708  w[1] W [1: Missile@5e8c0]
  +0x709  w[1] R/W [2: CheckForCollision@5f6d0, Missile@5e8c0]
  +0x70a  w[1] R/W [4: CheckForCollision@5f6d0, Missile@5e8c0, Render@5f3d0, Simulate@60a30]
  +0x70c  w[4] R/W float [2: Missile@5e8c0, Simulate@60a30]
  +0x710  w[4] R/W float [2: CheckForCollision@5f6d0, Missile@5e8c0]
  +0x714  w[4] W [1: Missile@5e8c0]
  +0x718  w[4] W float [1: Missile@5e8c0]
  +0x71c  w[4] W [1: Missile@5e8c0]
  +0x720  w[4] W [1: Missile@5e8c0]

PS2 this-relative accesses (PS2 offsets):
  +0x030  w[4] R [1: CheckForCollision@182978]
  +0x046  w[2] R [5: CheckForCollision@182978, Missile@180bc8, Render@181960, Simulate@181c80, SteerMissile@181e70]
  +0x048  w[4] R -> RSceneObj::StopFX [2: Render@181960, Simulate@181c80]
  +0x068  w[4] W [2: Missile@180bc8, ~Missile@1818c8]
  +0x06c  w[4] R/W -> WTargetable::RemoveReference [5: GetTarget@183b38, Missile@180bc8, SetTarget@183888, SteerMissile@181e70, ~Missile@1818c8]
  +0x070  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]
  +0x074  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]
  +0x078  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]
  +0x07c  w[4] W float [1: Missile@180bc8]
  +0x080  w[4] R/W float [3: Missile@180bc8, SetUserSteerX@181df0, SetUserSteerY@181e30]
  +0x084  w[4] R/W float [3: Missile@180bc8, Render@181960, SteerMissile@181e70]
  +0x088  w[4] R/W float [2: CheckForCollision@182978, Missile@180bc8]
  +0x08c  w[4] R/W float [3: Missile@180bc8, Render@181960, Simulate@181c80]
  +0x090  w[4] R/W [3: CheckForCollision@182978, Missile@180bc8, SteerMissile@181e70]
  +0x094  w[4] R/W [3: Missile@180bc8, Render@181960, SteerMissile@181e70]
  +0x0a0  w- LEA addr-taken [3: Missile@180bc8, Render@181960, ~Missile@1818c8]
  +0x0a8  w[4] W [1: Missile@180bc8]
  +0x6f0  w[4] R/W [3: Missile@180bc8, Simulate@181c80, ~Missile@1818c8]
  +0x6f4  w[4] R/W [5: CheckForCollision@182978, GetWeaponType@183b20, Missile@180bc8, Render@181960, SteerMissile@181e70]
  +0x6f8  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]
  +0x6fc  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]
  +0x700  w[4] R/W float [3: Missile@180bc8, SetUserSteerX@181df0, SteerMissile@181e70]
  +0x704  w[4] R/W float [3: Missile@180bc8, SetUserSteerY@181e30, SteerMissile@181e70]
  +0x708  w[4] R/W [2: Missile@180bc8, SteerMissile@181e70]
  +0x70c  w[4] R/W [4: CheckForCollision@182978, Detonate@183b28, Missile@180bc8, SteerMissile@181e70]
  +0x710  w[4] R/W [5: CheckForCollision@182978, GetIsDecaying@183b40, Missile@180bc8, Render@181960, Simulate@181c80]
  +0x714  w[4] R/W float [2: Missile@180bc8, Simulate@181c80]
  +0x718  w[4] R/W float [2: CheckForCollision@182978, Missile@180bc8]
  +0x71c  w[4] R/W [2: Missile@180bc8, SteerMissile@181e70]
  +0x720  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]
  +0x724  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]
  +0x728  w[4] R/W float [2: Missile@180bc8, SteerMissile@181e70]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
