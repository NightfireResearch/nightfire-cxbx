# RRenderer

FastAlloc/constructed sizes under its tag: {'allocated': [144], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0x7d2a0 first calls: ['RDrawGroup::RDrawGroup', 'RDrawGroup::RDrawGroup', 'CPU_detect']

Xbox methods (14):
  0x7cfb0 void __thiscall ConfigureRes(RRenderer * this)
  0x7d020 undefined FlushDrawLists(void)
  0x7d030 undefined EndView(void)
  0x7d060 undefined StartFrame(void)
  0x7d070 undefined Flush(undefined1 param_1)
  0x7d0b0 bool __thiscall EnableAlphaWrites(RRenderer * this)
  0x7d0e0 undefined DisableAlphaWrites(void)
  0x7d110 undefined DoScreenCapture(void)
  0x7d2a0 RRenderer * __thiscall RRenderer(RRenderer * this, char * a, int b, bool c)
  0x7d560 void __fastcall LoadDebugFont(RRenderer * this)
  0x7d5b0 void __thiscall EndFrame(RRenderer * this)
  0x7d5e0 void __cdecl Init(char * a, int b, bool c)
  0x7d670 void __thiscall ~RRenderer(RRenderer * this)
  0x7d740 undefined __stdcall Shutdown(void)

PS2 methods (25):
  0x1ab910 RRenderer::Init
  0x1ab988 RRenderer::Shutdown
  0x1ab9c0 RRenderer::RRenderer
  0x1abd58 RRenderer::LoadDebugFont
  0x1abdd8 RRenderer::~RRenderer
  0x1abee0 RRenderer::ConfigureRes
  0x1abf80 RRenderer::CalculateCompensatedDepthBias
  0x1abf98 RRenderer::BeginView
  0x1abfa0 RRenderer::FlushDrawLists
  0x1abfc0 RRenderer::ClearDrawLists
  0x1abfe8 RRenderer::EndView
  0x1abff8 RRenderer::EnterReflection
  0x1ac010 RRenderer::BufferName
  0x1ac048 RRenderer::EAGL_allocator
  0x1ac068 RRenderer::EAGL_deallocator
  0x1ac088 RRenderer::ReverseBackfaceCulling
  0x1ac090 RRenderer::Configure
  0x1ac098 RRenderer::ConfigureIndex
  0x1ac0a0 RRenderer::StartFrame
  0x1ac0c0 RRenderer::EndFrame
  0x1ac118 RRenderer::Flush
  0x1ac178 RRenderer::EnableAlphaWrites
  0x1ac1b8 RRenderer::DisableAlphaWrites
  0x1ac1f8 RRenderer::DoScreenCapture
  0x1ac648 RRenderer::fgRenderer_global_ctors

Sheet rows:
  RRenderer::Init(char *, int, bool)
  RRenderer::Shutdown(void)
  RRenderer::RRenderer(char *, int, bool)
  RRenderer::LoadDebugFont(void)
  RRenderer::~RRenderer(void)
  RRenderer::ConfigureRes(void)
  RRenderer::CalculateCompensatedDepthBias(float, float)
  RRenderer::BeginView(RViewCamera &)
  RRenderer::FlushDrawLists(void)
  RRenderer::ClearDrawLists(void)
  RRenderer::FlushReflectedDrawLists(void)
  RRenderer::EndView(void)
  RRenderer::EnterReflection(void)
  RRenderer::LeaveReflection(void)
  RRenderer::CaptureBuffers(unsigned int, unsigned int)
  RRenderer::VerifyBuffers(void)
  RRenderer::BufferAllocCatch(void *, unsigned int)
  RRenderer::BufferName(unsigned int)
  RRenderer::EAGL_allocator(unsigned int, char *)
  RRenderer::EAGL_deallocator(void *, unsigned int)
  RRenderer::ReverseBackfaceCulling(void)
  RRenderer::Configure(int, int, int, int)
  RRenderer::ConfigureIndex(int)
  RRenderer::StartFrame(void)
  RRenderer::EndFrame(void)
  RRenderer::Flush(bool)
  RRenderer::EnableAlphaWrites(void)
  RRenderer::DisableAlphaWrites(void)
  RRenderer::DoScreenCapture(void)
  RRenderer::fgRenderer
  RRenderer::fMemClass

Xbox methods treated as members (13 of 14; untyped ones count when ECX is read before it is written): ConfigureRes, DisableAlphaWrites, DoScreenCapture, EnableAlphaWrites, EndFrame, EndView, Flush, FlushDrawLists, Init, LoadDebugFont, RRenderer, StartFrame, ~RRenderer

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R/W [2: EndView@7d030, RRenderer@7d2a0]
  +0x008  w- LEA addr-taken -> RDrawGroup::~RDrawGroup [2: RRenderer@7d2a0, ~RRenderer@7d670]
  +0x00c  w- LEA addr-taken -> RDrawGroup::RDrawGroup, RDrawGroup::~RDrawGroup [2: RRenderer@7d2a0, ~RRenderer@7d670]
  +0x010  w[4] R/W [2: FlushDrawLists@7d020, RRenderer@7d2a0]
  +0x014  w[4] W [1: RRenderer@7d2a0]
  +0x018  w[4] W [1: RRenderer@7d2a0]
  +0x01c  w[1] W [1: RRenderer@7d2a0]
  +0x01d  w[1] R/W [2: EndFrame@7d5b0, RRenderer@7d2a0]
  +0x01e  w[1] R/W [2: DisableAlphaWrites@7d0e0, EnableAlphaWrites@7d0b0]
  +0x020  w[4] W [1: RRenderer@7d2a0]
  +0x024  w[4] RW/W [2: EndFrame@7d5b0, RRenderer@7d2a0]
  +0x040  w[4] R/W float [2: ConfigureRes@7cfb0, RRenderer@7d2a0]
  +0x044  w[4] R/W float [2: ConfigureRes@7cfb0, RRenderer@7d2a0]
  +0x048  w[4] R/W [2: ConfigureRes@7cfb0, RRenderer@7d2a0]
  +0x04c  w[1] W [2: ConfigureRes@7cfb0, RRenderer@7d2a0]
  +0x04d  w[1] R/W [2: ConfigureRes@7cfb0, RRenderer@7d2a0]
  +0x050  w[4] W [2: ConfigureRes@7cfb0, RRenderer@7d2a0]
  +0x054  w[4] W [2: ConfigureRes@7cfb0, RRenderer@7d2a0]
  +0x058  w[4] W [1: RRenderer@7d2a0]
  +0x05c  w[4] W [1: RRenderer@7d2a0]
  +0x060  w[4] R/W -> EAGL::Device::DeleteRenderContext, EAGL::Device::NewRenderContext, EAGL::Device::~Device [2: RRenderer@7d2a0, ~RRenderer@7d670]
  +0x064  w[4] R/W -> EAGL::RenderContext::BeginFrame, EAGL::RenderContext::DeleteViewPort, EAGL::RenderContext::EndFrame, EAGL::RenderContext [6: DoScreenCapture@7d110, EndFrame@7d5b0, Flush@7d070, RRenderer@7d2a0, StartFrame@7d060, ~RRenderer@7d670]
  +0x068  w[4] R/W -> EAGL::ViewPort::SetViewMatrix, FUN_000e4870 [2: RRenderer@7d2a0, ~RRenderer@7d670]
  +0x06c  w[4] R/W [3: LoadDebugFont@7d560, RRenderer@7d2a0, ~RRenderer@7d670]
  +0x070  w[4] R/W -> MEM_free [2: LoadDebugFont@7d560, ~RRenderer@7d670]
  +0x074  w[1] R/W [3: EndFrame@7d5b0, Flush@7d070, RRenderer@7d2a0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] W float [1: CalculateCompensatedDepthBias@1abf80]
  +0x004  w[4] R/W [3: BeginView@1abf98, EndView@1abfe8, RRenderer@1ab9c0]
  +0x008  w- LEA addr-taken [1: RRenderer@1ab9c0]
  +0x00c  w- LEA addr-taken [2: EnterReflection@1abff8, RRenderer@1ab9c0]
  +0x010  w[4] W [2: EnterReflection@1abff8, RRenderer@1ab9c0]
  +0x018  w[8] W [1: RRenderer@1ab9c0]
  +0x020  w[8] W [1: RRenderer@1ab9c0]
  +0x028  w[4] W [1: RRenderer@1ab9c0]
  +0x02c  w[4] R/W [2: EndFrame@1ac0c0, RRenderer@1ab9c0]
  +0x030  w[4] R/W [2: DisableAlphaWrites@1ac1b8, EnableAlphaWrites@1ac178]
  +0x034  w[4] R/W [2: EnterReflection@1abff8, RRenderer@1ab9c0]
  +0x038  w[4] R/W [2: EndFrame@1ac0c0, RRenderer@1ab9c0]
  +0x050  w[4] R/W float -> FeatureManager::Init [2: ConfigureRes@1abee0, RRenderer@1ab9c0]
  +0x054  w[4] R/W float [2: ConfigureRes@1abee0, RRenderer@1ab9c0]
  +0x058  w[4] W [2: ConfigureRes@1abee0, RRenderer@1ab9c0]
  +0x05c  w[4] W [2: ConfigureRes@1abee0, RRenderer@1ab9c0]
  +0x060  w[4] W [2: ConfigureRes@1abee0, RRenderer@1ab9c0]
  +0x064  w[4] W float [2: ConfigureRes@1abee0, RRenderer@1ab9c0]
  +0x068  w[4] W float [2: ConfigureRes@1abee0, RRenderer@1ab9c0]
  +0x06c  w[4] W float [1: RRenderer@1ab9c0]
  +0x070  w[4] W [1: RRenderer@1ab9c0]
  +0x074  w[4] R/W -> EAGL::Device::Init, EAGL::Device::NewRenderContext, EAGL::DeviceExtension::SetDMABufferLength [1: RRenderer@1ab9c0]
  +0x078  w[4] R/W -> EAGL::RenderContext::BeginFrame, EAGL::RenderContext::EndFrame, EAGL::RenderContext::GetSize, EAGL::RenderContext::SetBa [4: DoScreenCapture@1ac1f8, EndFrame@1ac0c0, Flush@1ac118, RRenderer@1ab9c0]
  +0x07c  w[4] R/W -> EAGL::ViewPort::SetViewMatrix [1: RRenderer@1ab9c0]
  +0x080  w[4] R/W [2: LoadDebugFont@1abd58, RRenderer@1ab9c0]
  +0x084  w[4] R/W [1: LoadDebugFont@1abd58]
  +0x088  w[4] R/W [3: EndFrame@1ac0c0, Flush@1ac118, RRenderer@1ab9c0]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  FlushDrawLists: R +0x10 w4
  EndView: R +0x4 w4
  StartFrame: R +0x64 w4
