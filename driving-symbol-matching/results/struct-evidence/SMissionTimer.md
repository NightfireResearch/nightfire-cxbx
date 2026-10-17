# SMissionTimer

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none

Xbox methods (7):
  0xb95e0 undefined Reset(undefined4 param_1)
  0xb9600 undefined Start(undefined4 param_1)
  0xb9620 undefined Increment(undefined4 param_1)
  0xb9630 undefined GetValue(void)
  0xb96a0 undefined IsReverse(void)
  0xb96b0 undefined SetTicking(undefined1 param_1)
  0xb9ce0 undefined Stop(void)

PS2 methods (10):
  0x209bb0 SMissionTimer::SMissionTimer
  0x209bc8 SMissionTimer::~SMissionTimer
  0x209bf0 SMissionTimer::Reset
  0x209c08 SMissionTimer::Start
  0x209c38 SMissionTimer::Stop
  0x209c78 SMissionTimer::Increment
  0x209c88 SMissionTimer::GetValue
  0x209d40 SMissionTimer::IsActive
  0x209d48 SMissionTimer::IsReverse
  0x209d58 SMissionTimer::SetTicking

Sheet rows:
  SMissionTimer::SMissionTimer(void)
  SMissionTimer::~SMissionTimer(void)
  SMissionTimer::Reset(int)
  SMissionTimer::Start(int)
  SMissionTimer::Stop(void)
  SMissionTimer::Increment(int)
  SMissionTimer::GetValue(void)
  SMissionTimer::IsActive(void)
  SMissionTimer::IsReverse(void)
  SMissionTimer::SetTicking(bool)

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): GetValue, Increment, IsReverse, Reset, SetTicking, Start, Stop

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[1] R/W [3: GetValue@b9630, Start@b9600, Stop@b9ce0]
  +0x001  w[1] R/W [2: GetValue@b9630, SetTicking@b96b0]
  +0x004  w[4] R/W [3: GetValue@b9630, Reset@b95e0, Start@b9600]
  +0x008  w[4] R/W [4: GetValue@b9630, Increment@b9620, Reset@b95e0, Stop@b9ce0]
  +0x00c  w[4] R/W [3: GetValue@b9630, IsReverse@b96a0, Start@b9600]
  +0x010  w[4] R/W [1: GetValue@b9630]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [5: GetValue@209c88, IsActive@209d40, SMissionTimer@209bb0, Start@209c08, Stop@209c38]
  +0x004  w[4] R/W [3: GetValue@209c88, SMissionTimer@209bb0, SetTicking@209d58]
  +0x008  w[4] R/W [3: GetValue@209c88, Reset@209bf0, Start@209c08]
  +0x00c  w[4] R/W [5: GetValue@209c88, Increment@209c78, Reset@209bf0, SMissionTimer@209bb0, Stop@209c38]
  +0x010  w[4] R/W [3: GetValue@209c88, IsReverse@209d48, Start@209c08]
  +0x014  w[4] R/W [2: GetValue@209c88, SMissionTimer@209bb0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Increment: R +0x8 w4
  IsReverse: R +0xc w4
  SetTicking: W +0x1 w1
