# AFader

FastAlloc/constructed sizes under its tag: {'allocated': [4], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x125180 first calls: ['UMemory::FastAlloc', 'AFader::Priv::Priv']

Xbox methods (10):
  0x125180 undefined AFader(undefined4 param_1, undefined4 param_2)
  0x1251f0 undefined QueuePrimary(void)
  0x125200 void __thiscall SetSecondary(AFader * this, byte * param_1)
  0x125210 undefined SetSecondary(undefined1 param_1)
  0x125220 void __thiscall SetFilter(int * param_1_00, undefined4 param_2)
  0x125230 undefined Call911(void)
  0x125260 undefined Update(void)
  0x125cb0 undefined Create(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x125d40 undefined4 __cdecl Get(char * param_1)
  0x125d60 undefined4 __cdecl Remove(char * param_1)

PS2 methods (11):
  0x2f95b0 AFader::AFader
  0x2f9610 AFader::~AFader
  0x2f9688 AFader::Create
  0x2f9710 AFader::Get
  0x2f9740 AFader::Remove
  0x2f9798 AFader::QueuePrimary
  0x2f97b8 AFader::SetSecondary
  0x2f97d8 AFader::SetSecondary
  0x2f97f8 AFader::SetFilter
  0x2f9808 AFader::Call911
  0x2f9828 AFader::Update

Sheet rows:
  AFader::Priv::Priv(AStream &, AMix &)
  AFader::Priv::~Priv(void)
  AFader::Priv::Event(char *, float, bool, bool)
  AFader::Priv::SetSecondary(char *)
  AFader::Priv::Call911(void)
  AFader::Priv::SetSecondary(bool)
  AFader::Priv::Update(void)
  AFader::AFader(AStream &, AMix &)
  AFader::~AFader(void)
  AFader::Create(char *, AStream &, AMix &)
  AFader::Get(char *)
  AFader::Remove(char *)
  AFader::QueuePrimary(char *, float, bool, bool)
  AFader::SetSecondary(char *)
  AFader::SetSecondary(bool)
  AFader::SetFilter(float)
  AFader::Call911(void)
  AFader::Update(void)
  AFader::GetFade(void) const
  AFader::GetSecondary(void) const
  AFader::fgLocale

Xbox methods treated as members (8 of 10; untyped ones count when ECX is read before it is written): AFader, Call911, Create, QueuePrimary, SetFilter, SetSecondary, Update

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [7: AFader@125180, Call911@125230, QueuePrimary@1251f0, SetFilter@125220, SetSecondary@125200, SetSecondary@125210…]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> AFader::Priv::~Priv [3: AFader@2f95b0, SetFilter@2f97f8, ~AFader@2f9610]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  QueuePrimary: R +0x0 w4
  SetSecondary: R +0x0 w4
  SetSecondary: R +0x0 w4
  SetFilter: R +0x0 w4
  Update: R +0x0 w4
