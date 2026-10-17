# Shell

FastAlloc/constructed sizes under its tag: {'allocated': [144], 'constructed': []}
deleting destructor 0x74980 frees/deletes with size 0x90 (call to UMemory::FastFree)
Xbox vtable 0x0019006c (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Shell virtual table']
constructor 0x74770 first calls: ['PhysicsObject::PhysicsObject', '__ftol2', 'VU0_v4scale']

Xbox methods (6):
  0x74660 undefined ~Shell(void)
  0x74670 undefined ApplyWorldDamage(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, und
  0x74770 undefined Shell(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 par
  0x74980 undefined scalar_deleting_destructor(undefined1 param_1)
  0x749b0 undefined ApplyWorldEffects(undefined4 param_1, undefined4 param_2, undefined param_3, undefined4 param_4, und
  0x74e80 undefined Simulate(void)

PS2 methods (8):
  0x19cd90 Shell::Shell
  0x19d028 Shell::~Shell
  0x19d080 Shell::Simulate
  0x19d5a8 Shell::ApplyWorldEffects
  0x19ddb8 Shell::ApplyWorldDamage
  0x19e068 Shell::operator_new
  0x19e088 Shell::operator_delete
  0x19e0b8 Shell::Shell_global_ctors

Sheet rows:
  Shell::Shell(WeaponType, COORD3 &, COORD3 &, SimObjSig, float,
  Shell::~Shell(void)
  Shell::Simulate(void)
  Shell::ApplyWorldEffects(COORD4 &, COORD4 &, int, int, WSurface
  Shell::ApplyWorldDamage(float, WWorldPos &, float, DecalType, W
  Shell type_info function
  Shell::operator new(unsigned int)
  Shell::operator delete(void *, unsigned int)
  Shell::GetWeaponType(void)
  Shell::GetStreakType(void)
  Shell ** find<Shell **, Shell *>(Shell **, Shell **, Shell * &,
  Shell ** remove_copy<Shell **, Shell **, Shell *>(Shell **, She
  Shell ** remove<Shell **, Shell *>(Shell **, Shell **, Shell *
  Shell virtual table
  Shell type_info node

Xbox methods treated as members (4 of 6; untyped ones count when ECX is read before it is written): Shell, Simulate, scalar_deleting_destructor, ~Shell

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Shell@74770, ~Shell@74660]
  +0x004  w- LEA addr-taken [1: Simulate@74e80]
  +0x04a  w[2] R [2: Shell@74770, Simulate@74e80]
  +0x06c  w[4] R/W [2: Shell@74770, Simulate@74e80]
  +0x070  w[4] R/W float [2: Shell@74770, Simulate@74e80]
  +0x074  w[4] R/W float [2: Shell@74770, Simulate@74e80]
  +0x078  w[4] R/W [2: Shell@74770, Simulate@74e80]
  +0x080  w- LEA addr-taken [2: Shell@74770, Simulate@74e80]
  +0x08c  w[4] W [1: Shell@74770]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[8] R [1: ApplyWorldEffects@19d5a8]
  +0x008  w[4] R [1: ApplyWorldEffects@19d5a8]
  +0x046  w[2] R [2: Shell@19cd90, Simulate@19d080]
  +0x068  w[4] W [2: Shell@19cd90, ~Shell@19d028]
  +0x06c  w[4] R/W [2: Shell@19cd90, Simulate@19d080]
  +0x070  w[4] R/W float [2: Shell@19cd90, Simulate@19d080]
  +0x074  w[4] R/W float [2: Shell@19cd90, Simulate@19d080]
  +0x078  w[4] R/W float [2: Shell@19cd90, Simulate@19d080]
  +0x080  w[8] LEA/W addr-taken [2: Shell@19cd90, Simulate@19d080]
  +0x088  w[4] W [1: Shell@19cd90]
  +0x08c  w[4] W [1: Shell@19cd90]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~Shell: W +0x0 w4
