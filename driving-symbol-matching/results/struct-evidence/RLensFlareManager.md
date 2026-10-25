# RLensFlareManager

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [8]}
Xbox vtable 0x00192bf4 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RLensFlareManager virtual table']
constructor 0x9e460 first calls: ['__builtin_new', 'RTextureContextManager::FindOrCreateTexture', 'RTextureContextManager::GetContext']

Xbox methods (10):
  0x8b8c0 undefined __stdcall Init(void)
  0x9e150 undefined Reset(void)
  0x9e1b0 undefined ~RLensFlareManager(void)
  0x9e1d0 undefined Kill(void)
  0x9e1f0 undefined Enable(undefined1 param_1)
  0x9e200 undefined EndFrame(void)
  0x9e460 undefined RLensFlareManager(void)
  0x9e520 undefined scalar_deleting_destructor(undefined1 param_1)
  0x9e540 undefined __thiscall DrawFlares(RLensFlareManager * this)
  0x9e720 undefined __thiscall TestFlares(RLensFlareManager * this)

PS2 methods (12):
  0x1dfc58 RLensFlareManager::RLensFlareManager
  0x1dfcf8 RLensFlareManager::Reset
  0x1dfd38 RLensFlareManager::~RLensFlareManager
  0x1dfd98 RLensFlareManager::Enable
  0x1dfda8 RLensFlareManager::DrawFlares
  0x1dfff0 RLensFlareManager::EndFrame
  0x1e0030 RLensFlareManager::TestFlares
  0x1e0358 RLensFlareManager::AddFlare
  0x1e0860 RLensFlareManager::Get
  0x1e0870 RLensFlareManager::Init
  0x1e08a8 RLensFlareManager::Kill
  0x1e08e0 RLensFlareManager::fgThis_RLensFlareManager_global_ctors

Sheet rows:
  RLensFlareManager::RLensFlareManager(void)
  RLensFlareManager::Reset(void)
  RLensFlareManager::~RLensFlareManager(void)
  RLensFlareManager::Enable(bool)
  RLensFlareManager::DrawFlares(void)
  RLensFlareManager::EndFrame(void)
  RLensFlareManager::TestFlares(RViewCamera &)
  RLensFlareManager::AddFlare(COORD4 &, RLensFlareManager::FlareT
  RLensFlareManager type_info function
  RLensFlareManager::Get(void)
  RLensFlareManager::Init(void)
  RLensFlareManager::Kill(void)
  RLensFlareManager::fgThis_RLensFlareManager
  RLensFlareManager virtual table
  RLensFlareManager type_info node

Xbox methods treated as members (9 of 10; untyped ones count when ECX is read before it is written): DrawFlares, Enable, EndFrame, Init, RLensFlareManager, Reset, TestFlares, scalar_deleting_destructor, ~RLensFlareManager

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RLensFlareManager@9e460, ~RLensFlareManager@9e1b0]
  +0x004  w[4] R/W [7: DrawFlares@9e540, Enable@9e1f0, EndFrame@9e200, RLensFlareManager@9e460, Reset@9e150, TestFlares@9e720…]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RLensFlareManager@1dfc58, ~RLensFlareManager@1dfd38]
  +0x004  w[4] R/W -> RGlareManager::DrawGlares [8: AddFlare@1e0358, DrawFlares@1dfda8, Enable@1dfd98, EndFrame@1dfff0, RLensFlareManager@1dfc58, Reset@1dfcf8…]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Enable: R +0x4 w4
