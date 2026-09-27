# Human

FastAlloc/constructed sizes under its tag: {'allocated': [144], 'constructed': []}
deleting destructor 0x5dea0 frees/deletes with size 0x90 (call to UMemory::FastFree)
Xbox vtable 0x0018edf4 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Human virtual table']
constructor 0x5dd90 first calls: ['PhysicsObject::PhysicsObject', 'Util_GenerateMatrix', 'Simulation::GetSimpleRigidBody']

Xbox methods (8):
  0x5dc60 undefined ~Human(void)
  0x5dcc0 undefined Activate(void)
  0x5dce0 undefined Deactivate(void)
  0x5dd00 undefined Simulate(void)
  0x5dd90 Human * __thiscall Human(Human * this, undefined4 param_1, undefined4 param_2, undefined4 * param_3)
  0x5dea0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x5ded0 undefined SetAICharacter(undefined4 param_1)
  0x5dfa0 undefined ApplyDamage(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined param_4, undefined

PS2 methods (13):
  0x17f3b0 Human::Human
  0x17f4c0 Human::~Human
  0x17f540 Human::Activate
  0x17f570 Human::Deactivate
  0x17f5a0 Human::SetAICharacter
  0x17f6c0 Human::ApplyDamage
  0x17f980 Human::Simulate
  0x17fbd0 Human::GetHuman
  0x17fbd8 Human::GetAICharacter
  0x17fbe0 Human::GetDamagePos
  0x17fbe8 Human::SetInsideVehicle
  0x17fbf0 Human::GetInsideVehicle
  0x17fbf8 Human::Human_global_ctors

Sheet rows:
  Human::Human(HumanType, COORD3 &, COORD3 &, SimObjSig)
  Human::~Human(void)
  Human::Activate(void)
  Human::Deactivate(void)
  Human::SetAICharacter(AICharacter *)
  Human::ApplyDamage(COORD3 &, COORD3 &, float, float, DamageType
  Human::Simulate(void)
  Human type_info function
  Human::operator new(unsigned int)
  Human::operator delete(void *, unsigned int)
  Human::GetHuman(void) const
  Human::GetAICharacter(void) const
  Human::GetDamagePos(void)
  Human::SetInsideVehicle(bool)
  Human::GetInsideVehicle(void)
  Human ** find<Human **, Human *>(Human **, Human **, Human * &,
  Human ** remove_copy<Human **, Human **, Human *>(Human **, Hum
  Human ** remove<Human **, Human *>(Human **, Human **, Human *
  Human virtual table
  Human type_info node

Xbox methods treated as members (8 of 8; untyped ones count when ECX is read before it is written): Activate, ApplyDamage, Deactivate, Human, SetAICharacter, Simulate, scalar_deleting_destructor, ~Human

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Human@5dd90, ~Human@5dc60]
  +0x04a  w[2] R [6: Activate@5dcc0, ApplyDamage@5dfa0, Deactivate@5dce0, Human@5dd90, SetAICharacter@5ded0, Simulate@5dd00]
  +0x06c  w[4] R/W [2: ApplyDamage@5dfa0, Human@5dd90]
  +0x070  w[4] R/W -> AICharacter::Execute [5: ApplyDamage@5dfa0, Human@5dd90, SetAICharacter@5ded0, Simulate@5dd00, ~Human@5dc60]
  +0x074  w[1] R/W [2: ApplyDamage@5dfa0, Human@5dd90]
  +0x080  w- LEA addr-taken [1: ApplyDamage@5dfa0]
  +0x08c  w[1] W [1: Human@5dd90]

PS2 this-relative accesses (PS2 offsets):
  +0x046  w[2] R [5: Activate@17f540, Deactivate@17f570, Human@17f3b0, SetAICharacter@17f5a0, Simulate@17f980]
  +0x068  w[4] W [2: Human@17f3b0, ~Human@17f4c0]
  +0x06c  w[4] R/W [3: ApplyDamage@17f6c0, GetHuman@17fbd0, Human@17f3b0]
  +0x070  w[4] R/W -> AICharacter::Execute [6: ApplyDamage@17f6c0, GetAICharacter@17fbd8, Human@17f3b0, SetAICharacter@17f5a0, Simulate@17f980, ~Human@17f4c0]
  +0x074  w[4] R/W [2: ApplyDamage@17f6c0, Human@17f3b0]
  +0x080  w[8] LEA/W addr-taken [2: ApplyDamage@17f6c0, GetDamagePos@17fbe0]
  +0x088  w[4] W [1: ApplyDamage@17f6c0]
  +0x08c  w[4] R/W [3: GetInsideVehicle@17fbf0, Human@17f3b0, SetInsideVehicle@17fbe8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Activate: R +0x4a w2
  Deactivate: R +0x4a w2
