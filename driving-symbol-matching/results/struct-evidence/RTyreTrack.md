# RTyreTrack

FastAlloc/constructed sizes under its tag: {'allocated': [2336], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xabfc0 first calls: ['VU0_v4Init', 'VU0_v4Init']

Xbox methods (5):
  0xabfc0 RTyreTrack * __thiscall RTyreTrack(RTyreTrack * this, float param_1, bool param_2, bool param_3)
  0xac050 undefined Init(void)
  0xac170 undefined Break(undefined4 param_1)
  0xac250 undefined Draw(void)
  0xac4c0 undefined Add(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 param

PS2 methods (8):
  0x1f4fb0 RTyreTrack::RTyreTrack
  0x1f5010 RTyreTrack::Init
  0x1f5218 RTyreTrack::Kill
  0x1f5220 RTyreTrack::Draw
  0x1f5550 RTyreTrack::Add
  0x1f5a48 RTyreTrack::Break
  0x1f5ca8 RTyreTrack::RTyreTrack_global_ctors
  0x1f5cc8 RTyreTrack::RTyreTrack_global_dtors

Sheet rows:
  RTyreTrack::RTyreTrack(float, bool, bool)
  RTyreTrack::Init(void)
  RTyreTrack::Kill(void)
  RTyreTrack::Draw(void)
  RTyreTrack::Add(COORD4 &, float, float, int, WWorldPos *)
  RTyreTrack::Break(int)

Xbox methods treated as members (4 of 5; untyped ones count when ECX is read before it is written): Add, Break, Draw, RTyreTrack

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x900  w[4] R/W [4: Add@ac4c0, Break@ac170, Draw@ac250, RTyreTrack@abfc0]
  +0x904  w[4] R/W [4: Add@ac4c0, Break@ac170, Draw@ac250, RTyreTrack@abfc0]
  +0x908  w[4] R/W [3: Add@ac4c0, Break@ac170, RTyreTrack@abfc0]
  +0x90c  w[4] R/W [4: Add@ac4c0, Break@ac170, Draw@ac250, RTyreTrack@abfc0]
  +0x910  w[4] W float [2: Add@ac4c0, RTyreTrack@abfc0]
  +0x914  w[1] R/RW/W [3: Add@ac4c0, Break@ac170, RTyreTrack@abfc0]
  +0x915  w[1] R/W [4: Add@ac4c0, Break@ac170, Draw@ac250, RTyreTrack@abfc0]
  +0x916  w[1] R/W [4: Add@ac4c0, Break@ac170, Draw@ac250, RTyreTrack@abfc0]
  +0x917  w[1] R/W [3: Add@ac4c0, Break@ac170, RTyreTrack@abfc0]

PS2 this-relative accesses (PS2 offsets):
  +0x004  w- LEA addr-taken [1: Add@1f5550]
  +0x008  w- LEA addr-taken [1: Add@1f5550]
  +0x800  w- LEA addr-taken [2: Break@1f5a48, Draw@1f5220]
  +0x900  w[4] R/W [4: Add@1f5550, Break@1f5a48, Draw@1f5220, RTyreTrack@1f4fb0]
  +0x904  w[4] R/W [4: Add@1f5550, Break@1f5a48, Draw@1f5220, RTyreTrack@1f4fb0]
  +0x908  w[4] R/W [3: Add@1f5550, Break@1f5a48, RTyreTrack@1f4fb0]
  +0x90c  w[4] R/W [4: Add@1f5550, Break@1f5a48, Draw@1f5220, RTyreTrack@1f4fb0]
  +0x910  w[4] R/W float [3: Add@1f5550, Draw@1f5220, RTyreTrack@1f4fb0]
  +0x914  w[1] R/W [3: Add@1f5550, Break@1f5a48, RTyreTrack@1f4fb0]
  +0x915  w[1] R/W [4: Add@1f5550, Break@1f5a48, Draw@1f5220, RTyreTrack@1f4fb0]
  +0x916  w[1] R/W [4: Add@1f5550, Break@1f5a48, Draw@1f5220, RTyreTrack@1f4fb0]
  +0x917  w[1] R/W [4: Add@1f5550, Break@1f5a48, Draw@1f5220, RTyreTrack@1f4fb0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
