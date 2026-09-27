# RMissileStreak

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xed310 first calls: []

Xbox methods (6):
  0xa0e90 undefined Update(void)
  0xa0ea0 undefined Update(undefined4 param_1)
  0xa1070 undefined Init(void)
  0xa1330 undefined Draw(void)
  0xa1460 undefined __stdcall Kill(void)
  0xed310 undefined RMissileStreak(void)

PS2 methods (11):
  0x1e3e28 RMissileStreak::Init
  0x1e41a0 RMissileStreak::Kill
  0x1e4200 RMissileStreak::RMissileStreak
  0x1e4218 RMissileStreak::RMissileStreak
  0x1e4230 RMissileStreak::~RMissileStreak
  0x1e4258 RMissileStreak::Draw
  0x1e43e0 RMissileStreak::Update
  0x1e43f0 RMissileStreak::Reset
  0x1e4640 RMissileStreak::Update
  0x1e4b08 RMissileStreak::Init_global_ctors
  0x1e4b28 RMissileStreak::Init_global_dtors

Sheet rows:
  RMissileStreak::Init(void)
  RMissileStreak::Kill(void)
  RMissileStreak::RMissileStreak(void)
  RMissileStreak::RMissileStreak(RMissileStreak::StreakType)
  RMissileStreak::~RMissileStreak(void)
  RMissileStreak::Draw(void)
  RMissileStreak::Update(void)
  RMissileStreak::Reset(void)
  RMissileStreak::Update(COORD4 &, float)
  RMissileStreak::Update(COORD4 &)

Xbox methods treated as members (4 of 6; untyped ones count when ECX is read before it is written): Draw, RMissileStreak, Update

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: Draw@a1330, RMissileStreak@ed310, Update@a0ea0]
  +0x004  w[4] R/RW/W [4: Draw@a1330, RMissileStreak@ed310, Update@a0e90, Update@a0ea0]
  +0x008  w[4] R/W [3: Draw@a1330, RMissileStreak@ed310, Update@a0ea0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [5: Draw@1e4258, RMissileStreak@1e4200, RMissileStreak@1e4218, Reset@1e43f0, Update@1e4640]
  +0x004  w[4] R/W [6: Draw@1e4258, RMissileStreak@1e4200, RMissileStreak@1e4218, Reset@1e43f0, Update@1e43e0, Update@1e4640]
  +0x008  w[4] R/W [4: Draw@1e4258, RMissileStreak@1e4200, RMissileStreak@1e4218, Update@1e4640]
  +0x010  w- LEA addr-taken [1: Update@1e4640]
  +0x014  w- LEA addr-taken [1: Update@1e4640]
  +0x018  w- LEA addr-taken [1: Update@1e4640]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Update: RW +0x4 w4
