# RVehicle

FastAlloc/constructed sizes under its tag: {'allocated': [880], 'constructed': []}
deleting destructor 0x967f0 frees/deletes with size 0x370 (call to UMemory::FastFree)
Xbox vtable 0x001922d0 (19 slots) stored by its constructor
PS2 sheet virtual table row: ['RVehicle virtual table']
constructor 0x953e0 first calls: ['RSkeletalObj::RSkeletalObj', 'UMemory::FastAlloc', 'RVehicleParticle::RVehicleParticle']

Xbox methods (26):
  0x953e0 undefined RVehicle(undefined4 param_1)
  0x95560 undefined ResolveObjectData(undefined4 param_1, undefined4 param_2)
  0x955b0 undefined SetViewDrawList(undefined4 param_1)
  0x955d0 undefined PostLoad(void)
  0x958a0 undefined __stdcall InitSharedBuffers(void)
  0x95970 undefined __stdcall ClearSharedBuffers(void)
  0x959b0 undefined TriggerIlluminate(undefined4 param_1, undefined4 param_2)
  0x959d0 undefined SetEMPVictimEffect(undefined1 param_1)
  0x959f0 undefined TriggerTurboBoostSmoke(void)
  0x95a10 undefined TriggerEmpBolts(void)
  0x95a80 undefined ComputeDamage(void)
  0x95b30 undefined RenderShadow(void)
  0x95b60 undefined RenderFX(undefined4 param_1)
  0x95c60 undefined GetNeedlePos(undefined4 param_1, undefined4 param_2)
  0x95cc0 undefined GetRenderOffset(void)
  0x95ce0 undefined StartMuzzleFlash(undefined4 param_1, undefined4 param_2, undefined4 param_3, undefined4 param_4)
  0x95e20 undefined StartColourDissolve(void)
  0x95ed0 undefined TriggerFX(void)
  0x95ff0 undefined SetPerObjectBuffers(void)
  0x96060 undefined ComputeWheels(void)
  0x96410 undefined RenderShadowGeometry(void)
  0x96430 undefined ~RVehicle(void)
  0x964b0 undefined UpdatePosition(void)
  0x965e0 undefined Render(void)
  0x96770 undefined RenderDeferredEffects(void)
  0x967f0 undefined scalar_deleting_destructor(undefined1 param_1)

PS2 methods (34):
  0x1d2338 RVehicle::RVehicle
  0x1d2508 RVehicle::~RVehicle
  0x1d2588 RVehicle::ResolveObjectData
  0x1d2618 RVehicle::SetViewDrawList
  0x1d2648 RVehicle::PostLoad
  0x1d2910 RVehicle::InitSharedBuffers
  0x1d2a50 RVehicle::ClearSharedBuffers
  0x1d2aa0 RVehicle::TriggerIlluminate
  0x1d2b38 RVehicle::SetPerObjectBuffers
  0x1d2bd0 RVehicle::ComputeDissolve
  0x1d2bd8 RVehicle::SetEMPVictimEffect
  0x1d2bf0 RVehicle::TriggerTurboBoostSmoke
  0x1d2c18 RVehicle::TriggerEmpBolts
  0x1d2c60 RVehicle::SetIntensityEmpBolts
  0x1d2c78 RVehicle::CancelEmpBolts
  0x1d2cb0 RVehicle::ComputeWheels
  0x1d3138 RVehicle::ComputeDamage
  0x1d3228 RVehicle::RenderShadow
  0x1d3278 RVehicle::RenderFX
  0x1d3438 RVehicle::DrawWireframeCollisionBox
  0x1d3440 RVehicle::GetNeedlePos
  0x1d34a8 RVehicle::UpdatePosition
  0x1d35d8 RVehicle::Render
  0x1d37f0 RVehicle::RenderDeferredEffects
  0x1d38a8 RVehicle::GetRenderOffset
  0x1d38d8 RVehicle::StartMuzzleFlash
  0x1d3b10 RVehicle::StartColourDissolve
  0x1d3c28 RVehicle::TriggerFX
  0x1d3d20 RVehicle::RenderShadowGeometry
  0x1d3d48 RVehicle::RenderShadowVolume
  0x1d3e30 RVehicle::Debug
  0x1d4058 RVehicle::operator_new
  0x1d4078 RVehicle::operator_delete
  0x1d40d0 RVehicle::fDoSimpleShadow_global_ctors

Sheet rows:
  RVehicle::RVehicle(PVehicle *)
  RVehicle::~RVehicle(void)
  RVehicle::ResolveObjectData(char *, bool &)
  RVehicle::SetViewDrawList(unsigned int)
  RVehicle::PostLoad(void)
  RVehicle::InitSharedBuffers(void)
  RVehicle::ClearSharedBuffers(void)
  RVehicle::TriggerIlluminate(int, int, int)
  RVehicle::SetPerObjectBuffers(void)
  RVehicle::ComputeDissolve(void)
  RVehicle::SetEMPVictimEffect(bool)
  RVehicle::TriggerTurboBoostSmoke(int)
  RVehicle::TriggerEmpBolts(void)
  RVehicle::SetIntensityEmpBolts(float)
  RVehicle::CancelEmpBolts(void)
  RVehicle::ComputeWheels(void)
  RVehicle::ComputeDamage(void)
  RVehicle::RenderShadow(void)
  RVehicle::RenderFX(MATRIX4 &)
  RVehicle::DrawWireframeCollisionBox(MATRIX4 &)
  RVehicle::GetNeedlePos(int, float)
  RVehicle::UpdatePosition(bool)
  RVehicle::Render(void)
  RVehicle::RenderDeferredEffects(void)
  RVehicle::GetRenderOffset(void) const
  RVehicle::StartMuzzleFlash(COORD3 &, COORD3 &, unsigned int, un
  RVehicle::StartColourDissolve(int, unsigned int)
  RVehicle::TriggerFX(SceneObjFXType, unsigned int, unsigned int,
  RVehicle::RenderShadowGeometry(void)
  RVehicle::RenderShadowVolume(MATRIX4 &, CARP::Instance &)
  RVehicle::Debug(void)
  RVehicle type_info function
  RVehicle::operator new(unsigned int)
  RVehicle::operator delete(void *, unsigned int)
  RVehicle::SetEMPShootingEffect(float)
  RVehicle::SetSimpleShadow(bool)
  RVehicle::SharedDamageZoneBuffer(void)
  RVehicle::SharedSwitchEffectBuffer(void)
  RVehicle::fDoSimpleShadow
  RVehicle virtual table
  RVehicle::fgSharedSwitchEffectBuffer
  RVehicle::fgSharedDamageBuffer
  RVehicle type_info node

Xbox methods treated as members (23 of 26; untyped ones count when ECX is read before it is written): ClearSharedBuffers, ComputeDamage, ComputeWheels, GetRenderOffset, PostLoad, RVehicle, Render, RenderDeferredEffects, RenderFX, RenderShadow, RenderShadowGeometry, ResolveObjectData, SetEMPVictimEffect, SetPerObjectBuffers, SetViewDrawList, StartColourDissolve, StartMuzzleFlash, TriggerEmpBolts, TriggerIlluminate, TriggerTurboBoostSmoke, UpdatePosition, scalar_deleting_destructor, ~RVehicle

Xbox this-relative accesses (offset, widths, R/W, float, addr-taken, pointer hints, methods):
  +0x000  w[4] R/W [3: RVehicle@953e0, Render@965e0, ~RVehicle@96430]
  +0x010  w[4] R [2: GetRenderOffset@95cc0, StartColourDissolve@95e20]
  +0x01d  w[1] R [1: StartColourDissolve@95e20]
  +0x01f  w[1] R/W [1: RVehicle@953e0]
  +0x028  w[4] R [6: ComputeDamage@95a80, ComputeWheels@96060, Render@965e0, RenderDeferredEffects@96770, StartMuzzleFlash@95ce0, UpdatePosition@964b0]
  +0x02c  w[4] R [1: Render@965e0]
  +0x040  w[4] W float [1: UpdatePosition@964b0]
  +0x044  w[4] W float [1: UpdatePosition@964b0]
  +0x048  w[4] W float [1: UpdatePosition@964b0]
  +0x054  w[4] W float [1: UpdatePosition@964b0]
  +0x058  w[4] W float [1: UpdatePosition@964b0]
  +0x0a0  w- LEA addr-taken [1: RVehicle@953e0]
  +0x0e0  w- LEA addr-taken [1: ComputeDamage@95a80]
  +0x0e4  w- LEA addr-taken [1: ComputeDamage@95a80]
  +0x1e0  w- LEA addr-taken [1: RenderFX@95b60]
  +0x20c  w- LEA addr-taken [1: RVehicle@953e0]
  +0x220  w- LEA addr-taken [1: RVehicle@953e0]
  +0x230  w- LEA addr-taken [1: RenderFX@95b60]
  +0x258  w- LEA addr-taken [1: RVehicle@953e0]
  +0x284  w[4] W [1: RVehicle@953e0]
  +0x288  w[4] W [1: RVehicle@953e0]
  +0x294  w[4] W [1: RVehicle@953e0]
  +0x298  w[4] W [1: RVehicle@953e0]
  +0x29c  w[4] W [1: RVehicle@953e0]
  +0x2a0  w[4] W [1: RVehicle@953e0]
  +0x2a4  w[4] W [1: RVehicle@953e0]
  +0x2a8  w- LEA addr-taken [1: RVehicle@953e0]
  +0x2cc  w[4] R -> FUN_00095f90 [1: SetPerObjectBuffers@95ff0]
  +0x328  w[4] W float [2: ComputeWheels@96060, RVehicle@953e0]
  +0x32c  w[4] W float [2: ComputeWheels@96060, RVehicle@953e0]
  +0x330  w[4] W [2: RVehicle@953e0, StartColourDissolve@95e20]
  +0x334  w[4] LEA/R/W addr-taken [3: RVehicle@953e0, Render@965e0, SetViewDrawList@955b0]
  +0x338  w[4] W float [2: ComputeWheels@96060, RVehicle@953e0]
  +0x348  w[4] R/W [3: RVehicle@953e0, Render@965e0, SetViewDrawList@955b0]
  +0x34c  w[4] W [2: RVehicle@953e0, StartColourDissolve@95e20]
  +0x350  w[4] R/W -> EAGL::Model::Draw [3: RVehicle@953e0, RenderShadow@95b30, RenderShadowGeometry@96410]
  +0x354  w[4] W [1: RVehicle@953e0]
  +0x358  w[4] R/W -> RVehicleParticle::UpdateCar [4: RVehicle@953e0, RenderFX@95b60, TriggerTurboBoostSmoke@959f0, ~RVehicle@96430]
  +0x35c  w[4] R/W float -> REmp::DrawShootingEffect [2: RVehicle@953e0, RenderFX@95b60]
  +0x360  w[1] R/W [3: RVehicle@953e0, RenderFX@95b60, SetEMPVictimEffect@959d0]
  +0x364  w[4] W [1: SetEMPVictimEffect@959d0]
  +0x368  w[4] R/W -> FUN_0009d630, REmpBolts::Draw [4: RVehicle@953e0, Render@965e0, TriggerEmpBolts@95a10, ~RVehicle@96430]

PS2 this-relative accesses (PS2 offsets):
  +0x00c  w[4] R [3: GetRenderOffset@1d38a8, PostLoad@1d2648, StartColourDissolve@1d3b10]
  +0x010  w[4] R [1: PostLoad@1d2648]
  +0x018  w[4] R/W [1: RVehicle@1d2338]
  +0x019  w[1] R [1: StartColourDissolve@1d3b10]
  +0x024  w[4] R [7: ComputeDamage@1d3138, ComputeWheels@1d2cb0, PostLoad@1d2648, Render@1d35d8, RenderDeferredEffects@1d37f0, StartMuzzleFlash@1d38d8…]
  +0x028  w[4] R [2: PostLoad@1d2648, Render@1d35d8]
  +0x03c  w[4] R/W [3: RVehicle@1d2338, Render@1d35d8, ~RVehicle@1d2508]
  +0x040  w[4] W float [1: UpdatePosition@1d34a8]
  +0x044  w[4] W float [1: UpdatePosition@1d34a8]
  +0x048  w[4] W float [1: UpdatePosition@1d34a8]
  +0x07c  w[4] R [1: ComputeWheels@1d2cb0]
  +0x0a0  w[16] LEA/W addr-taken [1: RVehicle@1d2338]
  +0x0b0  w[16] W [1: RVehicle@1d2338]
  +0x0c0  w[16] W [1: RVehicle@1d2338]
  +0x0e0  w- LEA addr-taken [3: ComputeDamage@1d3138, ResolveObjectData@1d2588, SetPerObjectBuffers@1d2b38]
  +0x0ec  w- LEA addr-taken [1: ComputeDamage@1d3138]
  +0x1e0  w- LEA addr-taken [1: RenderFX@1d3278]
  +0x1ec  w- LEA addr-taken [1: RVehicle@1d2338]
  +0x200  w- LEA addr-taken [1: RenderFX@1d3278]
  +0x20c  w- LEA addr-taken [1: RVehicle@1d2338]
  +0x220  w- LEA addr-taken [2: RVehicle@1d2338, StartMuzzleFlash@1d38d8]
  +0x228  w- LEA addr-taken [1: RenderFX@1d3278]
  +0x230  w- LEA addr-taken [1: RenderFX@1d3278]
  +0x238  w- LEA addr-taken [2: ComputeWheels@1d2cb0, PostLoad@1d2648]
  +0x248  w- LEA addr-taken [3: ComputeWheels@1d2cb0, PostLoad@1d2648, RVehicle@1d2338]
  +0x258  w- LEA addr-taken [3: ComputeWheels@1d2cb0, PostLoad@1d2648, RVehicle@1d2338]
  +0x268  w- LEA addr-taken [2: RVehicle@1d2338, SetPerObjectBuffers@1d2b38]
  +0x284  w[4] W float [1: RVehicle@1d2338]
  +0x288  w[4] W float [1: RVehicle@1d2338]
  +0x28c  w[4] W float [1: SetPerObjectBuffers@1d2b38]
  +0x290  w[4] W float [1: SetPerObjectBuffers@1d2b38]
  +0x294  w[4] W float [1: RVehicle@1d2338]
  +0x298  w[4] W float [1: RVehicle@1d2338]
  +0x29c  w[4] W float [1: RVehicle@1d2338]
  +0x2a0  w[4] W float [1: RVehicle@1d2338]
  +0x2a4  w[4] W float [1: RVehicle@1d2338]
  +0x2a8  w- LEA addr-taken [1: RVehicle@1d2338]
  +0x2cc  w[4] R [1: SetPerObjectBuffers@1d2b38]
  +0x2d0  w[4] R [1: SetPerObjectBuffers@1d2b38]
  +0x328  w[4] R/W float [2: ComputeWheels@1d2cb0, RVehicle@1d2338]
  +0x32c  w[4] R/W float [2: ComputeWheels@1d2cb0, RVehicle@1d2338]
  +0x330  w[4] W [2: RVehicle@1d2338, StartColourDissolve@1d3b10]
  +0x334  w[4] LEA/R/W addr-taken [3: RVehicle@1d2338, Render@1d35d8, SetViewDrawList@1d2618]
  +0x338  w[4] R/W float [2: ComputeWheels@1d2cb0, RVehicle@1d2338]
  +0x348  w[4] R/W [3: RVehicle@1d2338, Render@1d35d8, SetViewDrawList@1d2618]
  +0x34c  w[4] R/W [3: RVehicle@1d2338, Render@1d35d8, StartColourDissolve@1d3b10]
  +0x350  w[4] R/W -> EAGL::Model::Draw [4: PostLoad@1d2648, RVehicle@1d2338, RenderShadow@1d3228, RenderShadowGeometry@1d3d20]
  +0x354  w[4] R/W -> EAGL::Model::Draw, EAGL::Model::SetModelMatrix [3: PostLoad@1d2648, RVehicle@1d2338, RenderShadowVolume@1d3d48]
  +0x358  w[4] R/W -> RVehicleParticle::TriggerTireSmoke, RVehicleParticle::UpdateCar [4: RVehicle@1d2338, RenderFX@1d3278, TriggerTurboBoostSmoke@1d2bf0, ~RVehicle@1d2508]
  +0x35c  w[4] R/W float [2: RVehicle@1d2338, RenderFX@1d3278]
  +0x360  w[4] R/W [3: RVehicle@1d2338, RenderFX@1d3278, SetEMPVictimEffect@1d2bd8]
  +0x364  w[4] W [1: SetEMPVictimEffect@1d2bd8]
  +0x368  w[4] R/W -> REmpBolts::Draw, REmpBolts::~REmpBolts [5: CancelEmpBolts@1d2c78, RVehicle@1d2338, Render@1d35d8, SetIntensityEmpBolts@1d2c60, TriggerEmpBolts@1d2c18]

Short single-field methods (accessor candidates; check they touch this, not a pointee):
  TriggerTurboBoostSmoke: R +0x358 w4
  RenderShadowGeometry: R +0x350 w4
