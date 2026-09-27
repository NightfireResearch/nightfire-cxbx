# RVehicleParticle

FastAlloc/constructed sizes under its tag: {'allocated': [76], 'constructed': []}
deleting destructor 0xa8220 frees/deletes with size 0x4c (call to UMemory::FastFree)
Xbox vtable 0x001930b0 (1 slots) stored by its constructor
PS2 sheet virtual table row: ['RVehicleParticle virtual table']
constructor 0xa6de0 first calls: ['__builtin_vec_new', 'UMemory::FastAlloc', 'RParticleSystem::RParticleSystem']

Xbox methods (11):
  0xa6db0 undefined4 * __stdcall LoadAttributes(void)
  0xa6de0 undefined8 __thiscall RVehicleParticle(RVehicleParticle * this, RVehicleParticle * param_1_00, void * vehicle)
  0xa6f10 undefined TriggerTireSmoke(undefined4 param_1)
  0xa6f70 undefined TriggerSparks(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefi
  0xa7300 undefined ResetAll(void)
  0xa79f0 undefined ~RVehicleParticle(void)
  0xa7ac0 undefined UpdateCar(undefined1 param_1)
  0xa8120 undefined UpdateAll(void)
  0xa8220 undefined scalar_deleting_destructor(undefined1 param_1)
  0xa8240 undefined __stdcall Init(void)
  0xa8430 undefined __stdcall Kill(void)

PS2 methods (15):
  0x1edf38 RVehicleParticle::Init
  0x1ee170 RVehicleParticle::Kill
  0x1ee1f0 RVehicleParticle::LoadAttributes
  0x1ee238 RVehicleParticle::RVehicleParticle
  0x1ee3d8 RVehicleParticle::Reset
  0x1ee3e0 RVehicleParticle::~RVehicleParticle
  0x1ee4a0 RVehicleParticle::UpdateCar
  0x1eece8 RVehicleParticle::TriggerTireSmoke
  0x1eed18 RVehicleParticle::TriggerSparks
  0x1ef1e0 RVehicleParticle::UpdateAll
  0x1ef338 RVehicleParticle::ResetAll
  0x1f02c0 RVehicleParticle::RVehicleParticle_type_info_function
  0x1f0300 RVehicleParticle::operator_new
  0x1f0320 RVehicleParticle::operator_delete
  0x1f0340 RVehicleParticle::fAllCars_global_ctors

Sheet rows:
  RVehicleParticle::Init(void)
  RVehicleParticle::Kill(void)
  RVehicleParticle::LoadAttributes(void)
  RVehicleParticle::RVehicleParticle(PVehicle *)
  RVehicleParticle::Reset(void)
  RVehicleParticle::~RVehicleParticle(void)
  RVehicleParticle::UpdateCar(bool)
  RVehicleParticle::TriggerTireSmoke(int)
  RVehicleParticle::TriggerSparks(COORD3 &, COORD3, float, RVehic
  RVehicleParticle::UpdateAll(void)
  RVehicleParticle::ResetAll(void)
  RVehicleParticle type_info function
  RVehicleParticle::operator new(unsigned int)
  RVehicleParticle::operator delete(void *, unsigned int)
  RVehicleParticle::fAllCars
  RVehicleParticle virtual table
  RVehicleParticle type_info node

Xbox methods treated as members (8 of 11; untyped ones count when ECX is read before it is written): RVehicleParticle, ResetAll, TriggerSparks, TriggerTireSmoke, UpdateAll, UpdateCar, scalar_deleting_destructor, ~RVehicleParticle

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RVehicleParticle@a6de0, ~RVehicleParticle@a79f0]
  +0x004  w[4] R/W -> __builtin_delete [4: RVehicleParticle@a6de0, TriggerSparks@a6f70, UpdateCar@a7ac0, ~RVehicleParticle@a79f0]
  +0x008  w- LEA addr-taken [3: RVehicleParticle@a6de0, UpdateCar@a7ac0, ~RVehicleParticle@a79f0]
  +0x040  w[4] R/W [3: RVehicleParticle@a6de0, TriggerSparks@a6f70, UpdateCar@a7ac0]
  +0x044  w[4] R/W [3: RVehicleParticle@a6de0, TriggerTireSmoke@a6f10, UpdateCar@a7ac0]
  +0x048  w[4] W float [3: RVehicleParticle@a6de0, TriggerTireSmoke@a6f10, UpdateCar@a7ac0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> __builtin_vec_delete [4: RVehicleParticle@1ee238, TriggerSparks@1eed18, UpdateCar@1ee4a0, ~RVehicleParticle@1ee3e0]
  +0x004  w- LEA addr-taken [3: RVehicleParticle@1ee238, UpdateCar@1ee4a0, ~RVehicleParticle@1ee3e0]
  +0x03c  w[4] R/W [3: RVehicleParticle@1ee238, TriggerSparks@1eed18, UpdateCar@1ee4a0]
  +0x040  w[4] R/W [4: RVehicleParticle@1ee238, Reset@1ee3d8, TriggerTireSmoke@1eece8, UpdateCar@1ee4a0]
  +0x044  w[4] R/W float [3: RVehicleParticle@1ee238, TriggerTireSmoke@1eece8, UpdateCar@1ee4a0]
  +0x048  w[4] W [2: RVehicleParticle@1ee238, ~RVehicleParticle@1ee3e0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
