# RViewCamera

FastAlloc/constructed sizes under its tag: {'allocated': [76], 'constructed': [76]}
deleting destructor 0x96c60 frees/deletes with size 0x4c (call to UMemory::FastFree)
Xbox vtable 0x00192444 (5 slots) stored by its constructor
PS2 sheet virtual table row: ['RViewCamera virtual table']
constructor 0x96ae0 first calls: ['EAGL::RenderContext::NewViewPort', 'UMemory::FastAlloc', 'RCamera::RCamera']

Xbox methods (18):
  0x8bf20 undefined ~RViewCamera(void)
  0x96830 undefined ~RViewCamera(void)
  0x96850 undefined SetGuardBandSize(undefined4 param_1)
  0x96860 void __thiscall UpdateForResolution(RViewCamera * this)
  0x968b0 undefined RefreshLODMultiplier(void)
  0x968e0 undefined EndView(void)
  0x96900 undefined SetZBufferRange(undefined4 param_1, undefined4 param_2)
  0x96960 undefined AspectRatio(void)
  0x969f0 undefined SetViewPortToUnitTransformMode(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0x96a70 void __thiscall SetDeviceTransformMode(RViewCamera * this)
  0x96ae0 RViewCamera * __thiscall RViewCamera(RViewCamera * this, void * rCamera)
  0x96c60 void __thiscall scalar_deleting_destructor(RViewCamera * this)
  0x96ca0 undefined SetFillColour(undefined4 param_1)
  0x96cc0 undefined SetExtents(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x96d70 undefined SetExtents(undefined4 param_1)
  0x96eb0 undefined SetWorldTransformMode(void)
  0x96fc0 undefined SetRenderCamera(void)
  0x97060 void __thiscall Render(RViewCamera * this)

PS2 methods (54):
  0x1d40f0 RViewCamera::RViewCamera
  0x1d4238 RViewCamera::~RViewCamera
  0x1d4298 RViewCamera::SetGuardBandSize
  0x1d42c0 RViewCamera::SetFillColour
  0x1d42e8 RViewCamera::ClearZToFar
  0x1d42f0 RViewCamera::ClearZToNear
  0x1d42f8 RViewCamera::UpdateForResolution
  0x1d43f8 RViewCamera::SetScreenYLimits
  0x1d4420 RViewCamera::SetExtents
  0x1d4468 RViewCamera::SetExtents
  0x1d44c8 RViewCamera::RefreshLODMultiplier
  0x1d4500 RViewCamera::Render
  0x1d4570 RViewCamera::BeginView
  0x1d4628 RViewCamera::EndView
  0x1d4660 RViewCamera::SetZBufferRange
  0x1d4688 RViewCamera::AspectRatio
  0x1d46d0 RViewCamera::SetRenderCamera
  0x1d4878 RViewCamera::SetWorldTransformMode
  0x1d4ab0 RViewCamera::SetViewPortToDeviceTransformMode
  0x1d4b78 RViewCamera::SetViewPortToUnitTransformMode
  0x1d4c20 RViewCamera::SetViewPortToOrthoTransformMode
  0x1d4cb8 RViewCamera::SetDeviceTransformMode
  0x1d4d30 RViewCamera::SetUnitTransformMode
  0x1d4da8 RViewCamera::CompensateDepthBias
  0x1d4db0 RViewCamera::ReconfigZPlanes
  0x1d4e20 RViewCamera::SetClearDepth
  0x1d5060 RViewCamera::operator_new
  0x1d5080 RViewCamera::operator_delete
  0x1d50a0 RViewCamera::ViewPort
  0x1d50a8 RViewCamera::Camera
  0x1d50b0 RViewCamera::Camera
  0x1d50b8 RViewCamera::SetCamera
  0x1d50c0 RViewCamera::GetScreenYLimits
  0x1d50d8 RViewCamera::GetScreenXLimits
  0x1d50f0 RViewCamera::GetNearZPlane
  0x1d50f8 RViewCamera::GetFarZPlane
  0x1d5100 RViewCamera::SetNearZPlane
  0x1d5120 RViewCamera::SetFarZPlane
  0x1d5140 RViewCamera::GetLODMultiplier
  0x1d5148 RViewCamera::SetAspectRatioMultiplier
  0x1d5158 RViewCamera::GetFillColour
  0x1d5160 RViewCamera::SetViewId
  0x1d5168 RViewCamera::GetViewId
  0x1d5170 RViewCamera::GetGuardBandSize
  0x1d5178 RViewCamera::SetActive
  0x1d5180 RViewCamera::GetCulling
  0x1d5190 RViewCamera::ExportedEndView
  0x1d51b0 RViewCamera::Debug
  0x1d51b8 RViewCamera::PreRender
  0x1d51c0 RViewCamera::PostRender
  0x1d51c8 RViewCamera::GetPlayer
  0x1d51d0 RViewCamera::SetPlayer
  0x1d51d8 RViewCamera::fCulling_global_ctors
  0x1d51f8 RViewCamera::fCulling_global_dtors

Sheet rows:
  RViewCamera::RViewCamera(RCamera *)
  RViewCamera::~RViewCamera(void)
  RViewCamera::SetGuardBandSize(float)
  RViewCamera::SetFillColour(unsigned int)
  RViewCamera::ClearZToFar(void) const
  RViewCamera::ClearZToNear(void) const
  RViewCamera::UpdateForResolution(void) const
  RViewCamera::SetScreenYLimits(float, float)
  RViewCamera::SetExtents(float, float, float, float)
  RViewCamera::SetExtents(RViewCamera &)
  RViewCamera::RefreshLODMultiplier(void)
  RViewCamera::Render(void)
  RViewCamera::BeginView(void)
  RViewCamera::EndView(void)
  RViewCamera::SetZBufferRange(unsigned long, unsigned long)
  RViewCamera::AspectRatio(void)
  RViewCamera::SetRenderCamera(void)
  RViewCamera::SetWorldTransformMode(void)
  RViewCamera::SetViewPortToDeviceTransformMode(EAGL::ViewPort &,
  RViewCamera::SetViewPortToUnitTransformMode(EAGL::ViewPort &, f
  RViewCamera::SetViewPortToOrthoTransformMode(EAGL::ViewPort &,
  RViewCamera::SetDeviceTransformMode(void)
  RViewCamera::SetUnitTransformMode(void)
  RViewCamera::CompensateDepthBias(void)
  RViewCamera::ReconfigZPlanes(float, float)
  RViewCamera::SetClearDepth(float)
  RViewCamera type_info function
  RViewCamera::operator new(unsigned int)
  RViewCamera::operator delete(void *, unsigned int)
  RViewCamera::ViewPort(void) const
  RViewCamera::Camera(void) const
  RViewCamera::Camera(void)
  RViewCamera::SetCamera(RCamera &)
  RViewCamera::GetScreenYLimits(float &, float &) const
  RViewCamera::GetScreenXLimits(float &, float &)
  RViewCamera::GetNearZPlane(void) const
  RViewCamera::GetFarZPlane(void) const
  RViewCamera::SetNearZPlane(float)
  RViewCamera::SetFarZPlane(float)
  RViewCamera::GetLODMultiplier(void) const
  RViewCamera::SetAspectRatioMultiplier(float)
  RViewCamera::GetFillColour(void) const
  RViewCamera::SetViewId(RViewCamera::ViewId)
  RViewCamera::GetViewId(void) const
  RViewCamera::GetGuardBandSize(void)
  RViewCamera::SetActive(bool)
  RViewCamera::GetCulling(void)
  RViewCamera::ExportedEndView(void)
  RViewCamera::Debug(void)
  RViewCamera::PreRender(void)
  RViewCamera::PostRender(void)
  RViewCamera::GetPlayer(void)
  RViewCamera::SetPlayer(int)
  RViewCamera::fCulling
  RViewCamera::fAspectRatioMult
  RViewCamera virtual table
  RViewCamera type_info node

Xbox methods treated as members (17 of 18; untyped ones count when ECX is read before it is written): AspectRatio, EndView, RViewCamera, RefreshLODMultiplier, Render, SetDeviceTransformMode, SetExtents, SetFillColour, SetGuardBandSize, SetRenderCamera, SetWorldTransformMode, SetZBufferRange, UpdateForResolution, scalar_deleting_destructor, ~RViewCamera

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [4: RViewCamera@96ae0, Render@97060, scalar_deleting_destructor@96c60, ~RViewCamera@96830]
  +0x004  w[4] R/W [6: RViewCamera@96ae0, RefreshLODMultiplier@968b0, SetExtents@96cc0, SetExtents@96d70, SetRenderCamera@96fc0, SetWorldTransformMode@96eb0]
  +0x008  w[4] R/W [2: RViewCamera@96ae0, SetRenderCamera@96fc0]
  +0x00c  w[4] R/W [2: RViewCamera@96ae0, SetRenderCamera@96fc0]
  +0x010  w[4] W [1: RViewCamera@96ae0]
  +0x014  w[4] W [1: RViewCamera@96ae0]
  +0x018  w[4] R/W -> EAGL::ViewPort::BeginView, EAGL::ViewPort::ClearViewPort, EAGL::ViewPort::EndView, EAGL::ViewPort::SetBackgroundColour,  [14: EndView@968e0, RViewCamera@96ae0, Render@97060, SetDeviceTransformMode@96a70, SetExtents@96cc0, SetExtents@96d70…]
  +0x01c  w[4] LEA/W float addr-taken [6: RViewCamera@96ae0, RefreshLODMultiplier@968b0, SetExtents@96cc0, SetExtents@96d70, SetZBufferRange@96900, UpdateForResolution@96860]
  +0x020  w[4] W float [5: RViewCamera@96ae0, SetExtents@96cc0, SetExtents@96d70, SetZBufferRange@96900, UpdateForResolution@96860]
  +0x024  w[4] R [1: SetWorldTransformMode@96eb0]
  +0x028  w[4] LEA/W float addr-taken [6: RViewCamera@96ae0, RefreshLODMultiplier@968b0, SetExtents@96cc0, SetExtents@96d70, SetZBufferRange@96900, UpdateForResolution@96860]
  +0x02c  w[4] W float [5: RViewCamera@96ae0, SetExtents@96cc0, SetExtents@96d70, SetZBufferRange@96900, UpdateForResolution@96860]
  +0x030  w[4] R [1: SetWorldTransformMode@96eb0]
  +0x034  w[4] W float [4: RViewCamera@96ae0, RefreshLODMultiplier@968b0, SetExtents@96cc0, SetExtents@96d70]
  +0x038  w[4] R/W [3: RViewCamera@96ae0, SetFillColour@96ca0, SetRenderCamera@96fc0]
  +0x03c  w[1] R/W [6: EndView@968e0, RViewCamera@96ae0, Render@97060, SetDeviceTransformMode@96a70, SetRenderCamera@96fc0, SetWorldTransformMode@96eb0]
  +0x040  w[4] W [2: RViewCamera@96ae0, SetZBufferRange@96900]
  +0x044  w[4] W [2: RViewCamera@96ae0, SetZBufferRange@96900]
  +0x048  w[4] W [1: SetGuardBandSize@96850]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R/W [8: BeginView@1d4570, Camera@1d50a8, Camera@1d50b0, RViewCamera@1d40f0, ReconfigZPlanes@1d4db0, RefreshLODMultiplier@1d44c8…]
  +0x004  w[4] R/W [2: RViewCamera@1d40f0, SetRenderCamera@1d46d0]
  +0x008  w[4] R/W [2: RViewCamera@1d40f0, SetRenderCamera@1d46d0]
  +0x00c  w[4] R/W [3: GetViewId@1d5168, RViewCamera@1d40f0, SetViewId@1d5160]
  +0x010  w[4] R/W [3: GetPlayer@1d51c8, RViewCamera@1d40f0, SetPlayer@1d51d0]
  +0x014  w[4] R/W -> EAGL::ViewPort::BeginView, EAGL::ViewPort::ClearViewPort, EAGL::ViewPort::SetViewMatrix, FUN_00295910, RRenderer::EndVie [10: BeginView@1d4570, EndView@1d4628, RViewCamera@1d40f0, ReconfigZPlanes@1d4db0, SetDeviceTransformMode@1d4cb8, SetUnitTransformMode@1d4d30…]
  +0x018  w[4, 8] R/W float [6: GetScreenXLimits@1d50d8, RViewCamera@1d40f0, RefreshLODMultiplier@1d44c8, SetExtents@1d4420, SetExtents@1d4468, UpdateForResolution@1d42f8]
  +0x01c  w[4] R/W float [4: GetScreenYLimits@1d50c0, SetExtents@1d4420, SetScreenYLimits@1d43f8, UpdateForResolution@1d42f8]
  +0x020  w[4] R/W float [5: GetNearZPlane@1d50f0, RViewCamera@1d40f0, ReconfigZPlanes@1d4db0, SetExtents@1d4468, SetWorldTransformMode@1d4878]
  +0x024  w[4, 8] R/W float [6: GetScreenXLimits@1d50d8, RViewCamera@1d40f0, RefreshLODMultiplier@1d44c8, SetExtents@1d4420, SetExtents@1d4468, UpdateForResolution@1d42f8]
  +0x028  w[4] R/W float [3: GetScreenYLimits@1d50c0, SetExtents@1d4420, UpdateForResolution@1d42f8]
  +0x02c  w[4] R/W float [5: GetFarZPlane@1d50f8, RViewCamera@1d40f0, ReconfigZPlanes@1d4db0, SetExtents@1d4468, SetWorldTransformMode@1d4878]
  +0x030  w[4] R/W float [2: GetLODMultiplier@1d5140, RefreshLODMultiplier@1d44c8]
  +0x034  w[4] R/W [4: BeginView@1d4570, GetFillColour@1d5158, RViewCamera@1d40f0, SetFillColour@1d42c0]
  +0x038  w[4] R/W [7: BeginView@1d4570, EndView@1d4628, RViewCamera@1d40f0, SetActive@1d5178, SetDeviceTransformMode@1d4cb8, SetUnitTransformMode@1d4d30…]
  +0x040  w[8] R/W [3: RViewCamera@1d40f0, SetZBufferRange@1d4660, UpdateForResolution@1d42f8]
  +0x048  w[8] R/W -> __floatdisf [2: RViewCamera@1d40f0, UpdateForResolution@1d42f8]
  +0x050  w[4] R/W float [2: GetGuardBandSize@1d5170, SetGuardBandSize@1d4298]
  +0x054  w[4] R/W [3: RViewCamera@1d40f0, Render@1d4500, ~RViewCamera@1d4238]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
