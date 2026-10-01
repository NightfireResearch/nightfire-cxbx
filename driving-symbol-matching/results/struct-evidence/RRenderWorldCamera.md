# RRenderWorldCamera

FastAlloc/constructed sizes under its tag: None
deleting destructor 0x8ccf0 frees/deletes with size 0x4c (call to UMemory::FastFree)
Xbox vtable 0x00191b2c (6 slots) stored by its constructor
PS2 sheet virtual table row: ['RRenderWorldCamera virtual table']
constructor 0x8c8e0 first calls: ['RViewCamera::RViewCamera', 'RLensFlareManager::Enable', 'RViewCamera::SetGuardBandSize']

Xbox methods (17):
  0x13020 undefined DrawActorWeapons(undefined4 param_1, undefined4 param_2)
  0x8c8e0 undefined RRenderWorldCamera(undefined4 param_1)
  0x8c950 undefined ~RRenderWorldCamera(void)
  0x8c960 undefined4 * __stdcall LoadAttributes(void)
  0x8c990 undefined PreRender(void)
  0x8c9b0 undefined CullModule(undefined4 param_1, undefined4 param_2)
  0x8ca90 undefined PerformCulling(void)
  0x8cb30 undefined DrawStaticWorldGeometry(undefined4 param_1)
  0x8cb90 undefined DrawFinalStaticWorldGeometry(undefined4 param_1)
  0x8cbf0 undefined DrawPostProcessingEffects(void)
  0x8cc70 undefined DrawVehiclesAndDeferredSceneObjects(void)
  0x8ccf0 undefined scalar_deleting_destructor(undefined1 param_1)
  0x8cd10 undefined __stdcall DrawTyreTracks(void)
  0x8cd60 void __thiscall DrawBulletStreaks(RRenderWorldCamera * this)
  0x8ce50 undefined __stdcall AddPlayerHeadlight(void)
  0x8cf10 undefined DrawEffects(void)
  0x8cfa0 undefined DoRender(void)

PS2 methods (25):
  0x1c5358 RRenderWorldCamera::RRenderWorldCamera
  0x1c53b8 RRenderWorldCamera::~RRenderWorldCamera
  0x1c5410 RRenderWorldCamera::LoadAttributes
  0x1c5458 RRenderWorldCamera::PreRender
  0x1c54a0 RRenderWorldCamera::CullModule
  0x1c55d8 RRenderWorldCamera::DoRender
  0x1c5778 RRenderWorldCamera::PostRender
  0x1c5780 RRenderWorldCamera::PerformCulling
  0x1c5870 RRenderWorldCamera::DrawActors
  0x1c58a0 RRenderWorldCamera::DrawActorWeapons
  0x1c58c8 RRenderWorldCamera::DrawActorsPOV
  0x1c5928 RRenderWorldCamera::DrawEffects
  0x1c59e8 RRenderWorldCamera::DrawTyreTracks
  0x1c5aa8 RRenderWorldCamera::DrawBulletStreaks
  0x1c5c20 RRenderWorldCamera::DrawStaticWorldGeometry
  0x1c5ca8 RRenderWorldCamera::DrawTranslucentStaticWorldGeometry
  0x1c5d08 RRenderWorldCamera::DrawFinalStaticWorldGeometry
  0x1c5d98 RRenderWorldCamera::DrawPostProcessingEffects
  0x1c5e50 RRenderWorldCamera::DrawSceneObjects
  0x1c5eb0 RRenderWorldCamera::DrawVehiclesAndDeferredSceneObjects
  0x1c5f80 RRenderWorldCamera::AddPlayerHeadlight
  0x1c60c0 RRenderWorldCamera::Debug
  0x1c62f0 RRenderWorldCamera::operator_new
  0x1c6310 RRenderWorldCamera::operator_delete
  0x1c6330 RRenderWorldCamera::RRenderWorldCamera_global_ctors

Sheet rows:
  RRenderWorldCamera::RRenderWorldCamera(RCamera *)
  RRenderWorldCamera::~RRenderWorldCamera(void)
  RRenderWorldCamera::LoadAttributes(void)
  RRenderWorldCamera::PreRender(void)
  RRenderWorldCamera::CullModule(int (*)(COORD4 &, float &, bool
  RRenderWorldCamera::DoRender(void)
  RRenderWorldCamera::PostRender(void)
  RRenderWorldCamera::PerformCulling(void)
  RRenderWorldCamera::DrawActors(void)
  RRenderWorldCamera::DrawActorWeapons(void)
  RRenderWorldCamera::DrawActorsPOV(void)
  RRenderWorldCamera::DrawEffects(void)
  RRenderWorldCamera::DrawTyreTracks(void)
  RRenderWorldCamera::DrawBulletStreaks(void)
  RRenderWorldCamera::DrawStaticWorldGeometry(CachedDrawInfo &)
  RRenderWorldCamera::DrawTranslucentStaticWorldGeometry(CachedDr
  RRenderWorldCamera::DrawFinalStaticWorldGeometry(CachedDrawInfo
  RRenderWorldCamera::DrawPostProcessingEffects(void)
  RRenderWorldCamera::DrawSceneObjects(void)
  RRenderWorldCamera::DrawVehiclesAndDeferredSceneObjects(void)
  RRenderWorldCamera::AddPlayerHeadlight(void)
  RRenderWorldCamera::Debug(void)
  RRenderWorldCamera type_info function
  RRenderWorldCamera::operator new(unsigned int)
  RRenderWorldCamera::operator delete(void *, unsigned int)
  RRenderWorldCamera virtual table
  RRenderWorldCamera type_info node

Xbox methods treated as members (10 of 17; untyped ones count when ECX is read before it is written): DoRender, DrawActorWeapons, DrawBulletStreaks, DrawPostProcessingEffects, DrawVehiclesAndDeferredSceneObjects, PerformCulling, PreRender, RRenderWorldCamera, scalar_deleting_destructor, ~RRenderWorldCamera

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: PreRender@8c990, RRenderWorldCamera@8c8e0, ~RRenderWorldCamera@8c950]
  +0x004  w[4] R [2: DrawActorWeapons@13020, PerformCulling@8ca90]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4] R [1: PerformCulling@1c5780]
  +0x00c  w[4] R [1: DrawBulletStreaks@1c5aa8]
  +0x054  w[4] R/W [2: PreRender@1c5458, ~RRenderWorldCamera@1c53b8]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  ~RRenderWorldCamera: W +0x0 w4
