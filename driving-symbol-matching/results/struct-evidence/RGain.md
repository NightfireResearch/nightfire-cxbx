# RGain

FastAlloc/constructed sizes under its tag: {'allocated': [300], 'constructed': []}
deleting destructor 0x9e120 frees/deletes with size 0x12c (call to UMemory::FastFree)
Xbox vtable 0x00192bdc (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RGain virtual table']
constructor 0x9dfe0 first calls: ['EAGL::GeoPrimState::GeoPrimState', 'EAGL::DynamicModel::DynamicModel', 'FUN_0009dd40']

Xbox methods (7):
  0x8bcf0 undefined __stdcall Init(void)
  0x9de00 undefined ~RGain(void)
  0x9de80 undefined Kill(void)
  0x9dea0 undefined Reset(void)
  0x9dec0 undefined __thiscall Draw(RGain * this)
  0x9dfe0 undefined RGain(void)
  0x9e120 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (13):
  0x1df7c8 RGain::RGain
  0x1df800 RGain::~RGain
  0x1df830 RGain::Reset
  0x1df880 RGain::SetGain
  0x1df898 RGain::SetOffset
  0x1df8b0 RGain::Draw
  0x1df8b8 RGain::Debug
  0x1dfb78 RGain::Get
  0x1dfb88 RGain::Init
  0x1dfbc0 RGain::Kill
  0x1dfbf8 RGain::operator_new
  0x1dfc18 RGain::operator_delete
  0x1dfc38 RGain::fgThis_RGain_global_ctors

Sheet rows:
  RGain::RGain(void)
  RGain::~RGain(void)
  RGain::Reset(void)
  RGain::SetGain(float, float, float, float)
  RGain::SetOffset(float, float, float, float)
  RGain::Draw(void)
  RGain::Debug(void)
  RGain type_info function
  RGain::Get(void)
  RGain::Init(void)
  RGain::Kill(void)
  RGain::operator new(unsigned int)
  RGain::operator delete(void *, unsigned int)
  RGain::fgThis_RGain
  RGain virtual table
  RGain type_info node

Xbox methods treated as members (6 of 7; untyped ones count when ECX is read before it is written): Draw, Init, RGain, Reset, scalar_deleting_destructor, ~RGain

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RGain@9dfe0, ~RGain@9de00]
  +0x004  w[4] LEA/R/W addr-taken [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x008  w[4] R/W [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x00c  w[4] R/W [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x010  w[4] R/W [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x014  w[4] LEA/W float addr-taken [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x018  w[4] W float [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x01c  w[4] W float [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x020  w[4] W float [3: Draw@9dec0, RGain@9dfe0, Reset@9dea0]
  +0x024  w- LEA addr-taken -> FUN_000ef490 [2: RGain@9dfe0, ~RGain@9de00]
  +0x070  w- LEA addr-taken -> EAGL::DynamicModel::~DynamicModel [3: Draw@9dec0, RGain@9dfe0, ~RGain@9de00]
  +0x0c8  w[4] LEA/W addr-taken [2: Draw@9dec0, RGain@9dfe0]
  +0x0cc  w[4] W [1: RGain@9dfe0]
  +0x0d0  w[4] W [1: RGain@9dfe0]
  +0x0d4  w[4] W [1: RGain@9dfe0]
  +0x0d8  w[4] LEA/R addr-taken -> FUN_0009dd40, FUN_000f12c0 [2: RGain@9dfe0, ~RGain@9de00]
  +0x0dc  w[4] W [1: RGain@9dfe0]
  +0x0e0  w[4] W [1: RGain@9dfe0]
  +0x0e4  w[4] W [1: Draw@9dec0]
  +0x0e8  w[4] W [1: Draw@9dec0]
  +0x0ec  w[4] W [1: RGain@9dfe0]
  +0x0f0  w[4] W [1: RGain@9dfe0]
  +0x104  w[4] W [1: RGain@9dfe0]
  +0x108  w[4] W [1: RGain@9dfe0]
  +0x10c  w[4] W [1: RGain@9dfe0]
  +0x110  w[4] W [1: RGain@9dfe0]
  +0x114  w[4] W [1: Draw@9dec0]
  +0x118  w[4] W [1: Draw@9dec0]
  +0x11c  w[4] W [1: Draw@9dec0]
  +0x120  w[4] W [1: Draw@9dec0]
  +0x124  w[4] W [1: Draw@9dec0]
  +0x128  w[4] W [1: Draw@9dec0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RGain@1df7c8, ~RGain@1df800]
  +0x004  w[4] W float [1: SetGain@1df880]
  +0x008  w[4] W float [1: SetGain@1df880]
  +0x00c  w[4] W float [1: SetGain@1df880]
  +0x010  w[4] W float [1: SetGain@1df880]
  +0x014  w[4] W float [1: SetOffset@1df898]
  +0x018  w[4] W float [1: SetOffset@1df898]
  +0x01c  w[4] W float [1: SetOffset@1df898]
  +0x020  w[4] W float [1: SetOffset@1df898]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
