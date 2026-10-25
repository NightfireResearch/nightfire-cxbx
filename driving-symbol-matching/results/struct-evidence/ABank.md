# ABank

FastAlloc/constructed sizes under its tag: {'allocated': [24], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x125db0 first calls: ['AIndex::AIndex']
constructor 0x125dd0 first calls: ['AIndex::AIndex', 'strcat', 'UFileLoader::FileLoadz']

Xbox methods (11):
  0x125db0 undefined ABank(void)
  0x125dd0 ABank * __thiscall ABank(ABank * this, undefined4 * param_1_00, char param_2)
  0x125ec0 bool __thiscall IsPatchPresent(ABank * this, int patchNum)
  0x125ef0 undefined ~ABank(void)
  0x126910 undefined Begin(undefined4 param_1)
  0x126930 undefined End(undefined4 param_1)
  0x126940 undefined Load(undefined4 param_1, undefined4 param_2)
  0x1269d0 undefined * __thiscall Get(ABank * this, char * param_1)
  0x1269f0 undefined Remove(void)
  0x126a30 undefined GetPatchName(undefined4 param_1, undefined4 param_2)
  0x126ad0 undefined Load(undefined4 param_1, undefined4 param_2)

PS2 methods (15):
  0x2fd1a0 ABank::Begin
  0x2fd1d0 ABank::End
  0x2fd1f8 ABank::Load
  0x2fd288 ABank::Load
  0x2fd308 ABank::Get
  0x2fd348 ABank::Remove
  0x2fd3b8 ABank::RemoveAll
  0x2fd418 ABank::ABank
  0x2fd458 ABank::ABank
  0x2fd598 ABank::Status
  0x2fd698 ABank::IsPatchPresent
  0x2fd6d8 ABank::GetPatchName
  0x2fd868 ABank::~ABank
  0x2fe980 ABank::fgPath_global_ctors
  0x2fe9a0 ABank::fgPath_global_dtors

Sheet rows:
  ABank::Begin(void)
  ABank::End(void)
  ABank::Load(char *, bool)
  ABank::Load(char *, int)
  ABank::Get(char *)
  ABank::Remove(void)
  ABank::RemoveAll(void)
  ABank::ABank(void)
  ABank::ABank(char *, bool)
  ABank::Status(void) const
  ABank::IsPatchPresent(int) const
  ABank::GetPatchName(int, int)
  ABank::~ABank(void)
  ABank::fgPath
  ABank::fgEmpty
  ABank::fgBanks

Xbox methods treated as members (8 of 11; untyped ones count when ECX is read before it is written): ABank, Get, GetPatchName, IsPatchPresent, Load, Remove, ~ABank

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> MEM_free, UMemory::Free [3: ABank@125db0, ABank@125dd0, ~ABank@125ef0]
  +0x004  w[4] R/W [3: ABank@125db0, ABank@125dd0, Remove@1269f0]
  +0x008  w[4] LEA/R/W addr-taken [4: ABank@125db0, ABank@125dd0, IsPatchPresent@125ec0, ~ABank@125ef0]
  +0x00c  w[4] W [1: ABank@125dd0]
  +0x010  w- LEA addr-taken -> AIndex::AIndex, AIndex::~AIndex [3: ABank@125db0, ABank@125dd0, ~ABank@125ef0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> AIndex::~AIndex [3: ABank@2fd418, ABank@2fd458, ~ABank@2fd868]
  +0x004  w[4] W [2: ABank@2fd418, ABank@2fd458]
  +0x008  w[4] LEA/R/W addr-taken -> UMemory::Alloc, UMemory::Free [4: ABank@2fd418, ABank@2fd458, IsPatchPresent@2fd698, ~ABank@2fd868]
  +0x00c  w[4] R/W [2: ABank@2fd458, Status@2fd598]
  +0x010  w- LEA addr-taken [1: ~ABank@2fd868]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
