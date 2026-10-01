# RSniperZoom

FastAlloc/constructed sizes under its tag: {'allocated': [3184], 'constructed': []}
deleting destructor 0xa6a50 frees/deletes with size 0xc70 (call to UMemory::FastFree)
Xbox vtable 0x00193008 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RSniperZoom virtual table']
constructor 0xa69a0 first calls: ['ROffscreenBuffer::ROffscreenBuffer', 'USimpleTexturedMaterial::USimpleTexturedMaterial', 'FUN_000eecf0']

Xbox methods (9):
  0x8be40 undefined __stdcall Init(void)
  0xa6810 undefined ~RSniperZoom(void)
  0xa6870 undefined __stdcall Kill(void)
  0xa6890 undefined Reset(void)
  0xa68a0 undefined __thiscall GrabBackBuffer(RSniperZoom * this)
  0xa69a0 undefined RSniperZoom(void)
  0xa6a50 undefined scalar_deleting_destructor(undefined1 param_1)
  0xa6a80 undefined SetupVerts(void)
  0xa6bc0 int __thiscall Draw(RSniperZoom * this)

PS2 methods (16):
  0x1ed480 RSniperZoom::RSniperZoom
  0x1ed570 RSniperZoom::~RSniperZoom
  0x1ed5e0 RSniperZoom::GrabBackBuffer
  0x1ed650 RSniperZoom::Draw
  0x1ed8d8 RSniperZoom::SetupVerts
  0x1edcc0 RSniperZoom::RSniperZoom_type_info_function
  0x1edd38 RSniperZoom::Get
  0x1edd48 RSniperZoom::Init
  0x1edd80 RSniperZoom::Kill
  0x1eddb8 RSniperZoom::operator_new
  0x1eddd8 RSniperZoom::operator_delete
  0x1eddf8 RSniperZoom::Reset
  0x1ede08 RSniperZoom::Enabled
  0x1ede10 RSniperZoom::Show
  0x1ede20 RSniperZoom::Hide
  0x1ede28 RSniperZoom::fgThis_RSniperZoom_global_ctors

Sheet rows:
  RSniperZoom::RSniperZoom(void)
  RSniperZoom::~RSniperZoom(void)
  RSniperZoom::GrabBackBuffer(void)
  RSniperZoom::Draw(void)
  RSniperZoom::SetupVerts(void)
  RSniperZoom type_info function
  RSniperZoom::Get(void)
  RSniperZoom::Init(void)
  RSniperZoom::Kill(void)
  RSniperZoom::operator new(unsigned int)
  RSniperZoom::operator delete(void *, unsigned int)
  RSniperZoom::Reset(void)
  RSniperZoom::Enabled(void)
  RSniperZoom::Show(unsigned int)
  RSniperZoom::Hide(void)
  RSniperZoom::fgThis_RSniperZoom
  RSniperZoom virtual table
  RSniperZoom type_info node

Xbox methods treated as members (8 of 9; untyped ones count when ECX is read before it is written): Draw, GrabBackBuffer, Init, RSniperZoom, Reset, SetupVerts, scalar_deleting_destructor, ~RSniperZoom

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RSniperZoom@a69a0, ~RSniperZoom@a6810]
  +0x004  w[1] R/W [4: Draw@a6bc0, GrabBackBuffer@a68a0, RSniperZoom@a69a0, Reset@a6890]
  +0x008  w[4] W [2: RSniperZoom@a69a0, Reset@a6890]
  +0x00c  w- LEA addr-taken -> ROffscreenBuffer::ROffscreenBuffer, ROffscreenBuffer::~ROffscreenBuffer [4: Draw@a6bc0, GrabBackBuffer@a68a0, RSniperZoom@a69a0, ~RSniperZoom@a6810]
  +0x018  w[4] R [1: Draw@a6bc0]
  +0x030  w- LEA addr-taken [2: Draw@a6bc0, SetupVerts@a6a80]
  +0x2d0  w- LEA addr-taken [1: Draw@a6bc0]
  +0x570  w- LEA addr-taken [1: Draw@a6bc0]
  +0x810  w- LEA addr-taken [1: Draw@a6bc0]
  +0xab0  w- LEA addr-taken [2: Draw@a6bc0, RSniperZoom@a69a0]
  +0xb60  w- LEA addr-taken [3: Draw@a6bc0, RSniperZoom@a69a0, SetupVerts@a6a80]
  +0xc10  w- LEA addr-taken -> thunk_FUN_000ef490 [3: Draw@a6bc0, RSniperZoom@a69a0, ~RSniperZoom@a6810]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RSniperZoom@1ed480, ~RSniperZoom@1ed570]
  +0x004  w[4] R/W [7: Draw@1ed650, Enabled@1ede08, GrabBackBuffer@1ed5e0, Hide@1ede20, RSniperZoom@1ed480, Reset@1eddf8…]
  +0x008  w[4] R/W [4: RSniperZoom@1ed480, Reset@1eddf8, SetupVerts@1ed8d8, Show@1ede10]
  +0x00c  w- LEA addr-taken [2: Draw@1ed650, ~RSniperZoom@1ed570]
  +0x030  w- LEA addr-taken [2: Draw@1ed650, SetupVerts@1ed8d8]
  +0x2d0  w- LEA addr-taken [2: Draw@1ed650, SetupVerts@1ed8d8]
  +0x570  w- LEA addr-taken [2: Draw@1ed650, SetupVerts@1ed8d8]
  +0x810  w- LEA addr-taken [2: Draw@1ed650, SetupVerts@1ed8d8]
  +0xab0  w- LEA addr-taken [2: Draw@1ed650, RSniperZoom@1ed480]
  +0xb60  w- LEA addr-taken [3: Draw@1ed650, RSniperZoom@1ed480, SetupVerts@1ed8d8]
  +0xc10  w- LEA addr-taken [2: Draw@1ed650, RSniperZoom@1ed480]
  +0xc40  w[8] R/W [1: RSniperZoom@1ed480]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
