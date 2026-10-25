# Newton

FastAlloc/constructed sizes under its tag: {'allocated': [144], 'constructed': []}
Xbox vtable 0x0018a398 (19 slots) stored by its constructor
Xbox vtable 0x0018ef7c (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Newton virtual table']
constructor 0x61060 first calls: ['PhysicsObject::PhysicsObject', '__ftol2', 'Util_GenerateMatrix']

Xbox methods (6):
  0x60b60 undefined ~Newton(void)
  0x60b70 undefined Simulate(void)
  0x61050 undefined ApplyDamage(void)
  0x61060 ulong __thiscall Newton(Newton * this, COORD3.conflict * param_1, COORD3.conflict * param_2, COORD3.conflict *
  0x612f0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x61310 undefined SpawnFromEvent(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined1 param_4, undef

PS2 methods (8):
  0x183c70 Newton::Newton
  0x184040 Newton::~Newton
  0x184098 Newton::Simulate
  0x1847e0 Newton::ApplyDamage
  0x1847e8 Newton::SpawnFromEvent
  0x185258 Newton::operator_new
  0x185280 Newton::operator_delete
  0x185398 Newton::Newton_global_ctors

Sheet rows:
  Newton::Newton(COORD3 &, COORD3 &, COORD3 &, COORD3 &, CARP::In
  Newton::~Newton(void)
  Newton::Simulate(void)
  Newton::ApplyDamage(COORD3 &, COORD3 &, float, float, DamageTyp
  Newton::SpawnFromEvent(float, float, CARP::Instance *, bool, in
  Newton type_info function
  Newton::operator new(unsigned int)
  Newton::operator delete(void *, unsigned int)
  Newton ** find<Newton **, Newton *>(Newton **, Newton **, Newto
  Newton ** remove_copy<Newton **, Newton **, Newton *>(Newton **
  Newton ** remove<Newton **, Newton *>(Newton **, Newton **, New
  Newton virtual table
  Newton type_info node

Xbox methods treated as members (5 of 6; untyped ones count when ECX is read before it is written): ApplyDamage, Newton, Simulate, scalar_deleting_destructor, ~Newton

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Newton@61060, ~Newton@60b60]
  +0x004  w- LEA addr-taken [1: Simulate@60b70]
  +0x04a  w[2] R [2: Newton@61060, Simulate@60b70]
  +0x04c  w[4] R [1: Newton@61060]
  +0x06c  w- LEA addr-taken [1: Newton@61060]
  +0x070  w[4] R/W [2: Newton@61060, Simulate@60b70]
  +0x074  w[4] W [1: Newton@61060]
  +0x080  w- LEA addr-taken [2: Newton@61060, Simulate@60b70]

PS2 this-relative accesses (PS2 offsets):
  +0x046  w[2] R [2: Newton@183c70, Simulate@184098]
  +0x048  w[4] R [1: Newton@183c70]
  +0x068  w[4] W [2: Newton@183c70, ~Newton@184040]
  +0x06c  w[4] LEA/W float addr-taken [1: Newton@183c70]
  +0x070  w[4] R/W float [2: Newton@183c70, Simulate@184098]
  +0x074  w[4] W [1: Newton@183c70]
  +0x080  w[8] R/W [2: Newton@183c70, Simulate@184098]
  +0x088  w[8] R/W [2: Newton@183c70, Simulate@184098]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~Newton: W +0x0 w4
