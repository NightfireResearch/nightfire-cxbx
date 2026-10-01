# RLightManager

FastAlloc/constructed sizes under its tag: {'allocated': [], 'constructed': [1008]}
Xbox vtable 0x00191628 (3 slots) stored by its constructor
PS2 sheet virtual table row: ['RLightManager virtual table']
constructor 0x7f010 first calls: ['__builtin_new', '__builtin_new', None]

Xbox methods (14):
  0x7e430 undefined VerticalFalloffMultiplier(void)
  0x7e440 void __thiscall AddPositionalLight(RLightManager * this, int param_1_00, undefined4 * param_2, undefined4 * pa
  0x7e860 undefined DisablePositionalLighting(undefined1 param_1)
  0x7e8c0 undefined ~RLightManager(void)
  0x7e930 undefined __stdcall Kill(void)
  0x7e950 void __thiscall AddSpecularLight(RLightManager * this, int param_1_00, float * param_2)
  0x7eaa0 undefined SetCurrentAmbientAndDiffuse(void)
  0x7eb30 undefined SetSurfaceProperties(undefined4 param_1, undefined4 param_2)
  0x7efb0 undefined RefreshLightBlocks(void)
  0x7f010 undefined RLightManager(void)
  0x7f450 undefined scalar_deleting_destructor(undefined1 param_1)
  0x7f470 void __thiscall SetLightingModel(int param_1_00, int param_2)
  0x7f540 undefined FinishLights(void)
  0x8b620 void __stdcall Init(void)

PS2 methods (42):
  0x1ae630 RLightManager::RLightManager
  0x1aeba8 RLightManager::~RLightManager
  0x1aec20 RLightManager::SetLightingModel
  0x1aedc8 RLightManager::CopyPositionalLightsToPlatformSpecific
  0x1aedd0 RLightManager::SetAmbientPlatformSpecific
  0x1aede0 RLightManager::DisableDirectionalDiffuseToPlatformSpecific
  0x1aede8 RLightManager::FinishLights
  0x1aee60 RLightManager::ClearAllLights
  0x1aefb8 RLightManager::AddSpecularLight
  0x1af200 RLightManager::VerticalFalloffMultiplier
  0x1af210 RLightManager::SetCurrentAmbientAndDiffuse
  0x1af2d0 RLightManager::SetSurfacePropertiesPlatformSpecific
  0x1af2d8 RLightManager::SetSurfaceProperties
  0x1af468 RLightManager::SetAmbientLight
  0x1af510 RLightManager::SetSpecularStrength
  0x1af518 RLightManager::SetDiffuseStrength
  0x1af520 RLightManager::AddPositionalLight
  0x1af638 RLightManager::NumPositionalLightsAffecting
  0x1affd0 RLightManager::DisablePositionalLighting
  0x1b0150 RLightManager::Debug
  0x1b0158 RLightManager::RefreshLightBlocks
  0x1b04b0 RLightManager::Get
  0x1b04c0 RLightManager::Init
  0x1b04f8 RLightManager::Kill
  0x1b0530 RLightManager::WorldLightInfo
  0x1b0538 RLightManager::CharacterLightInfo
  0x1b0540 RLightManager::LightInfo
  0x1b0548 RLightManager::GetCurrentAmbientDiffuse
  0x1b0550 RLightManager::PositionalLightInfo
  0x1b0558 RLightManager::PrimaryLightSource
  0x1b0560 RLightManager::SetPrimaryLightSource
  0x1b0588 RLightManager::SpecularMatrix
  0x1b0590 RLightManager::ForcePositionalLighting
  0x1b0598 RLightManager::NumPositionalLights
  0x1b05a0 RLightManager::HighLevelManager
  0x1b05a8 RLightManager::Reset
  0x1b05b0 RLightManager::GetLightAngles
  0x1b05c8 RLightManager::GetSunHeight
  0x1b05d0 RLightManager::GetMoonSize
  0x1b05d8 RLightManager::LightMapsEnabled
  0x1b05e0 RLightManager::LightMapLightingBias
  0x1b05e8 RLightManager::fgThis_RLightManager_global_ctors

Sheet rows:
  RLightManager::RLightManager(void)
  RLightManager::~RLightManager(void)
  RLightManager::SetLightingModel(RLightManager::LightingModel)
  RLightManager::CopyPositionalLightsToPlatformSpecific(void)
  RLightManager::SetAmbientPlatformSpecific(EAGL::FloatColour &)
  RLightManager::EnableAndCopyDirectionalDiffuseToPlatformSpecifi
  RLightManager::DisableDirectionalDiffuseToPlatformSpecific(void
  RLightManager::FinishLights(void)
  RLightManager::ClearAllLights(void)
  RLightManager::AddDirectionalLight(COORD4 &, COORD4 &)
  RLightManager::AddSpecularLight(COORD4 &)
  RLightManager::VerticalFalloffMultiplier(COORD3 &)
  RLightManager::SetCurrentAmbientAndDiffuse(void)
  RLightManager::SetSurfacePropertiesPlatformSpecific(float, floa
  RLightManager::SetSurfaceProperties(float, float, float)
  RLightManager::SetAmbientLight(COORD4 &, COORD4 &)
  RLightManager::SetSpecularStrength(float)
  RLightManager::SetDiffuseStrength(float)
  RLightManager::AddPositionalLight(COORD4 &, COORD4 &)
  RLightManager::NumPositionalLightsAffecting(COORD4 &)
  RLightManager::DisablePositionalLighting(bool)
  RLightManager::Debug(void)
  RLightManager::RefreshLightBlocks(void)
  RLightManager type_info function
  RLightManager::Get(void)
  RLightManager::Init(void)
  RLightManager::Kill(void)
  RLightManager::WorldLightInfo(void)
  RLightManager::CharacterLightInfo(void)
  RLightManager::LightInfo(void)
  RLightManager::GetCurrentAmbientDiffuse(void)
  RLightManager::PositionalLightInfo(void)
  RLightManager::PrimaryLightSource(void)
  RLightManager::SetPrimaryLightSource(COORD4 &)
  RLightManager::SpecularMatrix(void)
  RLightManager::ForcePositionalLighting(bool)
  RLightManager::NumPositionalLights(void)
  RLightManager::HighLevelManager(void)
  RLightManager::Reset(void)
  RLightManager::GetLightAngles(RLightManager::eLightBlocks)
  RLightManager::GetSunHeight(void)
  RLightManager::GetMoonSize(void)
  RLightManager::LightMapsEnabled(void)
  RLightManager::LightMapLightingBias(void)
  RLightManager::fgThis_RLightManager
  RLightManager virtual table
  RLightManager type_info node

Xbox methods treated as members (13 of 14; untyped ones count when ECX is read before it is written): AddPositionalLight, AddSpecularLight, DisablePositionalLighting, FinishLights, Init, RLightManager, RefreshLightBlocks, SetCurrentAmbientAndDiffuse, SetLightingModel, SetSurfaceProperties, VerticalFalloffMultiplier, scalar_deleting_destructor, ~RLightManager

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] W [2: RLightManager@7f010, ~RLightManager@7e8c0]
  +0x010  w- LEA addr-taken [1: AddSpecularLight@7e950]
  +0x060  w[4] W [2: FinishLights@7f540, RLightManager@7f010]
  +0x064  w[4] R/W [3: AddPositionalLight@7e440, FinishLights@7f540, RLightManager@7f010]
  +0x068  w[4] R/W [3: FinishLights@7f540, RLightManager@7f010, SetCurrentAmbientAndDiffuse@7eaa0]
  +0x06c  w[1] W [1: RLightManager@7f010]
  +0x06d  w[1] W [2: DisablePositionalLighting@7e860, RLightManager@7f010]
  +0x070  w[4] R/W [1: SetLightingModel@7f470]
  +0x074  w[4] R/W [2: SetLightingModel@7f470, SetSurfaceProperties@7eb30]
  +0x078  w[4] R/W -> MEM_copy [6: AddSpecularLight@7e950, RLightManager@7f010, SetCurrentAmbientAndDiffuse@7eaa0, SetLightingModel@7f470, SetSurfaceProperties@7eb30, ~RLightManager@7e8c0]
  +0x07c  w- LEA addr-taken [3: RLightManager@7f010, RefreshLightBlocks@7efb0, SetLightingModel@7f470]
  +0x0ac  w- LEA addr-taken [1: RLightManager@7f010]
  +0x0bc  w- LEA addr-taken [1: RLightManager@7f010]
  +0x0cc  w- LEA addr-taken [1: RLightManager@7f010]
  +0x0dc  w- LEA addr-taken -> dbattrib_floatrgb [1: RLightManager@7f010]
  +0x0ec  w- LEA addr-taken [1: SetLightingModel@7f470]
  +0x0f4  w[4] W [1: RefreshLightBlocks@7efb0]
  +0x104  w[4] W [1: RefreshLightBlocks@7efb0]
  +0x114  w[4] W [1: RefreshLightBlocks@7efb0]
  +0x23c  w- LEA addr-taken [2: RLightManager@7f010, RefreshLightBlocks@7efb0]
  +0x240  w- LEA addr-taken [1: RLightManager@7f010]
  +0x244  w- LEA addr-taken -> dbattrib_float [1: RLightManager@7f010]
  +0x248  w- LEA addr-taken -> dbattrib_float [1: RLightManager@7f010]
  +0x24c  w- LEA addr-taken -> dbattrib_float [1: RLightManager@7f010]
  +0x250  w- LEA addr-taken [1: RLightManager@7f010]
  +0x29c  w[4] R/W [4: AddPositionalLight@7e440, FinishLights@7f540, RLightManager@7f010, ~RLightManager@7e8c0]
  +0x2a0  w[4] R/W -> MEM_copy, MEM_fill, __builtin_delete [4: DisablePositionalLighting@7e860, FinishLights@7f540, RLightManager@7f010, ~RLightManager@7e8c0]
  +0x2a4  w- LEA addr-taken [1: SetCurrentAmbientAndDiffuse@7eaa0]
  +0x2c4  w- LEA addr-taken [1: RLightManager@7f010]
  +0x2c5  w[1] LEA/W addr-taken [1: RLightManager@7f010]
  +0x2c8  w- LEA addr-taken [1: RLightManager@7f010]
  +0x2cc  w- LEA addr-taken [1: RLightManager@7f010]
  +0x2d0  w[4] LEA/W addr-taken [1: RLightManager@7f010]
  +0x2d4  w[4] LEA/W addr-taken [1: RLightManager@7f010]
  +0x2d8  w- LEA addr-taken [1: FinishLights@7f540]
  +0x358  w[4] W [1: RLightManager@7f010]
  +0x3ac  w[4] W [1: RLightManager@7f010]
  +0x3b0  w[4] W [1: RLightManager@7f010]
  +0x3b4  w[4] W [1: RLightManager@7f010]
  +0x3b8  w[4] W [1: RLightManager@7f010]
  +0x3bc  w[4] W [1: RLightManager@7f010]

PS2 this-relative accesses (PS2 offsets):
  +0x000  w[4, 16] W [3: AddSpecularLight@1aefb8, RLightManager@1ae630, ~RLightManager@1aeba8]
  +0x010  w[8, 16] LEA/W addr-taken [3: AddSpecularLight@1aefb8, PrimaryLightSource@1b0558, SetPrimaryLightSource@1b0560]
  +0x018  w[8] W [2: AddSpecularLight@1aefb8, SetPrimaryLightSource@1b0560]
  +0x020  w[16] LEA/W addr-taken [2: AddSpecularLight@1aefb8, SpecularMatrix@1b0588]
  +0x030  w[16] W [1: AddSpecularLight@1aefb8]
  +0x060  w[4] W [3: ClearAllLights@1aee60, FinishLights@1aede8, RLightManager@1ae630]
  +0x064  w[4] R/W [4: AddPositionalLight@1af520, ClearAllLights@1aee60, FinishLights@1aede8, RLightManager@1ae630]
  +0x068  w[4] R/W float [5: FinishLights@1aede8, NumPositionalLights@1b0598, NumPositionalLightsAffecting@1af638, RLightManager@1ae630, SetCurrentAmbientAndDiffuse@1af210]
  +0x06c  w[4] R/W [3: ForcePositionalLighting@1b0590, NumPositionalLightsAffecting@1af638, RLightManager@1ae630]
  +0x070  w[4] R/W [3: DisablePositionalLighting@1affd0, NumPositionalLightsAffecting@1af638, RLightManager@1ae630]
  +0x074  w[4] R/W [1: SetLightingModel@1aec20]
  +0x078  w[4] R/W [2: SetLightingModel@1aec20, SetSurfaceProperties@1af2d8]
  +0x07c  w[4] R/W -> MEM_copy [7: AddSpecularLight@1aefb8, LightInfo@1b0540, RLightManager@1ae630, SetCurrentAmbientAndDiffuse@1af210, SetLightingModel@1aec20, SetSurfaceProperties@1af2d8…]
  +0x080  w- LEA addr-taken [4: RLightManager@1ae630, RefreshLightBlocks@1b0158, SetLightingModel@1aec20, WorldLightInfo@1b0530]
  +0x088  w[4] W float [2: RefreshLightBlocks@1b0158, SetAmbientLight@1af468]
  +0x098  w[4] W float [2: RefreshLightBlocks@1b0158, SetAmbientLight@1af468]
  +0x0a8  w[4] W float [2: RefreshLightBlocks@1b0158, SetAmbientLight@1af468]
  +0x0b0  w- LEA addr-taken [1: RLightManager@1ae630]
  +0x0c0  w- LEA addr-taken [1: RLightManager@1ae630]
  +0x0d0  w[4] LEA/W float addr-taken [2: RLightManager@1ae630, SetAmbientLight@1af468]
  +0x0d4  w[4] W float [1: SetAmbientLight@1af468]
  +0x0d8  w[4] W float [1: SetAmbientLight@1af468]
  +0x0dc  w[4] W float [1: SetAmbientLight@1af468]
  +0x0e0  w[8] LEA/W addr-taken [2: RLightManager@1ae630, SetAmbientLight@1af468]
  +0x0e8  w[8] W [1: SetAmbientLight@1af468]
  +0x0ec  w[4] W float [1: SetSpecularStrength@1af510]
  +0x0f0  w- LEA addr-taken [2: CharacterLightInfo@1b0538, SetLightingModel@1aec20]
  +0x0f8  w[4] W float [1: RefreshLightBlocks@1b0158]
  +0x108  w[4] W float [1: RefreshLightBlocks@1b0158]
  +0x118  w[4] W float [1: RefreshLightBlocks@1b0158]
  +0x240  w- LEA addr-taken [2: RLightManager@1ae630, RefreshLightBlocks@1b0158]
  +0x244  w- LEA addr-taken [1: RLightManager@1ae630]
  +0x248  w- LEA addr-taken [1: RLightManager@1ae630]
  +0x24c  w- LEA addr-taken [1: RLightManager@1ae630]
  +0x250  w- LEA addr-taken [1: RLightManager@1ae630]
  +0x254  w- LEA addr-taken [1: RLightManager@1ae630]
  +0x2a0  w[4] R/W -> MEM_fill, __builtin_delete [5: AddPositionalLight@1af520, ClearAllLights@1aee60, FinishLights@1aede8, RLightManager@1ae630, ~RLightManager@1aeba8]
  +0x2a4  w[4] R/W -> MEM_copy, MEM_fill [6: DisablePositionalLighting@1affd0, FinishLights@1aede8, NumPositionalLightsAffecting@1af638, PositionalLightInfo@1b0550, RLightManager@1ae630, ~RLightManager@1aeba8]
  +0x2a8  w[8] LEA/W addr-taken [2: GetCurrentAmbientDiffuse@1b0548, SetCurrentAmbientAndDiffuse@1af210]
  +0x2b0  w[8] W [1: SetCurrentAmbientAndDiffuse@1af210]
  +0x2b8  w[8] W [1: SetCurrentAmbientAndDiffuse@1af210]
  +0x2c0  w[8] W [1: SetCurrentAmbientAndDiffuse@1af210]
  +0x2c8  w[4] LEA/W addr-taken [1: RLightManager@1ae630]
  +0x2cc  w[4] LEA/R/W addr-taken [2: LightMapsEnabled@1b05d8, RLightManager@1ae630]
  +0x2d0  w[4] LEA/W addr-taken [1: RLightManager@1ae630]
  +0x2d4  w[4] LEA/R/W float addr-taken [2: GetSunHeight@1b05c8, RLightManager@1ae630]
  +0x2d8  w[4] LEA/R/W addr-taken [2: GetMoonSize@1b05d0, RLightManager@1ae630]
  +0x2dc  w[4] LEA/R/W float addr-taken [2: LightMapLightingBias@1b05e0, RLightManager@1ae630]
  +0x2e0  w- LEA addr-taken [4: ClearAllLights@1aee60, FinishLights@1aede8, HighLevelManager@1b05a0, RLightManager@1ae630]
  +0x3c0  w- LEA addr-taken [1: RLightManager@1ae630]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
