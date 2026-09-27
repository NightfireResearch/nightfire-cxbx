# RPostProcessing

FastAlloc/constructed sizes under its tag: {'allocated': [8], 'constructed': []}
deleting destructor 0xa5060 frees/deletes with size 0x8 (call to UMemory::FastFree)
Xbox vtable 0x00192ec4 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RPostProcessing virtual table']
constructor 0xa49a0 first calls: ['__builtin_new', 'FUN_000a4630', 'RTextureContextManager::GetContext']

Xbox methods (9):
  0x8bc80 undefined __stdcall Init(void)
  0xa4550 undefined Reset(void)
  0xa4560 undefined GetSubsampledBackBuffer(void)
  0xa49a0 undefined RPostProcessing(void)
  0xa4eb0 undefined __stdcall Kill(void)
  0xa4ed0 undefined __thiscall Draw(RPostProcessing * this)
  0xa4f70 undefined ~RPostProcessing(void)
  0xa5020 undefined __thiscall GrabBackBuffer(RPostProcessing * this)
  0xa5060 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (15):
  0x1ea130 RPostProcessing::RPostProcessing
  0x1ea328 RPostProcessing::~RPostProcessing
  0x1ea3f8 RPostProcessing::Reset
  0x1ea408 RPostProcessing::GetSubsampledBackBuffer
  0x1ea430 RPostProcessing::GetSubsampledFrontBuffer
  0x1ea460 RPostProcessing::Draw
  0x1ea468 RPostProcessing::Debug
  0x1ea470 RPostProcessing::GrabBackBuffer
  0x1ea790 RPostProcessing::RPostProcessing_type_info_function
  0x1ea808 RPostProcessing::Get
  0x1ea818 RPostProcessing::Init
  0x1ea850 RPostProcessing::Kill
  0x1ea888 RPostProcessing::operator_new
  0x1ea8a8 RPostProcessing::operator_delete
  0x1ea8c8 RPostProcessing::fgThis_RPostProcessing_global_ctors

Sheet rows:
  RPostProcessing::RPostProcessing(void)
  RPostProcessing::~RPostProcessing(void)
  RPostProcessing::Reset(void)
  RPostProcessing::GetSubsampledBackBuffer(void)
  RPostProcessing::GetSubsampledFrontBuffer(void)
  RPostProcessing::PPPrivateData::DrawBlurPoly(float, float)
  RPostProcessing::Draw(void)
  RPostProcessing::Debug(void)
  RPostProcessing::GrabBackBuffer(void)
  RPostProcessing::PPPrivateData::ExtractAlphaInfo(void)
  RPostProcessing type_info function
  RPostProcessing::Get(void)
  RPostProcessing::Init(void)
  RPostProcessing::Kill(void)
  RPostProcessing::operator new(unsigned int)
  RPostProcessing::operator delete(void *, unsigned int)
  RPostProcessing::fgThis_RPostProcessing
  RPostProcessing virtual table
  RPostProcessing type_info node

Xbox methods treated as members (8 of 9; untyped ones count when ECX is read before it is written): Draw, GetSubsampledBackBuffer, GrabBackBuffer, Init, RPostProcessing, Reset, scalar_deleting_destructor, ~RPostProcessing

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RPostProcessing@a49a0, ~RPostProcessing@a4f70]
  +0x004  w[4] R/W -> FUN_000a4690, FUN_000a4ee0, __builtin_new [6: Draw@a4ed0, GetSubsampledBackBuffer@a4560, GrabBackBuffer@a5020, RPostProcessing@a49a0, Reset@a4550, ~RPostProcessing@a4f70]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W [2: RPostProcessing@1ea130, ~RPostProcessing@1ea328]
  +0x004  w[4] R/W -> EAGL::GeoPrimState::SetDepthTestMethod, EAGL::GeoPrimState::SetPrimitiveType [6: GetSubsampledBackBuffer@1ea408, GetSubsampledFrontBuffer@1ea430, GrabBackBuffer@1ea470, RPostProcessing@1ea130, Reset@1ea3f8, ~RPostProcessing@1ea328]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  Reset: R +0x4 w4
  Draw: R +0x4 w4
