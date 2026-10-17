# AIVehicle

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x35b90 frees/deletes with size 0xc0 (call to UMemory::FastFree)
deleting destructor 0x35b90 frees/deletes with size 0x80 (call to UMemory::FastFree)
Xbox vtable 0x0018bb10 (5 slots) stored by its constructor
PS2 sheet virtual table row: ['AIVehicle virtual table']
constructor 0x357b0 first calls: ['UMemory::FastAlloc', 'WRoadNav::WRoadNav']

Xbox methods (10):
  0x357b0 undefined AIVehicle(void)
  0x35840 undefined GetPhysicsObject(void)
  0x35850 undefined SetPhysicsObject(undefined4 param_1)
  0x35860 undefined SetSplinePath(undefined4 param_1)
  0x35930 undefined SetNextSplinePath(undefined4 param_1)
  0x359d0 undefined4 __fastcall GetSplinePath(int param_1)
  0x359e0 undefined GetPosition(void)
  0x35a20 undefined GetRotPos(void)
  0x35b40 undefined ~AIVehicle(void)
  0x35b90 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (21):
  0x13de68 AIVehicle::AIVehicle
  0x13df50 AIVehicle::~AIVehicle
  0x13dfc8 AIVehicle::GetPhysicsObject
  0x13dfd0 AIVehicle::SetPhysicsObject
  0x13dfd8 AIVehicle::SetSplinePath
  0x13e058 AIVehicle::SetNextSplinePath
  0x13e0b0 AIVehicle::GetSplinePath
  0x13e0b8 AIVehicle::GetPosition
  0x13e120 AIVehicle::GetRotPos
  0x13e398 AIVehicle::operator_new
  0x13e3b8 AIVehicle::operator_delete
  0x13e3d8 AIVehicle::GetVehicleType
  0x13e3e0 AIVehicle::SetVehicleType
  0x13e3e8 AIVehicle::GetDriveToNav
  0x13e3f0 AIVehicle::SetActive
  0x13e3f8 AIVehicle::IsActive
  0x13e400 AIVehicle::SetValidCommand
  0x13e408 AIVehicle::ValidCommand
  0x13e410 AIVehicle::GetControllerIdx
  0x13e418 AIVehicle::SetControllerIdx
  0x13e420 AIVehicle::AIVehicle_global_ctors

Sheet rows:
  AIVehicle::AIVehicle(void)
  AIVehicle::~AIVehicle(void)
  AIVehicle::GetPhysicsObject(void)
  AIVehicle::SetPhysicsObject(PhysicsObject *)
  AIVehicle::SetSplinePath(CARP::AISpline *)
  AIVehicle::SetNextSplinePath(CARP::AISpline *)
  AIVehicle::GetSplinePath(void)
  AIVehicle::GetPosition(void)
  AIVehicle::GetRotPos(void)
  AIVehicle type_info function
  AIVehicle::operator new(unsigned int)
  AIVehicle::operator delete(void *, unsigned int)
  AIVehicle::GetVehicleType(void)
  AIVehicle::SetVehicleType(AIVehicle::EVehicleType)
  AIVehicle::GetDriveToNav(void)
  AIVehicle::SetActive(bool)
  AIVehicle::IsActive(void)
  AIVehicle::SetValidCommand(bool)
  AIVehicle::ValidCommand(void)
  AIVehicle::GetControllerIdx(void)
  AIVehicle::SetControllerIdx(int)
  AIVehicle virtual table
  AIVehicle type_info node

Xbox methods treated as members (10 of 10; untyped ones count when ECX is read before it is written): AIVehicle, GetPhysicsObject, GetPosition, GetRotPos, GetSplinePath, SetNextSplinePath, SetPhysicsObject, SetSplinePath, scalar_deleting_destructor, ~AIVehicle

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [3: AIVehicle@357b0, scalar_deleting_destructor@35b90, ~AIVehicle@35b40]
  +0x010  w[4] W [3: AIVehicle@357b0, scalar_deleting_destructor@35b90, ~AIVehicle@35b40]
  +0x014  w[4] R/W -> PhysicsObject::GetPosition [5: AIVehicle@357b0, GetPhysicsObject@35840, GetPosition@359e0, GetRotPos@35a20, SetPhysicsObject@35850]
  +0x020  w- LEA addr-taken [2: AIVehicle@357b0, GetRotPos@35a20]
  +0x050  w[4] LEA/W addr-taken [1: GetPosition@359e0]
  +0x054  w[4] W [1: GetPosition@359e0]
  +0x058  w[4] W [1: GetPosition@359e0]
  +0x060  w[4] R/W -> AISplinePath::~AISplinePath, __builtin_new [6: AIVehicle@357b0, GetSplinePath@359d0, SetNextSplinePath@35930, SetSplinePath@35860, scalar_deleting_destructor@35b90, ~AIVehicle@35b40]
  +0x064  w[4] R/W -> WRoadNav::~WRoadNav [3: AIVehicle@357b0, scalar_deleting_destructor@35b90, ~AIVehicle@35b40]
  +0x068  w[1] W [3: AIVehicle@357b0, scalar_deleting_destructor@35b90, ~AIVehicle@35b40]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [4: AIVehicle@13de68, GetVehicleType@13e3d8, SetVehicleType@13e3e0, ~AIVehicle@13df50]
  +0x004  w[4] R/W -> PhysicsObject::GetPosition [5: AIVehicle@13de68, GetPhysicsObject@13dfc8, GetPosition@13e0b8, GetRotPos@13e120, SetPhysicsObject@13dfd0]
  +0x010  w[8] LEA/W addr-taken [2: AIVehicle@13de68, GetRotPos@13e120]
  +0x018  w[8] W [1: AIVehicle@13de68]
  +0x020  w[8] W [1: AIVehicle@13de68]
  +0x028  w[8] W [1: AIVehicle@13de68]
  +0x030  w[8] W [1: AIVehicle@13de68]
  +0x038  w[8] W [1: AIVehicle@13de68]
  +0x040  w[4, 8] LEA/W float addr-taken [2: AIVehicle@13de68, GetPosition@13e0b8]
  +0x044  w[4] W float [1: GetPosition@13e0b8]
  +0x048  w[4, 8] W float [2: AIVehicle@13de68, GetPosition@13e0b8]
  +0x050  w[4] R/W -> AISplinePath::~AISplinePath, __builtin_new [5: AIVehicle@13de68, GetSplinePath@13e0b0, SetNextSplinePath@13e058, SetSplinePath@13dfd8, ~AIVehicle@13df50]
  +0x054  w[4] R/W -> WRoadNav::~WRoadNav [3: AIVehicle@13de68, GetDriveToNav@13e3e8, ~AIVehicle@13df50]
  +0x058  w[4] R/W [4: AIVehicle@13de68, IsActive@13e3f8, SetActive@13e3f0, ~AIVehicle@13df50]
  +0x05c  w[4] R/W [2: SetValidCommand@13e400, ValidCommand@13e408]
  +0x060  w[4] R/W [2: GetControllerIdx@13e410, SetControllerIdx@13e418]
  +0x06c  w[4] W [2: AIVehicle@13de68, ~AIVehicle@13df50]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetPhysicsObject: R +0x14 w4
  SetPhysicsObject: W +0x14 w4
  GetSplinePath: R +0x60 w4
