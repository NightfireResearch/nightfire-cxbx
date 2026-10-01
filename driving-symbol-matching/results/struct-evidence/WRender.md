# WRender

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [224]}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xc7a50 first calls: ['UGroup::GroupLocateTag', 'UGroup::DataLocateFirst', 'UGroup::DataLocateFirst']

Xbox methods (13):
  0xc7330 undefined FindVisibleTreeNodesAndCurtains(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefine
  0xc7510 undefined GenerateCurtainsAndNodes(undefined4 param_1, undefined4 param_2)
  0xc75b0 undefined __cdecl PrepareForCull(undefined4 param_1)
  0xc75e0 undefined GetNextPoint(void)
  0xc7700 undefined SetCull(void)
  0xc7750 undefined CopyDrawPasses(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xc77f0 undefined SimpleDrawVisibleTreeNodes(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 pa
  0xc7a50 undefined WRender(void)
  0xc7ce0 undefined __stdcall Init(void)
  0xc7d50 undefined __stdcall ShutDown(void)
  0xc8370 undefined DrawPass(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xc8450 undefined DrawWorld(undefined4 param_1, undefined4 param_2)
  0xc8490 undefined DrawWorldAtPoint(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, und

PS2 methods (14):
  0x21eae8 WRender::WRender
  0x21eec8 WRender::~WRender
  0x21eef0 WRender::Init
  0x21ef30 WRender::ShutDown
  0x21ef68 WRender::FindVisibleTreeNodesAndCurtains
  0x21f1d8 WRender::GenerateCurtainsAndNodes
  0x21f288 WRender::PrepareForCull
  0x21f2b8 WRender::GetNextPoint
  0x21f570 WRender::SetCull
  0x21f5b8 WRender::CopyDrawPasses
  0x21f670 WRender::DrawPass
  0x21f868 WRender::DrawWorld
  0x21f8d0 WRender::SimpleDrawVisibleTreeNodes
  0x21fa58 WRender::DrawWorldAtPoint

Sheet rows:
  WRender::WRender(void)
  WRender::~WRender(void)
  WRender::Init(void)
  WRender::ShutDown(void)
  WRender::FindVisibleTreeNodesAndCurtains(CARP::MapNode &, COORD
  WRender::GenerateCurtainsAndNodes(RCamera &, float)
  WRender::PrepareForCull(CachedDrawInfo &)
  WRender::GetNextPoint(COORD4 &, float &, bool &, float &)
  WRender::SetCull(int, bool, float)
  WRender::CopyDrawPasses(CachedDrawInfo &, CachedDrawInfo &, uns
  WRender::DrawPass(CachedDrawInfo &, unsigned int, unsigned int,
  WRender::DrawWorld(CachedDrawInfo &, WRender::SortBy)
  WRender::SimpleDrawVisibleTreeNodes(CARP::MapNode &, COORD4 &,
  WRender::DrawWorldAtPoint(CachedDrawInfo &, COORD3 &, float, fl
  WRender::Debug(void)
  WRender::fgWRender
  WRender::fgCurrentCullCheckNode
  WRender::fgCurrentCullCheckInstance
  WRender::fgCachedDrawInfo
  WRender::fgCachedDrawInfoTempPtr

Xbox methods treated as members (10 of 13; untyped ones count when ECX is read before it is written): CopyDrawPasses, DrawPass, DrawWorld, DrawWorldAtPoint, FindVisibleTreeNodesAndCurtains, GenerateCurtainsAndNodes, GetNextPoint, Init, SimpleDrawVisibleTreeNodes, WRender

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: DrawWorld@c8450, DrawWorldAtPoint@c8490, WRender@c7a50]
  +0x010  w- LEA addr-taken [1: WRender@c7a50]
  +0x014  w[4] W [1: WRender@c7a50]
  +0x018  w- LEA addr-taken [1: WRender@c7a50]
  +0x028  w[4] W [1: WRender@c7a50]
  +0x02c  w[4] W [1: WRender@c7a50]
  +0x030  w[4] R/W [1: WRender@c7a50]
  +0x034  w[4] R/W [2: FindVisibleTreeNodesAndCurtains@c7330, WRender@c7a50]
  +0x0d4  w[4] R/W [2: FindVisibleTreeNodesAndCurtains@c7330, GenerateCurtainsAndNodes@c7510]
  +0x0d8  w[4] W [1: GenerateCurtainsAndNodes@c7510]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W -> RSky::Draw [4: DrawWorld@21f868, DrawWorldAtPoint@21fa58, GetNextPoint@21f2b8, WRender@21eae8]
  +0x004  w- LEA addr-taken [3: CopyDrawPasses@21f5b8, DrawPass@21f670, WRender@21eae8]
  +0x008  w[8] W [1: GetNextPoint@21f2b8]
  +0x00c  w[4] W float [1: GetNextPoint@21f2b8]
  +0x014  w[4] W [1: WRender@21eae8]
  +0x018  w- LEA addr-taken [3: CopyDrawPasses@21f5b8, DrawPass@21f670, WRender@21eae8]
  +0x028  w[4] W [1: WRender@21eae8]
  +0x02c  w[4] R/W [3: DrawPass@21f670, DrawWorldAtPoint@21fa58, WRender@21eae8]
  +0x030  w[4] R/W [1: WRender@21eae8]
  +0x034  w[4] R/W [2: FindVisibleTreeNodesAndCurtains@21ef68, WRender@21eae8]
  +0x0d4  w[4] R/W [2: FindVisibleTreeNodesAndCurtains@21ef68, GenerateCurtainsAndNodes@21f1d8]
  +0x0d8  w[4] W float [1: GenerateCurtainsAndNodes@21f1d8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
