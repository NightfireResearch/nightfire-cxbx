# RParticulate

FastAlloc/constructed sizes under its tag: {'allocated': [4208], 'constructed': []}
deleting destructor 0xa3fa0 frees/deletes with size 0x1070 (call to UMemory::FastFree)
Xbox vtable 0x00192e54 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RParticulate virtual table']
constructor 0xa3f30 first calls: ['USimpleMaterial::USimpleMaterial', 'RParticulate::_init']

Xbox methods (9):
  0x8bd60 undefined __stdcall Init(void)
  0xa3210 undefined LoadAttributes(void)
  0xa3240 undefined ~RParticulate(void)
  0xa3290 undefined __stdcall Kill(void)
  0xa32b0 undefined _init(void)
  0xa3e60 undefined __thiscall Draw(RParticulate * this)
  0xa3f30 undefined RParticulate(void)
  0xa3fa0 undefined scalar_deleting_destructor(undefined1 param_1)
  0xa3fd0 undefined __thiscall Update(RParticulate * this)

PS2 methods (16):
  0x1e91c0 RParticulate::RParticulate
  0x1e9220 RParticulate::~RParticulate
  0x1e9288 RParticulate::Reset
  0x1e9290 RParticulate::_init
  0x1e9628 RParticulate::_uninit
  0x1e9630 RParticulate::Update
  0x1e9b38 RParticulate::LoadAttributes
  0x1e9b78 RParticulate::Draw
  0x1e9fb8 RParticulate::RParticulate_type_info_function
  0x1ea030 RParticulate::Get
  0x1ea040 RParticulate::Init
  0x1ea078 RParticulate::Kill
  0x1ea0b0 RParticulate::operator_new
  0x1ea0d0 RParticulate::operator_delete
  0x1ea0f0 RParticulate::fgThis_RParticulate_global_ctors
  0x1ea110 RParticulate::fgThis_RParticulate_global_dtors

Sheet rows:
  RParticulate::RParticulate(void)
  RParticulate::~RParticulate(void)
  RParticulate::Reset(void)
  RParticulate::_init(void)
  RParticulate::_uninit(void)
  RParticulate::Update(void)
  RParticulate::LoadAttributes(void)
  RParticulate::Draw(void)
  RParticulate type_info function
  RParticulate::Get(void)
  RParticulate::Init(void)
  RParticulate::Kill(void)
  RParticulate::operator new(unsigned int)
  RParticulate::operator delete(void *, unsigned int)
  RParticulate::fgThis_RParticulate
  RParticulate virtual table
  RParticulate type_info node

Xbox methods treated as members (8 of 9; untyped ones count when ECX is read before it is written): Draw, Init, LoadAttributes, RParticulate, Update, _init, scalar_deleting_destructor, ~RParticulate

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RParticulate@a3f30, ~RParticulate@a3240]
  +0x010  w- LEA addr-taken [1: Draw@a3e60]
  +0x020  w- LEA addr-taken [1: _init@a32b0]
  +0xc90  w- LEA addr-taken [2: Draw@a3e60, RParticulate@a3f30]
  +0xc94  w- LEA addr-taken [1: _init@a32b0]
  +0xfb0  w[4] W float [2: Update@a3fd0, _init@a32b0]
  +0xfb4  w[4] W float [2: Update@a3fd0, _init@a32b0]
  +0xfc0  w[4] LEA/W float addr-taken [1: _init@a32b0]
  +0xfc4  w[4] W float [1: _init@a32b0]
  +0xfc8  w[4] W float [1: _init@a32b0]
  +0xfd0  w[4] W [1: _init@a32b0]
  +0xfd4  w[4] W [1: _init@a32b0]
  +0xfd8  w[4] W [1: _init@a32b0]
  +0xfdc  w[4] W float [2: Update@a3fd0, _init@a32b0]
  +0xfe0  w[4] W [1: _init@a32b0]
  +0xffc  w[1] R/W [3: Draw@a3e60, RParticulate@a3f30, Update@a3fd0]
  +0x1000  w[4] R/W [2: Draw@a3e60, _init@a32b0]
  +0x1004  w[4] R/W [2: Draw@a3e60, _init@a32b0]
  +0x1010  w- LEA addr-taken -> USimpleMaterial::USimpleMaterial, thunk_FUN_000ef490 [3: Draw@a3e60, RParticulate@a3f30, ~RParticulate@a3240]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RParticulate@1e91c0, ~RParticulate@1e9220]
  +0x010  w- LEA addr-taken [1: Update@1e9630]
  +0xc90  w- LEA addr-taken [2: RParticulate@1e91c0, Update@1e9630]
  +0xfb0  w[4] R/W float [2: Update@1e9630, _init@1e9290]
  +0xfb4  w[4] R/W float [2: Update@1e9630, _init@1e9290]
  +0xfc0  w[4, 8] R/W float [2: Update@1e9630, _init@1e9290]
  +0xfc4  w[4] R float [2: Update@1e9630, _init@1e9290]
  +0xfc8  w[4, 8] R/W float [2: Update@1e9630, _init@1e9290]
  +0xfd0  w[4] R/W float [2: Update@1e9630, _init@1e9290]
  +0xfd4  w[4] R/W float [2: Update@1e9630, _init@1e9290]
  +0xfd8  w[4] R/W float [2: Update@1e9630, _init@1e9290]
  +0xfdc  w[4] W float [1: _init@1e9290]
  +0xfe0  w[4] W float [1: _init@1e9290]
  +0xff0  w[4] R/W float [1: Update@1e9630]
  +0xff4  w[4] R/W float [1: Update@1e9630]
  +0xff8  w[4] R/W float [1: Update@1e9630]
  +0xffc  w[4] LEA/R/W addr-taken [4: Draw@1e9b78, LoadAttributes@1e9b38, RParticulate@1e91c0, Update@1e9630]
  +0x1000  w[4] R/W [2: Draw@1e9b78, _init@1e9290]
  +0x1004  w[4] R/W [2: Draw@1e9b78, _init@1e9290]
  +0x1010  w- LEA addr-taken [2: RParticulate@1e91c0, ~RParticulate@1e9220]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
