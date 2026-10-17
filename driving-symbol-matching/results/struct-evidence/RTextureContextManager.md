# RTextureContextManager

FastAlloc/constructed sizes under its tag: None
Xbox vtable 0x001921b4 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RTextureContextManager virtual table']

Xbox methods (7):
  0x8b700 undefined Init(void)
  0x93fb0 int __thiscall FindOrCreateTexture(RTextureContextManager * this, int param_1_00, uint param_2, int param_3)
  0x940c0 undefined4 __cdecl GetContext(int param_1)
  0x94c90 undefined KillContext(undefined4 param_1)
  0x951e0 undefined NewContext(undefined4 param_1, undefined4 param_2)
  0x95290 undefined ~RTextureContextManager(void)
  0x95360 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (11):
  0x1cfd80 RTextureContextManager::NewContext
  0x1cfe40 RTextureContextManager::KillContext
  0x1d0420 RTextureContextManager::GetContext
  0x1d0458 RTextureContextManager::FindOrCreateTexture
  0x1d0578 RTextureContextManager::RTextureContextManager
  0x1d0600 RTextureContextManager::~RTextureContextManager
  0x1d07b8 RTextureContextManager::EmergencyMemoryDeallocator
  0x1d1de0 RTextureContextManager::Get
  0x1d1df0 RTextureContextManager::Init
  0x1d1e28 RTextureContextManager::Kill
  0x1d1e60 RTextureContextManager::Reset

Sheet rows:
  RTextureContextManager::NewContext(char *, RTextureContextManag
  RTextureContextManager::KillContext(RTextureContext *)
  RTextureContextManager::GetContext(RTextureContextManager::Text
  RTextureContextManager::FindOrCreateTexture(unsigned int, RText
  RTextureContextManager::RTextureContextManager(void)
  RTextureContextManager::~RTextureContextManager(void)
  RTextureContextManager::EmergencyMemoryDeallocator(void)
  RTextureContextManager type_info function
  RTextureContextManager::Get(void)
  RTextureContextManager::Init(void)
  RTextureContextManager::Kill(void)
  RTextureContextManager::Reset(void)
  RTextureContextManager::fgThis_RTextureContextManager
  RTextureContextManager virtual table
  RTextureContextManager type_info node

Xbox methods treated as members (7 of 7; untyped ones count when ECX is read before it is written): FindOrCreateTexture, GetContext, Init, KillContext, NewContext, scalar_deleting_destructor, ~RTextureContextManager

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: ~RTextureContextManager@95290]
  +0x004  w[4] R -> FUN_00094d10 [4: FindOrCreateTexture@93fb0, KillContext@94c90, NewContext@951e0, ~RTextureContextManager@95290]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RTextureContextManager@1d0578, ~RTextureContextManager@1d0600]
  +0x004  w[4] R/W [5: FindOrCreateTexture@1d0458, KillContext@1cfe40, NewContext@1cfd80, RTextureContextManager@1d0578, ~RTextureContextManager@1d0600]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
