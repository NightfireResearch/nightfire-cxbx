# EAGL::RenderContext

FastAlloc/constructed sizes under its tag: {'allocated': [332], 'constructed': []}
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xe8740 first calls: []

Xbox methods (16):
  0xe6610 undefined BeginFrame(void)
  0xe6640 undefined EndFrame(void)
  0xe6a60 undefined SetSize(undefined4 param_1, undefined4 param_2)
  0xe6a80 undefined GetSize(undefined4 param_1, undefined4 param_2)
  0xe6aa0 undefined SetFrontBufferDepth(void)
  0xe6ac0 undefined SetBackBufferDepth(undefined4 param_1)
  0xe6b10 undefined SetZBufferDepth(undefined4 param_1)
  0xe6b60 undefined SetSyncToVBL(undefined1 param_1)
  0xe6c00 undefined SetupFrameBuffers(void)
  0xe73f0 undefined1 __thiscall SetZWritesEnable(RenderContext * this, byte param_1)
  0xe7430 undefined GetZWritesEnable(undefined4 param_1)
  0xe8600 undefined ~RenderContext(void)
  0xe8740 undefined RenderContext(undefined4 param_1)
  0xee010 undefined NewViewPort(void)
  0xee0a0 undefined DeleteViewPort(undefined4 param_1)
  0xf3520 undefined GetCurrentViewPort(void)

PS2 methods (22):
  0x28a0b0 EAGL::RenderContext::RenderContext
  0x28ab18 EAGL::RenderContext::BeginFrame
  0x28af48 EAGL::RenderContext::EndFrame
  0x28bb30 EAGL::RenderContext::SetupFrameBuffers
  0x290c08 EAGL::RenderContext::~RenderContext
  0x290cc8 EAGL::RenderContext::SetSize
  0x290ce0 EAGL::RenderContext::GetSize
  0x290d00 EAGL::RenderContext::SetFrontBufferDepth
  0x290d08 EAGL::RenderContext::GetFrontBufferDepth
  0x290d10 EAGL::RenderContext::SetBackBufferDepth
  0x290d18 EAGL::RenderContext::GetBackBufferDepth
  0x290d20 EAGL::RenderContext::SetZBufferDepth
  0x290d28 EAGL::RenderContext::GetZBufferDepth
  0x290d30 EAGL::RenderContext::SetSyncToVBL
  0x290d38 EAGL::RenderContext::GetSyncToVBL
  0x290d40 EAGL::RenderContext::SetDitherEnable
  0x290d50 EAGL::RenderContext::GetDitherEnable
  0x2913f8 EAGL::RenderContext::SetZWritesEnable
  0x291420 EAGL::RenderContext::GetZWritesEnable
  0x291750 EAGL::RenderContext::NewViewPort
  0x2917a8 EAGL::RenderContext::DeleteViewPort
  0x291840 EAGL::RenderContext::GetCurrentViewPort

Sheet rows:
  EAGL::RenderContext::RenderContext(EAGL::Device *)
  EAGL::RenderContext::BeginFrame(void)
  EAGL::RenderContext::EndFrame(void)
  EAGL::RenderContext::SetupFrameBuffers(void)
  EAGL::RenderContext::~RenderContext(void)
  EAGL::RenderContext::SetSize(float, float)
  EAGL::RenderContext::GetSize(float &, float &) const
  EAGL::RenderContext::SetFrontBufferDepth(int)
  EAGL::RenderContext::GetFrontBufferDepth(void) const
  EAGL::RenderContext::SetBackBufferDepth(int)
  EAGL::RenderContext::GetBackBufferDepth(void) const
  EAGL::RenderContext::SetZBufferDepth(int)
  EAGL::RenderContext::GetZBufferDepth(void) const
  EAGL::RenderContext::SetSyncToVBL(bool)
  EAGL::RenderContext::GetSyncToVBL(void)
  EAGL::RenderContext::SetDitherEnable(bool)
  EAGL::RenderContext::GetDitherEnable(bool &) const
  EAGL::RenderContext::SetZWritesEnable(bool)
  EAGL::RenderContext::GetZWritesEnable(bool &) const
  EAGL::RenderContext::NewViewPort(void)
  EAGL::RenderContext::DeleteViewPort(EAGL::ViewPort *)
  EAGL::RenderContext::GetCurrentViewPort(void) const

Xbox methods treated as members (16 of 16; untyped ones count when ECX is read before it is written): BeginFrame, DeleteViewPort, EndFrame, GetCurrentViewPort, GetSize, GetZWritesEnable, NewViewPort, RenderContext, SetBackBufferDepth, SetFrontBufferDepth, SetSize, SetSyncToVBL, SetZBufferDepth, SetZWritesEnable, SetupFrameBuffers, ~RenderContext

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [2: RenderContext@e8740, ~RenderContext@e8600]
  +0x004  w[4] W [1: RenderContext@e8740]
  +0x008  w[4] R/W [2: SetBackBufferDepth@e6ac0, SetupFrameBuffers@e6c00]
  +0x00c  w[4] R/W [2: SetZBufferDepth@e6b10, SetupFrameBuffers@e6c00]
  +0x010  w[4] R/W [3: EndFrame@e6640, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x014  w[4] R/W [3: RenderContext@e8740, SetSize@e6a60, SetupFrameBuffers@e6c00]
  +0x018  w[4] R/W [3: RenderContext@e8740, SetSize@e6a60, SetupFrameBuffers@e6c00]
  +0x01c  w[4] R/W [3: RenderContext@e8740, SetBackBufferDepth@e6ac0, SetupFrameBuffers@e6c00]
  +0x020  w[4] R/W [3: RenderContext@e8740, SetBackBufferDepth@e6ac0, SetupFrameBuffers@e6c00]
  +0x024  w[4] R/W [4: GetCurrentViewPort@f3520, RenderContext@e8740, SetZBufferDepth@e6b10, SetupFrameBuffers@e6c00]
  +0x02c  w[1] R/W [3: RenderContext@e8740, SetSyncToVBL@e6b60, SetupFrameBuffers@e6c00]
  +0x034  w[1] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x038  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x03c  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x040  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x044  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x048  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x04c  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x050  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x054  w[4] W [1: RenderContext@e8740]
  +0x058  w[1] R/W -> D3D8::D3DDevice_SetRenderState_StencilEnable [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x059  w[1] R/W [4: GetZWritesEnable@e7430, RenderContext@e8740, SetZWritesEnable@e73f0, SetupFrameBuffers@e6c00]
  +0x05c  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x060  w[1] W [1: RenderContext@e8740]
  +0x061  w[1] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x064  w[4] R/W [3: EndFrame@e6640, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x068  w[4] R/W [3: EndFrame@e6640, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x06c  w[4] R/W [3: EndFrame@e6640, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x070  w[4] R/W [3: EndFrame@e6640, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x074  w[4] R/W -> D3D8::D3DDevice_SetRenderState_FogColor [3: EndFrame@e6640, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x078  w[1] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x079  w[1] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x07c  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x080  w[4] R/W -> D3D8::D3D_SetPushBufferSize [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x084  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x088  w[4] W float [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x08c  w[4] W float [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x090  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x094  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x098  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x09c  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x0a0  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x0a4  w[4] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x0a8  w[1] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x0a9  w[1] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x0aa  w[1] W [1: RenderContext@e8740]
  +0x0ac  w[4] W [1: RenderContext@e8740]
  +0x0b0  w[1] R/W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x0b4  w[4] R/W [3: RenderContext@e8740, SetSyncToVBL@e6b60, SetupFrameBuffers@e6c00]
  +0x0b8  w- LEA addr-taken [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x0bc  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0c0  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0c4  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0c8  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0cc  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0d4  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0d8  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0dc  w[4] W [1: SetupFrameBuffers@e6c00]
  +0x0e0  w[4] RW/W [1: SetupFrameBuffers@e6c00]
  +0x0e4  w[4] W [2: SetSyncToVBL@e6b60, SetupFrameBuffers@e6c00]
  +0x0e8  w[4] W [2: SetSyncToVBL@e6b60, SetupFrameBuffers@e6c00]
  +0x0fc  w[4] W float [3: GetSize@e6a80, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x100  w[4] W float [3: GetSize@e6a80, RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x104  w[4] W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x108  w[4] W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x10c  w[4] W [2: RenderContext@e8740, SetupFrameBuffers@e6c00]
  +0x110  w[4] R/W -> D3D8::D3DSurface_GetDesc [3: EndFrame@e6640, SetupFrameBuffers@e6c00, ~RenderContext@e8600]
  +0x114  w[4] R/W -> D3D8::D3DDevice_SetRenderTarget [4: BeginFrame@e6610, EndFrame@e6640, SetupFrameBuffers@e6c00, ~RenderContext@e8600]
  +0x118  w[4] R/W [4: BeginFrame@e6610, EndFrame@e6640, SetupFrameBuffers@e6c00, ~RenderContext@e8600]
  +0x11c  w[4] W [1: RenderContext@e8740]
  +0x120  w[4] R/W [2: EndFrame@e6640, SetupFrameBuffers@e6c00]
  +0x124  w[4] R/W [2: EndFrame@e6640, SetupFrameBuffers@e6c00]
  +0x128  w[4] R/W [2: EndFrame@e6640, SetupFrameBuffers@e6c00]
  +0x12c  w[4] R/W [1: SetupFrameBuffers@e6c00]
  +0x130  w[4] R/W [1: SetupFrameBuffers@e6c00]
  +0x134  w[4] R/W [1: SetupFrameBuffers@e6c00]
  +0x138  w[4] R/W [2: DeleteViewPort@ee0a0, RenderContext@e8740]
  +0x13c  w[4] R/W [4: DeleteViewPort@ee0a0, NewViewPort@ee010, RenderContext@e8740, ~RenderContext@e8600]
  +0x140  w[4] W [1: RenderContext@e8740]
  +0x144  w[4] W [1: RenderContext@e8740]
  +0x148  w[4] W [1: RenderContext@e8740]

PS2 this-relative accesses (PS2 offsets):
  +0x74c  w[4] R/W [1: EndFrame@28af48]
  +0x750  w[4] W [1: EndFrame@28af48]
  +0x754  w[4] W [1: EndFrame@28af48]
  +0x758  w- LEA addr-taken [3: BeginFrame@28ab18, EndFrame@28af48, RenderContext@28a0b0]
  +0x75c  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x760  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x764  w[4] R/W float -> EAGL::TAR::~TAR, FUN_0027ed78 [2: GetSize@290ce0, SetupFrameBuffers@28bb30]
  +0x768  w[4] R/W float [2: GetSize@290ce0, SetupFrameBuffers@28bb30]
  +0x774  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x778  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x77c  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x780  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x784  w[4] R/W [1: BeginFrame@28ab18]
  +0x788  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x78c  w[4] R/W [1: SetupFrameBuffers@28bb30]
  +0x790  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x794  w[4] R/W [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x798  w[4] R/W [2: GetFrontBufferDepth@290d08, SetupFrameBuffers@28bb30]
  +0x79c  w[4] R/W [3: BeginFrame@28ab18, GetBackBufferDepth@290d18, SetupFrameBuffers@28bb30]
  +0x7a0  w[4] R/W [2: GetZBufferDepth@290d28, SetupFrameBuffers@28bb30]
  +0x7a4  w[4] R/W [3: BeginFrame@28ab18, EndFrame@28af48, SetupFrameBuffers@28bb30]
  +0x7a8  w[4] R [1: BeginFrame@28ab18]
  +0x7ac  w[4] R [1: BeginFrame@28ab18]
  +0x7b0  w[4] R/W [1: SetupFrameBuffers@28bb30]
  +0x7b4  w[4] R/W [1: SetupFrameBuffers@28bb30]
  +0x7b8  w[4] R/W [1: SetupFrameBuffers@28bb30]
  +0x7d8  w[4] W [1: SetupFrameBuffers@28bb30]
  +0x7dc  w[4] R [2: BeginFrame@28ab18, SetupFrameBuffers@28bb30]
  +0x7e0  w[4] W [1: SetupFrameBuffers@28bb30]
  +0x7e4  w[4] W [1: SetupFrameBuffers@28bb30]
  +0x7e8  w[4] W [1: SetupFrameBuffers@28bb30]
  +0x7ec  w[4] W [1: SetupFrameBuffers@28bb30]
  +0x7f4  w[4] R/W [4: BeginFrame@28ab18, GetDitherEnable@290d50, SetDitherEnable@290d40, SetupFrameBuffers@28bb30]
  +0x7f8  w[4] R/W [3: EndFrame@28af48, GetSyncToVBL@290d38, SetSyncToVBL@290d30]
  +0x7fc  w[4] R [1: EndFrame@28af48]
  +0x800  w[4] R/W [1: EndFrame@28af48]
  +0x804  w[4] R/W [2: EndFrame@28af48, SetupFrameBuffers@28bb30]
  +0x808  w[4] R [1: GetZWritesEnable@291420]
  +0x80c  w[4] R [1: EndFrame@28af48]
  +0x810  w[4] R/W float [2: SetSize@290cc8, SetupFrameBuffers@28bb30]
  +0x814  w[4] R/W float [2: SetSize@290cc8, SetupFrameBuffers@28bb30]
  +0x818  w[4] R/W [2: SetFrontBufferDepth@290d00, SetupFrameBuffers@28bb30]
  +0x81c  w[4] R/W [2: SetBackBufferDepth@290d10, SetupFrameBuffers@28bb30]
  +0x820  w[4] R/W [2: SetZBufferDepth@290d20, SetupFrameBuffers@28bb30]
  +0x828  w[4] R/W [2: DeleteViewPort@2917a8, GetCurrentViewPort@291840]
  +0x82c  w[4] R/W [3: DeleteViewPort@2917a8, NewViewPort@291750, ~RenderContext@290c08]
  +0x838  w[4] R/W -> EAGL::TAR::~TAR [2: SetupFrameBuffers@28bb30, ~RenderContext@290c08]
  +0x83c  w[4] R/W -> EAGL::TAR::~TAR [2: SetupFrameBuffers@28bb30, ~RenderContext@290c08]
  +0x840  w[4] R/W -> EAGL::TAR::~TAR [2: SetupFrameBuffers@28bb30, ~RenderContext@290c08]
  +0x87c  w[4] R/W -> EAGL::DeviceExtension::SetTextureBasePointer [2: RenderContext@28a0b0, SetupFrameBuffers@28bb30]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetZWritesEnable: R +0x59 w1
  GetCurrentViewPort: R +0x24 w4
