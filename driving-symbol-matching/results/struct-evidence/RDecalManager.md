# RDecalManager

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [672]}
Xbox vtable 0x00192838 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RDecalManager virtual table']
constructor 0x9acf0 first calls: [None, 'FUN_0009ac40', 'RTextureContextManager::GetContext']

Xbox methods (8):
  0x8b7e0 undefined __stdcall Init(void)
  0x9acf0 undefined RDecalManager(void)
  0x9af50 undefined __stdcall Kill(void)
  0x9af70 undefined Reset(void)
  0x9b070 undefined AddDecal(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 
  0x9b2d0 undefined __thiscall DrawDecals(RDecalManager * this)
  0x9b3c0 undefined ~RDecalManager(void)
  0x9b450 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (11):
  0x1da030 RDecalManager::RDecalManager
  0x1da310 RDecalManager::~RDecalManager
  0x1da558 RDecalManager::Reset
  0x1da6c8 RDecalManager::AddDecal
  0x1da978 RDecalManager::DrawDecals
  0x1dadd8 RDecalManager::Get
  0x1dade8 RDecalManager::Init
  0x1dae20 RDecalManager::Kill
  0x1dae58 RDecalManager::ClearDecals
  0x1dae60 RDecalManager::fgThis_RDecalManager_global_ctors
  0x1dae80 RDecalManager::fgThis_RDecalManager_global_dtors

Sheet rows:
  RDecalManager::RDecalManager(void)
  RDecalManager::~RDecalManager(void)
  RDecalManager::Reset(void)
  RDecalManager::AddDecal(COORD4 &, COORD4 &, COORD4 &, DecalType
  RDecalManager::DrawDecals(void)
  RDecalManager type_info function
  RDecalManager::Get(void)
  RDecalManager::Init(void)
  RDecalManager::Kill(void)
  RDecalManager::ClearDecals(void)
  RDecalManager::fgThis_RDecalManager
  RDecalManager virtual table
  RDecalManager type_info node

Xbox methods treated as members (6 of 8; untyped ones count when ECX is read before it is written): AddDecal, DrawDecals, Init, RDecalManager, scalar_deleting_destructor, ~RDecalManager

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RDecalManager@9acf0, ~RDecalManager@9b3c0]
  +0x294  w[4] R/W [2: AddDecal@9b070, RDecalManager@9acf0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RDecalManager@1da030, ~RDecalManager@1da310]
  +0x010  w- LEA addr-taken [1: RDecalManager@1da030]
  +0x054  w- LEA addr-taken [1: AddDecal@1da6c8]
  +0x290  w[4] W [1: ClearDecals@1dae58]
  +0x294  w[4] R/W [2: AddDecal@1da6c8, RDecalManager@1da030]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
