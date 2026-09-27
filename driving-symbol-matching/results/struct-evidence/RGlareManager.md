# RGlareManager

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [13376]}
Xbox vtable 0x001931e0 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RGlareManager virtual table']
constructor 0xa9c70 first calls: ['RTextureContextManager::GetContext', 'RTextureContext::FindOrCreateTexture', 'EAGL::GeoPrimState::SetPrimitiveType']

Xbox methods (8):
  0x8b850 undefined __stdcall Init(void)
  0xa9870 void __thiscall CreateUVsFromTexIDs(RGlareManager * this)
  0xa9aa0 void __thiscall AddModelGlare(RGlareManager * this, Glare * node, MATRIX4 * transform, float distance)
  0xa9c10 void __thiscall AddGlare(RGlareManager * this, Glare * glare, int unused)
  0xa9c70 RGlareManager * __thiscall RGlareManager(RGlareManager * this)
  0xa9eb0 undefined __stdcall Kill(void)
  0xa9ed0 undefined Reset(void)
  0xaa5d0 undefined __thiscall DrawGlares(RGlareManager * this, undefined1 param_1)

PS2 methods (12):
  0x1f14b0 RGlareManager::RGlareManager
  0x1f19c0 RGlareManager::~RGlareManager
  0x1f19f0 RGlareManager::CreateUVsFromTexIDs
  0x1f1f88 RGlareManager::AddGlare
  0x1f2058 RGlareManager::DrawGlares
  0x1f2888 RGlareManager::RGlareManager_type_info_function
  0x1f2900 RGlareManager::Get
  0x1f2910 RGlareManager::Init
  0x1f2948 RGlareManager::Kill
  0x1f2980 RGlareManager::ClearGlares
  0x1f2988 RGlareManager::EnableGlares
  0x1f2990 RGlareManager::Reset

Sheet rows:
  RGlareManager::RGlareManager(void)
  RGlareManager::~RGlareManager(void)
  RGlareManager::CreateUVsFromTexIDs(void)
  RGlareManager::AddGlare(CARP::GlareData &, MATRIX4 &, float)
  RGlareManager::AddGlare(CARP::GlareData &, float)
  RGlareManager::DrawGlares(bool)
  RGlareManager::DrawGlaresReflected(void)
  RGlareManager type_info function
  RGlareManager::Get(void)
  RGlareManager::Init(void)
  RGlareManager::Kill(void)
  RGlareManager::ClearGlares(void)
  RGlareManager::EnableGlares(bool)
  RGlareManager::Reset(void)
  RGlareManager::fgThis_RGlareManager
  RGlareManager virtual table
  RGlareManager type_info node

Xbox methods treated as members (7 of 8; untyped ones count when ECX is read before it is written): AddGlare, AddModelGlare, CreateUVsFromTexIDs, DrawGlares, Init, RGlareManager, Reset

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [1: RGlareManager@a9c70]
  +0x014  w[4] W float [1: DrawGlares@aa5d0]
  +0x018  w[4] W [1: DrawGlares@aa5d0]
  +0x030  w- LEA addr-taken [1: DrawGlares@aa5d0]
  +0x2020  w[4] R/W [5: AddGlare@a9c10, AddModelGlare@a9aa0, DrawGlares@aa5d0, RGlareManager@a9c70, Reset@a9ed0]
  +0x2024  w[1] R/W [3: AddGlare@a9c10, AddModelGlare@a9aa0, RGlareManager@a9c70]
  +0x2044  w- LEA addr-taken -> dbattrib_float [1: RGlareManager@a9c70]
  +0x2054  w- LEA addr-taken [1: RGlareManager@a9c70]
  +0x2058  w- LEA addr-taken [1: RGlareManager@a9c70]
  +0x205c  w- LEA addr-taken [1: RGlareManager@a9c70]
  +0x2060  w- LEA addr-taken [1: RGlareManager@a9c70]
  +0x2064  w- LEA addr-taken -> dbattrib_float [1: RGlareManager@a9c70]
  +0x2068  w- LEA addr-taken [2: CreateUVsFromTexIDs@a9870, RGlareManager@a9c70]
  +0x206c  w- LEA addr-taken -> dbattrib_u8 [1: RGlareManager@a9c70]
  +0x2070  w- LEA addr-taken [1: RGlareManager@a9c70]
  +0x28b0  w[4] W [1: RGlareManager@a9c70]
  +0x28f0  w[4] W [1: RGlareManager@a9c70]
  +0x2d70  w[4] W [1: RGlareManager@a9c70]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RGlareManager@1f14b0, ~RGlareManager@1f19c0]
  +0x010  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x020  w- LEA addr-taken [1: DrawGlares@1f2058]
  +0x03c  w- LEA addr-taken [1: DrawGlares@1f2058]
  +0x2020  w[4] R/W [5: AddGlare@1f1f88, ClearGlares@1f2980, DrawGlares@1f2058, RGlareManager@1f14b0, Reset@1f2990]
  +0x2024  w[4] R/W [3: AddGlare@1f1f88, EnableGlares@1f2988, RGlareManager@1f14b0]
  +0x2044  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x2054  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x2058  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x205c  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x2060  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x2064  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x206c  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x2070  w- LEA addr-taken [1: RGlareManager@1f14b0]
  +0x28b0  w[4] W float [1: RGlareManager@1f14b0]
  +0x28f0  w[4] W float [1: RGlareManager@1f14b0]
  +0x2d70  w[4] W float [1: RGlareManager@1f14b0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Reset: W +0x2020 w4
