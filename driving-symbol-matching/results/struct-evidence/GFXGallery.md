# GFXGallery

FastAlloc/constructed sizes under its tag: None
No vtable stored by its constructor(s) (no vptr => no +4 shift; check subclasses)
PS2 sheet virtual table row: none
constructor 0xd44b0 first calls: ['UMemory::Alloc', 'dummyGetNullValue', 'MEM_fill']

Xbox methods (24):
  0xd38f0 undefined GET_Keys(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined4 
  0xd39d0 undefined GET_FirstIdleKey(undefined4 param_1)
  0xd39f0 undefined GET_EffectPosQuat(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xd3aa0 void __thiscall CULL_Start(GFXGallery * this)
  0xd3ab0 int __thiscall CULL_GetNextCanvas(int param_1_00, int param_2, undefined4 * param_3, undefined1 * param_4, und
  0xd3b30 void __thiscall CULL_SetCanvas(int param_1_00, int param_2, byte param_3)
  0xd3bc0 undefined STICKY_GetNewIndex(undefined4 param_1)
  0xd3c30 undefined STICKY_UpdateAllIndexes(void)
  0xd3ce0 undefined GET_StickyEffectPos(undefined4 param_1, undefined4 param_2)
  0xd3d90 undefined CANVAS_HasStopped(undefined4 param_1)
  0xd3ed0 undefined CANVAS_PurgeSceneObj(undefined4 param_1)
  0xd4010 undefined CANVAS_Purge(undefined4 param_1)
  0xd4030 undefined CANVAS_Suppress(undefined4 param_1)
  0xd4050 undefined CANVAS_SetIntensity(undefined4 param_1, undefined4 param_2)
  0xd4160 undefined GLARE_Draw(undefined param_1, undefined4 param_2, undefined4 param_3)
  0xd43a0 undefined PARTICLE_Add(undefined param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefine
  0xd44b0 undefined GFXGallery(undefined4 param_1)
  0xd45f0 undefined PurgeAllEffects(void)
  0xd47a0 undefined CANVAS_Add(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4, undefined
  0xd4b30 undefined CANVAS_PurgeSceneObjEffects(undefined4 param_1)
  0xd4b90 undefined DEBRIS_Draw(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0xd5170 undefined ~GFXGallery(void)
  0xd51e0 undefined CANVAS_Draw(undefined4 param_1, undefined4 param_2, undefined4 param_3)
  0xd55f0 undefined FRAME_Draw(void)

PS2 methods (40):
  0x233618 GFXGallery::GFXGallery
  0x233760 GFXGallery::~GFXGallery
  0x2337f8 GFXGallery::PurgeAllEffects
  0x2339c8 GFXGallery::DataIndex
  0x2339d8 GFXGallery::Debug
  0x2339e0 GFXGallery::GET_Gallery
  0x2339e8 GFXGallery::GET_Canvas
  0x233aa8 GFXGallery::GET_Name
  0x233b48 GFXGallery::GET_Puppet
  0x233c70 GFXGallery::GET_PuppetAndGroup
  0x233da8 GFXGallery::GET_SceneObj
  0x233dd0 GFXGallery::GET_Key
  0x233df8 GFXGallery::GET_Keys
  0x233f18 GFXGallery::GET_FirstIdleKey
  0x233f88 GFXGallery::GET_EffectPosQuat
  0x234068 GFXGallery::Get_InterpolatedKey
  0x234190 GFXGallery::GET_NumParticleLibrarySystems
  0x234198 GFXGallery::CULL_Start
  0x2341a8 GFXGallery::CULL_GetNextCanvas
  0x234258 GFXGallery::CULL_SetCanvas
  0x234330 GFXGallery::STICKY_ReleaseAllIndexes
  0x234350 GFXGallery::STICKY_GetNewIndex
  0x2343e0 GFXGallery::STICKY_UpdateAllIndexes
  0x2344d0 GFXGallery::STICKY_ReleaseIndex
  0x234508 GFXGallery::GET_StickyEffectPos
  0x2345d8 GFXGallery::FRAME_Draw
  0x234818 GFXGallery::CANVAS_Draw
  0x234d80 GFXGallery::CANVAS_Add
  0x235270 GFXGallery::CANVAS_HasStopped
  0x235490 GFXGallery::CANVAS_PurgeSceneObjEffects
  0x235520 GFXGallery::CANVAS_PurgeSceneObj
  0x2356b0 GFXGallery::CANVAS_Purge
  0x2356d8 GFXGallery::CANVAS_SetCulling
  0x235720 GFXGallery::CANVAS_Suppress
  0x235750 GFXGallery::CANVAS_SetIntensity
  0x235808 GFXGallery::DEBRIS_SetState
  0x235810 GFXGallery::DEBRIS_Draw
  0x235d50 GFXGallery::GLARE_Draw
  0x235ea0 GFXGallery::GLARE_Interpolate
  0x2363b0 GFXGallery::PARTICLE_Add

Sheet rows:
  GFXGallery::GFXGallery(char *)
  GFXGallery::~GFXGallery(void)
  GFXGallery::PurgeAllEffects(void)
  GFXGallery::DataIndex(int, int)
  GFXGallery::Debug(void)
  GFXGallery::GET_Gallery(void)
  GFXGallery::GET_Canvas(UGroup *, char *)
  GFXGallery::GET_Name(UGroup *, UGroup *)
  GFXGallery::GET_Puppet(UGroup *, char *, int, int)
  GFXGallery::GET_PuppetAndGroup(UGroup *, char *, UGroup **, int
  GFXGallery::GET_SceneObj(AnimRef *)
  GFXGallery::GET_Key(void *, int)
  GFXGallery::GET_Keys(void *, int, int, void **, int *, void **,
  GFXGallery::GET_FirstIdleKey(UGroup *)
  GFXGallery::GET_EffectPosQuat(int, COORD4 &, COORD4 &)
  GFXGallery::Get_InterpolatedKey(GFXGallery::Keys *, GFXGallery:
  GFXGallery::GET_NumParticleLibrarySystems(void)
  GFXGallery::CULL_Start(void)
  GFXGallery::CULL_GetNextCanvas(COORD4 &, float &, bool &, float
  GFXGallery::CULL_SetCanvas(int, bool, float)
  GFXGallery::STICKY_ReleaseAllIndexes(void)
  GFXGallery::STICKY_GetNewIndex(int)
  GFXGallery::STICKY_UpdateAllIndexes(void)
  GFXGallery::STICKY_ReleaseIndex(unsigned int)
  GFXGallery::GET_StickyEffectPos(unsigned int, COORD4 &)
  GFXGallery::FRAME_Draw(void)
  GFXGallery::CANVAS_Draw(GFXGallery::Canvas &, COORD4 &, COORD4
  GFXGallery::CANVAS_Add(UGroup *, RSceneObj *, int, COORD4 &, CO
  GFXGallery::CANVAS_HasStopped(GFXGallery::Canvas &)
  GFXGallery::CANVAS_PurgeSceneObjEffects(RSceneObj *)
  GFXGallery::CANVAS_PurgeSceneObj(int)
  GFXGallery::CANVAS_Purge(GiottoHandle *)
  GFXGallery::CANVAS_SetCulling(AnimRef *, bool)
  GFXGallery::CANVAS_Suppress(GiottoHandle *)
  GFXGallery::CANVAS_SetIntensity(GiottoHandle *, float)
  GFXGallery::DEBRIS_SetState(GFXGallery::Keys *)
  GFXGallery::DEBRIS_Draw(GFXGallery::Keys *, COORD4 &, COORD4 &,
  GFXGallery::GLARE_SetState(GFXGallery::Keys *)
  GFXGallery::GLARE_Draw(UGroup *, GFXGallery::Glares *, COORD4 *
  GFXGallery::GLARE_Interpolate(GFXGallery::Glares *, GFXGallery:
  GFXGallery::PARTICLE_Add(UGroup *, GFXGallery::Particles *)
  GFXGallery::PARTICLE_Draw(UGroup *, CARP::ParticleData *, GFXGa
  GFXGallery::SOUND_Play(UGroup *, GFXGallery::Sounds *, ASound *

Xbox methods treated as members (19 of 24; untyped ones count when ECX is read before it is written): CANVAS_Add, CANVAS_Draw, CANVAS_HasStopped, CANVAS_Purge, CANVAS_PurgeSceneObj, CANVAS_PurgeSceneObjEffects, CANVAS_SetIntensity, CANVAS_Suppress, CULL_GetNextCanvas, CULL_SetCanvas, CULL_Start, FRAME_Draw, GET_EffectPosQuat, GET_StickyEffectPos, GFXGallery, PurgeAllEffects, STICKY_GetNewIndex, STICKY_UpdateAllIndexes, ~GFXGallery

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x400  w[4] LEA/W addr-taken [5: GFXGallery@d44b0, PurgeAllEffects@d45f0, STICKY_GetNewIndex@d3bc0, STICKY_UpdateAllIndexes@d3c30, ~GFXGallery@d5170]
  +0x404  w[4] W [2: GFXGallery@d44b0, ~GFXGallery@d5170]
  +0x408  w- LEA addr-taken [3: CANVAS_PurgeSceneObjEffects@d4b30, FRAME_Draw@d55f0, PurgeAllEffects@d45f0]
  +0xc08  w- LEA addr-taken [2: GFXGallery@d44b0, PurgeAllEffects@d45f0]
  +0x1408  w[4] W [1: GFXGallery@d44b0]
  +0x140c  w[4] R/W [2: GFXGallery@d44b0, ~GFXGallery@d5170]
  +0x1410  w[4] R/W [2: GFXGallery@d44b0, ~GFXGallery@d5170]
  +0x1414  w[4] R/W -> MEM_fill [11: CANVAS_PurgeSceneObj@d3ed0, CANVAS_PurgeSceneObjEffects@d4b30, CANVAS_SetIntensity@d4050, CANVAS_Suppress@d4030, CULL_SetCanvas@d3b30, FRAME_Draw@d55f0…]
  +0x1418  w[4] W [1: GFXGallery@d44b0]
  +0x141c  w[4] R/W [6: CANVAS_Add@d47a0, CANVAS_PurgeSceneObjEffects@d4b30, CULL_GetNextCanvas@d3ab0, FRAME_Draw@d55f0, GFXGallery@d44b0, PurgeAllEffects@d45f0]
  +0x1420  w[4] R/W [4: CANVAS_Add@d47a0, FRAME_Draw@d55f0, GFXGallery@d44b0, PurgeAllEffects@d45f0]
  +0x1424  w[4] R/W [3: CULL_GetNextCanvas@d3ab0, CULL_Start@d3aa0, GFXGallery@d44b0]
  +0x1428  w[4] RW/W [2: FRAME_Draw@d55f0, GFXGallery@d44b0]
  +0x142c  w[4] R/RW/W [3: CULL_SetCanvas@d3b30, CULL_Start@d3aa0, GFXGallery@d44b0]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 8] R/W [3: Get_InterpolatedKey@234068, STICKY_GetNewIndex@234350, STICKY_ReleaseAllIndexes@234330]
  +0x008  w[8] W [1: Get_InterpolatedKey@234068]
  +0x00c  w- LEA addr-taken [1: STICKY_GetNewIndex@234350]
  +0x010  w[8] W [1: Get_InterpolatedKey@234068]
  +0x018  w[8] W [1: Get_InterpolatedKey@234068]
  +0x020  w[8] W [1: Get_InterpolatedKey@234068]
  +0x028  w[8] W [1: Get_InterpolatedKey@234068]
  +0x030  w[4] W [1: Get_InterpolatedKey@234068]
  +0x400  w- LEA addr-taken [1: STICKY_UpdateAllIndexes@2343e0]
  +0x408  w- LEA addr-taken [2: CANVAS_PurgeSceneObjEffects@235490, FRAME_Draw@2345d8]
  +0xc08  w- LEA addr-taken [1: PurgeAllEffects@2337f8]
  +0x1404  w- LEA addr-taken [1: GFXGallery@233618]
  +0x1408  w[4] R/W [2: GET_Gallery@2339e0, GFXGallery@233618]
  +0x140c  w[4] R/W -> UMemory::Free [2: GFXGallery@233618, ~GFXGallery@233760]
  +0x1410  w[4] R/W -> MEM_fill, UMemory::Free [2: GFXGallery@233618, ~GFXGallery@233760]
  +0x1414  w[4] R/W -> MEM_fill, UMemory::Free [13: CANVAS_Add@234d80, CANVAS_PurgeSceneObj@235520, CANVAS_PurgeSceneObjEffects@235490, CANVAS_SetCulling@2356d8, CANVAS_SetIntensity@235750, CANVAS_Suppress@235720…]
  +0x1418  w[4] R/W [2: GET_NumParticleLibrarySystems@234190, GFXGallery@233618]
  +0x141c  w[4] R/W [6: CANVAS_Add@234d80, CANVAS_PurgeSceneObjEffects@235490, CULL_GetNextCanvas@2341a8, FRAME_Draw@2345d8, GFXGallery@233618, PurgeAllEffects@2337f8]
  +0x1420  w[4] R/W [4: CANVAS_Add@234d80, FRAME_Draw@2345d8, GFXGallery@233618, PurgeAllEffects@2337f8]
  +0x1424  w[4] R/W [3: CULL_GetNextCanvas@2341a8, CULL_Start@234198, GFXGallery@233618]
  +0x1428  w[4] R/W [2: FRAME_Draw@2345d8, GFXGallery@233618]
  +0x142c  w[4] R/W [3: CULL_SetCanvas@234258, CULL_Start@234198, GFXGallery@233618]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  CANVAS_Suppress: R +0x1414 w4
