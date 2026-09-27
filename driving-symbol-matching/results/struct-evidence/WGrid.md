# WGrid

FastAlloc/constructed sizes under its tag: {'allocated': [36], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xc57b0 first calls: ['UMemory::Alloc']

Xbox methods (10):
  0xc5710 void __thiscall RangeCheckROWCOL(WGrid * this, uint * param_1, uint * param_2)
  0xc57b0 undefined WGrid(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xc59f0 undefined __stdcall Shutdown(void)
  0xc5fc0 undefined __cdecl Init(int param_1)
  0xc6290 undefined __stdcall Restart(void)
  0xc62b0 void __thiscall FindNodesBox(WGrid * this, uint * param_1, int param_2)
  0xc6450 void __thiscall FindNodes(WGrid * this, void * param_1_00, float * param_2, float param_3, int param_4)
  0xc64b0 undefined FindNodes(undefined4 param_1, undefined4 param_2)
  0xc6d70 undefined AddGridNodeDynamicElement(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 par
  0xc7200 undefined UpdateDynamicNodes(void)

PS2 methods (15):
  0x21c0f0 WGrid::WGrid
  0x21c1b0 WGrid::~WGrid
  0x21c1f8 WGrid::Init
  0x21c678 WGrid::Shutdown
  0x21c758 WGrid::Restart
  0x21c780 WGrid::UpdateDynamicNodes
  0x21cad0 WGrid::AddGridNodeDynamicElement
  0x21cf30 WGrid::FindNodes
  0x21cf80 WGrid::FindNodesBox
  0x21d298 WGrid::FindNodes
  0x21dd98 WGrid::FindNodes
  0x21ddf8 WGrid::FindNodes
  0x21e100 WGrid::RangeCheck
  0x21e610 WGrid::fgGrid_global_ctors
  0x21e630 WGrid::fgGrid_global_dtors

Sheet rows:
  WGrid::WGrid(COORD4 &, unsigned int, unsigned int, float)
  WGrid::~WGrid(void)
  WGrid::Init(UGroup *)
  WGrid::Shutdown(void)
  WGrid::Restart(void)
  WGrid::UpdateDynamicNodes(void)
  WGrid::AddGridNodeDynamicElement(COORD4 *, COORD4 *, WGridNode_
  WGrid::FindNodes(COORD3 &, float, vector<unsigned int, allocato
  WGrid::FindNodesBox(COORD4 *, vector<unsigned int, allocator<un
  WGrid::FindNodes(COORD4 *, vector<unsigned int, allocator<unsig
  WGrid::FindNodes(COORD4 &, COORD4 &, vector<unsigned int, alloc
  WGrid::FindNodes(COORD3 &, float, float, vector<unsigned int, a
  WGrid::RangeCheck(COORD3 *) const
  WGrid::fgGrid
  WGrid::fgMapGroup

Xbox methods treated as members (6 of 10; untyped ones count when ECX is read before it is written): FindNodes, FindNodesBox, RangeCheckROWCOL, Shutdown, WGrid

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W float [3: FindNodes@c64b0, RangeCheckROWCOL@c5710, WGrid@c57b0]
  +0x004  w[4] W [1: WGrid@c57b0]
  +0x008  w[4] W [1: WGrid@c57b0]
  +0x00c  w[4] W [1: WGrid@c57b0]
  +0x010  w[4] W [1: WGrid@c57b0]
  +0x014  w[4] W float [2: RangeCheckROWCOL@c5710, WGrid@c57b0]
  +0x018  w[4] R/W [2: FindNodesBox@c62b0, WGrid@c57b0]
  +0x01c  w[4] R/W [2: FindNodesBox@c62b0, WGrid@c57b0]
  +0x020  w[4] R/W [1: WGrid@c57b0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W float [5: FindNodes@21d298, FindNodes@21ddf8, FindNodesBox@21cf80, RangeCheck@21e100, WGrid@21c0f0]
  +0x008  w[4, 8] R/W float [5: FindNodes@21d298, FindNodes@21ddf8, FindNodesBox@21cf80, RangeCheck@21e100, WGrid@21c0f0]
  +0x010  w[4] R/W float [3: FindNodes@21d298, FindNodes@21ddf8, WGrid@21c0f0]
  +0x014  w[4] R/W float [5: FindNodes@21d298, FindNodes@21ddf8, FindNodesBox@21cf80, RangeCheck@21e100, WGrid@21c0f0]
  +0x018  w[4] R/W [5: FindNodes@21d298, FindNodes@21ddf8, FindNodesBox@21cf80, RangeCheck@21e100, WGrid@21c0f0]
  +0x01c  w[4] R/W [5: FindNodes@21d298, FindNodes@21ddf8, FindNodesBox@21cf80, RangeCheck@21e100, WGrid@21c0f0]
  +0x020  w[4] R/W -> UMemory::Free [2: WGrid@21c0f0, ~WGrid@21c1b0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
