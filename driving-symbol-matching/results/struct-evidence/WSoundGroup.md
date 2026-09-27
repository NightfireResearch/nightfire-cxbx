# WSoundGroup

FastAlloc/constructed sizes under its tag: {'allocated': [16], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xcd5a0 first calls: ['UMemory::FastAlloc', 'WSoundMap::WSoundMap']

Xbox methods (8):
  0xccad0 undefined __thiscall Start(WSoundGroup * this)
  0xccb40 void __fastcall Update(int param_1)
  0xcd190 void __thiscall End(WSoundGroup * this)
  0xcd220 undefined Clear(void)
  0xcd370 undefined8 * __thiscall Add(int param_1_00, undefined8 * param_2, undefined4 param_3, int param_4)
  0xcd4c0 void * __thiscall Add(WSoundGroup * this, undefined4 param_1, undefined4 param_2)
  0xcd5a0 WSoundGroup * __thiscall WSoundGroup(WSoundGroup * this)
  0xcd630 undefined ~WSoundGroup(void)

PS2 methods (9):
  0x227868 WSoundGroup::WSoundGroup
  0x2278f8 WSoundGroup::Start
  0x2279d0 WSoundGroup::Add
  0x227d80 WSoundGroup::Add
  0x227e38 WSoundGroup::Add
  0x227e58 WSoundGroup::End
  0x228470 WSoundGroup::Update
  0x228560 WSoundGroup::Clear
  0x228b70 WSoundGroup::~WSoundGroup

Sheet rows:
  WSoundGroup::WSoundGroup(void)
  WSoundGroup::Start(void)
  WSoundGroup::Add(int, int, int)
  WSoundGroup::Add(int, char *)
  WSoundGroup::Add(int, int)
  WSoundGroup::End(void)
  WSoundGroup::Update(void)
  WSoundGroup::Clear(void)
  WSoundGroup::~WSoundGroup(void)

Xbox methods treated as members (8 of 8; untyped ones count when ECX is read before it is written): Add, Clear, End, Start, Update, WSoundGroup, ~WSoundGroup

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: WSoundGroup@cd5a0]
  +0x004  w[4] W [1: WSoundGroup@cd5a0]
  +0x008  w[4] W [1: WSoundGroup@cd5a0]
  +0x00c  w[4] R/W -> FUN_000ccc20, FUN_000cd2b0, FUN_000cd560 [7: Add@cd370, Clear@cd220, End@cd190, Start@ccad0, Update@ccb40, WSoundGroup@cd5a0…]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [1: WSoundGroup@227868]
  +0x004  w[4] W [1: WSoundGroup@227868]
  +0x008  w[4] W [1: WSoundGroup@227868]
  +0x00c  w[4] R/W [7: Add@2279d0, Clear@228560, End@227e58, Start@2278f8, Update@228470, WSoundGroup@227868…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
