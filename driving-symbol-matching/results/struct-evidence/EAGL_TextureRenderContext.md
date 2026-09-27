# EAGL::TextureRenderContext

FastAlloc/constructed sizes under its tag: {'allocated': [212], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xf3450 first calls: ['EAGL::TextureRenderContextExtension::TextureRenderContextExtension', 'EAGLInternal::TextureRenderContextPrivate::TextureRenderContextPrivate']

Xbox methods (9):
  0xee080 undefined GetCurrentViewPort(void)
  0xf3450 undefined TextureRenderContext(undefined4 param_1)
  0xf34b0 undefined NewViewPort(void)
  0xf3540 undefined DeleteViewPort(undefined4 param_1)
  0xf35a0 undefined ~TextureRenderContext(void)
  0xf3600 undefined BeginFrame(void)
  0xf3620 undefined EndFrame(void)
  0xf3640 undefined GetSize(undefined4 param_1, undefined4 param_2)
  0xf36e0 undefined SetupFrameBuffers(undefined4 param_1, undefined4 param_2)

PS2 methods (13):
  0x291858 EAGL::TextureRenderContext::BeginFrame
  0x2919b0 EAGL::TextureRenderContext::EndFrame
  0x291af0 EAGL::TextureRenderContext::SetupFrameBuffers
  0x2921d8 EAGL::TextureRenderContext::GetSize
  0x2921f8 EAGL::TextureRenderContext::GetBufferDepth
  0x292200 EAGL::TextureRenderContext::GetZBufferDepth
  0x292208 EAGL::TextureRenderContext::SetZWritesEnable
  0x292230 EAGL::TextureRenderContext::GetZWritesEnable
  0x292358 EAGL::TextureRenderContext::TextureRenderContext
  0x2923a0 EAGL::TextureRenderContext::~TextureRenderContext
  0x292418 EAGL::TextureRenderContext::NewViewPort
  0x292470 EAGL::TextureRenderContext::DeleteViewPort
  0x2924e8 EAGL::TextureRenderContext::GetCurrentViewPort

Sheet rows:
  EAGL::TextureRenderContext::BeginFrame(void)
  EAGL::TextureRenderContext::EndFrame(void)
  EAGL::TextureRenderContext::SetupFrameBuffers(EAGL::TAR *, EAGL
  EAGL::TextureRenderContext::GetSize(float &, float &) const
  EAGL::TextureRenderContext::GetBufferDepth(void) const
  EAGL::TextureRenderContext::GetZBufferDepth(void) const
  EAGL::TextureRenderContext::SetZWritesEnable(bool)
  EAGL::TextureRenderContext::GetZWritesEnable(bool &) const
  EAGL::TextureRenderContext::TextureRenderContext(EAGL::Device *
  EAGL::TextureRenderContext::~TextureRenderContext(void)
  EAGL::TextureRenderContext::NewViewPort(void)
  EAGL::TextureRenderContext::DeleteViewPort(EAGL::ViewPort *)
  EAGL::TextureRenderContext::GetCurrentViewPort(void) const

Xbox methods treated as members (9 of 9; untyped ones count when ECX is read before it is written): BeginFrame, DeleteViewPort, EndFrame, GetCurrentViewPort, GetSize, NewViewPort, SetupFrameBuffers, TextureRenderContext, ~TextureRenderContext

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w- LEA addr-taken -> EAGLInternal::TextureRenderContextPrivate::TextureRenderContextPrivate [1: TextureRenderContext@f3450]
  +0x010  w[4] W float [2: GetSize@f3640, SetupFrameBuffers@f36e0]
  +0x014  w[4] W float [2: GetSize@f3640, SetupFrameBuffers@f36e0]
  +0x01c  w[4] W [1: SetupFrameBuffers@f36e0]
  +0x020  w[4] W [1: SetupFrameBuffers@f36e0]
  +0x028  w[4] R/W [3: DeleteViewPort@f3540, NewViewPort@f34b0, ~TextureRenderContext@f35a0]
  +0x034  w[4] W [1: SetupFrameBuffers@f36e0]
  +0x038  w[4] W [1: SetupFrameBuffers@f36e0]
  +0x084  w[4] W [2: BeginFrame@f3600, EndFrame@f3620]
  +0x0d0  w[4] W [1: TextureRenderContext@f3450]
  +0x138  w[4] R [1: GetCurrentViewPort@ee080]

PS2 this-relative accesses (PS2 offsets):
  +0x008  w- LEA addr-taken [1: TextureRenderContext@292358]
  +0x00c  w[4] W [1: SetupFrameBuffers@291af0]
  +0x010  w[4] W [1: SetupFrameBuffers@291af0]
  +0x014  w[4] R/W float [2: GetSize@2921d8, SetupFrameBuffers@291af0]
  +0x018  w[4] R/W float [2: GetSize@2921d8, SetupFrameBuffers@291af0]
  +0x01c  w[4] W [1: SetupFrameBuffers@291af0]
  +0x020  w[4] R/W [1: SetupFrameBuffers@291af0]
  +0x024  w[4] W [1: SetupFrameBuffers@291af0]
  +0x028  w[4] W [1: SetupFrameBuffers@291af0]
  +0x02c  w[4] R/W [1: SetupFrameBuffers@291af0]
  +0x030  w[4] R/W [2: GetBufferDepth@2921f8, SetupFrameBuffers@291af0]
  +0x034  w[4] R/W [2: GetZBufferDepth@292200, SetupFrameBuffers@291af0]
  +0x040  w[4] R [1: GetCurrentViewPort@2924e8]
  +0x044  w[4] R/W [3: DeleteViewPort@292470, NewViewPort@292418, ~TextureRenderContext@2923a0]
  +0x050  w- LEA addr-taken [2: BeginFrame@291858, EndFrame@2919b0]
  +0x060  w[8] R [1: EndFrame@2919b0]
  +0x080  w[8] R [1: EndFrame@2919b0]
  +0x0e0  w[4] W [2: BeginFrame@291858, EndFrame@2919b0]
  +0x0e8  w- LEA addr-taken [2: BeginFrame@291858, SetupFrameBuffers@291af0]
  +0x0f8  w[8] R [1: BeginFrame@291858]
  +0x118  w[8] R/W [2: BeginFrame@291858, SetupFrameBuffers@291af0]
  +0x128  w[8] W [1: SetupFrameBuffers@291af0]
  +0x178  w[4] R [1: GetZWritesEnable@292230]
  +0x180  w[4] W [1: TextureRenderContext@292358]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetCurrentViewPort: R +0x138 w4
