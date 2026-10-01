# RShadowMap

FastAlloc/constructed sizes under its tag: {'allocated': [88], 'constructed': []}
deleting destructor 0xa6670 frees/deletes with size 0x58 (call to UMemory::FastFree)
Xbox vtable 0x00192fd4 (5 slots) stored by its constructor
PS2 sheet virtual table row: ['RShadowMap::USingleton virtual table']
constructor 0xa5ac0 first calls: ['RViewCamera::RViewCamera', '__builtin_new', 'RShadowMap::ShadowPrivateData::ShadowPrivateData']

Xbox methods (11):
  0x8beb0 undefined Init(void)
  0xa5820 undefined4 * __stdcall LoadAttributes(void)
  0xa5950 undefined ProjectedShadowsEnabled(void)
  0xa5960 undefined ClearShadowMap(void)
  0xa5ac0 undefined RShadowMap(void)
  0xa5c10 undefined ~RShadowMap(void)
  0xa5c90 undefined SetCamera(undefined4 param_1, undefined4 param_2)
  0xa6050 undefined Begin(undefined4 param_1, undefined4 param_2)
  0xa60b0 undefined Draw(undefined4 param_1, undefined4 param_2)
  0xa60f0 undefined DrawSimpleShadow(undefined4 param_1)
  0xa6670 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (25):
  0x1eb7d8 RShadowMap::GetTexture
  0x1eb7e8 RShadowMap::GetShadowMatrix
  0x1eb7f0 RShadowMap::LoadAttributes
  0x1eb9c8 RShadowMap::RShadowMap
  0x1ebaf8 RShadowMap::~RShadowMap
  0x1ebb90 RShadowMap::ReInit
  0x1ebb98 RShadowMap::Lock
  0x1ebba0 RShadowMap::UnLockAll
  0x1ebbe8 RShadowMap::SetCamera
  0x1ec138 RShadowMap::Begin
  0x1ec1e8 RShadowMap::ProjectedShadowsEnabled
  0x1ec1f8 RShadowMap::ClearShadowMap
  0x1ec2d8 RShadowMap::Draw
  0x1ec300 RShadowMap::End
  0x1ec370 RShadowMap::DrawShadowUnderCar
  0x1ec390 RShadowMap::Resolve
  0x1ec4a0 RShadowMap::DrawSimpleShadow
  0x1ecd28 RShadowMap::Get
  0x1ecd38 RShadowMap::Init
  0x1ecd70 RShadowMap::Kill
  0x1ecda8 RShadowMap::operator_new
  0x1ecdc8 RShadowMap::operator_delete
  0x1ecde8 RShadowMap::Reset
  0x1ecdf0 RShadowMap::DoRender
  0x1ecdf8 RShadowMap::fgThis_RShadowMap_global_ctors

Sheet rows:
  RShadowMap::ShadowPrivateData::ShadowPrivateData(void)
  RShadowMap::ShadowPrivateData::~ShadowPrivateData(void)
  RShadowMap::GetTexture(void)
  RShadowMap::GetShadowMatrix(void)
  RShadowMap::LoadAttributes(void)
  RShadowMap::RShadowMap(void)
  RShadowMap::~RShadowMap(void)
  RShadowMap::ReInit(void)
  RShadowMap::Lock(void)
  RShadowMap::UnLockAll(void)
  RShadowMap::SetCamera(RVehicle &, COORD4 &)
  RShadowMap::Begin(RVehicle &, COORD4 &)
  RShadowMap::ProjectedShadowsEnabled(void)
  RShadowMap::ClearShadowMap(void)
  RShadowMap::Draw(RVehicle &, COORD4 &)
  RShadowMap::End(void)
  RShadowMap::DrawShadowUnderCar(RVehicle &, COORD4 &)
  RShadowMap::Resolve(char *, bool &)
  RShadowMap::DrawSimpleShadow(RVehicle &)
  RShadowMap type_info function
  RShadowMap::Get(void)
  RShadowMap::Init(void)
  RShadowMap::Kill(void)
  RShadowMap::operator new(unsigned int)
  RShadowMap::operator delete(void *, unsigned int)
  RShadowMap::Reset(void)
  RShadowMap::DoRender(void)
  RShadowMap::fgThis_RShadowMap
  RShadowMap::USingleton virtual table
  RShadowMap virtual table
  RShadowMap type_info node

Xbox methods treated as members (10 of 11; untyped ones count when ECX is read before it is written): Begin, ClearShadowMap, Draw, DrawSimpleShadow, Init, ProjectedShadowsEnabled, RShadowMap, SetCamera, scalar_deleting_destructor, ~RShadowMap

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RShadowMap@a5ac0, ~RShadowMap@a5c10]
  +0x018  w[4] R/W -> EAGL::ViewPort::BeginView, EAGL::ViewPort::ClearViewPort, EAGL::ViewPort::SetBackgroundColour, EAGL::ViewPort::SetViewMa [5: Begin@a6050, ClearShadowMap@a5960, RShadowMap@a5ac0, SetCamera@a5c90, ~RShadowMap@a5c10]
  +0x03c  w[1] W [2: Begin@a6050, ClearShadowMap@a5960]
  +0x04c  w[4] W [2: RShadowMap@a5ac0, ~RShadowMap@a5c10]
  +0x050  w[4] R/W [6: Begin@a6050, ClearShadowMap@a5960, Draw@a60b0, RShadowMap@a5ac0, SetCamera@a5c90, ~RShadowMap@a5c10]
  +0x054  w[4] R/W [2: RShadowMap@a5ac0, ~RShadowMap@a5c10]

PS2 this-relative accesses (PS2 offsets):
  +0x014  w[4] R/W -> EAGL::ViewPort::BeginView, EAGL::ViewPort::ClearViewPort, EAGL::ViewPort::SetBackgroundColour, EAGL::ViewPort::SetViewMa [5: Begin@1ec138, ClearShadowMap@1ec1f8, RShadowMap@1eb9c8, SetCamera@1ebbe8, ~RShadowMap@1ebaf8]
  +0x038  w[4] W [2: Begin@1ec138, ClearShadowMap@1ec1f8]
  +0x054  w[4] W [2: RShadowMap@1eb9c8, ~RShadowMap@1ebaf8]
  +0x058  w[4] W [2: RShadowMap@1eb9c8, ~RShadowMap@1ebaf8]
  +0x060  w[4] R/W -> RShadowMap::ShadowPrivateData::~ShadowPrivateData, RViewCamera::EndView, VU0_MATRIX4_mult [9: Begin@1ec138, ClearShadowMap@1ec1f8, DrawSimpleShadow@1ec4a0, GetShadowMatrix@1eb7e8, GetTexture@1eb7d8, RShadowMap@1eb9c8…]
  +0x064  w[4] R/W [2: RShadowMap@1eb9c8, ~RShadowMap@1ebaf8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
