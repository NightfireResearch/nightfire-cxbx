# Smackable

FastAlloc/constructed sizes under its tag: {'allocated': [136], 'constructed': []}
deleting destructor 0x75530 frees/deletes with size 0x88 (call to UMemory::FastFree)
Xbox vtable 0x00190248 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Smackable virtual table']
constructor 0x75920 first calls: ['AttributeSet::AttributeSet', 'PhysicsObject::PhysicsObject', 'AttributeSet::~AttributeSet']

Xbox methods (6):
  0x75300 undefined ~Smackable(void)
  0x75370 undefined GoToSleep(void)
  0x75420 undefined Simulate(void)
  0x75530 undefined scalar_deleting_destructor(undefined1 param_1)
  0x75560 undefined ApplyDamage(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined param_4, undefined
  0x75920 undefined Smackable(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)

PS2 methods (9):
  0x19e100 Smackable::Smackable
  0x19e590 Smackable::~Smackable
  0x19e610 Smackable::GoToSleep
  0x19e718 Smackable::Simulate
  0x19e8c0 Smackable::ApplyDamage
  0x19ef90 Smackable::operator_new
  0x19efb0 Smackable::operator_delete
  0x19efd0 Smackable::GetTrigger
  0x19efd8 Smackable::Smackable_global_ctors

Sheet rows:
  Smackable::Smackable(RSceneObj *, WTrigger *, COORD3 &, unsigne
  Smackable::~Smackable(void)
  Smackable::GoToSleep(void)
  Smackable::Simulate(void)
  Smackable::ApplyDamage(COORD3 &, COORD3 &, float, float, Damage
  Smackable type_info function
  Smackable::operator new(unsigned int)
  Smackable::operator delete(void *, unsigned int)
  Smackable::GetTrigger(void) const
  Smackable ** find<Smackable **, Smackable *>(Smackable **, Smac
  Smackable ** remove_copy<Smackable **, Smackable **, Smackable
  Smackable ** remove<Smackable **, Smackable *>(Smackable **, Sm
  Smackable virtual table
  Smackable type_info node

Xbox methods treated as members (6 of 6; untyped ones count when ECX is read before it is written): ApplyDamage, GoToSleep, Simulate, Smackable, scalar_deleting_destructor, ~Smackable

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Smackable@75920, ~Smackable@75300]
  +0x04a  w[2] R [3: GoToSleep@75370, Simulate@75420, Smackable@75920]
  +0x04c  w[4] R [3: ApplyDamage@75560, GoToSleep@75370, Simulate@75420]
  +0x050  w[4] R [2: ApplyDamage@75560, Simulate@75420]
  +0x06c  w[4] R/W [2: Simulate@75420, Smackable@75920]
  +0x070  w[4] R/W [2: Simulate@75420, Smackable@75920]
  +0x074  w[4] LEA/W float addr-taken [3: ApplyDamage@75560, GoToSleep@75370, Smackable@75920]
  +0x078  w[4] R/W [3: ApplyDamage@75560, GoToSleep@75370, Smackable@75920]
  +0x07c  w[4] R/W -> WTrigger::UpdateRotPos [3: GoToSleep@75370, Smackable@75920, ~Smackable@75300]
  +0x080  w[4] R/W [2: GoToSleep@75370, Smackable@75920]
  +0x084  w[4] R/W -> ASmackable::Detatch [2: Smackable@75920, ~Smackable@75300]

PS2 this-relative accesses (PS2 offsets):
  +0x046  w[2] R [3: GoToSleep@19e610, Simulate@19e718, Smackable@19e100]
  +0x048  w[4] R [3: ApplyDamage@19e8c0, GoToSleep@19e610, Simulate@19e718]
  +0x04c  w[4] R [2: ApplyDamage@19e8c0, Simulate@19e718]
  +0x068  w[4] W [2: Smackable@19e100, ~Smackable@19e590]
  +0x06c  w[4] R/W [2: Simulate@19e718, Smackable@19e100]
  +0x070  w[4] R/W [2: Simulate@19e718, Smackable@19e100]
  +0x074  w[4] LEA/R/W float addr-taken [3: ApplyDamage@19e8c0, GoToSleep@19e610, Smackable@19e100]
  +0x078  w[4] R/W [3: ApplyDamage@19e8c0, GoToSleep@19e610, Smackable@19e100]
  +0x07c  w[4] R/W [4: GetTrigger@19efd0, GoToSleep@19e610, Smackable@19e100, ~Smackable@19e590]
  +0x080  w[4] R/W [2: GoToSleep@19e610, Smackable@19e100]
  +0x084  w[4] R/W -> PhysicsObject::SetAudioObject [2: Smackable@19e100, ~Smackable@19e590]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
