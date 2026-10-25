# RStateManager

FastAlloc/constructed sizes under its tag: None
Xbox vtable 0x0018f8f8 (1 slots) stored by its constructor
Xbox vtable 0x00192130 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RStateManager::USymbolTable::Namespace virtual table']
constructor 0x92f50 first calls: [None, '??_L@YGXPAXIHP6EX0@Z1@Z', '__builtin_new']

Xbox methods (5):
  0x92280 undefined ParseFirstStateTag(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x92d20 undefined GetGeoPrimState(undefined4 param_1)
  0x92f50 undefined RStateManager(void)
  0x930a0 undefined ~RStateManager(void)
  0x93160 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (12):
  0x1cdf08 RStateManager::RStateManager
  0x1ce038 RStateManager::~RStateManager
  0x1ce158 RStateManager::GetGeoPrimState
  0x1ce378 RStateManager::Debug
  0x1ce380 RStateManager::NameLookup
  0x1ce3b0 RStateManager::ParseFirstStateTag
  0x1cedb8 RStateManager::Get
  0x1cedc8 RStateManager::Init
  0x1cee00 RStateManager::Kill
  0x1cee38 RStateManager::Reset
  0x1cee80 RStateManager::fgThis_RStateManager_global_ctors
  0x1ceea0 RStateManager::fgThis_RStateManager_global_dtors

Sheet rows:
  RStateManager::RStateManager(void)
  RStateManager::~RStateManager(void)
  RStateManager::GetGeoPrimState(char *)
  RStateManager::Debug(void)
  RStateManager::NameLookup(char *, unsigned int &) const
  RStateManager::ParseFirstStateTag(char *, EAGL::GeoPrimState &,
  RStateManager type_info function
  RStateManager::Get(void)
  RStateManager::Init(void)
  RStateManager::Kill(void)
  RStateManager::Reset(void)
  RStateManager::fgThis_RStateManager
  RStateManager::USymbolTable::Namespace virtual table
  RStateManager virtual table
  RStateManager type_info node

Xbox methods treated as members (4 of 5; untyped ones count when ECX is read before it is written): GetGeoPrimState, RStateManager, scalar_deleting_destructor, ~RStateManager

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RStateManager@92f50, ~RStateManager@930a0]
  +0x004  w[4] W [2: RStateManager@92f50, ~RStateManager@930a0]
  +0x008  w[4] R/W [2: GetGeoPrimState@92d20, RStateManager@92f50]
  +0x00c  w[4] R/W -> MEM_fill [3: GetGeoPrimState@92d20, RStateManager@92f50, ~RStateManager@930a0]
  +0x010  w[4] R/W -> _Rb_tree<StateRef,_StateRef>::insert_unique [3: GetGeoPrimState@92d20, RStateManager@92f50, ~RStateManager@930a0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RStateManager@1cdf08, ~RStateManager@1ce038]
  +0x004  w[4] W [2: RStateManager@1cdf08, ~RStateManager@1ce038]
  +0x008  w[4] R/W [2: GetGeoPrimState@1ce158, RStateManager@1cdf08]
  +0x00c  w[4] R/W -> EAGL::GeoPrimState::~GeoPrimState [3: GetGeoPrimState@1ce158, RStateManager@1cdf08, ~RStateManager@1ce038]
  +0x010  w[4] R/W -> FUN_001ce6c0 [3: GetGeoPrimState@1ce158, RStateManager@1cdf08, ~RStateManager@1ce038]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
