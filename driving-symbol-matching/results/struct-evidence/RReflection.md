# RReflection

FastAlloc/constructed sizes under its tag: {'allocated': [84], 'constructed': []}
deleting destructor 0x99cc0 frees/deletes with size 0x54 (call to UMemory::FastFree)
Xbox vtable 0x001926a4 (5 slots) stored by its constructor
PS2 sheet virtual table row: ['RReflection virtual table']
constructor 0x994c0 first calls: ['RViewCamera::RViewCamera', 'UMemory::FastAlloc', 'RReflection::ReflPrivateData::ReflPrivateData']

Xbox methods (22):
  0x98210 undefined GetReflectionMapWarpageData(void)
  0x98220 undefined EnableReflectionMapWarpage(undefined1 param_1)
  0x98340 undefined __stdcall Kill(void)
  0x98360 undefined SetReflectiveSpecularStrength(undefined4 param_1, undefined4 param_2)
  0x98380 undefined LightingProps(undefined4 param_1, undefined4 param_2)
  0x98410 undefined GetReflectionData2(undefined4 param_1)
  0x98470 undefined GetReflectionCarPos(void)
  0x98480 undefined TextureWeaponEnvMap(void)
  0x98490 undefined TextureSpecular(void)
  0x984a0 undefined Texture(void)
  0x984b0 undefined GetReflectionMatrix(void)
  0x984c0 undefined Begin(undefined4 param_1)
  0x98520 void __thiscall PrivateSubmitSceneObj(RReflection * this, float distance, RSceneObj * sceneObj)
  0x986a0 undefined DeregisterSceneObj(undefined4 param_1)
  0x98850 undefined SceneObjRender(void)
  0x988a0 undefined ResetSceneObjDistances(void)
  0x993e0 undefined InitPostSim(void)
  0x994c0 undefined RReflection(void)
  0x99590 undefined ~RReflection(void)
  0x99610 undefined SetReflectivity(undefined4 param_1)
  0x99c50 void __stdcall Init(void)
  0x99cc0 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (38):
  0x1c1358 RReflection::InitPostSim
  0x1c14d8 RReflection::Init
  0x1c1518 RReflection::Kill
  0x1c1560 RReflection::RReflection
  0x1c16c8 RReflection::~RReflection
  0x1c1758 RReflection::ReInit
  0x1c1760 RReflection::SetReflectiveSpecularStrength
  0x1c1778 RReflection::LightingProps
  0x1c1828 RReflection::GetReflectionData
  0x1c1840 RReflection::GetReflectionData2
  0x1c18b0 RReflection::Lock
  0x1c1930 RReflection::SetReflectivity
  0x1c1950 RReflection::SetupGifState
  0x1c1a58 RReflection::UseSimpleTexture
  0x1c1a90 RReflection::UseSimpleTexture
  0x1c1ac8 RReflection::TextureWeaponEnvMap
  0x1c1ad8 RReflection::TextureSpecular
  0x1c1ae8 RReflection::Texture
  0x1c1af8 RReflection::GetReflectionState
  0x1c1b08 RReflection::GetReflectionStateGlass
  0x1c1b18 RReflection::SetCamera
  0x1c1e10 RReflection::Begin
  0x1c1ec0 RReflection::Draw
  0x1c2440 RReflection::End
  0x1c2508 RReflection::PrivateSubmitSceneObj
  0x1c25f8 RReflection::DeregisterSceneObj
  0x1c2720 RReflection::SceneObjRender
  0x1c27a8 RReflection::ResetSceneObjDistances
  0x1c2898 RReflection::GetReflectionMapWarpageData
  0x1c28a0 RReflection::GetReflectionCarPos
  0x1c28a8 RReflection::GetReflectionMatrix
  0x1c28b0 RReflection::EnableReflectionMapWarpage
  0x1c2ad8 RReflection::operator_new
  0x1c2af8 RReflection::operator_delete
  0x1c2b18 RReflection::Get
  0x1c2b28 RReflection::SubmitSceneObj
  0x1c2b58 RReflection::DoRender
  0x1c2b60 RReflection::fgThis_global_ctors

Sheet rows:
  RReflection::ReflPrivateData::ReflPrivateData(void)
  RReflection::ReflPrivateData::~ReflPrivateData(void)
  RReflection::InitPostSim(void)
  RReflection::Init(void)
  RReflection::Kill(void)
  RReflection::RReflection(void)
  RReflection::~RReflection(void)
  RReflection::ReInit(void)
  RReflection::SetReflectiveSpecularStrength(int, float)
  RReflection::LightingProps(RReflection::kLightingSurfaceType, c
  RReflection::GetReflectionData(unsigned int)
  RReflection::GetReflectionData2(char *)
  RReflection::Lock(void)
  RReflection::UnLockAll(void)
  RReflection::SetReflectivity(RSceneObj *)
  RReflection::SetupGifState(unsigned int, bool)
  RReflection::UseSimpleTexture(RSceneObj *)
  RReflection::UseSimpleTexture(void)
  RReflection::TextureWeaponEnvMap(void)
  RReflection::TextureSpecular(void)
  RReflection::Texture(void)
  RReflection::GetReflectionState(void)
  RReflection::GetReflectionStateGlass(void)
  RReflection::SetCamera(COORD3 &, COORD3 &)
  RReflection::Begin(void)
  RReflection::Draw(COORD3 &)
  RReflection::End(void)
  RReflection::PrivateSubmitSceneObj(float, RSceneObj *)
  RReflection::DeregisterSceneObj(RSceneObj *)
  RReflection::SceneObjRender(EAGL::ViewPort &)
  RReflection::ResetSceneObjDistances(void)
  RReflection::GetReflectionMapWarpageData(void)
  RReflection::GetReflectionCarPos(void)
  RReflection::GetReflectionMatrix(void)
  RReflection::EnableReflectionMapWarpage(bool)
  RReflection type_info function
  RReflection::operator new(unsigned int)
  RReflection::operator delete(void *, unsigned int)
  RReflection::Get(void)
  RReflection::SubmitSceneObj(float, RSceneObj *)
  RReflection::DoRender(void)
  RReflection::fgThis
  RReflection virtual table
  RReflection type_info node

Xbox methods treated as members (21 of 22; untyped ones count when ECX is read before it is written): Begin, DeregisterSceneObj, EnableReflectionMapWarpage, GetReflectionCarPos, GetReflectionData2, GetReflectionMapWarpageData, GetReflectionMatrix, Init, InitPostSim, LightingProps, PrivateSubmitSceneObj, RReflection, ResetSceneObjDistances, SceneObjRender, SetReflectiveSpecularStrength, SetReflectivity, Texture, TextureSpecular, TextureWeaponEnvMap, scalar_deleting_destructor, ~RReflection

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RReflection@994c0, ~RReflection@99590]
  +0x004  w[4] R [1: Begin@984c0]
  +0x018  w[4] R/W [3: Begin@984c0, RReflection@994c0, ~RReflection@99590]
  +0x03c  w[1] W [1: Begin@984c0]
  +0x04c  w[4] R/W [18: DeregisterSceneObj@986a0, EnableReflectionMapWarpage@98220, GetReflectionCarPos@98470, GetReflectionData2@98410, GetReflectionMapWarpageData@98210, GetReflectionMatrix@984b0…]
  +0x050  w[4] W float [4: DeregisterSceneObj@986a0, PrivateSubmitSceneObj@98520, RReflection@994c0, ResetSceneObjDistances@988a0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R -> RCamera::SetMatrix4 [2: Begin@1c1e10, SetCamera@1c1b18]
  +0x014  w[4] R/W -> EAGL::ViewPort::BeginView, EAGL::ViewPort::SetViewMatrix [5: Draw@1c1ec0, Lock@1c18b0, RReflection@1c1560, SetCamera@1c1b18, ~RReflection@1c16c8]
  +0x038  w[4] W [2: Begin@1c1e10, End@1c2440]
  +0x054  w[4] W [2: RReflection@1c1560, ~RReflection@1c16c8]
  +0x058  w[4] R/W -> PVehicle::NameToIndex, RReflection::ReflPrivateData::~ReflPrivateData, RViewCamera::EndView [23: Begin@1c1e10, DeregisterSceneObj@1c25f8, Draw@1c1ec0, End@1c2440, GetReflectionData2@1c1840, GetReflectionData@1c1828…]
  +0x05c  w[4] R/W float [5: DeregisterSceneObj@1c25f8, PrivateSubmitSceneObj@1c2508, RReflection@1c1560, ResetSceneObjDistances@1c27a8, SubmitSceneObj@1c2b28]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  GetReflectionMapWarpageData: R +0x4c w4
  SetReflectiveSpecularStrength: R +0x4c w4
  GetReflectionCarPos: R +0x4c w4
  TextureWeaponEnvMap: R +0x4c w4
  TextureSpecular: R +0x4c w4
  Texture: R +0x4c w4
  GetReflectionMatrix: R +0x4c w4
