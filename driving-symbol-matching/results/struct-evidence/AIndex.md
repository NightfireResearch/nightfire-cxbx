# AIndex

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x1276c0 first calls: ['UMemory::FastAlloc', 'FUN_00127620', '_strrchr']
constructor 0x1279e0 first calls: ['UMemory::FastAlloc', 'FUN_00127620']

Xbox methods (5):
  0x126d10 undefined Lookup(undefined1 param_1)
  0x126d40 undefined __thiscall Lookup(undefined4 param_1_00, char * param_2)
  0x1276c0 undefined AIndex(undefined4 param_1, undefined4 param_2)
  0x1279e0 undefined AIndex(void)
  0x127a40 undefined ~AIndex(void)

PS2 methods (5):
  0x2f5498 AIndex::AIndex
  0x2f54e0 AIndex::AIndex
  0x2f5828 AIndex::Lookup
  0x2f5880 AIndex::Lookup
  0x2f58e0 AIndex::~AIndex

Sheet rows:
  AIndex::AIndex(void)
  AIndex::AIndex(char *, char *)
  AIndex::Lookup(int) const
  AIndex::Lookup(char *) const
  AIndex::~AIndex(void)

Xbox methods treated as members (5 of 5; untyped ones count when ECX is read before it is written): AIndex, Lookup, ~AIndex

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W -> FUN_00127580 [5: AIndex@1276c0, AIndex@1279e0, Lookup@126d10, Lookup@126d40, ~AIndex@127a40]
  +0x004  w[4] R/W [3: AIndex@1276c0, AIndex@1279e0, ~AIndex@127a40]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W -> AIndexMap::~AIndexMap [5: AIndex@2f5498, AIndex@2f54e0, Lookup@2f5828, Lookup@2f5880, ~AIndex@2f58e0]
  +0x004  w[4] R/W -> UMemory::Free [3: AIndex@2f5498, AIndex@2f54e0, ~AIndex@2f58e0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
