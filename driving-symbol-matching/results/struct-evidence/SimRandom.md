# SimRandom

FastAlloc/constructed sizes under its tag: {'allocated': [16], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x5cc80 first calls: []

Xbox methods (3):
  0x5cc80 undefined SimRandom(void)
  0x5cc90 uint __fastcall SimRandom_Generate(uint * this)
  0x5ccb0 uint __thiscall Reset(SimRandom * this)

PS2 methods (5):
  0x17db68 SimRandom::SimRandom
  0x17db80 SimRandom::~SimRandom
  0x17dba8 SimRandom::SetSeed
  0x17dbb0 SimRandom::Reset
  0x17dc28 SimRandom::SimRandom_Generate

Sheet rows:
  SimRandom::SimRandom(void)
  SimRandom::~SimRandom(void)
  SimRandom::SetSeed(unsigned int)
  SimRandom::Reset(void)
  SimRandom::SimRandom_Generate(void)

Xbox methods treated as members (3 of 3; untyped ones count when ECX is read before it is written): Reset, SimRandom, SimRandom_Generate

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: Reset@5ccb0, SimRandom_Generate@5cc90]
  +0x004  w[4] R/W [2: Reset@5ccb0, SimRandom_Generate@5cc90]
  +0x008  w[4] R/W [3: Reset@5ccb0, SimRandom@5cc80, SimRandom_Generate@5cc90]
  +0x00c  w[4] RW/W [2: Reset@5ccb0, SimRandom_Generate@5cc90]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [2: Reset@17dbb0, SimRandom_Generate@17dc28]
  +0x004  w[4] W [2: Reset@17dbb0, SimRandom_Generate@17dc28]
  +0x008  w[4] R/W [4: Reset@17dbb0, SetSeed@17dba8, SimRandom@17db68, SimRandom_Generate@17dc28]
  +0x00c  w[4] R/W [2: Reset@17dbb0, SimRandom_Generate@17dc28]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  SimRandom: W +0x8 w4
