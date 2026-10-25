# Mine

FastAlloc/constructed sizes under its tag: {'allocated': [136], 'constructed': []}
deleting destructor 0x5e6e0 frees/deletes with size 0x88 (call to UMemory::FastFree)
Xbox vtable 0x0018a398 (19 slots) stored by its constructor
Xbox vtable 0x0018ee28 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Mine virtual table']
constructor 0x5e480 first calls: ['PhysicsObject::PhysicsObject', 'VU0_v4scale', 'Util_GenerateMatrix']

Xbox methods (7):
  0x5e180 undefined ~Mine(void)
  0x5e200 undefined CheckForProximity(void)
  0x5e3e0 undefined ApplyDamage(void)
  0x5e400 undefined CheckTimer(void)
  0x5e480 undefined Mine(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 para
  0x5e6e0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x5e710 undefined Simulate(void)

PS2 methods (11):
  0x17fc18 Mine::Mine
  0x17ff48 Mine::~Mine
  0x17ffc8 Mine::Simulate
  0x1802e0 Mine::CheckForProximity
  0x1805f8 Mine::ApplyDamage
  0x180620 Mine::CheckTimer
  0x180820 Mine::operator_new
  0x180840 Mine::operator_delete
  0x180860 Mine::GetWeaponType
  0x180868 Mine::BlowUp
  0x180970 Mine::Mine_global_ctors

Sheet rows:
  Mine::Mine(WeaponType, COORD3 &, COORD3 &, float, SimObjSig)
  Mine::~Mine(void)
  Mine::Simulate(void)
  Mine::CheckForProximity(void)
  Mine::ApplyDamage(COORD3 &, COORD3 &, float, float, DamageType,
  Mine::CheckTimer(void)
  Mine type_info function
  Mine::operator new(unsigned int)
  Mine::operator delete(void *, unsigned int)
  Mine::GetWeaponType(void) const
  Mine::BlowUp(void)
  Mine ** find<Mine **, Mine *>(Mine **, Mine **, Mine * &, rando
  Mine ** remove_copy<Mine **, Mine **, Mine *>(Mine **, Mine **,
  Mine ** remove<Mine **, Mine *>(Mine **, Mine **, Mine * &)
  Mine virtual table
  Mine type_info node

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): ApplyDamage, CheckForProximity, CheckTimer, Mine, Simulate, scalar_deleting_destructor, ~Mine

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Mine@5e480, ~Mine@5e180]
  +0x04a  w[2] R [4: CheckForProximity@5e200, CheckTimer@5e400, Mine@5e480, Simulate@5e710]
  +0x04c  w[4] R [1: Simulate@5e710]
  +0x06c  w[4] W [1: Mine@5e480]
  +0x070  w[4] R/W [3: CheckTimer@5e400, Mine@5e480, Simulate@5e710]
  +0x074  w[4] R/W [2: CheckTimer@5e400, Mine@5e480]
  +0x078  w[4] W [1: Mine@5e480]
  +0x07c  w[1] R/W [3: ApplyDamage@5e3e0, Mine@5e480, Simulate@5e710]
  +0x080  w[4] R/W -> WTargetable::RemoveReference [2: Mine@5e480, ~Mine@5e180]
  +0x084  w[1] R/W [2: Mine@5e480, Simulate@5e710]

PS2 this-relative accesses (PS2 offsets):
  +0x046  w[2] R [4: CheckForProximity@1802e0, CheckTimer@180620, Mine@17fc18, Simulate@17ffc8]
  +0x048  w[4] R [1: Simulate@17ffc8]
  +0x068  w[4] W [2: Mine@17fc18, ~Mine@17ff48]
  +0x06c  w[4] R/W float [2: CheckForProximity@1802e0, Mine@17fc18]
  +0x070  w[4] R/W [5: CheckForProximity@1802e0, CheckTimer@180620, GetWeaponType@180860, Mine@17fc18, Simulate@17ffc8]
  +0x074  w[4] R/W [2: CheckTimer@180620, Mine@17fc18]
  +0x078  w[4] R/W [2: CheckForProximity@1802e0, Mine@17fc18]
  +0x07c  w[4] R/W [4: ApplyDamage@1805f8, BlowUp@180868, Mine@17fc18, Simulate@17ffc8]
  +0x080  w[4] R/W -> PhysicsObject::~PhysicsObject [2: Mine@17fc18, ~Mine@17ff48]
  +0x084  w[4] R/W [2: Mine@17fc18, Simulate@17ffc8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ApplyDamage: W +0x7c w1
