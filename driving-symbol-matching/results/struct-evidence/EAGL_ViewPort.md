# EAGL::ViewPort

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xf37d0 first calls: ['FUN_000e4b90', 'FUN_000e4bb0']

Xbox methods (10):
  0xe4680 undefined GetShape(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 
  0xe4900 undefined SetOrthographic(undefined4 param_1, undefined4 param_2)
  0xe49a0 undefined EndView(void)
  0xe49d0 undefined ClearViewPort(undefined1 param_1)
  0xe4a10 undefined IsSphereInView(undefined4 param_1, undefined4 param_2)
  0xe4be0 undefined BeginView(void)
  0xe4ef0 undefined SetViewMatrix(undefined4 param_1)
  0xf37d0 undefined ViewPort(undefined4 param_1)
  0xf3900 undefined GetEnableModelSphereCull(void)
  0xf3970 undefined SetBackgroundColour(undefined4 param_1)

PS2 methods (19):
  0x295db8 EAGL::ViewPort::SetOrthographic
  0x295fe8 EAGL::ViewPort::BeginView
  0x296548 EAGL::ViewPort::ClearViewPort
  0x296b98 EAGL::ViewPort::IsSphereInView
  0x296d88 EAGL::ViewPort::GetShape
  0x296e80 EAGL::ViewPort::SetViewMatrix
  0x296f58 EAGL::ViewPort::EndView
  0x296f98 EAGL::ViewPort::SetGuardBandScale
  0x297010 EAGL::ViewPort::ViewPort
  0x2971f8 EAGL::ViewPort::ViewPort
  0x2973e0 EAGL::ViewPort::SetEnableModelSphereCull
  0x2973e8 EAGL::ViewPort::GetEnableModelSphereCull
  0x2973f0 EAGL::ViewPort::~ViewPort
  0x297440 EAGL::ViewPort::GetFrustum
  0x297468 EAGL::ViewPort::GetProjectionType
  0x297470 EAGL::ViewPort::SetBackgroundColour
  0x297478 EAGL::ViewPort::GetBackgroundColour
  0x297488 EAGL::ViewPort::GetProjectionMatrix
  0x297490 EAGL::ViewPort::GetViewProjectionMatrix

Sheet rows:
  EAGL::ViewPort::SetShape(float, float, float, float, float, flo
  EAGL::ViewPort::SetPerspective(float, float, float, float)
  EAGL::ViewPort::SetOrthographic(float, float, float)
  EAGL::ViewPort::SetOrthographicScreenSpace(float, float)
  EAGL::ViewPort::BeginView(void)
  EAGL::ViewPort::ClearViewPort(EAGL::ClearFlags)
  EAGL::ViewPort::IsSphereInView(COORD3 &, float)
  EAGL::ViewPort::GetShape(float &, float &, float &, float &, fl
  EAGL::ViewPort::SetViewMatrix(MATRIX4 &)
  EAGL::ViewPort::EndView(void)
  EAGL::ViewPort::SetGuardBandScale(float)
  EAGL::ViewPort::ViewPort(EAGL::RenderContext *)
  EAGL::ViewPort::ViewPort(EAGL::TextureRenderContext *)
  EAGL::ViewPort::SetEnableModelSphereCull(int)
  EAGL::ViewPort::GetEnableModelSphereCull(void)
  EAGL::ViewPort::~ViewPort(void)
  EAGL::ViewPort::GetFrustum(float &, float &, float &, float &)
  EAGL::ViewPort::GetProjectionType(void) const
  EAGL::ViewPort::SetBackgroundColour(EAGL::Colour)
  EAGL::ViewPort::GetBackgroundColour(void) const
  EAGL::ViewPort::GetViewMatrix(void)
  EAGL::ViewPort::GetProjectionMatrix(void) const
  EAGL::ViewPort::GetViewProjectionMatrix(void)
  EAGL::ViewPort::gpViewMatrix
  EAGL::ViewPort::gpProjectionMatrix
  EAGL::ViewPort::gpModelMatrix
  EAGL::ViewPort::gpViewProjectionMatrix
  EAGL::ViewPort::gpModelViewMatrix
  EAGL::ViewPort::gpModelViewProjectionMatrix

Xbox methods treated as members (10 of 10; untyped ones count when ECX is read before it is written): BeginView, ClearViewPort, EndView, GetEnableModelSphereCull, GetShape, IsSphereInView, SetBackgroundColour, SetOrthographic, SetViewMatrix, ViewPort

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x004  w[4] R/W [3: BeginView@e4be0, EndView@e49a0, ViewPort@f37d0]
  +0x008  w[4] R/W [2: GetEnableModelSphereCull@f3900, ViewPort@f37d0]
  +0x010  w- LEA addr-taken -> FUN_000e4bb0 [2: BeginView@e4be0, ViewPort@f37d0]
  +0x028  w[4] R/W -> EAGL::TextureRenderContext::GetCurrentViewPort [3: BeginView@e4be0, SetOrthographic@e4900, ViewPort@f37d0]
  +0x02c  w[4] R/W -> D3D8::D3DDevice_SetRenderTarget, EAGL::RenderContext::GetCurrentViewPort, EAGL::TextureRenderContext::GetSize [3: BeginView@e4be0, SetOrthographic@e4900, ViewPort@f37d0]
  +0x030  w[4] R/W [2: IsSphereInView@e4a10, SetOrthographic@e4900]
  +0x034  w[4] R/W [2: ClearViewPort@e49d0, SetBackgroundColour@f3970]
  +0x040  w- LEA addr-taken [3: BeginView@e4be0, SetOrthographic@e4900, ViewPort@f37d0]
  +0x080  w- LEA addr-taken [3: BeginView@e4be0, SetViewMatrix@e4ef0, ViewPort@f37d0]
  +0x0c0  w- LEA addr-taken [2: BeginView@e4be0, ViewPort@f37d0]
  +0x118  w[4] W float [1: GetShape@e4680]
  +0x11c  w[4] W float [1: GetShape@e4680]
  +0x120  w[4] W float [2: GetShape@e4680, SetOrthographic@e4900]
  +0x124  w[4] W float [2: GetShape@e4680, SetOrthographic@e4900]
  +0x128  w[4] R [1: GetShape@e4680]
  +0x12c  w[4] R [1: GetShape@e4680]
  +0x138  w[4] W float [1: IsSphereInView@e4a10]
  +0x13c  w[4] W float [1: IsSphereInView@e4a10]
  +0x158  w[4] W float [1: IsSphereInView@e4a10]
  +0x15c  w[4] W float [1: IsSphereInView@e4a10]
  +0x160  w[4] W float [1: IsSphereInView@e4a10]
  +0x164  w[4] W float [1: IsSphereInView@e4a10]
  +0x168  w[4] W float [1: IsSphereInView@e4a10]
  +0x16c  w[4] W float [1: IsSphereInView@e4a10]
  +0x170  w[4] W float [1: IsSphereInView@e4a10]
  +0x174  w[4] W float [1: IsSphereInView@e4a10]
  +0x188  w[1] R/W [3: BeginView@e4be0, EndView@e49a0, SetViewMatrix@e4ef0]
  +0x190  w[4] R -> EAGL::ViewPort::BeginView [1: SetViewMatrix@e4ef0]

PS2 this-relative accesses (PS2 offsets):
  +0x004  w[4] R/W -> EAGL::ViewPort::BeginView [4: BeginView@295fe8, EndView@296f58, ViewPort@297010, ViewPort@2971f8]
  +0x008  w[4] R/W [4: GetEnableModelSphereCull@2973e8, SetEnableModelSphereCull@2973e0, ViewPort@297010, ViewPort@2971f8]
  +0x010  w[4] LEA/R/W addr-taken -> EAGL::RenderContext::GetCurrentViewPort, EAGL::TextureRenderContext::GetCurrentViewPort, EAGLInternal::RenderContextPriv [5: BeginView@295fe8, ClearViewPort@296548, SetOrthographic@295db8, ViewPort@297010, ViewPort@2971f8]
  +0x014  w[4] R/W -> EAGL::TextureRenderContext::GetCurrentViewPort, EAGL::TextureRenderContext::GetSize, EAGL::ViewPort::EndView, EAGLIntern [5: BeginView@295fe8, ClearViewPort@296548, SetOrthographic@295db8, ViewPort@297010, ViewPort@2971f8]
  +0x018  w[4] R/W [3: GetProjectionType@297468, IsSphereInView@296b98, SetOrthographic@295db8]
  +0x01c  w[4] R/W [3: ClearViewPort@296548, GetBackgroundColour@297478, SetBackgroundColour@297470]
  +0x020  w[4, 8] LEA/R/W float addr-taken [5: BeginView@295fe8, GetProjectionMatrix@297488, SetOrthographic@295db8, ViewPort@297010, ViewPort@2971f8]
  +0x028  w[8] R/W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x030  w[8] R/W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x038  w[8] R/W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x040  w[8] R/W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x048  w[8] R/W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x050  w[8] R/W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x058  w[8] R/W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x060  w[8] LEA/R/W addr-taken [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x068  w[8] R/W [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x070  w[8] R/W [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x078  w[8] R/W [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x080  w[8] R/W [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x088  w[8] R/W [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x090  w[8] R/W [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x098  w[8] R/W [4: BeginView@295fe8, SetViewMatrix@296e80, ViewPort@297010, ViewPort@2971f8]
  +0x0a0  w[8] LEA/W addr-taken [4: BeginView@295fe8, GetViewProjectionMatrix@297490, ViewPort@297010, ViewPort@2971f8]
  +0x0a8  w[8] W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x0b0  w[8] W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x0b8  w[8] W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x0c0  w[8] W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x0c8  w[8] W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x0d0  w[8] W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x0d8  w[8] W [3: BeginView@295fe8, ViewPort@297010, ViewPort@2971f8]
  +0x0e0  w[4] R [3: BeginView@295fe8, ClearViewPort@296548, SetOrthographic@295db8]
  +0x0e4  w[4] R [3: BeginView@295fe8, ClearViewPort@296548, SetOrthographic@295db8]
  +0x0e8  w[4] R float [3: BeginView@295fe8, ClearViewPort@296548, SetOrthographic@295db8]
  +0x0ec  w[4] R float [3: BeginView@295fe8, ClearViewPort@296548, SetOrthographic@295db8]
  +0x0f0  w[4] R float [1: SetOrthographic@295db8]
  +0x0f4  w[4] R float [1: SetOrthographic@295db8]
  +0x0f8  w[4] R float [1: GetShape@296d88]
  +0x0fc  w[4] R float [1: GetShape@296d88]
  +0x100  w[4] R float [1: GetShape@296d88]
  +0x104  w[4] R float [1: GetShape@296d88]
  +0x108  w[4] R float [1: GetShape@296d88]
  +0x10c  w[4] R float [1: GetShape@296d88]
  +0x110  w[4] R float [1: GetFrustum@297440]
  +0x114  w[4] R float [1: GetFrustum@297440]
  +0x118  w[4] R float [2: GetFrustum@297440, IsSphereInView@296b98]
  +0x11c  w[4] R float [2: GetFrustum@297440, IsSphereInView@296b98]
  +0x120  w[4] R float [1: SetOrthographic@295db8]
  +0x124  w[4] R float [1: SetOrthographic@295db8]
  +0x130  w[4] R float [1: SetOrthographic@295db8]
  +0x134  w[4] R float [1: SetOrthographic@295db8]
  +0x138  w[4] R float [1: IsSphereInView@296b98]
  +0x13c  w[4] R float [1: IsSphereInView@296b98]
  +0x140  w[4] R float [1: IsSphereInView@296b98]
  +0x144  w[4] R float [1: IsSphereInView@296b98]
  +0x148  w[4] R float [1: IsSphereInView@296b98]
  +0x14c  w[4] R float [1: IsSphereInView@296b98]
  +0x150  w[4] R float [1: IsSphereInView@296b98]
  +0x154  w[4] R float [1: IsSphereInView@296b98]
  +0x168  w[4] R/W [3: BeginView@295fe8, EndView@296f58, SetViewMatrix@296e80]
  +0x16c  w[4] R/W float [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x170  w[4] R/W float [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x174  w[4] R/W float [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x178  w[4] R/W [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x17c  w[4] R/W float [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x180  w[4] R/W float [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x184  w[4] R/W float [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x188  w[4] R/W float [2: BeginView@295fe8, SetOrthographic@295db8]
  +0x190  w[4] R/W float [2: SetGuardBandScale@296f98, SetOrthographic@295db8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetEnableModelSphereCull: R +0x8 w4
  SetBackgroundColour: W +0x34 w4
