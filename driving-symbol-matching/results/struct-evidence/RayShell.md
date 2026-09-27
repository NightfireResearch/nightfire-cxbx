# RayShell

FastAlloc/constructed sizes under its tag: {'allocated': [224], 'constructed': []}
deleting destructor 0x72160 frees/deletes with size 0xe0 (call to UMemory::FastFree)
Xbox vtable 0x0018ffe4 (1 slots) stored by its constructor
PS2 sheet virtual table row: ['RayShell virtual table']
constructor 0x72e90 first calls: ['Simulation::GetPlayerObject', 'Simulation::FindPhysicsObjectSignature', 'PhysicsObject::IsOwnedBy']

Xbox methods (19):
  0x71930 undefined GetNumActiveRayShells(void)
  0x71940 undefined4 * __cdecl GetActiveRayShell(int param_1)
  0x71960 undefined __stdcall ClearActiveRayShells(void)
  0x71970 undefined SetTriggerHittingRayShell(undefined4 param_1)
  0x71980 undefined4 * __stdcall GetTriggerHittingRayShell(void)
  0x719a0 undefined ClearTracers(undefined4 param_1)
  0x71a60 undefined DrawTracers(void)
  0x71c80 undefined DrawFlashes(void)
  0x71d40 undefined __stdcall Reset(void)
  0x71d80 undefined AddNewRayShellForTriggerCheck(undefined4 param_1)
  0x71e40 undefined ConsiderVictim(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x71fb0 undefined StaticStoreTracer(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, un
  0x720d0 undefined AddFlash(undefined4 param_1, undefined4 param_2)
  0x72160 undefined scalar_deleting_destructor(undefined1 param_1)
  0x72190 undefined CollideWithObjects(void)
  0x72790 undefined CollideWithWorld(undefined4 param_1)
  0x72ba0 undefined DamagePhysicsObject(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x72d60 undefined Fire(undefined4 param_1)
  0x72e90 undefined RayShell(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 

PS2 methods (24):
  0x198e80 RayShell::RayShell
  0x199200 RayShell::~RayShell
  0x199230 RayShell::GetNumActiveRayShells
  0x199240 RayShell::GetActiveRayShell
  0x199258 RayShell::ClearActiveRayShells
  0x199268 RayShell::SetTriggerHittingRayShell
  0x199278 RayShell::GetTriggerHittingRayShell
  0x1992d8 RayShell::DeleteTracer
  0x199358 RayShell::ClearTracers
  0x1993e8 RayShell::DrawTracers
  0x199668 RayShell::ClearFlashes
  0x199678 RayShell::DrawFlashes
  0x199780 RayShell::Reset
  0x1997e0 RayShell::AddNewRayShellForTriggerCheck
  0x1998c0 RayShell::CollideWithObjects
  0x199f88 RayShell::CollideWithWorld
  0x19a3e0 RayShell::Fire
  0x19a548 RayShell::ConsiderVictim
  0x19a710 RayShell::DamagePhysicsObject
  0x19a988 RayShell::StoreTracer
  0x19a9b8 RayShell::StaticStoreTracer
  0x19abb8 RayShell::AddFlash
  0x19ae58 RayShell::operator_new
  0x19ae78 RayShell::operator_delete

Sheet rows:
  RayShell::RayShell(int, SimObjSig, COORD3 &, COORD3 &, COORD3 &
  RayShell::~RayShell(void)
  RayShell::GetNumActiveRayShells(void)
  RayShell::GetActiveRayShell(int)
  RayShell::ClearActiveRayShells(void)
  RayShell::SetTriggerHittingRayShell(int)
  RayShell::GetTriggerHittingRayShell(void)
  RayShell::GetNumTracers(void)
  RayShell::GetTracer(int)
  RayShell::DeleteTracer(int)
  RayShell::ClearTracers(int)
  RayShell::DrawTracers(void)
  RayShell::ClearFlashes(void)
  RayShell::DrawFlashes(void)
  RayShell::Reset(void)
  RayShell::AddNewRayShellForTriggerCheck(float)
  RayShell::CollideWithObjects(void)
  RayShell::CollideWithWorld(float &)
  RayShell::Fire(float &)
  RayShell::ConsiderVictim(SimObjSig, COORD3 &, COORD3 &, float)
  RayShell::DamagePhysicsObject(SimObjSig, COORD3 &, COORD3 &)
  RayShell::StoreTracer(COORD3 &, COORD3 &)
  RayShell::StaticStoreTracer(COORD3 &, COORD3 &, int, RSceneObj
  RayShell::AddFlash(SimObjSig, COORD3 &)
  RayShell type_info function
  RayShell::operator new(unsigned int)
  RayShell::operator delete(void *, unsigned int)
  RayShell::fNumTracers
  RayShell::fTracer
  RayShell::fNumFlashes
  RayShell::fFlash
  RayShell::fNumActiveRayShells
  RayShell::fActiveRayShells
  RayShell::fTriggerHittingRayShellIndex
  RayShell::kMaxRange
  RayShell::kScorchRadius
  RayShell::kBulletForce
  RayShell virtual table
  RayShell type_info node
  RayShell::fTracerSceneObjRef

Xbox methods treated as members (15 of 19; untyped ones count when ECX is read before it is written): AddFlash, AddNewRayShellForTriggerCheck, ClearActiveRayShells, ClearTracers, CollideWithObjects, CollideWithWorld, ConsiderVictim, DamagePhysicsObject, Fire, GetActiveRayShell, GetNumActiveRayShells, GetTriggerHittingRayShell, RayShell, SetTriggerHittingRayShell, scalar_deleting_destructor

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: RayShell@72e90, scalar_deleting_destructor@72160]
  +0x010  w[4] LEA/R addr-taken [6: AddNewRayShellForTriggerCheck@71d80, CollideWithObjects@72190, CollideWithWorld@72790, DamagePhysicsObject@72ba0, Fire@72d60, RayShell@72e90]
  +0x014  w[4] R [1: CollideWithWorld@72790]
  +0x018  w[4] R [1: CollideWithWorld@72790]
  +0x020  w- LEA addr-taken -> VU0_v3add [1: RayShell@72e90]
  +0x02c  w[4] R/W float [4: AddNewRayShellForTriggerCheck@71d80, CollideWithWorld@72790, DamagePhysicsObject@72ba0, RayShell@72e90]
  +0x030  w[4] LEA/R addr-taken -> VU0_v4scale [6: AddNewRayShellForTriggerCheck@71d80, CollideWithObjects@72190, CollideWithWorld@72790, DamagePhysicsObject@72ba0, Fire@72d60, RayShell@72e90]
  +0x034  w[4] R [2: CollideWithWorld@72790, DamagePhysicsObject@72ba0]
  +0x038  w[4] R [2: CollideWithWorld@72790, DamagePhysicsObject@72ba0]
  +0x040  w- LEA addr-taken [1: RayShell@72e90]
  +0x04c  w[4] R/W float [3: CollideWithWorld@72790, Fire@72d60, RayShell@72e90]
  +0x050  w- LEA addr-taken [4: CollideWithWorld@72790, DamagePhysicsObject@72ba0, Fire@72d60, RayShell@72e90]
  +0x05c  w[4] R/W [3: CollideWithWorld@72790, DamagePhysicsObject@72ba0, RayShell@72e90]
  +0x060  w[4] R/W [4: CollideWithWorld@72790, ConsiderVictim@71e40, Fire@72d60, RayShell@72e90]
  +0x064  w[4] LEA/R/W addr-taken [6: AddNewRayShellForTriggerCheck@71d80, CollideWithObjects@72190, CollideWithWorld@72790, DamagePhysicsObject@72ba0, Fire@72d60, RayShell@72e90]
  +0x070  w- LEA addr-taken [2: ConsiderVictim@71e40, Fire@72d60]
  +0x080  w- LEA addr-taken [2: ConsiderVictim@71e40, Fire@72d60]
  +0x08c  w[4] R/W [3: ConsiderVictim@71e40, Fire@72d60, RayShell@72e90]
  +0x090  w[4] R/W float [4: CollideWithWorld@72790, ConsiderVictim@71e40, Fire@72d60, RayShell@72e90]
  +0x0a0  w- LEA addr-taken [2: ConsiderVictim@71e40, Fire@72d60]
  +0x0b0  w- LEA addr-taken [1: ConsiderVictim@71e40]
  +0x0bc  w[4] R/W [3: ConsiderVictim@71e40, Fire@72d60, RayShell@72e90]
  +0x0c0  w[4] R/W float [2: ConsiderVictim@71e40, Fire@72d60]
  +0x0d0  w[4] R/W [4: CollideWithWorld@72790, DamagePhysicsObject@72ba0, Fire@72d60, RayShell@72e90]
  +0x0d4  w[4] R/W [4: CollideWithWorld@72790, DamagePhysicsObject@72ba0, Fire@72d60, RayShell@72e90]
  +0x0d8  w[4] R/W [4: CollideWithWorld@72790, DamagePhysicsObject@72ba0, Fire@72d60, RayShell@72e90]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W float [4: CollideWithObjects@1998c0, CollideWithWorld@199f88, RayShell@198e80, StaticStoreTracer@19a9b8]
  +0x004  w[4] R float [3: CollideWithObjects@1998c0, CollideWithWorld@199f88, StaticStoreTracer@19a9b8]
  +0x008  w[4] R/W float [4: CollideWithObjects@1998c0, CollideWithWorld@199f88, RayShell@198e80, StaticStoreTracer@19a9b8]
  +0x010  w[8] W [1: RayShell@198e80]
  +0x018  w[4] W [1: RayShell@198e80]
  +0x01c  w[4] R/W float [3: CollideWithWorld@199f88, DamagePhysicsObject@19a710, RayShell@198e80]
  +0x020  w[4, 8] LEA/R/W float addr-taken [5: CollideWithObjects@1998c0, CollideWithWorld@199f88, DamagePhysicsObject@19a710, Fire@19a3e0, RayShell@198e80]
  +0x028  w[4] R/W [2: Fire@19a3e0, RayShell@198e80]
  +0x030  w[8] LEA/W addr-taken [1: RayShell@198e80]
  +0x038  w[4] W [1: RayShell@198e80]
  +0x03c  w[4] R/W float [3: CollideWithWorld@199f88, Fire@19a3e0, RayShell@198e80]
  +0x040  w[8] LEA/W addr-taken [4: CollideWithWorld@199f88, DamagePhysicsObject@19a710, Fire@19a3e0, RayShell@198e80]
  +0x048  w[4] W [1: RayShell@198e80]
  +0x04c  w[4] R/W [3: CollideWithWorld@199f88, DamagePhysicsObject@19a710, RayShell@198e80]
  +0x050  w[4] R/W [5: CollideWithObjects@1998c0, CollideWithWorld@199f88, ConsiderVictim@19a548, Fire@19a3e0, RayShell@198e80]
  +0x054  w[4] LEA/R/W addr-taken [5: CollideWithObjects@1998c0, CollideWithWorld@199f88, DamagePhysicsObject@19a710, Fire@19a3e0, RayShell@198e80]
  +0x060  w[8] LEA/W addr-taken [2: ConsiderVictim@19a548, Fire@19a3e0]
  +0x068  w[4] W [1: ConsiderVictim@19a548]
  +0x070  w[8] LEA/W addr-taken [2: ConsiderVictim@19a548, Fire@19a3e0]
  +0x078  w[4] W [1: ConsiderVictim@19a548]
  +0x07c  w[4] R/W [3: ConsiderVictim@19a548, Fire@19a3e0, RayShell@198e80]
  +0x080  w[4] R/W float [4: CollideWithWorld@199f88, ConsiderVictim@19a548, Fire@19a3e0, RayShell@198e80]
  +0x090  w[8] LEA/W addr-taken [2: ConsiderVictim@19a548, Fire@19a3e0]
  +0x098  w[4] W [1: ConsiderVictim@19a548]
  +0x0a0  w[8] W [1: ConsiderVictim@19a548]
  +0x0a8  w[4] W [1: ConsiderVictim@19a548]
  +0x0ac  w[4] R/W [3: ConsiderVictim@19a548, Fire@19a3e0, RayShell@198e80]
  +0x0b0  w[4] R/W float [2: ConsiderVictim@19a548, Fire@19a3e0]
  +0x0c0  w[4] W [1: RayShell@198e80]
  +0x0c4  w[4] R/W [2: RayShell@198e80, StoreTracer@19a988]
  +0x0c8  w[4] R/W [2: RayShell@198e80, StoreTracer@19a988]
  +0x0cc  w[4] W [2: RayShell@198e80, ~RayShell@199200]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
