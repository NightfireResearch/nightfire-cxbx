# Grenade

FastAlloc/constructed sizes under its tag: {'allocated': [1760], 'constructed': []}
deleting destructor 0x5d7a0 frees/deletes with size 0x6e0 (call to UMemory::FastFree)
Xbox vtable 0x0018a398 (19 slots) stored by its constructor
Xbox vtable 0x0018ed98 (7 slots) stored by its constructor
PS2 sheet virtual table row: ['Grenade virtual table']
constructor 0x5d5c0 first calls: ['PhysicsObject::PhysicsObject', 'RMissileStreak::RMissileStreak', 'VU0_v4scale']

Xbox methods (5):
  0x5d4f0 undefined ~Grenade(void)
  0x5d550 undefined Render(undefined1 param_1)
  0x5d5c0 undefined Grenade(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 p
  0x5d7a0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x5d7d0 undefined Simulate(void)

PS2 methods (11):
  0x17e560 Grenade::Grenade
  0x17e7b0 Grenade::~Grenade
  0x17e818 Grenade::Render
  0x17e8b8 Grenade::Simulate
  0x17f008 Grenade::CheckForProximity
  0x17f0d8 Grenade::CheckTimer
  0x17f240 Grenade::operator_new
  0x17f260 Grenade::operator_delete
  0x17f280 Grenade::GetWeaponType
  0x17f288 Grenade::BlowUp
  0x17f390 Grenade::Grenade_global_ctors

Sheet rows:
  Grenade::Grenade(WeaponType, COORD3 &, COORD3 &, float, SimObjS
  Grenade::~Grenade(void)
  Grenade::Render(bool)
  Grenade::Simulate(void)
  Grenade::CheckForProximity(void)
  Grenade::CheckTimer(void)
  Grenade type_info function
  Grenade::operator new(unsigned int)
  Grenade::operator delete(void *, unsigned int)
  Grenade::GetWeaponType(void) const
  Grenade::BlowUp(void)
  Grenade ** find<Grenade **, Grenade *>(Grenade **, Grenade **,
  Grenade ** remove_copy<Grenade **, Grenade **, Grenade *>(Grena
  Grenade ** remove<Grenade **, Grenade *>(Grenade **, Grenade **
  Grenade virtual table
  Grenade type_info node

Xbox methods treated as members (5 of 5; untyped ones count when ECX is read before it is written): Grenade, Render, Simulate, scalar_deleting_destructor, ~Grenade

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: Grenade@5d5c0, ~Grenade@5d4f0]
  +0x004  w- LEA addr-taken -> WWorldPos::FindClosestFace [1: Simulate@5d7d0]
  +0x034  w[1] R [1: Simulate@5d7d0]
  +0x04a  w[2] R [3: Grenade@5d5c0, Render@5d550, Simulate@5d7d0]
  +0x04c  w[4] R -> RSceneObj::Load [1: Grenade@5d5c0]
  +0x070  w- LEA addr-taken -> RMissileStreak::Draw, RMissileStreak::RMissileStreak, RMissileStreak::Update, dummyNullFunction [3: Grenade@5d5c0, Render@5d550, ~Grenade@5d4f0]
  +0x6c0  w[4] R/W [2: Grenade@5d5c0, Simulate@5d7d0]
  +0x6c4  w[4] W [1: Grenade@5d5c0]
  +0x6c8  w[4] R/W [2: Grenade@5d5c0, Simulate@5d7d0]
  +0x6cc  w[4] R/W [2: Grenade@5d5c0, Simulate@5d7d0]
  +0x6d0  w[4] R/W [2: Grenade@5d5c0, Simulate@5d7d0]
  +0x6d4  w[1] R/W [2: Grenade@5d5c0, Simulate@5d7d0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R float [1: Simulate@17e8b8]
  +0x004  w[4] R float [1: Simulate@17e8b8]
  +0x008  w[4] R float [1: Simulate@17e8b8]
  +0x010  w[4] R float [1: Simulate@17e8b8]
  +0x014  w[4] R float [1: Simulate@17e8b8]
  +0x018  w[4] R float [1: Simulate@17e8b8]
  +0x020  w[4] R float [1: Simulate@17e8b8]
  +0x024  w[4] R float [1: Simulate@17e8b8]
  +0x028  w[4] R float [1: Simulate@17e8b8]
  +0x030  w[4] R [1: Simulate@17e8b8]
  +0x046  w[2] R [4: CheckForProximity@17f008, Grenade@17e560, Render@17e818, Simulate@17e8b8]
  +0x048  w[4] R [1: Grenade@17e560]
  +0x068  w[4] W [2: Grenade@17e560, ~Grenade@17e7b0]
  +0x070  w- LEA addr-taken [2: Grenade@17e560, Render@17e818]
  +0x6c0  w[4] R/W [3: GetWeaponType@17f280, Grenade@17e560, Simulate@17e8b8]
  +0x6c4  w[4] W float [1: Grenade@17e560]
  +0x6c8  w[4] R/W [2: CheckTimer@17f0d8, Grenade@17e560]
  +0x6cc  w[4] R/W [2: Grenade@17e560, Simulate@17e8b8]
  +0x6d0  w[4] R/W [2: Grenade@17e560, Simulate@17e8b8]
  +0x6d4  w[4] R/W [5: BlowUp@17f288, CheckForProximity@17f008, CheckTimer@17f0d8, Grenade@17e560, Simulate@17e8b8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
